using System.Runtime.InteropServices;

namespace RezPad.Host.Media;

/// <summary>
/// Phase 5 stand-in for executing ACTION messages: injects Windows media keys, the same effect the
/// device's own HID keys have. Phase 6 replaces this with GSMTC (precise play/pause, seek) and Core
/// Audio (exact volume steps).
/// </summary>
internal static class MediaKeyFallback
{
    private const byte VkVolumeMute = 0xAD;
    private const byte VkVolumeDown = 0xAE;
    private const byte VkVolumeUp = 0xAF;
    private const byte VkMediaNext = 0xB0;
    private const byte VkMediaPrev = 0xB1;
    private const byte VkMediaPlayPause = 0xB3;
    private const uint KeyeventfExtendedKey = 0x0001;
    private const uint KeyeventfKeyUp = 0x0002;

    [DllImport("user32.dll")]
    private static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);

    /// <summary>Returns false when the action has no media-key equivalent.</summary>
    public static bool Execute(string action, int value)
    {
        switch (action)
        {
            case "media.play_pause": Tap(VkMediaPlayPause, 1); return true;
            case "media.next": Tap(VkMediaNext, 1); return true;
            case "media.prev": Tap(VkMediaPrev, 1); return true;
            case "media.mute": Tap(VkVolumeMute, 1); return true;
            case "media.volume" when value != 0:
                Tap(value > 0 ? VkVolumeUp : VkVolumeDown, Math.Min(Math.Abs(value), 20));
                return true;
            default:
                return false;  // media.seek needs GSMTC (Phase 6)
        }
    }

    private static void Tap(byte vk, int times)
    {
        for (var i = 0; i < times; i++)
        {
            keybd_event(vk, 0, KeyeventfExtendedKey, UIntPtr.Zero);
            keybd_event(vk, 0, KeyeventfExtendedKey | KeyeventfKeyUp, UIntPtr.Zero);
        }
    }
}
