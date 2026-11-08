using System.Text.Json.Nodes;
using RezPad.Host.Assets;
using RezPad.Host.Config;
using RezPad.Protocol;

namespace RezPad.Host.Device;

/// <summary>
/// Keeps the device's copy of the config in sync (docs/protocol.md): pushes the config when the
/// device reports another CRC on connect, and after every saved change. Also reads the device's
/// copy back (CONFIG_GET) for diagnostics.
/// </summary>
internal sealed class ConfigSync
{
    private static readonly TimeSpan AckTimeout = TimeSpan.FromSeconds(5);
    private static readonly TimeSpan AssetTimeout = TimeSpan.FromSeconds(60);  // 1 MB at about 80 KB/s

    private readonly SerialLink _link;
    private readonly ConfigService _config;
    private readonly AssetStore _assets;
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly ConfigTransfer.Receiver _receiver = new();
    private TaskCompletionSource<ConfigAckMsg>? _ack;
    private TaskCompletionSource<byte[]?>? _data;
    private TaskCompletionSource<AssetAckMsg>? _assetAck;
    private TaskCompletionSource<AssetListMsg?>? _assetList;

    public ConfigSync(SerialLink link, ConfigService config, AssetStore assets)
    {
        _link = link;
        _config = config;
        _assets = assets;
        _link.Connected += ack =>
        {
            DeviceCrc = ack.CfgCrc;
            if (ack.CfgCrc != _config.Crc)
            {
                Log.Info($"config: device has {ack.CfgCrc:X8}, host {_config.Crc:X8}, pushing");
                _ = Task.Run(PushAsync);
            }
        };
        _config.Changed += () =>
        {
            if (_link.IsConnected)
            {
                ChangePush = Task.Run(PushAsync);
            }
        };
    }

    /// <summary>Raised after the device applied a pushed config (CONFIG_ACK ok).</summary>
    public event Action<ConfigAckMsg>? Applied;

    /// <summary>CRC32 of the device's stored config, as last reported (null before the first connect).</summary>
    public uint? DeviceCrc { get; private set; }

    public bool InSync => DeviceCrc == _config.Crc;

    /// <summary>The push started by the last saved change (set before ConfigService.Replace returns).</summary>
    public Task<PushResult>? ChangePush { get; private set; }

    /// <summary>The last push result: the device's CONFIG_ACK, or an error from the host side.</summary>
    public PushResult? Last { get; private set; }

    public sealed record PushResult(bool Ok, string? Error, DateTimeOffset At, long Ms, int ImagesSent = 0);

    /// <summary>What a running push is doing ("Sending image 2 of 3"), null when idle.</summary>
    public string? Progress { get; private set; }

    /// <summary>Sends the current config and waits for CONFIG_ACK. Pushes run one at a time.</summary>
    public async Task<PushResult> PushAsync()
    {
        await _gate.WaitAsync();
        try
        {
            var bytes = _config.Bytes;
            var started = DateTimeOffset.UtcNow;
            var sw = System.Diagnostics.Stopwatch.StartNew();
            // Image files first: the device builds image widgets from them when the config arrives.
            var (imagesSent, assetError) = await SendAssetsAsync(bytes);
            if (assetError is not null)
            {
                return Finish(new PushResult(false, assetError, started, sw.ElapsedMilliseconds, imagesSent));
            }
            Progress = "Sending the config";
            var tcs = new TaskCompletionSource<ConfigAckMsg>(TaskCreationOptions.RunContinuationsAsynchronously);
            Volatile.Write(ref _ack, tcs);
            foreach (var (type, body) in ConfigTransfer.Packets(bytes))
            {
                if (!_link.Send(type, body))
                {
                    return Finish(new PushResult(false, "device not connected", started, sw.ElapsedMilliseconds));
                }
            }
            var done = await Task.WhenAny(tcs.Task, Task.Delay(AckTimeout));
            if (done != tcs.Task)
            {
                return Finish(new PushResult(false, "no CONFIG_ACK from the device within 5 s", started, sw.ElapsedMilliseconds));
            }
            var ack = tcs.Task.Result;
            if (ack.Ok)
            {
                DeviceCrc = ack.Crc32;
                Log.Info($"config: device applied {bytes.Length} bytes (crc {ack.Crc32:X8}) in {sw.ElapsedMilliseconds} ms");
                Applied?.Invoke(ack);
                return Finish(new PushResult(true, null, started, sw.ElapsedMilliseconds, imagesSent));
            }
            return Finish(new PushResult(false, $"device rejected the config: {ack.Error}", started, sw.ElapsedMilliseconds));
        }
        finally
        {
            Volatile.Write(ref _ack, null);
            Progress = null;
            _gate.Release();
        }
    }

