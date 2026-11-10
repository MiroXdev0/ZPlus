using System.Runtime.InteropServices;
using Microsoft.Win32;

namespace ZPlus.Setup;

internal sealed record IntegrationSnapshot(
    string? ZpAssociation, bool ZpKeyExisted, bool ProgIdExisted);

internal static class WindowsIntegration
{
    private const string UserEnvironmentKey = @"Environment";
    private const string ClassesKey = @"Software\Classes";
    private const string ProgId = "ZPlus.Source";
    private const string UninstallKey =
        @"Software\Microsoft\Windows\CurrentVersion\Uninstall\ZPlus";

    public static string DefaultInstallPath =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "Programs", "ZPlus");

    public static string? ExistingInstallPath()
    {
        using RegistryKey? key = Registry.CurrentUser.OpenSubKey(UninstallKey);
        return key?.GetValue("InstallLocation") as string;
    }

    public static IntegrationSnapshot Capture()
    {
        using RegistryKey? classes = Registry.CurrentUser.OpenSubKey(
            $@"{ClassesKey}\.zp", writable: false);
        using RegistryKey? classesRoot =
            Registry.CurrentUser.OpenSubKey(ClassesKey, writable: false);
        using RegistryKey? prog = classesRoot?.OpenSubKey(ProgId);
        return new IntegrationSnapshot(
            classes?.GetValue(null) as string,
            classes is not null,
            prog is not null);
    }

    public static bool AddUserPath(string binDirectory)
    {
        string normalized = NormalizePath(binDirectory);
        using RegistryKey environment =
            Registry.CurrentUser.CreateSubKey(UserEnvironmentKey, writable: true)
            ?? throw new InvalidOperationException("Cannot open the current user's environment settings.");
        string current = environment.GetValue(
            "Path", "", RegistryValueOptions.DoNotExpandEnvironmentNames) as string ?? "";
        string[] entries = current.Split(';', StringSplitOptions.RemoveEmptyEntries);
        if (entries.Any(entry => SamePath(entry, normalized))) return false;
        RegistryValueKind kind = ReadPathKind(environment);
        environment.SetValue("Path",
            current.Length == 0 ? normalized : current.TrimEnd(';') + ";" + normalized,
            kind);
        BroadcastEnvironmentChange();
        return true;
    }

    public static void RestoreSnapshot(IntegrationSnapshot snapshot, string installPath)
    {
        using RegistryKey? classes =
            Registry.CurrentUser.OpenSubKey(ClassesKey, writable: true);
        using (RegistryKey? zp = classes?.OpenSubKey(".zp", writable: true))
        {
            if (zp?.GetValue(null) as string == ProgId)
            {
                if (snapshot.ZpAssociation is null)
                    zp.DeleteValue("", throwOnMissingValue: false);
                else
                    zp.SetValue(null, snapshot.ZpAssociation, RegistryValueKind.String);
                NotifyShell();
            }
        }
        if (!snapshot.ZpKeyExisted)
            DeleteKeyIfEmpty(classes, ".zp");
        if (!snapshot.ProgIdExisted)
            RemoveOwnedProgIdValues(classes, installPath);
    }

    public static void RegisterFileAssociation(string installPath)
    {
        string bin = Path.Combine(installPath, "bin");
        string compiler = Path.Combine(bin, "zp.exe");
        using RegistryKey classes =
            Registry.CurrentUser.CreateSubKey(ClassesKey, writable: true)
            ?? throw new InvalidOperationException("Cannot open current-user file associations.");
        using RegistryKey? existing = classes.OpenSubKey(ProgId);
        bool existingOwned = false;
        if (existing is not null)
        {
            using RegistryKey? existingIcon = existing.OpenSubKey("DefaultIcon");
            using RegistryKey? existingRun = existing.OpenSubKey(@"shell\run\command");
            string? iconValue = existingIcon?.GetValue(null) as string;
            string? runHandlerCommand = existingRun?.GetValue(null) as string;
            existingOwned =
                (iconValue?.Contains(compiler, StringComparison.OrdinalIgnoreCase) ?? false) ||
                (runHandlerCommand?.Contains(compiler, StringComparison.OrdinalIgnoreCase) ?? false);
        }
        if (existing is not null && !existingOwned)
            throw new InvalidOperationException(
                $"The file-type registration '{ProgId}' already belongs to another application.");
        using RegistryKey prog = classes.CreateSubKey(ProgId, writable: true)
            ?? throw new InvalidOperationException("Cannot register the .zp file type.");
        prog.SetValue(null, "Z+ source file");
        using RegistryKey icon = prog.CreateSubKey("DefaultIcon", writable: true)
            ?? throw new InvalidOperationException("Cannot register the Z+ file icon.");
        icon.SetValue(null, $"\"{compiler}\",0");
        using RegistryKey open = prog.CreateSubKey(@"shell\open\command", writable: true)
            ?? throw new InvalidOperationException("Cannot register the source-file open command.");
        open.SetValue(null, $"\"{Path.Combine(Environment.SystemDirectory, "notepad.exe")}\" \"%1\"");
        using RegistryKey run = prog.CreateSubKey(@"shell\run", writable: true)
            ?? throw new InvalidOperationException("Cannot register the Run with Z+ command.");
        run.SetValue(null, "Run with Z+");
        using RegistryKey runCommand = prog.CreateSubKey(@"shell\run\command", writable: true)
            ?? throw new InvalidOperationException("Cannot register the Run with Z+ command.");
        runCommand.SetValue(null, $"\"{compiler}\" run \"%1\"");

        using RegistryKey zp = classes.CreateSubKey(".zp", writable: true)
            ?? throw new InvalidOperationException("Cannot associate .zp source files.");
        zp.SetValue(null, ProgId, RegistryValueKind.String);
        NotifyShell();
    }

    public static void RemoveFileAssociation(InstallRecord record, string installPath)
    {
        using RegistryKey? classes =
            Registry.CurrentUser.OpenSubKey(ClassesKey, writable: true);
        if (classes is null) return;
        using (RegistryKey? zp = classes.OpenSubKey(".zp", writable: true))
        {
            if (record.AssociationApplied && zp?.GetValue(null) as string == ProgId)
            {
                if (record.PreviousZpAssociation is null)
                    zp.DeleteValue("", throwOnMissingValue: false);
                else
                    zp.SetValue(null, record.PreviousZpAssociation, RegistryValueKind.String);
            }
        }

        if (record.AssociationApplied && !record.PreviousZpKeyExisted)
            DeleteKeyIfEmpty(classes, ".zp");
        string compiler = Path.Combine(installPath, "bin", "zp.exe");
        bool owned;
        using (RegistryKey? prog = classes.OpenSubKey(ProgId))
        using (RegistryKey? icon = prog?.OpenSubKey("DefaultIcon"))
        using (RegistryKey? run = prog?.OpenSubKey(@"shell\run\command"))
        {
            string? iconValue = icon?.GetValue(null) as string;
            string? runCommand = run?.GetValue(null) as string;
            owned = (iconValue?.Contains(compiler, StringComparison.OrdinalIgnoreCase) ?? false) ||
                    (runCommand?.Contains(compiler, StringComparison.OrdinalIgnoreCase) ?? false);
        }
        if (owned)
        {
            RemoveOwnedProgIdValues(classes, installPath);
        }
        NotifyShell();
    }

    public static void RemoveUserPath(string binDirectory)
    {
        string normalized = NormalizePath(binDirectory);
        using RegistryKey? environment =
            Registry.CurrentUser.OpenSubKey(UserEnvironmentKey, writable: true);
        if (environment is null) return;
        string current = environment.GetValue(
            "Path", "", RegistryValueOptions.DoNotExpandEnvironmentNames) as string ?? "";
        string[] entries = current.Split(';', StringSplitOptions.RemoveEmptyEntries);
        int ownedEntry = Array.FindIndex(entries, entry => SamePath(entry, normalized));
        if (ownedEntry < 0) return;
        var retained = entries.Where((_, index) => index != ownedEntry).ToArray();
        string updated = string.Join(';', retained);
        if (updated.Length == 0)
            environment.DeleteValue("Path", throwOnMissingValue: false);
        else
            environment.SetValue("Path", updated, ReadPathKind(environment));
        BroadcastEnvironmentChange();
    }

    public static void RegisterUninstallEntry(InstallRecord record)
    {
        using RegistryKey key =
            Registry.CurrentUser.CreateSubKey(UninstallKey, writable: true)
            ?? throw new InvalidOperationException("Cannot register Z+ in the user's installed-app list.");
        key.SetValue("DisplayName", "Z+ Programming Language");
        key.SetValue("DisplayVersion", record.Version);
        key.SetValue("Publisher", "Miroslav Aleksandrov");
        key.SetValue("InstallLocation", record.InstallPath);
        key.SetValue("UninstallString",
            $"\"{Path.Combine(record.InstallPath, "ZPlusUninstall.exe")}\" --uninstall");
        key.SetValue("NoModify", 1, RegistryValueKind.DWord);
        key.SetValue("NoRepair", 1, RegistryValueKind.DWord);
        key.SetValue("URLInfoAbout", "https://zplus-lang.org/");
    }

    public static void RemoveUninstallEntry(string installPath)
    {
        using RegistryKey? uninstall =
            Registry.CurrentUser.OpenSubKey(
                @"Software\Microsoft\Windows\CurrentVersion\Uninstall", writable: true);
        using RegistryKey? entry = uninstall?.OpenSubKey("ZPlus");
        if (entry?.GetValue("InstallLocation") is string registered &&
            SamePath(registered, installPath))
            uninstall?.DeleteSubKeyTree("ZPlus", throwOnMissingSubKey: false);
    }

    public static string CreateShortcut(
        string shortcutPath, string target, string arguments, string workingDirectory)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(shortcutPath)!);
        if (File.Exists(shortcutPath))
        {
            ShortcutInfo existing = ReadShortcut(shortcutPath);
            if (!SamePath(existing.Target, target) ||
                !existing.Arguments.Equals(arguments, StringComparison.Ordinal))
                throw new IOException(
                    $"A shortcut already exists and is not owned by this installation: {shortcutPath}");
        }

        Type shellType = Type.GetTypeFromProgID("WScript.Shell")
            ?? throw new InvalidOperationException("Windows Script Host is unavailable for shortcut creation.");
        object shell = Activator.CreateInstance(shellType)
            ?? throw new InvalidOperationException("Could not start the Windows shortcut service.");
        try
        {
            dynamic shortcut = shellType.InvokeMember(
                "CreateShortcut",
                System.Reflection.BindingFlags.InvokeMethod,
                null,
                shell,
                [shortcutPath])!;
            shortcut.TargetPath = target;
            shortcut.Arguments = arguments;
            shortcut.WorkingDirectory = workingDirectory;
            shortcut.Description = "Z+ Programming Language";
            shortcut.Save();
        }
        finally
        {
            if (Marshal.IsComObject(shell)) Marshal.FinalReleaseComObject(shell);
        }
        return shortcutPath;
    }

    public static bool IsShortcutOwned(
        string shortcutPath, string target, string expectedArguments)
    {
        if (!File.Exists(shortcutPath)) return false;
        ShortcutInfo current = ReadShortcut(shortcutPath);
        return SamePath(current.Target, target) &&
            current.Arguments.Equals(expectedArguments, StringComparison.Ordinal);
    }

    public static void RemoveShortcutIfOwned(
        string shortcutPath, string target, string expectedArguments)
    {
        if (IsShortcutOwned(shortcutPath, target, expectedArguments))
            File.Delete(shortcutPath);
    }

    public static string NormalizePath(string path)
    {
        string expanded = Environment.ExpandEnvironmentVariables(path.Trim().Trim('"'));
        return Path.TrimEndingDirectorySeparator(Path.GetFullPath(expanded));
    }

    private static bool SamePath(string first, string second)
    {
        try { return string.Equals(NormalizePath(first), NormalizePath(second),
            StringComparison.OrdinalIgnoreCase); }
        catch (Exception exception) when (exception is ArgumentException or IOException or
                                           NotSupportedException)
        {
            return string.Equals(first.Trim().Trim('"'), second.Trim().Trim('"'),
                StringComparison.OrdinalIgnoreCase);
        }
    }

    private static ShortcutInfo ReadShortcut(string shortcutPath)
    {
        Type shellType = Type.GetTypeFromProgID("WScript.Shell")
            ?? throw new InvalidOperationException("Windows Script Host is unavailable.");
        object shell = Activator.CreateInstance(shellType)
            ?? throw new InvalidOperationException("Could not start the Windows shortcut service.");
        try
        {
            dynamic shortcut = shellType.InvokeMember(
                "CreateShortcut",
                System.Reflection.BindingFlags.InvokeMethod,
                null,
                shell,
                [shortcutPath])!;
            return new ShortcutInfo((string)shortcut.TargetPath,
                (string?)shortcut.Arguments ?? "");
        }
        finally
        {
            if (Marshal.IsComObject(shell)) Marshal.FinalReleaseComObject(shell);
        }
    }

    private sealed record ShortcutInfo(string Target, string Arguments);

    private static void BroadcastEnvironmentChange()
    {
        SendMessageTimeout(new IntPtr(0xffff), 0x001A, IntPtr.Zero,
            "Environment", 0x0002, 5000, out _);
    }

    private static void NotifyShell() => SHChangeNotify(0x08000000, 0x0000, IntPtr.Zero, IntPtr.Zero);

    private static void RemoveOwnedProgIdValues(RegistryKey? classes, string installPath)
    {
        string compiler = Path.Combine(installPath, "bin", "zp.exe");
        using (RegistryKey? prog = classes?.OpenSubKey(ProgId, writable: true))
        {
            if (prog is not null)
            {
                DeleteValueIfEquals(prog, "", "Z+ source file");
                using (RegistryKey? icon = prog.OpenSubKey("DefaultIcon", writable: true))
                    if (icon is not null)
                        DeleteValueIfEquals(icon, "", $"\"{compiler}\",0");
                using (RegistryKey? shell = prog.OpenSubKey("shell", writable: true))
                {
                    if (shell is not null)
                    {
                        using (RegistryKey? open = shell.OpenSubKey("open", writable: true))
                        using (RegistryKey? openCommand = open?.OpenSubKey("command", writable: true))
                            if (openCommand is not null)
                                DeleteValueIfEquals(openCommand, "",
                                    $"\"{Path.Combine(Environment.SystemDirectory, "notepad.exe")}\" \"%1\"");
                        using (RegistryKey? run = shell.OpenSubKey("run", writable: true))
                        {
                            if (run is not null)
                            {
                                DeleteValueIfEquals(run, "", "Run with Z+");
                                using RegistryKey? runCommand = run.OpenSubKey("command", writable: true);
                                if (runCommand is not null)
                                    DeleteValueIfEquals(runCommand, "", $"\"{compiler}\" run \"%1\"");
                            }
                        }
                    }
                }
            }
        }
        DeleteKeyIfEmpty(classes, $@"{ProgId}\DefaultIcon");
        DeleteKeyIfEmpty(classes, $@"{ProgId}\shell\open\command");
        DeleteKeyIfEmpty(classes, $@"{ProgId}\shell\open");
        DeleteKeyIfEmpty(classes, $@"{ProgId}\shell\run\command");
        DeleteKeyIfEmpty(classes, $@"{ProgId}\shell\run");
        DeleteKeyIfEmpty(classes, $@"{ProgId}\shell");
        DeleteKeyIfEmpty(classes, ProgId);
    }

    private static void DeleteValueIfEquals(
        RegistryKey key, string name, string expected)
    {
        if (key.GetValue(name) is string actual &&
            actual.Equals(expected, StringComparison.OrdinalIgnoreCase))
            key.DeleteValue(name, throwOnMissingValue: false);
    }

    private static RegistryValueKind ReadPathKind(RegistryKey environment)
    {
        RegistryValueKind kind = environment.GetValueNames().Contains(
            "Path", StringComparer.OrdinalIgnoreCase)
            ? environment.GetValueKind("Path")
            : RegistryValueKind.ExpandString;
        if (kind is not (RegistryValueKind.String or RegistryValueKind.ExpandString))
            throw new InvalidDataException(
                $"The current user's PATH has unexpected registry type {kind}; no change was made.");
        return kind;
    }

    private static void DeleteKeyIfEmpty(RegistryKey? root, string subKey)
    {
        using RegistryKey? key = root?.OpenSubKey(subKey, writable: true);
        if (key is not null && key.GetValueNames().Length == 0 &&
            key.GetSubKeyNames().Length == 0)
            root?.DeleteSubKey(subKey, throwOnMissingSubKey: false);
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr SendMessageTimeout(
        IntPtr hWnd, uint message, IntPtr wParam, string lParam,
        uint flags, uint timeout, out IntPtr result);

    [DllImport("shell32.dll")]
    private static extern void SHChangeNotify(
        uint eventId, uint flags, IntPtr item1, IntPtr item2);
}
