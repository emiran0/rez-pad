using System.Runtime.InteropServices;
using System.Text;

namespace RezPad.Host.Stats;

/// <summary>
/// NVIDIA GPU readings straight from NVML (nvml.dll, installed with the NVIDIA driver in System32).
/// Each call costs a few microseconds, versus about 33 ms of CPU for a full LibreHardwareMonitor
/// GPU update. No admin rights needed. Values are read once per sample by <see cref="Sample"/>.
/// </summary>
internal sealed class Nvml : IDisposable
{
    private const int Success = 0;
    private const int TemperatureGpu = 0;  // NVML_TEMPERATURE_GPU
    private const int ClockGraphics = 0;   // NVML_CLOCK_GRAPHICS

    private readonly IntPtr _device;
    private bool _fanRpmSupported = true;

    private Nvml(IntPtr device, string name, (uint, uint)? pci)
    {
        _device = device;
        Name = name;
        Pci = pci;
    }

    public string Name { get; }

    /// <summary>PCI bus and device number, to find the same GPU among the D3D adapters.</summary>
    public (uint Bus, uint Device)? Pci { get; }

    public double? Temperature { get; private set; }
    public double? PowerW { get; private set; }
    public double? ClockMhz { get; private set; }
    public double? CoreLoad { get; private set; }
    public double? FanPct { get; private set; }
    public double? FanRpm { get; private set; }
    public double? VramUsedGb { get; private set; }
    public double? VramPct { get; private set; }

    /// <summary>First NVIDIA GPU, or null without an NVIDIA driver.</summary>
    public static Nvml? Open()
    {
        try
        {
            if (nvmlInit_v2() != Success)
            {
                return null;
            }
            if (nvmlDeviceGetCount_v2(out var count) != Success || count == 0 ||
                nvmlDeviceGetHandleByIndex_v2(0, out var device) != Success)
            {
                nvmlShutdown();
                return null;
            }
            var name = new byte[96];
            var n = nvmlDeviceGetName(device, name, (uint)name.Length) == Success ? Array.IndexOf(name, (byte)0) : 0;
            // nvmlPciInfo_t: char busIdLegacy[16], uint domain, bus, device, pciDeviceId, pciSubSystemId, char busId[32].
            var pci = new byte[68];
            (uint, uint)? address = nvmlDeviceGetPciInfo_v3(device, pci) == Success
                ? (BitConverter.ToUInt32(pci, 20), BitConverter.ToUInt32(pci, 24))
                : null;
            return new Nvml(device, Encoding.UTF8.GetString(name, 0, n < 0 ? name.Length : n), address);
        }
        catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException or BadImageFormatException)
        {
            return null;  // no NVIDIA driver
        }
    }

    public void Sample()
    {
        Temperature = nvmlDeviceGetTemperature(_device, TemperatureGpu, out var t) == Success && t > 0 ? t : null;
        PowerW = nvmlDeviceGetPowerUsage(_device, out var mw) == Success ? mw / 1000.0 : null;
        ClockMhz = nvmlDeviceGetClockInfo(_device, ClockGraphics, out var mhz) == Success ? mhz : null;
        CoreLoad = nvmlDeviceGetUtilizationRates(_device, out var util) == Success ? util.Gpu : null;
        FanPct = nvmlDeviceGetFanSpeed(_device, out var fan) == Success ? fan : null;
        if (nvmlDeviceGetMemoryInfo(_device, out var mem) == Success && mem.Total > 0)
        {
            VramUsedGb = mem.Used / 1073741824.0;
            VramPct = 100.0 * mem.Used / mem.Total;
        }
        else
        {
            VramUsedGb = VramPct = null;
        }
        FanRpm = null;
        if (_fanRpmSupported)
        {
            try
            {
                // nvmlFanSpeedInfo_v1: version = sizeof | (1 << 24). Newer drivers only (R535+).
                var info = new FanSpeedInfo { Version = (uint)Marshal.SizeOf<FanSpeedInfo>() | (1u << 24), Fan = 0 };
                if (nvmlDeviceGetFanSpeedRPM(_device, ref info) == Success)
                {
                    FanRpm = info.Speed;
                }
            }
            catch (EntryPointNotFoundException)
            {
                _fanRpmSupported = false;
            }
        }
    }

    public void Dispose() => nvmlShutdown();

    [StructLayout(LayoutKind.Sequential)]
    private struct Utilization
    {
        public uint Gpu;
        public uint Memory;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct Memory
    {
        public ulong Total;
        public ulong Free;
        public ulong Used;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct FanSpeedInfo
    {
        public uint Version;
        public uint Fan;
        public uint Speed;
    }

    [DllImport("nvml.dll")]
    private static extern int nvmlInit_v2();

    [DllImport("nvml.dll")]
    private static extern int nvmlShutdown();

    [DllImport("nvml.dll")]
    private static extern int nvmlDeviceGetCount_v2(out uint count);

    [DllImport("nvml.dll")]
    private static extern int nvmlDeviceGetHandleByIndex_v2(uint index, out IntPtr device);

    [DllImport("nvml.dll")]
    private static extern int nvmlDeviceGetName(IntPtr device, [Out] byte[] name, uint length);

    [DllImport("nvml.dll")]
    private static extern int nvmlDeviceGetPciInfo_v3(IntPtr device, [Out] byte[] pciInfo);

    [DllImport("nvml.dll")]
    private static extern int nvmlDeviceGetTemperature(IntPtr device, int sensor, out uint celsius);

    [DllImport("nvml.dll")]
    private static extern int nvmlDeviceGetPowerUsage(IntPtr device, out uint milliwatts);

    [DllImport("nvml.dll")]
    private static extern int nvmlDeviceGetClockInfo(IntPtr device, int type, out uint mhz);

    [DllImport("nvml.dll")]
    private static extern int nvmlDeviceGetUtilizationRates(IntPtr device, out Utilization utilization);

    [DllImport("nvml.dll")]
    private static extern int nvmlDeviceGetFanSpeed(IntPtr device, out uint percent);

    [DllImport("nvml.dll")]
    private static extern int nvmlDeviceGetMemoryInfo(IntPtr device, out Memory memory);

    [DllImport("nvml.dll")]
    private static extern int nvmlDeviceGetFanSpeedRPM(IntPtr device, ref FanSpeedInfo info);
}
