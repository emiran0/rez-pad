using System.Diagnostics;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Microsoft.Win32;
using RezPad.Protocol;

namespace RezPad.Host.Device;

/// <summary>
/// Firmware update from the web page (Device > Firmware). The running firmware restarts into the
/// ESP32-S3's ROM download mode on the same native USB port (CMD <c>bootloader</c>; it appears as
/// USB-Serial/JTAG 303A:1001), then this class writes the image with the ROM loader protocol
/// (esptool's commands, no stub), checks it with the ROM's MD5, clears the forced download boot
/// and resets the chip. A board that is already in download mode (BOOT held while plugging in,
/// or a blank board) is updated the same way. LittleFS (config, images) is never written: the
/// full image ends long before it.
/// </summary>
internal sealed class FirmwareUpdater
{
    public sealed record Status(
        bool Active, string Stage, double Progress, string? Message, string? Error, bool Done, string? Version);

    private const string BootKey = @"SYSTEM\CurrentControlSet\Enum\USB\VID_303A&PID_1001";
    private const string BootSerialKey = @"SYSTEM\CurrentControlSet\Enum\USB\VID_303A&PID_1001&MI_00";
    // ESP32-S3 registers (esptool targets/esp32s3.py).
    private const uint RtcCntlBase = 0x60008000;
    private const uint WdtConfig0 = RtcCntlBase + 0x98;
    private const uint WdtWprotect = RtcCntlBase + 0xB0;
    private const uint WdtWkey = 0x50D83AA1;
    private const uint SwdConf = RtcCntlBase + 0xB4;
    private const uint SwdWprotect = RtcCntlBase + 0xB8;
    private const uint SwdWkey = 0x8F1D312A;
    private const uint SwdAutoFeed = 1u << 31;
    private const uint Option1 = 0x6000812C;
    private const uint ForceDownloadBoot = 0x1;

    private readonly SerialLink _link;
    private readonly object _lock = new();
    private Status _status = new(false, "idle", 0, null, null, false, null);

    public FirmwareUpdater(SerialLink link)
    {
        _link = link;
        Bundled = LoadBundled();
    }

    /// <summary>The image embedded in this host (null in a build without firmware).</summary>
    public FirmwareImage? Bundled { get; }

    public Status Current
    {
        get
        {
            lock (_lock)
            {
                return _status;
            }
        }
    }

    private static FirmwareImage? LoadBundled()
    {
        try
        {
            using var s = typeof(FirmwareUpdater).Assembly.GetManifestResourceStream("RezPad.Host.firmware.bin");
            if (s is null)
            {
                return null;
            }
            using var ms = new MemoryStream();
            s.CopyTo(ms);
            return FirmwareImage.Parse(ms.ToArray());
        }
        catch (InvalidDataException e)
        {
            Log.Warn($"firmware: bundled image unusable: {e.Message}");
            return null;
        }
    }

    /// <summary>COM port of an ESP32-S3 (or other Espressif chip) in ROM download mode, if any.</summary>
    public static string? FindBootloaderPort()
    {
        var present = Win32Serial.PresentPorts();
        if (present.Count == 0)
        {
            return null;
        }
        using var serialKey = Registry.LocalMachine.OpenSubKey(BootSerialKey);
        if (serialKey is null)
        {
            return null;
        }
        foreach (var child in serialKey.GetSubKeyNames())
        {
            using var parameters = serialKey.OpenSubKey(child + @"\Device Parameters");
            if (parameters?.GetValue("PortName") is string port && present.Contains(port))
            {
                return port;
            }
        }
        return null;
    }

    /// <summary>Starts an update in the background; false (and why) when one cannot start now.</summary>
    public bool Start(FirmwareImage image, out string? error)
    {
        lock (_lock)
        {
            if (_status.Active)
            {
                error = "an update is already running";
                return false;
            }
            if (_link.Status.State != DeviceLinkState.Connected && FindBootloaderPort() is null)
            {
                error = "no board found: plug in the native USB port (or hold BOOT while plugging it in)";
                return false;
            }
            _status = new Status(true, "Starting", 0, null, null, false, image.Version);
        }
        error = null;
        _ = Task.Run(() => Run(image));
        return true;
    }

    private void Set(string stage, double progress, string? message = null)
    {
        lock (_lock)
        {
            _status = _status with { Stage = stage, Progress = progress, Message = message };
        }
    }

