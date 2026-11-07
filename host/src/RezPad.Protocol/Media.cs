using System.Buffers.Binary;
using System.Text;

namespace RezPad.Protocol;

/// <summary>Helpers for the Now Playing messages (docs/protocol.md).</summary>
public static class TrackId
{
    /// <summary>CRC32 of UTF-8 <c>title + "\n" + artist + "\n" + album</c>.</summary>
    public static uint Compute(string title, string artist, string album) =>
        Crc32.Compute(Encoding.UTF8.GetBytes($"{title}\n{artist}\n{album}"));
}

public static class ArtTransfer
{
    /// <summary>
    /// The packets that send one JPEG image: ART_BEGIN, ART_CHUNKs of up to
    /// <see cref="Frame.ChunkSize"/> bytes, ART_END.
    /// </summary>
    public static IEnumerable<(MsgType Type, byte[] Body)> Packets(uint id, int w, int h, byte[] jpeg)
    {
        yield return (MsgType.ArtBegin,
            ProtocolJson.Serialize(new ArtBeginMsg(id, w, h, "jpeg", jpeg.Length, Crc32.Compute(jpeg))));
        for (var offset = 0; offset < jpeg.Length; offset += Frame.ChunkSize)
        {
            var n = Math.Min(Frame.ChunkSize, jpeg.Length - offset);
            var body = new byte[8 + n];
            BinaryPrimitives.WriteUInt32LittleEndian(body, id);
            BinaryPrimitives.WriteUInt32LittleEndian(body.AsSpan(4), (uint)offset);
            jpeg.AsSpan(offset, n).CopyTo(body.AsSpan(8));
            yield return (MsgType.ArtChunk, body);
        }
        yield return (MsgType.ArtEnd, ProtocolJson.Serialize(new ArtIdMsg(id)));
    }
}
