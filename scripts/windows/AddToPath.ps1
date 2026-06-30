# Adds (or removes) nav's bin directory to the system (all-users) PATH.
# Invoked by the NSIS installer/uninstaller. The bin dir is resolved relative to
# this script's own location ($INSTDIR\bin), so no path needs to be passed in —
# keeping the installer's invocation free of fragile quoting.
#
# Idempotent: any existing entry is stripped first, so install/reinstall never
# duplicates it. Writing to the Machine scope broadcasts WM_SETTINGCHANGE, so new
# terminals pick up the change without a reboot.
param([switch]$Remove)

$ErrorActionPreference = 'Stop'
$bin = Join-Path $PSScriptRoot 'bin'

$current = [Environment]::GetEnvironmentVariable('Path', 'Machine')
$parts = @()
if ($current) {
    $parts = $current -split ';' | Where-Object { $_ -ne '' -and $_ -ne $bin }
}
if (-not $Remove) {
    $parts += $bin
}
[Environment]::SetEnvironmentVariable('Path', ($parts -join ';'), 'Machine')
