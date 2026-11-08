using RezPad.Host.Config;
using RezPad.Host.Device;
using RezPad.Protocol;

namespace RezPad.Host.Audio;

/// <summary>
/// Sends MIXER (docs/protocol.md) while the device is connected and the layout has a <c>mixer</c>
/// widget: on connect, whenever the list changes (checked every second) and right after a mixer
/// action. Executes the device's <c>mixer.volume</c> / <c>mixer.mute</c> actions.
/// </summary>
internal sealed class MixerBridge : IDisposable
{
    private static readonly TimeSpan Poll = TimeSpan.FromSeconds(1);

    private readonly SerialLink _link;
    private readonly ConfigService _config;
    private readonly Func<int> _step;
    private readonly System.Threading.Timer _timer;
    private readonly object _lock = new();
    private byte[]? _lastSent;
    private int _busy;

    public MixerBridge(SerialLink link, ConfigService config, Func<int> volumeStep)
    {
        _link = link;
        _config = config;
        _step = volumeStep;
        _link.Connected += _ => Task.Run(() => Refresh(force: true));
        _config.Changed += () => Task.Run(() => Refresh(force: true));
        _timer = new System.Threading.Timer(_ => Refresh(force: false), null, Poll, Poll);
    }

    /// <summary>The last list sent (for the web preview), empty while not needed.</summary>
    public IReadOnlyList<MixerAppMsg> Latest { get; private set; } = [];

    /// <summary>Handles ACTION mixer.*; returns false for other actions.</summary>
    public bool Handle(ActionMsg action)
    {
        if (!action.Action.StartsWith("mixer.", StringComparison.Ordinal))
        {
            return false;
        }
        var id = action.Id ?? "";
        var ok = action.Action switch
        {
            "mixer.volume" => MixerService.Step(id, action.Value * _step()),
            "mixer.mute" => MixerService.ToggleMute(id),
            _ => false,
        };
        Log.Info($"mixer: {action.Action} {action.Value} \"{id}\"{(ok ? "" : " (app not found)")}");
        Task.Run(() => Refresh(force: true));
        return true;
    }

    private bool Wanted => _link.IsConnected && _config.HasWidget("mixer");

    private void Refresh(bool force)
    {
        if (!Wanted)
        {
            lock (_lock)
            {
                _lastSent = null;
                Latest = [];
            }
            return;
        }
        if (Interlocked.Exchange(ref _busy, 1) == 1)
        {
            return;
        }
        try
        {
            var apps = MixerService.Snapshot(_config.MixerPins);
            var body = ProtocolJson.Serialize(new MixerMsg(apps));
            lock (_lock)
            {
                Latest = apps;
                if (!force && _lastSent is not null && body.AsSpan().SequenceEqual(_lastSent))
                {
                    return;
                }
                _lastSent = body;
            }
            _link.Send(MsgType.Mixer, body);
        }
        finally
        {
            Volatile.Write(ref _busy, 0);
        }
    }

    public void Dispose() => _timer.Dispose();
}
