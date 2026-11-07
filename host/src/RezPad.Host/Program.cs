using System.Reflection;
using RezPad.Host.Audio;
using RezPad.Host.Device;
using RezPad.Host.Media;
using RezPad.Host.Web;
using RezPad.Protocol;

namespace RezPad.Host;

internal static class Program
{
    public const int DefaultPort = 47800;
    private const string ExitEventName = @"Local\RezPad.Host.Exit";

    public static string Version { get; } =
        Assembly.GetExecutingAssembly().GetName().Version?.ToString(3) ?? "0.0.0";

    [STAThread]
    private static int Main(string[] args)
    {
        // `rez-pad.exe --exit` asks the running instance to quit cleanly (used by installers/updaters).
        using var exitSignal = new EventWaitHandle(false, EventResetMode.AutoReset, ExitEventName);
        if (args.Contains("--exit"))
        {
            exitSignal.Set();
            return 0;
        }

        // One instance per user session: a second copy would fight over the COM port and the HTTP port.
        using var mutex = new Mutex(initiallyOwned: true, @"Local\RezPad.Host", out var createdNew);
        if (!createdNew)
        {
            return 1;
        }

        var port = DefaultPort;
        var portArg = Array.IndexOf(args, "--port");
        if (portArg >= 0 && portArg + 1 < args.Length && int.TryParse(args[portArg + 1], out var p))
        {
            port = p;
        }

        Log.Info($"rez-pad host {Version} starting, log at {Log.LogFile}");
        AppDomain.CurrentDomain.UnhandledException += (_, e) => Log.Error($"unhandled: {e.ExceptionObject}");

        using var link = new SerialLink(Version);
        var mediaOptions = MediaOptions.Default;
        using var audio = new AudioService();
        using var media = new MediaService(mediaOptions);
        using var bridge = new MediaBridge(link, media, audio, mediaOptions);
        link.PacketReceived += packet =>
        {
            if (!bridge.HandlePacket(packet))
            {
                OnPacket(packet);
            }
        };

        audio.Start();
        media.Start();
        var web = new WebServer(port, link, media, audio);
        web.Start();
        link.Start();

        ApplicationConfiguration.Initialize();
        var tray = new TrayContext(web, link);
        var ui = SynchronizationContext.Current ?? new WindowsFormsSynchronizationContext();
        ThreadPool.RegisterWaitForSingleObject(exitSignal, (_, _) => ui.Post(_ => tray.ExitThread(), null), null,
            Timeout.Infinite, executeOnlyOnce: true);
        Application.Run(tray);
        // Let a new instance start right away even if closing the port stalls (Windows can take up
        // to 30 s to close a USB CDC port in some states, see docs/measurements.md). The new
        // instance retries the busy port every 0.5 s.
        mutex.ReleaseMutex();
        var stopwatch = System.Diagnostics.Stopwatch.StartNew();
        link.Dispose();
        Log.Info($"link closed in {stopwatch.ElapsedMilliseconds} ms");

        web.StopAsync().GetAwaiter().GetResult();
        Log.Info("rez-pad host stopped");
        return 0;
    }

    private static void OnPacket(Packet packet)
    {
        switch (packet.Type)
        {
            case MsgType.Log:
                var log = ProtocolJson.Deserialize<LogMsg>(packet.Body);
                Log.Info($"device {log?.Lvl}: {log?.Msg}");
                break;
            default:
                break;  // SENSOR, CONFIG_*, CMD_RESULT: later phases
        }
    }
}
