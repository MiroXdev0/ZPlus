using System.Security.Cryptography;
using System.Text.Json;
using System.Runtime.InteropServices;
using Microsoft.Win32;

namespace ZPlus.Setup;

internal sealed record UninstallResult(
    string InstallPath,
    IReadOnlyList<string> RemovedFiles,
    IReadOnlyList<string> PreservedFiles);

internal static class Uninstaller
{
    public static string? RegisteredInstallPath() => WindowsIntegration.ExistingInstallPath();

    public static InstallRecord ReadRecord(string installPath)
    {
        string fullPath = Path.GetFullPath(installPath);
        string marker = Path.Combine(fullPath, PackageInfo.MarkerFileName);
        if (!File.Exists(marker))
            throw new FileNotFoundException(
                $"Z+ installation record was not found in '{fullPath}'.", marker);
        InstallRecord record = JsonSerializer.Deserialize<InstallRecord>(File.ReadAllText(marker))
            ?? throw new InvalidDataException("The Z+ installation record is empty.");
        if (record.Product != PackageInfo.Product ||
            !PathEquals(record.InstallPath, fullPath))
            throw new InvalidDataException(
                "The installation record does not match the selected Z+ directory.");
        if (record.Files is null || record.ShortcutPaths is null ||
            record.Directories is null)
            throw new InvalidDataException("The installation record is missing required ownership data.");
        foreach (string relative in record.Files.Keys) ValidateRelativePath(relative);
        foreach (string relative in record.Directories) ValidateRelativePath(relative);
        foreach (string shortcut in record.ShortcutPaths)
            ValidateShortcutPath(shortcut);
        return record;
    }

    public static async Task<UninstallResult> UninstallAsync(
        string installPath, InstallLogger logger, CancellationToken cancellationToken)
    {
        string root = Path.GetFullPath(installPath);
        InstallRecord record = ReadRecord(root);
        string bin = Path.Combine(root, "bin");
        var removed = new List<string>();
        var preserved = new List<string>();

        foreach ((string relative, string expectedHash) in record.Files)
        {
            cancellationToken.ThrowIfCancellationRequested();
            string path = SafePath(root, relative);
            EnsureNoReparsePoints(root, path);
            if (!File.Exists(path)) continue;
            string actual = await HashFileAsync(path, cancellationToken);
            if (actual.Equals(expectedHash, StringComparison.OrdinalIgnoreCase))
            {
                File.Delete(path);
                removed.Add(path);
                logger.Info($"Removed installed file '{path}'.");
            }
            else
            {
                preserved.Add(path);
                logger.Info($"Preserved modified installed file '{path}'.");
            }
        }

        RemoveOwnedShortcuts(record, root, logger);
        WindowsIntegration.RemoveFileAssociation(record, root);
        if (record.AddedPathEntry)
            WindowsIntegration.RemoveUserPath(bin);
        WindowsIntegration.RemoveUninstallEntry(root);

        string marker = Path.Combine(root, PackageInfo.MarkerFileName);
        if (File.Exists(marker))
        {
            File.Delete(marker);
            removed.Add(marker);
        }
        foreach (string relativeDirectory in record.Directories
                     .OrderByDescending(path => path.Length))
        {
            ValidateRelativePath(relativeDirectory);
            string directory = SafePath(root, relativeDirectory);
            EnsureNoReparsePoints(root, directory);
            if (Directory.Exists(directory) &&
                !Directory.EnumerateFileSystemEntries(directory).Any())
                Directory.Delete(directory);
        }
        if (Directory.Exists(root) &&
            !Directory.EnumerateFileSystemEntries(root).Any())
            Directory.Delete(root);

        foreach (string remainingFile in EnumerateFilesWithoutFollowingLinks(root))
            if (!removed.Contains(remainingFile, StringComparer.OrdinalIgnoreCase) &&
                !preserved.Contains(remainingFile, StringComparer.OrdinalIgnoreCase))
                preserved.Add(remainingFile);

        logger.Info($"Uninstall completed for '{root}'; preserved {preserved.Count} user-modified or untracked file(s).");
        return new UninstallResult(root, removed, preserved);
    }

    public static string RelaunchUninstallerFromTemporaryDirectory(string installPath)
    {
        string currentExecutable = Environment.ProcessPath
            ?? throw new InvalidOperationException("The running uninstaller path is unavailable.");
        string copy = Path.Combine(
            Path.GetTempPath(), $"ZPlusUninstall-{Guid.NewGuid():N}.exe");
        File.Copy(currentExecutable, copy, overwrite: false);
        var start = new System.Diagnostics.ProcessStartInfo
        {
            FileName = copy,
            UseShellExecute = false,
            WorkingDirectory = Path.GetTempPath()
        };
        start.ArgumentList.Add("--uninstall");
        start.ArgumentList.Add("--install-dir");
        start.ArgumentList.Add(installPath);
        start.ArgumentList.Add("--quiet");
        start.ArgumentList.Add("--detached");
        if (System.Diagnostics.Process.Start(start) is null)
            throw new InvalidOperationException("Could not start the temporary Z+ uninstaller.");
        return copy;
    }

    public static void ScheduleRunningCopyForRemoval(string path)
    {
        if (!MoveFileEx(path, null, 0x00000004))
            throw new IOException(
                $"Uninstall completed, but temporary cleanup could not be scheduled for '{path}'.",
                Marshal.GetExceptionForHR(Marshal.GetHRForLastWin32Error()));
    }

