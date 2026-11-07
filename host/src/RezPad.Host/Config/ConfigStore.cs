using System.Text.Json;
using System.Text.Json.Nodes;

namespace RezPad.Host.Config;

internal sealed record WeatherLocation(string Name, double? Lat, double? Lon, int RefreshMin)
{
    public bool IsSet => Lat is not null && Lon is not null;
}

/// <summary>The config's "units" section: temp c/f, wind kmh/ms/mph, pressure hpa/inhg.</summary>
internal sealed record Units(string Temp, string Wind, string Pressure)
{
    public static Units Default { get; } = new("c", "kmh", "hpa");

    public static bool Valid(Units u) =>
        u.Temp is "c" or "f" && u.Wind is "kmh" or "ms" or "mph" && u.Pressure is "hpa" or "inhg";
}

/// <summary>
/// The master config at %APPDATA%\rez-pad\config.json (docs/config-schema.md). For now only the
/// sections the host already uses are read and written (weather, units); everything else in the
/// file is kept as it is, so the full schema (Phase 9) can grow into the same file. Writes are
/// atomic (temp file, then replace).
/// </summary>
internal sealed class ConfigStore
{
    private static readonly JsonSerializerOptions WriteOptions = new() { WriteIndented = true };

    private readonly object _lock = new();
    private readonly string _path;
    private JsonObject _root;

    public ConfigStore(string? path = null)
    {
        _path = path ?? Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "rez-pad", "config.json");
        _root = Load();
    }

    /// <summary>Raised after a change was saved.</summary>
    public event Action? Changed;

    public string FilePath => _path;

    public WeatherLocation Weather
    {
        get
        {
            lock (_lock)
            {
                var w = _root["weather"] as JsonObject;
                return new WeatherLocation(
                    Str(w, "name") ?? "",
                    Num(w, "lat"),
                    Num(w, "lon"),
                    (int)Math.Clamp(Num(w, "refresh_min") ?? 15, 5, 180));
            }
        }
    }

    public Units Units
    {
        get
        {
            lock (_lock)
            {
                var u = _root["units"] as JsonObject;
                var units = new Units(
                    Str(u, "temp") ?? Units.Default.Temp,
                    Str(u, "wind") ?? Units.Default.Wind,
                    Str(u, "pressure") ?? Units.Default.Pressure);
                return Units.Valid(units) ? units : Units.Default;
            }
        }
    }

    public void SetWeatherLocation(string name, double lat, double lon)
    {
        lock (_lock)
        {
            var w = Section("weather");
            w["name"] = name;
            w["lat"] = Math.Round(lat, 5);
            w["lon"] = Math.Round(lon, 5);
            w["refresh_min"] ??= 15;
            Save();
        }
        Changed?.Invoke();
    }

    public void SetUnits(Units units)
    {
        if (!Units.Valid(units))
        {
            throw new ArgumentException("units: temp c|f, wind kmh|ms|mph, pressure hpa|inhg");
        }
        lock (_lock)
        {
            var u = Section("units");
            u["temp"] = units.Temp;
            u["wind"] = units.Wind;
            u["pressure"] = units.Pressure;
            Save();
        }
        Changed?.Invoke();
    }

    private JsonObject Load()
    {
        try
        {
            if (File.Exists(_path) && JsonNode.Parse(File.ReadAllText(_path)) is JsonObject root)
            {
                Log.Info($"config: loaded {_path}");
                return root;
            }
        }
        catch (Exception ex) when (ex is JsonException or IOException or UnauthorizedAccessException)
        {
            // Keep the broken file for the user to look at; start from defaults.
            Log.Warn($"config: {_path} unreadable ({ex.Message}); using defaults, old file kept as config.json.bad");
            try
            {
                File.Copy(_path, _path + ".bad", overwrite: true);
            }
            catch (IOException)
            {
            }
        }
        // Defaults for the sections the host uses so far (docs/config-schema.md). No location until
        // the user picks one: weather stays empty instead of showing a made-up place.
        return new JsonObject
        {
            ["schema"] = 1,
            ["units"] = new JsonObject { ["temp"] = "c", ["pressure"] = "hpa", ["wind"] = "kmh" },
            ["weather"] = new JsonObject { ["name"] = "", ["lat"] = null, ["lon"] = null, ["refresh_min"] = 15 },
        };
    }

    // Under _lock.
    private JsonObject Section(string name)
    {
        if (_root[name] is JsonObject o)
        {
            return o;
        }
        var created = new JsonObject();
        _root[name] = created;
        return created;
    }

    // Under _lock.
    private void Save()
    {
        Directory.CreateDirectory(Path.GetDirectoryName(_path)!);
        var tmp = _path + ".tmp";
        File.WriteAllText(tmp, _root.ToJsonString(WriteOptions));
        File.Move(tmp, _path, overwrite: true);
        Log.Info($"config: saved {_path}");
    }

    private static string? Str(JsonObject? o, string key) =>
        o?[key] is JsonValue v && v.TryGetValue<string>(out var s) ? s : null;

    private static double? Num(JsonObject? o, string key) =>
        o?[key] is JsonValue v && v.TryGetValue<double>(out var d) ? d : null;
}
