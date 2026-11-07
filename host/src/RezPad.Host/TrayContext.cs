using System.Diagnostics;
using RezPad.Host.Device;
using RezPad.Host.Stats;
using RezPad.Host.Web;

namespace RezPad.Host;

/// <summary>Tray icon and menu. The app has no main window; the config editor runs in the browser.</summary>
internal sealed class TrayContext : ApplicationContext
{
    private const string PawnIoUrl = "https://pawnio.eu/";

    private readonly NotifyIcon _icon;
    private readonly ToolStripMenuItem _statusItem;
    private readonly ToolStripMenuItem _sensorItem;
    private readonly ToolStripMenuItem _pawnIoItem;
    private readonly ToolStripMenuItem _startupItem;
    private readonly WebServer _web;
    private readonly SerialLink _link;
    private readonly SystemStatsService _stats;
    private readonly SynchronizationContext _ui;

    public TrayContext(WebServer web, SerialLink link, SystemStatsService stats)
    {
        _web = web;
        _link = link;
        _stats = stats;
        _ui = SynchronizationContext.Current ?? new WindowsFormsSynchronizationContext();

        _statusItem = new ToolStripMenuItem("Looking for rez-pad...") { Enabled = false };
        _sensorItem = new ToolStripMenuItem { Enabled = false, Visible = false };
        _pawnIoItem = new ToolStripMenuItem("Get the PawnIO driver...", null, (_, _) => Open(PawnIoUrl)) { Visible = false };
        _startupItem = new ToolStripMenuItem("Start with Windows (full sensor access)", null, (_, _) => ToggleStartup());
        var menu = new ContextMenuStrip();
        menu.Items.Add(_statusItem);
        menu.Items.Add(_sensorItem);
        menu.Items.Add(_pawnIoItem);
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add("Open config page", null, (_, _) => Open(_web.Url));
        menu.Items.Add("Open log file", null, (_, _) => Open(Log.LogFile));
        menu.Items.Add(_startupItem);
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add("Exit", null, (_, _) => ExitThread());
        // Re-read things that change outside the app (task deleted elsewhere, sensors ready).
        menu.Opening += (_, _) => RefreshMenu();

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
        RefreshMenu();
    }

    private void RefreshMenu()
    {
        var access = _stats.Access;
        _sensorItem.Visible = access.Message is not null;
        _sensorItem.Text = access.Message ?? "";
        _pawnIoItem.Visible = !access.PawnIoInstalled && !access.CpuTempAvailable && access.Message is not null;
        _startupItem.Checked = StartupTask.IsRegistered();
    }

    private void ToggleStartup()
    {
        var enable = !StartupTask.IsRegistered();
        var args = enable
            ? $"{StartupTask.RegisterArg} \"{System.Security.Principal.WindowsIdentity.GetCurrent().Name}\""
            : StartupTask.UnregisterArg;
        if (!StartupTask.RunElevatedHelper(args) || StartupTask.IsRegistered() != enable)
        {
            MessageBox.Show(enable
                    ? "Could not create the startup task (the administrator prompt was declined or failed). See the log file."
                    : "Could not remove the startup task. See the log file.",
                "rez-pad", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            RefreshMenu();
            return;
        }
        RefreshMenu();
        if (enable && !StartupTask.IsElevated() &&
            MessageBox.Show("rez-pad will start with Windows with full sensor access.\n\nRestart it that way now?",
                "rez-pad", MessageBoxButtons.YesNo, MessageBoxIcon.Question) == DialogResult.Yes)
        {
            // The new instance waits for this one to exit (single-instance mutex).
            if (StartupTask.RunNow())
            {
                Log.Info("restarting through the startup task");
                ExitThread();
            }
            else
            {
                MessageBox.Show("Windows did not start the task. It will run at the next logon.", "rez-pad",
                    MessageBoxButtons.OK, MessageBoxIcon.Information);
            }
        }
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
