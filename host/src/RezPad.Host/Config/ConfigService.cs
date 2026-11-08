using System.Text.Json;
using System.Text.Json.Nodes;
using RezPad.Host.Media;
using RezPad.Protocol;

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
/// The master config at %APPDATA%\rez-pad\config.json (docs/config-schema.md, schema 1). Loads,
/// migrates and validates it (falling back to the built-in default, docs/default-config.json),
/// writes it atomically, and gives the bytes and CRC32 the device gets (ConfigSync pushes them).
/// The JSON tree is kept as it is, so keys this version does not know survive a round trip.
/// </summary>
internal sealed class ConfigService
{
    private static readonly JsonSerializerOptions WriteOptions = new()
    {
        WriteIndented = true,
        Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
    };

    private readonly object _lock = new();
    private readonly string _path;
    private JsonObject _root;
    private byte[] _bytes;

    public ConfigService(string? path = null)
    {
        _path = path ?? Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "rez-pad", "config.json");
        _root = Load();
        _bytes = ConfigTransfer.ToBytes(_root);
        Crc = Crc32.Compute(_bytes);
    }

    /// <summary>Whether an uploaded image exists (image widgets may only use those). Set by Program.</summary>
    public Func<string, bool>? AssetExists { get; set; }

    /// <summary>Raised after a change was saved (on the caller's thread).</summary>
    public event Action? Changed;

    public string FilePath => _path;

    /// <summary>CRC32 of <see cref="Bytes"/>, compared with HELLO_ACK cfg_crc.</summary>
    public uint Crc { get; private set; }

    /// <summary>The config as the device stores it (compact UTF-8 JSON).</summary>
    public byte[] Bytes
    {
        get
        {
            lock (_lock)
            {
                return _bytes;
            }
        }
    }

    /// <summary>A copy of the whole config.</summary>
    public JsonObject Snapshot()
    {
        lock (_lock)
        {
            return _root.DeepClone().AsObject();
        }
    }

    /// <summary>A copy of the whole config and its CRC, taken together.</summary>
    public (JsonObject Root, uint Crc) SnapshotWithCrc()
    {
        lock (_lock)
        {
            return (_root.DeepClone().AsObject(), Crc);
        }
    }

    public static JsonObject Default() =>
        JsonNode.Parse(typeof(ConfigService).Assembly.GetManifestResourceStream("RezPad.Host.default-config.json")!)!
            .AsObject();

    /// <summary>Validates and saves a whole new config. Returns the validation error, or null when saved.</summary>
    public string? Replace(JsonNode? config)
    {
        if (config is not JsonObject o)
        {
            return "config: must be a JSON object";
        }
        Migrate(o);
        // Validate exactly what the device will get (also turns values set from code into plain JSON).
        var bytes = ConfigTransfer.ToBytes(o);
        var error = ConfigValidator.Validate(bytes);
        if (error is not null)
        {
            return error;
        }
        if (AssetExists is { } exists &&
            AssetFormat.Placements(JsonNode.Parse(bytes)).FirstOrDefault(p => !exists(p.Asset)) is { } gone)
        {
            return $"{gone.Path}.asset: no uploaded image \"{gone.Asset}\"";
        }
        lock (_lock)
        {
            _root = JsonNode.Parse(bytes)!.AsObject();
            Save();
        }
        Changed?.Invoke();
        return null;
    }

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
                    (int)(Num(w, "refresh_min") ?? 15));
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
                return new Units(
                    Str(u, "temp") ?? Units.Default.Temp,
                    Str(u, "wind") ?? Units.Default.Wind,
                    Str(u, "pressure") ?? Units.Default.Pressure);
            }
        }
    }

    public MediaOptions Media
    {
        get
        {
            lock (_lock)
            {
                var m = _root["media"] as JsonObject;
                var d = MediaOptions.Default;
                var priority = (m?["app_priority"] as JsonArray)?.Select(x => x!.GetValue<string>()).ToList();
                return new MediaOptions(
                    priority ?? d.AppPriority,
                    (int)(Num(m, "art_size") ?? d.ArtSize),
                    (int)(Num(m, "volume_step") ?? d.VolumeStep),
                    (int)(Num(m, "seek_step_s") ?? d.SeekStepS));
            }
        }
    }

    public double StatsRateHz
    {
        get
        {
            lock (_lock)
            {
                return Num(_root["stats"] as JsonObject, "rate_hz") ?? 1;
            }
        }
    }

    public IReadOnlyList<string> MixerPins
    {
        get
        {
            lock (_lock)
            {
                return ConfigCatalog.MixerPins(_root);
            }
        }
    }

    public bool HasWidget(string type)
    {
        lock (_lock)
        {
            return ConfigCatalog.HasWidget(_root, type);
        }
    }

    /// <summary>Data sources the layout shows (metric widgets on any page).</summary>
    public IReadOnlyList<string> ReferencedSources
    {
        get
        {
            lock (_lock)
            {
                return ConfigCatalog.ReferencedSources(_root);
            }
        }
    }

    public void SetWeatherLocation(string name, double lat, double lon)
    {
        var next = Snapshot();
        var w = Section(next, "weather");
        w["name"] = name;
        w["lat"] = Math.Round(lat, 5);
        w["lon"] = Math.Round(lon, 5);
        w["refresh_min"] ??= 15;
        if (Replace(next) is { } error)
        {
            throw new ArgumentException(error);
        }
    }

    public void SetUnits(Units units)
    {
        if (!Units.Valid(units))
        {
            throw new ArgumentException("units: temp c|f, wind kmh|ms|mph, pressure hpa|inhg");
        }
        var next = Snapshot();
        var u = Section(next, "units");
        u["temp"] = units.Temp;
        u["wind"] = units.Wind;
        u["pressure"] = units.Pressure;
        if (Replace(next) is { } error)
        {
            throw new ArgumentException(error);
        }
    }

    private JsonObject Load()
    {
        JsonObject? root = null;
        try
        {
            if (File.Exists(_path))
            {
                root = JsonNode.Parse(File.ReadAllText(_path)) as JsonObject;
                if (root is null)
                {
                    throw new JsonException("not a JSON object");
                }
            }
        }
        catch (Exception ex) when (ex is JsonException or IOException or UnauthorizedAccessException)
        {
            Backup($"unreadable ({ex.Message})");
            root = null;
        }

        if (root is null)
        {
            Log.Info($"config: no config at {_path}, starting from the default");
            root = Default();
            _root = root;
            TrySave();
            return root;
        }

        var before = root.ToJsonString();
        Migrate(root);
        root = JsonNode.Parse(ConfigTransfer.ToBytes(root))!.AsObject();
        if (ConfigValidator.Validate(root) is { } error)
        {
            Backup($"invalid ({error})");
            root = Default();
        }
        if (root.ToJsonString() != before)
        {
            _root = root;
            TrySave();
        }
        Log.Info($"config: loaded {_path}");
        return root;
    }

    /// <summary>
    /// Brings an older config up to the current schema. Schema 1 is the first released one; files
    /// written before Phase 9 only had "weather" and "units", so missing sections (and missing keys of
    /// known sections) come from the default. Later schema bumps add their steps here.
    /// </summary>
    private static void Migrate(JsonObject root)
    {
        if (root["schema"] is null)
        {
            root["schema"] = ConfigCatalog.Schema;
        }
        var def = Default();
        foreach (var (key, value) in def)
        {
            if (root[key] is null)
            {
                root[key] = value?.DeepClone();
            }
            else if (key != "pages" && root[key] is JsonObject section && value is JsonObject defSection)
            {
                foreach (var (k, v) in defSection)
                {
                    if (!section.ContainsKey(k))
                    {
                        section[k] = v?.DeepClone();
                    }
                }
            }
        }
    }

    private void Backup(string why)
    {
        Log.Warn($"config: {_path} {why}; using the default, old file kept as config.json.bad");
        try
        {
            File.Copy(_path, _path + ".bad", overwrite: true);
        }
        catch (IOException)
        {
        }
    }

    private static JsonObject Section(JsonObject root, string name)
    {
        if (root[name] is JsonObject o)
        {
            return o;
        }
        var created = new JsonObject();
        root[name] = created;
        return created;
    }

    private void TrySave()
    {
        try
        {
            Save();
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            Log.Warn($"config: could not save {_path}: {ex.Message}");
        }
    }

    // Under _lock (or from the constructor).
    private void Save()
    {
        Directory.CreateDirectory(Path.GetDirectoryName(_path)!);
        var tmp = _path + ".tmp";
        File.WriteAllText(tmp, _root.ToJsonString(WriteOptions));
        File.Move(tmp, _path, overwrite: true);
        _bytes = ConfigTransfer.ToBytes(_root);
        Crc = Crc32.Compute(_bytes);
        Log.Info($"config: saved {_path} ({_bytes.Length} bytes, crc {Crc:X8})");
    }

    private static string? Str(JsonObject? o, string key) =>
        o?[key] is JsonValue v && v.TryGetValue<string>(out var s) ? s : null;

    private static double? Num(JsonObject? o, string key) =>
        o?[key] is JsonValue v && v.TryGetValue<double>(out var d) ? d : null;
}
