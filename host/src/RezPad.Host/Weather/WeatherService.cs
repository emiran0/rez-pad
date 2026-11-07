using System.Globalization;
using System.Net.Http.Json;
using System.Text.Json;
using System.Text.Json.Serialization;
using RezPad.Host.Config;
using RezPad.Protocol;

namespace RezPad.Host.Weather;

internal sealed record GeocodeResult(string Name, string? Admin1, string? Country, string? CountryCode, double Lat,
    double Lon, string? Timezone);

/// <summary>
/// Weather from Open-Meteo (no API key): fetched every <c>weather.refresh_min</c> minutes, when the
/// location or units change, with a growing retry delay after failures. Also the city search for
/// the web page (Open-Meteo geocoding).
/// </summary>
internal sealed class WeatherService : IDisposable
{
    private const string ForecastUrl = "https://api.open-meteo.com/v1/forecast";
    private const string GeocodeUrl = "https://geocoding-api.open-meteo.com/v1/search";
    private static readonly TimeSpan[] RetryDelays =
        [TimeSpan.FromMinutes(1), TimeSpan.FromMinutes(2), TimeSpan.FromMinutes(5), TimeSpan.FromMinutes(10)];

    private static readonly HttpClient Http = CreateClient();

    private readonly ConfigService _config;
    private readonly CancellationTokenSource _cts = new();
    private readonly SemaphoreSlim _poke = new(0, 1);
    private WeatherMsg? _latest;
    private int _failures;

    public WeatherService(ConfigService config)
    {
        _config = config;
        _lastSettings = (config.Weather, config.Units);
        // Fetch again only when the location or units changed, not on every layout edit.
        _config.Changed += () =>
        {
            var now = (_config.Weather, _config.Units);
            if (now != _lastSettings)
            {
                _lastSettings = now;
                Refresh();
            }
        };
    }

    private (WeatherLocation, Units) _lastSettings;

    /// <summary>Raised after every successful fetch (on a thread-pool thread).</summary>
    public event Action<WeatherMsg>? Updated;

    public WeatherMsg? Latest => Volatile.Read(ref _latest);

    public string? LastError { get; private set; }

    public DateTimeOffset? LastAttempt { get; private set; }

    public void Start() => _ = Task.Run(RunAsync);

    /// <summary>Fetch now (location or units changed).</summary>
    public void Refresh()
    {
        try
        {
            _poke.Release();
        }
        catch (SemaphoreFullException)
        {
            // Already pending.
        }
    }

    private async Task RunAsync()
    {
        var ct = _cts.Token;
        while (!ct.IsCancellationRequested)
        {
            var location = _config.Weather;
            TimeSpan wait;
            if (!location.IsSet)
            {
                wait = Timeout.InfiniteTimeSpan;  // nothing to fetch until a city is picked
            }
            else if (await FetchAsync(location, _config.Units, ct))
            {
                _failures = 0;
                wait = TimeSpan.FromMinutes(location.RefreshMin);
            }
            else
            {
                wait = RetryDelays[Math.Min(_failures++, RetryDelays.Length - 1)];
            }
            try
            {
                await _poke.WaitAsync(wait, ct);
            }
            catch (OperationCanceledException)
            {
                return;
            }
        }
    }

    private async Task<bool> FetchAsync(WeatherLocation loc, Units units, CancellationToken ct)
    {
        LastAttempt = DateTimeOffset.UtcNow;
        // Invariant formatting: a Turkish or German locale would write "41,01".
        var lat = loc.Lat!.Value.ToString(CultureInfo.InvariantCulture);
        var lon = loc.Lon!.Value.ToString(CultureInfo.InvariantCulture);
        var url =
            $"{ForecastUrl}?latitude={lat}&longitude={lon}" +
            "&current=temperature_2m,apparent_temperature,relative_humidity_2m,wind_speed_10m,weather_code,is_day,surface_pressure" +
            "&hourly=temperature_2m,weather_code,precipitation_probability,is_day" +
            "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max" +
            $"&forecast_days=6&forecast_hours=13&timezone=auto&timeformat=unixtime" +
            $"&temperature_unit={(units.Temp == "f" ? "fahrenheit" : "celsius")}&wind_speed_unit={units.Wind}";
        try
        {
            var data = await Http.GetFromJsonAsync<Forecast>(url, ct)
                       ?? throw new JsonException("empty response");
            var msg = Build(loc, units, data);
            Volatile.Write(ref _latest, msg);
            LastError = null;
            Log.Info($"weather: {loc.Name} {msg.Now.Temp}{units.Temp.ToUpperInvariant()}, code {msg.Now.Code}, " +
                     $"{msg.Daily.Count} days, {msg.Hourly.Count} hours");
            Updated?.Invoke(msg);
            return true;
        }
        catch (Exception ex)
        {
            // Network errors, timeouts and unexpected response shapes all just mean "try again later".
            if (ct.IsCancellationRequested)
            {
                return false;
            }
            LastError = ex is TaskCanceledException ? "timed out" : ex.Message;
            Log.Warn($"weather: fetch failed ({LastError}), retrying");
            return false;
        }
    }

