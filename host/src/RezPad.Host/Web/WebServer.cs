using System.Net;
using System.Reflection;
using RezPad.Protocol;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.FileProviders;
using Microsoft.Extensions.Logging;

namespace RezPad.Host.Web;

/// <summary>Kestrel bound to 127.0.0.1 only. Serves web/ and the /api endpoints (docs/config-schema.md).</summary>
internal sealed class WebServer(int port)
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

        var hostVersion = Assembly.GetExecutingAssembly().GetName().Version?.ToString(3) ?? "0.0.0";
        app.MapGet("/api/status", () => Results.Json(new StatusDto(
            HostVersion: hostVersion,
            ProtocolVersion: Frame.ProtocolVersion,
            DeviceConnected: false)));

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

    private sealed record StatusDto(string HostVersion, int ProtocolVersion, bool DeviceConnected);
}
