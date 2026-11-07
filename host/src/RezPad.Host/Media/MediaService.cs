using System.Collections.Concurrent;
using System.Runtime.InteropServices;
using RezPad.Protocol;
using Windows.Media.Control;
using Session = Windows.Media.Control.GlobalSystemMediaTransportControlsSession;
using SessionManager = Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager;
using Status = Windows.Media.Control.GlobalSystemMediaTransportControlsSessionPlaybackStatus;

namespace RezPad.Host.Media;

/// <summary>Media settings from the config's "media" section (docs/config-schema.md). Defaults until ConfigService exists.</summary>
internal sealed record MediaOptions(IReadOnlyList<string> AppPriority, int ArtSize, int VolumeStep, int SeekStepS)
{
    public static MediaOptions Default { get; } = new(["Spotify", "*"], 200, 2, 5);
}

/// <summary>What is playing in the selected media session. Position is valid at <see cref="SampledAt"/>.</summary>
internal sealed record MediaState(
    string State,
    uint TrackId,
    string Title,
    string Artist,
    string Album,
    string App,
    long PositionMs,
    long DurationMs,
    DateTimeOffset SampledAt,
    bool CanSeek,
    bool CanNext,
    bool CanPrev)
{
    public static MediaState None { get; } = new("none", 0, "", "", "", "", 0, 0, DateTimeOffset.UnixEpoch, false, false, false);

    public long PositionAt(DateTimeOffset now)
    {
        var p = PositionMs;
        if (State == "playing")
        {
            p += (long)(now - SampledAt).TotalMilliseconds;
        }
        return Math.Clamp(p, 0, DurationMs > 0 ? DurationMs : long.MaxValue);
    }

    public NowPlayingMsg ToMessage(DateTimeOffset now) => new(State, TrackId, Title, Artist, Album, App,
        PositionAt(now), DurationMs, now.ToUnixTimeMilliseconds(), CanSeek, CanNext, CanPrev);

    public bool SameExceptPosition(MediaState other) =>
        this with { PositionMs = 0, SampledAt = default } == other with { PositionMs = 0, SampledAt = default };
}

/// <summary>Album art source for a track: raw thumbnail bytes, or null when the track has none.</summary>
internal sealed record ArtState(uint TrackId, byte[]? Source, uint SourceCrc);

/// <summary>
/// Now Playing from the Global System Media Transport Controls (GSMTC): picks a session (app
/// priority, playing first), follows its events and executes media actions on it. GSMTC events
/// arrive on arbitrary threads; they only poke a worker loop that re-reads the state, so bursts
/// of events (track changes fire several) collapse into one update.
/// </summary>
internal sealed class MediaService(MediaOptions options) : IDisposable
{
    // Thumbnails often lag the title on a track change (Spotify sends the old image first).
    private static readonly TimeSpan ArtDebounce = TimeSpan.FromMilliseconds(200);
    // A position jump bigger than this counts as a change (seek); smaller drift is just timing.
    private const long PositionJumpMs = 1500;
    // After our own seek, ignore older positions that some apps still report for a moment.
    private static readonly TimeSpan SeekSettle = TimeSpan.FromMilliseconds(1200);

    private static readonly ConcurrentDictionary<string, string> AppNames = new();

    private readonly CancellationTokenSource _cts = new();
    private readonly SemaphoreSlim _signal = new(0, 1);
    private readonly object _lock = new();
    private readonly List<Session> _subscribed = [];
    private SessionManager? _manager;
    private Session? _session;
    private string? _sessionId;
    private MediaState _current = MediaState.None;
    private ArtState _art = new(0, null, 0);
    private int _dirty;
    private int _sessionsDirty = 1;
    private DateTimeOffset? _artDue;
    private (long Pos, DateTimeOffset At)? _seek;

    /// <summary>Raised on a worker thread when the track, state or controls change, or after a seek.</summary>
    public event Action<MediaState>? Changed;

    /// <summary>Raised on a worker thread when the art for the current track changes.</summary>
    public event Action<ArtState>? ArtChanged;

    public MediaState Current
    {
        get
        {
            lock (_lock)
            {
                return _current;
            }
        }
    }

