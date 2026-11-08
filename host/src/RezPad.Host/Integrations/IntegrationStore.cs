using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace RezPad.Host.Integrations;

/// <summary>Beszel hub connection (Settings > Integrations). The password never leaves the host.</summary>
internal sealed record BeszelSettings(bool Enabled, string Url, string Email, string? Password, int RefreshS, bool InsecureTls)
{
    public const int DefaultRefreshS = 15;
    public static BeszelSettings Empty { get; } = new(false, "", "", null, DefaultRefreshS, false);
}

/// <summary>
/// Settings of external integrations in <c>%APPDATA%\rez-pad\integrations.json</c>, host-only: the
/// config (which is stored on the device) never holds URLs or credentials, only source ids. Secrets
/// are encrypted with Windows DPAPI for the current user (an elevated instance of the same user can
/// read them too). Writes are atomic.
/// </summary>
internal sealed class IntegrationStore
{
    private static readonly byte[] Entropy = Encoding.UTF8.GetBytes("rez-pad integrations v1");
    private readonly string _path;
    private readonly object _lock = new();
    private BeszelSettings _beszel = BeszelSettings.Empty;

    public IntegrationStore(string? dir = null)
    {
        dir ??= Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "rez-pad");
        Directory.CreateDirectory(dir);
        _path = Path.Combine(dir, "integrations.json");
        Load();
    }

    public event Action? Changed;

    public BeszelSettings Beszel
    {
        get
        {
            lock (_lock)
            {
                return _beszel;
            }
        }
    }

    /// <summary>Saves Beszel settings; a null password keeps the stored one.</summary>
    public void SetBeszel(BeszelSettings s)
    {
        lock (_lock)
        {
            _beszel = s with { Password = s.Password ?? _beszel.Password };
            Save();
        }
        Changed?.Invoke();
    }

    private void Load()
    {
        try
        {
            if (!File.Exists(_path))
            {
                return;
            }
            var root = JsonNode.Parse(File.ReadAllText(_path)) as JsonObject;
            if (root?["beszel"] is JsonObject b)
            {
                _beszel = new BeszelSettings(
                    (bool?)b["enabled"] ?? false,
                    (string?)b["url"] ?? "",
                    (string?)b["email"] ?? "",
                    Unprotect((string?)b["password_dpapi"]),
                    Math.Clamp((int?)b["refresh_s"] ?? BeszelSettings.DefaultRefreshS, 5, 300),
                    (bool?)b["insecure_tls"] ?? false);
            }
        }
        catch (Exception ex) when (ex is IOException or JsonException or UnauthorizedAccessException)
        {
            Log.Warn($"integrations: cannot read {_path}: {ex.Message}");
        }
    }

    // Under _lock.
    private void Save()
    {
        var b = _beszel;
        var root = new JsonObject
        {
            ["beszel"] = new JsonObject
            {
                ["enabled"] = b.Enabled,
                ["url"] = b.Url,
                ["email"] = b.Email,
                ["password_dpapi"] = Protect(b.Password),
                ["refresh_s"] = b.RefreshS,
                ["insecure_tls"] = b.InsecureTls,
            },
        };
        var tmp = _path + ".tmp";
        File.WriteAllText(tmp, root.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
        File.Move(tmp, _path, overwrite: true);
    }

    private static string? Protect(string? secret) => string.IsNullOrEmpty(secret)
        ? null
        : Convert.ToBase64String(ProtectedData.Protect(Encoding.UTF8.GetBytes(secret), Entropy, DataProtectionScope.CurrentUser));

    private static string? Unprotect(string? blob)
    {
        if (string.IsNullOrEmpty(blob))
        {
            return null;
        }
        try
        {
            return Encoding.UTF8.GetString(ProtectedData.Unprotect(Convert.FromBase64String(blob), Entropy, DataProtectionScope.CurrentUser));
        }
        catch (Exception ex) when (ex is CryptographicException or FormatException)
        {
            Log.Warn("integrations: stored password cannot be decrypted (another Windows user?); enter it again");
            return null;
        }
    }
}