    private void Run(FirmwareImage image)
    {
        var sw = Stopwatch.StartNew();
        try
        {
            var port = EnterBootloader();
            Log.Info($"firmware: writing {image.Bytes.Length} bytes at 0x{image.Offset:X} through {port}");
            Set("Connecting", 0.02, "Talking to the chip's built-in loader");
            using (var rom = RomSession.Connect(port))
            {
                rom.CheckChip();
                rom.DisableWatchdogs();
                rom.Attach();
                Set("Erasing", 0.05, "Erasing the old firmware");
                var blocks = rom.FlashBegin(image.Offset, image.Bytes.Length);
                for (var seq = 0; seq < blocks; seq++)
                {
                    rom.FlashData(image.Bytes, seq);
                    if (seq % 16 == 0 || seq == blocks - 1)
                    {
                        Set("Writing", 0.05 + 0.85 * (seq + 1) / blocks, $"{(seq + 1) * EspRom.FlashBlock / 1024} of {image.Bytes.Length / 1024} KB");
                    }
                }
                Set("Verifying", 0.92, "Checking what was written");
                rom.Verify(image.Offset, image.Bytes);
                Set("Restarting", 0.95, "Starting the new firmware");
                rom.ResetToApp();
            }
            WaitForApp(image);
            Log.Info($"firmware: update to {image.Version ?? "?"} done in {sw.Elapsed.TotalSeconds:F1} s");
            lock (_lock)
            {
                _status = _status with { Active = false, Stage = "Done", Progress = 1, Done = true, Message = $"Updated in {sw.Elapsed.TotalSeconds:F0} s" };
            }
        }
        catch (Exception e) when (e is IOException or TimeoutException or InvalidOperationException or UnauthorizedAccessException)
        {
            Log.Warn($"firmware: update failed: {e.Message}");
            var stillInLoader = FindBootloaderPort() is not null;
            lock (_lock)
            {
                _status = _status with
                {
                    Active = false, Done = false, Error = e.Message,
                    Message = stillInLoader
                        ? "The board waits in update mode; start the update again (nothing is lost, the chip's loader is in ROM)."
                        : "Unplug and replug the board, then try again.",
                };
            }
        }
    }

    /// <summary>Restarts the running firmware into download mode; returns the loader's COM port.</summary>
    private string EnterBootloader()
    {
        var port = FindBootloaderPort();
        if (port is not null)
        {
            return port;  // already there (BOOT held, blank board, an earlier try)
        }
        Set("Restarting into update mode", 0.01, "The screen goes dark for a moment");
        var args = JsonSerializer.SerializeToElement(new { });
        if (!_link.SendJson(MsgType.Cmd, new CmdMsg("bootloader", args)))
        {
            throw new InvalidOperationException("the board is not connected");
        }
        var deadline = DateTime.UtcNow.AddSeconds(15);
        while (DateTime.UtcNow < deadline)
        {
            Thread.Sleep(250);
            port = FindBootloaderPort();
            if (port is not null)
            {
                Thread.Sleep(500);  // let the driver finish setting up the port
                return port;
            }
        }
        throw new TimeoutException("the board did not come up in update mode (its firmware may be too old for this: flash it once with esptool or hold BOOT while plugging it in)");
    }

    private void WaitForApp(FirmwareImage image)
    {
        Set("Restarting", 0.97, "Waiting for the board to connect");
        var deadline = DateTime.UtcNow.AddSeconds(25);
        while (DateTime.UtcNow < deadline)
        {
            Thread.Sleep(300);
            var s = _link.Status;
            if (s.State == DeviceLinkState.Connected && s.Device is not null)
            {
                if (image.Version is not null && s.Device.FwVer != image.Version)
                {
                    throw new InvalidOperationException($"the board runs {s.Device.FwVer} after the update, expected {image.Version}");
                }
                return;
            }
        }
        if (FindBootloaderPort() is { } port)
        {
            // Still in the loader: try the watchdog reset esptool uses for USB-OTG.
            Log.Warn("firmware: still in download mode after the reset, trying a watchdog reset");
            using (var rom = RomSession.Open(port))
            {
                rom.Sync();
                rom.WatchdogReset();
            }
            deadline = DateTime.UtcNow.AddSeconds(20);
            while (DateTime.UtcNow < deadline)
            {
                Thread.Sleep(300);
                if (_link.Status.State == DeviceLinkState.Connected)
                {
                    return;
                }
            }
        }
        throw new TimeoutException("the firmware was written and checked, but the board did not connect afterwards: unplug and replug it");
    }

