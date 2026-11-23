param(
    [Parameter(Mandatory = $true)]
    [string]$ReleaseDirectory
)

$ErrorActionPreference = 'Stop'
$release = (Resolve-Path $ReleaseDirectory).Path
$setup = Join-Path $release 'ZPlus-1.3.5-Setup.exe'
if (-not (Test-Path $setup -PathType Leaf)) {
    throw "Setup executable not found: $setup"
}

$testBase = Join-Path $env:TEMP "zplus-installer-tests-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $testBase | Out-Null
$firstInstall = Join-Path $testBase 'custom installation with spaces'
$secondInstall = Join-Path $testBase 'reinstallation'
$tamperedPackage = Join-Path $testBase 'tampered-package'
$testLog = Join-Path $testBase 'installer-test.log'
$startMenu = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Z+'
$environmentKeyPath = 'Environment'
$uninstallKeyPath = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\ZPlus'
$classesPath = 'Software\Classes'
$progIdPath = 'Software\Classes\ZPlus.Source'
$zpAssociationPath = 'Software\Classes\.zp'
$registry = [Microsoft.Win32.Registry]::CurrentUser
$environmentKey = $registry.OpenSubKey($environmentKeyPath, $true)
$originalPath = if ($environmentKey) {
    $environmentKey.GetValue('Path', $null,
        [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
} else { $null }
$originalPathKind = if ($environmentKey -and
    $environmentKey.GetValueNames() -contains 'Path') {
    $environmentKey.GetValueKind('Path')
} else { [Microsoft.Win32.RegistryValueKind]::ExpandString }
if ($environmentKey) { $environmentKey.Dispose() }

$oldUninstall = $registry.OpenSubKey($uninstallKeyPath)
if ($oldUninstall) {
    $oldLocation = $oldUninstall.GetValue('InstallLocation')
    $oldUninstall.Dispose()
    if ($oldLocation -and
        -not $oldLocation.StartsWith($testBase, [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $testBase -Recurse -Force
        throw "Refusing to overwrite an existing user's Z+ registration at '$oldLocation'."
    }
}
$oldProg = $registry.OpenSubKey($progIdPath)
if ($oldProg) {
    $existingCommand = $oldProg.OpenSubKey('shell\open\command')
    $existingTarget = if ($existingCommand) { $existingCommand.GetValue('') } else { $null }
    if ($existingCommand) { $existingCommand.Dispose() }
    $oldProg.Dispose()
    if ($existingTarget -and
        -not $existingTarget.Contains($firstInstall, [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $testBase -Recurse -Force
        throw "Refusing to replace a pre-existing Z+ source association: '$existingTarget'."
    }
}
$existingShortcutNames = @('Z+ Compiler.lnk', 'Z+ Syntax Reference.lnk', 'Uninstall Z+.lnk')
foreach ($shortcutName in $existingShortcutNames) {
    if (Test-Path (Join-Path $startMenu $shortcutName)) {
        Remove-Item -LiteralPath $testBase -Recurse -Force
        throw "Refusing to overwrite an existing Start Menu shortcut: $(Join-Path $startMenu $shortcutName)"
    }
}

$zpKey = $registry.OpenSubKey($zpAssociationPath)
$zpKeyExisted = $zpKey -ne $null
$originalZpDefault = if ($zpKey) {
    $zpKey.GetValue('', $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
} else { $null }
$originalZpKind = if ($zpKey -and $zpKey.GetValueNames() -contains '') {
    $zpKey.GetValueKind('')
} else { [Microsoft.Win32.RegistryValueKind]::String }
if ($zpKey) { $zpKey.Dispose() }
$classes = $registry.CreateSubKey($classesPath)
$zp = $classes.CreateSubKey('.zp')
$zp.SetValue('', 'ZPlusInstallerTest.Previous', [Microsoft.Win32.RegistryValueKind]::String)
$zp.Dispose()
$classes.Dispose()

function Invoke-Setup {
    param(
        [string[]]$Arguments,
        [string]$Log,
        [string]$Executable = $setup,
        [switch]$AllowFailure
    )
    if (Test-Path $Log) { Remove-Item -LiteralPath $Log -Force }
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $Executable
    $startInfo.UseShellExecute = $false
    foreach ($argument in ($Arguments + @('--log-file', $Log))) {
        $startInfo.ArgumentList.Add($argument)
    }
    $process = [Diagnostics.Process]::Start($startInfo)
    if (-not $process) { throw "Could not start setup executable '$Executable'." }
    try {
        $process.WaitForExit()
        $exitCode = $process.ExitCode
    } finally { $process.Dispose() }
    if ($exitCode -ne 0 -and -not $AllowFailure) {
        $details = if (Test-Path $Log) { Get-Content -LiteralPath $Log -Raw } else { '(no installer log)' }
        throw "Setup failed with exit code $exitCode.`n$details"
    }
    if ($AllowFailure) { return $exitCode }
}

function Read-UserPath {
    $key = $registry.OpenSubKey($environmentKeyPath)
    if (-not $key) { return '' }
    try {
        return [string]$key.GetValue('Path', '',
            [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
    } finally { $key.Dispose() }
}

function Assert-UserPathContains([string]$Entry, [bool]$Expected) {
    $entries = (Read-UserPath) -split ';' | Where-Object { $_ }
    $found = $false
    foreach ($candidate in $entries) {
        try {
            if ([IO.Path]::GetFullPath(
                [Environment]::ExpandEnvironmentVariables($candidate.Trim().Trim('"'))) -eq
                [IO.Path]::GetFullPath($Entry)) { $found = $true }
        } catch { }
    }
    if ($found -ne $Expected) {
        throw "User PATH entry expectation failed for '$Entry'; expected $Expected, found $found."
    }
}

try {
    foreach ($case in @(
        @{
            Path = [IO.Path]::GetPathRoot($env:SystemRoot)
            ExpectedMessage = 'drive roots and network paths are not valid destinations'
        },
        @{
            Path = Join-Path $env:SystemRoot 'ZPlusInstaller-Invalid-Location-Test'
            ExpectedMessage = 'Installing inside the Windows directory is not permitted'
        },
        @{
            Path = '\\localhost\C$\ZPlusInstaller-Invalid-Location-Test'
            ExpectedMessage = 'drive roots and network paths are not valid destinations'
        }
    )) {
        $validationExit = Invoke-Setup @('--install-dir', $case.Path, '--quiet') `
            $testLog -AllowFailure
        if ($validationExit -eq 0) {
            throw "Installer accepted an invalid location: '$($case.Path)'."
        }
        $validationLog = Get-Content -LiteralPath $testLog -Raw
        if (-not $validationLog.Contains($case.ExpectedMessage)) {
            throw "Invalid-location diagnostic was missing for '$($case.Path)':`n$validationLog"
        }
    }

    Invoke-Setup @('--install-dir', $firstInstall, '--quiet', '--desktop-shortcut') $testLog
    $compiler = Join-Path $firstInstall 'bin\zp.exe'
    $stdlibFile = Join-Path $firstInstall 'stdlib\zp\math\math.zh'
    $licenseFile = Join-Path $firstInstall 'LICENSE.txt'
    $fileCheckDeadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        $compilerExists = [IO.File]::Exists($compiler)
        $stdlibExists = [IO.File]::Exists($stdlibFile)
        $licenseExists = [IO.File]::Exists($licenseFile)
        if ($compilerExists -and $stdlibExists -and $licenseExists) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $fileCheckDeadline)
    if (-not $compilerExists -or -not $stdlibExists -or -not $licenseExists) {
        $checkDetails = "Fresh installation is missing required files under '$firstInstall'. " +
            "Compiler ($compiler): $compilerExists; standard library ($stdlibFile): $stdlibExists; " +
            "license ($licenseFile): $licenseExists."
        throw $checkDetails
    }
    $version = (& $compiler version | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $version -ne 'Z+ Compiler 1.3.5') {
        throw "Installed compiler returned an invalid version: '$version'."
    }
    Assert-UserPathContains (Join-Path $firstInstall 'bin') $true
    $installedAssociation = $registry.OpenSubKey($zpAssociationPath)
    $associationValue = if ($installedAssociation) { $installedAssociation.GetValue('') } else { $null }
    if ($installedAssociation) { $installedAssociation.Dispose() }
    if ($associationValue -ne 'ZPlus.Source') {
        throw "Expected .zp to map to ZPlus.Source, found '$associationValue'."
    }
    foreach ($shortcut in @('Z+ Compiler.lnk', 'Z+ Syntax Reference.lnk', 'Uninstall Z+.lnk')) {
        if (-not (Test-Path (Join-Path $startMenu $shortcut))) {
            throw "Start Menu shortcut was not created: $shortcut"
        }
    }
    if (-not (Test-Path (Join-Path ([Environment]::GetFolderPath('Desktop')) 'Z+ Compiler.lnk'))) {
        throw 'The requested desktop shortcut was not created.'
    }

    Set-Content -LiteralPath (Join-Path $firstInstall 'my-project.zp') `
        -Value 'start: say("user file") stop' -Encoding utf8
    Invoke-Setup @('--install-dir', $firstInstall, '--quiet') $testLog
    if (-not (Test-Path (Join-Path $firstInstall 'my-project.zp'))) {
        throw 'Upgrade removed an untracked user project.'
    }

    New-Item -ItemType Directory -Path $tamperedPackage | Out-Null
    Copy-Item -LiteralPath $setup -Destination $tamperedPackage
    Copy-Item -LiteralPath (Join-Path $release 'payload-manifest.json') -Destination $tamperedPackage
    Copy-Item -LiteralPath (Join-Path $release 'payload') -Destination $tamperedPackage -Recurse
    $tamperedCompiler = Join-Path $tamperedPackage 'payload\bin\zp.exe'
    $bytes = [IO.File]::ReadAllBytes($tamperedCompiler)
    $bytes[0] = $bytes[0] -bxor 0x01
    [IO.File]::WriteAllBytes($tamperedCompiler, $bytes)
    $tamperedSetup = Join-Path $tamperedPackage 'ZPlus-1.3.5-Setup.exe'
    $tamperedExitCode = Invoke-Setup @('--install-dir', $firstInstall, '--quiet') `
        $testLog -Executable $tamperedSetup -AllowFailure
    if ($tamperedExitCode -eq 0) {
        throw 'Installer accepted a payload file with a corrupted SHA-256 hash.'
    }
    $failedLog = Get-Content -LiteralPath $testLog -Raw
    if (-not $failedLog.Contains('SHA-256 integrity check failed')) {
        throw "Corrupted-payload failure did not report the integrity error:`n$failedLog"
    }
    $version = (& $compiler version | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $version -ne 'Z+ Compiler 1.3.5') {
        throw 'Failed installation recovery damaged the existing compiler.'
    }
    if (Get-ChildItem (Split-Path $firstInstall -Parent) -Force |
        Where-Object Name -Like '.zplus-*') {
        throw 'Failed installation left staging or rollback directories behind.'
    }

    $syntaxPath = Join-Path $firstInstall 'docs\SYNTAX.md'
    Add-Content -LiteralPath $syntaxPath -Value "`nUser customization preserved by uninstaller."
    Invoke-Setup @('--uninstall', '--install-dir', $firstInstall, '--quiet') $testLog
    Assert-UserPathContains (Join-Path $firstInstall 'bin') $false
    $restoredAssociation = $registry.OpenSubKey($zpAssociationPath)
    $restoredValue = if ($restoredAssociation) { $restoredAssociation.GetValue('') } else { $null }
    if ($restoredAssociation) { $restoredAssociation.Dispose() }
    if ($restoredValue -ne 'ZPlusInstallerTest.Previous') {
        throw "Uninstaller did not restore the previous .zp association: '$restoredValue'."
    }
    if (Test-Path $compiler) { throw 'Uninstaller left the unchanged compiler executable behind.' }
    if (-not (Test-Path $syntaxPath) -or
        -not (Test-Path (Join-Path $firstInstall 'my-project.zp'))) {
        throw 'Uninstaller removed modified or untracked user content.'
    }
    if (Test-Path (Join-Path $startMenu 'Z+ Compiler.lnk')) {
        throw 'Uninstaller left a shortcut it created.'
    }
    if (Test-Path (Join-Path ([Environment]::GetFolderPath('Desktop')) 'Z+ Compiler.lnk')) {
        throw 'Uninstaller left the optional desktop shortcut it created.'
    }

    Invoke-Setup @('--install-dir', $secondInstall, '--quiet', '--no-tools',
        '--no-docs', '--no-examples') $testLog
    if (-not (Test-Path (Join-Path $secondInstall 'bin\zp.exe'))) {
        throw 'Reinstallation after uninstall did not create the compiler.'
    }
    if (Test-Path (Join-Path $secondInstall 'docs\SYNTAX.md')) {
        throw 'The installer installed an explicitly deselected documentation component.'
    }
    Invoke-Setup @('--uninstall', '--install-dir', $secondInstall, '--quiet') $testLog
    if (Test-Path (Join-Path $secondInstall 'bin\zp.exe')) {
        throw 'Reinstalled compiler was not removed by the uninstaller.'
    }
    Assert-UserPathContains (Join-Path $secondInstall 'bin') $false

    $finalPath = Read-UserPath
    if ($finalPath -ne [string]$originalPath) {
        throw "Installer/uninstaller changed unrelated user PATH content.`nBefore: $originalPath`nAfter: $finalPath"
    }
    Write-Host 'Installer integration tests passed: fresh, custom path, upgrade, integrity rejection and rollback, PATH/association/shortcuts, safe uninstall, and reinstall.'
}
finally {
    foreach ($testInstall in @($firstInstall, $secondInstall)) {
        if (Test-Path (Join-Path $testInstall '.zplus-installation.json')) {
            try {
                $cleanupCode = Invoke-Setup @('--uninstall', '--install-dir',
                    $testInstall, '--quiet') $testLog -AllowFailure
                if ($cleanupCode -ne 0) {
                    $details = if (Test-Path $testLog) {
                        Get-Content -LiteralPath $testLog -Raw
                    } else { '(no installer log)' }
                    Write-Warning "Test cleanup could not uninstall '$testInstall': $details"
                }
            } catch {
                Write-Warning "Test cleanup failed for '$testInstall': $_"
            }
        }
    }

    $environmentKey = $registry.CreateSubKey($environmentKeyPath)
    if ($null -eq $originalPath) {
        $environmentKey.DeleteValue('Path', $false)
    } else {
        $environmentKey.SetValue('Path', $originalPath, $originalPathKind)
    }
    $environmentKey.Dispose()

    $classes = $registry.CreateSubKey($classesPath)
    $zp = $classes.OpenSubKey('.zp', $true)
    if ($zp) {
        if ($null -eq $originalZpDefault) { $zp.DeleteValue('', $false) }
        else { $zp.SetValue('', $originalZpDefault, $originalZpKind) }
        $removeEmptyZpKey = -not $zpKeyExisted -and
            $zp.GetValueNames().Length -eq 0 -and $zp.GetSubKeyNames().Length -eq 0
        $zp.Dispose()
        if ($removeEmptyZpKey) { $classes.DeleteSubKey('.zp', $false) }
    }
    $classes.Dispose()

    $ownedProg = $registry.OpenSubKey($progIdPath, $true)
    if ($ownedProg) {
        $open = $ownedProg.OpenSubKey('shell\open\command')
        $command = if ($open) { $open.GetValue('') } else { $null }
        if ($open) { $open.Dispose() }
        if ($command -and $command.Contains($firstInstall, [StringComparison]::OrdinalIgnoreCase)) {
            $ownedProg.Dispose()
            $registry.DeleteSubKeyTree($progIdPath, $false)
        } else { $ownedProg.Dispose() }
    }
    Remove-Item -LiteralPath $testBase -Recurse -Force -ErrorAction SilentlyContinue
}
