using System.ComponentModel;
using System.Diagnostics;
using System.Net.NetworkInformation;
using System.Runtime.InteropServices;
using System.Security.Principal;
using LibreHardwareMonitor.Hardware;
using Microsoft.Win32;

namespace RezPad.Host.Stats;

/// <summary>What a source has to refresh before it can be read. Only what the layout uses is refreshed.</summary>
[Flags]
internal enum Needs
{
    None = 0,
    CpuCounters = 1,
    Network = 2,
    CpuSensors = 4,
    GpuSensors = 8,
    BoardSensors = 16,
    Nvml = 32,
    Engines = 64,
}

/// <summary>A data source the layout can show (docs/config-schema.md, "Data source IDs").</summary>
internal sealed record StatSource(string Id, string Name, string Unit, Func<double?> Read, Needs Needs = Needs.None);

/// <summary>What limits the sensor readings on this PC, for the tray menu and the web UI.</summary>
internal sealed record SensorAccess(bool IsAdmin, bool PawnIoInstalled, bool CpuTempAvailable, string? Message);

/// <summary>
/// Samples system metrics once per tick on its own thread and keeps the latest value of every
/// source in the catalog. CPU load, clock, RAM, disks and network come from Windows itself (same
/// numbers as Task Manager); temperatures, GPU and fans from LibreHardwareMonitorLib.
/// </summary>
internal sealed class SystemStatsService : IDisposable
{
    private readonly object _lock = new();
    private readonly HardwareSensors _hw = new();
    private Nvml? _nvml;
    private GpuEngines? _engines;
    private readonly CancellationTokenSource _cts = new();
    private TimeSpan _period;
    // While the web page shows the whole catalog, sample everything for a few seconds.
    private static readonly TimeSpan CatalogWindow = TimeSpan.FromSeconds(5);
    private long _catalogWantedAt;
    private Thread? _thread;
    private List<StatSource> _catalog = [];
    private Dictionary<string, double?> _values = [];

    // Windows counters
    private PerformanceCounter? _cpuUtility;
    private PerformanceCounter? _cpuPerformance;
    private PerformanceCounter? _cpuFrequency;
    private CpuTimes _cpuTimes;  // fallback when the performance counters are missing
    private double? _cpuLoad;
    private double? _cpuClock;
    private long _netRx;
    private long _netTx;
    private long _netAt;
    private double? _netDown;
    private double? _netUp;

    public SystemStatsService(double rateHz) => RateHz = rateHz;

    /// <summary>Samples per second (config "stats.rate_hz", 0.2 to 4); applies from the next tick.</summary>
    public double RateHz
    {
        get => 1000 / _period.TotalMilliseconds;
        set => _period = TimeSpan.FromMilliseconds(1000 / Math.Clamp(value, 0.2, 4));
    }

    /// <summary>Raised on the sampling thread after every tick.</summary>
    public event Action? Sampled;

    public SensorAccess Access { get; private set; } = new(false, false, false, null);

    /// <summary>Sources the device layout shows; only these (and what they need) are sampled normally.</summary>
    public IReadOnlyCollection<string> Referenced { get; set; } = [];

    /// <summary>Called by the web API: sample every source for the next few seconds.</summary>
    public void WantCatalog() => Interlocked.Exchange(ref _catalogWantedAt, Stopwatch.GetTimestamp());

    public IReadOnlyList<StatSource> Catalog
    {
        get
        {
            lock (_lock)
            {
                return _catalog;
            }
        }
    }

    /// <summary>Latest value per source id; null when the source has no value right now.</summary>
    public double? Value(string id)
    {
        lock (_lock)
        {
            return _values.TryGetValue(id, out var v) ? v : null;
        }
    }

    public IEnumerable<object> HardwareReport() => _hw.Report();

    public bool Has(string id)
    {
        lock (_lock)
        {
            return _values.ContainsKey(id);
        }
    }

    public void Start()
    {
        _thread = new Thread(Run) { IsBackground = true, Name = "SystemStats", Priority = ThreadPriority.BelowNormal };
        _thread.Start();
    }

