using System.Text;

namespace RezPad.Protocol;

/// <summary>
/// Source ids of external integrations: <c>bz.&lt;machine&gt;.&lt;metric&gt;</c> for Beszel. They must fit
/// the device's 31-byte source id, so machine names become short slugs.
/// </summary>
public static class ExternalIds
{
    public const string BeszelPrefix = "bz.";
    public const int MaxSlug = 14;

    /// <summary>
    /// Lowercase letters, digits and '-' from <paramref name="name"/>, at most <see cref="MaxSlug"/>
    /// characters; "-2", "-3"... when <paramref name="taken"/> already has it.
    /// </summary>
    public static string Slug(string name, ICollection<string> taken)
    {
        var sb = new StringBuilder();
        // Letters without a decomposition that would otherwise vanish (Turkish dotless i and so on).
        var mapped = name.ToLowerInvariant()
            .Replace("ı", "i").Replace("ß", "ss").Replace("ø", "o").Replace("đ", "d")
            .Replace("ł", "l").Replace("æ", "ae").Replace("œ", "oe");
        foreach (var ch in mapped.Normalize(NormalizationForm.FormD))
        {
            if (ch is >= 'a' and <= 'z' or >= '0' and <= '9')
            {
                sb.Append(ch);
            }
            else if (sb.Length > 0 && sb[^1] != '-' && char.GetUnicodeCategory(ch) != System.Globalization.UnicodeCategory.NonSpacingMark)
            {
                sb.Append('-');
            }
        }
        var slug = sb.ToString().Trim('-');
        if (slug.Length > MaxSlug)
        {
            slug = slug[..MaxSlug].TrimEnd('-');
        }
        if (slug.Length == 0)
        {
            slug = "system";
        }
        var result = slug;
        for (var n = 2; taken.Contains(result); n++)
        {
            var suffix = "-" + n;
            result = slug[..Math.Min(slug.Length, MaxSlug - suffix.Length)] + suffix;
        }
        return result;
    }
}
