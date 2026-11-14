using System.Diagnostics;

namespace ZPlus.Setup;

internal sealed class CompilerVerifier(InstallLogger logger)
{
    private static readonly string[] RequiredRuntimeFiles =
    [
        "zgraphics_runtime.dll",
        "zaudio_runtime.dll",
        "zphysics_runtime.dll",
        "sqlite3.dll"
    ];

    public async Task VerifyAsync(
        string installPath,
        IReadOnlyCollection<string> installedComponents,
        CancellationToken cancellationToken)
    {
        string bin = Path.Combine(installPath, "bin");
        string compiler = Path.Combine(bin, "zp.exe");
        if (!File.Exists(compiler))
            throw new FileNotFoundException("The installed Z+ compiler is missing.", compiler);
        foreach (string runtime in RequiredRuntimeFiles)
        {
            string path = Path.Combine(bin, runtime);
            if (!File.Exists(path))
                throw new FileNotFoundException(
                    $"A required Z+ runtime dependency is missing: {runtime}", path);
        }

        ProcessResult version = await RunProcessAsync(
            compiler, ["version"], installPath, null, cancellationToken);
        EnsureSuccess("Compiler version check", version);
        if (!version.StandardOutput.Contains(PackageInfo.Version, StringComparison.Ordinal))
            throw new InvalidOperationException(
                $"Compiler reported an unexpected version. Expected {PackageInfo.Version}; " +
                $"received: {version.StandardOutput.Trim()}");
        logger.Info($"Compiler version verified: {version.StandardOutput.Trim()}");

        string stdlib = Path.Combine(installPath, "stdlib");
        if (!Directory.Exists(stdlib) ||
            !File.Exists(Path.Combine(stdlib, "zmath", "zmath.zh")))
            throw new DirectoryNotFoundException(
                $"The installed standard library is missing or incomplete: {stdlib}");

        string verificationDirectory = Path.Combine(
            Path.GetTempPath(), $"zplus-verify-{Guid.NewGuid():N}");
        Directory.CreateDirectory(verificationDirectory);
        try
        {
            string libraryProgram = Path.Combine(verificationDirectory, "stdlib-check.zp");
            await File.WriteAllTextAsync(libraryProgram,
                "use zmath as math\n\nstart:\n    say(math.pi() > 3 and math.pi() < 4 ? \"stdlib-ok\" : \"stdlib-fail\")\nstop\n",
                cancellationToken);
            var environment = new Dictionary<string, string?>
            {
                ["ZPLUS_STDLIB_PATH"] = stdlib
            };
            ProcessResult check = await RunProcessAsync(
                compiler, ["check", libraryProgram], installPath, environment, cancellationToken);
            EnsureSuccess("Standard-library compilation check", check);
            ProcessResult libraryRun = await RunProcessAsync(
                compiler, ["run", libraryProgram], installPath, environment, cancellationToken);
            EnsureSuccess("Standard-library execution", libraryRun);
            if (libraryRun.StandardOutput.Trim() != "stdlib-ok")
                throw new InvalidOperationException(
                    "Standard-library smoke program returned unexpected output: " +
                    libraryRun.StandardOutput.Trim());
            logger.Info("Standard library module resolution and interpreter execution passed.");

            string nativeSource = Path.Combine(verificationDirectory, "native-check.zp");
            string nativeOutput = Path.Combine(verificationDirectory, "native-check.exe");
            await File.WriteAllTextAsync(nativeSource,
                "start:\n    say(\"Z+ native verification passed\")\nstop\n",
                cancellationToken);
            ProcessResult nativeBuild = await RunProcessAsync(
                compiler,
                ["build", nativeSource, "-o", nativeOutput],
                installPath,
                environment,
                cancellationToken);
            EnsureSuccess("Native compiler smoke build", nativeBuild);
            if (!File.Exists(nativeOutput))
                throw new FileNotFoundException(
                    "The compiler reported success but did not create its native output.",
                    nativeOutput);
            ProcessResult nativeRun = await RunProcessAsync(
                nativeOutput, [], verificationDirectory, null, cancellationToken);
            EnsureSuccess("Native executable smoke test", nativeRun);
            if (nativeRun.StandardOutput.Trim() != "Z+ native verification passed")
                throw new InvalidOperationException(
                    "Native smoke program returned unexpected output: " +
                    nativeRun.StandardOutput.Trim());
            logger.Info("Native Windows x64 compile-and-run smoke test passed.");
        }
        finally
        {
            if (Directory.Exists(verificationDirectory))
                Directory.Delete(verificationDirectory, recursive: true);
        }

        if (installedComponents.Contains("tools"))
        {
            string zbuild = Path.Combine(bin, "zbuild.exe");
            if (!File.Exists(zbuild) || !Directory.Exists(Path.Combine(bin, "zbuild-templates")))
                throw new FileNotFoundException(
                    "The selected ZBuild component or built-in templates are missing.", zbuild);
            ProcessResult tool = await RunProcessAsync(
                zbuild, ["--version"], installPath, null, cancellationToken);
            EnsureSuccess("ZBuild verification", tool);
            logger.Info($"ZBuild tool verified: {tool.StandardOutput.Trim()}");
            ProcessResult templates = await RunProcessAsync(
                zbuild, ["templates"], installPath, null, cancellationToken);
            EnsureSuccess("ZBuild template verification", templates);
            if (!templates.StandardOutput.Contains("Console", StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException(
                    "ZBuild did not discover its packaged Console project template.");
            logger.Info("ZBuild discovered its adjacent built-in templates.");
        }
    }

    private static async Task<ProcessResult> RunProcessAsync(
        string executable,
        IReadOnlyList<string> arguments,
        string workingDirectory,
        IReadOnlyDictionary<string, string?>? environment,
        CancellationToken cancellationToken)
    {
        var startInfo = new ProcessStartInfo
        {
            FileName = executable,
            WorkingDirectory = workingDirectory,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true
        };
        foreach (string argument in arguments) startInfo.ArgumentList.Add(argument);
        if (environment is not null)
            foreach ((string key, string? value) in environment)
                startInfo.Environment[key] = value;

        using var process = new Process { StartInfo = startInfo };
        if (!process.Start())
            throw new InvalidOperationException($"Could not start '{executable}'.");
        Task<string> stdoutTask = process.StandardOutput.ReadToEndAsync(cancellationToken);
        Task<string> stderrTask = process.StandardError.ReadToEndAsync(cancellationToken);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(60));
        try
        {
            await process.WaitForExitAsync(timeout.Token);
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
        {
            process.Kill(entireProcessTree: true);
            throw new TimeoutException($"'{executable}' did not finish within 60 seconds.");
        }
        return new ProcessResult(process.ExitCode, await stdoutTask, await stderrTask);
    }

    private static void EnsureSuccess(string operation, ProcessResult result)
    {
        if (result.ExitCode == 0) return;
        throw new InvalidOperationException(
            $"{operation} failed with exit code {result.ExitCode}.\n" +
            $"stdout:\n{result.StandardOutput}\nstderr:\n{result.StandardError}");
    }

    private sealed record ProcessResult(int ExitCode, string StandardOutput, string StandardError);
}
