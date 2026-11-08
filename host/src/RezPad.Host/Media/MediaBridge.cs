using System.Diagnostics;
using RezPad.Host.Audio;
using RezPad.Host.Device;
using RezPad.Protocol;

namespace RezPad.Host.Media;

/// <summary>
/// Connects the media and audio services to the device: sends NOW_PLAYING, album art and VOLUME
/// (on connect, on change, NOW_PLAYING also every 5 s) and executes the device's media ACTIONs.
/// </summary>
internal sealed class MediaBridge : IDisposable
{
    private static readonly TimeSpan Resync = TimeSpan.FromSeconds(5);

    private readonly SerialLink _link;
    private readonly MediaService _media;
    private readonly AudioService _audio;
    private readonly Func<MediaOptions> _options;
    private readonly SemaphoreSlim _artGate = new(1, 1);
    private readonly System.Threading.Timer _resync;
    private (uint Id, uint Crc)? _deviceArt;  // what the device has; Crc 0 = ART_NONE
    private int _artSize;
    private long _trackChangedAt;
    private static readonly TimeSpan AppPoll = TimeSpan.FromSeconds(1);
    private readonly System.Threading.Timer _appPoll;
    private readonly object _appLock = new();
    private AppVolumeState? _appVolume;
    private int _appPolling;
    private uint _lastTrack;  // Stopwatch timestamp of the last track change, for the art latency log

    public MediaBridge(SerialLink link, MediaService media, AudioService audio, Func<MediaOptions> options)
    {
        _link = link;
        _media = media;
        _audio = audio;
        _options = options;
        _artSize = options().ArtSize;
        _resync = new System.Threading.Timer(_ => SendNowPlaying(), null, Resync, Resync);

        _link.Connected += ack => Task.Run(() => OnConnectedAsync(ack));
        _media.Changed += OnMediaChanged;
        _media.ArtChanged += _ => Task.Run(() => SendArtAsync(force: false));
        _audio.Changed += _ => SendVolume();
        // The mixer has no change events we subscribe to; poll the media app's volume.
        _appPoll = new System.Threading.Timer(_ => PollAppVolume(), null, AppPoll, AppPoll);
    }

    /// <summary>Handles device packets that belong to media. Returns false for anything else.</summary>
    public bool HandlePacket(Packet packet)
    {
        switch (packet.Type)
        {
            case MsgType.Action:
                var action = ProtocolJson.Deserialize<ActionMsg>(packet.Body);
                if (action is not null)
                {
                    Execute(action.Action, action.Value);
                }
                return true;
            case MsgType.ArtRequest:
                var req = ProtocolJson.Deserialize<ArtIdMsg>(packet.Body);
                Log.Info($"device asked for art {req?.Id:X8}");
                _ = Task.Run(() => SendArtAsync(force: true));
                return true;
            default:
                return false;
        }
    }

    private void Execute(string action, int value)
    {
        switch (action)
        {
            case "media.play_pause": _media.PlayPause(); break;
            case "media.next": _media.Next(); break;
            case "media.prev": _media.Previous(); break;
            case "media.seek": _media.Seek(value); break;
            case "media.volume": _audio.Step(value * _options().VolumeStep); break;
            case "media.mute": _audio.ToggleMute(); break;
            case "media.app_volume":
                SetAppVolume(StepApp(value * _options().VolumeStep));
                break;
            case "media.app_mute":
                SetAppVolume(StepApp(0, mute: true));
                break;
            default:
                Log.Info($"action {action} {value} not handled by the host");
                return;
        }
        Log.Info($"action {action} {value}");
    }

    private async Task OnConnectedAsync(HelloAckMsg ack)
    {
        // The device may have lost its art (reset, reflash); start over. Not forced: with _deviceArt
        // cleared it is sent anyway, and a send already queued by a media change is not repeated.
        _deviceArt = null;
        _artSize = Math.Clamp(ack.Art ?? _options().ArtSize, 32, 480);
        SendNowPlaying();
        await SendArtAsync(force: false);
        SendVolume();
    }

    /// <summary>A new config is on the device; its layout may want another album art size.</summary>
    public void OnConfigApplied(ConfigAckMsg ack)
    {
        if (ack.Art is not { } art || art <= 0 || art == _artSize)
        {
            return;
        }
        _artSize = Math.Clamp(art, 32, 480);
        Log.Info($"album art size now {_artSize}px");
        _ = Task.Run(() => SendArtAsync(force: true));
    }

