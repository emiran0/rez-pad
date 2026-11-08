using RezPad.Host.Config;
using RezPad.Host.Device;
using RezPad.Protocol;

namespace RezPad.Host.Audio;

/// <summary>
/// Sends SPECTRUM frames while the device is connected and the layout has a <c>spectrum</c> widget
/// (docs/protocol.md). Capture runs only then. After 1 s of silence it sends one all-zero frame and
/// pauses until sound returns.
/// </summary>
internal sealed class SpectrumBridge : IDisposable
{
    private const double SilencePauseSeconds = 1.0;

    private readonly SerialLink _link;
    private readonly ConfigService _config;
    private readonly LoopbackCapture _capture;
    private readonly object _lock = new();
    private bool _wanted;
    private DateTime? _silentSince;
    private bool _paused;
    private byte[] _latest = new byte[SpectrumAnalyzer.Bands];
    private long _sent;
    private long _frames;
    private long _sendTicks;

    public SpectrumBridge(SerialLink link, ConfigService config)
    {
        _link = link;
        _config = config;
        _capture = new LoopbackCapture(OnFrame);
        _config.Changed += Update;
        _link.StatusChanged += _ => Update();
        Update();
    }

    /// <summary>The last frame (for the web preview), all zero while capture is off.</summary>
    public byte[] Latest => Volatile.Read(ref _latest);

    public bool Capturing => _capture.Running;

    public long Sent => Interlocked.Read(ref _sent);

    /// <summary>Frames produced by the capture (sent or not).</summary>
    public long Frames => Interlocked.Read(ref _frames);

    /// <summary>Total time spent writing SPECTRUM frames to the link, in ms.</summary>
    public double SendMs => Interlocked.Read(ref _sendTicks) * 1000.0 / System.Diagnostics.Stopwatch.Frequency;

    private void Update()
    {
        lock (_lock)
        {
            var wanted = _link.IsConnected && _config.HasWidget("spectrum");
            if (wanted == _wanted)
            {
                return;
            }
            _wanted = wanted;
            if (wanted)
            {
                _silentSince = null;
                _paused = false;
                _capture.Start();
            }
            else
            {
                _capture.Stop();
                Volatile.Write(ref _latest, new byte[SpectrumAnalyzer.Bands]);
            }
            Log.Info($"spectrum: capture {(wanted ? "on" : "off")}");
        }
    }

    private void OnFrame(byte[] bins)
    {
        Volatile.Write(ref _latest, bins);
        Interlocked.Increment(ref _frames);
        var silent = Array.TrueForAll(bins, b => b == 0);
        var now = DateTime.UtcNow;
        if (silent)
        {
            _silentSince ??= now;
            if (_paused)
            {
                return;
            }
            if ((now - _silentSince.Value).TotalSeconds >= SilencePauseSeconds)
            {
                _paused = true;  // this zero frame is the last one until sound returns
            }
        }
        else
        {
            _silentSince = null;
            _paused = false;
        }
        var t0 = System.Diagnostics.Stopwatch.GetTimestamp();
        if (_link.Send(MsgType.Spectrum, SpectrumAnalyzer.Encode(bins)))
        {
            Interlocked.Increment(ref _sent);
        }
        Interlocked.Add(ref _sendTicks, System.Diagnostics.Stopwatch.GetTimestamp() - t0);
    }

    public void Dispose()
    {
        _config.Changed -= Update;
        _capture.Dispose();
    }
}
