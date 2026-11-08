using System.Collections.Concurrent;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using RezPad.Host.Media;
using RezPad.Protocol;

namespace RezPad.Host.Audio;

/// <summary>
/// The Windows volume mixer of the default playback device: apps with audio sessions, grouped by
/// process like the mixer, with their volume and mute (Core Audio session API). Every call enumerates
/// the sessions afresh on a thread-pool (MTA) thread; an app's change applies to all its sessions.
/// </summary>
internal static class MixerService
{
    public const int MaxApps = 8;
    public const string SystemId = "system";
    private const int ClsctxAll = 0x17;
    private const int SessionExpired = 2;
    private const float ActivePeak = 0.001f;

    private static readonly ConcurrentDictionary<string, string> Names = new();

    /// <summary>
    /// The mixer's apps: those in <paramref name="pinned"/> first (mixer widgets' priority lists, so
    /// the 8-app limit never drops them), then the rest by name, System sounds last; at most
    /// <see cref="MaxApps"/>.
    /// </summary>
    public static IReadOnlyList<MixerAppMsg> Snapshot(IReadOnlyList<string>? pinned = null) => Run(groups =>
    {
        var apps = new List<MixerAppMsg>();
        foreach (var (id, g) in groups)
        {
            g.Volumes[0].GetMasterVolume(out var level);
            g.Volumes[0].GetMute(out var muted);
            apps.Add(new MixerAppMsg(id, g.Name, ToPercent(level), muted, g.Active));
        }
        var pins = (pinned ?? []).ToList();
        return apps
            .OrderBy(a => pins.IndexOf(a.Id) is var k && k >= 0 ? k : int.MaxValue)
            .ThenBy(a => a.Id == SystemId)
            .ThenBy(a => a.Name, StringComparer.CurrentCultureIgnoreCase)
            .Take(MaxApps)
            .ToList();
    }) ?? [];

    /// <summary>Steps app <paramref name="id"/> by <paramref name="delta"/> percent (up also unmutes). False when it is gone.</summary>
    public static bool Step(string id, int delta) => Run(groups =>
    {
        if (!groups.TryGetValue(id, out var g))
        {
            return false;
        }
        g.Volumes[0].GetMasterVolume(out var current);
        g.Volumes[0].GetMute(out var muted);
        var level = Math.Clamp(ToPercent(current) + delta, 0, 100);
        var ctx = Guid.Empty;
        foreach (var v in g.Volumes)
        {
            v.SetMasterVolume(level / 100f, ref ctx);
            if (delta > 0 && muted)
            {
                v.SetMute(false, ref ctx);
            }
        }
        return true;
    });

    public static bool ToggleMute(string id) => Run(groups =>
    {
        if (!groups.TryGetValue(id, out var g))
        {
            return false;
        }
        g.Volumes[0].GetMute(out var muted);
        var ctx = Guid.Empty;
        foreach (var v in g.Volumes)
        {
            v.SetMute(!muted, ref ctx);
        }
        return true;
    });

    private sealed class Group(string name)
    {
        public string Name { get; } = name;
        public List<ISimpleAudioVolume> Volumes { get; } = [];
        public bool Active { get; set; }
    }

    private static int ToPercent(float scalar) => (int)Math.Round(Math.Clamp(scalar, 0f, 1f) * 100);

    private static T? Run<T>(Func<Dictionary<string, Group>, T> action)
    {
        return Task.Run(() =>
        {
            var keep = new List<object>();
            try
            {
                return action(Enumerate(keep));
            }
            catch (Exception ex) when (ex is COMException or InvalidCastException)
            {
                Log.Warn($"mixer: {ex.Message}");
                return default;
            }
            finally
            {
                foreach (var o in keep)
                {
                    Marshal.ReleaseComObject(o);
                }
            }
        }).GetAwaiter().GetResult();
    }

