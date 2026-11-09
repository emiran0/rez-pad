using System.Text;
using RezPad.Protocol;
using Xunit;

namespace RezPad.Protocol.Tests;

public class EspLoaderTests
{
    [Fact]
    public void SlipEscapesAndRoundTrips()
    {
        byte[] packet = [0x00, 0xC0, 0x01, 0xDB, 0x02];
        var enc = Slip.Encode(packet);
        Assert.Equal(new byte[] { 0xC0, 0x00, 0xDB, 0xDC, 0x01, 0xDB, 0xDD, 0x02, 0xC0 }, enc);
        var dec = new Slip.Decoder();
        byte[]? got = null;
        // Boot noise before the first frame is ignored.
        foreach (var b in Encoding.ASCII.GetBytes("ESP-ROM:esp32s3\r\n").Concat(enc))
        {
            got = dec.Push(b) ?? got;
        }
        Assert.Equal(packet, got);
    }

    [Fact]
    public void RequestLayoutAndChecksum()
    {
        var req = EspRom.Request(EspRom.FlashData, [1, 2, 3], 0xAB);
        Assert.Equal(new byte[] { 0x00, 0x03, 0x03, 0x00, 0xAB, 0x00, 0x00, 0x00, 1, 2, 3 }, req);
        Assert.Equal(0xEFu ^ 1 ^ 2 ^ 3, EspRom.Checksum([1, 2, 3]));
        Assert.Equal(36, EspRom.SyncData().Length);
    }

    [Fact]
    public void FlashDataPadsTheLastBlock()
    {
        var image = Enumerable.Range(0, EspRom.FlashBlock + 10).Select(i => (byte)i).ToArray();
        var last = EspRom.FlashDataPayload(image, 1);
        Assert.Equal(16 + EspRom.FlashBlock, last.Length);
        Assert.Equal(1, last[4]);                    // seq
        Assert.Equal(image[EspRom.FlashBlock], last[16]);
        Assert.Equal(0xFF, last[16 + 10]);           // padding
    }

    [Fact]
    public void ResponseStatus()
    {
        // [1][op][len=6][value] + 2 result bytes + 4 status bytes (ok)
        byte[] ok = [0x01, 0x13, 0x06, 0x00, 0, 0, 0, 0, 0x41, 0x42, 0x00, 0x00, 0x00, 0x00];
        var r = EspRom.Parse(ok)!;
        Assert.Equal(EspRom.SpiFlashMd5, r.Op);
        Assert.False(r.Status(2).Failed);
        byte[] bad = [0x01, 0x02, 0x04, 0x00, 0, 0, 0, 0, 0x01, 0x05, 0x00, 0x00];
        Assert.Equal((true, (byte)0x05), EspRom.Parse(bad)!.Status());
        Assert.Null(EspRom.Parse([0x00, 0x02]));
    }

    private static byte[] Image(bool full, string? version, ushort chip = 9)
    {
        var b = new byte[full ? 0x12000 : 0x2000];
        b[0] = 0xE9;
        b[12] = (byte)chip;
        if (full)
        {
            b[0x8000] = 0xAA;
            b[0x8001] = 0x50;
            b[0x10000] = 0xE9;
        }
        if (version is not null)
        {
            var tag = Encoding.ASCII.GetBytes("rez-pad-fw-version:" + version + "\0");
            tag.CopyTo(b, b.Length - 0x100);
        }
        return b;
    }

    [Fact]
    public void FullImageGoesAtZeroAppImageAt0x10000()
    {
        var full = FirmwareImage.Parse(Image(true, "0.3.0+abc1234"));
        Assert.Equal(0u, full.Offset);
        Assert.Equal("0.3.0+abc1234", full.Version);
        var app = FirmwareImage.Parse(Image(false, null));
        Assert.Equal(FirmwareImage.AppOffset, app.Offset);
        Assert.Null(app.Version);
    }

    [Fact]
    public void RejectsOtherFiles()
    {
        Assert.Throws<InvalidDataException>(() => FirmwareImage.Parse(new byte[0x2000]));
        Assert.Throws<InvalidDataException>(() => FirmwareImage.Parse(Image(false, null, chip: 5)));
        Assert.Throws<InvalidDataException>(() => FirmwareImage.Parse([0xE9, 0, 0]));
    }

    [Theory]
    [InlineData("0.2.0+cc01efd", "0.3.0", -1)]
    [InlineData("0.3.0+cc01efd.dirty", "0.3.0", 0)]
    [InlineData("1.0.0", "0.9.9", 1)]
    [InlineData(null, "0.1.0", -1)]
    public void VersionCompareUsesTheReleasePart(string? a, string b, int sign)
    {
        Assert.Equal(sign, Math.Sign(FirmwareImage.Compare(a, b)));
    }
}
