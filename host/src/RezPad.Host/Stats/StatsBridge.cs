using RezPad.Host.Config;
using RezPad.Host.Device;
using RezPad.Protocol;

namespace RezPad.Host.Stats;

/// <summary>
/// Sends SYS_STATS to the device after every sample, with only the sources the config's layout shows,
/// at the config's "stats.rate_hz".
/// </summary>
internal sealed class StatsBridge
{
    private readonly SerialLink _link;
    private readonly SystemStatsService _stats;
    private readonly ConfigService _config;
    private readonly Func<string, double?> _external;
    private IReadOnlyList<string> _sources = [];

    /// <param name="external">Values of integration sources (e.g. Beszel's bz.*), null when unknown.</param>
    public StatsBridge(SerialLink link, SystemStatsService stats, ConfigService config, Func<string, double?> external)
    {
        _link = link;
        _stats = stats;
        _config = config;
        _external = external;
        Reload();
        _config.Changed += Reload;
        _stats.Sampled += Send;
    }

    private void Reload()
    {
        var sources = _config.ReferencedSources;
        _sources = sources;
        _stats.Referenced = sources.ToHashSet();
        _stats.RateHz = _config.StatsRateHz;
    }

    private void Send()
    {
        if (!_link.IsConnected)
        {
            return;
        }
        var sources = _sources;
        var values = new Dictionary<string, double?>(sources.Count);
        foreach (var id in sources)
        {
            // One decimal is all the widgets show; keeps the message small.
            values[id] = (_stats.Value(id) ?? _external(id)) is { } v ? Math.Round(v, 1) : null;
        }
        _link.SendJson(MsgType.SysStats, new SysStatsMsg(DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(), values));
    }
}
