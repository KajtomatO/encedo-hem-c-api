#!/usr/bin/env bash
#
# install-deps-linux.sh — install the build/test dependencies for
# encedo-hem-c-api on Linux.
#
# Toolchain per ARCHITECTURE.md §1 (fixed decisions):
#   - CMake >= 3.20 + CTest       (build system)
#   - C99 compiler: GCC and Clang (both, for the warnings-as-errors matrix)
#   - libcurl dev                 (default transport, REQ-NET-002)
#   - CMocka dev                  (unit tests, REQ-TEST-001)
#   - pkg-config, make/ninja, git (build glue)
#   - wolfSSL dev                 (crypto shim from M2 onward; best-effort —
#                                  M2 may switch to a FetchContent build with
#                                  a pinned wolfCrypt config, see §12 risk)
#
# cJSON and phc-winner-argon2 are *vendored* into the source tree
# (ARCHITECTURE.md §1, §10), so they are deliberately NOT installed here.
#
# Primary target is Debian/Ubuntu (apt); dnf, pacman and zypper are handled
# best-effort for contributor convenience. Idempotent — safe to re-run.
#
# Usage: scripts/install-deps-linux.sh [--no-crypto] [--yes] [--help]
set -euo pipefail

WITH_CRYPTO=1
ASSUME_YES=0

usage() {
  sed -n '3,22p' "$0" | sed 's/^# \{0,1\}//'
  cat <<'EOF'

Options:
  --no-crypto   Skip wolfSSL (only the M1 build/test deps).
  --yes, -y     Non-interactive; pass the manager's assume-yes flag.
  --help, -h    Show this help.
EOF
}

log()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33mwarning:\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }

for arg in "$@"; do
  case "$arg" in
    --no-crypto) WITH_CRYPTO=0 ;;
    --yes|-y)    ASSUME_YES=1 ;;
    --help|-h)   usage; exit 0 ;;
    *)           die "unknown option: $arg (try --help)" ;;
  esac
done

# Elevate with sudo only when we are not already root.
SUDO=""
if [ "$(id -u)" -ne 0 ]; then
  if command -v sudo >/dev/null 2>&1; then
    SUDO="sudo"
  else
    die "not running as root and 'sudo' not found; re-run as root"
  fi
fi

detect_mgr() {
  for m in apt-get dnf pacman zypper; do
    if command -v "$m" >/dev/null 2>&1; then echo "$m"; return; fi
  done
  echo ""
}

MGR="$(detect_mgr)"
[ -n "$MGR" ] || die "no supported package manager (apt-get/dnf/pacman/zypper) found"
log "Package manager: $MGR"

# --- package name maps (core = fatal, crypto = best-effort) ---------------
case "$MGR" in
  apt-get)
    CORE=(build-essential clang cmake pkg-config git ninja-build
          libcurl4-openssl-dev libcmocka-dev)
    CRYPTO=(libwolfssl-dev)
    ;;
  dnf)
    CORE=(gcc gcc-c++ clang make cmake pkgconf-pkg-config git ninja-build
          libcurl-devel libcmocka-devel)
    CRYPTO=(wolfssl-devel)
    ;;
  pacman)
    CORE=(base-devel clang cmake pkgconf git ninja curl cmocka)
    CRYPTO=(wolfssl)
    ;;
  zypper)
    CORE=(gcc gcc-c++ clang make cmake pkg-config git ninja
          libcurl-devel libcmocka-devel)
    CRYPTO=(libwolfssl-devel)
    ;;
esac

# --- install helpers ------------------------------------------------------
refresh() {
  case "$MGR" in
    apt-get) $SUDO apt-get update ;;
    dnf)     : ;;                       # dnf refreshes metadata implicitly
    pacman)  $SUDO pacman -Sy ;;
    zypper)  $SUDO zypper --non-interactive refresh ;;
  esac
}

