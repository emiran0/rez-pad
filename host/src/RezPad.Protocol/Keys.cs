namespace RezPad.Protocol;

/// <summary>
/// Key shortcuts like "ctrl+shift+m" (docs/config-schema.md, "Buttons and macros"). Mirrors
/// actions::parseKeys in firmware/lib/config_logic: same names, same HID usages. Also gives the
/// Windows virtual-key code of each key for the host's macro runner.
/// </summary>
public static class KeyCombo
{
    public sealed record Key(string Name, byte Usage, ushort Vk, bool Extended = false);

    /// <summary>Modifier names, their HID bits and virtual keys.</summary>
    public static readonly IReadOnlyList<(string Name, byte Bit, ushort Vk)> Modifiers =
    [
        ("ctrl", 0x01, 0xA2), ("shift", 0x02, 0xA0), ("alt", 0x04, 0xA4), ("win", 0x08, 0x5B), ("altgr", 0x40, 0xA5),
    ];

    public static readonly IReadOnlyDictionary<string, Key> Keys = Build();

    private static Dictionary<string, Key> Build()
    {
        var list = new List<Key>();
        for (var c = 'a'; c <= 'z'; c++)
        {
            list.Add(new(c.ToString(), (byte)(0x04 + c - 'a'), (ushort)char.ToUpperInvariant(c)));
        }
        for (var d = 1; d <= 9; d++)
        {
            list.Add(new(d.ToString(System.Globalization.CultureInfo.InvariantCulture), (byte)(0x1E + d - 1), (ushort)('0' + d)));
        }
        list.Add(new("0", 0x27, '0'));
        for (var f = 1; f <= 24; f++)
        {
            list.Add(new($"f{f}", (byte)(f <= 12 ? 0x3A + f - 1 : 0x68 + f - 13), (ushort)(0x70 + f - 1)));
        }
        for (var n = 0; n <= 9; n++)
        {
            list.Add(new($"num{n}", (byte)(n == 0 ? 0x62 : 0x59 + n - 1), (ushort)(0x60 + n)));
        }
        list.AddRange(
        [
            new("enter", 0x28, 0x0D), new("esc", 0x29, 0x1B), new("backspace", 0x2A, 0x08), new("tab", 0x2B, 0x09),
            new("space", 0x2C, 0x20), new("minus", 0x2D, 0xBD), new("equal", 0x2E, 0xBB), new("lbracket", 0x2F, 0xDB),
            new("rbracket", 0x30, 0xDD), new("backslash", 0x31, 0xDC), new("semicolon", 0x33, 0xBA), new("quote", 0x34, 0xDE),
            new("grave", 0x35, 0xC0), new("comma", 0x36, 0xBC), new("period", 0x37, 0xBE), new("slash", 0x38, 0xBF),
            new("capslock", 0x39, 0x14), new("printscreen", 0x46, 0x2C, true), new("scrolllock", 0x47, 0x91),
            new("pause", 0x48, 0x13), new("insert", 0x49, 0x2D, true), new("home", 0x4A, 0x24, true),
            new("pgup", 0x4B, 0x21, true), new("delete", 0x4C, 0x2E, true), new("end", 0x4D, 0x23, true),
            new("pgdn", 0x4E, 0x22, true), new("right", 0x4F, 0x27, true), new("left", 0x50, 0x25, true),
            new("down", 0x51, 0x28, true), new("up", 0x52, 0x26, true), new("numlock", 0x53, 0x90, true),
            new("numdiv", 0x54, 0x6F, true), new("nummul", 0x55, 0x6A), new("numsub", 0x56, 0x6D), new("numadd", 0x57, 0x6B),
            new("numenter", 0x58, 0x0D, true), new("numdot", 0x63, 0x6E), new("intl", 0x64, 0xE2), new("menu", 0x65, 0x5D, true),
        ]);
        return list.ToDictionary(k => k.Name);
    }

    public sealed record Parsed(byte Mods, Key? Key, IReadOnlyList<ushort> ModifierVks);

    /// <summary>Parses "mod+mod+key" (case-insensitive, at most one key). Null when invalid.</summary>
    public static Parsed? Parse(string? s)
    {
        if (string.IsNullOrEmpty(s) || s.Length > 63)
        {
            return null;
        }
        byte mods = 0;
        Key? key = null;
        var vks = new List<ushort>();
        foreach (var raw in s.Split('+'))
        {
            var part = raw.ToLowerInvariant();
            if (part.Length == 0)
            {
                return null;
            }
            var mod = Modifiers.FirstOrDefault(m => m.Name == part);
            if (mod.Name is not null)
            {
                if ((mods & mod.Bit) == 0)
                {
                    vks.Add(mod.Vk);
                }
                mods |= mod.Bit;
                continue;
            }
            if (key is not null || !Keys.TryGetValue(part, out var k))
            {
                return null;
            }
            key = k;
        }
        return mods == 0 && key is null ? null : new Parsed(mods, key, vks);
    }
}
