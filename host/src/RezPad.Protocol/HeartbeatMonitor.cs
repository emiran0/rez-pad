namespace RezPad.Protocol;

/// <summary>
/// Host-side heartbeat (docs/protocol.md, "Connection rules"): PING every second, the device is
/// lost after 3 s without a PONG. Pure logic with an injectable clock so it can be unit-tested.
/// </summary>
public sealed class HeartbeatMonitor(TimeProvider? clock = null)
{
    public static readonly TimeSpan PingInterval = TimeSpan.FromSeconds(1);
    public static readonly TimeSpan Timeout = TimeSpan.FromSeconds(3);
    public static readonly TimeSpan TimeSyncInterval = TimeSpan.FromSeconds(60);

    private readonly TimeProvider _clock = clock ?? TimeProvider.System;
    private long _lastPong;
    private long _lastPing;
    private long _lastTimeSync;
    private bool _started;

    /// <summary>Call right after the handshake succeeded.</summary>
    public void Start()
    {
        var now = _clock.GetTimestamp();
        _lastPong = now;
        _lastPing = now;
        _lastTimeSync = now;
        _started = true;
    }

    public void Stop() => _started = false;

    /// <summary>Any packet from the device proves it is alive.</summary>
    public void OnDeviceAlive() => _lastPong = _clock.GetTimestamp();

    public bool PingDue()
    {
        if (!_started || _clock.GetElapsedTime(_lastPing) < PingInterval)
        {
            return false;
        }
        _lastPing = _clock.GetTimestamp();
        return true;
    }

    public bool TimeSyncDue()
    {
        if (!_started || _clock.GetElapsedTime(_lastTimeSync) < TimeSyncInterval)
        {
            return false;
        }
        _lastTimeSync = _clock.GetTimestamp();
        return true;
    }

    public bool Lost => _started && _clock.GetElapsedTime(_lastPong) > Timeout;
}
