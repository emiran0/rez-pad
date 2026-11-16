using System.Buffers.Binary;
using System.Globalization;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace RezPad.Protocol;

/// <summary>
/// Widget catalog, actions and data source defaults of config schema 1 (docs/config-schema.md).
/// Mirrors firmware/lib/config_logic; both are tested against docs/config-test-vectors.json.
/// </summary>
public static class ConfigCatalog
{
    public const int Schema = 1;
    public const int MaxPages = 8;
    public const int MaxWidgets = 16;
    public const int MaxGrid = 8;
    public const int MaxConfigBytes = 32768;

    /// <summary>Different data sources over all pages (the device keeps 128 SYS_STATS values).</summary>
    public const int MaxSources = 128;

    public const int MaxButtons = 4;
    public const int MaxKnobs = 3;
    public static readonly string[] Knobs = ["enc1", "enc2", "enc3"];
    public const int MaxStatusItems = 3;
    public const int MaxMacros = 32;
    public const int MaxMacroSteps = 32;

    // Screen geometry (firmware/src/ui/pages.cpp uses the same numbers). The screen size comes from
    // display.panel and display.orientation; the grid is the screen below the status bar.
    public const int StatusH = 20;
    public const int CellGap = 4;
    public const int CardPad = 7;

    /// <summary>A screen in px; the widget grid is the area below the status bar.</summary>
    public readonly record struct Screen(int W, int H)
    {
        public int GridH => H - StatusH;
    }

    /// <summary>display.panel values with their native (portrait) size, in the firmware's order.</summary>
    public static readonly IReadOnlyDictionary<string, (int W, int H)> Panels = new Dictionary<string, (int, int)>
    {
        ["ili9488"] = (320, 480),
        ["st7789"] = (240, 320),
    };

    /// <summary>display.orientation values, in the firmware's order.</summary>
    public static readonly IReadOnlyList<string> Orientations = ["landscape", "portrait", "landscape_flipped", "portrait_flipped"];

    public static readonly Screen DefaultScreen = new(480, 320);

    /// <summary>Screen size for a panel and orientation (unknown values fall back to the defaults).</summary>
    public static Screen ScreenOf(string? panel, string? orientation)
    {
        var (w, h) = Panels.TryGetValue(panel ?? "ili9488", out var s) ? s : Panels["ili9488"];
        var landscape = orientation is null or "landscape" or "landscape_flipped" || !Orientations.Contains(orientation);
        return landscape ? new Screen(h, w) : new Screen(w, h);
    }

    /// <summary>The screen a config is for (its display.panel and display.orientation).</summary>
    public static Screen ScreenOf(JsonNode? root) =>
        ScreenOf((string?)(root?["display"]?["panel"] as JsonValue), (string?)(root?["display"]?["orientation"] as JsonValue));

    /// <summary>
    /// Smallest content area (card minus padding) in px each widget type can draw in. Any size whose
    /// content area is at least this big is allowed, on any grid.
    /// </summary>
    public static readonly IReadOnlyDictionary<string, (int W, int H)> WidgetMin = new Dictionary<string, (int, int)>
    {
        ["album_art"] = (40, 40),
        ["track_info"] = (80, 18),
        ["media_controls"] = (60, 24),
        ["progress"] = (60, 10),
        ["volume"] = (40, 34),
        ["now_playing"] = (150, 60),
        ["metric_value"] = (50, 30),
        ["metric_bar"] = (60, 30),
        ["metric_gauge"] = (44, 44),
        ["metric_graph"] = (80, 40),
        ["clock"] = (50, 26),
        ["room_climate"] = (60, 30),
        ["weather_now"] = (60, 34),
        ["weather_forecast"] = (90, 46),
        ["label"] = (16, 14),
        ["image"] = (16, 16),
        ["spectrum"] = (60, 30),
        ["server"] = (110, 60),
        ["mixer"] = (110, 50),
    };

    /// <summary>Theme presets, in the firmware's order.</summary>
    public static readonly IReadOnlyList<string> Themes = ["dark", "midnight", "black", "graphite"];

    public static int CardW(int w, int cols, Screen s) => w * (s.W / cols) - CellGap;

    public static int CardH(int h, int rows, Screen s) => h * (s.GridH / rows) - CellGap;

    /// <summary>Whether a w x h widget of this type fits its minimum content size on a cols x rows grid.</summary>
    public static bool SizeAllowed(string type, int w, int h, int cols, int rows, Screen s) =>
        WidgetMin.TryGetValue(type, out var min) &&
        CardW(w, cols, s) - 2 * CardPad >= min.W && CardH(h, rows, s) - 2 * CardPad >= min.H;

