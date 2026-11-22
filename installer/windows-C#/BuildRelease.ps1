param(
    [switch]$SkipCompilerBuild,
    [switch]$SkipTests
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$buildDir = Join-Path $root 'build-zbuild'
$cachePath = Join-Path $buildDir 'CMakeCache.txt'
$releaseDir = Join-Path $root 'release\windows'
$releaseZip = Join-Path $root 'release\ZPlus-1.3.5-Windows-x64.zip'
$payloadDir = Join-Path $releaseDir 'payload'

function Invoke-Checked {
    param([string]$Executable, [string[]]$Arguments)
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "'$Executable $($Arguments -join ' ')' failed with exit code $LASTEXITCODE."
    }
}

function Copy-NonemptyTree {
    param([string]$SourceDirectory, [string]$DestinationDirectory)

    $sourceRoot = (Resolve-Path -LiteralPath $SourceDirectory).Path
    New-Item -ItemType Directory -Path $DestinationDirectory -Force | Out-Null
    foreach ($file in Get-ChildItem -LiteralPath $sourceRoot -File -Recurse -Force) {
        if ($file.Length -eq 0) {
            continue
        }
        $relative = [IO.Path]::GetRelativePath($sourceRoot, $file.FullName)
        $destination = Join-Path $DestinationDirectory $relative
        New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $destination
    }
}

if (-not $SkipCompilerBuild) {
    Invoke-Checked 'cmake' @('--build', $buildDir, '--config', 'Release', '--target', 'zp', 'zbuild', '--', '/m')
}
if (-not (Test-Path $cachePath)) {
    throw "CMake cache not found at '$cachePath'. Configure the project first."
}
$releaseLine = Select-String -Path $cachePath -Pattern '^ZPLUS_RELEASE_BIN_DIR:PATH=(.+)$' |
    Select-Object -First 1
if (-not $releaseLine) {
    throw 'CMake cache does not define ZPLUS_RELEASE_BIN_DIR.'
}
$binaryDir = $releaseLine.Matches[0].Groups[1].Value.Trim()
if (-not (Test-Path (Join-Path $binaryDir 'zp.exe')) -or
    -not (Test-Path (Join-Path $binaryDir 'zbuild.exe'))) {
    throw "Release compiler or ZBuild output is missing from '$binaryDir'."
}
$versionOutput = (& (Join-Path $binaryDir 'zp.exe') version | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $versionOutput -ne 'Z+ Compiler 1.3.5') {
    throw "Release compiler version verification failed: '$versionOutput'."
}

if (-not $SkipTests) {
    Invoke-Checked 'ctest' @('--test-dir', $buildDir, '-C', 'Release',
        '--output-on-failure', '-R',
        '^(native_executable_integration|module_system_integration|runtime_arrays_and_diagnostics|runtime_error_handling|parser_semantics_parity|standard_library_integration)$')
}

if (Test-Path $releaseDir) {
    Remove-Item -LiteralPath $releaseDir -Recurse -Force
}
if (Test-Path $releaseZip) {
    Remove-Item -LiteralPath $releaseZip -Force
}
New-Item -ItemType Directory -Path $payloadDir -Force | Out-Null
$binPayload = Join-Path $payloadDir 'bin'
New-Item -ItemType Directory -Path $binPayload -Force | Out-Null

$requiredBinaries = @(
    'zp.exe',
    'zgraphics_runtime.dll',
    'zaudio_runtime.dll',
    'zphysics_runtime.dll',
    'sqlite3.dll',
    'zbuild.exe'
)
foreach ($file in $requiredBinaries) {
    $source = Join-Path $binaryDir $file
    if (-not (Test-Path $source -PathType Leaf)) {
        throw "Required release component was not built: $source"
    }
}
$coreBinaries = @(
    'zp.exe',
    'zgraphics_runtime.dll',
    'zaudio_runtime.dll',
    'zphysics_runtime.dll',
    'sqlite3.dll'
)
foreach ($file in $coreBinaries) {
    Copy-Item -LiteralPath (Join-Path $binaryDir $file) -Destination $binPayload
}
Copy-Item -LiteralPath (Join-Path $binaryDir 'zbuild.exe') -Destination $binPayload
$templates = Join-Path $binaryDir 'zbuild-templates'
if (-not (Test-Path $templates -PathType Container)) {
    throw "ZBuild templates were not built: $templates"
}
Copy-Item -LiteralPath $templates -Destination $binPayload -Recurse

