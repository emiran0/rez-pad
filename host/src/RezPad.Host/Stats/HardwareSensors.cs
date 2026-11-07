using LibreHardwareMonitor.Hardware;

namespace RezPad.Host.Stats;

/// <summary>
/// Temperatures, GPU, clocks and fans through LibreHardwareMonitorLib. CPU temperature and
/// motherboard fans need the PawnIO driver and admin rights; GPUs (NVAPI/ADL) work without either.
/// Not thread-safe: <see cref="SystemStatsService"/> calls it from its sampling thread only.
/// </summary>
internal sealed class HardwareSensors : IDisposable
{
    private readonly Computer _computer = new()
    {
        IsCpuEnabled = true,
        IsMotherboardEnabled = true,
    };
    private bool _open;

    public IHardware? Cpu { get; private set; }
    public IHardware? Gpu { get; private set; }
    public IReadOnlyList<ISensor> Fans { get; private set; } = [];

    /// <summary>
    /// Opens CPU and motherboard sensors, and GPUs when <paramref name="gpu"/> is set. With NVML and
    /// D3DKMT covering the GPU, its LibreHardwareMonitor part (vendor DLLs, about 33 ms CPU per
    /// update) is left closed until <see cref="EnableGpu"/>.
    /// </summary>
    public void Open(bool gpu)
    {
        _computer.IsGpuEnabled = gpu;
        try
        {
            _computer.Open();
            _open = true;
        }
        catch (Exception ex)
        {
            Log.Warn($"stats: LibreHardwareMonitor failed to open: {ex.Message}");
            return;
        }
        Update(cpu: true, gpu: true, board: true);
        Cpu = _computer.Hardware.FirstOrDefault(h => h.HardwareType == HardwareType.Cpu);
        FindGpu();
        // Motherboard fans live on the SuperIO chip (sub-hardware). Only fans that spin are listed.
        Fans = _computer.Hardware
            .Where(h => h.HardwareType == HardwareType.Motherboard)
            .SelectMany(h => h.SubHardware)
            .SelectMany(h => h.Sensors)
            .Where(s => s.SensorType == SensorType.Fan && s.Value > 0)
            .ToList();
        foreach (var hw in _computer.Hardware)
        {
            Log.Info($"stats: {hw.HardwareType} \"{hw.Name}\", {hw.Sensors.Length} sensors" +
                     $"{(hw.SubHardware.Length > 0 ? $", {hw.SubHardware.Length} sub-devices" : "")}");
        }
    }

    public bool GpuEnabled => _computer.IsGpuEnabled;

    /// <summary>Opens the GPU part on first use (takes about a second).</summary>
    public void EnableGpu()
    {
        if (!_open || _computer.IsGpuEnabled)
        {
            return;
        }
        _computer.IsGpuEnabled = true;
        FindGpu();
        Log.Info($"stats: LibreHardwareMonitor GPU sensors opened ({Gpu?.Name ?? "none"})");
    }

    private void FindGpu() =>
        // Prefer a discrete GPU over integrated graphics.
        Gpu = _computer.Hardware.FirstOrDefault(h => h.HardwareType is HardwareType.GpuNvidia or HardwareType.GpuAmd)
              ?? _computer.Hardware.FirstOrDefault(h => h.HardwareType == HardwareType.GpuIntel);

    /// <summary>Every hardware item and sensor with its last value (debugging aid for /api/hardware).</summary>
    public IEnumerable<object> Report()
    {
        if (!_open)
        {
            yield break;
        }
        foreach (var hw in _computer.Hardware.SelectMany(h => h.SubHardware.Prepend(h)))
        {
            yield return new
            {
                hardware = $"{hw.HardwareType} {hw.Name}",
                sensors = hw.Sensors.Select(s => new { name = s.Name, type = s.SensorType.ToString(), value = s.Value }),
            };
        }
    }

    /// <summary>Refreshes only the hardware groups something needs; each Update costs driver calls.</summary>
    public void Update(bool cpu, bool gpu, bool board)
    {
        if (!_open)
        {
            return;
        }
        foreach (var hw in _computer.Hardware)
        {
            var wanted = hw.HardwareType switch
            {
                HardwareType.Cpu => cpu,
                HardwareType.GpuNvidia or HardwareType.GpuAmd or HardwareType.GpuIntel => gpu,
                HardwareType.Motherboard => board,
                _ => false,
            };
            if (!wanted)
            {
                continue;
            }
            hw.Update();
            foreach (var sub in hw.SubHardware)
            {
                sub.Update();
            }
        }
    }

    /// <summary>First sensor of a type whose name matches one of the names, in order of preference.</summary>
    public static ISensor? Find(IHardware? hw, SensorType type, params string[] names)
    {
        if (hw is null)
        {
            return null;
        }
        foreach (var name in names)
        {
            var s = hw.Sensors.FirstOrDefault(x => x.SensorType == type && x.Name.Equals(name, StringComparison.OrdinalIgnoreCase));
            if (s is not null)
            {
                return s;
            }
        }
        return null;
    }

    public void Dispose()
    {
        if (_open)
        {
            _computer.Close();
            _open = false;
        }
    }
}
