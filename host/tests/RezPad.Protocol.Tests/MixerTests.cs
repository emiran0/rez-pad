using System.Text;
using RezPad.Protocol;
using Xunit;

namespace RezPad.Protocol.Tests;

public class MixerTests
{
    [Fact]
    public void MixerUsesWireFieldNames()
    {
        var msg = new MixerMsg([new MixerAppMsg("spotify", "Spotify", 80, false, true), new MixerAppMsg("system", "System sounds", 100, true, false)]);
        Assert.Equal("""{"apps":[{"id":"spotify","name":"Spotify","level":80,"muted":false,"active":true},{"id":"system","name":"System sounds","level":100,"muted":true,"active":false}]}""",
            Encoding.UTF8.GetString(ProtocolJson.Serialize(msg)));
    }

    [Fact]
    public void MixerActionCarriesTheAppId()
    {
        Assert.Equal("""{"action":"mixer.volume","value":-2,"id":"chrome"}""",
            Encoding.UTF8.GetString(ProtocolJson.Serialize(new ActionMsg("mixer.volume", -2, "chrome"))));
    }
}

public class MixerPinTests
{
    [Fact]
    public void PinsComeFromEveryMixerWidgetInOrderWithoutRepeats()
    {
        var root = System.Text.Json.Nodes.JsonNode.Parse("""
            {"pages": [
              {"widgets": [{"type": "mixer", "apps": ["spotify", "discord"]}, {"type": "clock"}]},
              {"widgets": [{"type": "mixer", "apps": ["discord", "brave"]}, {"type": "mixer"}]}
            ]}
            """);
        Assert.Equal(["spotify", "discord", "brave"], ConfigCatalog.MixerPins(root));
    }
}
