using System.Net;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.FileProviders;
using Microsoft.Extensions.Logging;
using RezPad.Host.Assets;
using RezPad.Host.Audio;
using RezPad.Host.Config;
using RezPad.Host.Device;
using RezPad.Host.Media;
using RezPad.Host.Stats;
using RezPad.Host.Weather;
using RezPad.Protocol;

namespace RezPad.Host.Web;

/// <summary>Kestrel bound to 127.0.0.1 only. Serves web/ and the /api endpoints (docs/config-schema.md).</summary>
internal sealed record WebServices(SerialLink Link, MediaService Media, AudioService Audio, SystemStatsService Stats,
    ConfigService Config, WeatherService Weather, RoomSensor Room, ConfigSync ConfigSync, AssetStore Assets,
    SpectrumBridge Spectrum, Integrations.IntegrationStore Integrations, Integrations.BeszelService Beszel);

internal sealed class WebServer(int port, WebServices services)
{
    private readonly SerialLink link = services.Link;
    private readonly MediaService media = services.Media;
    private readonly AudioService audio = services.Audio;
    private readonly SystemStatsService stats = services.Stats;
    private readonly ConfigService config = services.Config;
    private readonly ConfigSync configSync = services.ConfigSync;
    private readonly AssetStore assets = services.Assets;
    private readonly WeatherService weather = services.Weather;
    private readonly RoomSensor room = services.Room;
    private readonly SpectrumBridge spectrum = services.Spectrum;
    private readonly Integrations.IntegrationStore integrations = services.Integrations;
    private readonly Integrations.BeszelService beszel = services.Beszel;
    private readonly InputLearn learn = new(services.Link);
    private readonly FirmwareUpdater firmware = new(services.Link);

    private WebApplication? _app;

    public string Url { get; } = $"http://127.0.0.1:{port}/";

    private static async Task<Integrations.BeszelSettings?> ReadBeszelAsync(HttpRequest req)
    {
        var o = await System.Text.Json.Nodes.JsonNode.ParseAsync(req.Body) as System.Text.Json.Nodes.JsonObject;
        var url = ((string?)o?["url"] ?? "").Trim();
        if (url.Length > 0 && !(url.StartsWith("http://", StringComparison.OrdinalIgnoreCase) || url.StartsWith("https://", StringComparison.OrdinalIgnoreCase)))
        {
            return null;
        }
        var password = (string?)o?["password"];
        return new Integrations.BeszelSettings(
            (bool?)o?["enabled"] ?? false,
            url,
            ((string?)o?["email"] ?? "").Trim(),
            string.IsNullOrEmpty(password) ? null : password,  // empty: keep the stored one
            Math.Clamp((int?)o?["refresh_s"] ?? Integrations.BeszelSettings.DefaultRefreshS, 5, 300),
            (bool?)o?["insecure_tls"] ?? false);
    }