    public static readonly IReadOnlyList<string> Actions =
    [
        "none", "nav.page", "nav.next_page", "nav.prev_page", "nav.home", "media.play_pause", "media.next",
        "media.prev", "media.seek", "media.volume", "media.mute", "display.toggle", "display.brightness",
        "media.app_volume", "media.app_mute", "mixer.focus", "media.app_switch", "media.app_next",
    ];

    /// <summary>Actions that take a detent count: allowed on rotate gestures only, the others on presses only.</summary>
    public static readonly IReadOnlySet<string> RotateActions =
        new HashSet<string> { "nav.page", "media.seek", "media.volume", "display.brightness", "media.app_volume", "media.app_switch" };

    public static bool IsMetric(string type) => type.StartsWith("metric_", StringComparison.Ordinal);

    public sealed record SourceStyle(string Unit, double Min, double Max, double? Warn, double? Crit);

    /// <summary>Unit, range and thresholds a widget gets when it does not set them (same table as the firmware).</summary>
    public static SourceStyle SourceDefaults(string? s)
    {
        if (string.IsNullOrEmpty(s))
        {
            return new("", 0, 100, null, null);
        }
        if (s.Contains(".temp", StringComparison.Ordinal) || s.Contains("_temp", StringComparison.Ordinal))
        {
            return new("°C", 20, 100, 75, 90);
        }
        if (s.EndsWith(".load", StringComparison.Ordinal) || s.EndsWith("_load", StringComparison.Ordinal))
        {
            return new("%", 0, 100, 80, 95);
        }
        if (s.EndsWith("used_pct", StringComparison.Ordinal) || s.EndsWith("vram_pct", StringComparison.Ordinal))
        {
            return s.StartsWith("sys.disk.", StringComparison.Ordinal) ? new("%", 0, 100, 90, 97) : new("%", 0, 100, 85, 95);
        }
        if (s.EndsWith("_pct", StringComparison.Ordinal) || s == "room.hum")
        {
            return new("%", 0, 100, null, null);
        }
        if (s.EndsWith("_kbps", StringComparison.Ordinal))
        {
            return new("kB/s", 0, 10000, null, null);
        }
        if (s.EndsWith("_mhz", StringComparison.Ordinal))
        {
            return new("MHz", 0, 6000, null, null);
        }
        if (s.EndsWith("_w", StringComparison.Ordinal))
        {
            return new("W", 0, 300, null, null);
        }
        if (s.EndsWith("_gb", StringComparison.Ordinal))
        {
            return new("GB", 0, 64, null, null);
        }
        if (s.EndsWith(".rpm", StringComparison.Ordinal) || s.EndsWith("_rpm", StringComparison.Ordinal))
        {
            return new("rpm", 0, 3000, null, null);
        }
        return s == "room.pressure" ? new("hPa", 950, 1050, null, null) : new("", 0, 100, null, null);
    }

    /// <summary>Data source ids the config's pages show (metric widgets), in page order, no duplicates.</summary>
    public static IReadOnlyList<string> ReferencedSources(JsonNode? root)
    {
        var list = new List<string>();
        // Status bar items of type "metric" first: they show on every page.
        foreach (var item in (root?["status_bar"]?["items"] as JsonArray) ?? [])
        {
            if (item?["type"] is JsonValue t && t.TryGetValue<string>(out var type) && type == "metric" &&
                item["source"] is JsonValue s && s.TryGetValue<string>(out var id) && id.Length > 0 && !list.Contains(id))
            {
                list.Add(id);
            }
        }
        foreach (var page in (root?["pages"] as JsonArray) ?? [])
        {
            foreach (var w in (page?["widgets"] as JsonArray) ?? [])
            {
                var type = w?["type"] is JsonValue t && t.TryGetValue<string>(out var tv) ? tv : "";
                if (IsMetric(type) && w!["source"] is JsonValue s && s.TryGetValue<string>(out var id) && id.Length > 0 && !list.Contains(id))
                {
                    list.Add(id);
                }
                else if (type == "server" && w?["system"] is JsonValue sys && sys.TryGetValue<string>(out var slug) && slug.Length > 0)
                {
                    foreach (var m in ServerMetrics)
                    {
                        var sid = $"{ExternalIds.BeszelPrefix}{slug}.{m}";
                        if (!list.Contains(sid))
                        {
                            list.Add(sid);
                        }
                    }
                }
            }
        }
        return list;
    }

    /// <summary>Sources a server card (Beszel machine) shows: bz.&lt;system&gt;.&lt;metric&gt;. Same list as the firmware's kServerMetrics.</summary>
    public static readonly IReadOnlyList<string> ServerMetrics =
        ["online", "cpu_load", "mem_used_pct", "disk_used_pct", "load1", "temp", "net_rx_kbps", "net_tx_kbps"];

