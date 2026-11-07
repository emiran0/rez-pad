using System.Collections.Concurrent;
using System.Text;

namespace RezPad.Host;

/// <summary>
/// Minimal app log: an in-memory ring (served at /api/log) plus %LOCALAPPDATA%\rez-pad\host.log,
/// rolled to host.log.1 at 1 MB.
/// </summary>
internal static class Log
{
    private const int RingSize = 300;
    private const long MaxFileBytes = 1024 * 1024;

    private static readonly ConcurrentQueue<string> Ring = new();
    private static readonly object FileLock = new();
    private static readonly string Dir =
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "rez-pad");
    private static readonly string FilePath = Path.Combine(Dir, "host.log");

    public static string LogFile => FilePath;

    public static void Info(string msg) => Write("INF", msg);
    public static void Warn(string msg) => Write("WRN", msg);
    public static void Error(string msg) => Write("ERR", msg);

    public static IReadOnlyList<string> Recent() => Ring.ToArray();

    private static void Write(string level, string msg)
    {
        var line = $"{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff} {level} {msg}";
        Ring.Enqueue(line);
        while (Ring.Count > RingSize && Ring.TryDequeue(out _))
        {
        }
        try
        {
            lock (FileLock)
            {
                Directory.CreateDirectory(Dir);
                var info = new FileInfo(FilePath);
                if (info.Exists && info.Length > MaxFileBytes)
                {
                    File.Move(FilePath, FilePath + ".1", overwrite: true);
                }
                File.AppendAllText(FilePath, line + Environment.NewLine, Encoding.UTF8);
            }
        }
        catch (IOException)
        {
            // Logging must never take the app down.
        }
    }
}
