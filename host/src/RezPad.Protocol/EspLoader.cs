using System.Buffers.Binary;
using System.Text;

namespace RezPad.Protocol;

/// <summary>
/// SLIP framing of the ESP32 ROM serial loader (the protocol esptool speaks): every packet is
/// wrapped in 0xC0, with 0xC0 sent as DB DC and 0xDB as DB DD.
/// </summary>
public static class Slip
{
    public const byte End = 0xC0;
    private const byte Esc = 0xDB;
    private const byte EscEnd = 0xDC;
    private const byte EscEsc = 0xDD;

    public static byte[] Encode(ReadOnlySpan<byte> packet)
    {
        var out_ = new List<byte>(packet.Length + 16) { End };
        foreach (var b in packet)
        {
            if (b == End)
            {
                out_.Add(Esc);
                out_.Add(EscEnd);
            }
            else if (b == Esc)
            {
                out_.Add(Esc);
                out_.Add(EscEsc);
            }
            else
            {
                out_.Add(b);
            }
        }
        out_.Add(End);
        return [.. out_];
    }

    /// <summary>Feeds received bytes one at a time; returns a packet when a frame ends.</summary>
    public sealed class Decoder
    {
        private readonly List<byte> _buf = [];
        private bool _inFrame;
        private bool _esc;

        public byte[]? Push(byte b)
        {
            if (b == End)
            {
                var done = _inFrame && _buf.Count > 0 ? _buf.ToArray() : null;
                _buf.Clear();
                _inFrame = true;  // an end byte also starts the next frame
                _esc = false;
                return done;
            }
            if (!_inFrame)
            {
                return null;  // noise before the first frame (boot messages)
            }
            if (_esc)
            {
                _buf.Add(b == EscEnd ? End : b == EscEsc ? Esc : b);
                _esc = false;
            }
            else if (b == Esc)
            {
                _esc = true;
            }
            else
            {
                _buf.Add(b);
            }
            return null;
        }
    }
}

/// <summary>Commands and packets of the ESP32-S3 ROM loader (esptool's loader.py).</summary>
public static class EspRom
{
    public const byte FlashBegin = 0x02;
    public const byte FlashData = 0x03;
    public const byte FlashEnd = 0x04;
    public const byte Sync = 0x08;
    public const byte WriteReg = 0x09;
    public const byte ReadReg = 0x0A;
    public const byte SpiSetParams = 0x0B;
    public const byte SpiAttach = 0x0D;
    public const byte SpiFlashMd5 = 0x13;
    public const byte GetSecurityInfo = 0x14;

    public const int FlashBlock = 0x400;  // ROM write block over USB-Serial/JTAG
    public const uint Esp32S3ChipId = 9;

    /// <summary>A request: [0x00][op][len u16][checksum u32][data].</summary>
    public static byte[] Request(byte op, ReadOnlySpan<byte> data, uint checksum = 0)
    {
        var p = new byte[8 + data.Length];
        p[1] = op;
        BinaryPrimitives.WriteUInt16LittleEndian(p.AsSpan(2), (ushort)data.Length);
        BinaryPrimitives.WriteUInt32LittleEndian(p.AsSpan(4), checksum);
        data.CopyTo(p.AsSpan(8));
        return p;
    }

    public static byte[] SyncData()
    {
        var d = new byte[36];
        d[0] = 0x07;
        d[1] = 0x07;
        d[2] = 0x12;
        d[3] = 0x20;
        Array.Fill(d, (byte)0x55, 4, 32);
        return d;
    }

    /// <summary>The data checksum FLASH_DATA carries: 0xEF xor every byte.</summary>
    public static uint Checksum(ReadOnlySpan<byte> data)
    {
        byte c = 0xEF;
        foreach (var b in data)
        {
            c ^= b;
        }
        return c;
    }

    public static byte[] Words(params uint[] words)
    {
        var d = new byte[words.Length * 4];
        for (var i = 0; i < words.Length; i++)
        {
            BinaryPrimitives.WriteUInt32LittleEndian(d.AsSpan(i * 4), words[i]);
        }
        return d;
    }

    /// <summary>FLASH_DATA payload: [len][seq][0][0] + block padded with 0xFF to FlashBlock.</summary>
    public static byte[] FlashDataPayload(ReadOnlySpan<byte> image, int seq)
    {
        var block = new byte[FlashBlock];
        Array.Fill(block, (byte)0xFF);
        var start = seq * FlashBlock;
        image.Slice(start, Math.Min(FlashBlock, image.Length - start)).CopyTo(block);
        var d = new byte[16 + FlashBlock];
        Words((uint)FlashBlock, (uint)seq, 0, 0).CopyTo(d, 0);
        block.CopyTo(d, 16);
        return d;
    }

