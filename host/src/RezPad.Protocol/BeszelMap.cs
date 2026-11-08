using System.Text.Json.Nodes;

namespace RezPad.Protocol;

/// <summary>
/// Beszel hub data (PocketBase records, checked against hub 0.21.1 with 0.17 to 0.21 agents) turned
/// into data sources <c>bz.&lt;machine&gt;.&lt;metric&gt;</c>. From a system's <c>info</c>: cpu, mp (memory %),
/// dp (disk %), la (load 1/5/15), dt (the temperature Beszel shows), u (uptime s), bb (network bytes/s;
/// older agents send b in MB/s), efs (extra filesystems, % used). From the newest one-minute
/// <c>system_stats</c> record: b ([sent, received] bytes/s), mu/du (memory/disk used GB) and t (every
/// temperature sensor). Every field is optional; missing ones give no value (the widget shows "--").
/// </summary>
public static class BeszelMap
{
    public sealed record System(string Id, string Name, string Slug, string Status);

    public sealed record Source(string Id, string Name, string Unit, string Group);

    public sealed record Result(IReadOnlyList<System> Systems, IReadOnlyList<Source> Sources, IReadOnlyDictionary<string, double?> Values);

    public const int MaxExtraFilesystems = 3;

    /// <param name="items">The systems collection's <c>items</c>.</param>
    /// <param name="minute">Newest one-minute stats record per system id (may be empty).</param>
    public static Result Compute(JsonArray items, IReadOnlyDictionary<string, JsonObject> minute)
    {
        var systems = new List<System>();
        var sources = new List<Source>();
        var values = new Dictionary<string, double?>();
        var taken = new HashSet<string>();
        foreach (var it in items.OfType<JsonObject>())
        {
            var id = (string?)it["id"] ?? "";
            var name = (string?)it["name"] ?? "?";
            var slug = ExternalIds.Slug(name, taken);
            taken.Add(slug);
            var status = (string?)it["status"] ?? "";
            systems.Add(new System(id, name, slug, status));
            var up = status == "up";
            var info = it["info"] as JsonObject;
            var stats = minute.TryGetValue(id, out var rec) ? rec["stats"] as JsonObject : null;
            var group = $"Beszel: {name}";
            var prefix = ExternalIds.BeszelPrefix + slug + ".";

            void Add(string key, string label, string unit, double? value)
            {
                sources.Add(new Source(prefix + key, $"{name} {label}", unit, group));
                values[prefix + key] = up ? value : null;
            }

            sources.Add(new Source(prefix + "online", $"{name} online (1 = up)", "", group));
            values[prefix + "online"] = up ? 1 : 0;
            Add("cpu_load", "CPU", "%", Num(info, "cpu"));
            Add("mem_used_pct", "memory", "%", Num(info, "mp"));
            Add("mem_used_gb", "memory used", "GB", Num(stats, "mu"));
            Add("disk_used_pct", "disk", "%", Num(info, "dp"));
            Add("disk_used_gb", "disk used", "GB", Num(stats, "du"));
            Add("load1", "load 1 min", "", Item(info?["la"], 0));
            Add("load5", "load 5 min", "", Item(info?["la"], 1));
            Add("load15", "load 15 min", "", Item(info?["la"], 2));
            var total = Num(info, "bb") is { } bytes ? bytes / 1000 : Num(info, "b") is { } mb ? mb * 1000 : (double?)null;
            Add("net_kbps", "network", "kB/s", total is { } t0 ? Math.Round(t0, 1) : null);
            Add("net_tx_kbps", "network up", "kB/s", Item(stats?["b"], 0) is { } tx ? Math.Round(tx / 1000, 1) : null);
            Add("net_rx_kbps", "network down", "kB/s", Item(stats?["b"], 1) is { } rx ? Math.Round(rx / 1000, 1) : null);
            Add("temp", "temperature", "°C", Num(info, "dt") is { } dt && dt > 0 ? dt : null);
            Add("cpu_temp", "CPU temperature", "°C", CpuTemp(stats?["t"] as JsonObject));
            Add("uptime_h", "uptime", "h", Num(info, "u") is { } u ? Math.Round(u / 3600, 1) : null);
            if (info?["efs"] is JsonObject efs)
            {
                var k = 0;
                foreach (var (fs, pct) in efs.OrderBy(kv => kv.Key, StringComparer.OrdinalIgnoreCase))
                {
                    if (++k > MaxExtraFilesystems)
                    {
                        break;
                    }
                    Add($"fs{k}_used_pct", $"disk {fs}", "%", pct is JsonValue v && v.TryGetValue<double>(out var d) ? d : null);
                }
            }
        }
        return new Result(systems, sources, values);
    }

    /// <summary>
    /// The CPU's temperature from the sensor map: the package sensor (Intel coretemp), Tctl/Tdie
    /// (AMD k10temp) or cpu_thermal (Raspberry Pi and other SoCs); else the hottest core; else null.
    /// </summary>
    public static double? CpuTemp(JsonObject? sensors)
    {
        if (sensors is null)
        {
            return null;
        }
        var all = sensors.Select(kv => (Key: kv.Key.ToLowerInvariant(), Value: kv.Value is JsonValue v && v.TryGetValue<double>(out var d) ? d : (double?)null))
            .Where(x => x.Value is > 0).ToList();
        foreach (var want in new[] { "package", "tctl", "tdie", "cpu_thermal", "cpu" })
        {
            if (all.FirstOrDefault(x => x.Key.Contains(want)) is { Value: { } hit })
            {
                return hit;
            }
        }
        var cores = all.Where(x => x.Key.Contains("core")).Select(x => x.Value!.Value).ToList();
        return cores.Count > 0 ? cores.Max() : null;
    }

    private static double? Num(JsonObject? o, string key) =>
        o?[key] is JsonValue v && v.TryGetValue<double>(out var d) ? d : null;

    private static double? Item(JsonNode? array, int i) =>
        array is JsonArray a && a.Count > i && a[i] is JsonValue v && v.TryGetValue<double>(out var d) ? d : null;
}
