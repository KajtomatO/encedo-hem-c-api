<#
.SYNOPSIS
    Install the build/test dependencies for encedo-hem-c-api on Windows
    (MSYS2 / MinGW-w64 toolchain).

.DESCRIPTION
    Windows toolchain per ARCHITECTURE.md Sec.1 (fixed decision, 2026-07-15):
    MinGW-w64 via MSYS2 - the GCC build closest to the Linux one. This is
    also what the CI Windows job uses (msys2/setup-msys2).

    The script:
      1. Ensures MSYS2 is installed (installs it with winget if missing).
      2. Updates the pacman database.
      3. Installs the MinGW-w64 packages the project needs:
           - toolchain (gcc, make, gdb)      build
           - cmake, ninja, pkgconf           build system  (REQ-BUILD-001)
           - curl                            default transport (REQ-NET-002)
           - cmocka                          unit tests    (REQ-TEST-001)
           - git                             source glue
           - wolfssl                         crypto shim, M2+ (best-effort)

    cJSON and phc-winner-argon2 are vendored into the tree (ARCHITECTURE.md
    Sec.1, Sec.10) and are intentionally NOT installed here.

.PARAMETER MsysRoot
    MSYS2 install root. Default: C:\msys64.

.PARAMETER Env
    MinGW environment / package prefix: mingw64 (x86_64, default) or ucrt64.

.PARAMETER NoCrypto
    Skip wolfSSL (install only the M1 build/test dependencies).

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\install-deps-windows.ps1

.NOTES
    After it finishes, open the "MSYS2 MINGW64" shell and build:
        cmake -B build -G Ninja
        cmake --build build
        ctest --test-dir build -L unit
#>
[CmdletBinding()]
param(
    [string]$MsysRoot = 'C:\msys64',
    [ValidateSet('mingw64', 'ucrt64')]
    [string]$Env = 'mingw64',
    [switch]$NoCrypto
)

$ErrorActionPreference = 'Stop'

function Write-Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }
function Write-Warn($msg) { Write-Host "warning: $msg" -ForegroundColor Yellow }
function Die($msg)        { Write-Host "error: $msg" -ForegroundColor Red; exit 1 }

# Package prefix (mingw-w64-x86_64-* for mingw64, mingw-w64-ucrt-x86_64-* for ucrt64).
$prefix = if ($Env -eq 'ucrt64') { 'mingw-w64-ucrt-x86_64' } else { 'mingw-w64-x86_64' }

$core = @(
    "$prefix-toolchain",
    "$prefix-cmake",
    "$prefix-ninja",
    "$prefix-pkgconf",
    "$prefix-curl",
    "$prefix-cmocka",
    'git'
)
$crypto = @("$prefix-wolfssl")

# --- 1. Ensure MSYS2 ------------------------------------------------------
$bash = Join-Path $MsysRoot 'usr\bin\bash.exe'
if (-not (Test-Path $bash)) {
    Write-Step "MSYS2 not found at $MsysRoot - attempting install via winget..."
    if (Get-Command winget -ErrorAction SilentlyContinue) {
        winget install --id MSYS2.MSYS2 --accept-package-agreements --accept-source-agreements --disable-interactivity
    } elseif (Get-Command choco -ErrorAction SilentlyContinue) {
        choco install -y msys2
    } else {
        Die @"
MSYS2 is not installed and neither winget nor choco is available.
Install MSYS2 from https://www.msys2.org/ (or pass -MsysRoot to point at an
existing install), then re-run this script.
"@
    }
    if (-not (Test-Path $bash)) {
        Die "MSYS2 install did not produce $bash. If it installed elsewhere, pass -MsysRoot <path>."
    }
}
Write-Step "Using MSYS2 at $MsysRoot (env: $Env)"

# Run a command in the MSYS2 login shell; throw on non-zero exit.
# Feed the script over stdin (bash -l -s) rather than as a `-c <arg>`: Windows
# PowerShell 5.1 mangles the quoting of multi-line native-exe arguments, which
# corrupts multi-line scripts (e.g. the verify block) before bash parses them.
# Normalize CRLF -> LF so bash never sees a stray carriage return.
function Invoke-Msys([string]$cmd) {
    ($cmd -replace "`r`n", "`n") | & $bash -l -s
    if ($LASTEXITCODE -ne 0) { throw "MSYS2 command failed (exit $LASTEXITCODE): $cmd" }
}

# --- 2. Update pacman -----------------------------------------------------
Write-Step "Updating MSYS2 package database..."
# On a fresh MSYS2 the core runtime may update and close the shell; run twice.
& $bash -lc 'pacman -Syuu --noconfirm' | Out-Host
Invoke-Msys 'pacman -Syuu --noconfirm'

# --- 3. Install packages --------------------------------------------------
Write-Step "Installing core build & test dependencies..."
Invoke-Msys ("pacman -S --needed --noconfirm " + ($core -join ' '))

if (-not $NoCrypto) {
    Write-Step "Installing crypto dependencies (wolfSSL, for M2+)..."
    try {
        Invoke-Msys ("pacman -S --needed --noconfirm " + ($crypto -join ' '))
    } catch {
        Write-Warn "wolfSSL not installed - M2 can fall back to a vendored/FetchContent build (ARCHITECTURE.md Sec.12). Not needed for M1."
    }
} else {
    Write-Step "Skipping crypto dependencies (-NoCrypto)."
}

# --- 4. Verify ------------------------------------------------------------
Write-Step "Verifying toolchain (inside the $Env environment)..."
$verify = @'
set -e
for t in gcc cmake ninja pkg-config git; do
  if command -v "$t" >/dev/null 2>&1; then
    printf '  ok   %-10s %s\n' "$t" "$($t --version 2>&1 | head -1)"
  else
    printf '  MISS %-10s (not found)\n' "$t"; exit 1
  fi
done
cmv="$(cmake --version | head -1 | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?')"
[ "$(printf '%s\n3.20\n' "$cmv" | sort -V | head -1)" = "3.20" ] || { echo "cmake $cmv < 3.20"; exit 1; }
pkg-config --exists libcurl && echo "  ok   libcurl    (pkg-config)" || { echo "  MISS libcurl"; exit 1; }
'@
# Force the login shell into the selected MinGW environment before verifying.
Invoke-Msys ("export MSYSTEM=" + $Env.ToUpper() + "; source /etc/profile; " + $verify)

Write-Host ''
Write-Step 'All dependencies satisfied. Open the "MSYS2 MINGW64" shell and build:'
Write-Host '    cmake -B build -G Ninja'
Write-Host '    cmake --build build'
Write-Host '    ctest --test-dir build -L unit'
