@echo off
setlocal
set "ZPLUS_DIR=%~dp0"

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference = 'Stop'; try { $target = [System.IO.Path]::GetFullPath($env:ZPLUS_DIR).TrimEnd('\'); $userPath = [Environment]::GetEnvironmentVariable('Path', 'User'); $entries = @($userPath -split ';' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }); $found = $false; foreach ($entry in $entries) { $candidate = [Environment]::ExpandEnvironmentVariables($entry.Trim()).TrimEnd('\'); try { $candidate = [System.IO.Path]::GetFullPath($candidate).TrimEnd('\') } catch { Write-Warning ('Could not normalize existing PATH entry: ' + $entry) }; if ([string]::Equals($candidate, $target, [StringComparison]::OrdinalIgnoreCase)) { $found = $true; break } }; if (-not $found) { $updated = if ([string]::IsNullOrWhiteSpace($userPath)) { $target } else { $userPath.TrimEnd(';') + ';' + $target }; [Environment]::SetEnvironmentVariable('Path', $updated, 'User'); Write-Output 'Added Z+ to the current user PATH.' } else { Write-Output 'Z+ is already present in the current user PATH.' }; exit 0 } catch { Write-Error $_; exit 1 }"
if errorlevel 1 (
    echo Failed to update the current user's PATH.
    exit /b 1
)

echo PATH setup completed. Open a new terminal before using zp or zbuild.
exit /b 0
