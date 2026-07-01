<#
.SYNOPSIS
  Self-contained Windows build + packaging for nav. Produces a static (no
  MinGW/MSYS DLLs required) nav.exe and an NSIS installer + portable ZIP.

.DESCRIPTION
  Acquires its own toolchain (WinLibs MinGW-w64, NSIS, Ninja) by download so it
  runs identically on a clean GitHub `windows-latest` runner and on a local
  Windows box — no choco/MSYS2/preinstall assumptions. CMake is reused from PATH
  if present (it is on GitHub runners), else downloaded. Tools are cached under
  -ToolsDir and skipped when already present, so local re-runs are fast.

  Exit code is non-zero on any failure so CI fails loudly.

.PARAMETER SourceDir
  Repository root (the checkout). Defaults to the current directory.

.PARAMETER ToolsDir
  Where downloaded toolchains live. Defaults to <temp>\navtools.

.PARAMETER BuildDir
  CMake build tree. Defaults to <SourceDir>\build.
#>
[CmdletBinding()]
param(
    [string]$SourceDir = (Get-Location).Path,
    [string]$ToolsDir  = (Join-Path $env:TEMP 'navtools'),
    [string]$BuildDir  = ''
)

$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'
if ([string]::IsNullOrWhiteSpace($BuildDir)) { $BuildDir = Join-Path $SourceDir 'build' }
New-Item -ItemType Directory -Force $ToolsDir | Out-Null

function Info($m) { Write-Host "==> $m" }

# Resumable, retrying download. curl.exe ships in-box on Windows 10+/Server.
function Get-File($url, $out) {
    Info "download $url"
    & curl.exe -L --fail --retry 30 --retry-all-errors --retry-delay 3 `
        --speed-limit 5000 --speed-time 30 -C - -o $out $url
    if ($LASTEXITCODE -ne 0) { throw "download failed ($LASTEXITCODE): $url" }
}

function Latest-Asset($repo, $pattern) {
    # Authenticate the API call when a token is available (CI) to dodge the low
    # unauthenticated rate limit on shared runner IPs; works unauthenticated too.
    $headers = @{ 'User-Agent' = 'nav-ci' }
    if ($env:GITHUB_TOKEN) { $headers['Authorization'] = "Bearer $env:GITHUB_TOKEN" }
    $rel = Invoke-RestMethod "https://api.github.com/repos/$repo/releases/latest" -Headers $headers
    $a = $rel.assets | Where-Object { $_.name -match $pattern } | Select-Object -First 1
    if (-not $a) { throw "no asset matching /$pattern/ in $repo latest release" }
    return $a
}

# --- 1) MinGW-w64 (WinLibs UCRT, posix threads, SEH) -------------------------
$mingwBin = Join-Path $ToolsDir 'mingw64\bin'
if (-not (Test-Path (Join-Path $mingwBin 'g++.exe'))) {
    $a = Latest-Asset 'brechtsanders/winlibs_mingw' '^winlibs-x86_64-posix-seh-(?!.*llvm).*ucrt.*\.zip$'
    $zip = Join-Path $ToolsDir 'winlibs.zip'
    Get-File $a.browser_download_url $zip
    Info "extract $($a.name)"
    Expand-Archive $zip $ToolsDir -Force      # unpacks to <ToolsDir>\mingw64
    Remove-Item $zip -Force
}
if (-not (Test-Path (Join-Path $mingwBin 'g++.exe'))) { throw 'MinGW g++ not found after install' }

# --- 2) NSIS (portable) ------------------------------------------------------
$makensis = Join-Path $ToolsDir 'nsis\makensis.exe'
if (-not (Test-Path $makensis)) {
    $zip = Join-Path $ToolsDir 'nsis.zip'
    Get-File 'https://downloads.sourceforge.net/project/nsis/NSIS%203/3.10/nsis-3.10.zip' $zip
    Expand-Archive $zip $ToolsDir -Force
    if (Test-Path (Join-Path $ToolsDir 'nsis-3.10')) {
        Rename-Item (Join-Path $ToolsDir 'nsis-3.10') (Join-Path $ToolsDir 'nsis')
    }
    Remove-Item $zip -Force
}
if (-not (Test-Path $makensis)) { throw 'makensis not found after install' }

# --- 3) Ninja ----------------------------------------------------------------
$ninjaDir = Join-Path $ToolsDir 'ninja'
if (-not (Test-Path (Join-Path $ninjaDir 'ninja.exe'))) {
    $a = Latest-Asset 'ninja-build/ninja' '^ninja-win\.zip$'
    $zip = Join-Path $ToolsDir 'ninja.zip'
    Get-File $a.browser_download_url $zip
    Expand-Archive $zip $ninjaDir -Force
    Remove-Item $zip -Force
}

# --- 4) CMake: reuse PATH / nav's provisioned copy, else download ------------
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $cmake = (Get-ChildItem "$env:USERPROFILE\.nav\toolchains\cmake" -Recurse -Filter cmake.exe `
              -ErrorAction SilentlyContinue | Select-Object -First 1).FullName
}
if (-not $cmake) {
    $zip = Join-Path $ToolsDir 'cmake.zip'
    Get-File 'https://github.com/Kitware/CMake/releases/download/v3.30.5/cmake-3.30.5-windows-x86_64.zip' $zip
    Expand-Archive $zip $ToolsDir -Force
    Remove-Item $zip -Force
    $cmake = (Get-ChildItem "$ToolsDir\cmake-3.30.5-windows-x86_64" -Recurse -Filter cmake.exe |
              Select-Object -First 1).FullName
}
if (-not $cmake) { throw 'cmake not found' }
$cpack = Join-Path (Split-Path $cmake) 'cpack.exe'

