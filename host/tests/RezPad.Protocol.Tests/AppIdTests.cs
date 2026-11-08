using RezPad.Protocol;
using Xunit;

namespace RezPad.Protocol.Tests;

public class AppIdTests
{
    [Theory]
    [InlineData("Spotify.exe", "Spotify", null, true)]
    [InlineData("SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify", "Spotify", "SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify", true)]
    [InlineData("SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify", "Spotify", null, true)]
    [InlineData("Chrome", "chrome", null, true)]
    [InlineData("MSEdge", "msedge", null, true)]
    [InlineData("308046B0AF4A39CB", "firefox", null, true)]
    [InlineData("308046B0AF4A39CB", "chrome", null, false)]
    [InlineData("Spotify.exe", "Discord", null, false)]
    [InlineData("Chrome", "chromedriver", null, false)]
    [InlineData("Microsoft.ZuneMusic_8wekyb3d8bbwe!Microsoft.ZuneMusic", "Microsoft.Media.Player", "Microsoft.ZuneMusic_8wekyb3d8bbwe!Microsoft.ZuneMusic", true)]
    public void MatchesMediaSessionToProcess(string appId, string process, string? aumid, bool expected) =>
        Assert.Equal(expected, AppIds.Matches(appId, process, aumid));

    [Fact]
    public void VolumeOmitsAppWhenUnknown()
    {
        Assert.Equal("""{"level":40,"muted":false}""",
            System.Text.Encoding.UTF8.GetString(ProtocolJson.Serialize(new VolumeMsg(40, false))));
        Assert.Equal("""{"level":40,"muted":false,"app":{"name":"Spotify","level":70,"muted":true}}""",
            System.Text.Encoding.UTF8.GetString(ProtocolJson.Serialize(new VolumeMsg(40, false, new AppVolumeMsg("Spotify", 70, true)))));
    }
}
