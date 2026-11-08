using System.Text.Json.Nodes;
using RezPad.Protocol;
using Xunit;

namespace RezPad.Protocol.Tests;

/// <summary>Asset names and the RPA1 container; the same values as firmware/test/test_asset.</summary>
public class AssetTests
{
    [Fact]
    public void NameMatchesFirmware()
    {
        Assert.Equal("3f9a0c1d2e4b_118x98_c_1b1f25.rpa", AssetFormat.Name("3f9a0c1d2e4b", 118, 98, false, 0x1B1F25));
        Assert.Equal("ab_16x16_f_000000.rpa", AssetFormat.Name("ab", 16, 16, true, 0));
        Assert.True(AssetFormat.ValidId("3f9a0c1d2e4b"));
        Assert.False(AssetFormat.ValidId("../x"));
        Assert.False(AssetFormat.ValidId("ABC"));
    }

    [Fact]
    public void BuildMatchesFirmwareBytes()
    {
        byte[] expected = [(byte)'R', (byte)'P', (byte)'A', (byte)'1', 2, 0, 3, 0, 2, 0, 0, 0,
            100, 0, 2, 0, 0, 0, (byte)'A', (byte)'B',
            40, 0, 3, 0, 0, 0, (byte)'C', (byte)'D', (byte)'E'];
        var built = AssetFormat.Build(2, 3, [new AssetFormat.Frame(100, "AB"u8.ToArray()), new AssetFormat.Frame(40, "CDE"u8.ToArray())]);
        Assert.Equal(expected, built);
    }

    [Fact]
    public void PlacementsUsePageGridAndTheme()
    {
        var root = JsonNode.Parse("""
            {"schema":1,"grid":{"cols":4,"rows":3},"theme":{"name":"dark"},"pages":[
              {"id":"a","widgets":[{"type":"image","x":0,"y":0,"w":1,"h":1,"asset":"abc"}]},
              {"id":"b","grid":{"cols":2,"rows":2},"theme":{"name":"black"},
               "widgets":[{"type":"image","x":0,"y":0,"w":2,"h":1,"asset":"def","fit":"contain"},{"type":"clock","x":0,"y":1,"w":1,"h":1}]}]}
            """);
        var p = AssetFormat.Placements(root);
        Assert.Equal(2, p.Count);
        Assert.Equal("abc_114x94_c_1b1f25.rpa", p[0].FileName);  // 120-4-2 x 100-4-2
        Assert.Equal("def_474x144_f_0f0f10.rpa", p[1].FileName); // 2*240-4-2 x 150-4-2
        Assert.Equal("pages[1].widgets[0]", p[1].Path);
    }

    [Fact]
    public void TransferPackets()
    {
        var file = new byte[9000];
        new Random(1).NextBytes(file);
        var packets = AssetTransfer.Packets("a_1x1_c_000000.rpa", file).ToList();
        Assert.Equal(MsgType.AssetBegin, packets[0].Type);
        Assert.Equal(5, packets.Count);  // begin, 3 chunks (4096 + 4096 + 808), end
        var begin = ProtocolJson.Deserialize<AssetBeginMsg>(packets[0].Body)!;
        Assert.Equal(9000, begin.Size);
        Assert.Equal(Crc32.Compute(file), begin.Crc32);
    }
}