    private static Dictionary<string, Group> Enumerate(List<object> keep)
    {
        var groups = new Dictionary<string, Group>();
        var enumerator = (IMMDeviceEnumerator)new MMDeviceEnumeratorComObject();
        keep.Add(enumerator);
        if (enumerator.GetDefaultAudioEndpoint(EDataFlow.Render, ERole.Multimedia, out var device) != 0 || device is null)
        {
            return groups;
        }
        keep.Add(device);
        var iid = typeof(IAudioSessionManager2).GUID;
        Marshal.ThrowExceptionForHR(device.Activate(ref iid, ClsctxAll, IntPtr.Zero, out var obj));
        var manager = (IAudioSessionManager2)obj!;
        keep.Add(manager);
        Marshal.ThrowExceptionForHR(manager.GetSessionEnumerator(out var list));
        keep.Add(list);
        Marshal.ThrowExceptionForHR(list.GetCount(out var count));
        for (var i = 0; i < count; i++)
        {
            if (list.GetSession(i, out var control) != 0 || control is null)
            {
                continue;
            }
            keep.Add(control);
            var c2 = (IAudioSessionControl2)control;
            if (c2.GetState(out var state) != 0 || state == SessionExpired)
            {
                continue;
            }
            string id, name;
            if (c2.IsSystemSoundsSession() == 0)
            {
                (id, name) = (SystemId, "System sounds");
            }
            else if (c2.GetProcessId(out var pid) != 0 || pid == 0 || pid == Environment.ProcessId ||
                     !Identify(pid, out id, out name))
            {
                continue;  // also skips our own session (the spectrum's loopback capture)
            }
            if (!groups.TryGetValue(id, out var g))
            {
                groups[id] = g = new Group(name);
            }
            g.Volumes.Add((ISimpleAudioVolume)control);
            if (!g.Active && ((IAudioMeterInformation)control).GetPeakValue(out var peak) == 0 && peak > ActivePeak)
            {
                g.Active = true;
            }
        }
        return groups;
    }

    /// <summary>App key (lowercase process name) and display name of a process.</summary>
    private static bool Identify(uint pid, out string id, out string name)
    {
        id = name = "";
        string processName;
        string? file = null;
        try
        {
            using var p = Process.GetProcessById((int)pid);
            processName = p.ProcessName;
            try
            {
                file = p.MainModule?.FileName;
            }
            catch (Exception ex) when (ex is System.ComponentModel.Win32Exception or InvalidOperationException)
            {
                // Protected or 32/64-bit mismatch: the process name has to do.
            }
        }
        catch (Exception ex) when (ex is ArgumentException or InvalidOperationException)
        {
            return false;  // exited
        }
        id = Clip(processName.ToLowerInvariant());
        var key = id;
        name = Names.GetOrAdd(key, _ => Clip(DisplayName(pid, processName, file)));
        return true;
    }

    private static string DisplayName(uint pid, string processName, string? file)
    {
        if (AppUserModelId(pid) is { } aumid)
        {
            var n = MediaService.AppName(aumid);  // Store app: its package display name
            if (!string.IsNullOrWhiteSpace(n))
            {
                return n;
            }
        }
        if (file is not null)
        {
            try
            {
                var desc = FileVersionInfo.GetVersionInfo(file).FileDescription?.Trim();
                if (!string.IsNullOrEmpty(desc))
                {
                    return desc;
                }
            }
            catch (FileNotFoundException)
            {
            }
        }
        return processName.Length > 0 ? char.ToUpperInvariant(processName[0]) + processName[1..] : processName;
    }

    /// <summary>At most 23 UTF-8 bytes (the device's field size), cut at a character boundary.</summary>
    public static string Clip(string s)
    {
        if (Encoding.UTF8.GetByteCount(s) <= 23)
        {
            return s;
        }
        var sb = new StringBuilder();
        var bytes = 0;
        foreach (var rune in s.EnumerateRunes())
        {
            var n = rune.Utf8SequenceLength;
            if (bytes + n > 23)
            {
                break;
            }
            sb.Append(rune.ToString());
            bytes += n;
        }
        return sb.ToString().TrimEnd();
    }

    private static string? AppUserModelId(uint pid)
    {
        var handle = OpenProcess(0x1000, false, pid);  // PROCESS_QUERY_LIMITED_INFORMATION
        if (handle == IntPtr.Zero)
        {
            return null;
        }
        try
        {
            var length = 256u;
            var buffer = new StringBuilder((int)length);
            return GetApplicationUserModelId(handle, ref length, buffer) == 0 ? buffer.ToString() : null;
        }
        finally
        {
            CloseHandle(handle);
        }
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);

    [DllImport("kernel32.dll")]
    private static extern bool CloseHandle(IntPtr handle);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetApplicationUserModelId(IntPtr process, ref uint length, StringBuilder id);
}

[ComImport]
[Guid("C02216F6-8C67-4B5B-9D00-D008E73E0064")]
[InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
internal interface IAudioMeterInformation
{
    [PreserveSig]
    int GetPeakValue(out float peak);
}
