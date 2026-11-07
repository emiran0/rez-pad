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

    private readonly SerialLink _link;
    private readonly ConfigService _config;
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly ConfigTransfer.Receiver _receiver = new();
    private TaskCompletionSource<ConfigAckMsg>? _ack;
    private TaskCompletionSource<byte[]?>? _data;

    public ConfigSync(SerialLink link, ConfigService config)
    {
        _link = link;
        _config = config;
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

    public sealed record PushResult(bool Ok, string? Error, DateTimeOffset At, long Ms);

    /// <summary>Sends the current config and waits for CONFIG_ACK. Pushes run one at a time.</summary>
    public async Task<PushResult> PushAsync()
    {
        await _gate.WaitAsync();
        try
        {
            var bytes = _config.Bytes;
            var started = DateTimeOffset.UtcNow;
            var sw = System.Diagnostics.Stopwatch.StartNew();
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
                return Finish(new PushResult(true, null, started, sw.ElapsedMilliseconds));
            }
            return Finish(new PushResult(false, $"device rejected the config: {ack.Error}", started, sw.ElapsedMilliseconds));
        }
        finally
        {
            Volatile.Write(ref _ack, null);
            _gate.Release();
        }
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