$stdlibDestination = Join-Path $payloadDir 'stdlib'
Copy-NonemptyTree -SourceDirectory (Join-Path $root 'stdlib') -DestinationDirectory $stdlibDestination

$docsDestination = Join-Path $payloadDir 'docs'
New-Item -ItemType Directory -Path $docsDestination -Force | Out-Null
foreach ($name in @('README.md', 'MODULES.md', 'NATIVE_BACKEND.md')) {
    Copy-Item -LiteralPath (Join-Path $root $name) -Destination $docsDestination
}
Copy-Item -LiteralPath (Join-Path $root 'docs\syntax.md') `
    -Destination (Join-Path $docsDestination 'SYNTAX.md')
Copy-NonemptyTree -SourceDirectory (Join-Path $root 'docs') -DestinationDirectory $docsDestination
Copy-NonemptyTree -SourceDirectory (Join-Path $root 'examples') -DestinationDirectory (Join-Path $payloadDir 'examples')
Copy-Item -LiteralPath (Join-Path $root 'LICENSE.txt') -Destination $payloadDir
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'THIRD-PARTY-NOTICES.txt') -Destination $payloadDir

$noticesDir = Join-Path $payloadDir 'THIRD-PARTY-NOTICES'
New-Item -ItemType Directory -Path $noticesDir -Force | Out-Null
$box2dLicense = Join-Path $buildDir '_deps\box2d-src\LICENSE'
$miniaudioLicense = Join-Path $buildDir '_deps\miniaudio-src\LICENSE'
$sqliteLine = Select-String -Path $cachePath -Pattern '^ZPLUS_SQLITE_RUNTIME_FILE:FILEPATH=(.+)$' |
    Select-Object -First 1
if (-not $sqliteLine) {
    throw 'SQLite runtime source path is not recorded in CMakeCache.txt.'
}
$sqliteSource = $sqliteLine.Matches[0].Groups[1].Value.Trim()
$sqlitePrefix = Split-Path (Split-Path $sqliteSource -Parent) -Parent
$sqliteLicense = Join-Path $sqlitePrefix 'share\licenses\sqlite3\LICENSE'
foreach ($notice in @($box2dLicense, $miniaudioLicense, $sqliteLicense)) {
    if (-not (Test-Path $notice -PathType Leaf)) {
        throw "Third-party license notice is missing: $notice"
    }
}
Copy-Item -LiteralPath $box2dLicense -Destination (Join-Path $noticesDir 'Box2D-LICENSE.txt')
Copy-Item -LiteralPath $miniaudioLicense -Destination (Join-Path $noticesDir 'miniaudio-LICENSE.txt')
Copy-Item -LiteralPath $sqliteLicense -Destination (Join-Path $noticesDir 'SQLite-LICENSE.txt')
@'
stb_vorbis v1.22 is the public-domain Ogg Vorbis decoder by Sean Barrett.
The source file bundled in miniaudio's extras directory identifies this work
as public domain and contains its license notice.
'@ | Set-Content -LiteralPath (Join-Path $noticesDir 'stb_vorbis-NOTICE.txt') -Encoding utf8

$logoSource = Join-Path $root 'logo\Z+ Emblem - for dark theme.png'
if (-not (Test-Path $logoSource -PathType Leaf)) {
    throw "The official dark-surface Z+ logo is missing: $logoSource"
}
Copy-Item -LiteralPath $logoSource -Destination (Join-Path $releaseDir 'ZPlusLogo.png')
Copy-Item -LiteralPath (Join-Path $root 'LICENSE.txt') -Destination $releaseDir
Copy-Item -LiteralPath (Join-Path $root 'docs\INSTALLER.md') -Destination $releaseDir
Copy-Item -LiteralPath (Join-Path $root 'installer\windows\THIRD-PARTY-NOTICES.txt') `
    -Destination (Join-Path $releaseDir 'THIRD-PARTY-NOTICES.txt')

