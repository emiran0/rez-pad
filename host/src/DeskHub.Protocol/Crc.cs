namespace DeskHub.Protocol;

/// <summary>CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, xorout 0.</summary>
public static class Crc16
{
    private static readonly ushort[] Table = BuildTable();

    public static ushort Compute(ReadOnlySpan<byte> data, ushort crc = 0xFFFF)
    {
        foreach (var b in data)
        {
            crc = (ushort)((crc << 8) ^ Table[((crc >> 8) ^ b) & 0xFF]);
        }
        return crc;
    }

    private static ushort[] BuildTable()
    {
        var t = new ushort[256];
        for (var i = 0; i < 256; i++)
        {
            var c = (ushort)(i << 8);
            for (var k = 0; k < 8; k++)
            {
                c = (c & 0x8000) != 0 ? (ushort)((c << 1) ^ 0x1021) : (ushort)(c << 1);
            }
            t[i] = c;
        }
        return t;
    }
}

/// <summary>CRC-32/ISO-HDLC (zlib). Pass a previous result to continue a running CRC.</summary>
public static class Crc32
{
    private static readonly uint[] Table = BuildTable();

    public static uint Compute(ReadOnlySpan<byte> data, uint crc = 0)
    {
        crc = ~crc;
        foreach (var b in data)
        {
            crc = (crc >> 8) ^ Table[(crc ^ b) & 0xFF];
        }
        return ~crc;
    }

    private static uint[] BuildTable()
    {
        var t = new uint[256];
        for (uint i = 0; i < 256; i++)
        {
            var c = i;
            for (var k = 0; k < 8; k++)
            {
                c = (c & 1) != 0 ? (c >> 1) ^ 0xEDB88320u : c >> 1;
            }
            t[i] = c;
        }
        return t;
    }
}