    /// <summary>App ids pinned by mixer widgets ("apps"), in order, without repeats.</summary>
    public static IReadOnlyList<string> MixerPins(JsonNode? root)
    {
        var list = new List<string>();
        foreach (var page in (root?["pages"] as JsonArray) ?? [])
        {
            foreach (var w in (page?["widgets"] as JsonArray) ?? [])
            {
                if (w?["type"] is JsonValue t && t.TryGetValue<string>(out var type) && type == "mixer")
                {
                    foreach (var a in (w["apps"] as JsonArray) ?? [])
                    {
                        if (a is JsonValue v && v.TryGetValue<string>(out var id) && !list.Contains(id))
                        {
                            list.Add(id);
                        }
                    }
                }
            }
        }
        return list;
    }

    /// <summary>True when any page has a widget of <paramref name="type"/>.</summary>
    public static bool HasWidget(JsonNode? root, string type)
    {
        foreach (var page in (root?["pages"] as JsonArray) ?? [])
        {
            foreach (var w in (page?["widgets"] as JsonArray) ?? [])
            {
                if (w?["type"] is JsonValue t && t.TryGetValue<string>(out var value) && value == type)
                {
                    return true;
                }
            }
        }
        return false;
    }
}

/// <summary>
/// Validates a config (schema 1). Returns the first error as "path: reason" (path like
/// "pages[1].widgets[2].source"), the same paths the firmware reports, or null when valid. Host-only
/// sections (weather, stats, media.app_priority, media.art_size) are checked too; the device ignores them.
/// </summary>
public static class ConfigValidator
{
    private sealed class Invalid(string message) : Exception(message);

    public static string? Validate(JsonNode? root, bool hostSections = true)
    {
        try
        {
            Check(root, hostSections);
            return null;
        }
        catch (Invalid e)
        {
            return e.Message;
        }
    }

    public static string? Validate(ReadOnlySpan<byte> utf8, bool hostSections = true)
    {
        if (utf8.Length > ConfigCatalog.MaxConfigBytes)
        {
            return $"config: too large (max {ConfigCatalog.MaxConfigBytes} bytes)";
        }
        JsonNode? root;
        try
        {
            root = JsonNode.Parse(utf8);
        }
        catch (JsonException e)
        {
            return $"config: invalid JSON ({e.Message})";
        }
        return Validate(root, hostSections);
    }

    private static Invalid Fail(string path, string reason) => new($"{path}: {reason}");

    private static string P(string parent, string key) => parent.Length == 0 ? key : $"{parent}.{key}";

    private static void Check(JsonNode? root, bool host)
    {
        if (root is not JsonObject o)
        {
            throw Fail("config", "must be a JSON object");
        }
        if (Int(o, "schema", "", 0, int.MaxValue) != ConfigCatalog.Schema)
        {
            throw Fail("schema", $"must be {ConfigCatalog.Schema}");
        }

        if (Obj(o, "display", "") is { } d)
        {
            Int(d, "brightness", "display", 0, 100);
            if (Obj(d, "night", "display") is { } n)
            {
                Bool(n, "enabled", "display.night");
                Time(n, "from", "display.night");
                Time(n, "to", "display.night");
                Int(n, "brightness", "display.night", 0, 100);
            }
            Bool(d, "sleep_on_pc_sleep", "display");
            Bool(d, "knob_popup", "display");
            Choice(d, "panel", "display", [.. ConfigCatalog.Panels.Keys]);
            Choice(d, "orientation", "display", [.. ConfigCatalog.Orientations]);
            Int(d, "spi_hz", "display", 1_000_000, 80_000_000);
        }

        var screen = ConfigCatalog.ScreenOf(o);
        var cols = 4;
        var rows = 3;
        if (Obj(o, "grid", "") is { } g)
        {
            cols = Int(g, "cols", "grid", 1, ConfigCatalog.MaxGrid) ?? cols;
            rows = Int(g, "rows", "grid", 1, ConfigCatalog.MaxGrid) ?? rows;
        }

        if (Obj(o, "theme", "") is { } t)
        {
            Theme(t, "theme");
        }

        if (Obj(o, "units", "") is { } u)
        {
            Choice(u, "temp", "units", "c", "f");
            Choice(u, "wind", "units", "kmh", "ms", "mph");
            Choice(u, "pressure", "units", "hpa", "inhg");
        }

        if (host && Obj(o, "weather", "") is { } w)
        {
            var lat = Num(w, "lat", "weather");
            var lon = Num(w, "lon", "weather");
            if (lat is < -90 or > 90)
            {
                throw Fail("weather.lat", "must be a number from -90 to 90");
            }
            if (lon is < -180 or > 180)
            {
                throw Fail("weather.lon", "must be a number from -180 to 180");
            }
            if ((lat is null) != (lon is null))
            {
                throw Fail("weather", "lat and lon must both be set or both be null");
            }
            Str(w, "name", "weather", 101);
            Int(w, "refresh_min", "weather", 5, 180);
        }

        if (Obj(o, "sensor", "") is { } se)
        {
            Range(se, "temp_offset_c", "sensor", -10, 10);
            Range(se, "hum_offset_pct", "sensor", -20, 20);
            Range(se, "trend_threshold_hpa", "sensor", 0.1, 10);
        }

        if (Obj(o, "media", "") is { } m)
        {
            Int(m, "volume_step", "media", 1, 20);
            Int(m, "seek_step_s", "media", 1, 60);
            if (host)
            {
                Int(m, "art_size", "media", 32, 480);
                if (m["app_priority"] is { } ap)
                {
                    if (ap is not JsonArray arr || arr.Count > 16 ||
                        arr.Any(x => x is not JsonValue v || !v.TryGetValue<string>(out var s) || s.Length is 0 or > 64))
                    {
                        throw Fail("media.app_priority", "must be a list of up to 16 app names");
                    }
                }
            }
        }

        if (host && Obj(o, "stats", "") is { } st)
        {
            Range(st, "rate_hz", "stats", 0.2, 4);
        }

        var macroIds = Macros(o, host);

        if (Obj(o, "inputs", "") is { } inputs)
        {
            Int(inputs, "buttons", "inputs", 0, ConfigCatalog.MaxButtons);
            Ports(inputs, "knob_ports", ConfigCatalog.MaxKnobs);
            Ports(inputs, "button_ports", ConfigCatalog.MaxButtons);
            foreach (var enc in ConfigCatalog.Knobs)
            {
                if (Obj(inputs, enc, "inputs") is { } e)
                {
                    Int(e, "counts_per_detent", P("inputs", enc), 1, 8);
                    Bool(e, "invert", P("inputs", enc));
                }
            }
            Gestures(inputs, "inputs", macroIds);
        }

        StatusBar(o);

        if (o["pages"] is not JsonArray pages)
        {
            throw Fail("pages", "required, must be an array");
        }
        if (pages.Count is < 1 or > ConfigCatalog.MaxPages)
        {
            throw Fail("pages", $"must have 1 to {ConfigCatalog.MaxPages} pages");
        }
        var ids = new List<string>();
        for (var i = 0; i < pages.Count; i++)
        {
            Page(pages[i], $"pages[{i}]", cols, rows, screen, ids, macroIds);
        }
        if (ConfigCatalog.ReferencedSources(o).Count > ConfigCatalog.MaxSources)
        {
            throw Fail("pages", $"at most {ConfigCatalog.MaxSources} different data sources");
        }
    }

