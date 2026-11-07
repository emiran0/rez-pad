using System.Runtime.InteropServices;

namespace RezPad.Host.Audio;

internal readonly record struct VolumeState(int Level, bool Muted, bool Available);

/// <summary>
/// Master volume and mute of the default playback device through Core Audio. Follows default
/// device changes and reports every change (ours, the volume flyout, the keyboard, other apps).
/// All COM calls run on MTA threads (thread pool); the UI thread is STA and never touches it.
/// </summary>
internal sealed class AudioService : IMMNotificationClient, IAudioEndpointVolumeCallback, IDisposable
{
    private const int ClsctxAll = 0x17;
    private static Guid _iidEndpointVolume = typeof(IAudioEndpointVolume).GUID;

    private readonly object _lock = new();
    private IMMDeviceEnumerator? _enumerator;
    private IAudioEndpointVolume? _endpoint;
    private Guid _context = Guid.NewGuid();
    private VolumeState _state;
    private bool _disposed;

    /// <summary>Raised on a COM/thread-pool thread whenever level or mute changes.</summary>
    public event Action<VolumeState>? Changed;

    public VolumeState State
    {
        get
        {
            lock (_lock)
            {
                return _state;
            }
        }
    }

    public void Start() => Task.Run(() =>
    {
        try
        {
            _enumerator = (IMMDeviceEnumerator)new MMDeviceEnumeratorComObject();
            Marshal.ThrowExceptionForHR(_enumerator.RegisterEndpointNotificationCallback(this));
            AttachDefault();
        }
        catch (Exception ex) when (ex is COMException or InvalidCastException)
        {
            Log.Error($"audio: Core Audio unavailable: {ex.Message}");
        }
    }).Wait();

    /// <summary>Changes the volume by <paramref name="delta"/> percent; turning up also unmutes (like the volume keys).</summary>
    public void Step(int delta)
    {
        lock (_lock)
        {
            if (_endpoint is null || delta == 0)
            {
                return;
            }
            var level = Math.Clamp(_state.Level + delta, 0, 100);
            _endpoint.SetMasterVolumeLevelScalar(level / 100f, ref _context);
            if (delta > 0 && _state.Muted)
            {
                _endpoint.SetMute(false, ref _context);
            }
            // Keep the new value right away so fast detents add up instead of reading a stale level.
            Set(new VolumeState(level, delta > 0 ? false : _state.Muted, true));
        }
    }

    public void ToggleMute()
    {
        lock (_lock)
        {
            if (_endpoint is null)
            {
                return;
            }
            _endpoint.SetMute(!_state.Muted, ref _context);
            Set(_state with { Muted = !_state.Muted });
        }
    }

    // Under _lock. Raises Changed outside the lock.
    private void Set(VolumeState s)
    {
        if (s == _state)
        {
            return;
        }
        _state = s;
        var handler = Changed;
        if (handler is not null)
        {
            ThreadPool.QueueUserWorkItem(_ => handler(s));
        }
    }

    private void AttachDefault()
    {
        lock (_lock)
        {
            if (_disposed || _enumerator is null)
            {
                return;
            }
            Detach();
            if (_enumerator.GetDefaultAudioEndpoint(EDataFlow.Render, ERole.Multimedia, out var device) != 0 || device is null)
            {
                Log.Warn("audio: no default playback device");
                Set(default);
                return;
            }
            Marshal.ThrowExceptionForHR(device.Activate(ref _iidEndpointVolume, ClsctxAll, IntPtr.Zero, out var obj));
            _endpoint = (IAudioEndpointVolume)obj!;
            Marshal.ThrowExceptionForHR(_endpoint.RegisterControlChangeNotify(this));
            _endpoint.GetMasterVolumeLevelScalar(out var level);
            _endpoint.GetMute(out var muted);
            Set(new VolumeState(ToPercent(level), muted, true));
            Log.Info($"audio: default device volume {_state.Level}{(muted ? " (muted)" : "")}");
        }
    }

    private void Detach()
    {
        if (_endpoint is null)
        {
            return;
        }
        try
        {
            _endpoint.UnregisterControlChangeNotify(this);
        }
        catch (COMException)
        {
            // The device may already be gone.
        }
        Marshal.ReleaseComObject(_endpoint);
        _endpoint = null;
    }

    private static int ToPercent(float scalar) => (int)Math.Round(Math.Clamp(scalar, 0f, 1f) * 100f);

    // ---- IAudioEndpointVolumeCallback ----

    int IAudioEndpointVolumeCallback.OnNotify(IntPtr data)
    {
        if (data == IntPtr.Zero)
        {
            return 0;
        }
        var muted = Marshal.ReadInt32(data, 16) != 0;
        var level = BitConverter.Int32BitsToSingle(Marshal.ReadInt32(data, 20));
        lock (_lock)
        {
            if (_endpoint is not null)
            {
                Set(new VolumeState(ToPercent(level), muted, true));
            }
        }
        return 0;
    }

    // ---- IMMNotificationClient ----

    int IMMNotificationClient.OnDefaultDeviceChanged(EDataFlow flow, ERole role, string? defaultDeviceId)
    {
        if (flow == EDataFlow.Render && role == ERole.Multimedia)
        {
            // Never re-register from inside a Core Audio callback.
            Task.Run(() =>
            {
                try
                {
                    AttachDefault();
                }
                catch (COMException ex)
                {
                    Log.Warn($"audio: could not attach to the new default device: {ex.Message}");
                }
            });
        }
        return 0;
    }

    int IMMNotificationClient.OnDeviceStateChanged(string deviceId, int newState) => 0;

    int IMMNotificationClient.OnDeviceAdded(string deviceId) => 0;

    int IMMNotificationClient.OnDeviceRemoved(string deviceId) => 0;

    int IMMNotificationClient.OnPropertyValueChanged(string deviceId, PropertyKey key) => 0;

    public void Dispose() => Task.Run(DisposeCore).Wait();  // on an MTA thread, like every other COM call

    private void DisposeCore()
    {
        lock (_lock)
        {
            if (_disposed)
            {
                return;
            }
            _disposed = true;
            Detach();
            if (_enumerator is not null)
            {
                _enumerator.UnregisterEndpointNotificationCallback(this);
                Marshal.ReleaseComObject(_enumerator);
                _enumerator = null;
            }
        }
    }
}