    public ArtState Art
    {
        get
        {
            lock (_lock)
            {
                return _art;
            }
        }
    }

    public void Start() => _ = Task.Run(RunAsync);

    // ---- actions (called from the link thread; never block it) ----

    public void PlayPause() => Control(s => s.TryTogglePlayPauseAsync().AsTask(), MediaKeyFallback.PlayPause);

    public void Next() => Control(s => s.TrySkipNextAsync().AsTask(), MediaKeyFallback.Next);

    public void Previous() => Control(s => s.TrySkipPreviousAsync().AsTask(), MediaKeyFallback.Previous);

    /// <summary>Seeks by <paramref name="detents"/> times the seek step. Fast detents add up from the last target.</summary>
    public void Seek(int detents)
    {
        Session? session;
        MediaState cur;
        long target;
        var now = DateTimeOffset.UtcNow;
        lock (_lock)
        {
            session = _session;
            cur = _current;
            if (session is null || !cur.CanSeek || detents == 0)
            {
                return;
            }
            var basePos = _seek is { } s && now - s.At < SeekSettle
                ? s.Pos + (cur.State == "playing" ? (long)(now - s.At).TotalMilliseconds : 0)
                : cur.PositionAt(now);
            target = Math.Clamp(basePos + detents * options.SeekStepS * 1000L, 0, Math.Max(0, cur.DurationMs - 1000));
            _seek = (target, now);
            // Show the new position right away; the app confirms through a timeline event.
            _current = cur with { PositionMs = target, SampledAt = now };
        }
        Changed?.Invoke(Current);
        _ = Task.Run(async () =>
        {
            try
            {
                var start = session.GetTimelineProperties().StartTime;
                var ok = await session.TryChangePlaybackPositionAsync((start + TimeSpan.FromMilliseconds(target)).Ticks);
                if (!ok)
                {
                    Log.Info($"media: {cur.App} refused the seek");
                }
            }
            catch (Exception ex) when (ex is COMException or InvalidOperationException)
            {
                Log.Warn($"media: seek failed: {ex.Message}");
            }
        });
    }

    private void Control(Func<Session, Task<bool>> action, Action fallback)
    {
        Session? session;
        lock (_lock)
        {
            session = _session;
        }
        if (session is null)
        {
            // No GSMTC session: apps that only listen to global media keys may still react.
            fallback();
            return;
        }
        _ = Task.Run(async () =>
        {
            try
            {
                await action(session);
            }
            catch (Exception ex) when (ex is COMException or InvalidOperationException)
            {
                Log.Warn($"media: control failed: {ex.Message}");
            }
        });
    }

    // ---- worker ----

    private void Poke(bool art)
    {
        if (art)
        {
            lock (_lock)
            {
                _artDue = DateTimeOffset.UtcNow + ArtDebounce;
            }
        }
        Interlocked.Exchange(ref _dirty, 1);
        try
        {
            _signal.Release();
        }
        catch (SemaphoreFullException)
        {
            // Already signalled.
        }
    }

    private async Task RunAsync()
    {
        var ct = _cts.Token;
        try
        {
            _manager = await SessionManager.RequestAsync();
        }
        catch (Exception ex)
        {
            Log.Error($"media: GSMTC unavailable: {ex.Message}");
            return;
        }
        _manager.SessionsChanged += (_, _) =>
        {
            Interlocked.Exchange(ref _sessionsDirty, 1);
            Poke(art: true);
        };
        _manager.CurrentSessionChanged += (_, _) => Poke(art: true);
        Poke(art: true);

        while (!ct.IsCancellationRequested)
        {
            TimeSpan wait;
            lock (_lock)
            {
                wait = _artDue is { } due ? Max(due - DateTimeOffset.UtcNow, TimeSpan.Zero) : Timeout.InfiniteTimeSpan;
            }
            try
            {
                await _signal.WaitAsync(wait, ct);
            }
            catch (OperationCanceledException)
            {
                return;
            }
            try
            {
                if (Interlocked.Exchange(ref _dirty, 0) == 1)
                {
                    await RefreshAsync();
                }
                bool artNow;
                lock (_lock)
                {
                    artNow = _artDue is { } d && d <= DateTimeOffset.UtcNow;
                    if (artNow)
                    {
                        _artDue = null;
                    }
                }
                if (artNow)
                {
                    await RefreshArtAsync();
                }
            }
            catch (Exception ex)
            {
                // A session can close between any two calls; the next event or poke recovers.
                Log.Warn($"media: refresh failed: {ex.GetType().Name}: {ex.Message}");
            }
        }
    }

