using System.Diagnostics;
using RezPad.Host.Web;

namespace RezPad.Host;

/// <summary>Tray icon and menu. The app has no main window; the config editor runs in the browser.</summary>
internal sealed class TrayContext : ApplicationContext
{
    private readonly NotifyIcon _icon;
    private readonly WebServer _web;

    public TrayContext(WebServer web)
    {
        _web = web;
        var menu = new ContextMenuStrip();
        menu.Items.Add("Open config page", null, (_, _) => OpenConfigPage());
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add("Exit", null, (_, _) => ExitThread());

        _icon = new NotifyIcon
        {
            Icon = SystemIcons.Application, // placeholder until the app icon exists (Phase 11)
            Text = "rez-pad",
            ContextMenuStrip = menu,
            Visible = true,
        };
        _icon.DoubleClick += (_, _) => OpenConfigPage();
    }

    private void OpenConfigPage()
    {
        Process.Start(new ProcessStartInfo(_web.Url) { UseShellExecute = true });
    }

    protected override void ExitThreadCore()
    {
        _icon.Visible = false;
        _icon.Dispose();
        base.ExitThreadCore();
    }
}
