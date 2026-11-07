using System.Runtime.InteropServices;

namespace RezPad.Host.Media;

/// <summary>
/// Injects Windows media keys. Used for play/pause, next and previous when there is no GSMTC
/// session: some players only listen to the global media keys.
/// </summary>
internal static class MediaKeyFallback
{
    private const byte VkMediaNext = 0xB0;
    private const byte VkMediaPrev = 0xB1;
    private const byte VkMediaPlayPause = 0xB3;
    private const uint KeyeventfExtendedKey = 0x0001;
    private const uint KeyeventfKeyUp = 0x0002;

    [DllImport("user32.dll")]
    private static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);

    public static void PlayPause() => Tap(VkMediaPlayPause, 1);

    public static void Next() => Tap(VkMediaNext, 1);

    public static void Previous() => Tap(VkMediaPrev, 1);

    private static void Tap(byte vk, int times)
    {
        for (var i = 0; i < times; i++)
        {
            keybd_event(vk, 0, KeyeventfExtendedKey, UIntPtr.Zero);
            keybd_event(vk, 0, KeyeventfExtendedKey | KeyeventfKeyUp, UIntPtr.Zero);
        }
    }
}
