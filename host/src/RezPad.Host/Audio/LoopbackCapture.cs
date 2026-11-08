using System.Diagnostics;
using System.Runtime.InteropServices;
using RezPad.Protocol;

namespace RezPad.Host.Audio;

/// <summary>
/// Captures what Windows plays on the default output device (WASAPI shared-mode loopback) on its own
/// thread and produces a spectrum frame about every 33 ms (<see cref="SpectrumAnalyzer"/>).
/// Follows default device changes; restarts after errors. Costs nothing while stopped.
/// </summary>
internal sealed class LoopbackCapture : IDisposable
{
    public const int FrameMs = 33;
    private const int PollMs = 10;
    private const int StreamLoopback = 0x00020000;  // AUDCLNT_STREAMFLAGS_LOOPBACK
    private const int BufferFlagSilent = 0x2;       // AUDCLNT_BUFFERFLAGS_SILENT
    private const long BufferDuration = 2_000_000;  // 200 ms in 100 ns units
    private const int ClsCtxAll = 23;

    private static readonly Guid FloatFormat = new("00000003-0000-0010-8000-00aa00389b71");
    private static readonly Guid PcmFormat = new("00000001-0000-0010-8000-00aa00389b71");

    private readonly Action<byte[]> _frame;
    private Thread? _thread;
    private volatile bool _stop;

    /// <param name="frame">Called on the capture thread with 32 smoothed levels per frame.</param>
    public LoopbackCapture(Action<byte[]> frame) => _frame = frame;

    public bool Running => _thread is not null;

    public void Start()
    {
        if (_thread is not null)
        {
            return;
        }
        _stop = false;
        _thread = new Thread(Run) { IsBackground = true, Name = "LoopbackCapture" };
        _thread.Start();
    }

    public void Stop()
    {
        var t = _thread;
        if (t is null)
        {
            return;
        }
        _stop = true;
        t.Join(2000);
        _thread = null;
    }

    public void Dispose() => Stop();

    private void Run()
    {
        var failures = 0;
        while (!_stop)
        {
            try
            {
                CaptureSession();
                failures = 0;
            }
            catch (Exception ex)
            {
                // Device unplugged or switched, exclusive mode elsewhere, no output device at all.
                if (failures++ == 0)
                {
                    Log.Info($"spectrum: capture stopped ({ex.Message}), retrying");
                }
            }
            for (var i = 0; i < 20 && !_stop; i++)
            {
                Thread.Sleep(100);  // 2 s between attempts
            }
        }
    }

    /// <summary>Runs until stopped, an error, or the default output device changes.</summary>
    private void CaptureSession()
    {
        var enumerator = (IMMDeviceEnumerator)new MMDeviceEnumeratorComObject();
        IMMDevice? device = null;
        IAudioClient? client = null;
        IAudioCaptureClient? capture = null;
        var formatPtr = IntPtr.Zero;
        try
        {
            Check(enumerator.GetDefaultAudioEndpoint(EDataFlow.Render, ERole.Console, out device), "default output");
            Check(device!.GetId(out var deviceId), "device id");
            var iid = typeof(IAudioClient).GUID;
            Check(device.Activate(ref iid, ClsCtxAll, IntPtr.Zero, out var obj), "activate");
            client = (IAudioClient)obj!;
            Check(client.GetMixFormat(out formatPtr), "mix format");
            var format = ReadFormat(formatPtr);
            Check(client.Initialize(0, StreamLoopback, BufferDuration, 0, formatPtr, IntPtr.Zero), "initialize");
            var ccIid = typeof(IAudioCaptureClient).GUID;
            Check(client.GetService(ref ccIid, out var ccObj), "capture client");
            capture = (IAudioCaptureClient)ccObj!;
            Check(client.Start(), "start");
            Log.Info($"spectrum: capturing {format.Rate} Hz, {format.Channels} ch, {(format.IsFloat ? "float" : "pcm")}{format.Bits}");

            var analyzer = new SpectrumAnalyzer(format.Rate);
            var mono = new float[format.Rate / 10];
            var clock = Stopwatch.StartNew();
            var lastFrame = clock.Elapsed;
            var lastDeviceCheck = clock.Elapsed;
            var lastData = clock.Elapsed;
            while (!_stop)
            {
                // Drain everything captured so far.
                var got = false;
                while (true)
                {
                    Check(capture.GetNextPacketSize(out var packet), "packet size");
                    if (packet == 0)
                    {
                        break;
                    }
                    Check(capture.GetBuffer(out var data, out var frames, out var flags, out _, out _), "get buffer");
                    var n = (int)Math.Min(frames, (uint)mono.Length);
                    if ((flags & BufferFlagSilent) != 0)
                    {
                        Array.Clear(mono, 0, n);
                    }
                    else
                    {
                        ToMono(data, n, format, mono);
                    }
                    analyzer.Push(mono.AsSpan(0, n));
                    Check(capture.ReleaseBuffer(frames), "release buffer");
                    got = true;
                }
                var now = clock.Elapsed;
                if (got)
                {
                    lastData = now;
                }
                else if ((now - lastData).TotalMilliseconds > 100)
                {
                    // Loopback delivers nothing while nothing plays: let the window drain to silence.
                    analyzer.Clear();
                }
                var dt = (now - lastFrame).TotalSeconds;
                if (dt * 1000 >= FrameMs)
                {
                    // Keep a 33 ms cadence on average: Sleep() wakes in 15.6 ms steps, so stepping
                    // from "now" would give about 23 frames per second.
                    lastFrame = dt * 1000 < FrameMs * 2 ? lastFrame + TimeSpan.FromMilliseconds(FrameMs) : now;
                    _frame(analyzer.Next(dt));
                }
                if ((now - lastDeviceCheck).TotalSeconds >= 2)
                {
                    lastDeviceCheck = now;
                    if (enumerator.GetDefaultAudioEndpoint(EDataFlow.Render, ERole.Console, out var current) == 0 && current is not null)
                    {
                        current.GetId(out var currentId);
                        Marshal.ReleaseComObject(current);
                        if (currentId != deviceId)
                        {
                            Log.Info("spectrum: default output device changed");
                            return;
                        }
                    }
                }
                Thread.Sleep(PollMs);
            }
        }
        finally
        {
            try
            {
                client?.Stop();
            }
            catch (COMException)
            {
            }
            if (formatPtr != IntPtr.Zero)
            {
                Marshal.FreeCoTaskMem(formatPtr);
            }
            foreach (var o in new object?[] { capture, client, device, enumerator })
            {
                if (o is not null)
                {
                    Marshal.ReleaseComObject(o);
                }
            }
        }
    }

