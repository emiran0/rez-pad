using System.Net;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.FileProviders;
using Microsoft.Extensions.Logging;
using RezPad.Host.Audio;
using RezPad.Host.Device;
using RezPad.Host.Media;
using RezPad.Host.Stats;
using RezPad.Protocol;

namespace RezPad.Host.Web;

/// <summary>Kestrel bound to 127.0.0.1 only. Serves web/ and the /api endpoints (docs/config-schema.md).</summary>
internal sealed class WebServer(int port, SerialLink link, MediaService media, AudioService audio,
    SystemStatsService stats)
{
    private WebApplication? _app;

    public string Url { get; } = $"http://127.0.0.1:{port}/";

    public void Start()
    {
        var builder = WebApplication.CreateSlimBuilder();
        builder.WebHost.ConfigureKestrel(o => o.Listen(IPAddress.Loopback, port));
        builder.Logging.SetMinimumLevel(LogLevel.Warning);

        var app = builder.Build();

        var webRoot = Path.Combine(AppContext.BaseDirectory, "web");
        if (Directory.Exists(webRoot))
        {
            var files = new PhysicalFileProvider(webRoot);
            app.UseDefaultFiles(new DefaultFilesOptions { FileProvider = files });
            app.UseStaticFiles(new StaticFileOptions { FileProvider = files });
        }

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
                BadFrames: decoder is null ? 0 : decoder.BadCobs + decoder.BadCrc + decoder.TooShort + decoder.TooLarge + decoder.BadVersion));
        });
        app.MapGet("/api/log", () => Results.Json(Log.Recent()));
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
            var used = StatsBridge.DefaultLayoutSources;
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
                }),
            }, ProtocolJson.Options);
        });
        // What the host sends as NOW_PLAYING and VOLUME right now (debugging aid).
        app.MapGet("/api/media", () =>
        {
            var art = media.Art;
            var vol = audio.State;
            return Results.Json(new
            {
                now_playing = media.Current.ToMessage(DateTimeOffset.UtcNow),
                art = new { track_id = art.TrackId, source_bytes = art.Source?.Length ?? 0 },
                volume = new { level = vol.Level, muted = vol.Muted, available = vol.Available },
            }, ProtocolJson.Options);
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
        uint BadFrames);
}