    /// <summary>One connection to the ROM loader.</summary>
    private sealed class RomSession : IDisposable
    {
        private readonly Win32Serial _port;
        private readonly Slip.Decoder _decoder = new();
        private readonly byte[] _rx = new byte[4096];
        private readonly Queue<byte[]> _packets = new();

        private RomSession(Win32Serial port) => _port = port;

        public static RomSession Open(string portName)
        {
            var deadline = DateTime.UtcNow.AddSeconds(5);
            while (true)
            {
                try
                {
                    // DTR and RTS off: RTS alone would reset the chip.
                    return new RomSession(Win32Serial.Open(portName, controlLines: false));
                }
                catch (Exception e) when ((e is UnauthorizedAccessException or IOException) && DateTime.UtcNow < deadline)
                {
                    Thread.Sleep(250);  // driver still starting, or another program has it
                }
            }
        }

        /// <summary>
        /// Opens the loader. When it does not answer, the port belongs to a running program (a new
        /// board's demo firmware uses the same USB-Serial/JTAG port): reset it into download mode
        /// with the DTR/RTS sequence esptool uses for that peripheral, then try again.
        /// </summary>
        public static RomSession Connect(string portName)
        {
            var rom = Open(portName);
            try
            {
                rom.Sync();
                return rom;
            }
            catch (TimeoutException)
            {
                Log.Info("firmware: no loader answer, resetting the board into download mode");
                rom._port.SetLines(dtr: false, rts: false);
                Thread.Sleep(100);
                rom._port.SetLines(dtr: true, rts: false);   // BOOT low
                Thread.Sleep(100);
                try
                {
                    rom._port.SetLines(dtr: false, rts: true);  // reset while BOOT is latched
                    Thread.Sleep(100);
                    rom._port.SetLines(dtr: false, rts: false);
                }
                catch (IOException)
                {
                    // The port goes away with the reset.
                }
                rom.Dispose();
            }
            catch
            {
                rom.Dispose();
                throw;
            }
            Thread.Sleep(1500);  // the chip re-enumerates
            var port = FindBootloaderPort() ?? portName;
            var again = Open(port);
            try
            {
                again.Sync();
                return again;
            }
            catch
            {
                again.Dispose();
                throw;
            }
        }

        public void Dispose() => _port.Dispose();

        private void Send(byte op, ReadOnlySpan<byte> data, uint checksum = 0) =>
            _port.Write(Slip.Encode(EspRom.Request(op, data, checksum)));

        /// <summary>The next response to `op`, or null after `timeoutMs`.</summary>
        private EspRom.Response? Receive(byte op, int timeoutMs)
        {
            var sw = Stopwatch.StartNew();
            while (true)
            {
                while (_packets.Count > 0)
                {
                    var r = EspRom.Parse(_packets.Dequeue());
                    if (r is not null && r.Op == op)
                    {
                        return r;
                    }
                }
                var n = _port.Read(_rx);
                for (var i = 0; i < n; i++)
                {
                    if (_decoder.Push(_rx[i]) is { } p)
                    {
                        _packets.Enqueue(p);
                    }
                }
                if (n == 0)
                {
                    if (sw.ElapsedMilliseconds > timeoutMs)
                    {
                        return null;
                    }
                    Thread.Sleep(1);
                }
            }
        }

        private EspRom.Response Command(string what, byte op, ReadOnlySpan<byte> data, uint checksum = 0, int respLen = 0,
            int timeoutMs = 3000)
        {
            Send(op, data, checksum);
            var r = Receive(op, timeoutMs) ?? throw new TimeoutException($"no answer to {what}");
            var (failed, reason) = r.Status(respLen);
            if (failed)
            {
                throw new IOException($"{what} failed (ROM error 0x{reason:X2})");
            }
            return r;
        }

        public void Sync()
        {
            for (var i = 0; i < 12; i++)
            {
                Send(EspRom.Sync, EspRom.SyncData());
                if (Receive(EspRom.Sync, 150) is not null)
                {
                    // The ROM answers a SYNC several times; drop the rest.
                    Receive(0xFF, 150);
                    _packets.Clear();
                    return;
                }
            }
            throw new TimeoutException("the chip's loader does not answer");
        }

