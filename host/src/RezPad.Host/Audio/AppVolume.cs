using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using RezPad.Protocol;

namespace RezPad.Host.Audio;

/// <summary>Volume of one app as the Windows volume mixer shows it.</summary>
internal readonly record struct AppVolumeState(string Name, int Level, bool Muted);

/// <summary>
/// Per-app volume through Core Audio sessions (the Windows volume mixer) on the default playback
/// device. An app is found by its GSMTC source app id (the Now Playing session): matched against each
/// audio session's process name, and the package's AppUserModelId for Store apps. All matching
/// sessions are changed together (browsers and some players have several). Each call enumerates the
/// sessions afresh, on a thread-pool (MTA) thread.
/// </summary>
internal static class AppVolume
{
    private const int ClsctxAll = 0x17;
    private const int SessionExpired = 2;  // AudioSessionStateExpired

    /// <summary>Current volume of the app, or null when it has no audio session.</summary>
    public static AppVolumeState? Get(string appId, string name) => Run(appId, sessions =>
    {
        sessions[0].GetMasterVolume(out var level);
        sessions[0].GetMute(out var muted);
        return new AppVolumeState(name, ToPercent(level), muted);
    });

    /// <summary>Changes the app's volume by <paramref name="delta"/> percent (turning up also unmutes).</summary>
    public static AppVolumeState? Step(string appId, string name, int delta) => Run(appId, sessions =>
    {
        sessions[0].GetMasterVolume(out var current);
        sessions[0].GetMute(out var muted);
        var level = Math.Clamp(ToPercent(current) + delta, 0, 100);
        var ctx = Guid.Empty;
        foreach (var s in sessions)
        {
            s.SetMasterVolume(level / 100f, ref ctx);
            if (delta > 0 && muted)
            {
                s.SetMute(false, ref ctx);
            }
        }
        return new AppVolumeState(name, level, delta > 0 ? false : muted);
    });

    public static AppVolumeState? ToggleMute(string appId, string name) => Run(appId, sessions =>
    {
        sessions[0].GetMasterVolume(out var level);
        sessions[0].GetMute(out var muted);
        var ctx = Guid.Empty;
        foreach (var s in sessions)
        {
            s.SetMute(!muted, ref ctx);
        }
        return new AppVolumeState(name, ToPercent(level), !muted);
    });

    private static int ToPercent(float scalar) => (int)Math.Round(Math.Clamp(scalar, 0f, 1f) * 100);

    private static AppVolumeState? Run(string appId, Func<List<ISimpleAudioVolume>, AppVolumeState> action)
    {
        return Task.Run(() =>
        {
            var objects = new List<object>();
            try
            {
                var sessions = FindSessions(appId, objects);
                return sessions.Count == 0 ? (AppVolumeState?)null : action(sessions);
            }
            catch (Exception ex) when (ex is COMException or InvalidCastException)
            {
                Log.Warn($"app volume: {ex.Message}");
                return null;
            }
            finally
            {
                foreach (var o in objects)
                {
                    Marshal.ReleaseComObject(o);
                }
            }
        }).GetAwaiter().GetResult();
    }

    private static List<ISimpleAudioVolume> FindSessions(string appId, List<object> keep)
    {
        var result = new List<ISimpleAudioVolume>();
        var enumerator = (IMMDeviceEnumerator)new MMDeviceEnumeratorComObject();
        keep.Add(enumerator);
        if (enumerator.GetDefaultAudioEndpoint(EDataFlow.Render, ERole.Multimedia, out var device) != 0 || device is null)
        {
            return result;
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
            var control2 = (IAudioSessionControl2)control;
            if (control2.GetState(out var state) != 0 || state == SessionExpired ||
                control2.IsSystemSoundsSession() == 0 || control2.GetProcessId(out var pid) != 0 || pid == 0)
            {
                continue;
            }
            if (!ProcessInfo(pid, out var processName, out var aumid) || !AppIds.Matches(appId, processName, aumid))
            {
                continue;
            }
            result.Add((ISimpleAudioVolume)control);
        }
        return result;
    }