    private readonly record struct Format(int Rate, int Channels, int Bits, int BlockAlign, bool IsFloat);

    private static Format ReadFormat(IntPtr p)
    {
        var tag = (ushort)Marshal.ReadInt16(p, 0);
        var channels = Marshal.ReadInt16(p, 2);
        var rate = Marshal.ReadInt32(p, 4);
        var blockAlign = Marshal.ReadInt16(p, 12);
        var bits = Marshal.ReadInt16(p, 14);
        bool isFloat;
        if (tag == 0xFFFE)  // WAVE_FORMAT_EXTENSIBLE: SubFormat GUID at offset 24
        {
            var sub = Marshal.PtrToStructure<Guid>(p + 24);
            isFloat = sub == FloatFormat;
            if (!isFloat && sub != PcmFormat)
            {
                throw new NotSupportedException($"mix format {sub}");
            }
        }
        else
        {
            isFloat = tag == 3;  // WAVE_FORMAT_IEEE_FLOAT; 1 = PCM
        }
        if ((isFloat && bits != 32) || (!isFloat && bits is not (16 or 24 or 32)))
        {
            throw new NotSupportedException($"{bits}-bit {(isFloat ? "float" : "pcm")}");
        }
        return new Format(rate, channels, bits, blockAlign, isFloat);
    }

    private static unsafe void ToMono(IntPtr data, int frames, Format f, float[] mono)
    {
        var bytes = (byte*)data;
        var bytesPerSample = f.Bits / 8;
        var scale = 1f / f.Channels;
        for (var i = 0; i < frames; i++)
        {
            var frame = bytes + (long)i * f.BlockAlign;
            float sum = 0;
            for (var c = 0; c < f.Channels; c++)
            {
                var s = frame + c * bytesPerSample;
                sum += f.IsFloat ? *(float*)s : f.Bits switch
                {
                    16 => *(short*)s / 32768f,
                    24 => ((s[0] << 8) | (s[1] << 16) | (s[2] << 24)) / 2147483648f,
                    _ => *(int*)s / 2147483648f,
                };
            }
            mono[i] = sum * scale;
        }
    }

    private static void Check(int hr, string what)
    {
        if (hr < 0)
        {
            throw new COMException($"{what} failed (0x{hr:X8})", hr);
        }
    }
}

// Audio client interfaces (audioclient.h). Method order must match the native vtable exactly.

[ComImport]
[Guid("1CB9AD4C-DBFA-4c32-B178-C2F568A703B2")]
[InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
internal interface IAudioClient
{
    [PreserveSig]
    int Initialize(int shareMode, int streamFlags, long bufferDuration, long periodicity, IntPtr format, IntPtr sessionGuid);

    [PreserveSig]
    int GetBufferSize(out uint frames);

    [PreserveSig]
    int GetStreamLatency(out long latency);

    [PreserveSig]
    int GetCurrentPadding(out uint frames);

    [PreserveSig]
    int IsFormatSupported(int shareMode, IntPtr format, out IntPtr closest);

    [PreserveSig]
    int GetMixFormat(out IntPtr format);

    [PreserveSig]
    int GetDevicePeriod(out long defaultPeriod, out long minimumPeriod);

    [PreserveSig]
    int Start();

    [PreserveSig]
    int Stop();

    [PreserveSig]
    int Reset();

    [PreserveSig]
    int SetEventHandle(IntPtr handle);

    [PreserveSig]
    int GetService(ref Guid iid, [MarshalAs(UnmanagedType.IUnknown)] out object? service);
}

[ComImport]
[Guid("C8ADBD64-E71E-48a0-A4DE-185C395CD317")]
[InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
internal interface IAudioCaptureClient
{
    [PreserveSig]
    int GetBuffer(out IntPtr data, out uint frames, out int flags, out ulong devicePosition, out ulong qpcPosition);

    [PreserveSig]
    int ReleaseBuffer(uint frames);

    [PreserveSig]
    int GetNextPacketSize(out uint frames);
}
