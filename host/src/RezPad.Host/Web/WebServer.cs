using System.Net;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.FileProviders;
using Microsoft.Extensions.Logging;
using RezPad.Host.Audio;
using RezPad.Host.Device;
using RezPad.Host.Media;
using RezPad.Protocol;

namespace RezPad.Host.Web;

/// <summary>Kestrel bound to 127.0.0.1 only. Serves web/ and the /api endpoints (docs/config-schema.md).</summary>
internal sealed class WebServer(int port, SerialLink link, MediaService media, AudioService audio)
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
            var stats = link.Stats;
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
                BadFrames: stats is null ? 0 : stats.BadCobs + stats.BadCrc + stats.TooShort + stats.TooLarge + stats.BadVersion));
        });
        app.MapGet("/api/log", () => Results.Json(Log.Recent()));
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
        uint BadFrames);
}
