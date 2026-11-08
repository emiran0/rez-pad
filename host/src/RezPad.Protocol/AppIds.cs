namespace RezPad.Protocol;

/// <summary>Matching media sessions (GSMTC source app ids) to processes, for per-app volume.</summary>
public static class AppIds
{
    /// <summary>
    /// Whether an audio session's process belongs to the GSMTC app id. Ids look like
    /// "Spotify.exe", "SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify" (Store), "Chrome", "MSEdge" or
    /// "308046B0AF4A39CB" (Firefox).
    /// </summary>
    public static bool Matches(string appId, string processName, string? packageAumid)
    {
        if (packageAumid is not null && string.Equals(appId, packageAumid, StringComparison.OrdinalIgnoreCase))
        {
            return true;
        }
        var known = appId.ToLowerInvariant() switch
        {
            "308046b0af4a39cb" => "firefox",
            _ => null,
        };
        if (known is not null)
        {
            return string.Equals(processName, known, StringComparison.OrdinalIgnoreCase);
        }
        var stem = appId;
        var bang = stem.LastIndexOf('!');
        if (bang >= 0)
        {
            stem = stem[(bang + 1)..];
        }
        if (stem.EndsWith(".exe", StringComparison.OrdinalIgnoreCase))
        {
            stem = stem[..^4];
        }
        return string.Equals(stem, processName, StringComparison.OrdinalIgnoreCase);
    }

}
