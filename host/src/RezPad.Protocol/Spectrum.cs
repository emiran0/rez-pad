namespace RezPad.Protocol;

/// <summary>
/// Audio spectrum for SPECTRUM messages (docs/protocol.md): 32 log-spaced bands from 50 Hz to
/// 16 kHz, each 0 (-70 dBFS) to 255 (0 dBFS) with a +3 dB/octave tilt around 1 kHz, smoothed
/// (instant rise, falls by 255 per 0.6 s). Feed mono samples with <see cref="Push"/>, call
/// <see cref="Next"/> once per frame. Not thread-safe.
/// </summary>
public sealed class SpectrumAnalyzer
{
    public const int Bands = 32;
    public const int FftSize = 2048;
    public const double MinHz = 50;
    public const double MaxHz = 16000;
    public const double FloorDb = -70;
    public const double TiltDbPerOctave = 3;
    public const double FallPerSecond = 255 / 0.6;

    private readonly int _sampleRate;
    private readonly float[] _ring = new float[FftSize];
    private int _ringPos;
    private readonly double[] _window = new double[FftSize];
    private readonly double[] _re = new double[FftSize];
    private readonly double[] _im = new double[FftSize];
    private readonly double[] _cos = new double[FftSize / 2];
    private readonly double[] _sin = new double[FftSize / 2];
    private readonly int[] _bitrev = new int[FftSize];
    private readonly (int From, int To)[] _bandBins = new (int, int)[Bands];
    private readonly double[] _tilt = new double[Bands];
    private readonly double[] _smoothed = new double[Bands];

    public SpectrumAnalyzer(int sampleRate)
    {
        if (sampleRate < 8000)
        {
            throw new ArgumentOutOfRangeException(nameof(sampleRate));
        }
        _sampleRate = sampleRate;
        for (var i = 0; i < FftSize; i++)
        {
            _window[i] = 0.5 - 0.5 * Math.Cos(2 * Math.PI * i / FftSize);  // Hann
        }
        for (var i = 0; i < FftSize / 2; i++)
        {
            _cos[i] = Math.Cos(2 * Math.PI * i / FftSize);
            _sin[i] = -Math.Sin(2 * Math.PI * i / FftSize);
        }
        var bits = (int)Math.Log2(FftSize);
        for (var i = 0; i < FftSize; i++)
        {
            var r = 0;
            for (var b = 0; b < bits; b++)
            {
                r |= ((i >> b) & 1) << (bits - 1 - b);
            }
            _bitrev[i] = r;
        }

        var hzPerBin = (double)sampleRate / FftSize;
        var maxBin = FftSize / 2 - 1;
        for (var b = 0; b < Bands; b++)
        {
            var lo = Edge(b);
            var hi = Edge(b + 1);
            var from = (int)Math.Ceiling(lo / hzPerBin);
            var to = (int)Math.Ceiling(hi / hzPerBin) - 1;  // inclusive
            if (to < from)
            {
                // Narrower than one FFT bin (low bands): use the bin nearest the band's center.
                from = to = (int)Math.Round(Math.Sqrt(lo * hi) / hzPerBin);
            }
            _bandBins[b] = (Math.Clamp(from, 1, maxBin), Math.Clamp(to, 1, maxBin));
            _tilt[b] = TiltDbPerOctave * Math.Log2(Math.Sqrt(lo * hi) / 1000);
        }
    }

    /// <summary>Lower edge of band <paramref name="b"/> in Hz (b = Bands gives the top edge).</summary>
    public static double Edge(int b) => MinHz * Math.Pow(MaxHz / MinHz, (double)b / Bands);

    public int SampleRate => _sampleRate;

    /// <summary>Appends mono samples (-1 to 1); only the last <see cref="FftSize"/> are analyzed.</summary>
    public void Push(ReadOnlySpan<float> mono)
    {
        if (mono.Length >= FftSize)
        {
            mono[^FftSize..].CopyTo(_ring);
            _ringPos = 0;
            return;
        }
        foreach (var s in mono)
        {
            _ring[_ringPos] = s;
            _ringPos = (_ringPos + 1) % FftSize;
        }
    }

    /// <summary>Clears the sample history (e.g. after silence or a device change).</summary>
    public void Clear()
    {
        Array.Clear(_ring);
        _ringPos = 0;
    }

    /// <summary>Levels of the current window without smoothing.</summary>
    public byte[] Analyze()
    {
        for (var i = 0; i < FftSize; i++)
        {
            var j = _bitrev[i];
            _re[j] = _ring[(_ringPos + i) % FftSize] * _window[i];
            _im[j] = 0;
        }
        Fft();

        // A full-scale sine through a Hann window peaks at N/4: that is 0 dBFS.
        const double fullScale = FftSize / 4.0;
        var levels = new byte[Bands];
        for (var b = 0; b < Bands; b++)
        {
            var (from, to) = _bandBins[b];
            double best = 0;
            for (var k = from; k <= to; k++)
            {
                var p = _re[k] * _re[k] + _im[k] * _im[k];
                if (p > best)
                {
                    best = p;
                }
            }
            var db = 10 * Math.Log10(best / (fullScale * fullScale) + 1e-20) + _tilt[b];
            var level = (db - FloorDb) / -FloorDb * 255;
            levels[b] = (byte)Math.Clamp(Math.Round(level), 0, 255);
        }
        return levels;
    }

    /// <summary>
    /// The next frame: analyzes the current window and applies the smoothing for
    /// <paramref name="dtSeconds"/> since the previous frame.
    /// </summary>
    public byte[] Next(double dtSeconds)
    {
        var raw = Analyze();
        var fall = FallPerSecond * Math.Max(0, dtSeconds);
        var output = new byte[Bands];
        for (var b = 0; b < Bands; b++)
        {
            _smoothed[b] = Math.Max(raw[b], _smoothed[b] - fall);
            output[b] = (byte)Math.Clamp(Math.Round(_smoothed[b]), 0, 255);
        }
        return output;
    }

    /// <summary>SPECTRUM body: <c>[n u8][bins u8 * n]</c>.</summary>
    public static byte[] Encode(ReadOnlySpan<byte> bins)
    {
        if (bins.Length is < 1 or > 64)
        {
            throw new ArgumentOutOfRangeException(nameof(bins), "1 to 64 bins");
        }
        var body = new byte[1 + bins.Length];
        body[0] = (byte)bins.Length;
        bins.CopyTo(body.AsSpan(1));
        return body;
    }

    private void Fft()
    {
        // Iterative radix-2, input already in bit-reversed order.
        for (var size = 2; size <= FftSize; size <<= 1)
        {
            var half = size >> 1;
            var step = FftSize / size;
            for (var start = 0; start < FftSize; start += size)
            {
                for (var k = 0; k < half; k++)
                {
                    var wr = _cos[k * step];
                    var wi = _sin[k * step];
                    var a = start + k;
                    var b = a + half;
                    var tr = _re[b] * wr - _im[b] * wi;
                    var ti = _re[b] * wi + _im[b] * wr;
                    _re[b] = _re[a] - tr;
                    _im[b] = _im[a] - ti;
                    _re[a] += tr;
                    _im[a] += ti;
                }
            }
        }
    }
}
