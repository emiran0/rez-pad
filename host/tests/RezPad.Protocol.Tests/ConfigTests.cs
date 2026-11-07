using System.Text.Json;
using System.Text.Json.Nodes;
using RezPad.Protocol;
using Xunit;

namespace RezPad.Protocol.Tests;

/// <summary>Config validation against docs/config-test-vectors.json, shared with the firmware tests.</summary>
public class ConfigTests
{
    private static string Doc(string name) => File.ReadAllText(Path.Combine(AppContext.BaseDirectory, name));

    private static JsonObject Default() => JsonNode.Parse(Doc("default-config.json"))!.AsObject();

    public static TheoryData<string> CaseNames()
    {
        var data = new TheoryData<string>();
        foreach (var c in JsonNode.Parse(Doc("config-test-vectors.json"))!["cases"]!.AsArray())
        {
            data.Add(c!["name"]!.GetValue<string>());
        }
        return data;
    }

    private static void Apply(JsonNode root, JsonArray patch)
    {
        foreach (var op in patch)
        {
            var tokens = op!["path"]!.GetValue<string>().Split('/')[1..];
            var parent = root;
            foreach (var t in tokens[..^1])
            {
                parent = parent is JsonArray a ? a[int.Parse(t)]! : parent[t]!;
            }
            var last = tokens[^1];
            var remove = op["op"]!.GetValue<string>() == "remove";
            var value = op["value"]?.DeepClone();
            if (parent is JsonArray arr)
            {
                if (remove)
                {
                    arr.RemoveAt(int.Parse(last));
                }
                else if (last == "-")
                {
                    arr.Add(value);
                }
                else
                {
                    arr[int.Parse(last)] = value;
                }
            }
            else if (remove)
            {
                parent.AsObject().Remove(last);
            }
            else
            {
                parent[last] = value;
            }
        }
    }

    [Theory]
    [MemberData(nameof(CaseNames))]
    public void SharedVector(string name)
    {
        var c = JsonNode.Parse(Doc("config-test-vectors.json"))!["cases"]!.AsArray()
            .First(x => x!["name"]!.GetValue<string>() == name)!;
        var root = Default();
        Apply(root, c["patch"]!.AsArray());
        // Round-trip through bytes, like a config from the web editor.
        var error = ConfigValidator.Validate(ConfigTransfer.ToBytes(root));
        var where = c["where"]?.GetValue<string>();
        if (where is null)
        {
            Assert.Null(error);
        }
        else
        {
            Assert.NotNull(error);
            Assert.StartsWith(where, error);
        }
        // The device ignores host-only sections ("device" overrides "where" for the firmware).
        var deviceWhere = c.AsObject().ContainsKey("device") ? c["device"]?.GetValue<string>() : where;
        var deviceError = ConfigValidator.Validate(root, hostSections: false);
        if (deviceWhere is null)
        {
            Assert.Null(deviceError);
        }
        else
        {
            Assert.StartsWith(deviceWhere, deviceError);
        }
    }

    [Fact]
    public void InvalidJsonAndTooLarge()
    {
        Assert.StartsWith("config: invalid JSON", ConfigValidator.Validate("{\"schema\":1,"u8));
        Assert.StartsWith("config: must be a JSON object", ConfigValidator.Validate("[1]"u8));
        Assert.StartsWith("config: too large", ConfigValidator.Validate(new byte[ConfigCatalog.MaxConfigBytes + 1]));
    }

    [Fact]
    public void ReferencedSourcesOfDefault()
    {
        Assert.Equal(
            ["sys.cpu.load", "sys.cpu.temp", "sys.gpu.load", "sys.gpu.temp", "sys.ram.used_pct", "sys.net.down_kbps", "sys.net.up_kbps"],
            ConfigCatalog.ReferencedSources(Default()));
    }

    [Fact]
    public void BytesAreCompactUtf8()
    {
        var root = new JsonObject { ["name"] = "Çay şehir", ["n"] = 1 };
        Assert.Equal("{\"name\":\"Çay şehir\",\"n\":1}"u8.ToArray(), ConfigTransfer.ToBytes(root));
    }

    [Fact]
    public void TransferRoundTrip()
    {
        var bytes = new byte[10000];
        new Random(3).NextBytes(bytes);
        var packets = ConfigTransfer.Packets(bytes).ToList();
        Assert.Equal(MsgType.ConfigBegin, packets[0].Type);
        Assert.Equal(MsgType.ConfigEnd, packets[^1].Type);
        Assert.Empty(packets[^1].Body);
        var begin = ProtocolJson.Deserialize<ConfigBeginMsg>(packets[0].Body)!;
        Assert.Equal(bytes.Length, begin.Size);

        // The device side replies with the same scheme (CONFIG_DATA_*).
        var rx = new ConfigTransfer.Receiver();
        rx.Begin(new ConfigDataBeginMsg(begin.Size, begin.Crc32));
        foreach (var (_, body) in packets.Skip(1).SkipLast(1))
        {
            rx.Chunk(body);
        }
        Assert.Equal(bytes, rx.End());

        rx.Begin(new ConfigDataBeginMsg(begin.Size, begin.Crc32));
        rx.Chunk(packets[2].Body);  // out of order
        Assert.Null(rx.End());
    }

    [Fact]
    public void AckParsesWithAndWithoutArt()
    {
        var ack = ProtocolJson.Deserialize<ConfigAckMsg>("{\"ok\":true,\"crc32\":4294967295,\"error\":\"\",\"art\":192}"u8)!;
        Assert.True(ack.Ok);
        Assert.Equal(uint.MaxValue, ack.Crc32);
        Assert.Equal(192, ack.Art);
        var nack = ProtocolJson.Deserialize<ConfigAckMsg>("{\"ok\":false,\"crc32\":1,\"error\":\"pages: x\"}"u8)!;
        Assert.False(nack.Ok);
        Assert.Null(nack.Art);
    }
}
