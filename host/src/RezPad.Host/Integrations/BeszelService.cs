using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;
using System.Text.Json.Nodes;
using RezPad.Protocol;

namespace RezPad.Host.Integrations;

/// <summary>A data source an integration provides (same shape as the PC's own sources).</summary>
internal sealed record ExternalSource(string Id, string Name, string Unit, string Group);

/// <summary>State of the Beszel connection for the settings page.</summary>
internal sealed record BeszelStatus(bool Enabled, bool Connected, string? Error, DateTimeOffset? LastOk,
    IReadOnlyList<BeszelSystem> Systems);

internal sealed record BeszelSystem(string Id, string Name, string Slug, string Status);

/// <summary>
/// Beszel hub client (PocketBase API): signs in with the hub user from Settings > Integrations,
/// reads the systems that user can see every <c>refresh_s</c> seconds and offers their values as
/// data sources <c>bz.&lt;machine&gt;.&lt;metric&gt;</c>. Runs only while enabled; a failed request just
/// marks the values unavailable until the next poll.
/// </summary>
internal sealed class BeszelService : IDisposable
{
    private readonly IntegrationStore _store;
    private readonly object _lock = new();
    private CancellationTokenSource? _cts;
    private Task? _loop;
    private HttpClient? _http;
    private string? _token;
    private Dictionary<string, double?> _values = [];
    private List<ExternalSource> _sources = [];
    private BeszelStatus _status = new(false, false, null, null, []);
    private string? _raw;
    private Dictionary<string, JsonObject> _minute = [];  // system id -> latest system_stats "1m" record
    private DateTime _minuteAt = DateTime.MinValue;

    public BeszelService(IntegrationStore store)
    {
        _store = store;
        _store.Changed += Restart;
    }

    public IReadOnlyList<ExternalSource> Sources
    {
        get
        {
            lock (_lock)
            {
                return _sources;
            }
        }
    }

    public BeszelStatus Status
    {
        get
        {
            lock (_lock)
            {
                return _status;
            }
        }
    }

    /// <summary>The last systems response and per-minute stats as the hub sent them (local debugging aid, no credentials).</summary>
    public string? Raw
    {
        get
        {
            lock (_lock)
            {
                if (_raw is null)
                {
                    return null;
                }
                var o = new JsonObject
                {
                    ["systems"] = JsonNode.Parse(_raw),
                    ["minute_stats"] = new JsonObject(_minute.Select(kv => KeyValuePair.Create(kv.Key, (JsonNode?)kv.Value.DeepClone()))),
                };
                return o.ToJsonString();
            }
        }
    }

    public double? Value(string id)
    {
        lock (_lock)
        {
            return _values.TryGetValue(id, out var v) ? v : null;
        }
    }

    public void Start() => Restart();

    /// <summary>Signs in and reads the systems once with these settings (the settings page's Test button).</summary>
    public async Task<BeszelStatus> TestAsync(BeszelSettings s, CancellationToken ct)
    {
        using var http = Client(s);
        try
        {
            var token = await SignInAsync(http, s, ct);
            var (items, _) = await ReadSystemsAsync(http, token, ct);
            var map = BeszelMap.Compute(items, new Dictionary<string, JsonObject>());
            return new BeszelStatus(true, true, null, DateTimeOffset.UtcNow, Systems(map));
        }
        catch (Exception ex) when (ex is HttpRequestException or TaskCanceledException or InvalidOperationException or JsonException)
        {
            return new BeszelStatus(true, false, Describe(ex), null, []);
        }
    }

    private void Restart()
    {
        _cts?.Cancel();
        try
        {
            _loop?.Wait(2000);
        }
        catch (AggregateException)
        {
        }
        var s = _store.Beszel;
        lock (_lock)
        {
            _token = null;
            _http?.Dispose();
            _http = null;
            _values = [];
            _status = new BeszelStatus(s.Enabled, false, null, null, []);
        }
        if (!s.Enabled || s.Url.Length == 0)
        {
            return;
        }
        _cts = new CancellationTokenSource();
        var ct = _cts.Token;
        _loop = Task.Run(() => LoopAsync(s, ct), ct);
    }

