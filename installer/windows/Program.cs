using System.Diagnostics;

namespace ZPlus.Setup;

internal static class Program
{
    [STAThread]
    private static async Task<int> Main(string[] args)
    {
        if (args.Contains("--version", StringComparer.Ordinal))
        {
            Console.WriteLine($"Z+ Setup {PackageInfo.Version}");
            return 0;
        }

        bool quiet = args.Contains("--quiet", StringComparer.Ordinal);
        string? explicitLog = ReadOption(args, "--log-file");
        InstallLogger? logger = null;
        try
        {
            logger = new InstallLogger(explicitLog);
            if (args.Contains("--uninstall", StringComparer.Ordinal))
                return await RunUninstallAsync(args, quiet, logger);

            string setupDirectory = AppContext.BaseDirectory;
            PackageManifestReader package = PackageManifestReader.Load(setupDirectory);
            if (args.Contains("--install-dir", StringComparer.Ordinal))
            {
                string destination = ReadOption(args, "--install-dir")
                    ?? throw new ArgumentException("--install-dir requires a directory path.");
                var options = new InstallOptions(
                    destination,
                    IncludeTools: !args.Contains("--no-tools", StringComparer.Ordinal),
                    IncludeDocumentation: !args.Contains("--no-docs", StringComparer.Ordinal),
                    IncludeExamples: !args.Contains("--no-examples", StringComparer.Ordinal),
                    CreateDesktopShortcut: args.Contains("--desktop-shortcut", StringComparer.Ordinal),
                    ReplaceModifiedFiles: args.Contains("--replace-modified", StringComparer.Ordinal));
                await new InstallEngine(package, logger).InstallAsync(
                    options, progress: null, CancellationToken.None);
                logger.Info($"Silent installation succeeded. Log: {logger.Path}");
                return 0;
            }

            if (quiet)
                throw new ArgumentException("--quiet requires --install-dir or --uninstall.");

            ApplicationConfiguration.Initialize();
            using var form = new InstallerForm(package, logger);
            Application.Run(form);
            return form.ExitCode;
        }
        catch (Exception error)
        {
            logger?.Error(error.ToString());
            if (!quiet)
            {
                MessageBox.Show(
                    $"{error.Message}\n\nDetails were written to:\n{logger?.Path ?? "the installer log"}",
                    "Z+ Setup — Installation Error",
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Error);
            }
            return 1;
        }
        finally
        {
            logger?.Dispose();
        }
    }

    private static async Task<int> RunUninstallAsync(
        string[] args, bool quiet, InstallLogger logger)
    {
        string installPath = ReadOption(args, "--install-dir")
            ?? Uninstaller.RegisteredInstallPath()
            ?? throw new InvalidOperationException(
                "No registered Z+ installation was found. Supply --install-dir.");
        InstallRecord record = Uninstaller.ReadRecord(installPath);
        string? runningExecutable = Environment.ProcessPath;
        bool runningFromInstall = runningExecutable is not null &&
            Path.GetFullPath(runningExecutable).StartsWith(
                Path.TrimEndingDirectorySeparator(Path.GetFullPath(installPath)) +
                Path.DirectorySeparatorChar,
                StringComparison.OrdinalIgnoreCase);
        bool detached = args.Contains("--detached", StringComparer.Ordinal);

        if (!detached && runningFromInstall)
        {
            if (!quiet)
            {
                DialogResult confirmation = MessageBox.Show(
                    $"Remove Z+ {record.Version} from:\n{record.InstallPath}?\n\n" +
                    "Modified and untracked files, including user projects, will be preserved.",
                    "Uninstall Z+",
                    MessageBoxButtons.YesNo,
                    MessageBoxIcon.Warning,
                    MessageBoxDefaultButton.Button2);
                if (confirmation != DialogResult.Yes) return 0;
            }
            string temporaryCopy =
                Uninstaller.RelaunchUninstallerFromTemporaryDirectory(installPath);
            logger.Info($"Started temporary uninstaller copy: {temporaryCopy}");
            return 0;
        }

        if (!quiet && !detached)
        {
            DialogResult confirmation = MessageBox.Show(
                $"Remove Z+ {record.Version} from:\n{record.InstallPath}?\n\n" +
                "Modified and untracked files, including user projects, will be preserved.",
                "Uninstall Z+",
                MessageBoxButtons.YesNo,
                MessageBoxIcon.Warning,
                MessageBoxDefaultButton.Button2);
            if (confirmation != DialogResult.Yes) return 0;
        }

        UninstallResult result = await Uninstaller.UninstallAsync(
            installPath, logger, CancellationToken.None);
        if (detached && runningExecutable is not null)
            Uninstaller.ScheduleRunningCopyForRemoval(runningExecutable);
        if (!quiet && !detached)
        {
            string message = result.PreservedFiles.Count == 0
                ? "Z+ was removed successfully."
                : $"Z+ was removed. {result.PreservedFiles.Count} modified or untracked file(s) were kept in:\n" +
                  $"{result.InstallPath}\n\nReview or remove them yourself if no longer needed.";
            MessageBox.Show(message, "Uninstall Z+", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }
        return 0;
    }

    private static string? ReadOption(string[] args, string name)
    {
        int index = Array.IndexOf(args, name);
        if (index < 0) return null;
        if (index + 1 >= args.Length || args[index + 1].StartsWith("--", StringComparison.Ordinal))
            throw new ArgumentException($"{name} requires a value.");
        return args[index + 1];
    }
}