$dotnetRoot = Join-Path $env:ProgramFiles 'dotnet'
$dotnetLicense = Join-Path $dotnetRoot 'LICENSE.txt'
$dotnetNotices = Join-Path $dotnetRoot 'ThirdPartyNotices.txt'
foreach ($notice in @($dotnetLicense, $dotnetNotices)) {
    if (-not (Test-Path $notice -PathType Leaf)) {
        throw "Self-contained setup runtime notice is missing: $notice"
    }
}
Copy-Item -LiteralPath $dotnetLicense -Destination (Join-Path $releaseDir 'LICENSE-DOTNET.txt')
Copy-Item -LiteralPath $dotnetNotices `
    -Destination (Join-Path $releaseDir 'THIRD-PARTY-NOTICES-DOTNET.txt')
Copy-Item -LiteralPath (Join-Path $root 'docs\INSTALLER.md') `
    -Destination (Join-Path $releaseDir 'INSTALLATION.md')

$components = @(
    @{ id = 'core'; name = 'Z+ compiler and standard library'; description = 'Compiler, Windows runtimes, standard modules, and license notices'; required = $true },
    @{ id = 'tools'; name = 'ZBuild project tool'; description = 'ZBuild command-line tool and built-in templates'; required = $false },
    @{ id = 'documentation'; name = 'Documentation'; description = 'Syntax, modules, backend, and tool documentation'; required = $false },
    @{ id = 'examples'; name = 'Example projects'; description = 'Example Z+ programs'; required = $false }
)
$files = [Collections.Generic.List[object]]::new()
foreach ($file in Get-ChildItem $payloadDir -Recurse -File | Sort-Object FullName) {
    $relative = [IO.Path]::GetRelativePath($payloadDir, $file.FullName).Replace('\', '/')
    $component = if ($relative -eq 'bin/zbuild.exe' -or
        $relative.StartsWith('bin/zbuild-templates/', [StringComparison]::OrdinalIgnoreCase)) {
        'tools'
    } elseif ($relative.StartsWith('docs/', [StringComparison]::OrdinalIgnoreCase)) {
        'documentation'
    } elseif ($relative.StartsWith('examples/', [StringComparison]::OrdinalIgnoreCase)) {
        'examples'
    } else {
        'core'
    }
    $files.Add(@{
        path = $relative
        component = $component
        sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        length = $file.Length
    })
}
$manifest = [ordered]@{
    product = 'Z+'
    version = '1.3.5'
    files = $files
    components = $components
}
$manifestPath = Join-Path $releaseDir 'payload-manifest.json'
$manifestJson = $manifest | ConvertTo-Json -Depth 6
[IO.File]::WriteAllText($manifestPath, $manifestJson + [Environment]::NewLine,
    [Text.UTF8Encoding]::new($false))
$manifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash

Invoke-Checked 'dotnet' @(
    'publish', (Join-Path $PSScriptRoot 'ZPlusInstaller.csproj'),
    '--configuration', 'Release', '--runtime', 'win-x64', '--self-contained', 'true',
    "-p:PackageManifestSha256=$manifestHash", '-p:PublishSingleFile=true',
    '-p:IncludeNativeLibrariesForSelfExtract=true', '-p:EnableCompressionInSingleFile=true',
    '--output', $releaseDir
)
$setup = Join-Path $releaseDir 'ZPlus-1.3.5-Setup.exe'
if (-not (Test-Path $setup -PathType Leaf)) {
    throw "The published setup executable was not produced: $setup"
}

$sumLines = [Collections.Generic.List[string]]::new()
foreach ($file in Get-ChildItem $releaseDir -Recurse -File |
             Where-Object Name -ne 'SHA256SUMS.txt' | Sort-Object FullName) {
    $relative = [IO.Path]::GetRelativePath($releaseDir, $file.FullName).Replace('\', '/')
    $sumLines.Add("$((Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash)  $relative")
}
$sumLines | Set-Content -LiteralPath (Join-Path $releaseDir 'SHA256SUMS.txt') -Encoding ascii

if (-not $SkipTests) {
    & (Join-Path $PSScriptRoot 'tests\Run-InstallerTests.ps1') -ReleaseDirectory $releaseDir
}

$temporaryZip = Join-Path $env:TEMP "ZPlus-1.3.5-Windows-x64-$([guid]::NewGuid().ToString('N')).zip"
[IO.Compression.ZipFile]::CreateFromDirectory(
    $releaseDir, $temporaryZip, [IO.Compression.CompressionLevel]::Optimal, $false)
New-Item -ItemType Directory -Path (Split-Path $releaseZip -Parent) -Force | Out-Null
Move-Item -LiteralPath $temporaryZip -Destination $releaseZip
Write-Host "Release setup: $setup"
Write-Host "Release archive: $releaseZip"
Write-Host "Payload manifest SHA-256: $manifestHash"
