using System.ComponentModel;
using System.Diagnostics;
using System.Security;
using System.Security.Principal;
using System.Text;

namespace RezPad.Host;

/// <summary>
/// "Start with Windows": a Task Scheduler task that starts the host at logon with highest
/// privileges (temperatures need admin; GSMTC needs the user session, so no Windows service).
/// Creating or deleting the task needs admin, so the tray app relaunches itself elevated with
/// <c>--register-startup</c> / <c>--unregister-startup</c> (one UAC prompt).
/// </summary>
internal static class StartupTask
{
    public const string TaskName = "rez-pad";
    public const string RegisterArg = "--register-startup";
    public const string UnregisterArg = "--unregister-startup";

    public static bool IsElevated()
    {
        using var id = WindowsIdentity.GetCurrent();
        return new WindowsPrincipal(id).IsInRole(WindowsBuiltInRole.Administrator);
    }

    public static bool IsRegistered() => Schtasks($"/Query /TN \"{TaskName}\"", out _) == 0;

    /// <summary>
    /// Handles the elevated helper modes. Returns an exit code, or null when <paramref name="args"/>
    /// is a normal start.
    /// </summary>
    public static int? RunHelper(string[] args)
    {
        var reg = Array.IndexOf(args, RegisterArg);
        if (reg >= 0)
        {
            // The user name comes from the unelevated instance: with an over-the-shoulder UAC
            // prompt the elevated process may run as a different (admin) account.
            var user = reg + 1 < args.Length ? args[reg + 1] : WindowsIdentity.GetCurrent().Name;
            return Register(user, Environment.ProcessPath!);
        }
        if (args.Contains(UnregisterArg))
        {
            var code = Schtasks($"/Delete /TN \"{TaskName}\" /F", out var output);
            Log.Info($"startup task removed: exit {code} {output.Trim()}");
            return code;
        }
        return null;
    }

    /// <summary>Runs this exe elevated with the given helper arguments and waits. False if the UAC prompt was declined or it failed.</summary>
    public static bool RunElevatedHelper(string arguments)
    {
        try
        {
            using var p = Process.Start(new ProcessStartInfo(Environment.ProcessPath!, arguments)
            {
                UseShellExecute = true,
                Verb = "runas",
            });
            if (p is null)
            {
                return false;
            }
            p.WaitForExit(30000);
            return p.HasExited && p.ExitCode == 0;
        }
        catch (Win32Exception ex)
        {
            Log.Info($"startup task: elevation declined ({ex.Message})");
            return false;
        }
    }

    /// <summary>Starts the registered task now (an elevated instance). Returns false if Windows refused.</summary>
    public static bool RunNow() => Schtasks($"/Run /TN \"{TaskName}\"", out _) == 0;

    private static int Register(string user, string exe)
    {
        var xml = $"""
            <?xml version="1.0" encoding="UTF-16"?>
            <Task version="1.2" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">
              <RegistrationInfo>
                <Description>Starts the rez-pad host at logon with access to hardware sensors.</Description>
              </RegistrationInfo>
              <Triggers>
                <LogonTrigger>
                  <Enabled>true</Enabled>
                  <UserId>{SecurityElement.Escape(user)}</UserId>
                  <Delay>PT5S</Delay>
                </LogonTrigger>
              </Triggers>
              <Principals>
                <Principal id="Author">
                  <UserId>{SecurityElement.Escape(user)}</UserId>
                  <LogonType>InteractiveToken</LogonType>
                  <RunLevel>HighestAvailable</RunLevel>
                </Principal>
              </Principals>
              <Settings>
                <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>
                <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>
                <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>
                <AllowStartOnDemand>true</AllowStartOnDemand>
                <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>
                <Priority>5</Priority>
              </Settings>
              <Actions Context="Author">
                <Exec>
                  <Command>"{SecurityElement.Escape(exe)}"</Command>
                  <WorkingDirectory>{SecurityElement.Escape(Path.GetDirectoryName(exe))}</WorkingDirectory>
                </Exec>
              </Actions>
            </Task>
            """;
        var file = Path.Combine(Path.GetTempPath(), $"rez-pad-task-{Environment.ProcessId}.xml");
        try
        {
            File.WriteAllText(file, xml, Encoding.Unicode);  // schtasks wants UTF-16 for /XML
            var code = Schtasks($"/Create /TN \"{TaskName}\" /XML \"{file}\" /F", out var output);
            Log.Info($"startup task for {user}: exit {code} {output.Trim()}");
            return code;
        }
        finally
        {
            File.Delete(file);
        }
    }

    private static int Schtasks(string arguments, out string output)
    {
        var psi = new ProcessStartInfo("schtasks.exe", arguments)
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
        };
        using var p = Process.Start(psi)!;
        output = p.StandardOutput.ReadToEnd() + p.StandardError.ReadToEnd();
        p.WaitForExit();
        return p.ExitCode;
    }
}
