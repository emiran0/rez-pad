using System.Diagnostics;
using RezPad.Host.Device;
using RezPad.Host.Web;

namespace RezPad.Host;

/// <summary>Tray icon and menu. The app has no main window; the config editor runs in the browser.</summary>
internal sealed class TrayContext : ApplicationContext
{
    private readonly NotifyIcon _icon;
    private readonly ToolStripMenuItem _statusItem;
    private readonly WebServer _web;
    private readonly SerialLink _link;
    private readonly SynchronizationContext _ui;

    public TrayContext(WebServer web, SerialLink link)
    {
        _web = web;
        _link = link;
        _ui = SynchronizationContext.Current ?? new WindowsFormsSynchronizationContext();

        _statusItem = new ToolStripMenuItem("Looking for rez-pad...") { Enabled = false };
        var menu = new ContextMenuStrip();
        menu.Items.Add(_statusItem);
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add("Open config page", null, (_, _) => Open(_web.Url));
        menu.Items.Add("Open log file", null, (_, _) => Open(Log.LogFile));
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add("Exit", null, (_, _) => ExitThread());

        _icon = new NotifyIcon
        {
            Icon = SystemIcons.Application, // placeholder until the app icon exists (Phase 11)
            Text = "rez-pad",
            ContextMenuStrip = menu,
            Visible = true,
        };
        _icon.DoubleClick += (_, _) => Open(_web.Url);

        _link.StatusChanged += status => _ui.Post(_ => ShowStatus(status), null);
        ShowStatus(_link.Status);
    }

    private void ShowStatus(LinkStatus s)
    {
        var text = s.State switch
        {
            DeviceLinkState.Connected => $"Connected ({s.Port}, firmware {s.Device?.FwVer})",
            DeviceLinkState.Connecting => $"Connecting ({s.Port})...",
            DeviceLinkState.Incompatible => $"Incompatible firmware: {s.Error}",
            _ => "Looking for rez-pad...",
        };
        _statusItem.Text = text;
        var tip = $"rez-pad: {text}";
        _icon.Text = tip.Length > 127 ? tip[..127] : tip;
    }

    private static void Open(string target)
    {
        try
        {
            Process.Start(new ProcessStartInfo(target) { UseShellExecute = true });
        }
        catch (Exception ex) when (ex is System.ComponentModel.Win32Exception or FileNotFoundException)
        {
            Log.Warn($"could not open {target}: {ex.Message}");
        }
    }

    protected override void ExitThreadCore()
    {
        _icon.Visible = false;
        _icon.Dispose();
        base.ExitThreadCore();
    }
}
