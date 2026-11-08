using RezPad.Protocol;
using Xunit;

namespace RezPad.Protocol.Tests;

public class ExternalIdTests
{
    [Theory]
    [InlineData("Machine A", "machine-a")]
    [InlineData("proxmox-01", "proxmox-01")]
    [InlineData("Çağrı's NAS", "cagri-s-nas")]
    [InlineData("  --Web__Server--  ", "web-server")]
    [InlineData("a-very-long-machine-name-indeed", "a-very-long-ma")]
    [InlineData("服务器", "system")]
    public void SlugIsShortLowercaseAscii(string name, string slug) => Assert.Equal(slug, ExternalIds.Slug(name, []));

    [Fact]
    public void SlugsStayUniqueAndFitTheDeviceIdLength()
    {
        var taken = new HashSet<string>();
        foreach (var name in new[] { "Machine A", "machine-a", "MACHINE A" })
        {
            taken.Add(ExternalIds.Slug(name, taken));
        }
        Assert.Equal(new[] { "machine-a", "machine-a-2", "machine-a-3" }, taken.ToArray());
        var longest = ExternalIds.BeszelPrefix + ExternalIds.Slug("an-extremely-long-name-2", ["an-extremely-l"]) + ".disk_used_pct";
        Assert.True(longest.Length <= 31, longest);
    }
}