    public static readonly IReadOnlyList<string> StatusTypes = ["date", "room_temp", "room_hum", "outdoor_temp", "volume", "metric"];

    private static void StatusBar(JsonObject o)
    {
        if (Obj(o, "status_bar", "") is not { } sb || sb["items"] is null)
        {
            return;
        }
        if (sb["items"] is not JsonArray items)
        {
            throw Fail("status_bar.items", "must be an array");
        }
        if (items.Count > ConfigCatalog.MaxStatusItems)
        {
            throw Fail("status_bar.items", $"at most {ConfigCatalog.MaxStatusItems} items");
        }
        for (var i = 0; i < items.Count; i++)
        {
            var path = $"status_bar.items[{i}]";
            if (items[i] is not JsonObject it)
            {
                throw Fail(path, "must be an object");
            }
            if (it["type"] is null)
            {
                throw Fail(P(path, "type"), "required");
            }
            Choice(it, "type", path, [.. StatusTypes]);
            var type = it["type"]!.GetValue<string>();
            var source = Str(it, "source", path, 40) ?? "";
            if (type == "metric" && source.Length == 0)
            {
                throw Fail(P(path, "source"), "required for metric");
            }
            Str(it, "label", path, 8);
        }
    }

    private static void Page(JsonNode? node, string path, int cols, int rows, ConfigCatalog.Screen screen, List<string> ids,
        List<string> macroIds)
    {
        if (node is not JsonObject p)
        {
            throw Fail(path, "must be an object");
        }
        var id = Str(p, "id", path, 24) ?? "";
        if (id.Length == 0)
        {
            throw Fail(P(path, "id"), "required, must not be empty");
        }
        if (ids.Contains(id))
        {
            throw Fail(P(path, "id"), $"duplicate page id \"{id}\"");
        }
        ids.Add(id);
        Str(p, "name", path, 32);
        if (Obj(p, "grid", path) is { } g)
        {
            cols = Int(g, "cols", P(path, "grid"), 1, ConfigCatalog.MaxGrid) ?? cols;
            rows = Int(g, "rows", P(path, "grid"), 1, ConfigCatalog.MaxGrid) ?? rows;
        }
        if (Obj(p, "theme", path) is { } t)
        {
            Theme(t, P(path, "theme"));
        }

        if (p["widgets"] is { } wn)
        {
            if (wn is not JsonArray widgets)
            {
                throw Fail(P(path, "widgets"), "must be an array");
            }
            if (widgets.Count > ConfigCatalog.MaxWidgets)
            {
                throw Fail(P(path, "widgets"), $"at most {ConfigCatalog.MaxWidgets} widgets per page");
            }
            var used = new bool[ConfigCatalog.MaxGrid, ConfigCatalog.MaxGrid];
            for (var i = 0; i < widgets.Count; i++)
            {
                Widget(widgets[i], $"{path}.widgets[{i}]", cols, rows, screen, used);
            }
        }

        if (Obj(p, "inputs_override", path) is { } ov)
        {
            Gestures(ov, P(path, "inputs_override"), macroIds);
        }
    }

