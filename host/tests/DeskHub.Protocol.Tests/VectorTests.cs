using System.Text.Json;
using DeskHub.Protocol;
using Xunit;

namespace DeskHub.Protocol.Tests;

/// <summary>Tests against docs/protocol-test-vectors.json, shared with the firmware tests.</summary>
public class VectorTests
{
    private static readonly JsonElement V = JsonDocument.Parse(
        File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "protocol-test-vectors.json"))).RootElement;

    private static byte[] Hex(JsonElement e, string name) => Convert.FromHexString(e.GetProperty(name).GetString()!);

    [Fact]
    public void ProtocolVersionMatches() =>
        Assert.Equal(Frame.ProtocolVersion, V.GetProperty("protocol_version").GetByte());

    [Fact]
    public void Crc16Vectors()
    {
        foreach (var e in V.GetProperty("crc16").EnumerateArray())
        {
            Assert.Equal(e.GetProperty("crc").GetUInt16(), Crc16.Compute(Hex(e, "data")));
        }
    }

    [Fact]
    public void Crc32Vectors()
    {
        foreach (var e in V.GetProperty("crc32").EnumerateArray())
        {
            Assert.Equal(e.GetProperty("crc").GetUInt32(), Crc32.Compute(Hex(e, "data")));
        }
    }

    [Fact]
    public void Crc32Incremental()
    {
        var e = V.GetProperty("crc32")[2];
        var data = Hex(e, "data");
        var c = Crc32.Compute(data.AsSpan(0, 100));
        c = Crc32.Compute(data.AsSpan(100), c);
        Assert.Equal(e.GetProperty("crc").GetUInt32(), c);
    }

    [Fact]
    public void CobsVectors()
    {
        foreach (var e in V.GetProperty("cobs").EnumerateArray())
        {
            var dec = Hex(e, "decoded");
            var enc = Hex(e, "encoded");
            Assert.Equal(enc, Cobs.Encode(dec));
            Assert.True(Cobs.TryDecode(enc, out var back));
            Assert.Equal(dec, back);
        }
    }

    [Fact]
    public void FrameVectors()
    {
        foreach (var e in V.GetProperty("frames").EnumerateArray())
        {
            var type = (MsgType)e.GetProperty("type").GetByte();
            var seq = e.GetProperty("seq").GetUInt16();
            var body = Hex(e, "body");
            var frame = Hex(e, "frame");
            Assert.Equal(frame, Frame.Build(type, seq, body));

            var dec = new FrameDecoder();
            var packets = new List<Packet>();
            dec.Push(frame, packets);
            var p = Assert.Single(packets);
            Assert.Equal(type, p.Type);
            Assert.Equal(seq, p.Seq);
            Assert.Equal(body, p.Body);
        }
    }

    [Fact]
    public void BadFramesAreDroppedAndCounted()
    {
        var dec = new FrameDecoder();
        var packets = new List<Packet>();
        foreach (var e in V.GetProperty("bad_frames").EnumerateArray())
        {
            dec.Push(Hex(e, "frame"), packets);
        }
        Assert.Empty(packets);
        Assert.Equal(1u, dec.Stats.BadCrc);
        Assert.Equal(1u, dec.Stats.TooShort);
        Assert.Equal(1u, dec.Stats.BadVersion);
        Assert.Equal(1u, dec.Stats.BadCobs);

        dec.Push(Hex(V.GetProperty("frames")[0], "frame"), packets);
        Assert.Single(packets);
    }
}

public class FramingTests
{
    [Fact]
    public void OverflowRecovers()
    {
        var dec = new FrameDecoder();
        var packets = new List<Packet>();
        dec.Push(Enumerable.Repeat((byte)0x55, 10_000).ToArray(), packets);
        dec.Push([0], packets);
        Assert.Equal(1u, dec.Stats.TooLarge);
        dec.Push(Frame.Build(MsgType.Ping, 1, []), packets);
        Assert.Single(packets);
    }

    [Fact]
    public void MaxBodyRoundTrip()
    {
        var body = new byte[Frame.MaxBody];
        for (var i = 0; i < body.Length; i++)
        {
            body[i] = (byte)(i * 7);
        }
        var dec = new FrameDecoder();
        var packets = new List<Packet>();
        dec.Push(Frame.Build(MsgType.ArtChunk, 7, body), packets);
        Assert.Equal(body, Assert.Single(packets).Body);
        Assert.Throws<ArgumentException>(() => Frame.Build(MsgType.ArtChunk, 7, new byte[Frame.MaxBody + 1]));
    }

    [Fact]
    public void SplitDeliveryAcrossReads()
    {
        var frame = Frame.Build(MsgType.Hello, 3, "{\"proto\":1}"u8);
        var dec = new FrameDecoder();
        var packets = new List<Packet>();
        foreach (var chunk in frame.Chunk(3))
        {
            dec.Push(chunk, packets);
        }
        Assert.Equal(MsgType.Hello, Assert.Single(packets).Type);
    }

    [Fact]
    public void RandomRoundTrip()
    {
        var rng = new Random(1234);
        for (var i = 0; i < 500; i++)
        {
            var data = new byte[rng.Next(0, 2000)];
            rng.NextBytes(data);
            if (i % 3 == 0)
            {
                Array.Clear(data, 0, data.Length / 2);
            }
            var enc = Cobs.Encode(data);
            Assert.DoesNotContain((byte)0, enc);
            Assert.True(enc.Length <= Cobs.MaxEncodedSize(data.Length));
            Assert.True(Cobs.TryDecode(enc, out var back));
            Assert.Equal(data, back);
        }
    }
}