    private static TimeSpan Max(TimeSpan a, TimeSpan b) => a > b ? a : b;

    private async Task RefreshAsync()
    {
        var manager = _manager!;
        var sessions = manager.GetSessions();
        if (Interlocked.Exchange(ref _sessionsDirty, 0) == 1)
        {
            Resubscribe(sessions);
        }
        var session = Select(sessions, manager.GetCurrentSession());
        var id = session?.SourceAppUserModelId;
        if (id != _sessionId)
        {
            Log.Info($"media: session {(id is null ? "none" : $"{AppName(id)} ({id})")}");
            _sessionId = id;
        }
        lock (_lock)
        {
            _session = session;
        }
        if (session is null)
        {
            Publish(MediaState.None);
            return;
        }

        var playback = session.GetPlaybackInfo();
        var timeline = session.GetTimelineProperties();
        var props = await session.TryGetMediaPropertiesAsync();
        var now = DateTimeOffset.UtcNow;

        var title = props?.Title ?? "";
        var artist = props?.Artist is { Length: > 0 } a ? a : props?.AlbumArtist ?? "";
        var album = props?.AlbumTitle ?? "";
        var previous = Current;
        var state = playback.PlaybackStatus switch
        {
            Status.Playing => "playing",
            Status.Paused => "paused",
            Status.Stopped => "stopped",
            Status.Closed => "none",
            // Opened/Changing happen during track changes: keep showing what it was doing.
            _ => previous.State is "playing" or "paused" ? previous.State : "paused",
        };

        var duration = (long)(timeline.EndTime - timeline.StartTime).TotalMilliseconds;
        var position = (long)(timeline.Position - timeline.StartTime).TotalMilliseconds;
        // LastUpdatedTime is when Position was valid. Some apps leave it unset; then it is "now".
        var sampledAt = timeline.LastUpdatedTime;
        if (sampledAt.Year < 2000 || sampledAt > now + TimeSpan.FromSeconds(1))
        {
            sampledAt = now;
        }
        if (duration <= 0)
        {
            duration = 0;
        }

        var trackId = TrackId.Compute(title, artist, album);
        lock (_lock)
        {
            // Right after our own seek some apps report the old position once more; keep the target.
            if (_seek is { } s && now - s.At < SeekSettle && trackId == previous.TrackId)
            {
                position = s.Pos;
                sampledAt = s.At;
            }
        }

        var controls = playback.Controls;
        Publish(new MediaState(state, trackId, title, artist, album, AppName(session.SourceAppUserModelId),
            position, duration, sampledAt,
            CanSeek: controls.IsPlaybackPositionEnabled && duration > 0,
            CanNext: controls.IsNextEnabled,
            CanPrev: controls.IsPreviousEnabled));
    }

    private void Publish(MediaState next)
    {
        bool changed;
        bool trackChanged;
        lock (_lock)
        {
            var now = DateTimeOffset.UtcNow;
            changed = !next.SameExceptPosition(_current) ||
                      Math.Abs(next.PositionAt(now) - _current.PositionAt(now)) > PositionJumpMs;
            trackChanged = next.TrackId != _current.TrackId || next.State == "none" != (_current.State == "none");
            _current = next;
            if (trackChanged)
            {
                _seek = null;
                _artDue ??= now + ArtDebounce;
            }
        }
        if (changed)
        {
            Changed?.Invoke(next);
        }
    }