    private static void Widget(JsonNode? node, string path, int cols, int rows, ConfigCatalog.Screen screen, bool[,] used)
    {
        if (node is not JsonObject w)
        {
            throw Fail(path, "must be an object");
        }
        var type = Str(w, "type", path, 20);
        if (type is null || !ConfigCatalog.WidgetMin.ContainsKey(type))
        {
            throw Fail(path, $"unknown widget type \"{type}\"");
        }
        var x = Int(w, "x", path, 0, ConfigCatalog.MaxGrid - 1);
        var y = Int(w, "y", path, 0, ConfigCatalog.MaxGrid - 1);
        var ww = Int(w, "w", path, 1, ConfigCatalog.MaxGrid);
        var hh = Int(w, "h", path, 1, ConfigCatalog.MaxGrid);
        if (x is not { } cx0 || y is not { } cy0 || ww is not { } wv || hh is not { } hv)
        {
            throw Fail(path, "x, y, w and h are required");
        }
        if (cx0 + wv > cols || cy0 + hv > rows)
        {
            throw Fail(path, $"outside the {cols}x{rows} grid");
        }
        if (!ConfigCatalog.SizeAllowed(type, wv, hv, cols, rows, screen))
        {
            var need = ConfigCatalog.WidgetMin[type];
            var cw = ConfigCatalog.CardW(wv, cols, screen) - 2 * ConfigCatalog.CardPad;
            var ch = ConfigCatalog.CardH(hv, rows, screen) - 2 * ConfigCatalog.CardPad;
            throw Fail(path, $"{wv}x{hv} is too small for {type} on a {cols}x{rows} grid (needs {need.W}x{need.H} px, has {cw}x{ch})");
        }
        for (var cy = cy0; cy < cy0 + hv; cy++)
        {
            for (var cx = cx0; cx < cx0 + wv; cx++)
            {
                if (used[cy, cx])
                {
                    throw Fail(path, "overlaps another widget");
                }
            }
        }
        for (var cy = cy0; cy < cy0 + hv; cy++)
        {
            for (var cx = cx0; cx < cx0 + wv; cx++)
            {
                used[cy, cx] = true;
            }
        }

        var source = Str(w, "source", path, 40) ?? "";
        if (ConfigCatalog.IsMetric(type) && source.Length == 0)
        {
            throw Fail(P(path, "source"), $"required for {type}");
        }
        Str(w, "label", path, 32);
        Str(w, "unit", path, 12);
        if (type == "weather_forecast")
        {
            Choice(w, "mode", path, "daily", "hourly");
        }
        var text = Str(w, "text", path, 64) ?? "";
        if (type == "label" && text.Length == 0)
        {
            throw Fail(P(path, "text"), "required for label");
        }
        var asset = Str(w, "asset", path, 40) ?? "";
        if (type == "image")
        {
            if (asset.Length == 0)
            {
                throw Fail(P(path, "asset"), "required for image");
            }
            Choice(w, "fit", path, "cover", "contain");
        }
        if (type == "server")
        {
            var sys = Str(w, "system", path, 15) ?? "";
            if (sys.Length == 0 || !sys.All(ch => ch is >= 'a' and <= 'z' or >= '0' and <= '9' or '-'))
            {
                throw Fail(P(path, "system"), "required, a Beszel system id (a-z, 0-9, -)");
            }
        }
        if (type == "mixer")
        {
            Choice(w, "layout", path, "list", "faders");
            if (w["apps"] is { } appsNode)
            {
                if (appsNode is not JsonArray apps)
                {
                    throw Fail(P(path, "apps"), "must be an array");
                }
                if (apps.Count > 8)
                {
                    throw Fail(P(path, "apps"), "at most 8 apps");
                }
                var total = 0;
                for (var k = 0; k < apps.Count; k++)
                {
                    var id = apps[k] is JsonValue v && v.TryGetValue<string>(out var s) ? s : null;
                    var n = id is null ? 0 : System.Text.Encoding.UTF8.GetByteCount(id);
                    if (id is null || n == 0 || n > 23 || id.Contains(','))
                    {
                        throw Fail($"{path}.apps[{k}]", "must be an app id (1 to 23 bytes, no comma)");
                    }
                    total += n + (k > 0 ? 1 : 0);
                    if (total > 63)
                    {
                        throw Fail(P(path, "apps"), "too long (63 bytes in all)");
                    }
                }
            }
            Bool(w, "only", path);
        }
        if (type == "volume")
        {
            Choice(w, "target", path, "master", "app");
        }
        if (type == "spectrum")
        {
            Int(w, "bars", path, 4, 32);
        }
        var def = ConfigCatalog.SourceDefaults(source);
        var min = Num(w, "min", path) ?? def.Min;
        var max = Num(w, "max", path) ?? def.Max;
        Num(w, "warn", path);
        Num(w, "crit", path);
        Int(w, "decimals", path, 0, 3);
        if (!(min < max))
        {
            throw Fail(path, "min must be below max");
        }
    }

