using System.Text;

namespace ZPlus.Setup;

internal sealed class InstallLogger : IDisposable
{
    private readonly StreamWriter writer;
    private readonly object gate = new();

    public string Path { get; }

    public InstallLogger(string? logPath = null)
    {
        string directory = logPath is null
            ? System.IO.Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "ZPlusInstaller",
                "Logs")
            : System.IO.Path.GetDirectoryName(logPath) ??
              throw new ArgumentException("Log file requires a parent directory.", nameof(logPath));
        Directory.CreateDirectory(directory);
        Path = logPath ?? System.IO.Path.Combine(
            directory, $"install-{DateTime.UtcNow:yyyyMMdd-HHmmss}-{Guid.NewGuid():N}.log");
        writer = new StreamWriter(new FileStream(
            Path, FileMode.CreateNew, FileAccess.Write, FileShare.Read),
            new UTF8Encoding(false)) { AutoFlush = true };
        Write("INFO", "Installer log started.");
    }

    public void Info(string message) => Write("INFO", message);
    public void Error(string message) => Write("ERROR", message);

    private void Write(string level, string message)
    {
        lock (gate)
        {
            writer.WriteLine($"{DateTimeOffset.Now:O} [{level}] {message}");
        }
    }

    public void Dispose() => writer.Dispose();
}
