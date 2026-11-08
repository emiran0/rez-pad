using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text.Json.Nodes;
using RezPad.Host.Config;
using RezPad.Protocol;

namespace RezPad.Host.Input;

/// <summary>
/// Runs the config's macros when a button asks for one (ACTION "macro", docs/config-schema.md
/// "Buttons and macros"). Steps: key shortcuts and text through SendInput (text as Unicode, so it
/// types correctly on any keyboard layout), waits, and opening a program, file or address. One
/// macro at a time, in the order the presses arrived.
/// </summary>
internal sealed class MacroRunner : IDisposable
{
    private readonly ConfigService _config;
    private readonly System.Threading.Channels.Channel<string> _queue =
        System.Threading.Channels.Channel.CreateBounded<string>(8);
    private readonly CancellationTokenSource _cts = new();

    public MacroRunner(ConfigService config)
    {
        _config = config;
        _ = Task.Run(RunAsync);
    }

    /// <summary>Queues a macro by id; returns false when it does not exist or too many are waiting.</summary>
    public bool Run(string id)
    {
        if (Find(id) is null)
        {
            Log.Warn($"macro \"{id}\" not found in the config");
            return false;
        }
        return _queue.Writer.TryWrite(id);
    }

    private JsonObject? Find(string id) =>
        (_config.Snapshot()["macros"] as JsonArray)?.OfType<JsonObject>().FirstOrDefault(m => (string?)m["id"] == id);

    private async Task RunAsync()
    {
        await foreach (var id in _queue.Reader.ReadAllAsync(_cts.Token))
        {
            if (Find(id) is not { } macro)
            {
                continue;
            }
            var sw = Stopwatch.StartNew();
            try
            {
                foreach (var step in (macro["steps"] as JsonArray ?? []).OfType<JsonObject>())
                {
                    if ((string?)step["keys"] is { } keys)
                    {
                        SendKeys(keys);
                    }
                    else if ((string?)step["text"] is { } text)
                    {
                        SendText(text);
                    }
                    else if ((int?)step["wait_ms"] is { } ms)
                    {
                        await Task.Delay(Math.Clamp(ms, 1, 10000), _cts.Token);
                    }
                    else if ((string?)step["open"] is { } target)
                    {
                        Open(target);
                    }
                }
                Log.Info($"macro \"{id}\" ran in {sw.ElapsedMilliseconds} ms");
            }
            catch (Exception ex) when (ex is not OperationCanceledException)
            {
                Log.Warn($"macro \"{id}\" failed: {ex.Message}");
            }
        }
    }

    // ---- steps ----

    private static void SendKeys(string keys)
    {
        if (KeyCombo.Parse(keys) is not { } k)
        {
            return;
        }
        var inputs = new List<INPUT>();
        foreach (var vk in k.ModifierVks)
        {
            inputs.Add(Key(vk, up: false, extended: vk is 0xA5 or 0x5B));
        }
        if (k.Key is { } main)
        {
            inputs.Add(Key(main.Vk, up: false, main.Extended));
            inputs.Add(Key(main.Vk, up: true, main.Extended));
        }
        foreach (var vk in k.ModifierVks.Reverse())
        {
            inputs.Add(Key(vk, up: true, extended: vk is 0xA5 or 0x5B));
        }
        Send(inputs);
    }

    private static void SendText(string text)
    {
        var inputs = new List<INPUT>();
        foreach (var c in text.Replace("\r\n", "\n", StringComparison.Ordinal))
        {
            if (c == '\n')
            {
                inputs.Add(Key(0x0D, up: false, extended: false));
                inputs.Add(Key(0x0D, up: true, extended: false));
                continue;
            }
            inputs.Add(Unicode(c, up: false));
            inputs.Add(Unicode(c, up: true));
        }
        Send(inputs);
    }

    // The host runs elevated (Start with Windows); starting through Explorer gives the program the
    // normal user rights instead of administrator rights.
    private static void Open(string target)
    {
        if (Environment.IsPrivilegedProcess)
        {
            Process.Start(new ProcessStartInfo("explorer.exe") { ArgumentList = { target }, UseShellExecute = false });
        }
        else
        {
            Process.Start(new ProcessStartInfo(target) { UseShellExecute = true });
        }
    }

    // ---- SendInput ----

    private const uint InputKeyboard = 1;
    private const uint KeyeventfExtendedkey = 0x0001;
    private const uint KeyeventfKeyup = 0x0002;
    private const uint KeyeventfUnicode = 0x0004;

    [StructLayout(LayoutKind.Sequential)]
    private struct KEYBDINPUT
    {
        public ushort WVk;
        public ushort WScan;
        public uint DwFlags;
        public uint Time;
        public IntPtr DwExtraInfo;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct MOUSEINPUT
    {
        public int Dx;
        public int Dy;
        public uint MouseData;
        public uint DwFlags;
        public uint Time;
        public IntPtr DwExtraInfo;
    }

    [StructLayout(LayoutKind.Explicit)]
    private struct InputUnion
    {
        [FieldOffset(0)] public MOUSEINPUT Mi;
        [FieldOffset(0)] public KEYBDINPUT Ki;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct INPUT
    {
        public uint Type;
        public InputUnion U;
    }

    [DllImport("user32.dll", SetLastError = true)]
    private static extern uint SendInput(uint nInputs, INPUT[] pInputs, int cbSize);

    [DllImport("user32.dll")]
    private static extern uint MapVirtualKey(uint uCode, uint uMapType);

    private static INPUT Key(ushort vk, bool up, bool extended) => new()
    {
        Type = InputKeyboard,
        U = new InputUnion
        {
            Ki = new KEYBDINPUT
            {
                WVk = vk,
                WScan = (ushort)MapVirtualKey(vk, 0),
                DwFlags = (up ? KeyeventfKeyup : 0) | (extended ? KeyeventfExtendedkey : 0),
            },
        },
    };

    private static INPUT Unicode(char c, bool up) => new()
    {
        Type = InputKeyboard,
        U = new InputUnion { Ki = new KEYBDINPUT { WScan = c, DwFlags = KeyeventfUnicode | (up ? KeyeventfKeyup : 0) } },
    };

    private static void Send(List<INPUT> inputs)
    {
        if (inputs.Count == 0)
        {
            return;
        }
        var sent = SendInput((uint)inputs.Count, [.. inputs], Marshal.SizeOf<INPUT>());
        if (sent != inputs.Count)
        {
            throw new InvalidOperationException($"SendInput sent {sent} of {inputs.Count} events (error {Marshal.GetLastWin32Error()})");
        }
    }

    public void Dispose()
    {
        _cts.Cancel();
        _cts.Dispose();
    }
}