    private void Run()
    {
        var sw = Stopwatch.StartNew();
        OpenCounters();
        _nvml = Nvml.Open();
        _engines = GpuEngines.Open(_nvml?.Pci);
        // LibreHardwareMonitor's GPU part only when the direct readers cannot cover the GPU.
        _hw.Open(gpu: _nvml is null || _engines is null);
        Log.Info($"stats: NVML {(_nvml is null ? "not available" : $"\"{_nvml.Name}\"")}, " +
                 $"GPU engines {(_engines is null ? "not available" : $"\"{_engines.Name}\" ({_engines.NodeCount} nodes)")}");
        var catalog = BuildCatalog();
        lock (_lock)
        {
            _catalog = catalog;
        }
        Access = CheckAccess();
        Log.Info($"stats: {catalog.Count} sources, ready in {sw.ElapsedMilliseconds} ms" +
                 (Access.Message is null ? "" : $"; {Access.Message}"));

        var ct = _cts.Token;
        while (!ct.WaitHandle.WaitOne(_period))
        {
            try
            {
                Sample(catalog);
            }
            catch (Exception ex)
            {
                Log.Warn($"stats: sample failed: {ex.GetType().Name}: {ex.Message}");
            }
            Sampled?.Invoke();
        }
    }

    private void Sample(List<StatSource> catalog)
    {
        var wantedAt = Interlocked.Read(ref _catalogWantedAt);
        var all = wantedAt != 0 && Stopwatch.GetElapsedTime(wantedAt) < CatalogWindow;
        var referenced = Referenced;
        var active = all ? catalog : catalog.Where(s => referenced.Contains(s.Id)).ToList();
        var needs = active.Aggregate(Needs.None, (n, s) => n | s.Needs);

        // Open LibreHardwareMonitor's GPU part only for the layout (e.g. hot spot temperature), never
        // just because the web page lists every source: once open, its vendor DLLs stay loaded.
        if (!_hw.GpuEnabled && catalog.Any(src => src.Needs.HasFlag(Needs.GpuSensors) && referenced.Contains(src.Id)))
        {
            _hw.EnableGpu();
        }
        _hw.Update(cpu: needs.HasFlag(Needs.CpuSensors), gpu: needs.HasFlag(Needs.GpuSensors),
            board: needs.HasFlag(Needs.BoardSensors));
        if (needs.HasFlag(Needs.CpuCounters))
        {
            SampleCpu();
        }
        if (needs.HasFlag(Needs.Network))
        {
            SampleNetwork();
        }
        if (needs.HasFlag(Needs.Nvml))
        {
            _nvml?.Sample();
        }
        if (needs.HasFlag(Needs.Engines))
        {
            _engines?.Sample();
        }
        Dictionary<string, double?> values;
        lock (_lock)
        {
            values = new Dictionary<string, double?>(_values);  // sources not sampled keep their last value
        }
        foreach (var s in active)
        {
            double? v;
            try
            {
                v = s.Read();
            }
            catch (Exception)
            {
                v = null;
            }
            values[s.Id] = v is double d && (double.IsNaN(d) || double.IsInfinity(d)) ? null : v;
        }
        lock (_lock)
        {
            _values = values;
        }
    }

    // ---- catalog ----

    private List<StatSource> BuildCatalog()
    {
        var list = new List<StatSource>
        {
            new("sys.cpu.load", "CPU load", "%", () => _cpuLoad, Needs.CpuCounters),
            new("sys.cpu.clock_mhz", "CPU clock", "MHz", () => _cpuClock, Needs.CpuCounters),
            new("sys.cpu.temp", "CPU temperature", "°C", CpuTemp, Needs.CpuSensors),
            new("sys.cpu.power_w", "CPU power", "W", () => Positive(Sensor(_hw.Cpu, SensorType.Power, "Package", "CPU Package")),
                Needs.CpuSensors),
            new("sys.ram.used_pct", "RAM used", "%", () => Memory() is { } m ? 100.0 * (m.Total - m.Avail) / m.Total : null),
            new("sys.ram.used_gb", "RAM used", "GB", () => Memory() is { } m ? (m.Total - m.Avail) / 1073741824.0 : null),
            new("sys.ram.total_gb", "RAM total", "GB", () => Memory() is { } m ? m.Total / 1073741824.0 : null),
            new("sys.net.down_kbps", "Network download", "kB/s", () => _netDown, Needs.Network),
            new("sys.net.up_kbps", "Network upload", "kB/s", () => _netUp, Needs.Network),
        };

        var gpu = _hw.Gpu;  // null while the LibreHardwareMonitor GPU part is closed (direct readers in use)
        var nv = _nvml;
        var eng = _engines;
        const Needs L = Needs.GpuSensors;  // LibreHardwareMonitor: about 33 ms CPU per update, fallback only
        // Direct readers first (NVML for NVIDIA values, D3DKMT for engine loads), LibreHardwareMonitor otherwise.
        StatSource Nv(string id, string name, string unit, Func<Nvml, double?> direct, Func<double?> lhm) =>
            nv is not null ? new(id, name, unit, () => direct(nv), Needs.Nvml) : new(id, name, unit, lhm, L);
        StatSource Eng(string id, string name, GpuEngines.Engine type, string lhmName) =>
            eng is not null
                ? new(id, name, "%", () => eng.Max(type), Needs.Engines)
                : new(id, name, "%", () => Sensor(gpu, SensorType.Load, lhmName), L);

        // Listed even without a GPU so layouts using them show "--" instead of an error.
        // GPU load is Task Manager's number: the busiest engine (3D, video decode, copy, ...).
        // NVIDIA's own core load only covers the graphics engine (1 % while a video decodes).
        list.Add(eng is not null
            ? new("sys.gpu.load", "GPU load (busiest engine, as Task Manager)", "%", () => eng.Max(), Needs.Engines)
            : new("sys.gpu.load", "GPU load (busiest engine, as Task Manager)", "%",
                () => MaxD3DLoad(gpu) ?? Sensor(gpu, SensorType.Load, "GPU Core"), L));
        list.Add(Nv("sys.gpu.temp", "GPU temperature", "\u00b0C", n => n.Temperature,
            () => Positive(Sensor(gpu, SensorType.Temperature, "GPU Core"))));
        if (gpu is not null || nv is not null || eng is not null)
        {
            list.Add(Nv("sys.gpu.core_load", "GPU core load (driver)", "%", n => n.CoreLoad,
                () => Sensor(gpu, SensorType.Load, "GPU Core")));
            list.Add(Eng("sys.gpu.3d_load", "GPU 3D engine", GpuEngines.Engine.ThreeD, "D3D 3D"));
            list.Add(Eng("sys.gpu.decode_load", "GPU video decode", GpuEngines.Engine.VideoDecode, "D3D Video Decode"));
            list.Add(Eng("sys.gpu.encode_load", "GPU video encode", GpuEngines.Engine.VideoEncode, "D3D Video Encode"));
            list.Add(Nv("sys.gpu.clock_mhz", "GPU clock", "MHz", n => n.ClockMhz, () => Sensor(gpu, SensorType.Clock, "GPU Core")));
            list.Add(Nv("sys.gpu.power_w", "GPU power", "W", n => n.PowerW,
                () => Sensor(gpu, SensorType.Power, "GPU Package", "GPU Power")));
            list.Add(Nv("sys.gpu.vram_used_gb", "GPU memory used", "GB", n => n.VramUsedGb,
                () => Sensor(gpu, SensorType.SmallData, "GPU Memory Used", "D3D Dedicated Memory Used") / 1024.0));
            list.Add(Nv("sys.gpu.vram_pct", "GPU memory used", "%", n => n.VramPct, () =>
                Sensor(gpu, SensorType.SmallData, "GPU Memory Used") is { } used &&
                Sensor(gpu, SensorType.SmallData, "GPU Memory Total") is { } total and > 0
                    ? 100.0 * used / total
                    : null));
            list.Add(Nv("sys.gpu.fan_pct", "GPU fan", "%", n => n.FanPct, () => Sensor(gpu, SensorType.Control, "GPU Fan", "GPU Fan 1")));
            list.Add(Nv("sys.gpu.fan_rpm", "GPU fan", "RPM", n => n.FanRpm, () => Sensor(gpu, SensorType.Fan, "GPU Fan", "GPU Fan 1")));
        }
        if (gpu is not null || nv is not null)
        {
            // Not in NVML's public API: only through LibreHardwareMonitor, whose GPU part opens the
            // first time the layout uses this source and is refreshed only while it is used.
            list.Add(new("sys.gpu.hotspot_temp", "GPU hot spot", "\u00b0C",
                () => Positive(Sensor(_hw.Gpu, SensorType.Temperature, "GPU Hot Spot")), L));
        }

        // Fan ids follow the motherboard header ("Fan #3" is sys.fan.3.rpm), not the order found.
        var used = new HashSet<int>();
        for (var i = 0; i < _hw.Fans.Count; i++)
        {
            var fan = _hw.Fans[i];
            var hash = fan.Name.LastIndexOf('#');
            var n = hash >= 0 && int.TryParse(fan.Name[(hash + 1)..], out var parsed) ? parsed : fan.Index + 1;
            while (!used.Add(n))
            {
                n++;  // a second SuperIO chip with the same names: keep ids unique
            }
            list.Add(new($"sys.fan.{n}.rpm", $"Motherboard fan {n}", "RPM", () => fan.Value, Needs.BoardSensors));
        }

        foreach (var drive in DriveInfo.GetDrives())
        {
            if (drive.DriveType != DriveType.Fixed || !drive.IsReady || drive.Name.Length < 1)
            {
                continue;
            }
            var letter = drive.Name[..1].ToUpperInvariant();
            var d = drive;
            list.Add(new($"sys.disk.{letter}.used_pct", $"Disk {letter}: used", "%",
                () => d.IsReady ? 100.0 * (d.TotalSize - d.TotalFreeSpace) / d.TotalSize : null));
            list.Add(new($"sys.disk.{letter}.free_gb", $"Disk {letter}: free", "GB",
                () => d.IsReady ? d.TotalFreeSpace / 1073741824.0 : null));
        }
        return list;
    }

    private static double? Sensor(IHardware? hw, SensorType type, params string[] names) =>
        HardwareSensors.Find(hw, type, names)?.Value;

    private static double? MaxD3DLoad(IHardware? gpu) => gpu?.Sensors
        .Where(s => s.SensorType == SensorType.Load && s.Name.StartsWith("D3D ", StringComparison.Ordinal))
        .Select(s => (double?)s.Value)
        .Max();

    // Without the PawnIO driver (or admin) LibreHardwareMonitor reports unreadable CPU sensors as 0,
    // not null. No PC part runs at 0 degrees or 0 W, so treat that as "no value".
    private static double? Positive(double? v) => v > 0 ? v : null;

    private double? CpuTemp() => Positive(Sensor(_hw.Cpu, SensorType.Temperature,
        "Core (Tctl/Tdie)", "CPU Package", "Core (Tctl)", "Core (Tdie)", "Core Average"));

    // ---- access (admin, PawnIO) ----

    private SensorAccess CheckAccess()
    {
        bool admin;
        using (var id = WindowsIdentity.GetCurrent())
        {
            admin = new WindowsPrincipal(id).IsInRole(WindowsBuiltInRole.Administrator);
        }
        bool pawnIo;
        using (var key = Registry.LocalMachine.OpenSubKey(@"SYSTEM\CurrentControlSet\Services\PawnIO"))
        {
            pawnIo = key is not null;
        }
        _hw.Update(cpu: true, gpu: false, board: false);
        var cpuTemp = CpuTemp() is not null;
        string? message = null;
        if (!cpuTemp)
        {
            message = (pawnIo, admin) switch
            {
                (false, _) => "CPU temperature and motherboard fans need the PawnIO driver (pawnio.eu) and admin rights",
                (true, false) => "CPU temperature and motherboard fans need admin rights: use \"Start with Windows\" (runs elevated)",
                _ => "CPU temperature is not available on this CPU",
            };
        }
        return new SensorAccess(admin, pawnIo, cpuTemp, message);
    }

    // ---- Windows counters ----

    private void OpenCounters()
    {
        try
        {
            // "% Processor Utility" is what Task Manager shows as CPU utilization (frequency aware).
            _cpuUtility = new PerformanceCounter("Processor Information", "% Processor Utility", "_Total", readOnly: true);
            _cpuPerformance = new PerformanceCounter("Processor Information", "% Processor Performance", "_Total", readOnly: true);
            _cpuFrequency = new PerformanceCounter("Processor Information", "Processor Frequency", "_Total", readOnly: true);
            _cpuUtility.NextValue();
            _cpuPerformance.NextValue();
        }
        catch (Exception ex) when (ex is InvalidOperationException or UnauthorizedAccessException or Win32Exception)
        {
            Log.Warn($"stats: CPU performance counters unavailable ({ex.Message}), using GetSystemTimes");
            _cpuUtility = _cpuPerformance = _cpuFrequency = null;
        }
        _cpuTimes = CpuTimes.Now();
        (_netRx, _netTx) = NetworkBytes();
        _netAt = Stopwatch.GetTimestamp();
    }

    private void SampleCpu()
    {
        if (_cpuUtility is not null)
        {
            try
            {
                _cpuLoad = Math.Clamp(_cpuUtility.NextValue(), 0, 100);
                _cpuClock = _cpuFrequency!.NextValue() * _cpuPerformance!.NextValue() / 100.0;
                return;
            }
            catch (InvalidOperationException)
            {
                _cpuUtility = null;  // counters went away; fall back below
            }
        }
        var now = CpuTimes.Now();
        var total = (now.Kernel - _cpuTimes.Kernel) + (now.User - _cpuTimes.User);  // kernel includes idle
        var idle = now.Idle - _cpuTimes.Idle;
        _cpuTimes = now;
        _cpuLoad = total > 0 ? Math.Clamp(100.0 * (total - idle) / total, 0, 100) : null;
        _cpuClock = null;
    }

    private void SampleNetwork()
    {
        var (rx, tx) = NetworkBytes();
        var at = Stopwatch.GetTimestamp();
        var dt = Stopwatch.GetElapsedTime(_netAt, at).TotalSeconds;
        if (dt > 0.05)
        {
            // Counters can drop when an adapter disappears; never report negative traffic.
            _netDown = Math.Max(0, rx - _netRx) / dt / 1000.0;
            _netUp = Math.Max(0, tx - _netTx) / dt / 1000.0;
        }
        (_netRx, _netTx, _netAt) = (rx, tx, at);
    }

    // Enumerating adapters costs about 6 ms of CPU; reading counters of known adapters is cheap.
    private static readonly TimeSpan NicRefresh = TimeSpan.FromSeconds(30);
    private NetworkInterface[] _nics = [];
    private long _nicsAt;

    /// <summary>Bytes received/sent on physical, connected adapters (no loopback, tunnels or virtual switches).</summary>
    private (long Rx, long Tx) NetworkBytes()
    {
        if (_nicsAt == 0 || Stopwatch.GetElapsedTime(_nicsAt) > NicRefresh)
        {
            _nics = NetworkInterface.GetAllNetworkInterfaces().Where(nic =>
                nic.OperationalStatus == OperationalStatus.Up &&
                nic.NetworkInterfaceType is not (NetworkInterfaceType.Loopback or NetworkInterfaceType.Tunnel) &&
                !nic.Description.Contains("Virtual", StringComparison.OrdinalIgnoreCase) &&
                !nic.Description.Contains("Hyper-V", StringComparison.OrdinalIgnoreCase) &&
                !nic.Name.StartsWith("vEthernet", StringComparison.OrdinalIgnoreCase)).ToArray();
            _nicsAt = Stopwatch.GetTimestamp();
        }
        long rx = 0, tx = 0;
        foreach (var nic in _nics)
        {
            try
            {
                var s = nic.GetIPStatistics();
                rx += s.BytesReceived;
                tx += s.BytesSent;
            }
            catch (NetworkInformationException)
            {
                _nicsAt = 0;  // adapter went away; re-enumerate next time
            }
        }
        return (rx, tx);
    }

    private static (ulong Total, ulong Avail)? Memory()
    {
        var m = new MemoryStatusEx { Length = (uint)Marshal.SizeOf<MemoryStatusEx>() };
        return GlobalMemoryStatusEx(ref m) && m.TotalPhys > 0 ? (m.TotalPhys, m.AvailPhys) : null;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct MemoryStatusEx
    {
        public uint Length;
        public uint MemoryLoad;
        public ulong TotalPhys;
        public ulong AvailPhys;
        public ulong TotalPageFile;
        public ulong AvailPageFile;
        public ulong TotalVirtual;
        public ulong AvailVirtual;
        public ulong AvailExtendedVirtual;
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GlobalMemoryStatusEx(ref MemoryStatusEx buffer);

    private readonly record struct CpuTimes(long Idle, long Kernel, long User)
    {
        public static CpuTimes Now() =>
            GetSystemTimes(out var idle, out var kernel, out var user) ? new(idle, kernel, user) : default;
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetSystemTimes(out long idle, out long kernel, out long user);

    public void Dispose()
    {
        _cts.Cancel();
        _thread?.Join(TimeSpan.FromSeconds(2));
        _hw.Dispose();
        _nvml?.Dispose();
        _cpuUtility?.Dispose();
        _cpuPerformance?.Dispose();
        _cpuFrequency?.Dispose();
        _cts.Dispose();
    }
}
