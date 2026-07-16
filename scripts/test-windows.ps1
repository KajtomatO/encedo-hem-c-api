<#
.SYNOPSIS
    Run the encedo-hem-c-api CTest suites on Windows through the MSYS2 /
    MinGW-w64 toolchain, from an ordinary PowerShell prompt.

.DESCRIPTION
    Companion to scripts\build-windows.ps1. It runs ctest *inside* the MinGW64
    environment (where the built DLLs and their curl/wolfSSL runtime deps are
    on PATH), so tests find their dependencies exactly as they were built.

    Tests are partitioned into CTest labels (ARCHITECTURE.md Sec.9):
      unit         offline CMocka tests + the symbol-export gate; the default.
      integration  real-device round-trips; gated on EHEM_TEST_URL. Skips when
                   the variable is unset, so a checkout without a device stays
                   green.
      disruptive   mutates device state (reboot/firmware/wipe); also needs
                   EHEM_ALLOW_DISRUPTIVE=1. Run attended, never unattended.

    For integration/disruptive runs the device credentials come from the
    environment if already set; otherwise, if a git-ignored .\hem.env exists at
    the repo root, it is sourced (matching the Linux `./dev` wrapper).

.PARAMETER Label
    CTest label to run: unit (default), integration, or disruptive.

.PARAMETER BuildDir
    Build directory produced by build-windows.ps1. Default: build.

.PARAMETER MsysRoot
    MSYS2 install root. Default: C:\msys64.

.PARAMETER Env
    MinGW environment: mingw64 (default) or ucrt64. Match the build.

.PARAMETER CTestArgs
    Any remaining arguments are passed through to ctest, e.g. -R <regex> or
    --repeat until-fail:3.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\test-windows.ps1

.EXAMPLE
    scripts\test-windows.ps1 -Label integration

.NOTES
    Build first with scripts\build-windows.ps1.
#>
[CmdletBinding()]
param(
    [ValidateSet('unit', 'integration', 'disruptive')]
    [string]$Label = 'unit',
    [string]$BuildDir = 'build',
    [string]$MsysRoot = 'C:\msys64',
    [ValidateSet('mingw64', 'ucrt64')]
    [string]$Env = 'mingw64',
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$CTestArgs = @()
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

$buildAbs = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } `
            else { Join-Path $repoRoot $BuildDir }
if (-not (Test-Path (Join-Path $buildAbs 'CMakeCache.txt'))) {
    Die @"
$BuildDir is not configured. Build it first:
    powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1
"@
}

# --- device credentials for integration/disruptive ------------------------
# Source .\hem.env inside the bash run only when the credentials are not
# already in the environment (mirrors ./dev). Never touched for the unit label.
$sourceEnv = ''
if ($Label -ne 'unit') {
    if ($env:EHEM_TEST_URL) {
        Write-Step "Using EHEM_TEST_URL from the environment."
    } elseif (Test-Path (Join-Path $repoRoot 'hem.env')) {
        Write-Warn "EHEM_TEST_URL not set -- sourcing .\hem.env for device credentials."
        $sourceEnv = '[ -f hem.env ] && . ./hem.env'
    } else {
        Write-Warn "No device credentials (EHEM_TEST_URL unset, no .\hem.env). '$Label' tests will skip."
    }
}

# --- run ctest inside the MinGW environment --------------------------------
# --no-tests=ignore so a label with no matching tests yet (e.g. disruptive
# before its first test exists) does not fail the run (matches ./dev).
$msystem = $Env.ToUpper()
$extra   = ($CTestArgs -join ' ')

$script = @'
export MSYSTEM=__MSYSTEM__
source /etc/profile
repo="$(cygpath -u '__REPO__')"
cd "$repo" || { echo "error: cannot cd to $repo" >&2; exit 2; }
__SOURCE_ENV__
echo "==> ctest -L __LABEL__ (__BUILD__) using $(command -v ctest)"
ctest --test-dir '__BUILD__' -L __LABEL__ --output-on-failure --no-tests=ignore __EXTRA__ || exit $?
'@

$script = $script.
    Replace('__MSYSTEM__',    $msystem).
    Replace('__REPO__',       $repoRoot).
    Replace('__SOURCE_ENV__', $sourceEnv).
    Replace('__LABEL__',      $Label).
    Replace('__BUILD__',      $BuildDir).
    Replace('__EXTRA__',      $extra)

Write-Step "Testing (-L $Label) via MSYS2 $msystem at $MsysRoot"
Invoke-BashScript $bash $script
if ($LASTEXITCODE -ne 0) { Die "tests failed (exit $LASTEXITCODE)" }

Write-Host ''
Write-Step "Tests passed (-L $Label)."
