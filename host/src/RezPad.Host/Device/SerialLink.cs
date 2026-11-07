using RezPad.Protocol;

namespace RezPad.Host.Device;

internal enum DeviceLinkState
{
    Searching,
    Connecting,
    Connected,
    Incompatible,
}

internal sealed record LinkStatus(
    DeviceLinkState State,
    string? Port = null,
    string? Serial = null,
    HelloAckMsg? Device = null,
    string? Error = null);

/// <summary>
/// Owns the connection to the device: discovery, handshake, heartbeat, time sync and reconnect
/// (docs/protocol.md, "Connection rules"). Runs on its own thread; events fire on that thread.
/// </summary>
internal sealed class SerialLink : IDisposable
{
    private static readonly TimeSpan HelloRetry = TimeSpan.FromMilliseconds(500);
    private static readonly TimeSpan SearchInterval = TimeSpan.FromMilliseconds(400);

    private readonly string _hostVersion;
    private readonly CancellationTokenSource _cts = new();
    private readonly object _writeLock = new();
    private Thread? _thread;
    private Win32Serial? _port;
    private ushort _seq;
    private LinkStatus _status = new(DeviceLinkState.Searching);
    private bool _disposed;

    public SerialLink(string hostVersion) => _hostVersion = hostVersion;

    public event Action<LinkStatus>? StatusChanged;
    public event Action<Packet>? PacketReceived;

    /// <summary>Raised on the link thread after HELLO_ACK and TIME_SYNC; handlers push the full state.</summary>
    public event Action<HelloAckMsg>? Connected;

    public LinkStatus Status => _status;
    public bool IsConnected => _status.State == DeviceLinkState.Connected;
    public DecoderStats? Stats { get; private set; }

    public void Start()
    {
        _thread = new Thread(Run) { IsBackground = true, Name = "SerialLink" };
        _thread.Start();
    }

    /// <summary>Sends a message if connected. Safe from any thread.</summary>
    public bool Send(MsgType type, ReadOnlySpan<byte> body)
    {
        lock (_writeLock)
        {
            var port = _port;
            if (port is null)
            {
                return false;
            }
            try
            {
                port.Write(Frame.Build(type, _seq++, body));
                return true;
            }
            catch (Exception ex) when (ex is IOException or ObjectDisposedException)
            {
                return false;
            }
        }
    }

    public bool SendJson<T>(MsgType type, T body) => Send(type, ProtocolJson.Serialize(body));

    private void SetStatus(LinkStatus status)
    {
        if (status == _status)
        {
            return;
        }
        _status = status;
        Log.Info($"link: {status.State}{(status.Port is null ? "" : $" {status.Port}")}" +
                 $"{(status.Device is null ? "" : $" fw {status.Device.FwVer}")}" +
                 $"{(status.Error is null ? "" : $" ({status.Error})")}");
        StatusChanged?.Invoke(status);
    }

    private void Run()
    {
        var ct = _cts.Token;
        var backoff = SearchInterval;
        while (!ct.IsCancellationRequested)
        {
            DeviceLocator.Found? found;
            try
            {
                found = DeviceLocator.Find();
            }
            catch (Exception ex)
            {
                Log.Warn($"device search failed: {ex.Message}");
                found = null;
            }
            if (found is null)
            {
                if (_status.State != DeviceLinkState.Incompatible)
                {
                    SetStatus(new LinkStatus(DeviceLinkState.Searching));
                }
                ct.WaitHandle.WaitOne(SearchInterval);
                continue;
            }

            string reason;
            try
            {
                reason = RunSession(found, ct);
            }
            catch (Exception ex)
            {
                reason = $"unexpected {ex.GetType().Name}: {ex.Message}";
                Log.Error($"link session failed: {ex}");
            }
            if (_status.State != DeviceLinkState.Incompatible)
            {
                SetStatus(new LinkStatus(DeviceLinkState.Searching, Error: reason));
            }
            // Retry quickly: a busy port is usually a previous instance still letting go of it.
            backoff = reason == "port busy" ? TimeSpan.FromMilliseconds(500) : TimeSpan.FromMilliseconds(250);
            ct.WaitHandle.WaitOne(backoff);
        }
    }

