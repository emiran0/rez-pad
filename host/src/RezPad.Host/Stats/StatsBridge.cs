using RezPad.Host.Device;
using RezPad.Protocol;

namespace RezPad.Host.Stats;

/// <summary>Sends SYS_STATS to the device after every sample, with only the sources the layout shows.</summary>
internal sealed class StatsBridge
{
    /// <summary>
    /// Sources used by the default layout (firmware/src/ui/default_layout.cpp, docs/config-schema.md).
    /// Phase 9 takes them from the config instead.
    /// </summary>
    public static readonly IReadOnlyList<string> DefaultLayoutSources =
    [
        "sys.cpu.load", "sys.cpu.temp", "sys.gpu.load", "sys.gpu.temp", "sys.ram.used_pct",
        "sys.net.down_kbps", "sys.net.up_kbps",
    ];

    private readonly SerialLink _link;
    private readonly SystemStatsService _stats;
    private readonly IReadOnlyList<string> _sources;

    public StatsBridge(SerialLink link, SystemStatsService stats, IReadOnlyList<string> sources)
    {
        _link = link;
        _stats = stats;
        _sources = sources;
        _stats.Referenced = sources.ToHashSet();
        _stats.Sampled += Send;
    }

    private void Send()
    {
        if (!_link.IsConnected)
        {
            return;
        }
        var values = new Dictionary<string, double?>(_sources.Count);
        foreach (var id in _sources)
        {
            // One decimal is all the widgets show; keeps the message small.
            values[id] = _stats.Value(id) is { } v ? Math.Round(v, 1) : null;
        }
        _link.SendJson(MsgType.SysStats, new SysStatsMsg(DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(), values));
    }
}