    public void Start()
    {
        var builder = WebApplication.CreateSlimBuilder();
        builder.WebHost.ConfigureKestrel(o => o.Listen(IPAddress.Loopback, port));
        builder.Logging.SetMinimumLevel(LogLevel.Warning);

        var app = builder.Build();

        // The editor is embedded in the exe (RezPad.Host.csproj), so a single-file publish carries it.
        // REZPAD_WEB_DIR (development): serve the editor from that folder instead, so web/ edits
        // show up with a browser reload.
        var webDir = Environment.GetEnvironmentVariable("REZPAD_WEB_DIR");
        IFileProvider files = !string.IsNullOrEmpty(webDir) && Directory.Exists(webDir)
            ? new PhysicalFileProvider(Path.GetFullPath(webDir))
            : new EmbeddedFileProvider(typeof(WebServer).Assembly, "RezPad.Host.web");
        app.UseDefaultFiles(new DefaultFilesOptions { FileProvider = files });
        // no-cache: the browser revalidates (cheap on localhost), so an updated host never runs
        // with editor scripts left over from the previous version.
        app.UseStaticFiles(new StaticFileOptions
        {
            FileProvider = files,
            OnPrepareResponse = ctx => ctx.Context.Response.Headers.CacheControl = "no-cache",
        });

        app.MapGet("/api/status", () =>
        {
            var s = link.Status;
            var decoder = link.Stats;
            return Results.Json(new StatusDto(
                HostVersion: Program.Version,
                ProtocolVersion: Frame.ProtocolVersion,
                DeviceConnected: s.State == DeviceLinkState.Connected,
                LinkState: s.State.ToString(),
                Port: s.Port,
                Serial: s.Serial,
                FirmwareVersion: s.Device?.FwVer,
                FirmwareProtocol: s.Device?.Proto,
                Error: s.Error,
                Elevated: StartupTask.IsElevated(),
                LogFileError: Log.FileError,
                Sensors: stats.Access,
                Config: new ConfigStatusDto(config.Crc, configSync.DeviceCrc, configSync.InSync, configSync.Last, configSync.Progress),
                BadFrames: decoder is null ? 0 : decoder.BadCobs + decoder.BadCrc + decoder.TooShort + decoder.TooLarge + decoder.BadVersion));
        });
        app.MapGet("/api/log", () => Results.Json(Log.Recent()));
        // The Windows volume mixer as a mixer widget shows it (editor preview).
        app.MapGet("/api/mixer", () => Results.Json(new MixerMsg(MixerService.Snapshot()), ProtocolJson.Options));
        // Live spectrum for the editor preview: 32 levels 0 to 255, all zero while capture is off.
        app.MapGet("/api/spectrum", () => Results.Json(new
        {
            capturing = spectrum.Capturing,
            sent = spectrum.Sent,
            frames = spectrum.Frames,
            send_ms = Math.Round(spectrum.SendMs, 1),
            bins = spectrum.Latest.Select(b => (int)b).ToArray(),
        }));

        // Weather location, units and the latest forecast the device got.
        app.MapGet("/api/weather", () =>
        {
            var latest = weather.Latest;
            return Results.Json(new
            {
                location = config.Weather,
                units = config.Units,
                latest,
                age_s = latest is null ? (long?)null : DateTimeOffset.UtcNow.ToUnixTimeSeconds() - latest.Updated,
                error = weather.LastError,
            }, ProtocolJson.Options);
        });
        // {"name": "...", "lat": .., "lon": ..} and/or {"units": {"temp": "c", "wind": "ms", "pressure": "hpa"}}
        app.MapPut("/api/weather", async (HttpRequest req) =>
        {
            WeatherUpdate? body;
            try
            {
                body = await System.Text.Json.JsonSerializer.DeserializeAsync<WeatherUpdate>(req.Body, ProtocolJson.Options);
            }
            catch (System.Text.Json.JsonException)
            {
                body = null;
            }
            if (body is null || (body.Lat is null != body.Lon is null) ||
                body.Lat is < -90 or > 90 || body.Lon is < -180 or > 180 ||
                (body.Units is not null && !Units.Valid(body.Units)))
            {
                return Results.BadRequest(new { error = "expected {name, lat, lon} and/or {units: {temp: c|f, wind: kmh|ms|mph, pressure: hpa|inhg}}" });
            }
            if (body.Units is not null)
            {
                config.SetUnits(body.Units);
            }
            if (body.Lat is { } lat && body.Lon is { } lon)
            {
                var name = string.IsNullOrWhiteSpace(body.Name) ? $"{lat:F2}, {lon:F2}" : body.Name.Trim();
                config.SetWeatherLocation(name[..Math.Min(name.Length, 40)], lat, lon);
            }
            return Results.Ok();
        });
        app.MapGet("/api/geocode", async (string? q, CancellationToken ct) =>
        {
            try
            {
                return Results.Json(await WeatherService.GeocodeAsync(q ?? "", ct), ProtocolJson.Options);
            }
            catch (Exception ex) when (ex is HttpRequestException or TaskCanceledException or System.Text.Json.JsonException)
            {
                return Results.Problem($"geocoding failed: {ex.Message}", statusCode: 502);
            }
        });
        // The device's room sensor (SENSOR every 10 s while connected).
        app.MapGet("/api/sensor", () => room.Latest is { } r
            ? Results.Json(new { reading = r.Reading, age_s = (int)(DateTimeOffset.UtcNow - r.At).TotalSeconds }, ProtocolJson.Options)
            : Results.Json(new { reading = (SensorMsg?)null }, ProtocolJson.Options));
        // Everything LibreHardwareMonitor sees, with raw sensor names (debugging aid).
        app.MapGet("/api/hardware", () =>
        {
            stats.WantCatalog();
            return Results.Json(stats.HardwareReport().ToList());
        });
        // Data source catalog for the editor: every source this PC has, its live value, and whether
        // the current layout uses it (docs/config-schema.md, "Data source IDs").
        app.MapGet("/api/sources", () =>
        {
            stats.WantCatalog();  // values for every source, not only the layout's, for the next few seconds
            var used = config.ReferencedSources;
            return Results.Json(new
            {
                sensors = stats.Access,
                sources = stats.Catalog.Select(s =>
                {
                    var v = stats.Value(s.Id);
                    return new
                    {
                        id = s.Id,
                        name = s.Name,
                        unit = s.Unit,
                        available = v is not null,
                        value = v is { } d ? Math.Round(d, 2) : (double?)null,
                        used = used.Contains(s.Id),
                    };
                }).Concat(beszel.Sources.Select(s =>
                {
                    // Integration sources: same shape, plus the group the editor lists them under.
                    var v = beszel.Value(s.Id);
                    return new
                    {
                        id = s.Id,
                        name = s.Name,
                        unit = s.Unit,
                        available = v is not null,
                        value = v is { } d ? Math.Round(d, 2) : (double?)null,
                        used = used.Contains(s.Id),
                        group = s.Group,
                    };
                }).Cast<object>()),
            }, ProtocolJson.Options);
        });

        // ---- integrations (Settings > Integrations); credentials stay on this PC ----
        app.MapGet("/api/integrations", () =>
        {
            var b = integrations.Beszel;
            return Results.Json(new
            {
                beszel = new
                {
                    settings = new { enabled = b.Enabled, url = b.Url, email = b.Email, has_password = !string.IsNullOrEmpty(b.Password), refresh_s = b.RefreshS, insecure_tls = b.InsecureTls },
                    status = beszel.Status,
                },
            }, ProtocolJson.Options);
        });
        app.MapPut("/api/integrations/beszel", async (HttpRequest req) =>
        {
            var s = await ReadBeszelAsync(req);
            if (s is null)
            {
                return Results.BadRequest(new { error = "url must be http:// or https://" });
            }
            integrations.SetBeszel(s);
            return Results.Json(new { saved = true });
        });
        // Signs in and lists the systems with the posted settings, without saving them.
        app.MapPost("/api/integrations/beszel/test", async (HttpRequest req, CancellationToken ct) =>
        {
            var s = await ReadBeszelAsync(req);
            if (s is null)
            {
                return Results.BadRequest(new { error = "url must be http:// or https://" });
            }
            return Results.Json(await beszel.TestAsync(s with { Password = s.Password ?? integrations.Beszel.Password }, ct), ProtocolJson.Options);
        });
        app.MapGet("/api/integrations/beszel/raw", () => beszel.Raw is { } raw ? Results.Text(raw, "application/json") : Results.NotFound());
        // What the host sends as NOW_PLAYING and VOLUME right now (debugging aid).
        // Apps with a media session now (switching order, the Now Playing one marked) and every app
        // seen with one before (the preferred apps picker).
        app.MapGet("/api/media/apps", () => Results.Json(new
        {
            apps = media.Apps().Select(a => new { id = a.Id, name = a.Name, state = a.State, current = a.Id == media.SessionAppId }),
            known = media.Known.All.Select(a => new { id = a.Id, name = a.Name }),
        }, ProtocolJson.Options));
        app.MapPost("/api/media/apps/choose", (string id) =>
            media.Choose(id) ? Results.NoContent() : Results.NotFound(new { error = "that app has no media session now" }));

        app.MapGet("/api/media", () =>
        {
            var art = media.Art;
            var vol = audio.State;
            return Results.Json(new
            {
                now_playing = media.Current.ToMessage(DateTimeOffset.UtcNow),
                art = new { track_id = art.TrackId, source_bytes = art.Source?.Length ?? 0 },
                volume = new { level = vol.Level, muted = vol.Muted, available = vol.Available },
                // The Now Playing app's own volume (volume widget with target "app"), null without one.
                app_volume = media.SessionAppId is { } appId && AppVolume.Get(appId, MediaService.AppName(appId)) is { } av
                    ? new { name = av.Name, level = av.Level, muted = av.Muted } : null,
            }, ProtocolJson.Options);
        });
        // The master config (docs/config-schema.md). PUT validates, saves and pushes it to the device;
        // the answer says whether the device applied it.
        app.MapGet("/api/config", (HttpResponse res) =>
        {
            var (root, crc) = config.SnapshotWithCrc();
            res.Headers["X-Config-Crc"] = crc.ToString(System.Globalization.CultureInfo.InvariantCulture);
            return Results.Text(root.ToJsonString(), "application/json");
        });
        app.MapGet("/api/config/default", () => Results.Text(ConfigService.Default().ToJsonString(), "application/json"));
        // The default grid and pages for a screen (docs/default-layouts.json): {grid, pages}.
        app.MapGet("/api/config/layout", (string? panel, string? orientation) =>
        {
            var layout = ConfigService.DefaultLayout(ConfigCatalog.ScreenOf(panel, orientation));
            return layout is null ? Results.NotFound(new { error = "no default layout for this screen" })
                : Results.Text(layout.ToJsonString(), "application/json");
        });
        // What the editor may offer: widget types and sizes, actions, limits, and the style defaults
        // of every data source on this PC (one source of truth: RezPad.Protocol.ConfigCatalog).
        app.MapGet("/api/catalog", () => Results.Json(new
        {
            schema = ConfigCatalog.Schema,
            limits = new
            {
                pages = ConfigCatalog.MaxPages,
                widgets = ConfigCatalog.MaxWidgets,
                grid = ConfigCatalog.MaxGrid,
                sources = ConfigCatalog.MaxSources,
                bytes = ConfigCatalog.MaxConfigBytes,
            },
            // Any size is allowed whose content area (card minus padding) is at least min_w x min_h px.
            geometry = new
            {
                status_h = ConfigCatalog.StatusH,
                panels = ConfigCatalog.Panels.Select(kv => new { name = kv.Key, w = kv.Value.W, h = kv.Value.H }),
                orientations = ConfigCatalog.Orientations,
                gap = ConfigCatalog.CellGap,
                pad = ConfigCatalog.CardPad,
            },
            widgets = ConfigCatalog.WidgetMin.Select(kv => new { type = kv.Key, min_w = kv.Value.W, min_h = kv.Value.H }),
            themes = ConfigCatalog.Themes,
            actions = ConfigCatalog.Actions,
            rotate_actions = ConfigCatalog.RotateActions,
            sources = stats.Catalog.Select(s => new { id = s.Id, name = s.Name, style = ConfigCatalog.SourceDefaults(s.Id) })
                .Concat(beszel.Sources.Select(s => new { id = s.Id, name = s.Name, style = ConfigCatalog.SourceDefaults(s.Id) })),
        }, ProtocolJson.Options));
        // Live validation while editing; same validator as PUT, nothing is saved.
        app.MapPost("/api/config/validate", async (HttpRequest req) =>
        {
            using var ms = new MemoryStream();
            await req.Body.CopyToAsync(ms);
            var error = ConfigValidator.Validate(ms.ToArray());
            return Results.Json(new { error, bytes = ms.Length }, ProtocolJson.Options);
        });
        app.MapPut("/api/config", async (HttpRequest req) =>
        {
            // ?base=<crc>: the config the editor started from. Refuse to overwrite a newer one (saved
            // from another tab, or a weather location set meanwhile).
            if (uint.TryParse(req.Query["base"], out var baseCrc) && baseCrc != config.Crc)
            {
                return Results.Conflict(new { error = "the config changed since the editor loaded it; reload to get the current one", crc = config.Crc });
            }
            System.Text.Json.Nodes.JsonNode? body;
            try
            {
                body = await System.Text.Json.Nodes.JsonNode.ParseAsync(req.Body);
            }
            catch (System.Text.Json.JsonException ex)
            {
                return Results.BadRequest(new { error = $"config: invalid JSON ({ex.Message})" });
            }
            if (config.Replace(body) is { } error)
            {
                return Results.BadRequest(new { error });
            }
            // Saving raised ConfigService.Changed, which started the push to a connected device.
            var push = link.IsConnected ? configSync.ChangePush : null;
            var result = push is null ? null : await push;
            return Results.Json(new { saved = true, device = result, crc = config.Crc }, ProtocolJson.Options);
        });
        // The current album art as the device gets it (editor preview).
        app.MapGet("/api/media/art", async (int? size) =>
        {
            var art = media.Art;
            if (art.Source is null || art.TrackId != media.Current.TrackId)
            {
                return Results.NotFound();
            }
            var jpeg = await ArtProcessor.ToJpegAsync(art.Source, Math.Clamp(size ?? 192, 32, 480));
            return jpeg is null ? Results.NotFound() : Results.File(jpeg, "image/jpeg");
        });
        // Uploaded images for image widgets (docs/protocol.md, "Assets").
        app.MapGet("/api/assets", () => Results.Json(assets.List(), ProtocolJson.Options));
        app.MapPost("/api/assets", async (HttpRequest req) =>
        {
            if (!req.HasFormContentType)
            {
                return Results.BadRequest(new { error = "send the image as multipart/form-data field \"file\"" });
            }
            var form = await req.ReadFormAsync();
            if (form.Files.GetFile("file") is not { } file)
            {
                return Results.BadRequest(new { error = "no file" });
            }
            if (file.Length > AssetStore.MaxUploadBytes)
            {
                return Results.BadRequest(new { error = $"images up to {AssetStore.MaxUploadBytes / 1024 / 1024} MB" });
            }
            using var ms = new MemoryStream();
            await file.CopyToAsync(ms);
            try
            {
                return Results.Json(assets.Add(ms.ToArray(), file.FileName), ProtocolJson.Options);
            }
            catch (ArgumentException ex)
            {
                return Results.BadRequest(new { error = ex.Message });
            }
        }).DisableAntiforgery();
        app.MapGet("/api/assets/{id}", (string id) =>
            assets.Original(id) is { } o ? Results.File(o.Path, o.ContentType) : Results.NotFound());
        app.MapDelete("/api/assets/{id}", (string id) =>
        {
            if (AssetFormat.Placements(config.Snapshot()).Any(p => p.Asset == id))
            {
                return Results.Conflict(new { error = "an image widget uses this image; remove the widget first" });
            }
            return assets.Delete(id) ? Results.Ok() : Results.NotFound();
        });
        // The device's stored copy (CONFIG_GET), for diagnostics.
        app.MapGet("/api/device/config", async () =>
        {
            if (!link.IsConnected)
            {
                return Results.Conflict(new { error = "device not connected" });
            }
            var bytes = await configSync.ReadDeviceAsync();
            return bytes is null
                ? Results.Problem("no complete CONFIG_DATA reply from the device", statusCode: 504)
                : Results.Text(System.Text.Encoding.UTF8.GetString(bytes), "application/json");
        });
        // Debug/maintenance commands for the device (docs/protocol.md, CMD 0x60), e.g. {"cmd":"reboot"}.
        app.MapPost("/api/device/cmd", async (HttpRequest req) =>
        {
            CmdMsg? cmd;
            try
            {
                cmd = await System.Text.Json.JsonSerializer.DeserializeAsync<CmdMsg>(req.Body, ProtocolJson.Options);
            }
            catch (System.Text.Json.JsonException)
            {
                cmd = null;
            }
            if (cmd is null || string.IsNullOrWhiteSpace(cmd.Cmd))
            {
                return Results.BadRequest(new { error = "expected {\"cmd\": \"...\", \"args\": {...}}" });
            }
            var sent = link.SendJson(MsgType.Cmd, cmd);
            Log.Info($"cmd {cmd.Cmd} {(sent ? "sent" : "not sent (device not connected)")}");
            return sent ? Results.Accepted() : Results.Conflict(new { error = "device not connected" });
        });

        // Firmware update (Device > Firmware): the image embedded in this host, or an uploaded .bin.
        app.MapGet("/api/firmware", () =>
        {
            var s = link.Status;
            var connected = s.State == DeviceLinkState.Connected;
            var deviceVer = connected ? s.Device?.FwVer : null;
            var bundled = firmware.Bundled;
            return Results.Json(new
            {
                bundled = bundled is null ? null : new { version = bundled.Version, size = bundled.Bytes.Length },
                device = new { connected, version = deviceVer },
                bootloaderPort = FirmwareUpdater.FindBootloaderPort(),
                updateAvailable = connected && bundled?.Version is not null && FirmwareImage.Compare(deviceVer, bundled.Version) < 0,
                status = firmware.Current,
            }, ProtocolJson.Options);
        });
        app.MapPost("/api/firmware/update", async (HttpRequest req) =>
        {
            FirmwareImage image;
            if (req.HasFormContentType && (await req.ReadFormAsync()).Files.GetFile("file") is { } file)
            {
                if (file.Length > 16 * 1024 * 1024)
                {
                    return Results.BadRequest(new { error = "larger than the 16 MB flash" });
                }
                using var ms = new MemoryStream();
                await file.CopyToAsync(ms);
                try
                {
                    image = FirmwareImage.Parse(ms.ToArray());
                }
                catch (InvalidDataException ex)
                {
                    return Results.BadRequest(new { error = $"{file.FileName}: {ex.Message}" });
                }
            }
            else if (firmware.Bundled is { } bundled)
            {
                image = bundled;
            }
            else
            {
                return Results.BadRequest(new { error = "this rez-pad has no firmware built in; choose a .bin file" });
            }
            return firmware.Start(image, out var error)
                ? Results.Accepted(value: new { version = image.Version })
                : Results.Conflict(new { error });
        }).DisableAntiforgery();

        // Knob and button numbering: waits for the next control turned or pressed on the device.
        app.MapPost("/api/device/learn", async (HttpRequest req) =>
        {
            var ms = int.TryParse(req.Query["ms"], out var v) ? v : 15000;
            var r = await learn.WaitAsync(ms, req.HttpContext.RequestAborted);
            return Results.Json(new { kind = r.Kind, port = r.Port, error = r.Error });
        });
        app.MapDelete("/api/device/learn", () =>
        {
            learn.Cancel();
            return Results.NoContent();
        });

        app.StartAsync().GetAwaiter().GetResult();
        _app = app;
    }

    public async Task StopAsync()
    {
        if (_app is not null)
        {
            await _app.StopAsync();
            await _app.DisposeAsync();
        }
    }

    private sealed record WeatherUpdate(string? Name, double? Lat, double? Lon, Units? Units);

    private sealed record StatusDto(
        string HostVersion,
        int ProtocolVersion,
        bool DeviceConnected,
        string LinkState,
        string? Port,
        string? Serial,
        string? FirmwareVersion,
        int? FirmwareProtocol,
        string? Error,
        bool Elevated,
        string? LogFileError,
        SensorAccess Sensors,
        ConfigStatusDto Config,
        uint BadFrames);

    private sealed record ConfigStatusDto(uint Crc, uint? DeviceCrc, bool InSync, ConfigSync.PushResult? LastPush, string? Progress);
}