        public void CheckChip()
        {
            // GET_SECURITY_INFO: flags u32, crypt count u8, key purposes [7], chip id u32, eco u32.
            var r = Command("read the chip id", EspRom.GetSecurityInfo, [], respLen: 20);
            var chip = BitConverter.ToUInt32(r.Data, 12);
            if (chip != EspRom.Esp32S3ChipId)
            {
                throw new InvalidOperationException($"the board in update mode is not an ESP32-S3 (chip id {chip}); nothing was written");
            }
        }

        public uint ReadReg(uint addr) => Command("read a register", EspRom.ReadReg, EspRom.Words(addr)).Value;

        public void WriteReg(uint addr, uint value, uint mask = 0xFFFFFFFF) =>
            Command("write a register", EspRom.WriteReg, EspRom.Words(addr, value, mask, 0));

        /// <summary>Over USB-Serial/JTAG the RTC and super watchdogs keep running in the loader (esptool).</summary>
        public void DisableWatchdogs()
        {
            WriteReg(WdtWprotect, WdtWkey);
            WriteReg(WdtConfig0, 0);
            WriteReg(WdtWprotect, 0);
            WriteReg(SwdWprotect, SwdWkey);
            WriteReg(SwdConf, ReadReg(SwdConf) | SwdAutoFeed);
            WriteReg(SwdWprotect, 0);
        }

        public void Attach()
        {
            Command("attach the flash", EspRom.SpiAttach, new byte[8]);
            // id, total size (16 MB), block, sector, page, status mask
            Command("set the flash size", EspRom.SpiSetParams, EspRom.Words(0, 16 * 1024 * 1024, 64 * 1024, 4 * 1024, 256, 0xFFFF));
        }

        public int FlashBegin(uint offset, int size)
        {
            var blocks = (size + EspRom.FlashBlock - 1) / EspRom.FlashBlock;
            // The ROM erases the whole region first: esptool allows 40 s per MB.
            var timeout = Math.Max(5000, (int)(40_000.0 * size / (1024 * 1024)));
            Command("erase the flash", EspRom.FlashBegin, EspRom.Words((uint)size, (uint)blocks, EspRom.FlashBlock, offset, 0),
                timeoutMs: timeout);
            return blocks;
        }

        public void FlashData(byte[] image, int seq)
        {
            var payload = EspRom.FlashDataPayload(image, seq);
            var checksum = EspRom.Checksum(payload.AsSpan(16));
            for (var attempt = 0; ; attempt++)
            {
                try
                {
                    Command($"write block {seq}", EspRom.FlashData, payload, checksum, timeoutMs: 3000);
                    return;
                }
                catch (Exception e) when (e is TimeoutException or IOException && attempt < 2)
                {
                    Log.Warn($"firmware: block {seq} retry: {e.Message}");
                }
            }
        }

        public void Verify(uint offset, byte[] image)
        {
            var timeout = Math.Max(5000, (int)(8_000.0 * image.Length / (1024 * 1024)));
            var r = Command("check the written data", EspRom.SpiFlashMd5, EspRom.Words(offset, (uint)image.Length, 0, 0),
                respLen: 32, timeoutMs: timeout);
            var got = Encoding.ASCII.GetString(r.Data, 0, 32).ToLowerInvariant();
            var want = Convert.ToHexStringLower(MD5.HashData(image));
            if (got != want)
            {
                throw new IOException($"the flash does not match the image (MD5 {got}, expected {want})");
            }
        }

        /// <summary>Clears the forced download boot (else the chip comes back into the loader) and pulses reset.</summary>
        public void ResetToApp()
        {
            WriteReg(Option1, 0, ForceDownloadBoot);
            // USB-Serial/JTAG: RTS without DTR holds the chip in reset (esptool's hard reset).
            _port.SetLines(dtr: false, rts: true);
            Thread.Sleep(200);
            try
            {
                _port.SetLines(dtr: false, rts: false);
            }
            catch (IOException)
            {
                // The port went away with the reset.
            }
        }

        public void WatchdogReset()
        {
            WriteReg(Option1, 0, ForceDownloadBoot);
            WriteReg(WdtWprotect, WdtWkey);
            WriteReg(RtcCntlBase + 0x9C, 2000);  // WDTCONFIG1: timeout
            WriteReg(WdtConfig0, (1u << 31) | (5u << 28) | (1u << 8) | 2);
            WriteReg(WdtWprotect, 0);
            Thread.Sleep(500);
        }
    }
}
