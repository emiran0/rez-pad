using System.Buffers.Binary;
using System.Text;
using RezPad.Protocol;
using Xunit;

namespace RezPad.Protocol.Tests;

public class MediaTests
{
    [Fact]
    public void TrackIdIsCrc32OfJoinedFields()
    {
        var expected = Crc32.Compute(Encoding.UTF8.GetBytes("Gökyüzü\nÇağrı\nAlbum"));
        Assert.Equal(expected, TrackId.Compute("Gökyüzü", "Çağrı", "Album"));
        Assert.NotEqual(TrackId.Compute("a", "bc", ""), TrackId.Compute("ab", "c", ""));
    }

    [Fact]
    public void NowPlayingUsesWireFieldNames()
    {
        var msg = new NowPlayingMsg("playing", 2849123344, "T", "A", "B", "Spotify", 81234, 215000, 1793912345678,
            true, true, false);
        var json = Encoding.UTF8.GetString(ProtocolJson.Serialize(msg));
        Assert.Equal("""{"state":"playing","track_id":2849123344,"title":"T","artist":"A","album":"B","app":"Spotify","pos_ms":81234,"dur_ms":215000,"ts":1793912345678,"can_seek":true,"can_next":true,"can_prev":false}""", json);
    }

    [Fact]
    public void NonAsciiTextStaysUtf8()
    {
        // The firmware reads raw UTF-8; escaping as \uXXXX would also work but costs bytes.
        var json = Encoding.UTF8.GetString(ProtocolJson.Serialize(new NowPlayingMsg("paused", 1, "Ночь", "", "", "", 0, 0, 0,
            false, false, false)));
        Assert.Contains("\"title\":\"Ночь\"", json);
    }

    [Fact]
    public void VolumeAndArtMessages()
    {
        Assert.Equal("""{"level":42,"muted":true}""", Encoding.UTF8.GetString(ProtocolJson.Serialize(new VolumeMsg(42, true))));
        Assert.Equal("""{"id":7,"w":200,"h":200,"fmt":"jpeg","size":10,"crc32":3}""",
            Encoding.UTF8.GetString(ProtocolJson.Serialize(new ArtBeginMsg(7, 200, 200, "jpeg", 10, 3))));
        Assert.Equal(new ArtIdMsg(5), ProtocolJson.Deserialize<ArtIdMsg>("""{"id":5}"""u8));
    }

    [Fact]
    public void HelloAckArtSizeIsOptional()
    {
        var old = ProtocolJson.Deserialize<HelloAckMsg>("""{"proto":1,"fw_ver":"0.1.0","w":480,"h":320,"psram_free":1,"cfg_crc":0,"buttons":0}"""u8);
        Assert.Null(old!.Art);
        var withArt = ProtocolJson.Deserialize<HelloAckMsg>("""{"proto":1,"fw_ver":"0.2.0","w":480,"h":320,"psram_free":1,"cfg_crc":0,"buttons":0,"art":196}"""u8);
        Assert.Equal(196, withArt!.Art);
    }

    [Fact]
    public void ArtTransferChunksAndReassembles()
    {
        var jpeg = new byte[Frame.ChunkSize * 2 + 123];
        new Random(1).NextBytes(jpeg);
        var packets = ArtTransfer.Packets(99, 200, 200, jpeg).ToList();

        Assert.Equal(MsgType.ArtBegin, packets[0].Type);
        var begin = ProtocolJson.Deserialize<ArtBeginMsg>(packets[0].Body)!;
        Assert.Equal(new ArtBeginMsg(99, 200, 200, "jpeg", jpeg.Length, Crc32.Compute(jpeg)), begin);
        Assert.Equal(MsgType.ArtEnd, packets[^1].Type);
        Assert.Equal(new ArtIdMsg(99), ProtocolJson.Deserialize<ArtIdMsg>(packets[^1].Body));

        var chunks = packets.Skip(1).SkipLast(1).ToList();
        Assert.Equal(3, chunks.Count);
        var rebuilt = new byte[jpeg.Length];
        foreach (var (type, body) in chunks)
        {
            Assert.Equal(MsgType.ArtChunk, type);
            Assert.True(body.Length <= 8 + Frame.ChunkSize);
            Assert.Equal(99u, BinaryPrimitives.ReadUInt32LittleEndian(body));
            var offset = (int)BinaryPrimitives.ReadUInt32LittleEndian(body.AsSpan(4));
            body.AsSpan(8).CopyTo(rebuilt.AsSpan(offset));
            // Every chunk fits in one frame.
            Assert.NotEmpty(Frame.Build(type, 0, body));
        }
        Assert.Equal(jpeg, rebuilt);
    }
}
