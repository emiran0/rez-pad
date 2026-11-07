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

// ---- Device -> Host ----

public sealed record HelloAckMsg(int Proto, string FwVer, int W, int H, long PsramFree, uint CfgCrc, int Buttons,
    string? Serial = null);

public sealed record ActionMsg(string Action, int Value);

public sealed record SensorMsg(double TempC, double HumPct, double PressHpa, string Trend);

public sealed record LogMsg(string Lvl, string Msg);

public sealed record ErrorMsg(string Code, string Msg);

public sealed record CmdResultMsg(string Cmd, bool Ok, JsonElement? Data = null);