    private async Task LoopAsync(BeszelSettings s, CancellationToken ct)
    {
        var http = Client(s);
        lock (_lock)
        {
            _http = http;
        }
        while (!ct.IsCancellationRequested)
        {
            try
            {
                _token ??= await SignInAsync(http, s, ct);
                var (items, raw) = await ReadSystemsAsync(http, _token, ct);
                // Network up/down, used GB and the sensor temperatures come from the per-minute
                // stats records, which change once a minute: read them that often.
                if (DateTime.UtcNow - _minuteAt > TimeSpan.FromSeconds(60))
                {
                    _minuteAt = DateTime.UtcNow;
                    var minute = new Dictionary<string, JsonObject>();
                    foreach (var it in items.OfType<JsonObject>().Where(x => (string?)x["status"] == "up"))
                    {
                        var id = (string?)it["id"] ?? "";
                        if (await ReadMinuteAsync(http, _token, id, ct) is { } rec)
                        {
                            minute[id] = rec;
                        }
                    }
                    lock (_lock)
                    {
                        _minute = minute;
                    }
                }
                Dictionary<string, JsonObject> current;
                lock (_lock)
                {
                    current = _minute;
                }
                var map = BeszelMap.Compute(items, current);
                lock (_lock)
                {
                    _values = new Dictionary<string, double?>(map.Values);
                    _sources = map.Sources.Select(x => new ExternalSource(x.Id, x.Name, x.Unit, x.Group)).ToList();
                    _raw = raw;
                    _status = new BeszelStatus(true, true, null, DateTimeOffset.UtcNow, Systems(map));
                }
            }
            catch (HttpRequestException ex) when (ex.StatusCode is HttpStatusCode.Unauthorized or HttpStatusCode.Forbidden && _token is not null)
            {
                _token = null;  // expired: sign in again right away
                continue;
            }
            catch (Exception ex) when (ex is HttpRequestException or TaskCanceledException or InvalidOperationException or JsonException)
            {
                if (ct.IsCancellationRequested)
                {
                    break;
                }
                lock (_lock)
                {
                    var was = _status.Error;
                    _values = _values.ToDictionary(kv => kv.Key, _ => (double?)null);  // keep the sources, values unknown
                    _status = _status with { Connected = false, Error = Describe(ex) };
                    if (was != _status.Error)
                    {
                        Log.Warn($"beszel: {_status.Error}");
                    }
                }
            }
            try
            {
                await Task.Delay(TimeSpan.FromSeconds(s.RefreshS), ct);
            }
            catch (TaskCanceledException)
            {
                break;
            }
        }
    }

    private static HttpClient Client(BeszelSettings s)
    {
        var handler = new HttpClientHandler();
        if (s.InsecureTls)
        {
            // Local hubs with a self-signed certificate (opt-in in the settings).
            handler.ServerCertificateCustomValidationCallback = HttpClientHandler.DangerousAcceptAnyServerCertificateValidator;
        }
        return new HttpClient(handler) { BaseAddress = new Uri(s.Url.TrimEnd('/') + "/"), Timeout = TimeSpan.FromSeconds(10) };
    }

    private static async Task<string> SignInAsync(HttpClient http, BeszelSettings s, CancellationToken ct)
    {
        if (string.IsNullOrEmpty(s.Email) || string.IsNullOrEmpty(s.Password))
        {
            throw new InvalidOperationException("email and password are needed");
        }
        using var res = await http.PostAsJsonAsync("api/collections/users/auth-with-password",
            new { identity = s.Email, password = s.Password }, ct);
        if (res.StatusCode is HttpStatusCode.BadRequest or HttpStatusCode.Unauthorized)
        {
            throw new InvalidOperationException("sign-in failed: wrong email or password");
        }
        res.EnsureSuccessStatusCode();
        var body = await res.Content.ReadFromJsonAsync<JsonObject>(ct);
        return (string?)body?["token"] ?? throw new InvalidOperationException("sign-in failed: no token in the answer");
    }

    private static List<BeszelSystem> Systems(BeszelMap.Result map) =>
        map.Systems.Select(x => new BeszelSystem(x.Id, x.Name, x.Slug, x.Status)).ToList();

    private static async Task<(JsonArray Items, string Raw)> ReadSystemsAsync(HttpClient http, string token, CancellationToken ct)
    {
        using var req = new HttpRequestMessage(HttpMethod.Get, "api/collections/systems/records?perPage=200&sort=name");
        req.Headers.Authorization = new AuthenticationHeaderValue("Bearer", token);
        using var res = await http.SendAsync(req, ct);
        res.EnsureSuccessStatusCode();
        var raw = await res.Content.ReadAsStringAsync(ct);
        return ((JsonNode.Parse(raw)?["items"] as JsonArray) ?? [], raw);
    }

    /// <summary>The newest one-minute stats record of a system (Beszel keeps one per minute), or null.</summary>
    private static async Task<JsonObject?> ReadMinuteAsync(HttpClient http, string token, string systemId, CancellationToken ct)
    {
        var filter = Uri.EscapeDataString($"(system='{systemId}' && type='1m')");
        using var req = new HttpRequestMessage(HttpMethod.Get, $"api/collections/system_stats/records?filter={filter}&sort=-created&perPage=1");
        req.Headers.Authorization = new AuthenticationHeaderValue("Bearer", token);
        using var res = await http.SendAsync(req, ct);
        if (!res.IsSuccessStatusCode)
        {
            return null;
        }
        var body = await res.Content.ReadFromJsonAsync<JsonObject>(ct);
        return (body?["items"] as JsonArray)?.FirstOrDefault() as JsonObject;
    }

    private static string Describe(Exception ex) => ex switch
    {
        InvalidOperationException => ex.Message,
        TaskCanceledException => "no answer from the hub (timeout)",
        HttpRequestException h when h.StatusCode is { } code => $"hub answered {(int)code} {code}",
        HttpRequestException h => $"cannot reach the hub: {h.InnerException?.Message ?? h.Message}",
        _ => ex.Message,
    };

    public void Dispose()
    {
        _cts?.Cancel();
        lock (_lock)
        {
            _http?.Dispose();
        }
    }
}
