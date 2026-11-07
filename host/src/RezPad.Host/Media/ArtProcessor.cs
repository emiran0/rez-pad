using RezPad.Protocol;
using Windows.Graphics.Imaging;
using Windows.Storage.Streams;

namespace RezPad.Host.Media;

/// <summary>
/// Turns a media thumbnail into the square JPEG the device shows: center crop, resize, encode
/// (Windows.Graphics.Imaging, no extra libraries). Results are cached by source image and size.
/// </summary>
internal static class ArtProcessor
{
    private const double JpegQuality = 0.85;
    private const int CacheSize = 8;
    private const int MaxSourceBytes = 8 * 1024 * 1024;

    private static readonly object CacheLock = new();
    private static readonly LinkedList<(uint Key, byte[] Jpeg)> Cache = new();

    /// <summary>Reads the raw thumbnail bytes, or null when there is none.</summary>
    public static async Task<byte[]?> ReadThumbnailAsync(IRandomAccessStreamReference? thumbnail)
    {
        if (thumbnail is null)
        {
            return null;
        }
        using var stream = await thumbnail.OpenReadAsync();
        if (stream.Size == 0 || stream.Size > MaxSourceBytes)
        {
            return null;
        }
        using var ms = new MemoryStream((int)stream.Size);
        await stream.AsStreamForRead().CopyToAsync(ms);
        return ms.ToArray();
    }

    /// <summary>Square JPEG of <paramref name="size"/> px, or null if the source cannot be decoded.</summary>
    public static async Task<byte[]?> ToJpegAsync(byte[] source, int size)
    {
        var key = Crc32.Compute(source, (uint)size);
        lock (CacheLock)
        {
            for (var node = Cache.First; node is not null; node = node.Next)
            {
                if (node.Value.Key == key)
                {
                    Cache.Remove(node);
                    Cache.AddFirst(node);
                    return node.Value.Jpeg;
                }
            }
        }

        byte[] jpeg;
        try
        {
            jpeg = await EncodeAsync(source, (uint)size);
        }
        catch (Exception ex)
        {
            Log.Warn($"art: could not convert thumbnail ({source.Length} bytes): {ex.Message}");
            return null;
        }
        lock (CacheLock)
        {
            Cache.AddFirst((key, jpeg));
            while (Cache.Count > CacheSize)
            {
                Cache.RemoveLast();
            }
        }
        return jpeg;
    }

    private static async Task<byte[]> EncodeAsync(byte[] source, uint size)
    {
        using var input = new MemoryStream(source, writable: false);
        var decoder = await BitmapDecoder.CreateAsync(input.AsRandomAccessStream());
        var w = decoder.PixelWidth;
        var h = decoder.PixelHeight;
        // Scale so the short side is `size`, then crop the middle square.
        var scale = (double)size / Math.Min(w, h);
        var sw = Math.Max(size, (uint)Math.Round(w * scale));
        var sh = Math.Max(size, (uint)Math.Round(h * scale));
        var transform = new BitmapTransform
        {
            ScaledWidth = sw,
            ScaledHeight = sh,
            InterpolationMode = BitmapInterpolationMode.Fant,
            Bounds = new BitmapBounds { X = (sw - size) / 2, Y = (sh - size) / 2, Width = size, Height = size },
        };
        using var bitmap = await decoder.GetSoftwareBitmapAsync(BitmapPixelFormat.Bgra8, BitmapAlphaMode.Ignore,
            transform, ExifOrientationMode.IgnoreExifOrientation, ColorManagementMode.ColorManageToSRgb);

        using var output = new InMemoryRandomAccessStream();
        var options = new BitmapPropertySet
        {
            { "ImageQuality", new BitmapTypedValue((float)JpegQuality, Windows.Foundation.PropertyType.Single) },
        };
        // Baseline JPEG with 4:2:0 chroma (WIC defaults), which JPEGDEC on the device handles.
        var encoder = await BitmapEncoder.CreateAsync(BitmapEncoder.JpegEncoderId, output, options);
        encoder.SetSoftwareBitmap(bitmap);
        await encoder.FlushAsync();

        output.Seek(0);
        using var ms = new MemoryStream((int)output.Size);
        await output.AsStreamForRead().CopyToAsync(ms);
        return ms.ToArray();
    }
}
