using System.Buffers.Binary;
using System.Globalization;
using System.Text;
using System.Text.Json.Nodes;

namespace RezPad.Protocol;

/// <summary>
/// Image assets for <c>image</c> widgets (docs/protocol.md, "Assets"): the file name both sides
/// derive from the config, the RPA1 container, and the messages. Mirrors
/// firmware/lib/protocol/src/asset_format.*; both are tested against the same name and bytes.
/// </summary>
public static class AssetFormat
{
    public const int MaxFileBytes = 1048576;
    public const int MinFrameDelayMs = 50;

    /// <summary>
    /// Keeps <paramref name="count"/> frames spread evenly over the animation; each kept frame also
    /// lasts as long as the dropped ones after it, so the total duration stays the same.
    /// </summary>
    public static List<(int First, int Delay)> Thin(List<(int First, int Delay)> groups, int count)
    {
        if (count >= groups.Count)
        {
            return groups;
        }
        count = Math.Max(1, count);
        var result = new List<(int First, int Delay)>(count);
        for (var k = 0; k < count; k++)
        {
            var from = (int)((long)k * groups.Count / count);
            var to = (int)((long)(k + 1) * groups.Count / count);
            var delay = 0;
            for (var i = from; i < to; i++)
            {
                delay += groups[i].Delay;
            }
            result.Add((groups[from].First, delay));
        }
        return result;
    }

    /// <summary>Card colors of the theme presets (firmware/src/ui/theme.cpp), the background images are composited on.</summary>
    public static readonly IReadOnlyDictionary<string, uint> ThemeCard = new Dictionary<string, uint>
    {
        ["dark"] = 0x1B1F25,
        ["midnight"] = 0x141C33,
        ["black"] = 0x0F0F10,
        ["graphite"] = 0x2E3138,
    };

    public static bool ValidId(string id) =>
        id.Length is > 0 and <= 16 && id.All(c => c is >= '0' and <= '9' or >= 'a' and <= 'z');

    /// <summary><c>&lt;id&gt;_&lt;w&gt;x&lt;h&gt;_&lt;c|f&gt;_&lt;rrggbb&gt;.rpa</c>.</summary>
    public static string Name(string id, int w, int h, bool contain, uint bg) =>
        string.Create(CultureInfo.InvariantCulture, $"{id}_{w}x{h}_{(contain ? 'f' : 'c')}_{bg & 0xFFFFFF:x6}.rpa");

    public sealed record Frame(int DelayMs, byte[] Jpeg);

    /// <summary>RPA1 file: header, then per frame [delay u16][len u32][JPEG].</summary>
    public static byte[] Build(int w, int h, IReadOnlyList<Frame> frames)
    {
        using var ms = new MemoryStream();
        Span<byte> head = stackalloc byte[12];
        Encoding.ASCII.GetBytes("RPA1", head);
        BinaryPrimitives.WriteUInt16LittleEndian(head[4..], (ushort)w);
        BinaryPrimitives.WriteUInt16LittleEndian(head[6..], (ushort)h);
        BinaryPrimitives.WriteUInt16LittleEndian(head[8..], (ushort)frames.Count);
        BinaryPrimitives.WriteUInt16LittleEndian(head[10..], 0);
        ms.Write(head);
        Span<byte> fh = stackalloc byte[6];
        foreach (var f in frames)
        {
            BinaryPrimitives.WriteUInt16LittleEndian(fh, (ushort)Math.Clamp(f.DelayMs, 0, ushort.MaxValue));
            BinaryPrimitives.WriteUInt32LittleEndian(fh[2..], (uint)f.Jpeg.Length);
            ms.Write(fh);
            ms.Write(f.Jpeg);
        }
        return ms.ToArray();
    }

    /// <summary>One image widget of a config: which asset, its box in px and how it is fitted.</summary>
    public sealed record Placement(string Asset, int W, int H, bool Contain, uint Bg, string Path)
    {
        public string FileName => Name(Asset, W, H, Contain, Bg);
    }

    /// <summary>Every image widget in a (valid) config, with the box size and card color the device uses.</summary>
    public static IReadOnlyList<Placement> Placements(JsonNode? root)
    {
        var list = new List<Placement>();
        var cols = (int?)root?["grid"]?["cols"] ?? 4;
        var rows = (int?)root?["grid"]?["rows"] ?? 3;
        var theme = (string?)root?["theme"]?["name"] ?? "dark";
        var pages = root?["pages"] as JsonArray ?? [];
        for (var p = 0; p < pages.Count; p++)
        {
            var page = pages[p];
            var pc = (int?)page?["grid"]?["cols"] ?? cols;
            var pr = (int?)page?["grid"]?["rows"] ?? rows;
            var pt = (string?)page?["theme"]?["name"] ?? theme;
            var bg = ThemeCard.TryGetValue(pt, out var c) ? c : ThemeCard["dark"];
            var widgets = page?["widgets"] as JsonArray ?? [];
            for (var i = 0; i < widgets.Count; i++)
            {
                var w = widgets[i];
                if ((string?)w?["type"] != "image" || (string?)w?["asset"] is not { } asset)
                {
                    continue;
                }
                list.Add(new Placement(asset,
                    ConfigCatalog.CardW((int)w!["w"]!, pc) - 2,
                    ConfigCatalog.CardH((int)w["h"]!, pr) - 2,
                    (string?)w["fit"] == "contain", bg, $"pages[{p}].widgets[{i}]"));
            }
        }
        return list;
    }
}

public sealed record AssetBeginMsg(string Name, int Size, uint Crc32);

public sealed record AssetAckMsg(bool Ok, string Name, string? Error);

public sealed record AssetFileInfo(string Name, long Size);

public sealed record AssetListMsg(IReadOnlyList<AssetFileInfo> Files, long Free);

public static class AssetTransfer
{
    /// <summary>ASSET_BEGIN, ASSET_CHUNKs of up to <see cref="Frame.ChunkSize"/> bytes, ASSET_END.</summary>
    public static IEnumerable<(MsgType Type, byte[] Body)> Packets(string name, byte[] file)
    {
        yield return (MsgType.AssetBegin, ProtocolJson.Serialize(new AssetBeginMsg(name, file.Length, Crc32.Compute(file))));
        for (var offset = 0; offset < file.Length; offset += RezPad.Protocol.Frame.ChunkSize)
        {
            var n = Math.Min(RezPad.Protocol.Frame.ChunkSize, file.Length - offset);
            var body = new byte[4 + n];
            BinaryPrimitives.WriteUInt32LittleEndian(body, (uint)offset);
            file.AsSpan(offset, n).CopyTo(body.AsSpan(4));
            yield return (MsgType.AssetChunk, body);
        }
        yield return (MsgType.AssetEnd, []);
    }
}
