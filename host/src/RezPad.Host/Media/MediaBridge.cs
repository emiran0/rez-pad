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
    private readonly MediaOptions _options;
    private readonly SemaphoreSlim _artGate = new(1, 1);
    private readonly System.Threading.Timer _resync;
    private (uint Id, uint Crc)? _deviceArt;  // what the device has; Crc 0 = ART_NONE
    private int _artSize;
    private long _trackChangedAt;
    private uint _lastTrack;  // Stopwatch timestamp of the last track change, for the art latency log

    public MediaBridge(SerialLink link, MediaService media, AudioService audio, MediaOptions options)
    {
        _link = link;
        _media = media;
        _audio = audio;
        _options = options;
        _artSize = options.ArtSize;
        _resync = new System.Threading.Timer(_ => SendNowPlaying(), null, Resync, Resync);

        _link.Connected += ack => Task.Run(() => OnConnectedAsync(ack));
        _media.Changed += OnMediaChanged;
        _media.ArtChanged += _ => Task.Run(() => SendArtAsync(force: false));
        _audio.Changed += _ => SendVolume();
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
            case "media.volume": _audio.Step(value * _options.VolumeStep); break;
            case "media.mute": _audio.ToggleMute(); break;
            default:
                Log.Info($"action {action} {value} not handled by the host");
                return;
        }
        Log.Info($"action {action} {value}");
    }

    private async Task OnConnectedAsync(HelloAckMsg ack)
    {
        // The device may have lost its art (reset, reflash); start over.
        _deviceArt = null;
        _artSize = Math.Clamp(ack.Art ?? _options.ArtSize, 32, 480);
        SendNowPlaying();
        await SendArtAsync(force: true);
        SendVolume();
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
            _link.SendJson(MsgType.Volume, new VolumeMsg(v.Level, v.Muted));
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

    public void Dispose() => _resync.Dispose();
}
