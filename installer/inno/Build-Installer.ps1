[CmdletBinding()]
param(
    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'output'),
    [string]$BinaryDirectory = (Join-Path $PSScriptRoot '..\..\bin'),
    [string]$IsccPath
)

$ErrorActionPreference = 'Stop'
$requiredInnoVersion = '7.1.1'
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not (Test-Path -LiteralPath $BinaryDirectory -PathType Container)) {
    throw "The compiler distribution directory does not exist: $BinaryDirectory"
}
$binaryDirectory = (Resolve-Path -LiteralPath $BinaryDirectory).Path
$temporaryDirectory = Join-Path ([IO.Path]::GetTempPath()) ("zplus-inno-" + [guid]::NewGuid().ToString('N'))

function Get-IsccCompiler {
    param([string]$RequestedPath)

    $candidates = [Collections.Generic.List[string]]::new()
    if ($RequestedPath) {
        if (-not (Test-Path -LiteralPath $RequestedPath -PathType Leaf)) {
            throw "The specified ISCC.exe does not exist: $RequestedPath"
        }
        $candidates.Add((Resolve-Path -LiteralPath $RequestedPath).Path)
    } else {
        $onPath = Get-Command ISCC.exe -ErrorAction SilentlyContinue
        if ($onPath) {
            $candidates.Add($onPath.Source)
        }

        foreach ($programFiles in @(${env:ProgramFiles(x86)}, $env:ProgramFiles)) {
            if ($programFiles) {
                $candidates.Add((Join-Path $programFiles 'Inno Setup 7.1.1\ISCC.exe'))
                $candidates.Add((Join-Path $programFiles 'Inno Setup 7\ISCC.exe'))
            }
        }

        foreach ($registryRoot in @(
            'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall',
            'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall',
            'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall'
        )) {
            foreach ($key in (Get-ChildItem -LiteralPath $registryRoot -ErrorAction SilentlyContinue)) {
                $app = Get-ItemProperty -LiteralPath $key.PSPath -ErrorAction SilentlyContinue
                if ($app.DisplayName -eq "Inno Setup $requiredInnoVersion" -and $app.InstallLocation) {
                    $candidates.Add((Join-Path $app.InstallLocation 'ISCC.exe'))
                }
            }
        }
    }

    $checked = [Collections.Generic.List[string]]::new()
    foreach ($candidate in $candidates | Select-Object -Unique) {
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            continue
        }
        $startInfo = New-Object Diagnostics.ProcessStartInfo
        $startInfo.FileName = $candidate
        $startInfo.Arguments = '--version'
        $startInfo.UseShellExecute = $false
        $startInfo.CreateNoWindow = $true
        $startInfo.RedirectStandardOutput = $true
        $startInfo.RedirectStandardError = $true
        $versionProcess = New-Object Diagnostics.Process
        $versionProcess.StartInfo = $startInfo
        if (-not $versionProcess.Start()) {
            throw "Could not start ISCC at '$candidate' to verify its version."
        }
        $versionOutput = $versionProcess.StandardOutput.ReadToEnd()
        $versionError = $versionProcess.StandardError.ReadToEnd()
        $versionProcess.WaitForExit()
        $exitCode = $versionProcess.ExitCode
        $version = $versionOutput.Trim()
        if (-not $version) {
            $version = $versionError.Trim()
        }
        $versionProcess.Dispose()
        $checked.Add("$candidate ($version)")
        if ($exitCode -eq 0 -and $version -eq $requiredInnoVersion) {
            return [pscustomobject]@{ Path = $candidate; Version = $version }
        }
        if ($RequestedPath) {
            throw "ISCC at '$candidate' does not report the required Inno Setup version $requiredInnoVersion. Version output: '$version'."
        }
    }

    $details = if ($checked.Count -gt 0) { " Found: $($checked -join '; ')." } else { '' }
    throw "Inno Setup $requiredInnoVersion (ISCC.exe) is required. Install that exact version or pass -IsccPath to its compiler.$details"
}