    private static WeatherMsg Build(WeatherLocation loc, Units units, Forecast f)
    {
        static double R(double v) => Math.Round(v, 1);
        double? Press(double? hpa) => hpa is null ? null : units.Pressure == "inhg" ? Math.Round(hpa.Value * 0.0295300, 2) : R(hpa.Value);

        var c = f.Current;
        var now = new WeatherNow(R(c.Temperature), R(c.Apparent), (int)Math.Round(c.Humidity), R(c.Wind),
            Press(c.Pressure), c.Code, c.IsDay == 1);

        // Hourly starts at the current hour; send 12 from now on.
        var hourStart = DateTimeOffset.UtcNow.ToUnixTimeSeconds() / 3600 * 3600;
        var hourly = new List<WeatherHour>(12);
        for (var i = 0; i < f.Hourly.Time.Length && hourly.Count < 12; i++)
        {
            if (f.Hourly.Time[i] < hourStart)
            {
                continue;
            }
            hourly.Add(new WeatherHour(f.Hourly.Time[i], R(f.Hourly.Temperature[i] ?? 0), f.Hourly.Code[i] ?? 0,
                f.Hourly.Pop[i] ?? 0, (f.Hourly.IsDay[i] ?? 1) == 1));
        }

        // Daily times are local midnight at the location; name them by the location's date.
        var daily = new List<WeatherDay>(5);
        for (var i = 0; i < f.Daily.Time.Length && daily.Count < 5; i++)
        {
            var date = DateTimeOffset.FromUnixTimeSeconds(f.Daily.Time[i] + f.UtcOffsetSeconds).UtcDateTime;
            daily.Add(new WeatherDay(date.ToString("yyyy-MM-dd", CultureInfo.InvariantCulture),
                R(f.Daily.Min[i] ?? 0), R(f.Daily.Max[i] ?? 0), f.Daily.Code[i] ?? 0, f.Daily.Pop[i] ?? 0));
        }

        return new WeatherMsg(loc.Name, new WeatherUnits(units.Temp, units.Wind, units.Pressure),
            DateTimeOffset.UtcNow.ToUnixTimeSeconds(), now, hourly, daily);
    }

    public static async Task<IReadOnlyList<GeocodeResult>> GeocodeAsync(string query, CancellationToken ct)
    {
        if (string.IsNullOrWhiteSpace(query) || query.Length > 100)
        {
            return [];
        }
        var url = $"{GeocodeUrl}?name={Uri.EscapeDataString(query.Trim())}&count=8&language=en&format=json";
        var res = await Http.GetFromJsonAsync<GeocodeResponse>(url, ct);
        return res?.Results?.Select(r => new GeocodeResult(r.Name, r.Admin1, r.Country, r.CountryCode, r.Latitude,
            r.Longitude, r.Timezone)).ToList() ?? [];
    }

    private static HttpClient CreateClient()
    {
        var http = new HttpClient { Timeout = TimeSpan.FromSeconds(15) };
        http.DefaultRequestHeaders.UserAgent.ParseAdd($"rez-pad/{Program.Version}");
        return http;
    }

    public void Dispose()
    {
        _cts.Cancel();
        _cts.Dispose();
    }

    // ---- Open-Meteo response shapes ----

    private sealed record Forecast(
        [property: JsonPropertyName("utc_offset_seconds")] long UtcOffsetSeconds,
        [property: JsonPropertyName("current")] CurrentData Current,
        [property: JsonPropertyName("hourly")] HourlyData Hourly,
        [property: JsonPropertyName("daily")] DailyData Daily);

    private sealed record CurrentData(
        [property: JsonPropertyName("temperature_2m")] double Temperature,
        [property: JsonPropertyName("apparent_temperature")] double Apparent,
        [property: JsonPropertyName("relative_humidity_2m")] double Humidity,
        [property: JsonPropertyName("wind_speed_10m")] double Wind,
        [property: JsonPropertyName("surface_pressure")] double? Pressure,
        [property: JsonPropertyName("weather_code")] int Code,
        [property: JsonPropertyName("is_day")] int IsDay);

    private sealed record HourlyData(
        [property: JsonPropertyName("time")] long[] Time,
        [property: JsonPropertyName("temperature_2m")] double?[] Temperature,
        [property: JsonPropertyName("weather_code")] int?[] Code,
        [property: JsonPropertyName("precipitation_probability")] int?[] Pop,
        [property: JsonPropertyName("is_day")] int?[] IsDay);

    private sealed record DailyData(
        [property: JsonPropertyName("time")] long[] Time,
        [property: JsonPropertyName("weather_code")] int?[] Code,
        [property: JsonPropertyName("temperature_2m_max")] double?[] Max,
        [property: JsonPropertyName("temperature_2m_min")] double?[] Min,
        [property: JsonPropertyName("precipitation_probability_max")] int?[] Pop);

    private sealed record GeocodeResponse([property: JsonPropertyName("results")] GeocodeItem[]? Results);

    private sealed record GeocodeItem(
        [property: JsonPropertyName("name")] string Name,
        [property: JsonPropertyName("admin1")] string? Admin1,
        [property: JsonPropertyName("country")] string? Country,
        [property: JsonPropertyName("country_code")] string? CountryCode,
        [property: JsonPropertyName("latitude")] double Latitude,
        [property: JsonPropertyName("longitude")] double Longitude,
        [property: JsonPropertyName("timezone")] string? Timezone);
}