    /// <summary>
    /// Plans the frame budget of every image widget (docs/protocol.md, "Assets"), asks the device which
    /// files it has and sends the missing or changed ones. Returns the number sent and an error.
    /// </summary>
    private async Task<(int Sent, string? Error)> SendAssetsAsync(byte[] config)
    {
        var placements = AssetFormat.Placements(JsonNode.Parse(config));
        if (placements.Count == 0)
        {
            return (0, null);
        }
        Progress = "Preparing images";
        var files = new Dictionary<string, byte[]>();
        try
        {
            foreach (var p in placements)
            {
                files.TryAdd(p.FileName, _assets.Variant(p));
            }
        }
        catch (Exception ex) when (ex is InvalidOperationException or IOException or ArgumentException)
        {
            return (0, ex.Message);
        }

        var listTcs = new TaskCompletionSource<AssetListMsg?>(TaskCreationOptions.RunContinuationsAsynchronously);
        Volatile.Write(ref _assetList, listTcs);
        if (!_link.Send(MsgType.AssetList, []))
        {
            return (0, "device not connected");
        }
        var done = await Task.WhenAny(listTcs.Task, Task.Delay(AckTimeout));
        Volatile.Write(ref _assetList, null);
        if (done != listTcs.Task || listTcs.Task.Result is not { } onDevice)
        {
            return (0, "the device did not list its images (firmware too old?)");
        }
        var have = onDevice.Files.ToDictionary(f => f.Name, f => f.Size);
        var missing = files.Where(f => !have.TryGetValue(f.Key, out var size) || size != f.Value.Length).ToList();
        var need = missing.Sum(f => (long)f.Value.Length);
        if (need > onDevice.Free + have.Where(h => !files.ContainsKey(h.Key)).Sum(h => h.Value))
        {
            return (0, $"not enough flash on the device for the images ({need / 1024} KB needed, {onDevice.Free / 1024} KB free)");
        }

        var sent = 0;
        foreach (var (name, file) in missing)
        {
            Progress = $"Sending image {sent + 1} of {missing.Count} ({file.Length / 1024} KB)";
            var ackTcs = new TaskCompletionSource<AssetAckMsg>(TaskCreationOptions.RunContinuationsAsynchronously);
            Volatile.Write(ref _assetAck, ackTcs);
            var sw = System.Diagnostics.Stopwatch.StartNew();
            foreach (var (type, body) in AssetTransfer.Packets(name, file))
            {
                if (!_link.Send(type, body))
                {
                    return (sent, "device not connected");
                }
            }
            var acked = await Task.WhenAny(ackTcs.Task, Task.Delay(AssetTimeout));
            Volatile.Write(ref _assetAck, null);
            if (acked != ackTcs.Task)
            {
                return (sent, $"no answer from the device for image {name}");
            }
            if (!ackTcs.Task.Result.Ok)
            {
                return (sent, $"the device did not take image {name}: {ackTcs.Task.Result.Error}");
            }
            Log.Info($"assets: sent {name} ({file.Length} bytes) in {sw.ElapsedMilliseconds} ms");
            sent++;
        }
        return (sent, null);
    }

    /// <summary>The device's stored config (CONFIG_GET), or null on timeout or a bad transfer.</summary>
    public async Task<byte[]?> ReadDeviceAsync()
    {
        await _gate.WaitAsync();
        try
        {
            var tcs = new TaskCompletionSource<byte[]?>(TaskCreationOptions.RunContinuationsAsynchronously);
            Volatile.Write(ref _data, tcs);
            if (!_link.Send(MsgType.ConfigGet, []))
            {
                return null;
            }
            var done = await Task.WhenAny(tcs.Task, Task.Delay(AckTimeout));
            return done == tcs.Task ? tcs.Task.Result : null;
        }
        finally
        {
            Volatile.Write(ref _data, null);
            _gate.Release();
        }
    }

    /// <summary>Handles CONFIG_* packets from the device (link thread). Returns false for anything else.</summary>
    public bool HandlePacket(Packet packet)
    {
        switch (packet.Type)
        {
            case MsgType.ConfigAck:
                if (ProtocolJson.Deserialize<ConfigAckMsg>(packet.Body) is { } ack)
                {
                    if (Volatile.Read(ref _ack) is { } tcs)
                    {
                        tcs.TrySetResult(ack);
                    }
                    else if (!ack.Ok)
                    {
                        Log.Warn($"config: unexpected CONFIG_ACK error from the device: {ack.Error}");
                    }
                }
                return true;
            case MsgType.ConfigDataBegin:
                if (ProtocolJson.Deserialize<ConfigDataBeginMsg>(packet.Body) is { } begin)
                {
                    _receiver.Begin(begin);
                }
                return true;
            case MsgType.ConfigDataChunk:
                _receiver.Chunk(packet.Body);
                return true;
            case MsgType.AssetAck:
                if (ProtocolJson.Deserialize<AssetAckMsg>(packet.Body) is { } assetAck)
                {
                    Volatile.Read(ref _assetAck)?.TrySetResult(assetAck);
                }
                return true;
            case MsgType.AssetListData:
                Volatile.Read(ref _assetList)?.TrySetResult(ProtocolJson.Deserialize<AssetListMsg>(packet.Body));
                return true;
            case MsgType.ConfigDataEnd:
                Volatile.Read(ref _data)?.TrySetResult(_receiver.End());
                return true;
            default:
                return false;
        }
    }

    private PushResult Finish(PushResult result)
    {
        Last = result;
        if (!result.Ok)
        {
            Log.Warn($"config: push failed: {result.Error}");
        }
        return result;
    }
}