# --- 4b) Git: FetchContent clones tomlplusplus/CLI11/json via git, so it must
# be on PATH. GitHub runners ship it; a bare box may not — reuse PATH / nav's
# provisioned copy, else download MinGit.
$gitExe = (Get-Command git -ErrorAction SilentlyContinue).Source
if (-not $gitExe) {
    $gitExe = (Get-ChildItem "$env:USERPROFILE\.nav\toolchains\git" -Recurse -Filter git.exe `
               -ErrorAction SilentlyContinue | Select-Object -First 1).FullName
}
if (-not $gitExe) {
    $zip = Join-Path $ToolsDir 'mingit.zip'
    Get-File 'https://github.com/git-for-windows/git/releases/download/v2.47.1.windows.1/MinGit-2.47.1-64-bit.zip' $zip
    Expand-Archive $zip (Join-Path $ToolsDir 'git') -Force
    Remove-Item $zip -Force
    $gitExe = (Get-ChildItem (Join-Path $ToolsDir 'git') -Recurse -Filter git.exe |
               Select-Object -First 1).FullName
}
if (-not $gitExe) { throw 'git not found' }

# Prepend our toolchain so cmake picks up gcc/g++/ninja/makensis/git.
$env:PATH = "$mingwBin;$ninjaDir;$(Split-Path $makensis);$(Split-Path $cmake);$(Split-Path $gitExe);$env:PATH"
Info ("git      = $gitExe")
Info ("g++      = " + ((& (Join-Path $mingwBin 'g++.exe') --version)[0]))
Info ("cmake    = $cmake")
Info ("makensis = " + (& $makensis '/VERSION'))

# --- 5) Configure + build (MinGW, Ninja, Release, static) --------------------
if (Test-Path $BuildDir) { Remove-Item -Recurse -Force $BuildDir }
Info 'configure'
& $cmake -S $SourceDir -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_CXX_COMPILER=g++
if ($LASTEXITCODE -ne 0) { throw "configure failed ($LASTEXITCODE)" }
Info 'build'
& $cmake --build $BuildDir --config Release -j
if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }
if (-not (Test-Path (Join-Path $BuildDir 'nav.exe'))) { throw 'nav.exe missing after build' }

# --- 6) Package: NSIS installer + portable ZIP -------------------------------
Info 'cpack (NSIS;ZIP)'
Push-Location $BuildDir
try {
    & $cpack -G 'NSIS;ZIP'
    if ($LASTEXITCODE -ne 0) { throw "cpack failed ($LASTEXITCODE)" }
} finally { Pop-Location }

# --- 7) Report artifacts -----------------------------------------------------
$made = @()
foreach ($pat in 'nav-*.exe', 'nav-*.zip') {
    Get-ChildItem $BuildDir -Filter $pat -ErrorAction SilentlyContinue |
        Where-Object { $_.Length -gt 100000 } | ForEach-Object {
            $made += $_.FullName
            Info ("ARTIFACT " + $_.Name + " (" + [math]::Round($_.Length / 1MB, 2) + " MB)")
        }
}
if (-not $made) { throw 'no installer/zip artifacts produced' }
Info 'DONE'
