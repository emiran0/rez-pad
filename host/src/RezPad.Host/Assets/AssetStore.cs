using System.Collections.Concurrent;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json;
using RezPad.Protocol;

namespace RezPad.Host.Assets;

/// <summary>An uploaded image as the web editor lists it.</summary>
internal sealed record AssetInfo(string Id, string Name, int W, int H, int Frames, long Bytes, string Format, DateTimeOffset Uploaded);

/// <summary>
/// Uploaded images and animations for <c>image</c> widgets, kept as the original files in
/// %APPDATA%\rez-pad\assets (docs/protocol.md, "Assets"). Converts them into the per-widget RPA1
/// files the device shows: resized to the widget box with GDI+ (System.Drawing, part of the
/// Windows desktop runtime; it composites GIF frames correctly), JPEG frames.
/// </summary>
internal sealed class AssetStore
{
    public const long MaxUploadBytes = 20L * 1024 * 1024;

    private static readonly JsonSerializerOptions Json = new() { WriteIndented = true };
    private readonly string _dir;
    private readonly ConcurrentDictionary<string, byte[]> _variants = new();  // file name + frame budget -> bytes

    public AssetStore(string? dir = null)
    {
        _dir = dir ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "rez-pad", "assets");
        Directory.CreateDirectory(_dir);
    }

    public IReadOnlyList<AssetInfo> List() =>
        Directory.EnumerateFiles(_dir, "*.json")
            .Select(f =>
            {
                try
                {
                    return JsonSerializer.Deserialize<AssetInfo>(File.ReadAllText(f));
                }
                catch (Exception ex) when (ex is IOException or JsonException)
                {
                    return null;
                }
            })
            .OfType<AssetInfo>()
            .OrderByDescending(a => a.Uploaded)
            .ToList();

    public bool Exists(string id) => AssetFormat.ValidId(id) && File.Exists(MetaPath(id));

    /// <summary>The original file and its content type, or null.</summary>
    public (string Path, string ContentType)? Original(string id)
    {
        if (!Exists(id))
        {
            return null;
        }
        var info = JsonSerializer.Deserialize<AssetInfo>(File.ReadAllText(MetaPath(id)))!;
        var path = Path.Combine(_dir, $"{id}.{info.Format}");
        return File.Exists(path) ? (path, ContentType(info.Format)) : null;
    }

    /// <summary>Stores an upload. Throws ArgumentException for anything GDI+ cannot read as an image.</summary>
    public AssetInfo Add(byte[] data, string fileName)
    {
        if (data.Length == 0 || data.Length > MaxUploadBytes)
        {
            throw new ArgumentException($"images up to {MaxUploadBytes / 1024 / 1024} MB");
        }
        string format;
        int w, h, frames;
        try
        {
            using var ms = new MemoryStream(data);
            using var img = Image.FromStream(ms);
            format = Format(img.RawFormat) ?? throw new ArgumentException("use PNG, JPEG, GIF or BMP");
            w = img.Width;
            h = img.Height;
            frames = img.FrameDimensionsList.Contains(FrameDimension.Time.Guid) ? img.GetFrameCount(FrameDimension.Time) : 1;
        }
        catch (Exception ex) when (ex is ArgumentException or ExternalException or OutOfMemoryException)
        {
            // GDI+ reports unreadable files as OutOfMemoryException or ArgumentException.
            throw new ArgumentException(ex is ArgumentException a && a.Message.StartsWith("use ", StringComparison.Ordinal)
                ? a.Message : "not an image this host can read (PNG, JPEG, GIF or BMP)");
        }
        var id = Convert.ToHexString(SHA256.HashData(data))[..12].ToLowerInvariant();
        File.WriteAllBytes(Path.Combine(_dir, $"{id}.{format}"), data);
        var info = new AssetInfo(id, Path.GetFileName(fileName), w, h, frames, data.Length, format, DateTimeOffset.UtcNow);
        File.WriteAllText(MetaPath(id), JsonSerializer.Serialize(info, Json));
        Log.Info($"assets: stored {id} \"{info.Name}\" {w}x{h}, {frames} frame(s), {data.Length} bytes");
        return info;
    }

    public bool Delete(string id)
    {
        if (!Exists(id))
        {
            return false;
        }
        foreach (var f in Directory.EnumerateFiles(_dir, $"{id}.*"))
        {
            File.Delete(f);
        }
        foreach (var k in _variants.Keys.Where(k => k.StartsWith(id + "_", StringComparison.Ordinal)))
        {
            _variants.TryRemove(k, out _);
        }
        return true;
    }

    /// <summary>Frames left after merging those shorter than the minimum delay (the natural frame count).</summary>
    public int PlayableFrames(string id)
    {
        if (Original(id) is not { } o)
        {
            return 0;
        }
        using var img = Image.FromFile(o.Path);
        return MergeShortFrames(Delays(img)).Count;
    }

    /// <summary>The RPA1 file for one widget box, with at most <paramref name="maxFrames"/> frames.</summary>
    public byte[] Variant(AssetFormat.Placement p, int maxFrames)
    {
        var key = $"{p.FileName}#{maxFrames}";
        return _variants.GetOrAdd(key, _ => Render(p, maxFrames));
    }

    private byte[] Render(AssetFormat.Placement p, int maxFrames)
    {
        var o = Original(p.Asset) ?? throw new InvalidOperationException($"no image {p.Asset}");
        using var img = Image.FromFile(o.Path);
        var groups = MergeShortFrames(Delays(img));
        // Drop frames evenly to fit the budget, keeping the total duration.
        while (groups.Count > Math.Max(1, maxFrames))
        {
            var merged = new List<(int First, int Delay)>();
            for (var i = 0; i < groups.Count; i += 2)
            {
                merged.Add(i + 1 < groups.Count ? (groups[i].First, groups[i].Delay + groups[i + 1].Delay) : groups[i]);
            }
            groups = merged;
        }

        // Output size: the whole box for cover, the fitted image for contain.
        var scale = p.Contain ? Math.Min((double)p.W / img.Width, (double)p.H / img.Height)
            : Math.Max((double)p.W / img.Width, (double)p.H / img.Height);
        var outW = p.Contain ? Math.Clamp((int)Math.Round(img.Width * scale), 1, p.W) : p.W;
        var outH = p.Contain ? Math.Clamp((int)Math.Round(img.Height * scale), 1, p.H) : p.H;
        var drawW = (float)(img.Width * scale);
        var drawH = (float)(img.Height * scale);
        var bg = Color.FromArgb(255, (int)(p.Bg >> 16) & 0xFF, (int)(p.Bg >> 8) & 0xFF, (int)p.Bg & 0xFF);
        var animated = img.FrameDimensionsList.Contains(FrameDimension.Time.Guid);

        for (var quality = 85L; ; quality -= 15)
        {
            var frames = new List<AssetFormat.Frame>();
            foreach (var (first, delay) in groups)
            {
                if (animated)
                {
                    img.SelectActiveFrame(FrameDimension.Time, first);
                }
                using var bmp = new Bitmap(outW, outH, PixelFormat.Format24bppRgb);
                using (var g = Graphics.FromImage(bmp))
                {
                    g.Clear(bg);
                    g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                    g.PixelOffsetMode = PixelOffsetMode.HighQuality;
                    g.CompositingQuality = CompositingQuality.HighQuality;
                    using var attrs = new ImageAttributes();
                    attrs.SetWrapMode(WrapMode.TileFlipXY);  // no dark fringe at the edges
                    var dest = new RectangleF((outW - drawW) / 2, (outH - drawH) / 2, drawW, drawH);
                    g.DrawImage(img, Rectangle.Round(dest), 0, 0, img.Width, img.Height, GraphicsUnit.Pixel, attrs);
                }
                frames.Add(new AssetFormat.Frame(groups.Count > 1 ? delay : 0, Jpeg(bmp, quality)));
            }
            var file = AssetFormat.Build(outW, outH, frames);
            if (file.Length <= AssetFormat.MaxFileBytes || quality <= 40)
            {
                if (file.Length > AssetFormat.MaxFileBytes)
                {
                    throw new InvalidOperationException($"{p.Path}: the image is too big for the device even at low quality; use fewer frames");
                }
                Log.Info($"assets: {p.FileName} {outW}x{outH}, {frames.Count} frame(s), {file.Length} bytes (quality {quality})");
                return file;
            }
        }
    }

    // GIF frame delays in ms (PropertyTagFrameDelay 0x5100, 1/100 s per frame); one 0 for stills.
    private static List<int> Delays(Image img)
    {
        if (!img.FrameDimensionsList.Contains(FrameDimension.Time.Guid))
        {
            return [0];
        }
        var n = img.GetFrameCount(FrameDimension.Time);
        var delays = Enumerable.Repeat(100, n).ToList();
        if (img.PropertyIdList.Contains(0x5100))
        {
            var raw = img.GetPropertyItem(0x5100)!.Value!;
            for (var i = 0; i < n && i * 4 + 3 < raw.Length; i++)
            {
                var cs = BitConverter.ToInt32(raw, i * 4);
                delays[i] = cs <= 1 ? 100 : cs * 10;  // browsers treat 0 and 1 as 100 ms too
            }
        }
        return delays;
    }

    // Groups consecutive frames until each lasts at least the minimum delay (first frame index, delay).
    private static List<(int First, int Delay)> MergeShortFrames(List<int> delays)
    {
        if (delays.Count == 1)
        {
            return [(0, 0)];
        }
        var groups = new List<(int First, int Delay)>();
        var start = 0;
        var sum = 0;
        for (var i = 0; i < delays.Count; i++)
        {
            sum += delays[i];
            if (sum >= AssetFormat.MinFrameDelayMs)
            {
                groups.Add((start, sum));
                start = i + 1;
                sum = 0;
            }
        }
        if (sum > 0)
        {
            groups.Add((start, Math.Max(sum, AssetFormat.MinFrameDelayMs)));
        }
        return groups;
    }

    private static byte[] Jpeg(Bitmap bmp, long quality)
    {
        var codec = ImageCodecInfo.GetImageEncoders().First(c => c.FormatID == ImageFormat.Jpeg.Guid);
        using var ps = new EncoderParameters(1);
        ps.Param[0] = new EncoderParameter(System.Drawing.Imaging.Encoder.Quality, quality);
        using var ms = new MemoryStream();
        bmp.Save(ms, codec, ps);
        return ms.ToArray();
    }

    private static string? Format(ImageFormat f) =>
        f.Guid == ImageFormat.Gif.Guid ? "gif" :
        f.Guid == ImageFormat.Png.Guid ? "png" :
        f.Guid == ImageFormat.Jpeg.Guid ? "jpg" :
        f.Guid == ImageFormat.Bmp.Guid ? "bmp" : null;

    private static string ContentType(string format) => format switch
    {
        "gif" => "image/gif",
        "png" => "image/png",
        "jpg" => "image/jpeg",
        _ => "image/bmp",
    };

    private string MetaPath(string id) => Path.Combine(_dir, $"{id}.json");
}
