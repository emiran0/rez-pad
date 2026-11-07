using System.Text;
using System.Text.Json;
using RezPad.Protocol;
using Xunit;

namespace RezPad.Protocol.Tests;

public class MessageTests
{
    [Fact]
    public void HelloUsesWireFieldNames()
    {
        var json = Encoding.UTF8.GetString(ProtocolJson.Serialize(new HelloMsg(1, "0.1.0")));
        Assert.Equal("""{"proto":1,"host_ver":"0.1.0"}""", json);
    }

    [Fact]
    public void TimeSyncUsesWireFieldNames()
    {
        var json = Encoding.UTF8.GetString(ProtocolJson.Serialize(new TimeSyncMsg(1793912345678, 60)));
        Assert.Equal("""{"unix_ms":1793912345678,"tz_offset_min":60}""", json);
    }

    [Fact]
    public void TimeSyncFromClock()
    {
        var clock = new FakeClock(DateTimeOffset.FromUnixTimeMilliseconds(1793912345678),
            TimeZoneInfo.CreateCustomTimeZone("plus3", TimeSpan.FromHours(3), "plus3", "plus3"));
        var msg = TimeSyncMsg.Now(clock);
        Assert.Equal(1793912345678, msg.UnixMs);
        Assert.Equal(180, msg.TzOffsetMin);
    }

    [Fact]
    public void ParsesHelloAckFromFirmware()
    {
        // Exactly what firmware/src/link/link.cpp produces.
        var body = """{"proto":1,"fw_ver":"0.1.0","w":480,"h":320,"psram_free":8382000,"cfg_crc":0,"buttons":0,"serial":"RP-ACA704F47EDC"}"""u8;
        var ack = ProtocolJson.Deserialize<HelloAckMsg>(body);
        Assert.NotNull(ack);
        Assert.Equal(1, ack.Proto);
        Assert.Equal("0.1.0", ack.FwVer);
        Assert.Equal(480, ack.W);
        Assert.Equal(8382000, ack.PsramFree);
        Assert.Equal("RP-ACA704F47EDC", ack.Serial);
    }

    [Fact]
    public void ParsesActionAndError()
    {
        var action = ProtocolJson.Deserialize<ActionMsg>("""{"action":"media.volume","value":-2}"""u8);
        Assert.Equal(new ActionMsg("media.volume", -2), action);
        var err = ProtocolJson.Deserialize<ErrorMsg>("""{"code":"need_hello","msg":"x"}"""u8);
        Assert.Equal("need_hello", err!.Code);
    }

    [Fact]
    public void SysStatsKeepsIdsAndNulls()
    {
        var msg = new SysStatsMsg(1793912345678, new Dictionary<string, double?>
        {
            ["sys.cpu.load"] = 12.5,
            ["sys.cpu.temp"] = null,
            ["sys.disk.C.used_pct"] = 71,
        });
        var json = Encoding.UTF8.GetString(ProtocolJson.Serialize(msg));
        // Source ids are not renamed by the snake_case policy, and null stays (the device shows "--").
        Assert.Equal("""{"t":1793912345678,"v":{"sys.cpu.load":12.5,"sys.cpu.temp":null,"sys.disk.C.used_pct":71}}""", json);
    }

    [Fact]
    public void BadJsonReturnsNull() => Assert.Null(ProtocolJson.Deserialize<HelloAckMsg>("{nope"u8));

    [Fact]
    public void CmdArgsOmittedWhenNull()
    {
        var json = Encoding.UTF8.GetString(ProtocolJson.Serialize(new CmdMsg("reboot")));
        Assert.Equal("""{"cmd":"reboot"}""", json);
        var withArgs = ProtocolJson.Serialize(new CmdMsg("set_spi_hz", JsonDocument.Parse("""{"hz":40000000}""").RootElement));
        Assert.Contains("\"args\":{\"hz\":40000000}", Encoding.UTF8.GetString(withArgs));
    }
}

public class HeartbeatTests
{
    [Fact]
    public void PingsEverySecondAndDetectsLoss()
    {
        var clock = new FakeClock(DateTimeOffset.UnixEpoch, TimeZoneInfo.Utc);
        var hb = new HeartbeatMonitor(clock);
        Assert.False(hb.PingDue());  // not started
        hb.Start();
        Assert.False(hb.PingDue());
        clock.Advance(TimeSpan.FromMilliseconds(1000));
        Assert.True(hb.PingDue());
        Assert.False(hb.PingDue());
        hb.OnDeviceAlive();  // PONG at t = 1 s

        clock.Advance(TimeSpan.FromMilliseconds(2500));
        Assert.False(hb.Lost);
        hb.OnDeviceAlive();
        clock.Advance(TimeSpan.FromMilliseconds(3000));
        Assert.False(hb.Lost);  // exactly 3 s
        clock.Advance(TimeSpan.FromMilliseconds(1));
        Assert.True(hb.Lost);
    }

    [Fact]
    public void TimeSyncEveryMinute()
    {
        var clock = new FakeClock(DateTimeOffset.UnixEpoch, TimeZoneInfo.Utc);
        var hb = new HeartbeatMonitor(clock);
        hb.Start();
        clock.Advance(TimeSpan.FromSeconds(59));
        Assert.False(hb.TimeSyncDue());
        clock.Advance(TimeSpan.FromSeconds(1));
        Assert.True(hb.TimeSyncDue());
        Assert.False(hb.TimeSyncDue());
    }
}

internal sealed class FakeClock(DateTimeOffset start, TimeZoneInfo zone) : TimeProvider
{
    private DateTimeOffset _now = start;
    private long _ticks;

    public override DateTimeOffset GetUtcNow() => _now;
    public override TimeZoneInfo LocalTimeZone => zone;
    public override long TimestampFrequency => TimeSpan.TicksPerSecond;
    public override long GetTimestamp() => _ticks;

    public void Advance(TimeSpan by)
    {
        _now += by;
        _ticks += by.Ticks;
    }
}
