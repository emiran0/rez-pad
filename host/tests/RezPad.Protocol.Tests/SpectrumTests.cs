using RezPad.Protocol;
using Xunit;

namespace RezPad.Protocol.Tests;

public class SpectrumTests
{
    private const int Rate = 48000;

    private static float[] Sine(double hz, double amplitude, int n = SpectrumAnalyzer.FftSize)
    {
        var s = new float[n];
        for (var i = 0; i < n; i++)
        {
            s[i] = (float)(amplitude * Math.Sin(2 * Math.PI * hz * i / Rate));
        }
        return s;
    }

    private static int BandOf(double hz)
    {
        for (var b = 0; b < SpectrumAnalyzer.Bands; b++)
        {
            if (hz >= SpectrumAnalyzer.Edge(b) && hz < SpectrumAnalyzer.Edge(b + 1))
            {
                return b;
            }
        }
        throw new ArgumentOutOfRangeException(nameof(hz));
    }

    [Fact]
    public void BandsAreLogSpacedFrom50HzTo16kHz()
    {
        Assert.Equal(50, SpectrumAnalyzer.Edge(0), 6);
        Assert.Equal(16000, SpectrumAnalyzer.Edge(SpectrumAnalyzer.Bands), 6);
        var ratio = SpectrumAnalyzer.Edge(1) / SpectrumAnalyzer.Edge(0);
        Assert.Equal(ratio, SpectrumAnalyzer.Edge(21) / SpectrumAnalyzer.Edge(20), 9);
    }

    [Fact]
    public void SilenceIsZero()
    {
        var a = new SpectrumAnalyzer(Rate);
        a.Push(new float[SpectrumAnalyzer.FftSize]);
        Assert.All(a.Analyze(), level => Assert.Equal(0, level));
    }

    [Theory]
    [InlineData(100.0)]
    [InlineData(1000.0)]
    [InlineData(8000.0)]
    public void SineLightsItsOwnBand(double hz)
    {
        var a = new SpectrumAnalyzer(Rate);
        a.Push(Sine(hz, 0.5));
        var levels = a.Analyze();
        var band = BandOf(hz);
        var peak = Array.IndexOf(levels, levels.Max());
        Assert.InRange(peak, band - 1, band + 1);  // a tone near an edge may land next door
        // Far away bands stay dark (Hann leakage falls off quickly).
        for (var b = 0; b < SpectrumAnalyzer.Bands; b++)
        {
            if (Math.Abs(b - band) >= 6)
            {
                Assert.True(levels[b] < 40, $"band {b} is {levels[b]} for a {hz} Hz tone");
            }
        }
    }

    [Fact]
    public void LevelIsLinearInDecibels()
    {
        // At 1 kHz the tilt is 0: a -40 dBFS sine maps to (70 - 40) / 70 * 255 = 109.
        var a = new SpectrumAnalyzer(Rate);
        a.Push(Sine(1000, 0.01));
        Assert.InRange(a.Analyze()[BandOf(1000)], 100, 112);
        a.Push(Sine(1000, 1.0));
        Assert.InRange(a.Analyze()[BandOf(1000)], 245, 255);
    }

    [Fact]
    public void SmoothingRisesAtOnceAndFallsSlowly()
    {
        var a = new SpectrumAnalyzer(Rate);
        a.Push(Sine(1000, 0.5));
        var loud = a.Next(0.033)[BandOf(1000)];
        Assert.True(loud > 200);
        a.Push(new float[SpectrumAnalyzer.FftSize]);
        var after = a.Next(0.1)[BandOf(1000)];
        Assert.InRange(after, loud - 43, loud - 41);  // 255 per 0.6 s
        for (var i = 0; i < 10; i++)
        {
            a.Next(0.1);
        }
        Assert.Equal(0, a.Next(0.1)[BandOf(1000)]);
    }

    [Fact]
    public void ShortPushesFillTheWindow()
    {
        var a = new SpectrumAnalyzer(Rate);
        var s = Sine(1000, 0.5, SpectrumAnalyzer.FftSize);
        for (var i = 0; i < s.Length; i += 480)
        {
            a.Push(s.AsSpan(i, Math.Min(480, s.Length - i)));
        }
        var b = new SpectrumAnalyzer(Rate);
        b.Push(s);
        Assert.Equal(b.Analyze(), a.Analyze());
    }

    [Fact]
    public void EncodeWritesCountThenBins()
    {
        Assert.Equal(new byte[] { 3, 0, 128, 255 }, SpectrumAnalyzer.Encode(new byte[] { 0, 128, 255 }));
        Assert.Throws<ArgumentOutOfRangeException>(() => SpectrumAnalyzer.Encode(ReadOnlySpan<byte>.Empty));
    }
}
