using Microsoft.Win32;

namespace RezPad.Host.Device;

/// <summary>
/// Finds the rez-pad's protocol COM port (docs/protocol.md, "USB identity and discovery").
/// Windows-specific: reads the PnP device tree from the registry (fast, no WMI, no admin needed):
///   Enum\USB\VID_303A&amp;PID_4005\RP-&lt;MAC&gt;            composite device, value ParentIdPrefix
///   Enum\USB\VID_303A&amp;PID_4005&amp;MI_01\&lt;prefix&gt;&amp;0001  CDC interface, Device Parameters\PortName
/// Only ports that exist right now (Win32Serial.PresentPorts) are returned.
/// </summary>
internal static class DeviceLocator
{
    public const string SerialPrefix = "RP-";
    private const string CompositeKey = @"SYSTEM\CurrentControlSet\Enum\USB\VID_303A&PID_4005";
    private const string CdcKey = @"SYSTEM\CurrentControlSet\Enum\USB\VID_303A&PID_4005&MI_01";

    public sealed record Found(string Port, string Serial);

    public static Found? Find()
    {
        var present = Win32Serial.PresentPorts();
        if (present.Count == 0)
        {
            return null;
        }
        using var composite = Registry.LocalMachine.OpenSubKey(CompositeKey);
        using var cdc = Registry.LocalMachine.OpenSubKey(CdcKey);
        if (composite is null || cdc is null)
        {
            return null;
        }
        foreach (var serial in composite.GetSubKeyNames())
        {
            if (!serial.StartsWith(SerialPrefix, StringComparison.OrdinalIgnoreCase))
            {
                continue;  // another TinyUSB CDC+HID device sharing the PID
            }
            using var dev = composite.OpenSubKey(serial);
            if (dev?.GetValue("ParentIdPrefix") is not string prefix)
            {
                continue;
            }
            foreach (var child in cdc.GetSubKeyNames())
            {
                if (!child.StartsWith(prefix + "&", StringComparison.OrdinalIgnoreCase))
                {
                    continue;
                }
                using var parameters = cdc.OpenSubKey(child + @"\Device Parameters");
                if (parameters?.GetValue("PortName") is string port && present.Contains(port))
                {
                    return new Found(port, serial);
                }
            }
        }
        return null;
    }
}
