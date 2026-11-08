using RezPad.Protocol;
using Xunit;

namespace RezPad.Protocol.Tests;

/// <summary>Key shortcut parsing; the HID values match firmware/test/test_config test_parse_keys.</summary>
public class KeyTests
{
    [Theory]
    [InlineData("ctrl+shift+m", 0x03, 0x10)]
    [InlineData("F13", 0x00, 0x68)]
    [InlineData("win+altgr+e", 0x48, 0x08)]
    [InlineData("alt+f4", 0x04, 0x3D)]
    [InlineData("0", 0x00, 0x27)]
    [InlineData("num0", 0x00, 0x62)]
    [InlineData("f24", 0x00, 0x73)]
    public void ParsesLikeFirmware(string s, int mods, int usage)
    {
        var p = KeyCombo.Parse(s);
        Assert.NotNull(p);
        Assert.Equal(mods, p.Mods);
        Assert.Equal(usage, p.Key!.Usage);
    }

    [Theory]
    [InlineData("")]
    [InlineData("f25")]
    [InlineData("ctrl+")]
    [InlineData("a+b")]
    [InlineData("hyper+x")]
    public void RejectsLikeFirmware(string s) => Assert.Null(KeyCombo.Parse(s));

    [Fact]
    public void ModifierOnlyAndVirtualKeys()
    {
        var p = KeyCombo.Parse("ctrl+shift")!;
        Assert.Null(p.Key);
        Assert.Equal([0xA2, 0xA0], p.ModifierVks);
        Assert.Equal(0x4D, KeyCombo.Parse("m")!.Key!.Vk);       // 'M'
        Assert.Equal(0x7C, KeyCombo.Parse("f13")!.Key!.Vk);     // VK_F13
        Assert.True(KeyCombo.Parse("up")!.Key!.Extended);
    }
}
