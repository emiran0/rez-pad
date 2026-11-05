namespace RezPad.Protocol;

/// <summary>Canonical Consistent Overhead Byte Stuffing (no trailing 0x01 after a final full block).</summary>
public static class Cobs
{
    public static int MaxEncodedSize(int length) => length + length / 254 + 1;

    public static byte[] Encode(ReadOnlySpan<byte> data)
    {
        var buf = new byte[MaxEncodedSize(data.Length)];
        var n = Encode(data, buf);
        return buf.AsSpan(0, n).ToArray();
    }

    /// <summary>Encodes into <paramref name="output"/> (must hold MaxEncodedSize bytes). Returns the length written.</summary>
    public static int Encode(ReadOnlySpan<byte> data, Span<byte> output)
    {
        var pos = 1;
        var codeIdx = 0;
        byte code = 1;
        var closedFull = false;
        foreach (var b in data)
        {
            if (b == 0)
            {
                if (closedFull)
                {
                    codeIdx = pos++;
                    code = 1;
                }
                output[codeIdx] = code;
                codeIdx = pos++;
                code = 1;
                closedFull = false;
                continue;
            }
            if (closedFull)
            {
                codeIdx = pos++;
                code = 1;
                closedFull = false;
            }
            output[pos++] = b;
            if (++code == 0xFF)
            {
                output[codeIdx] = code;
                closedFull = true;
            }
        }
        if (!closedFull)
        {
            output[codeIdx] = code;
        }
        return pos;
    }

    /// <summary>Decodes one COBS block (without the 0x00 delimiter). Returns false on malformed input.</summary>
    public static bool TryDecode(ReadOnlySpan<byte> data, out byte[] decoded)
    {
        var buf = new byte[data.Length];
        var n = Decode(data, buf);
        if (n < 0)
        {
            decoded = [];
            return false;
        }
        decoded = buf.AsSpan(0, n).ToArray();
        return true;
    }

    /// <summary>Decodes into <paramref name="output"/>. Returns the length, or -1 if malformed or too small.</summary>
    public static int Decode(ReadOnlySpan<byte> data, Span<byte> output)
    {
        var i = 0;
        var o = 0;
        while (i < data.Length)
        {
            var code = data[i++];
            if (code == 0)
            {
                return -1;
            }
            var n = code - 1;
            if (i + n > data.Length || o + n > output.Length)
            {
                return -1;
            }
            var block = data.Slice(i, n);
            if (block.Contains((byte)0))
            {
                return -1;
            }
            block.CopyTo(output[o..]);
            i += n;
            o += n;
            if (code != 0xFF && i < data.Length)
            {
                if (o >= output.Length)
                {
                    return -1;
                }
                output[o++] = 0;
            }
        }
        return o;
    }
}