    /// <summary>A parsed response: [0x01][op][len u16][value u32][data]. Null when it is not one.</summary>
    public sealed record Response(byte Op, uint Value, byte[] Data)
    {
        /// <summary>Status after `respLen` bytes of result: (failed, reason). ROM sends 4 status bytes, the first two count.</summary>
        public (bool Failed, byte Reason) Status(int respLen = 0) =>
            Data.Length < respLen + 2 ? (true, (byte)0) : (Data[respLen] != 0, Data[respLen + 1]);
    }

    public static Response? Parse(byte[] packet)
    {
        if (packet.Length < 8 || packet[0] != 0x01)
        {
            return null;
        }
        var len = BinaryPrimitives.ReadUInt16LittleEndian(packet.AsSpan(2));
        var value = BinaryPrimitives.ReadUInt32LittleEndian(packet.AsSpan(4));
        var data = packet.AsSpan(8, Math.Min(len, packet.Length - 8)).ToArray();
        return new Response(packet[1], value, data);
    }
}

/// <summary>
/// A firmware file for the web update: a full flash image written at 0x0 (bootloader, partition
/// table, app; what tools/package.ps1 ships) or an app image written at 0x10000. Checked for the
/// ESP image magic and the ESP32-S3 chip id; the rez-pad version is read from its version tag.
/// </summary>
public sealed record FirmwareImage(byte[] Bytes, uint Offset, string? Version)
{
    public const uint AppOffset = 0x10000;
    private const uint PartitionTable = 0x8000;
    private static readonly byte[] VersionTag = Encoding.ASCII.GetBytes("rez-pad-fw-version:");

    /// <summary>Checks the file; throws InvalidDataException with a reason a person can act on.</summary>
    public static FirmwareImage Parse(byte[] bytes)
    {
        if (bytes.Length < 0x1000 || bytes[0] != 0xE9)
        {
            throw new InvalidDataException("not an ESP32 firmware image (.bin)");
        }
        // Image header byte 12..13: chip id (9 = ESP32-S3).
        var chip = BinaryPrimitives.ReadUInt16LittleEndian(bytes.AsSpan(12));
        if (chip != EspRom.Esp32S3ChipId)
        {
            throw new InvalidDataException($"made for another chip (id {chip}), this board is an ESP32-S3");
        }
        if (bytes.Length > 16 * 1024 * 1024)
        {
            throw new InvalidDataException("larger than the 16 MB flash");
        }
        var full = bytes.Length > PartitionTable + 2 && bytes[PartitionTable] == 0xAA && bytes[PartitionTable + 1] == 0x50;
        if (full && bytes.Length > AppOffset && bytes[AppOffset] != 0xE9)
        {
            throw new InvalidDataException("the image has a partition table but no app at 0x10000");
        }
        if (!full && bytes.Length > 0x600000)
        {
            throw new InvalidDataException("the app is larger than the 6 MB app partition");
        }
        return new FirmwareImage(bytes, full ? 0u : AppOffset, ReadVersion(bytes));
    }

    /// <summary>"0.3.0+abc1234" from the image, or null when it has no rez-pad version tag.</summary>
    public static string? ReadVersion(byte[] bytes)
    {
        var i = bytes.AsSpan().IndexOf(VersionTag);
        if (i < 0)
        {
            return null;
        }
        var start = i + VersionTag.Length;
        var end = Array.IndexOf(bytes, (byte)0, start);
        if (end < 0 || end - start > 64)
        {
            return null;
        }
        return Encoding.ASCII.GetString(bytes, start, end - start);
    }

    /// <summary>
    /// The release part of a version ("0.3.0" of "0.3.0+abc1234.dirty") compared like SemVer
    /// cores: below 0 when a is older than b. Unreadable versions count as 0.0.0.
    /// </summary>
    public static int Compare(string? a, string? b)
    {
        static int[] Core(string? v)
        {
            var core = (v ?? "").Split('+')[0].Split('-')[0].Split('.');
            return [.. Enumerable.Range(0, 3).Select(i => i < core.Length && int.TryParse(core[i], out var n) ? n : 0)];
        }
        var x = Core(a);
        var y = Core(b);
        for (var i = 0; i < 3; i++)
        {
            if (x[i] != y[i])
            {
                return x[i].CompareTo(y[i]);
            }
        }
        return 0;
    }
}
