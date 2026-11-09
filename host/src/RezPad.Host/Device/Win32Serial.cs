using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32;
using Microsoft.Win32.SafeHandles;

namespace RezPad.Host.Device;

/// <summary>
/// Minimal COM port over plain Win32 calls, used instead of System.IO.Ports.SerialPort.
/// SerialPort runs a hidden WaitCommEvent thread and, when the USB device is yanked or resets,
/// can fail to close its handle; Windows then needs 30 s to tear the process down and keeps the
/// port "busy" for the next instance (measured, see docs/measurements.md). Here every handle is
/// closed deterministically and there is no background thread.
/// </summary>
internal sealed class Win32Serial : IDisposable
{
    private const uint GenericRead = 0x80000000;
    private const uint GenericWrite = 0x40000000;
    private const uint OpenExisting = 3;
    private const int ErrorFileNotFound = 2;
    private const int ErrorAccessDenied = 5;
    private const uint PurgeRxClear = 0x0008;
    private const uint PurgeTxClear = 0x0004;
    private const uint PurgeTxAbort = 0x0001;
    private const uint PurgeRxAbort = 0x0002;
    private const uint SetRts = 3;
    private const uint ClrRts = 4;
    private const uint SetDtr = 5;
    private const uint ClrDtr = 6;

    private readonly SafeFileHandle _handle;
    private readonly FileStream _stream;

    private Win32Serial(SafeFileHandle handle)
    {
        _handle = handle;
        // Synchronous handle. Reads never wait (COMMTIMEOUTS below): Windows serializes synchronous
        // I/O on one handle, so a waiting read would hold every write back until it times out.
        _stream = new FileStream(handle, FileAccess.ReadWrite, bufferSize: 0, isAsync: false);
    }

    /// <summary>
    /// Opens the port with DTR and RTS on (controlLines false: both off, for the ESP32-S3 ROM
    /// loader, where RTS without DTR resets the chip). Throws UnauthorizedAccessException when busy.
    /// </summary>
    public static Win32Serial Open(string portName, bool controlLines = true)
    {
        var handle = CreateFile(@"\\.\" + portName, GenericRead | GenericWrite, 0, IntPtr.Zero, OpenExisting, 0,
            IntPtr.Zero);
        if (handle.IsInvalid)
        {
            var err = Marshal.GetLastWin32Error();
            handle.Dispose();
            throw err switch
            {
                ErrorAccessDenied => new UnauthorizedAccessException($"{portName} is in use"),
                ErrorFileNotFound => new FileNotFoundException($"{portName} not present"),
                _ => new IOException($"open {portName} failed", new Win32Exception(err)),
            };
        }
        try
        {
            var dcb = new Dcb { DcbLength = (uint)Marshal.SizeOf<Dcb>() };
            Check(GetCommState(handle, ref dcb), "GetCommState");
            dcb.BaudRate = 115200;  // ignored by USB CDC, but never 1200 (see docs/protocol.md)
            dcb.ByteSize = 8;
            dcb.Parity = 0;
            dcb.StopBits = 0;
            // fBinary | fDtrControl = ENABLE | fRtsControl = ENABLE; no flow control, no abort on error.
            dcb.Flags = controlLines ? 1u | (1u << 4) | (1u << 12) : 1u;
            Check(SetCommState(handle, ref dcb), "SetCommState");

            // Reads return immediately with whatever is buffered, 0 bytes when nothing is (the caller
            // sleeps). A read that waited would block writes from other threads for that long: with a
            // 20 ms read timeout every write took about 30 ms (measured 2026-11-08).
            var timeouts = new CommTimeouts
            {
                ReadIntervalTimeout = uint.MaxValue,
                ReadTotalTimeoutMultiplier = 0,
                ReadTotalTimeoutConstant = 0,
                WriteTotalTimeoutMultiplier = 0,
                WriteTotalTimeoutConstant = 500,
            };
            Check(SetCommTimeouts(handle, ref timeouts), "SetCommTimeouts");
            PurgeComm(handle, PurgeRxClear | PurgeTxClear);
            return new Win32Serial(handle);
        }
        catch
        {
            handle.Dispose();
            throw;
        }
    }

    /// <summary>Reads the bytes already received, 0 when there are none (never waits). Throws IOException when the device is gone.</summary>
    public int Read(byte[] buffer) => _stream.Read(buffer, 0, buffer.Length);

    public void Write(byte[] data) => _stream.Write(data, 0, data.Length);

    /// <summary>Sets DTR and RTS (the USB-Serial/JTAG peripheral maps them to the chip's EN and BOOT).</summary>
    public void SetLines(bool dtr, bool rts)
    {
        EscapeCommFunction(_handle, dtr ? SetDtr : ClrDtr);
        EscapeCommFunction(_handle, rts ? SetRts : ClrRts);
        // usbser.sys sends the line state with the DTR change; repeat DTR so the RTS change goes out
        // too (esptool does the same on Windows).
        EscapeCommFunction(_handle, dtr ? SetDtr : ClrDtr);
    }

    /// <summary>COM port names that exist right now (HKLM\HARDWARE\DEVICEMAP\SERIALCOMM).</summary>
    public static HashSet<string> PresentPorts()
    {
        var set = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        using var key = Registry.LocalMachine.OpenSubKey(@"HARDWARE\DEVICEMAP\SERIALCOMM");
        if (key is null)
        {
            return set;
        }
        foreach (var name in key.GetValueNames())
        {
            if (key.GetValue(name) is string port)
            {
                set.Add(port);
            }
        }
        return set;
    }

    public void Dispose()
    {
        // Cancel anything in flight and drop DTR/RTS while the handle is still open, so the
        // driver has no pending work left when the handle closes.
        PurgeComm(_handle, PurgeRxAbort | PurgeTxAbort | PurgeRxClear | PurgeTxClear);
        EscapeCommFunction(_handle, ClrDtr);
        EscapeCommFunction(_handle, ClrRts);
        try
        {
            _stream.Dispose();
        }
        catch (IOException)
        {
            // Device already gone; the handle below is still released.
        }
        _handle.Dispose();
    }

    private static void Check(bool ok, string what)
    {
        if (!ok)
        {
            throw new IOException(what + " failed", new Win32Exception(Marshal.GetLastWin32Error()));
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct Dcb
    {
        public uint DcbLength;
        public uint BaudRate;
        public uint Flags;
        public ushort Reserved;
        public ushort XonLim;
        public ushort XoffLim;
        public byte ByteSize;
        public byte Parity;
        public byte StopBits;
        public sbyte XonChar;
        public sbyte XoffChar;
        public sbyte ErrorChar;
        public sbyte EofChar;
        public sbyte EvtChar;
        public ushort Reserved1;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct CommTimeouts
    {
        public uint ReadIntervalTimeout;
        public uint ReadTotalTimeoutMultiplier;
        public uint ReadTotalTimeoutConstant;
        public uint WriteTotalTimeoutMultiplier;
        public uint WriteTotalTimeoutConstant;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern SafeFileHandle CreateFile(string name, uint access, uint share, IntPtr security,
        uint creation, uint flags, IntPtr template);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool GetCommState(SafeFileHandle h, ref Dcb dcb);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool SetCommState(SafeFileHandle h, ref Dcb dcb);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool SetCommTimeouts(SafeFileHandle h, ref CommTimeouts t);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool PurgeComm(SafeFileHandle h, uint flags);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool EscapeCommFunction(SafeFileHandle h, uint func);
}
