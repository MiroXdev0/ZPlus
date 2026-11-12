param(
    [switch]$ReplaceExisting
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path $PSScriptRoot).Path
$repositoryRoot = (Resolve-Path (Join-Path $projectRoot '..\..')).Path
$zbuild = Join-Path $repositoryRoot 'build-zbuild\Release\zbuild.exe'
$compilerBuild = Join-Path $repositoryRoot 'build-zbuild\release-staging'
$projectOutput = Join-Path $projectRoot 'dist\release\bin'
$finalDirectory = Join-Path $repositoryRoot 'release\zplus-1.1.2-windows-x64'
$stagingDirectory = Join-Path $repositoryRoot 'release\zplus-1.1.2-windows-x64.staging'
$backupDirectory = Join-Path $repositoryRoot 'release\zplus-1.1.2-windows-x64.previous'

if (-not (Test-Path -LiteralPath $zbuild -PathType Leaf)) {
    throw "ZBuild was not found at '$zbuild'. Build target zbuild first."
}
foreach ($required in @('zp.exe', 'zgraphics_runtime.dll', 'sqlite3.dll', 'zbuild-templates')) {
    if (-not (Test-Path -LiteralPath (Join-Path $compilerBuild $required))) {
        throw "Required compiler distribution file or directory is missing: $required"
    }
    $versionOutput = & (Join-Path $compilerBuild 'zp.exe') version
    if ($LASTEXITCODE -ne 0 -or ($versionOutput -join "`n") -notmatch '1\.1\.2') {
        throw "The compiler build is not Z+ 1.1.2; refusing to package it."
    }
}
$expectedFinal = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot 'release\zplus-1.1.2-windows-x64'))
$expectedStaging = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot 'release\zplus-1.1.2-windows-x64.staging'))
if (-not [string]::Equals([System.IO.Path]::GetFullPath($finalDirectory), $expectedFinal, [System.StringComparison]::OrdinalIgnoreCase) -or
    -not [string]::Equals([System.IO.Path]::GetFullPath($stagingDirectory), $expectedStaging, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to use unexpected release output paths."
}
if ((Test-Path -LiteralPath $stagingDirectory) -or (Test-Path -LiteralPath $backupDirectory)) {
    if (-not $ReplaceExisting) {
        throw "Release staging or backup output already exists; pass -ReplaceExisting to replace only these scoped outputs."
    }
    if (Test-Path -LiteralPath $stagingDirectory) {
        Remove-Item -LiteralPath $stagingDirectory -Recurse -Force
    }
    if (Test-Path -LiteralPath $backupDirectory) {
        throw "Refusing to replace an existing release backup: '$backupDirectory'"
    }
}
if ((Test-Path -LiteralPath $finalDirectory) -and -not $ReplaceExisting) {
    throw "Release output already exists. Pass -ReplaceExisting to replace only this versioned release directory."
}

Push-Location $projectRoot
try {
    & $zbuild build --release
    if ($LASTEXITCODE -ne 0) {
        throw "ZBuild failed with exit code $LASTEXITCODE."
    }
}
finally {
    Pop-Location
}

$setupInput = Join-Path $projectOutput 'app.exe'
if (-not (Test-Path -LiteralPath $setupInput -PathType Leaf)) {
    throw "ZBuild did not produce the installer executable at '$setupInput'."
}
$setupBytes = [System.IO.File]::ReadAllBytes($setupInput)
if ($setupBytes.Length -lt 0x80 -or $setupBytes[0] -ne 0x4D -or $setupBytes[1] -ne 0x5A) {
    throw "ZBuild output is not a valid Windows executable."
}
$peOffset = [BitConverter]::ToInt32($setupBytes, 0x3C)
$subsystemOffset = $peOffset + 24 + 68
if ($peOffset -lt 0x40 -or $subsystemOffset -gt ($setupBytes.Length - 2) -or
    $setupBytes[$peOffset] -ne 0x50 -or $setupBytes[$peOffset + 1] -ne 0x45 -or
    [BitConverter]::ToUInt16($setupBytes, $subsystemOffset) -ne 2) {
    throw "ZBuild output is not a Windows GUI application; refusing to package a console installer."
}
New-Item -ItemType Directory -Path $stagingDirectory | Out-Null
try {
    $payload = Join-Path $stagingDirectory 'payload'
    $payloadBin = Join-Path $payload 'bin'
    $payloadStdlib = Join-Path $payloadBin 'stdlib'
    New-Item -ItemType Directory -Path $payloadBin, $payloadStdlib | Out-Null

    Copy-Item -LiteralPath $setupInput -Destination (Join-Path $stagingDirectory 'setup.exe')
    Copy-Item -LiteralPath (Join-Path $projectOutput 'zgraphics_runtime.dll') -Destination $stagingDirectory
    Copy-Item -LiteralPath (Join-Path $projectOutput 'sqlite3.dll') -Destination $stagingDirectory

    foreach ($binary in @('zp.exe', 'zgraphics_runtime.dll', 'sqlite3.dll', 'zbuild.exe')) {
        Copy-Item -LiteralPath (Join-Path $compilerBuild $binary) -Destination $payloadBin
    }
    Copy-Item -Path (Join-Path $repositoryRoot 'stdlib\*') -Destination $payloadStdlib -Recurse
    Copy-Item -LiteralPath (Join-Path $compilerBuild 'zbuild-templates') -Destination $payloadBin -Recurse

    $docs = Join-Path $payload 'docs'
    $examples = Join-Path $payload 'examples'
    New-Item -ItemType Directory -Path $docs, $examples | Out-Null
    Copy-Item -Path (Join-Path $repositoryRoot 'docs\*') -Destination $docs -Recurse
    Copy-Item -Path (Join-Path $repositoryRoot 'examples\*') -Destination $examples -Recurse
    Copy-Item -LiteralPath (Join-Path $repositoryRoot 'SYNTAX.md') -Destination $docs
    Copy-Item -LiteralPath (Join-Path $repositoryRoot 'LICENSE.txt') -Destination $payload
    Copy-Item -LiteralPath (Join-Path $repositoryRoot 'installer\windows\THIRD-PARTY-NOTICES.txt') -Destination $payload
    Copy-Item -LiteralPath (Join-Path $repositoryRoot 'release\windows\ZPlusLogo.png') -Destination $docs
    Copy-Item -LiteralPath (Join-Path $projectRoot 'README.md') -Destination (Join-Path $stagingDirectory 'INSTALLER-README.md')
    Copy-Item -LiteralPath (Join-Path $repositoryRoot 'LICENSE.txt') -Destination $stagingDirectory
    Copy-Item -LiteralPath (Join-Path $repositoryRoot 'installer\windows\THIRD-PARTY-NOTICES.txt') -Destination $stagingDirectory

    $manifestPath = Join-Path $stagingDirectory 'payload.sha256'
    $records = [System.Collections.Generic.List[string]]::new()
    foreach ($file in Get-ChildItem -LiteralPath $payload -File -Recurse) {
        $relative = $file.FullName.Substring($payload.Length + 1)
        if ($relative.Contains("`t") -or $relative.Contains("`r") -or $relative.Contains("`n")) {
            throw "Unsupported control character in payload file name: '$relative'"
        }
        $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        $records.Add("$relative`t$hash")
    }
    [System.IO.File]::WriteAllLines(
        $manifestPath,
        $records,
        [System.Text.UTF8Encoding]::new($false)
    )
    $verificationWork = Join-Path $stagingDirectory 'verification-work'
    & cmake `
        "-DZP=$(Join-Path $payloadBin 'zp.exe')" `
        "-DZBUILD=$(Join-Path $payloadBin 'zbuild.exe')" `
        "-DSOURCE_FILE=$(Join-Path $projectRoot 'tests\release_verification.zp')" `
        "-DINSTALL_SOURCE=$(Join-Path $projectRoot 'tests\release_install_workflow.zp')" `
        "-DPAYLOAD_DIRECTORY=$payload" `
        "-DMANIFEST_FILE=$manifestPath" `
        "-DWORK_DIR=$verificationWork" `
        "-DRUNTIME_FILE=$(Join-Path $payloadBin 'zgraphics_runtime.dll')" `
        "-DSQLITE_FILE=$(Join-Path $payloadBin 'sqlite3.dll')" `
        -P (Join-Path $projectRoot 'tests\verify_release.cmake')
    if ($LASTEXITCODE -ne 0) {
        throw "Packaged compiler verification failed with exit code $LASTEXITCODE."
    }
    $outerChecksums = [System.Collections.Generic.List[string]]::new()
    foreach ($name in @('setup.exe', 'zgraphics_runtime.dll', 'sqlite3.dll', 'payload.sha256', 'INSTALLER-README.md', 'LICENSE.txt', 'THIRD-PARTY-NOTICES.txt')) {
        $file = Join-Path $stagingDirectory $name
        $hash = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash
        $outerChecksums.Add("$hash  $name")
    }
    [System.IO.File]::WriteAllLines(
        (Join-Path $stagingDirectory 'SHA256SUMS.txt'),
        $outerChecksums,
        [System.Text.UTF8Encoding]::new($false)
    )
    if (Test-Path -LiteralPath $finalDirectory) {
        Move-Item -LiteralPath $finalDirectory -Destination $backupDirectory
    }
    try {
        Move-Item -LiteralPath $stagingDirectory -Destination $finalDirectory
    }
    catch {
        if (Test-Path -LiteralPath $backupDirectory) {
            Move-Item -LiteralPath $backupDirectory -Destination $finalDirectory
        }
        throw
    }
    if (Test-Path -LiteralPath $backupDirectory) {
        Remove-Item -LiteralPath $backupDirectory -Recurse -Force
    }
}
catch {
    throw "Z+ installer release assembly failed: $($_.Exception.Message)"
}

Write-Host "Built Z+ 1.1.2 Windows installer: $(Join-Path $finalDirectory 'setup.exe')"
