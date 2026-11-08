using System.Reflection;
using RezPad.Host.Assets;
using RezPad.Host.Audio;
using RezPad.Host.Config;
using RezPad.Host.Device;
using RezPad.Host.Input;
using RezPad.Host.Media;
using RezPad.Host.Stats;
using RezPad.Host.Weather;
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

        // Elevated helper modes for "Start with Windows" (create/delete the scheduled task).
        if (StartupTask.RunHelper(args) is { } helperExit)
        {
            return helperExit;
        }

        // One instance per user session: a second copy would fight over the COM port and the HTTP port.
        // Wait a little for a previous instance that is shutting down (restart through the startup task).
        using var mutex = new Mutex(initiallyOwned: true, @"Local\RezPad.Host", out var createdNew);
        if (!createdNew)
        {
            try
            {
                if (!mutex.WaitOne(TimeSpan.FromSeconds(10)))
                {
                    return 1;
                }
            }
            catch (AbandonedMutexException)
            {
                // The previous instance died without releasing it; it is ours now.
            }
        }

        var port = DefaultPort;
        var portArg = Array.IndexOf(args, "--port");
        if (portArg >= 0 && portArg + 1 < args.Length && int.TryParse(args[portArg + 1], out var p))
        {
            port = p;
        }

        Log.Info($"rez-pad host {Version} starting{(StartupTask.IsElevated() ? " (elevated)" : "")}, log at {Log.LogFile}");
        AppDomain.CurrentDomain.UnhandledException += (_, e) => Log.Error($"unhandled: {e.ExceptionObject}");

        var config = new ConfigService();
        var room = new RoomSensor();
        using var link = new SerialLink(Version);
        var assets = new AssetStore();
        config.AssetExists = assets.Exists;
        var configSync = new ConfigSync(link, config, assets);
        using var audio = new AudioService();
        using var media = new MediaService(() => config.Media);
        using var bridge = new MediaBridge(link, media, audio, () => config.Media);
        configSync.Applied += bridge.OnConfigApplied;
        using var macros = new MacroRunner(config);
        using var mixer = new MixerBridge(link, config, () => config.Media.VolumeStep);
        link.PacketReceived += packet =>
        {
            if (packet.Type == MsgType.Action && ProtocolJson.Deserialize<ActionMsg>(packet.Body) is { } mixAction &&
                mixer.Handle(mixAction))
            {
                return;
            }
            if (packet.Type == MsgType.Action && ProtocolJson.Deserialize<ActionMsg>(packet.Body) is { Action: "macro" } m)
            {
                Log.Info($"button: macro \"{m.Id}\"");
                macros.Run(m.Id ?? "");
                return;
            }
            if (!bridge.HandlePacket(packet) && !configSync.HandlePacket(packet))
            {
                OnPacket(packet, room);
            }
        };

        using var stats = new SystemStatsService(config.StatsRateHz);
        _ = new StatsBridge(link, stats, config);
        using var weather = new WeatherService(config);
        _ = new WeatherBridge(link, weather);
        using var spectrum = new SpectrumBridge(link, config);

        audio.Start();
        media.Start();
        stats.Start();
        weather.Start();
        var web = new WebServer(port, new WebServices(link, media, audio, stats, config, weather, room, configSync, assets, spectrum));
        web.Start();
        link.Start();

        ApplicationConfiguration.Initialize();
        var tray = new TrayContext(web, link, stats);
        var ui = SynchronizationContext.Current ?? new WindowsFormsSynchronizationContext();
        ThreadPool.RegisterWaitForSingleObject(exitSignal, (_, _) => ui.Post(_ => tray.ExitThread(), null), null,
            Timeout.Infinite, executeOnlyOnce: true);
        Application.Run(tray);
        // Free the HTTP port, then let a new instance start right away even if closing the COM port
        // stalls (Windows can take up to 30 s to close a USB CDC port in some states, see
        // docs/measurements.md). The new instance retries the busy port every 0.5 s.
        web.StopAsync().GetAwaiter().GetResult();
        mutex.ReleaseMutex();
        var stopwatch = System.Diagnostics.Stopwatch.StartNew();
        link.Dispose();
        Log.Info($"link closed in {stopwatch.ElapsedMilliseconds} ms");
        Log.Info("rez-pad host stopped");
        return 0;
    }

    private static void OnPacket(Packet packet, RoomSensor room)
    {
        switch (packet.Type)
        {
            case MsgType.Sensor:
                if (ProtocolJson.Deserialize<SensorMsg>(packet.Body) is { } reading)
                {
                    room.Update(reading);
                }
                break;
            case MsgType.Log:
                var log = ProtocolJson.Deserialize<LogMsg>(packet.Body);
                Log.Info($"device {log?.Lvl}: {log?.Msg}");
                break;
            default:
                break;  // CMD_RESULT: logged by the web API caller
        }
    }
}