    /// <summary>One connection attempt; returns why it ended.</summary>
    private string RunSession(DeviceLocator.Found found, CancellationToken ct)
    {
        Win32Serial port;
        try
        {
            // DTR and RTS on (TinyUSB only sends while DTR is set), 115200 baud, never 1200.
            port = Win32Serial.Open(found.Port);
        }
        catch (UnauthorizedAccessException)
        {
            return "port busy";
        }
        catch (IOException)
        {
            // Windows can keep listing a just-unplugged port for a few seconds; opening it fails.
            // Same reason every time, so the status (and log) stays quiet.
            return "waiting for port";
        }
        SetStatus(new LinkStatus(DeviceLinkState.Connecting, found.Port, found.Serial));

        lock (_writeLock)
        {
            _port = port;
            _seq = 0;
        }
        var decoder = new FrameDecoder();
        var heartbeat = new HeartbeatMonitor();
        var buffer = new byte[4096];
        var helloSent = DateTime.MinValue;
        HelloAckMsg? device = null;
        try
        {
            while (!ct.IsCancellationRequested)
            {
                // Until HELLO_ACK arrives, repeat HELLO (the device may still be booting).
                if (device is null && DateTime.UtcNow - helloSent > HelloRetry)
                {
                    SendJson(MsgType.Hello, new HelloMsg(Frame.ProtocolVersion, _hostVersion));
                    helloSent = DateTime.UtcNow;
                }

                var n = port.Read(buffer);  // returns 0 after Win32Serial.ReadTimeoutMs

                for (var i = 0; i < n; i++)
                {
                    if (!decoder.Push(buffer[i], out var packet))
                    {
                        continue;
                    }
                    heartbeat.OnDeviceAlive();
                    switch (packet.Type)
                    {
                        case MsgType.HelloAck:
                            var ack = ProtocolJson.Deserialize<HelloAckMsg>(packet.Body);
                            if (ack is null)
                            {
                                break;
                            }
                            if (ack.Proto != Frame.ProtocolVersion)
                            {
                                SetStatus(new LinkStatus(DeviceLinkState.Incompatible, found.Port, found.Serial, ack,
                                    $"firmware protocol v{ack.Proto}, host expects v{Frame.ProtocolVersion}: update firmware or host"));
                                return "incompatible";
                            }
                            device = ack;
                            heartbeat.Start();
                            SendJson(MsgType.TimeSync, TimeSyncMsg.Now());
                            SetStatus(new LinkStatus(DeviceLinkState.Connected, found.Port, found.Serial, ack));
                            Connected?.Invoke(ack);
                            break;
                        case MsgType.Error:
                            var err = ProtocolJson.Deserialize<ErrorMsg>(packet.Body);
                            Log.Warn($"device error: {err?.Code} {err?.Msg}");
                            if (err?.Code == "need_hello")
                            {
                                device = null;  // resend HELLO on the next loop
                                helloSent = DateTime.MinValue;
                            }
                            else if (err?.Code == "proto_mismatch")
                            {
                                SetStatus(new LinkStatus(DeviceLinkState.Incompatible, found.Port, found.Serial, null, err.Msg));
                                return "incompatible";
                            }
                            break;
                        case MsgType.Pong:
                            break;
                        default:
                            PacketReceived?.Invoke(packet);
                            break;
                    }
                }

                if (device is not null)
                {
                    if (heartbeat.Lost)
                    {
                        return "no PONG for 3 s";
                    }
                    if (heartbeat.PingDue())
                    {
                        Send(MsgType.Ping, []);
                    }
                    if (heartbeat.TimeSyncDue())
                    {
                        SendJson(MsgType.TimeSync, TimeSyncMsg.Now());
                    }
                }
            }
            return "stopped";
        }
        catch (Exception ex)
        {
            // A yanked or resetting device surfaces as IOException, OperationCanceledException
            // (aborted read), UnauthorizedAccessException or InvalidOperationException depending on
            // timing. Any of them just ends this session; the outer loop reconnects.
            return $"port lost ({ex.GetType().Name})";
        }
        finally
        {
            Stats = decoder.Stats;
            lock (_writeLock)
            {
                _port = null;
            }
            port.Dispose();
        }
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }
        _disposed = true;
        _cts.Cancel();
        _thread?.Join(TimeSpan.FromSeconds(2));
        _cts.Dispose();
    }
}