    private static void Theme(JsonObject t, string path)
    {
        Choice(t, "name", path, [.. ConfigCatalog.Themes]);
        if (t["accent"] is not null)
        {
            var a = t["accent"] is JsonValue av && av.TryGetValue<string>(out var s) ? s : "";
            if (a.Length != 7 || a[0] != '#' || !a[1..].All(Uri.IsHexDigit))
            {
                throw Fail(P(path, "accent"), "must be a color \"#RRGGBB\"");
            }
        }
    }

    // "knob_ports" / "button_ports": position = number - 1, value = physical port (1 to n), 0 = not
    // used; ports in use must differ.
    private static void Ports(JsonObject inputs, string key, int n)
    {
        var node = inputs[key];
        if (node is null)
        {
            return;
        }
        var path = P("inputs", key);
        if (node is not JsonArray arr || arr.Count > n)
        {
            throw Fail(path, $"must be an array of up to {n} port numbers");
        }
        var seen = new List<long>();
        foreach (var e in arr)
        {
            if (e is not JsonValue v || v.GetValueKind() != JsonValueKind.Number || !v.TryGetValue<long>(out var port) ||
                port < 0 || port > n)
            {
                throw Fail(path, $"port numbers are 0 (not used) to {n}");
            }
            if (port != 0 && seen.Contains(port))
            {
                throw Fail(path, $"port {port} is used twice");
            }
            seen.Add(port);
        }
    }

    private static void Gestures(JsonObject inputs, string path, List<string> macroIds)
    {
        foreach (var enc in ConfigCatalog.Knobs)
        {
            if (Obj(inputs, enc, path) is { } e)
            {
                var p = P(path, enc);
                Action(e, "rotate", p, rotate: true);
                Action(e, "rotate_pressed", p, rotate: true);
                Binding(e, "click", p, macroIds);
                Binding(e, "double", p, macroIds);
                Binding(e, "long", p, macroIds);
            }
        }
        foreach (var btn in new[] { "btn1", "btn2", "btn3", "btn4" })
        {
            if (Obj(inputs, btn, path) is { } b)
            {
                var p = P(path, btn);
                Binding(b, "click", p, macroIds);
                Binding(b, "double", p, macroIds);
                Binding(b, "long", p, macroIds);
            }
        }
    }

    // A press gesture: an action name, {"keys": "..."} or {"macro": "<id>"}.
    private static void Binding(JsonObject o, string key, string path, List<string> macroIds)
    {
        var n = o[key];
        if (n is null)
        {
            return;
        }
        if (n is JsonValue)
        {
            Action(o, key, path, rotate: false);
            return;
        }
        if (n is not JsonObject b)
        {
            throw Fail(P(path, key), "must be an action name or {keys} or {macro}");
        }
        var p = P(path, key);
        var hasKeys = b["keys"] is not null;
        var hasMacro = b["macro"] is not null;
        if (hasKeys == hasMacro)
        {
            throw Fail(p, "needs exactly one of keys or macro");
        }
        if (hasKeys)
        {
            var s = b["keys"] is JsonValue kv && kv.TryGetValue<string>(out var ks) ? ks : "";
            if (KeyCombo.Parse(s) is null)
            {
                throw Fail(P(p, "keys"), $"unknown keys \"{s}\"");
            }
        }
        else
        {
            var s = b["macro"] is JsonValue mv && mv.TryGetValue<string>(out var ms) ? ms : "";
            if (!macroIds.Contains(s))
            {
                throw Fail(P(p, "macro"), $"no macro \"{s}\"");
            }
        }
    }

