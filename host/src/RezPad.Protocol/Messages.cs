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

/// <summary>WEATHER (0x30), see docs/protocol.md "WEATHER body".</summary>
public sealed record WeatherMsg(
    string Loc,
    WeatherUnits Units,
    long Updated,
    WeatherNow Now,
    IReadOnlyList<WeatherHour> Hourly,
    IReadOnlyList<WeatherDay> Daily);

/// <summary><c>Temp</c> c/f, <c>Wind</c> kmh/ms/mph, <c>Pressure</c> hpa/inhg.</summary>
public sealed record WeatherUnits(string Temp, string Wind, string Pressure);

public sealed record WeatherNow(double Temp, double Feels, int Hum, double Wind, double? Press, int Code, bool IsDay);

public sealed record WeatherHour(long T, double Temp, int Code, int Pop, bool IsDay);

public sealed record WeatherDay(string D, double Min, double Max, int Code, int Pop);

/// <summary>VOLUME (0x15). Level 0 to 100.</summary>
/// <summary>VOLUME: master volume, plus the Now Playing app's own volume when it has an audio session.</summary>
public sealed record VolumeMsg(int Level, bool Muted, AppVolumeMsg? App = null);

public sealed record AppVolumeMsg(string Name, int Level, bool Muted);

/// <summary>MIXER (0x16): the Windows volume mixer for mixer widgets.</summary>
public sealed record MixerMsg(IReadOnlyList<MixerAppMsg> Apps);

public sealed record MixerAppMsg(string Id, string Name, int Level, bool Muted, bool Active);

// ---- Device -> Host ----

/// <summary>
/// HELLO_ACK. <c>Serial</c> and <c>Art</c> (album art edge in px the device's layout wants) are
/// optional additions within v1.
/// </summary>
public sealed record HelloAckMsg(int Proto, string FwVer, int W, int H, long PsramFree, uint CfgCrc, int Buttons,
    string? Serial = null, int? Art = null);

/// <summary>ACTION. <c>Id</c> names the macro for action "macro" (added within v1).</summary>
public sealed record ActionMsg(string Action, int Value, string? Id = null);

/// <summary>SENSOR (0xA0): the device's room sensor, metric, null when a reading is missing.</summary>
public sealed record SensorMsg(
    double? TempC,
    double? HumPct,
    double? PressHpa,
    string Trend,
    [property: JsonPropertyName("delta_3h")] double? Delta3h = null);

public sealed record LogMsg(string Lvl, string Msg);

public sealed record ErrorMsg(string Code, string Msg);

public sealed record CmdResultMsg(string Cmd, bool Ok, JsonElement? Data = null);