    private static void RemoveOwnedShortcuts(
        InstallRecord record, string installPath, InstallLogger logger)
    {
        string compiler = Path.Combine(installPath, "bin", "zp.exe");
        string uninstaller = Path.Combine(installPath, "ZPlusUninstall.exe");
        string syntax = Path.Combine(installPath, "docs", "SYNTAX.md");
        foreach (string shortcut in record.ShortcutPaths)
        {
            ValidateShortcutPath(shortcut);
            string shortcutName = Path.GetFileName(shortcut);
            string? expectedTarget = shortcutName switch
            {
                "Z+ Compiler.lnk" => compiler,
                "Z+ Syntax Reference.lnk" => syntax,
                "Uninstall Z+.lnk" => uninstaller,
                _ => null
            };
            if (Path.GetFileName(shortcut) == "Z+ Compiler.lnk" &&
                Path.GetDirectoryName(shortcut)?.Equals(
                    Environment.GetFolderPath(Environment.SpecialFolder.Desktop),
                    StringComparison.OrdinalIgnoreCase) == true)
                expectedTarget = compiler;
            if (expectedTarget is null) continue;
            string expectedArguments = shortcutName switch
            {
                "Z+ Compiler.lnk" => "version",
                "Uninstall Z+.lnk" => "--uninstall",
                _ => ""
            };
            WindowsIntegration.RemoveShortcutIfOwned(
                shortcut, expectedTarget, expectedArguments);
            logger.Info($"Checked installer shortcut '{shortcut}'.");
        }
        string productStartMenu = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.StartMenu),
            "Programs", "Z+");
        if (Directory.Exists(productStartMenu) &&
            !Directory.EnumerateFileSystemEntries(productStartMenu).Any())
            Directory.Delete(productStartMenu);
    }

    private static IEnumerable<string> EnumerateFilesWithoutFollowingLinks(string root)
    {
        if (!Directory.Exists(root)) yield break;
        var pending = new Stack<string>();
        pending.Push(root);
        while (pending.TryPop(out string? directory))
        {
            foreach (string entry in Directory.EnumerateFileSystemEntries(directory))
            {
                if ((File.GetAttributes(entry) & FileAttributes.ReparsePoint) != 0)
                    continue;
                if (Directory.Exists(entry)) pending.Push(entry);
                else yield return entry;
            }
        }
    }

    private static string SafePath(string root, string relative)
    {
        ValidateRelativePath(relative);
        string fullRoot = Path.GetFullPath(root)
            .TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar) +
            Path.DirectorySeparatorChar;
        string fullPath = Path.GetFullPath(Path.Combine(
            root, relative.Replace('/', Path.DirectorySeparatorChar)));
        if (!fullPath.StartsWith(fullRoot, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException($"Installed path escapes its root: {relative}");
        return fullPath;
    }

    private static void ValidateRelativePath(string relative)
    {
        if (string.IsNullOrWhiteSpace(relative) || Path.IsPathRooted(relative) ||
            relative.Contains(':') || relative.Contains('\\') ||
            relative.Split('/').Any(part =>
                part.Length == 0 || part is "." or ".." ||
                part.EndsWith(' ') || part.EndsWith('.')))
            throw new InvalidDataException($"Unsafe installation record path: {relative}");
    }

    private static void ValidateShortcutPath(string path)
    {
        string fullPath = Path.GetFullPath(path);
        string startMenuRoot = Path.GetFullPath(Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.StartMenu), "Programs", "Z+"));
        string desktopRoot = Path.GetFullPath(
            Environment.GetFolderPath(Environment.SpecialFolder.Desktop));
        if (!IsWithin(fullPath, startMenuRoot) && !IsWithin(fullPath, desktopRoot))
            throw new InvalidDataException($"Untrusted shortcut location in install record: {path}");
    }

    private static bool IsWithin(string path, string directory)
    {
        string prefix = Path.TrimEndingDirectorySeparator(directory) +
            Path.DirectorySeparatorChar;
        return path.StartsWith(prefix, StringComparison.OrdinalIgnoreCase);
    }

    private static void EnsureNoReparsePoints(string root, string path)
    {
        string current = Path.GetFullPath(root);
        if ((File.Exists(current) || Directory.Exists(current)) &&
            (File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0)
            throw new IOException($"Refusing to remove files through a reparse point: {current}");
        string relative = Path.GetRelativePath(current, Path.GetFullPath(path));
        foreach (string part in relative.Split(
                     [Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar],
                     StringSplitOptions.RemoveEmptyEntries))
        {
            current = Path.Combine(current, part);
            if ((File.Exists(current) || Directory.Exists(current)) &&
                (File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0)
                throw new IOException($"Refusing to remove files through a reparse point: {current}");
        }
    }

    private static async Task<string> HashFileAsync(
        string path, CancellationToken cancellationToken)
    {
        await using var stream = new FileStream(
            path, FileMode.Open, FileAccess.Read, FileShare.Read,
            128 * 1024, FileOptions.Asynchronous | FileOptions.SequentialScan);
        return Convert.ToHexString(await SHA256.HashDataAsync(stream, cancellationToken));
    }

    private static bool PathEquals(string first, string second) =>
        string.Equals(Path.TrimEndingDirectorySeparator(Path.GetFullPath(first)),
            Path.TrimEndingDirectorySeparator(Path.GetFullPath(second)),
            StringComparison.OrdinalIgnoreCase);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool MoveFileEx(string existingFileName, string? newFileName, uint flags);
}