    private static bool ProcessInfo(uint pid, out string name, out string? aumid)
    {
        name = "";
        aumid = null;
        try
        {
            using var p = Process.GetProcessById((int)pid);
            name = p.ProcessName;
        }
        catch (Exception ex) when (ex is ArgumentException or InvalidOperationException)
        {
            return false;  // exited
        }
        var handle = OpenProcess(ProcessQueryLimitedInformation, false, pid);
        if (handle == IntPtr.Zero)
        {
            return true;
        }
        try
        {
            var length = 256u;
            var buffer = new StringBuilder((int)length);
            if (GetApplicationUserModelId(handle, ref length, buffer) == 0)
            {
                aumid = buffer.ToString();  // only packaged (Store) apps have one
            }
        }
        finally
        {
            CloseHandle(handle);
        }
        return true;
    }

    private const uint ProcessQueryLimitedInformation = 0x1000;

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);

    [DllImport("kernel32.dll")]
    private static extern bool CloseHandle(IntPtr handle);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetApplicationUserModelId(IntPtr process, ref uint length, StringBuilder id);
}

// Audio session interfaces (audiopolicy.h, audioclient.h). Method order must match the native vtable,
// including the methods inherited from IAudioSessionManager / IAudioSessionControl.

[ComImport]
[Guid("77AA99A0-1BD6-484F-8BC7-2C654C9A9B6F")]
[InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
internal interface IAudioSessionManager2
{
    [PreserveSig]
    int GetAudioSessionControl(IntPtr sessionGuid, int flags, out IntPtr control);

    [PreserveSig]
    int GetSimpleAudioVolume(IntPtr sessionGuid, int flags, out IntPtr volume);

    [PreserveSig]
    int GetSessionEnumerator(out IAudioSessionEnumerator enumerator);
}

[ComImport]
[Guid("E2F5BB11-0570-40CA-ACDD-3AA01277DEE8")]
[InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
internal interface IAudioSessionEnumerator
{
    [PreserveSig]
    int GetCount(out int count);

    [PreserveSig]
    int GetSession(int index, [MarshalAs(UnmanagedType.IUnknown)] out object? session);
}

[ComImport]
[Guid("bfb7ff88-7239-4fc9-8fa2-07c950be9c6d")]
[InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
internal interface IAudioSessionControl2
{
    // IAudioSessionControl
    [PreserveSig]
    int GetState(out int state);

    [PreserveSig]
    int GetDisplayName(out IntPtr name);

    [PreserveSig]
    int SetDisplayName(IntPtr name, ref Guid context);

    [PreserveSig]
    int GetIconPath(out IntPtr path);

    [PreserveSig]
    int SetIconPath(IntPtr path, ref Guid context);

    [PreserveSig]
    int GetGroupingParam(out Guid param);

    [PreserveSig]
    int SetGroupingParam(ref Guid param, ref Guid context);

    [PreserveSig]
    int RegisterAudioSessionNotification(IntPtr events);

    [PreserveSig]
    int UnregisterAudioSessionNotification(IntPtr events);

    // IAudioSessionControl2
    [PreserveSig]
    int GetSessionIdentifier(out IntPtr id);

    [PreserveSig]
    int GetSessionInstanceIdentifier(out IntPtr id);

    [PreserveSig]
    int GetProcessId(out uint pid);

    /// <summary>S_OK (0) for the system sounds session, S_FALSE (1) otherwise.</summary>
    [PreserveSig]
    int IsSystemSoundsSession();

    [PreserveSig]
    int SetDuckingPreference([MarshalAs(UnmanagedType.Bool)] bool optOut);
}

[ComImport]
[Guid("87CE5498-68D6-44E5-9215-6DA47EF883D8")]
[InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
internal interface ISimpleAudioVolume
{
    [PreserveSig]
    int SetMasterVolume(float level, ref Guid context);

    [PreserveSig]
    int GetMasterVolume(out float level);

    [PreserveSig]
    int SetMute([MarshalAs(UnmanagedType.Bool)] bool mute, ref Guid context);

    [PreserveSig]
    int GetMute([MarshalAs(UnmanagedType.Bool)] out bool mute);
}