# install <fatal:0|1> <pkg...>
install() {
  local fatal="$1"; shift
  local yes=()
  [ "$ASSUME_YES" -eq 1 ] && case "$MGR" in
    apt-get|dnf) yes=(-y) ;;
    pacman)      yes=(--noconfirm) ;;
    zypper)      yes=(--non-interactive) ;;
  esac
  local cmd
  case "$MGR" in
    apt-get) cmd=($SUDO env DEBIAN_FRONTEND=noninteractive apt-get install "${yes[@]}" "$@") ;;
    dnf)     cmd=($SUDO dnf install "${yes[@]}" "$@") ;;
    pacman)  cmd=($SUDO pacman -S --needed "${yes[@]}" "$@") ;;
    zypper)  cmd=($SUDO zypper install "${yes[@]}" "$@") ;;
  esac
  if "${cmd[@]}"; then
    return 0
  elif [ "$fatal" -eq 1 ]; then
    die "failed to install: $*"
  else
    warn "could not install optional packages: $*"
    return 1
  fi
}

refresh
log "Installing core build & test dependencies..."
install 1 "${CORE[@]}"

if [ "$WITH_CRYPTO" -eq 1 ]; then
  log "Installing crypto dependencies (wolfSSL, for M2+)..."
  install 0 "${CRYPTO[@]}" || warn \
    "wolfSSL not installed from the system repo — M2 can fall back to a
     vendored/FetchContent wolfSSL build (ARCHITECTURE.md §12). Not needed for M1."
else
  log "Skipping crypto dependencies (--no-crypto)."
fi

# --- verification ---------------------------------------------------------
log "Verifying toolchain..."
ok=1
check() { # check <label> <command...>
  local label="$1"; shift
  if "$@" >/dev/null 2>&1; then
    printf '  \033[1;32mok\033[0m   %-12s %s\n' "$label" "$("$@" 2>&1 | head -1)"
  else
    printf '  \033[1;31mMISS\033[0m %-12s (not found)\n' "$label"; ok=0
  fi
}
check gcc        gcc --version
check clang      clang --version
check cmake      cmake --version
check pkg-config pkg-config --version
check git        git --version

# CMake must be >= 3.20 (REQ-BUILD-001).
if command -v cmake >/dev/null 2>&1; then
  cmv="$(cmake --version | head -1 | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?')"
  if [ "$(printf '%s\n3.20\n' "$cmv" | sort -V | head -1)" != "3.20" ]; then
    warn "CMake $cmv is older than the required 3.20"
    ok=0
  fi
fi

# Dev headers that the build links against.
if pkg-config --exists libcurl 2>/dev/null || [ -e /usr/include/curl/curl.h ]; then
  printf '  \033[1;32mok\033[0m   %-12s libcurl dev headers present\n' "libcurl"
else
  printf '  \033[1;31mMISS\033[0m %-12s (dev headers not found)\n' "libcurl"; ok=0
fi
if [ -e /usr/include/cmocka.h ] || pkg-config --exists cmocka 2>/dev/null; then
  printf '  \033[1;32mok\033[0m   %-12s cmocka dev headers present\n' "cmocka"
else
  printf '  \033[1;31mMISS\033[0m %-12s (dev headers not found)\n' "cmocka"; ok=0
fi
# wolfSSL is the crypto shim dependency, required from M2 on (REQ-AUTH-001). Only
# checked when crypto was requested; a miss is a warning (M2 may fall back to a
# FetchContent build), not a hard failure of the M1 toolchain.
if [ "$WITH_CRYPTO" -eq 1 ]; then
  if pkg-config --exists wolfssl 2>/dev/null; then
    printf '  \033[1;32mok\033[0m   %-12s wolfSSL dev present (%s)\n' "wolfssl" "$(pkg-config --modversion wolfssl)"
  else
    warn "wolfSSL dev headers not found — required to build from M2 on (crypto shim)"
  fi
fi

echo
if [ "$ok" -eq 1 ]; then
  log "All dependencies satisfied. Build with:"
  echo "    cmake -B build && cmake --build build && ctest --test-dir build -L unit"
else
  die "some dependencies are missing (see above)"
fi