    // "macros": ids (shared with the device) and, on the host, the steps.
    private static List<string> Macros(JsonObject root, bool host)
    {
        var ids = new List<string>();
        if (root["macros"] is not { } mn)
        {
            return ids;
        }
        if (mn is not JsonArray arr)
        {
            throw Fail("macros", "must be an array");
        }
        if (arr.Count > ConfigCatalog.MaxMacros)
        {
            throw Fail("macros", $"at most {ConfigCatalog.MaxMacros} macros");
        }
        for (var i = 0; i < arr.Count; i++)
        {
            var path = $"macros[{i}]";
            if (arr[i] is not JsonObject m)
            {
                throw Fail(path, "must be an object");
            }
            var id = Str(m, "id", path, 24) ?? "";
            if (id.Length == 0)
            {
                throw Fail(P(path, "id"), "required, must not be empty");
            }
            if (ids.Contains(id))
            {
                throw Fail(P(path, "id"), $"duplicate macro id \"{id}\"");
            }
            ids.Add(id);
        }
        if (!host)
        {
            return ids;
        }
        for (var i = 0; i < arr.Count; i++)
        {
            var path = $"macros[{i}]";
            var m = (JsonObject)arr[i]!;
            Str(m, "name", path, 32);
            if (m["steps"] is not JsonArray steps || steps.Count is 0 or > ConfigCatalog.MaxMacroSteps)
            {
                throw Fail(P(path, "steps"), $"required, 1 to {ConfigCatalog.MaxMacroSteps} steps");
            }
            for (var k = 0; k < steps.Count; k++)
            {
                var sp = $"{path}.steps[{k}]";
                if (steps[k] is not JsonObject step || step.Count != 1)
                {
                    throw Fail(sp, "must be one of {keys}, {text}, {wait_ms}, {open}");
                }
                if (step["keys"] is { } kn)
                {
                    var s = kn is JsonValue v && v.TryGetValue<string>(out var ks) ? ks : "";
                    if (KeyCombo.Parse(s) is null)
                    {
                        throw Fail(P(sp, "keys"), $"unknown keys \"{s}\"");
                    }
                }
                else if (step.ContainsKey("text"))
                {
                    if ((Str(step, "text", sp, 501) ?? "").Length == 0)
                    {
                        throw Fail(P(sp, "text"), "must not be empty");
                    }
                }
                else if (step.ContainsKey("wait_ms"))
                {
                    if (Int(step, "wait_ms", sp, 1, 10000) is null)
                    {
                        throw Fail(P(sp, "wait_ms"), "must be a whole number from 1 to 10000");
                    }
                }
                else if (step.ContainsKey("open"))
                {
                    if ((Str(step, "open", sp, 261) ?? "").Length == 0)
                    {
                        throw Fail(P(sp, "open"), "must not be empty");
                    }
                }
                else
                {
                    throw Fail(sp, "must be one of {keys}, {text}, {wait_ms}, {open}");
                }
            }
        }
        return ids;
    }

    // ---- typed getters: a missing or null key is fine and returns null ----

    private static JsonObject? Obj(JsonObject o, string key, string path) => o[key] switch
    {
        null => null,
        JsonObject x => x,
        _ => throw Fail(P(path, key), "must be an object"),
    };

    private static int? Int(JsonObject o, string key, string path, long lo, long hi)
    {
        var n = o[key];
        if (n is null)
        {
            return null;
        }
        if (n is not JsonValue v || v.GetValueKind() != JsonValueKind.Number || !v.TryGetValue<long>(out var l) ||
            l < lo || l > hi)
        {
            throw Fail(P(path, key), string.Create(CultureInfo.InvariantCulture, $"must be a whole number from {lo} to {hi}"));
        }
        return (int)Math.Clamp(l, int.MinValue, int.MaxValue);
    }

    private static double? Num(JsonObject o, string key, string path)
    {
        var n = o[key];
        if (n is null)
        {
            return null;
        }
        if (n is not JsonValue v || v.GetValueKind() != JsonValueKind.Number || !v.TryGetValue<double>(out var d))
        {
            throw Fail(P(path, key), "must be a number");
        }
        return d;
    }

    private static void Range(JsonObject o, string key, string path, double lo, double hi)
    {
        if (Num(o, key, path) is { } d && (d < lo || d > hi))
        {
            throw Fail(P(path, key), string.Create(CultureInfo.InvariantCulture, $"must be a number from {lo} to {hi}"));
        }
    }

    private static void Bool(JsonObject o, string key, string path)
    {
        var n = o[key];
        if (n is not null && (n is not JsonValue v || v.GetValueKind() is not (JsonValueKind.True or JsonValueKind.False)))
        {
            throw Fail(P(path, key), "must be true or false");
        }
    }

    /// <summary>String limited to cap - 1 UTF-8 bytes, like the firmware's buffers.</summary>
    private static string? Str(JsonObject o, string key, string path, int cap)
    {
        var n = o[key];
        if (n is null)
        {
            return null;
        }
        if (n is not JsonValue v || !v.TryGetValue<string>(out var s))
        {
            throw Fail(P(path, key), "must be a string");
        }
        if (Encoding.UTF8.GetByteCount(s) >= cap)
        {
            throw Fail(P(path, key), $"too long (max {cap - 1} bytes)");
        }
        return s;
    }