    private void OnMediaChanged(MediaState state)
    {
        if (state.TrackId != _lastTrack)
        {
            _lastTrack = state.TrackId;
            Interlocked.Exchange(ref _trackChangedAt, Stopwatch.GetTimestamp());
        }
        Log.Info($"media: {state.State} \"{state.Title}\" - {state.Artist} [{state.App}] " +
                 $"{state.PositionAt(DateTimeOffset.UtcNow) / 1000}/{state.DurationMs / 1000} s");
        SendNowPlaying();
        // The art for this track may already be known (it arrives separately, often first).
        _ = Task.Run(() => SendArtAsync(force: false));
    }

    private void SendNowPlaying()
    {
        if (_link.IsConnected)
        {
            _link.SendJson(MsgType.NowPlaying, _media.Current.ToMessage(DateTimeOffset.UtcNow));
        }
    }

    private void SendVolume()
    {
        var v = _audio.State;
        if (v.Available && _link.IsConnected)
        {
            AppVolumeState? app;
            lock (_appLock)
            {
                app = _appVolume;
            }
            _link.SendJson(MsgType.Volume, new VolumeMsg(v.Level, v.Muted,
                app is { } a ? new AppVolumeMsg(a.Name, a.Level, a.Muted) : null));
        }
    }

    // ---- per-app volume (the Now Playing app) ----

    private AppVolumeState? StepApp(int delta, bool mute = false)
    {
        var id = _media.SessionAppId;
        if (id is null)
        {
            Log.Info("app volume: no media app");
            return null;
        }
        var name = MediaService.AppName(id);
        var result = mute ? AppVolume.ToggleMute(id, name) : AppVolume.Step(id, name, delta);
        if (result is null)
        {
            Log.Info($"app volume: {name} has no audio session");
        }
        return result;
    }

    private void SetAppVolume(AppVolumeState? state)
    {
        lock (_appLock)
        {
            if (Equals(_appVolume, state))
            {
                return;
            }
            _appVolume = state;
        }
        SendVolume();
    }

    private void PollAppVolume()
    {
        if (!_link.IsConnected || Interlocked.Exchange(ref _appPolling, 1) == 1)
        {
            return;
        }
        try
        {
            var id = _media.SessionAppId;
            SetAppVolume(id is null ? null : AppVolume.Get(id, MediaService.AppName(id)));
        }
        finally
        {
            Volatile.Write(ref _appPolling, 0);
        }
    }

    private async Task SendArtAsync(bool force)
    {
        await _artGate.WaitAsync();
        try
        {
            var current = _media.Current;
            var art = _media.Art;
            if (!_link.IsConnected || current.State == "none" || art.TrackId != current.TrackId)
            {
                return;  // nothing to show, or the art belongs to another track (the matching one follows)
            }
            var id = art.TrackId;
            var jpeg = art.Source is null ? null : await ArtProcessor.ToJpegAsync(art.Source, _artSize);
            if (jpeg is null)
            {
                if (force || _deviceArt != (id, 0u))
                {
                    _link.SendJson(MsgType.ArtNone, new ArtIdMsg(id));
                    _deviceArt = (id, 0u);
                }
                return;
            }
            var crc = Crc32.Compute(jpeg);
            if (!force && _deviceArt == (id, crc))
            {
                return;
            }
            var sw = Stopwatch.StartNew();
            foreach (var (type, body) in ArtTransfer.Packets(id, _artSize, _artSize, jpeg))
            {
                if (!_link.Send(type, body))
                {
                    _deviceArt = null;
                    return;
                }
            }
            _deviceArt = (id, crc);
            var since = Interlocked.Exchange(ref _trackChangedAt, 0);
            Log.Info($"art {id:X8}: {jpeg.Length} bytes {_artSize}px sent in {sw.ElapsedMilliseconds} ms" +
                     (since == 0 ? "" : $", {Stopwatch.GetElapsedTime(since).TotalMilliseconds:F0} ms after the track change"));
        }
        catch (Exception ex)
        {
            Log.Warn($"art: {ex.GetType().Name}: {ex.Message}");
        }
        finally
        {
            _artGate.Release();
        }
    }

    public void Dispose()
    {
        _resync.Dispose();
        _appPoll.Dispose();
    }
}