    private async Task RefreshArtAsync()
    {
        Session? session;
        lock (_lock)
        {
            session = _session;
        }
        uint trackId = 0;
        byte[]? source = null;
        if (session is not null)
        {
            var props = await session.TryGetMediaPropertiesAsync();
            if (props is not null)
            {
                var artist = props.Artist is { Length: > 0 } a ? a : props.AlbumArtist ?? "";
                trackId = TrackId.Compute(props.Title ?? "", artist, props.AlbumTitle ?? "");
                source = await ArtProcessor.ReadThumbnailAsync(props.Thumbnail);
            }
        }
        var art = new ArtState(trackId, source, source is null ? 0 : Crc32.Compute(source));
        lock (_lock)
        {
            if (art.TrackId == _art.TrackId && art.SourceCrc == _art.SourceCrc && (art.Source is null) == (_art.Source is null))
            {
                return;
            }
            _art = art;
        }
        ArtChanged?.Invoke(art);
    }

    private void Resubscribe(IReadOnlyList<Session> sessions)
    {
        foreach (var s in _subscribed)
        {
            s.MediaPropertiesChanged -= OnMediaProperties;
            s.PlaybackInfoChanged -= OnPlaybackInfo;
            s.TimelinePropertiesChanged -= OnTimeline;
        }
        _subscribed.Clear();
        foreach (var s in sessions)
        {
            s.MediaPropertiesChanged += OnMediaProperties;
            s.PlaybackInfoChanged += OnPlaybackInfo;
            s.TimelinePropertiesChanged += OnTimeline;
            _subscribed.Add(s);
        }
    }

    private void OnMediaProperties(Session s, MediaPropertiesChangedEventArgs e) => Poke(art: true);

    private void OnPlaybackInfo(Session s, PlaybackInfoChangedEventArgs e) => Poke(art: false);

    private void OnTimeline(Session s, TimelinePropertiesChangedEventArgs e) => Poke(art: false);

    /// <summary>
    /// A playing session wins over paused ones; among equals the app priority list decides, then
    /// Windows' own current session.
    /// </summary>
    private Session? Select(IReadOnlyList<Session> sessions, Session? current)
    {
        Session? best = null;
        var bestScore = int.MaxValue;
        foreach (var s in sessions)
        {
            Status status;
            try
            {
                status = s.GetPlaybackInfo().PlaybackStatus;
            }
            catch (COMException)
            {
                continue;  // closing
            }
            if (status == Status.Closed)
            {
                continue;
            }
            var score = (status == Status.Playing ? 0 : 1000) + Priority(s.SourceAppUserModelId) * 2 +
                        (current is not null && s.SourceAppUserModelId == current.SourceAppUserModelId ? 0 : 1);
            if (score < bestScore)
            {
                best = s;
                bestScore = score;
            }
        }
        return best;
    }

    private int Priority(string appId)
    {
        var name = AppName(appId);
        for (var i = 0; i < options.AppPriority.Count; i++)
        {
            var p = options.AppPriority[i];
            if (p == "*" || name.Contains(p, StringComparison.OrdinalIgnoreCase) ||
                appId.Contains(p, StringComparison.OrdinalIgnoreCase))
            {
                return i;
            }
        }
        return options.AppPriority.Count;
    }

    /// <summary>Friendly app name for a GSMTC source app id, e.g. "Spotify.exe" to "Spotify".</summary>
    public static string AppName(string appId) => AppNames.GetOrAdd(appId, static id =>
    {
        try
        {
            var name = Windows.ApplicationModel.AppInfo.GetFromAppUserModelId(id).DisplayInfo.DisplayName;
            if (!string.IsNullOrWhiteSpace(name))
            {
                return name;
            }
        }
        catch (Exception ex) when (ex is COMException or ArgumentException or FileNotFoundException)
        {
            // Plain Win32 apps often have no AppInfo; fall through to the id itself.
        }
        var known = id.ToLowerInvariant() switch
        {
            "308046b0af4a39cb" => "Firefox",
            "chrome" => "Chrome",
            "msedge" => "Edge",
            _ => null,
        };
        if (known is not null)
        {
            return known;
        }
        var n = id;
        var bang = n.LastIndexOf('!');
        if (bang >= 0)
        {
            n = n[(bang + 1)..];
        }
        n = Path.GetFileNameWithoutExtension(n);
        return n.Length > 0 ? char.ToUpperInvariant(n[0]) + n[1..] : id;
    });

    public void Dispose()
    {
        _cts.Cancel();
        _cts.Dispose();
    }
}