    private static void Choice(JsonObject o, string key, string path, params string[] options)
    {
        var n = o[key];
        if (n is null)
        {
            return;
        }
        if (n is not JsonValue v || !v.TryGetValue<string>(out var s) || !options.Contains(s))
        {
            throw Fail(P(path, key), $"must be one of {string.Join(", ", options)}");
        }
    }

    private static void Time(JsonObject o, string key, string path)
    {
        var n = o[key];
        if (n is null)
        {
            return;
        }
        var s = n is JsonValue v && v.TryGetValue<string>(out var str) ? str : "";
        var ok = s.Length == 5 && char.IsAsciiDigit(s[0]) && char.IsAsciiDigit(s[1]) && s[2] == ':' &&
                 char.IsAsciiDigit(s[3]) && char.IsAsciiDigit(s[4]) &&
                 (s[0] - '0') * 10 + (s[1] - '0') <= 23 && (s[3] - '0') * 10 + (s[4] - '0') <= 59;
        if (!ok)
        {
            throw Fail(P(path, key), "must be a time \"HH:MM\"");
        }
    }

    private static void Action(JsonObject o, string key, string path, bool rotate)
    {
        var n = o[key];
        if (n is null)
        {
            return;
        }
        if (n is not JsonValue v || !v.TryGetValue<string>(out var a) || !ConfigCatalog.Actions.Contains(a))
        {
            throw Fail(P(path, key), "unknown action");
        }
        if (a != "none" && ConfigCatalog.RotateActions.Contains(a) != rotate)
        {
            throw Fail(P(path, key), rotate ? $"{a} needs a press, not a rotation" : $"{a} needs a rotation, not a press");
        }
    }
}

/// <summary>CONFIG_BEGIN body.</summary>
public sealed record ConfigBeginMsg(int Size, uint Crc32);

/// <summary>CONFIG_ACK body. <c>Art</c>: album art edge the new layout wants (0 = none), added within v1.</summary>
public sealed record ConfigAckMsg(bool Ok, uint Crc32, string? Error, int? Art = null, bool Restart = false);

/// <summary>CONFIG_DATA_BEGIN body (reply to CONFIG_GET).</summary>
public sealed record ConfigDataBeginMsg(int Size, uint Crc32);

public static class ConfigTransfer
{
    /// <summary>The bytes the device stores and CRCs: compact UTF-8 JSON, raw non-ASCII.</summary>
    public static byte[] ToBytes(JsonNode root) =>
        Encoding.UTF8.GetBytes(root.ToJsonString(new JsonSerializerOptions
        {
            Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
        }));

    /// <summary>CONFIG_BEGIN, CONFIG_CHUNKs of up to <see cref="Frame.ChunkSize"/> bytes, CONFIG_END.</summary>
    public static IEnumerable<(MsgType Type, byte[] Body)> Packets(byte[] config)
    {
        yield return (MsgType.ConfigBegin, ProtocolJson.Serialize(new ConfigBeginMsg(config.Length, Crc32.Compute(config))));
        for (var offset = 0; offset < config.Length; offset += Frame.ChunkSize)
        {
            var n = Math.Min(Frame.ChunkSize, config.Length - offset);
            var body = new byte[4 + n];
            BinaryPrimitives.WriteUInt32LittleEndian(body, (uint)offset);
            config.AsSpan(offset, n).CopyTo(body.AsSpan(4));
            yield return (MsgType.ConfigChunk, body);
        }
        yield return (MsgType.ConfigEnd, []);
    }

    /// <summary>Reassembles CONFIG_DATA_* from the device (reply to CONFIG_GET). Chunks in order.</summary>
    public sealed class Receiver
    {
        private byte[]? _buf;
        private int _received;
        private uint _crc;

        public void Begin(ConfigDataBeginMsg msg)
        {
            _buf = msg.Size is >= 0 and <= ConfigCatalog.MaxConfigBytes ? new byte[msg.Size] : null;
            _received = 0;
            _crc = msg.Crc32;
        }

        public void Chunk(ReadOnlySpan<byte> body)
        {
            if (_buf is null || body.Length < 4)
            {
                return;
            }
            var offset = BinaryPrimitives.ReadUInt32LittleEndian(body);
            var data = body[4..];
            if (offset != _received || _received + data.Length > _buf.Length)
            {
                _buf = null;
                return;
            }
            data.CopyTo(_buf.AsSpan(_received));
            _received += data.Length;
        }

        /// <summary>The complete data with a matching CRC, or null.</summary>
        public byte[]? End()
        {
            var buf = _buf;
            _buf = null;
            return buf is not null && _received == buf.Length && Crc32.Compute(buf) == _crc ? buf : null;
        }
    }
}
