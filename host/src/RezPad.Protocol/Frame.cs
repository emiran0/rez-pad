using System.Buffers.Binary;

namespace RezPad.Protocol;

/// <summary>Message type codes, see docs/protocol.md.</summary>
public enum MsgType : byte
{
    // Host -> Device
    Hello = 0x01,
    Ping = 0x02,
    TimeSync = 0x03,
    NowPlaying = 0x10,
    ArtBegin = 0x11,
    ArtChunk = 0x12,
    ArtEnd = 0x13,
    ArtNone = 0x14,
    Volume = 0x15,
    SysStats = 0x20,
    Weather = 0x30,
    ConfigBegin = 0x40,
    ConfigChunk = 0x41,
    ConfigEnd = 0x42,
    ConfigGet = 0x43,
    Spectrum = 0x50,
    Cmd = 0x60,

    // Device -> Host
    HelloAck = 0x81,
    Pong = 0x82,
    Action = 0x90,
    ArtRequest = 0x91,
    Sensor = 0xA0,
    ConfigAck = 0xC0,
    ConfigDataBegin = 0xC1,
    ConfigDataChunk = 0xC2,
    ConfigDataEnd = 0xC3,
    Log = 0xE0,
    Error = 0xE1,
    CmdResult = 0xE2,
}

public readonly record struct Packet(MsgType Type, ushort Seq, byte[] Body);

public enum ParseResult
{
    Ok,
    TooShort,
    TooLarge,
    BadVersion,
    BadCrc,
}

public static class Frame
{
    public const byte ProtocolVersion = 1;
    public const int HeaderSize = 4;
    public const int CrcSize = 2;
    public const int MinPacket = HeaderSize + CrcSize;
    public const int MaxPacket = 8192;
    public const int MaxBody = MaxPacket - MinPacket;
    public const int ChunkSize = 4096;

    /// <summary>Builds a wire frame: COBS(packet) followed by 0x00.</summary>
    public static byte[] Build(MsgType type, ushort seq, ReadOnlySpan<byte> body)
    {
        if (body.Length > MaxBody)
        {
            throw new ArgumentException($"Body of {body.Length} bytes exceeds the {MaxBody} byte limit; chunk it.", nameof(body));
        }
        var packet = new byte[MinPacket + body.Length];
        packet[0] = ProtocolVersion;
        packet[1] = (byte)type;
        BinaryPrimitives.WriteUInt16LittleEndian(packet.AsSpan(2), seq);
        body.CopyTo(packet.AsSpan(HeaderSize));
        var crc = Crc16.Compute(packet.AsSpan(0, HeaderSize + body.Length));
        BinaryPrimitives.WriteUInt16LittleEndian(packet.AsSpan(HeaderSize + body.Length), crc);

        var frame = new byte[Cobs.MaxEncodedSize(packet.Length) + 1];
        var n = Cobs.Encode(packet, frame);
        frame[n] = 0;
        return frame.AsSpan(0, n + 1).ToArray();
    }

    /// <summary>Validates a decoded (post-COBS) packet.</summary>
    public static ParseResult Parse(ReadOnlySpan<byte> packet, out Packet result)
    {
        result = default;
        if (packet.Length < MinPacket)
        {
            return ParseResult.TooShort;
        }
        if (packet.Length > MaxPacket)
        {
            return ParseResult.TooLarge;
        }
        if (packet[0] != ProtocolVersion)
        {
            return ParseResult.BadVersion;
        }
        var expected = BinaryPrimitives.ReadUInt16LittleEndian(packet[^2..]);
        if (Crc16.Compute(packet[..^2]) != expected)
        {
            return ParseResult.BadCrc;
        }
        result = new Packet(
            (MsgType)packet[1],
            BinaryPrimitives.ReadUInt16LittleEndian(packet[2..]),
            packet[HeaderSize..^2].ToArray());
        return ParseResult.Ok;
    }
}

public sealed class DecoderStats
{
    public uint Ok { get; internal set; }
    public uint BadCobs { get; internal set; }
    public uint TooShort { get; internal set; }
    public uint TooLarge { get; internal set; }
    public uint BadVersion { get; internal set; }
    public uint BadCrc { get; internal set; }
}

/// <summary>
/// Streaming receiver. Feed raw bytes from the serial port; complete valid packets are returned.
/// Malformed frames are dropped and counted, never thrown.
/// </summary>
public sealed class FrameDecoder
{
    private readonly byte[] _buf = new byte[Cobs.MaxEncodedSize(Frame.MaxPacket)];
    private int _len;
    private bool _overflow;

    public DecoderStats Stats { get; } = new();

    public void Reset()
    {
        _len = 0;
        _overflow = false;
    }

    /// <summary>Pushes bytes and appends every complete valid packet to <paramref name="packets"/>.</summary>
    public void Push(ReadOnlySpan<byte> data, List<Packet> packets)
    {
        foreach (var b in data)
        {
            if (Push(b, out var p))
            {
                packets.Add(p);
            }
        }
    }

    public bool Push(byte b, out Packet packet)
    {
        packet = default;
        if (b != 0)
        {
            if (_len < _buf.Length)
            {
                _buf[_len++] = b;
            }
            else
            {
                _overflow = true;
            }
            return false;
        }

        var len = _len;
        var overflowed = _overflow;
        Reset();
        if (len == 0)
        {
            return false;
        }
        if (overflowed)
        {
            Stats.TooLarge++;
            return false;
        }
        var n = Cobs.Decode(_buf.AsSpan(0, len), _buf);
        if (n < 0)
        {
            Stats.BadCobs++;
            return false;
        }
        switch (Frame.Parse(_buf.AsSpan(0, n), out packet))
        {
            case ParseResult.Ok:
                Stats.Ok++;
                return true;
            case ParseResult.TooShort:
                Stats.TooShort++;
                break;
            case ParseResult.TooLarge:
                Stats.TooLarge++;
                break;
            case ParseResult.BadVersion:
                Stats.BadVersion++;
                break;
            case ParseResult.BadCrc:
                Stats.BadCrc++;
                break;
        }
        return false;
    }
}
