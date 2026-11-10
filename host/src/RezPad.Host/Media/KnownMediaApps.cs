using System.Text.Json;
using System.Text.Json.Nodes;

namespace RezPad.Host.Media;

/// <summary>
/// Apps that ever had a media session on this PC (id, display name, last seen), kept in
/// %APPDATA%\rez-pad\media-apps.json so the editor's "preferred apps" picker can offer them while
/// they are closed. Host only; at most 40 entries, the oldest go first.
/// </summary>
internal sealed class KnownMediaApps
{
    public sealed record App(string Id, string Name, DateTimeOffset LastSeen);

    private const int Max = 40;
    private readonly string _path;
    private readonly object _lock = new();
    private readonly Dictionary<string, App> _apps = new(StringComparer.OrdinalIgnoreCase);

    public KnownMediaApps(string? path = null)
    {
        _path = path ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "rez-pad", "media-apps.json");
        try
        {
            if (File.Exists(_path) && JsonNode.Parse(File.ReadAllText(_path)) is JsonArray arr)
            {
                foreach (var n in arr)
                {
                    var id = (string?)n?["id"];
                    var name = (string?)n?["name"];
                    if (!string.IsNullOrEmpty(id) && !string.IsNullOrEmpty(name))
                    {
                        _apps[id] = new App(id, name, n?["last_seen"]?.GetValue<DateTimeOffset>() ?? DateTimeOffset.UnixEpoch);
                    }
                }
            }
        }
        catch (Exception e) when (e is IOException or JsonException or InvalidOperationException or FormatException)
        {
            Log.Warn($"media: {_path} unreadable, starting a new list: {e.Message}");
        }
    }

    public IReadOnlyList<App> All
    {
        get
        {
            lock (_lock)
            {
                return [.. _apps.Values.OrderBy(a => a.Name, StringComparer.OrdinalIgnoreCase)];
            }
        }
    }

    /// <summary>Records the apps seen now; writes the file only when something new appeared.</summary>
    public void Seen(IEnumerable<(string Id, string Name)> apps)
    {
        var now = DateTimeOffset.UtcNow;
        var changed = false;
        lock (_lock)
        {
            foreach (var (id, name) in apps)
            {
                if (!_apps.TryGetValue(id, out var old) || old.Name != name || now - old.LastSeen > TimeSpan.FromDays(1))
                {
                    _apps[id] = new App(id, name, now);
                    changed = true;
                }
            }
            if (!changed)
            {
                return;
            }
            foreach (var stale in _apps.Values.OrderByDescending(a => a.LastSeen).Skip(Max).ToList())
            {
                _apps.Remove(stale.Id);
            }
            try
            {
                var arr = new JsonArray([.. _apps.Values.Select(a => (JsonNode)new JsonObject
                {
                    ["id"] = a.Id, ["name"] = a.Name, ["last_seen"] = a.LastSeen,
                })]);
                Directory.CreateDirectory(Path.GetDirectoryName(_path)!);
                var tmp = _path + ".tmp";
                File.WriteAllText(tmp, arr.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
                File.Move(tmp, _path, overwrite: true);
            }
            catch (IOException e)
            {
                Log.Warn($"media: cannot write {_path}: {e.Message}");
            }
        }
    }
}
