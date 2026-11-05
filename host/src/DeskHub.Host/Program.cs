using DeskHub.Host.Web;

namespace DeskHub.Host;

internal static class Program
{
    public const int DefaultPort = 47800;

    [STAThread]
    private static int Main(string[] args)
    {
        // One instance per user session: a second copy would fight over the COM port and the HTTP port.
        using var mutex = new Mutex(initiallyOwned: true, @"Local\DeskHub.Host", out var createdNew);
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

        var web = new WebServer(port);
        web.Start();

        ApplicationConfiguration.Initialize();
        Application.Run(new TrayContext(web));

        web.StopAsync().GetAwaiter().GetResult();
        return 0;
    }
}
