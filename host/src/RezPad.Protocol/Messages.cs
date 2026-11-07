using System.Text.Json;
using System.Text.Json.Serialization;

namespace RezPad.Protocol;

/// <summary>JSON message bodies from docs/protocol.md. Field names are snake_case on the wire.</summary>
public static class ProtocolJson
{
    public static readonly JsonSerializerOptions Options = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
        // Raw UTF-8 for non-ASCII text (track titles): the default escapes it as \uXXXX, up to 6x
        // the bytes. The device is the only reader, never a browser, so HTML-safe escaping is moot.
        Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
    };

    public static byte[] Serialize<T>(T body) => JsonSerializer.SerializeToUtf8Bytes(body, Options);

    /// <summary>Returns null when the body is not valid JSON for <typeparamref name="T"/>.</summary>
    public static T? Deserialize<T>(ReadOnlySpan<byte> body) where T : class
    {
        try
        {
            return JsonSerializer.Deserialize<T>(body, Options);
        }
        catch (JsonException)
        {
            return null;
        }
    }
}

// ---- Host -> Device ----

public sealed record HelloMsg(int Proto, string HostVer);

public sealed record TimeSyncMsg(long UnixMs, int TzOffsetMin)
{
    public static TimeSyncMsg Now(TimeProvider? clock = null)
    {
        clock ??= TimeProvider.System;
        var utc = clock.GetUtcNow();
        var offset = clock.LocalTimeZone.GetUtcOffset(utc);
        return new TimeSyncMsg(utc.ToUnixTimeMilliseconds(), (int)offset.TotalMinutes);
    }
}

public sealed record CmdMsg(string Cmd, JsonElement? Args = null);

/// <summary>NOW_PLAYING (0x10). <c>State</c> is playing, paused, stopped or none.</summary>
public sealed record NowPlayingMsg(
    string State,
    uint TrackId,
    string Title,
    string Artist,
    string Album,
    string App,
    long PosMs,
    long DurMs,
    long Ts,
    bool CanSeek,
    bool CanNext,
    bool CanPrev)
{
    public static NowPlayingMsg None(long ts) => new("none", 0, "", "", "", "", 0, 0, ts, false, false, false);
}

/// <summary>ART_BEGIN (0x11).</summary>
public sealed record ArtBeginMsg(uint Id, int W, int H, string Fmt, int Size, uint Crc32);

/// <summary>ART_END (0x13), ART_NONE (0x14) and ART_REQUEST (0x91).</summary>
public sealed record ArtIdMsg(uint Id);

/// <summary>
/// SYS_STATS (0x20). <c>T</c> is the host unix time in ms, <c>V</c> maps source ids to values;
/// null means the source exists in the layout but has no value on this PC (no driver, no admin).
/// </summary>
public sealed record SysStatsMsg(long T, IReadOnlyDictionary<string, double?> V);

/// <summary>VOLUME (0x15). Level 0 to 100.</summary>
public sealed record VolumeMsg(int Level, bool Muted);

// ---- Device -> Host ----

/// <summary>
/// HELLO_ACK. <c>Serial</c> and <c>Art</c> (album art edge in px the device's layout wants) are
/// optional additions within v1.
/// </summary>
public sealed record HelloAckMsg(int Proto, string FwVer, int W, int H, long PsramFree, uint CfgCrc, int Buttons,
    string? Serial = null, int? Art = null);

public sealed record ActionMsg(string Action, int Value);

public sealed record SensorMsg(double TempC, double HumPct, double PressHpa, string Trend);

public sealed record LogMsg(string Lvl, string Msg);

public sealed record ErrorMsg(string Code, string Msg);

public sealed record CmdResultMsg(string Cmd, bool Ok, JsonElement? Data = null);
