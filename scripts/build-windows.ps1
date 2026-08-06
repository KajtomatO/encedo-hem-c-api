<#
.SYNOPSIS
    Configure and build encedo-hem-c-api on Windows through the MSYS2 /
    MinGW-w64 toolchain, from an ordinary PowerShell prompt.

.DESCRIPTION
    The project's dependencies (libcurl, wolfSSL, cmocka, ...) are installed
    inside MSYS2 by scripts\install-deps-windows.ps1. A plain
    `cmake -B build` run from PowerShell often picks up an unrelated cmake/gcc
    on PATH (e.g. Strawberry Perl's bundled toolchain), which has no libcurl
    dev files, and fails with:

        Could NOT find CURL (missing: CURL_LIBRARY CURL_INCLUDE_DIR)

    This helper sidesteps that by running cmake *inside* the MinGW64
    environment (the "MSYS2 MINGW64" shell), where libcurl/wolfSSL and their
    pkg-config/CMake package files live. It configures and builds in one step.

    CMake bakes the C compiler into CMakeCache.txt and refuses an in-place
    compiler swap. So if the build directory was previously configured with a
    compiler *outside* the chosen MinGW root (the classic Strawberry-gcc
    poisoning), this script wipes and reconfigures it -- the same self-heal the
    Linux `./dev` wrapper does.

.PARAMETER BuildDir
    Build directory (relative to the repo root, or absolute). Default: build.

.PARAMETER BuildType
    CMAKE_BUILD_TYPE. Default: Debug (matches the project default).

.PARAMETER MsysRoot
    MSYS2 install root. Default: C:\msys64.

.PARAMETER Env
    MinGW environment: mingw64 (x86_64, default) or ucrt64. Must match what
    install-deps-windows.ps1 populated.

.PARAMETER Clean
    Remove the build directory before configuring (force a fresh cache).

.PARAMETER CMakeArgs
    Any remaining arguments are passed through to the configure step, e.g.
    -DEHEM_SANITIZE=ON or -DBUILD_TESTING=OFF.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1

.EXAMPLE
    # Release build, sanitizers off, into a separate directory:
    scripts\build-windows.ps1 -BuildType Release -BuildDir build-rel

.NOTES
    Run scripts\install-deps-windows.ps1 first. Then test with
    scripts\test-windows.ps1.
#>
[CmdletBinding()]
param(
    [string]$BuildDir = 'build',
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$BuildType = 'Debug',
    [string]$MsysRoot = 'C:\msys64',
    [ValidateSet('mingw64', 'ucrt64')]
    [string]$Env = 'mingw64',
    [switch]$Clean,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$CMakeArgs = @()
)

$ErrorActionPreference = 'Stop'

function Write-Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }
function Write-Warn($msg) { Write-Host "warning: $msg" -ForegroundColor Yellow }
function Die($msg)        { Write-Host "error: $msg" -ForegroundColor Red; exit 1 }

# Run a bash script string in the MSYS2 login shell. We write it to a temp
# file and invoke `bash -l <file>` rather than piping over stdin: Windows
# PowerShell 5.1 pipes native-command stdin through [Console]::OutputEncoding,
# which here is UTF-8 *with BOM*, and the leading EF BB BF corrupts the first
# line ("$'\357\273\277export': command not found"). The file is written as
# UTF-8 without a BOM and with LF endings so bash sees clean bytes.
function Invoke-BashScript($bashExe, $body) {
    $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("ehem-" + [System.IO.Path]::GetRandomFileName() + ".sh")
    try {
        [System.IO.File]::WriteAllText($tmp, ($body -replace "`r`n", "`n"),
            (New-Object System.Text.UTF8Encoding $false))
        & $bashExe -l $tmp
    } finally {
        Remove-Item $tmp -Force -ErrorAction SilentlyContinue
    }
}

# Repo root is the parent of this scripts\ directory.
$repoRoot = Split-Path -Parent $PSScriptRoot

# --- locate the MSYS2 bash -------------------------------------------------
$bash = Join-Path $MsysRoot 'usr\bin\bash.exe'
if (-not (Test-Path $bash)) {
    Die @"
MSYS2 bash not found at $bash.
Install the toolchain first:
    powershell -ExecutionPolicy Bypass -File scripts\install-deps-windows.ps1
or pass -MsysRoot <path> if MSYS2 lives elsewhere.
"@
}

# Absolute build path (for the poisoned-cache check below).
$buildAbs = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } `
            else { Join-Path $repoRoot $BuildDir }

# --- self-heal a cache configured with the wrong compiler ------------------
# CMake refuses to swap the compiler in place; a build/ configured by the
# Strawberry (or any non-MinGW) gcc must be wiped, not reused.
$cache = Join-Path $buildAbs 'CMakeCache.txt'
if ($Clean -and (Test-Path $buildAbs)) {
    Write-Step "Clean: removing $buildAbs"
    Remove-Item -Recurse -Force $buildAbs
} elseif (Test-Path $cache) {
    # e.g. CMAKE_C_COMPILER:FILEPATH=C:/msys64/mingw64/bin/gcc.exe
    $line = Select-String -Path $cache -Pattern '^CMAKE_C_COMPILER:' -SimpleMatch:$false |
            Select-Object -First 1
    if ($line) {
        $compiler = ($line.Line -split '=', 2)[1]
        # Normalize slashes/case; the good compiler lives under <MsysRoot>\<Env>.
        $wantRoot = (Join-Path $MsysRoot $Env).Replace('\', '/').ToLower()
        $haveComp = $compiler.Replace('\', '/').ToLower()
        if (-not $haveComp.StartsWith($wantRoot)) {
            Write-Warn "$BuildDir was configured with '$compiler' (not under $MsysRoot\$Env); wiping and reconfiguring for MinGW $Env."
            Remove-Item -Recurse -Force $buildAbs
        }
    }
}

# --- run configure + build inside the MinGW environment --------------------
# Feed the script over stdin (bash -l -s) so Windows PowerShell 5.1 does not
# mangle multi-line native-exe argument quoting (same approach as
# install-deps-windows.ps1). Normalize CRLF -> LF for bash.
$msystem = $Env.ToUpper()
$extra   = ($CMakeArgs -join ' ')

$script = @'
export MSYSTEM=__MSYSTEM__
source /etc/profile
repo="$(cygpath -u '__REPO__')"
cd "$repo" || { echo "error: cannot cd to $repo" >&2; exit 2; }
echo "==> configure: __BUILD__ (__BUILDTYPE__) using $(command -v cmake)"
cmake -B '__BUILD__' -G Ninja -DCMAKE_BUILD_TYPE=__BUILDTYPE__ __EXTRA__ || exit $?
echo "==> build: __BUILD__"
cmake --build '__BUILD__' --parallel || exit $?
echo "==> done: __BUILD__"
'@

$script = $script.
    Replace('__MSYSTEM__',   $msystem).
    Replace('__REPO__',      $repoRoot).
    Replace('__BUILD__',     $BuildDir).
    Replace('__BUILDTYPE__', $BuildType).
    Replace('__EXTRA__',     $extra)

Write-Step "Building via MSYS2 $msystem at $MsysRoot"
Invoke-BashScript $bash $script
if ($LASTEXITCODE -ne 0) { Die "build failed (exit $LASTEXITCODE)" }

Write-Host ''
Write-Step "Build complete. Run the unit tests with:"
Write-Host "    powershell -ExecutionPolicy Bypass -File scripts\test-windows.ps1"
