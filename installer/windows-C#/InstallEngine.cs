using System.Security.Cryptography;
using System.Text.Json;

namespace ZPlus.Setup;

internal sealed class InstallEngine(
    PackageManifestReader package,
    InstallLogger logger)
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true
    };

    public async Task<InstallRecord> InstallAsync(
        InstallOptions options,
        IProgress<InstallProgress>? progress,
        CancellationToken cancellationToken)
    {
        ValidatedInstallLocation location = ValidateLocation(options.InstallPath);
        string target = location.Path;
        InstallRecord? previous = location.ExistingRecord;
        string markerPath = Path.Combine(target, PackageInfo.MarkerFileName);

        IReadOnlyList<PackageFile> files = package.SelectFiles(options);
        if (!files.Any(file => file.Path.Equals("bin/zp.exe", StringComparison.OrdinalIgnoreCase)))
            throw new InvalidDataException("The selected release payload does not contain bin/zp.exe.");
        var newHashes = files.ToDictionary(file => file.Path, file => file.Sha256,
            StringComparer.OrdinalIgnoreCase);

        string setupExecutable = Environment.ProcessPath
            ?? throw new InvalidOperationException("The running setup executable path is unavailable.");
        string uninstallerRelativePath = "ZPlusUninstall.exe";
        long setupLength = new FileInfo(setupExecutable).Length;
        string setupHash = await HashFileAsync(setupExecutable, cancellationToken);
        newHashes[uninstallerRelativePath] = setupHash;
        long newPayloadBytes = files.Sum(file => file.Length) + setupLength;
        var oldTracked = previous?.Files ?? new Dictionary<string, string>(
            StringComparer.OrdinalIgnoreCase);
        var staleFiles = new List<string>();
        var modifiedConflicts = new List<string>();
        foreach ((string relativePath, string expectedHash) in oldTracked)
        {
            ValidateOwnedPath(relativePath);
            string existing = SafeDestinationPath(target, relativePath);
            if (!File.Exists(existing)) continue;
            string actual = await HashFileAsync(existing, cancellationToken);
            if (newHashes.TryGetValue(relativePath, out string? newHash))
            {
                if (!actual.Equals(newHash, StringComparison.OrdinalIgnoreCase) &&
                    !actual.Equals(expectedHash, StringComparison.OrdinalIgnoreCase))
                    modifiedConflicts.Add(relativePath);
            }

            else if (actual.Equals(expectedHash, StringComparison.OrdinalIgnoreCase))
            {
                staleFiles.Add(relativePath);
            }
        }
        if (modifiedConflicts.Count > 0 && !options.ReplaceModifiedFiles)
            throw new InvalidOperationException(
                "These installed files were changed since installation and need confirmation " +
                "before replacement:\n" + string.Join("\n", modifiedConflicts));

        if (previous is not null &&
            !oldTracked.ContainsKey(uninstallerRelativePath) &&
            File.Exists(Path.Combine(target, uninstallerRelativePath)))
            throw new IOException(
                $"An unrelated file already exists at '{Path.Combine(target, uninstallerRelativePath)}'.");

        ValidateSelectedDestinations(target, files, oldTracked, previous is not null);
        CheckDiskSpace(target, newPayloadBytes, oldTracked);

        string parent = Path.GetDirectoryName(target)
            ?? throw new InvalidOperationException("The install destination has no parent directory.");
        Directory.CreateDirectory(parent);
        string transactionId = Guid.NewGuid().ToString("N");
        string staging = Path.Combine(parent, $".zplus-stage-{transactionId}");
        string backup = Path.Combine(parent, $".zplus-backup-{transactionId}");
        Directory.CreateDirectory(staging);
        try
        {
            Directory.CreateDirectory(backup);
        }
        catch
        {
            Directory.Delete(staging, recursive: true);
            throw;
        }
        var rollbackFiles = new Dictionary<string, bool>(StringComparer.OrdinalIgnoreCase);
        var createdShortcuts = new List<(string Path, string Target, string Arguments)>();
        var createdDirectories = new List<string>();
        var createdDirectoryPaths = new List<string>();
        bool targetCreated = false;
        bool pathAddedNow = false;
        bool associationAttempted = false;
        bool installEntryWritten = false;
        IntegrationSnapshot integrationSnapshot = WindowsIntegration.Capture();
        bool keepBackupForRecovery = false;

        try
        {
            long stagingTotal = newPayloadBytes;
            long stagingCompleted = 0;
            foreach (PackageFile file in files)
            {
                cancellationToken.ThrowIfCancellationRequested();
                string source = PackageManifestReader.ResolvePayloadPath(
                    package.PackageDirectory, file.Path);
                EnsureNoReparsePoints(package.PackageDirectory, source);
                string staged = SafeDestinationPath(staging, file.Path);
                await CopyAndHashAsync(source, staged, file.Sha256,
                    amount =>
                    {
                        stagingCompleted += amount;
                        progress?.Report(new InstallProgress(
                            "Verifying and staging", file.Path, stagingCompleted, stagingTotal));
                    }, cancellationToken);
            }

            string stagedUninstaller = Path.Combine(staging, uninstallerRelativePath);
            await CopyAndHashAsync(
                setupExecutable, stagedUninstaller, setupHash,
                amount =>
                {
                    stagingCompleted += amount;
                    progress?.Report(new InstallProgress(
                        "Verifying and staging", uninstallerRelativePath,
                        stagingCompleted, stagingTotal));
                }, cancellationToken);

            var affected = new HashSet<string>(newHashes.Keys, StringComparer.OrdinalIgnoreCase);
            foreach (string stale in staleFiles) affected.Add(stale);
            affected.Add(PackageInfo.MarkerFileName);
            foreach (string relativePath in affected)
            {
                cancellationToken.ThrowIfCancellationRequested();
                string destination = SafeDestinationPath(target, relativePath);
                EnsureNoReparsePoints(target, destination);
                string backupPath = SafeDestinationPath(backup, relativePath);
                bool existed = File.Exists(destination);
                if (existed)
                {
                    Directory.CreateDirectory(Path.GetDirectoryName(backupPath)!);
                    File.Copy(destination, backupPath, overwrite: false);
                }
                rollbackFiles[relativePath] = existed;
            }

            targetCreated = !Directory.Exists(target);
            Directory.CreateDirectory(target);
            long installTotal = newPayloadBytes + staleFiles.Sum(relativePath =>
            {
                string file = SafeDestinationPath(target, relativePath);
                return File.Exists(file) ? new FileInfo(file).Length : 0;
            });
            long installCompleted = 0;
            foreach (PackageFile file in files)
            {
                cancellationToken.ThrowIfCancellationRequested();
                string source = SafeDestinationPath(staging, file.Path);
                string destination = SafeDestinationPath(target, file.Path);
                EnsureInstallParent(
                    target, destination, createdDirectories, createdDirectoryPaths);
                await CopyAndHashAsync(source, destination, file.Sha256,
                    amount =>
                    {
                        installCompleted += amount;
                        progress?.Report(new InstallProgress(
                            "Installing components", file.Path, installCompleted, installTotal));
                    }, cancellationToken);
            }

            await CopyAndHashAsync(stagedUninstaller,
                Path.Combine(target, uninstallerRelativePath), setupHash,
                amount =>
                {
                    installCompleted += amount;
                    progress?.Report(new InstallProgress(
                        "Installing components", uninstallerRelativePath,
                        installCompleted, installTotal));
                }, cancellationToken);

            foreach (string relativePath in staleFiles)
            {
                cancellationToken.ThrowIfCancellationRequested();
                string stale = SafeDestinationPath(target, relativePath);
                long staleLength = File.Exists(stale) ? new FileInfo(stale).Length : 0;
                if (File.Exists(stale)) File.Delete(stale);
                installCompleted += staleLength;
                progress?.Report(new InstallProgress(
                    "Removing retired component files", relativePath,
                    installCompleted, installTotal));
            }

            var record = new InstallRecord
            {
                Version = PackageInfo.Version,
                InstallPath = target,
                Files = newHashes,
                AddedPathEntry = previous?.AddedPathEntry ?? false,
                AssociationApplied = false,
                PreviousZpAssociation = previous?.PreviousZpAssociation,
                PreviousZpKeyExisted = previous?.AssociationApplied == true
                    ? previous.PreviousZpKeyExisted
                    : integrationSnapshot.ZpKeyExisted,
                ShortcutPaths = [],
                Directories = previous?.Directories.ToList() ?? []
            };
            foreach (string directory in createdDirectoryPaths)
                if (!record.Directories.Contains(directory, StringComparer.OrdinalIgnoreCase))
                    record.Directories.Add(directory);
            foreach (string oldShortcut in previous?.ShortcutPaths ?? [])
                if (File.Exists(oldShortcut)) record.ShortcutPaths.Add(oldShortcut);

            string markerTemporary = markerPath + ".tmp-" + transactionId;
            await File.WriteAllTextAsync(markerTemporary,
                JsonSerializer.Serialize(record, JsonOptions), cancellationToken);
            File.Move(markerTemporary, markerPath, overwrite: true);

            cancellationToken.ThrowIfCancellationRequested();
            progress?.Report(new InstallProgress(
                "Verifying compiler and standard library", "", 0, 0));
            await new CompilerVerifier(logger).VerifyAsync(
                target,
                files.Select(file => file.Component)
                    .Distinct(StringComparer.Ordinal).ToArray(),
                cancellationToken);

            string binDirectory = Path.Combine(target, "bin");
            pathAddedNow = WindowsIntegration.AddUserPath(binDirectory);
            record.AddedPathEntry |= pathAddedNow;
            if (previous?.AddedPathEntry == true) record.AddedPathEntry = true;

            associationAttempted = true;
            record.PreviousZpAssociation = previous?.AssociationApplied == true
                ? previous.PreviousZpAssociation
                : integrationSnapshot.ZpAssociation;
            WindowsIntegration.RegisterFileAssociation(target);
            record.AssociationApplied = true;

            string startMenu = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.StartMenu),
                "Programs", "Z+");
            string compiler = Path.Combine(binDirectory, "zp.exe");
            string syntaxDocument = Path.Combine(target, "docs", "SYNTAX.md");
            string syntaxShortcut = Path.Combine(startMenu, "Z+ Syntax Reference.lnk");
            if (!File.Exists(syntaxDocument) &&
                record.ShortcutPaths.Contains(syntaxShortcut, StringComparer.OrdinalIgnoreCase))
            {
                WindowsIntegration.RemoveShortcutIfOwned(
                    syntaxShortcut, syntaxDocument, "");
                record.ShortcutPaths.RemoveAll(path =>
                    path.Equals(syntaxShortcut, StringComparison.OrdinalIgnoreCase));
            }
            var shortcutSpecs = new List<(string Path, string Target, string Arguments, string Working)>
            {
                (Path.Combine(startMenu, "Z+ Compiler.lnk"), compiler,
                    "version", target),
                (Path.Combine(startMenu, "Z+ Syntax Reference.lnk"),
                    Path.Combine(target, "docs", "SYNTAX.md"), "", target),
                (Path.Combine(startMenu, "Uninstall Z+.lnk"),
                    Path.Combine(target, uninstallerRelativePath), "--uninstall", target)
            };
            if (options.CreateDesktopShortcut)
                shortcutSpecs.Add((
                    Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Desktop),
                        "Z+ Compiler.lnk"),
                    compiler, "version", target));
            foreach ((string shortcutPath, string shortcutTarget, string arguments, string working) in
                     shortcutSpecs)
            {
                if (shortcutPath.EndsWith("Syntax Reference.lnk", StringComparison.OrdinalIgnoreCase) &&
                    !File.Exists(Path.Combine(target, "docs", "SYNTAX.md")))
                    continue;
                bool existed = File.Exists(shortcutPath);
                WindowsIntegration.CreateShortcut(
                    shortcutPath, shortcutTarget, arguments, working);
                if (!existed)
                    createdShortcuts.Add((shortcutPath, shortcutTarget, arguments));
                if (!record.ShortcutPaths.Contains(shortcutPath, StringComparer.OrdinalIgnoreCase))
                    record.ShortcutPaths.Add(shortcutPath);
            }
            if (!options.CreateDesktopShortcut)
            {
                string desktopShortcut = Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.Desktop),
                    "Z+ Compiler.lnk");
                if (previous?.ShortcutPaths.Contains(
                        desktopShortcut, StringComparer.OrdinalIgnoreCase) == true)
                {
                    WindowsIntegration.RemoveShortcutIfOwned(
                        desktopShortcut, compiler, "version");
                    record.ShortcutPaths.RemoveAll(path =>
                        path.Equals(desktopShortcut, StringComparison.OrdinalIgnoreCase));
                }
            }

            cancellationToken.ThrowIfCancellationRequested();
            await File.WriteAllTextAsync(markerTemporary,
                JsonSerializer.Serialize(record, JsonOptions), cancellationToken);
            File.Move(markerTemporary, markerPath, overwrite: true);
            installEntryWritten = true;
            WindowsIntegration.RegisterUninstallEntry(record);
            logger.Info($"Z+ {PackageInfo.Version} installed at '{target}'.");
            return record;
        }
        catch (Exception installError)
        {
            logger.Error($"Installation failed: {installError}");
            var rollbackErrors = new List<Exception>();
            foreach ((string shortcutPath, string shortcutTarget, string arguments) in createdShortcuts)
            {
                try
                {
                    WindowsIntegration.RemoveShortcutIfOwned(
                        shortcutPath, shortcutTarget, arguments);
                }
                catch (Exception error) { rollbackErrors.Add(error); }
            }
            if (pathAddedNow)
            {
                try { WindowsIntegration.RemoveUserPath(Path.Combine(target, "bin")); }
                catch (Exception error) { rollbackErrors.Add(error); }
            }
            if (associationAttempted)
            {
                try { WindowsIntegration.RestoreSnapshot(integrationSnapshot, target); }
                catch (Exception error) { rollbackErrors.Add(error); }
            }
            if (installEntryWritten)
            {
                try
                {
                    if (previous is null) WindowsIntegration.RemoveUninstallEntry(target);
                    else WindowsIntegration.RegisterUninstallEntry(previous);
                }
                catch (Exception error) { rollbackErrors.Add(error); }
            }

            foreach ((string relativePath, bool existed) in rollbackFiles.Reverse())
            {
                try
                {
                    string destination = SafeDestinationPath(target, relativePath);
                    if (existed)
                    {
                        string backupPath = SafeDestinationPath(backup, relativePath);
                        if (!File.Exists(backupPath))
                            throw new IOException($"Rollback copy is missing: {backupPath}");
                        Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
                        File.Copy(backupPath, destination, overwrite: true);
                    }
                    else if (File.Exists(destination))
                    {
                        File.Delete(destination);
                    }
                }
                catch (Exception error) { rollbackErrors.Add(error); }
            }
            foreach (string directory in createdDirectories.AsEnumerable().Reverse())
            {
                try
                {
                    if (Directory.Exists(directory) &&
                        !Directory.EnumerateFileSystemEntries(directory).Any())
                        Directory.Delete(directory);
                }
                catch (Exception error) { rollbackErrors.Add(error); }
            }
            if (targetCreated)
            {
                try { if (Directory.Exists(target)) Directory.Delete(target, recursive: false); }
                catch (Exception error) { rollbackErrors.Add(error); }
            }
            if (rollbackErrors.Count > 0)
            {
                keepBackupForRecovery = true;
                foreach (Exception error in rollbackErrors)
                    logger.Error($"Rollback failed: {error}");
                throw new AggregateException(
                    $"Installation failed and rollback was incomplete. Recovery files were kept at '{backup}'. " +
                    $"Original error: {installError.Message}",
                    [installError, .. rollbackErrors]);
            }
            throw;
        }
        finally
        {
            try
            {
                if (Directory.Exists(staging)) Directory.Delete(staging, recursive: true);
            }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException)
            {
                logger.Error($"Could not remove temporary staging directory '{staging}': {error}");
            }
            if (!keepBackupForRecovery)
            {
                try
                {
                    if (Directory.Exists(backup)) Directory.Delete(backup, recursive: true);
                }
                catch (Exception error) when (error is IOException or UnauthorizedAccessException)
                {
                    logger.Error($"Could not remove temporary rollback data '{backup}': {error}");
                }
            }
        }
    }

    private static InstallRecord? ReadExistingRecord(string target, string markerPath)
    {
        if (!File.Exists(markerPath)) return null;
        InstallRecord record;
        try
        {
            record = JsonSerializer.Deserialize<InstallRecord>(File.ReadAllText(markerPath))
                ?? throw new InvalidDataException("The existing Z+ installation marker is empty.");
        }
        catch (Exception error) when (error is JsonException or IOException or
                                       UnauthorizedAccessException)
        {
            throw new InvalidDataException(
                $"The destination contains an unreadable installation marker: {markerPath}", error);
        }
        if (record.Product != PackageInfo.Product || !PathEquals(record.InstallPath, target))
            throw new InvalidDataException(
                $"The destination marker does not identify a Z+ installation at '{target}'.");
        return record;
    }

    private static string ValidateDestination(string path)
    {
        if (string.IsNullOrWhiteSpace(path))
            throw new ArgumentException("Choose a non-empty installation directory.");
        string target = Path.GetFullPath(path.Trim());
        string root = Path.GetPathRoot(target)
            ?? throw new ArgumentException("The installation path must be fully qualified.");
        if (PathEquals(target, root) || target.StartsWith(@"\\", StringComparison.Ordinal))
            throw new ArgumentException(
                "Choose a directory below a local drive root; drive roots and network paths are not valid destinations.");
        if (PathEquals(target, AppContext.BaseDirectory) ||
            target.StartsWith(Path.GetFullPath(AppContext.BaseDirectory),
                StringComparison.OrdinalIgnoreCase))
            throw new ArgumentException("The installer cannot install into its own package directory.");

        string windowsDirectory = Environment.GetFolderPath(Environment.SpecialFolder.Windows);
        if (target.StartsWith(Path.TrimEndingDirectorySeparator(windowsDirectory) +
                              Path.DirectorySeparatorChar,
                StringComparison.OrdinalIgnoreCase))
            throw new ArgumentException("Installing inside the Windows directory is not permitted.");

        var drive = new DriveInfo(root);
        if (drive.DriveType is DriveType.Network or DriveType.CDRom)
            throw new ArgumentException("Choose a writable local drive for installation.");
        EnsureNoReparsePoints(root, target);
        return Path.TrimEndingDirectorySeparator(target);
    }

    internal static ValidatedInstallLocation ValidateLocation(string path)
    {
        string target = ValidateDestination(path);
        string? registeredInstall = WindowsIntegration.ExistingInstallPath();
        if (!string.IsNullOrWhiteSpace(registeredInstall) &&
            !PathEquals(registeredInstall, target))
            throw new InvalidOperationException(
                $"An existing Z+ installation is registered at '{registeredInstall}'. " +
                "Choose that location to update it, or uninstall it first.");

        string markerPath = Path.Combine(target, PackageInfo.MarkerFileName);
        InstallRecord? previous = ReadExistingRecord(target, markerPath);
        if (Directory.Exists(target) && previous is null &&
            Directory.EnumerateFileSystemEntries(target).Any())
            throw new InvalidOperationException(
                $"The destination is not empty and is not a recognized Z+ installation: {target}");

        EnsureWritableInstallDirectory(target);
        return new ValidatedInstallLocation(target, previous);
    }

    private static void EnsureWritableInstallDirectory(string target)
    {
        string candidate = target;
        while (true)
        {
            try
            {
                FileAttributes attributes = File.GetAttributes(candidate);
                if ((attributes & FileAttributes.Directory) == 0)
                    throw new IOException(
                        $"An existing file cannot be used as the installation directory: {candidate}");
                break;
            }
            catch (FileNotFoundException)
            {
                candidate = Path.GetDirectoryName(candidate)
                    ?? throw new IOException(
                        $"Cannot find an existing parent directory for '{target}'.");
            }
            catch (DirectoryNotFoundException)
            {
                candidate = Path.GetDirectoryName(candidate)
                    ?? throw new IOException(
                        $"Cannot find an existing parent directory for '{target}'.");
            }
        }

        string probePath = Path.Combine(candidate, $".zplus-write-test-{Guid.NewGuid():N}.tmp");
        using var probe = new FileStream(
            probePath, FileMode.CreateNew, FileAccess.Write, FileShare.None,
            bufferSize: 1, FileOptions.DeleteOnClose);
    }

    private static void CheckDiskSpace(
        string target, long selectedBytes,
        IReadOnlyDictionary<string, string> oldTracked)
    {
        string root = Path.GetPathRoot(target)
            ?? throw new IOException("Cannot determine the installation drive.");
        var drive = new DriveInfo(root);
        long backupBytes = 0;
        foreach (string path in oldTracked.Keys)
        {
            string file = SafeDestinationPath(target, path);
            if (File.Exists(file)) backupBytes += new FileInfo(file).Length;
        }
        long required = checked(selectedBytes * 2 + backupBytes + 1024 * 1024);
        if (drive.AvailableFreeSpace < required)
            throw new IOException(
                $"Not enough free space on {root}. Required approximately " +
                $"{required / (1024d * 1024d):N0} MiB; available " +
                $"{drive.AvailableFreeSpace / (1024d * 1024d):N0} MiB.");
    }

    private static void ValidateSelectedDestinations(
        string target,
        IReadOnlyList<PackageFile> files,
        IReadOnlyDictionary<string, string> oldTracked,
        bool updating)
    {
        foreach (PackageFile file in files)
        {
            string destination = SafeDestinationPath(target, file.Path);
            EnsureNoReparsePoints(target, destination);
            if (Directory.Exists(destination))
                throw new IOException(
                    $"A directory would be overwritten by installation: {destination}");
            if (File.Exists(destination) && !oldTracked.ContainsKey(file.Path))
                throw new IOException(
                    $"An unrelated file would be overwritten by installation: {destination}");
        }
        string uninstaller = Path.Combine(target, "ZPlusUninstall.exe");
        if (File.Exists(uninstaller) && !oldTracked.ContainsKey("ZPlusUninstall.exe") && updating)
            throw new IOException(
                $"An unrelated file would be overwritten by installation: {uninstaller}");
    }

    private static string SafeDestinationPath(string root, string relativePath)
    {
        ValidateOwnedPath(relativePath);
        string fullRoot = Path.GetFullPath(root)
            .TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar) +
            Path.DirectorySeparatorChar;
        string fullPath = Path.GetFullPath(Path.Combine(
            root, relativePath.Replace('/', Path.DirectorySeparatorChar)));
        if (!fullPath.StartsWith(fullRoot, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException($"Installation path escapes its root: {relativePath}");
        return fullPath;
    }

    private static void ValidateOwnedPath(string relativePath)
    {
        if (string.IsNullOrWhiteSpace(relativePath) || Path.IsPathRooted(relativePath) ||
            relativePath.Contains(':') || relativePath.Contains('\\') ||
            relativePath.Split('/').Any(part =>
                part.Length == 0 || part is "." or ".." ||
                part.EndsWith(' ') || part.EndsWith('.')))
            throw new InvalidDataException($"Unsafe installed-file path in the installation record: {relativePath}");
    }

    private static void EnsureNoReparsePoints(string root, string path)
    {
        string canonicalRoot = Path.GetFullPath(root);
        string canonicalPath = Path.GetFullPath(path);
        string current = canonicalRoot;
        if (File.Exists(current) || Directory.Exists(current))
            RejectReparsePoint(current);
        string relative = Path.GetRelativePath(canonicalRoot, canonicalPath);
        foreach (string part in relative.Split(
                     [Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar],
                     StringSplitOptions.RemoveEmptyEntries))
        {
            current = Path.Combine(current, part);
            if (File.Exists(current) || Directory.Exists(current))
                RejectReparsePoint(current);
        }
    }

    private static void EnsureInstallParent(
        string installRoot, string destination,
        ICollection<string> createdDirectories,
        ICollection<string> createdDirectoryPaths)
    {
        string parent = Path.GetDirectoryName(destination)
            ?? throw new IOException($"Installed file has no parent directory: {destination}");
        string relative = Path.GetRelativePath(installRoot, parent);
        if (relative == ".") return;
        var missing = new Stack<string>();
        string current = parent;
        while (!Directory.Exists(current) &&
               !current.Equals(installRoot, StringComparison.OrdinalIgnoreCase))
        {
            missing.Push(current);
            current = Path.GetDirectoryName(current)
                ?? throw new IOException($"Cannot create installation directory: {parent}");
        }
        while (missing.Count > 0)
        {
            string directory = missing.Pop();
            Directory.CreateDirectory(directory);
            createdDirectories.Add(directory);
            string relativeDirectory = Path.GetRelativePath(installRoot, directory)
                .Replace(Path.DirectorySeparatorChar, '/');
            if (!createdDirectoryPaths.Contains(
                    relativeDirectory, StringComparer.OrdinalIgnoreCase))
                createdDirectoryPaths.Add(relativeDirectory);
        }
    }

    private static void RejectReparsePoint(string path)
    {
        if ((File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0)
            throw new IOException($"Symbolic links and reparse points are not allowed in installer paths: {path}");
    }

    private static async Task<string> CopyAndHashAsync(
        string source,
        string destination,
        string? expectedHash,
        Action<long>? onBytes,
        CancellationToken cancellationToken)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
        string temporary = destination + ".zplus-tmp-" + Guid.NewGuid().ToString("N");
        try
        {
            using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
            await using (var input = new FileStream(
                             source, FileMode.Open, FileAccess.Read, FileShare.Read,
                             128 * 1024, FileOptions.Asynchronous | FileOptions.SequentialScan))
            await using (var output = new FileStream(
                             temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None,
                             128 * 1024, FileOptions.Asynchronous | FileOptions.SequentialScan))
            {
                byte[] buffer = new byte[128 * 1024];
                int read;
                while ((read = await input.ReadAsync(buffer, cancellationToken)) > 0)
                {
                    cancellationToken.ThrowIfCancellationRequested();
                    hash.AppendData(buffer.AsSpan(0, read));
                    await output.WriteAsync(buffer.AsMemory(0, read), cancellationToken);
                    onBytes?.Invoke(read);
                }
                await output.FlushAsync(cancellationToken);
            }
            string actualHash = Convert.ToHexString(hash.GetHashAndReset());
            if (expectedHash is not null &&
                !actualHash.Equals(expectedHash, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException(
                    $"SHA-256 integrity check failed for '{source}'. " +
                    $"Expected {expectedHash}; found {actualHash}.");
            File.Move(temporary, destination, overwrite: true);
            return actualHash;
        }
        finally
        {
            if (File.Exists(temporary)) File.Delete(temporary);
        }
    }

    private static async Task<string> HashFileAsync(
        string path, CancellationToken cancellationToken)
    {
        await using var stream = new FileStream(
            path, FileMode.Open, FileAccess.Read, FileShare.Read,
            128 * 1024, FileOptions.Asynchronous | FileOptions.SequentialScan);
        byte[] hash = await SHA256.HashDataAsync(stream, cancellationToken);
        return Convert.ToHexString(hash);
    }

    private static bool PathEquals(string first, string second)
    {
        return string.Equals(
            Path.TrimEndingDirectorySeparator(Path.GetFullPath(first)),
            Path.TrimEndingDirectorySeparator(Path.GetFullPath(second)),
            StringComparison.OrdinalIgnoreCase);
    }
}