function Copy-DirectoryContents {
    param([string]$SourceDirectory, [string]$DestinationDirectory)

    if (-not (Test-Path -LiteralPath $SourceDirectory -PathType Container)) {
        throw "Required distribution directory is missing: $SourceDirectory"
    }
    New-Item -ItemType Directory -Path $DestinationDirectory -Force | Out-Null
    foreach ($entry in Get-ChildItem -LiteralPath $SourceDirectory -Force) {
        Copy-Item -LiteralPath $entry.FullName -Destination $DestinationDirectory -Recurse -Force
    }
}

function Get-CompilerVersion {
    param([string]$CompilerPath)

    $output = (& $CompilerPath version 2>&1 | Out-String).Trim()
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0 -or $output -notmatch '^Z\+ Compiler (?<version>\d+\.\d+\.\d+)$') {
        throw "Could not read a stable version from '$CompilerPath'. Output: '$output'."
    }
    return $Matches.version
}

function New-SetupIcon {
    param([string]$LogoPath, [string]$IconPath)
    Add-Type -AssemblyName System.Drawing
    if (-not ('ZPlusInstallerNativeIcon' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class ZPlusInstallerNativeIcon
{
    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool DestroyIcon(IntPtr handle);
}
'@
    }
    $source = [System.Drawing.Image]::FromFile($LogoPath)
    $bitmap = [System.Drawing.Bitmap]::new(32, 32)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $handle = [IntPtr]::Zero
    try {
        $graphics.Clear([System.Drawing.Color]::Transparent)
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $scale = [Math]::Min(30.0 / $source.Width, 30.0 / $source.Height)
        $width = [Math]::Max(1, [int][Math]::Round($source.Width * $scale))
        $height = [Math]::Max(1, [int][Math]::Round($source.Height * $scale))
        $graphics.DrawImage($source, [int](32 - $width) / 2, [int](32 - $height) / 2, $width, $height)
        $handle = $bitmap.GetHicon()
        $icon = [System.Drawing.Icon]::FromHandle($handle)
        $stream = [IO.File]::Create($IconPath)
        try { $icon.Save($stream) } finally { $stream.Dispose(); $icon.Dispose() }
    }
    finally {
        if ($handle -ne [IntPtr]::Zero) { [void][ZPlusInstallerNativeIcon]::DestroyIcon($handle) }
        $graphics.Dispose()
        $bitmap.Dispose()
        $source.Dispose()
    }
}

function New-SetupWizardImage {
    param([string]$LogoPath, [string]$ImagePath)

    Add-Type -AssemblyName System.Drawing
    $source = [System.Drawing.Image]::FromFile($LogoPath)
    $bitmap = [System.Drawing.Bitmap]::new(
        55, 55, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.Clear([System.Drawing.Color]::White)
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $scale = [Math]::Min(51.0 / $source.Width, 51.0 / $source.Height)
        $width = [Math]::Max(1, [int][Math]::Round($source.Width * $scale))
        $height = [Math]::Max(1, [int][Math]::Round($source.Height * $scale))
        $graphics.DrawImage($source, [int](55 - $width) / 2, [int](55 - $height) / 2, $width, $height)
        $bitmap.Save($ImagePath, [System.Drawing.Imaging.ImageFormat]::Bmp)
    }
    finally {
        $graphics.Dispose()
        $bitmap.Dispose()
        $source.Dispose()
    }
}

try {
    $compiler = Get-IsccCompiler -RequestedPath $IsccPath
    Write-Host "Using Inno Setup $($compiler.Version): $($compiler.Path)"

    $version = Get-CompilerVersion -CompilerPath (Join-Path $binaryDirectory 'zp.exe')
    $requiredFiles = @(
        'zp.exe',
        'zbuild.exe',
        'zpbuild.exe',
        'zpm.exe',
        'zgraphics_runtime.dll',
        'zaudio_runtime.dll',
        'zphysics_runtime.dll',
        'sqlite3.dll',
        'zplus_input_runtime.dll',
        'stdlib\zfile\zfile.zh',
        'stdlib\zmath\zmath.zh',
        'zbuild-templates\Console\zproject.toml'
    )
    foreach ($relativePath in $requiredFiles) {
        $path = Join-Path $binaryDirectory $relativePath
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "The local Z+ $version distribution is missing required file '$relativePath' in '$binaryDirectory'."
        }
    }

    $zbuildVersion = (& (Join-Path $binaryDirectory 'zbuild.exe') --version 2>&1 | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $zbuildVersion -notmatch '^ZPBuild (?<version>\d+\.\d+\.\d+)$') {
        throw "The local ZBuild executable failed its version check: '$zbuildVersion'."
    }
    $zbuildSemanticVersion = $Matches.version
    $zpbuildVersion = (& (Join-Path $binaryDirectory 'zpbuild.exe') --version 2>&1 | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $zpbuildVersion -ne $zbuildVersion) {
        throw "The local ZPBuild alias failed its version check: '$zpbuildVersion'."
    }
    $zpmVersion = (& (Join-Path $binaryDirectory 'zpm.exe') --version 2>&1 | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $zpmVersion -notmatch '^ZPM (?<version>\d+\.\d+\.\d+)$') {
        throw "The local ZPM executable failed its version check: '$zpmVersion'."
    }
    $zpmSemanticVersion = $Matches.version

    $logoPath = Join-Path $repositoryRoot 'logo\Z+ Emblem - for ligth theme.png'
    foreach ($sourcePath in @(
        $logoPath,
        (Join-Path $repositoryRoot 'LICENSE.txt'),
        (Join-Path $repositoryRoot 'release\windows\THIRD-PARTY-NOTICES.txt')
    )) {
        if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
            throw "Required installer resource is missing: $sourcePath"
        }
    }

    New-Item -ItemType Directory -Path $temporaryDirectory -Force | Out-Null
    $stageDirectory = Join-Path $temporaryDirectory 'stage'
    $stagedBinaryDirectory = Join-Path $stageDirectory 'bin'
    New-Item -ItemType Directory -Path $stagedBinaryDirectory -Force | Out-Null
    $sourceCompilerHash = (Get-FileHash -LiteralPath (Join-Path $binaryDirectory 'zp.exe') -Algorithm SHA256).Hash
    foreach ($relativePath in @(
        'zp.exe',
        'zbuild.exe',
        'zpbuild.exe',
        'zpm.exe',
        'zgraphics_runtime.dll',
        'zaudio_runtime.dll',
        'zphysics_runtime.dll',
        'zplus_input_runtime.dll',
        'sqlite3.dll'
    )) {
        Copy-Item -LiteralPath (Join-Path $binaryDirectory $relativePath) `
            -Destination $stagedBinaryDirectory
    }
    Copy-DirectoryContents -SourceDirectory (Join-Path $binaryDirectory 'stdlib') `
        -DestinationDirectory (Join-Path $stagedBinaryDirectory 'stdlib')
    Copy-DirectoryContents -SourceDirectory (Join-Path $binaryDirectory 'zbuild-templates') `
        -DestinationDirectory (Join-Path $stagedBinaryDirectory 'zbuild-templates')
    $stagedCompiler = Join-Path $stagedBinaryDirectory 'zp.exe'
    $stagedCompilerVersion = Get-CompilerVersion -CompilerPath $stagedCompiler
    if ($stagedCompilerVersion -ne $version) {
        throw "Staged compiler version $stagedCompilerVersion does not match bin compiler version $version."
    }
    $stagedCompilerHash = (Get-FileHash -LiteralPath $stagedCompiler -Algorithm SHA256).Hash
    if ($stagedCompilerHash -ne $sourceCompilerHash) {
        throw 'The staged compiler is not byte-identical to the selected Release compiler.'
    }
    $stagedZbuild = Join-Path $stagedBinaryDirectory 'zbuild.exe'
    $stagedZbuildVersion = (& $stagedZbuild --version 2>&1 | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $stagedZbuildVersion -ne $zbuildVersion) {
        throw "The staged ZBuild executable failed its version check: '$stagedZbuildVersion'."
    }
    $stagedZpbuild = Join-Path $stagedBinaryDirectory 'zpbuild.exe'
    $stagedZpbuildVersion = (& $stagedZpbuild --version 2>&1 | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $stagedZpbuildVersion -ne $zbuildVersion) {
        throw "The staged ZPBuild alias failed its version check: '$stagedZpbuildVersion'."
    }
    $stagedZpm = Join-Path $stagedBinaryDirectory 'zpm.exe'
    $stagedZpmVersion = (& $stagedZpm --version 2>&1 | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $stagedZpmVersion -ne $zpmVersion) {
        throw "The staged ZPM executable failed its version check: '$stagedZpmVersion'."
    }
    foreach ($relativePath in @(
        'zp.exe',
        'zgraphics_runtime.dll',
        'zaudio_runtime.dll',
        'zphysics_runtime.dll',
        'zplus_input_runtime.dll'
    )) {
        $stagedPath = Join-Path $stagedBinaryDirectory $relativePath
        $stagedVersionInfo = (Get-Item -LiteralPath $stagedPath).VersionInfo
        if ($stagedVersionInfo.FileVersion.Trim() -ne $version -or
            $stagedVersionInfo.ProductVersion.Trim() -ne $version) {
            throw "Staged '$relativePath' does not contain Z+ $version Windows version metadata."
        }
    }
    foreach ($relativePath in @('zbuild.exe', 'zpbuild.exe')) {
        $stagedPath = Join-Path $stagedBinaryDirectory $relativePath
        $stagedVersionInfo = (Get-Item -LiteralPath $stagedPath).VersionInfo
        if ($stagedVersionInfo.FileVersion.Trim() -ne $zbuildSemanticVersion -or
            $stagedVersionInfo.ProductVersion.Trim() -ne $zbuildSemanticVersion) {
            throw "Staged '$relativePath' does not contain ZPBuild $zbuildSemanticVersion version metadata."
        }
    }
    $stagedZpmVersionInfo = (Get-Item -LiteralPath $stagedZpm).VersionInfo
    if ($stagedZpmVersionInfo.FileVersion.Trim() -ne $zpmSemanticVersion -or
        $stagedZpmVersionInfo.ProductVersion.Trim() -ne $zpmSemanticVersion) {
        throw "Staged 'zpm.exe' does not contain ZPM $zpmSemanticVersion version metadata."
    }
    Copy-DirectoryContents -SourceDirectory (Join-Path $repositoryRoot 'docs') -DestinationDirectory (Join-Path $stageDirectory 'docs')
    Copy-DirectoryContents -SourceDirectory (Join-Path $repositoryRoot 'examples') -DestinationDirectory (Join-Path $stageDirectory 'examples')
    $documentationLogoDirectory = Join-Path $stageDirectory 'docs\logo'
    New-Item -ItemType Directory -Path $documentationLogoDirectory -Force | Out-Null
    Copy-Item -LiteralPath $logoPath `
        -Destination (Join-Path $documentationLogoDirectory 'Z+ Emblem - for ligth theme.png')
    foreach ($document in @(
        @{ Source = 'README.md'; Destination = 'PROJECT-README.md' },
        @{ Source = 'docs\syntax.md'; Destination = 'LANGUAGE-SYNTAX.md' },
        @{ Source = 'MODULES.md'; Destination = 'MODULES.md' },
        @{ Source = 'NATIVE_BACKEND.md'; Destination = 'NATIVE_BACKEND.md' }
        @{ Source = 'docs\ZPM.md'; Destination = 'ZPM.md' }
    )) {
        Copy-Item -LiteralPath (Join-Path $repositoryRoot $document.Source) `
            -Destination (Join-Path $stageDirectory "docs\$($document.Destination)")
    }
    Copy-Item -LiteralPath (Join-Path $repositoryRoot 'LICENSE.txt') -Destination $stageDirectory
    Copy-Item -LiteralPath (Join-Path $repositoryRoot 'release\windows\THIRD-PARTY-NOTICES.txt') `
        -Destination $stageDirectory

    $iconPath = Join-Path $temporaryDirectory 'ZPlus.ico'
    $wizardImagePath = Join-Path $temporaryDirectory 'ZPlusWizard.bmp'
    New-SetupIcon -LogoPath $logoPath -IconPath $iconPath
    New-SetupWizardImage -LogoPath $logoPath -ImagePath $wizardImagePath
    $resolvedOutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
    New-Item -ItemType Directory -Path $resolvedOutputDirectory -Force | Out-Null
    $scriptPath = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot 'ZPlus.iss')).Path
    & $compiler.Path "/DAppVersion=$version" "/DStageDirectory=$stageDirectory" `
        "/DAppIcon=$iconPath" "/DAppWizardImage=$wizardImagePath" `
        "--output-dir=$resolvedOutputDirectory" $scriptPath
    $compileExitCode = $LASTEXITCODE
    if ($compileExitCode -ne 0) {
        throw "Inno Setup $requiredInnoVersion failed with exit code $compileExitCode."
    }
    $installerPath = Join-Path $resolvedOutputDirectory "ZPlus-$version-Setup.exe"
    if (-not (Test-Path -LiteralPath $installerPath -PathType Leaf)) {
        throw "Inno Setup reported success but the expected installer was not created: $installerPath"
    }
    $installerVersionInfo = (Get-Item -LiteralPath $installerPath).VersionInfo
    if ($installerVersionInfo.FileVersion.Trim() -ne $version -or
        $installerVersionInfo.ProductVersion.Trim() -ne $version) {
        throw "The generated installer does not contain Z+ $version Windows version metadata."
    }
    $installerHash = (Get-FileHash -LiteralPath $installerPath -Algorithm SHA256).Hash
    $installerName = [IO.Path]::GetFileName($installerPath)
    $checksumPath = "$installerPath.sha256"
    [IO.File]::WriteAllText(
        $checksumPath,
        "$installerHash  $installerName$([Environment]::NewLine)",
        [Text.Encoding]::ASCII)
    $releaseManifest = [ordered]@{
        product = 'Z+'
        version = $version
        platform = 'Windows'
        architecture = 'x64'
        compilerVersion = $version
        zpbuildVersion = $zbuildSemanticVersion
        zpmVersion = $zpmSemanticVersion
        installer = [ordered]@{
            filename = $installerName
            sha256 = $installerHash
        }
    }
    $manifestName = "ZPlus-$version-Windows-x64-release-manifest.json"
    $manifestPath = Join-Path $resolvedOutputDirectory $manifestName
    $manifestJson = $releaseManifest | ConvertTo-Json -Depth 5
    [IO.File]::WriteAllText(
        $manifestPath,
        $manifestJson + [Environment]::NewLine,
        [Text.UTF8Encoding]::new($false))
    Write-Host "Built Z+ $version installer with ZPBuild $zbuildVersion and $zpmVersion from '$binaryDirectory': $installerPath"
    Write-Host "SHA-256: $installerHash"
    Write-Host "Release manifest: $manifestPath"
}
catch {
    [Console]::Error.WriteLine("Installer build failed: $($_.Exception.Message)")
    exit 1
}
finally {
    if (Test-Path -LiteralPath $temporaryDirectory) {
        Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force
    }
}
