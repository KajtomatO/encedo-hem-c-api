#!/usr/bin/env bash
#
# package-static.sh — the one-file hem-tool (REQ-BUILD-009): hem-tool as a
# Release binary on the OpenSSL backend (REQ-BUILD-007) with EVERY third-party
# library linked in — OpenSSL and libcurl from the deps-openssl prefix
# (build-deps.sh <prefix> openssl). Linux keeps glibc dynamic (a static glibc
# breaks getaddrinfo/NSS); Windows imports system DLLs only. The unit suite runs
# on that build (the consumer gate included: the all-static pkg-config link,
# REQ-BUILD-008), the link mode and the absence of any wolfSSL code are
# asserted, the binary is packaged with its notices as
# hem-tool-<label>-<os>-x86_64.{tar.gz|zip} — the PLAIN name: from 1.2.0 the
# one-file build is the default asset and the wolfSSL build carries -shared
# (user decision 2026-10-19; until v1.2.0-rc1 this one was -static) — and
# smoke-tested from a directory of its own; then the static SDK library
# package (package-lib.sh static).
#
# implements: REQ-BUILD-009
#
#   scripts/release/package-static.sh <prefix> <linux|windows> <dist> <label> <source ref>
#
# Called by package.sh (the `static` variant), which writes SHA256SUMS afterwards.
set -euo pipefail

PREFIX="$(cd "${1:?usage: package-static.sh <prefix> <linux|windows> <dist> <label> <source ref>}" && pwd)"
OS="${2:?linux|windows}"
DIST="$(mkdir -p "${3:?dist}" && cd "$3" && pwd)"
LABEL="${4:?label}"
SRC_REF="${5:?source ref}"
BUILD="${STATIC_BUILD_DIR:-build-release-static}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

fail() { echo "package-static.sh: $*"; exit 1; }
# Paths handed to native Windows programs in their own form.
native() { if [ "$OS" = "windows" ]; then cygpath -m "$1"; else printf '%s' "$1"; fi; }

grep -q -- '^openssl-.*-static+curl-.*-static+' "$PREFIX/.stamp" 2>/dev/null \
  || fail "$PREFIX was not built by scripts/release/build-deps.sh <prefix> openssl"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
OPENSSL_VERSION="$(pkg-config --modversion libcrypto)"
CURL_VERSION="$(pkg-config --modversion libcurl)"
NAME="hem-tool-${LABEL}-${OS}-x86_64"

EXTRA=()
if [ "$OS" = "windows" ]; then
  # -static: libgcc, libwinpthread and every archive in.
  EXTRA+=(-DCMAKE_EXE_LINKER_FLAGS=-static)
  EXT=zip; X=.exe
else
  # The archives + libgcc in, glibc dynamic; nothing to find at run time, so no
  # RUNPATH either.
  EXTRA+=(-DCMAKE_EXE_LINKER_FLAGS=-static-libgcc)
  EXT=tar.gz; X=
fi

# --- build (Release, OpenSSL, every dependency static) -----------------------
# The static SDK only: hem-tool carries it (EHEM_TOOL_LINK=static), and the
# library package of this variant is static only. EHEM_DEPS_LINK=static makes
# a dependency found as a shared library a configure error (REQ-BUILD-006).
cmake -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$PREFIX" \
  -DEHEM_CRYPTO_BACKEND=openssl -DEHEM_DEPS_LINK=static -DEHEM_TOOL_LINK=static \
  -DEHEM_BUILD_SHARED=OFF "${EXTRA[@]}"
cmake --build "$BUILD"
ctest --test-dir "$BUILD" -L unit --output-on-failure

# --- link mode: nothing but the system's libraries ---------------------------
BIN="$BUILD/hem-tool$X"
if [ "$OS" = "windows" ]; then
  DLLS="$(objdump -p "$BIN" | awk '/DLL Name:/ {print tolower($3)}' | sort -u)"
  echo "imports:"
  # shellcheck disable=SC2001  # indenting a multi-line value
  echo "$DLLS" | sed 's/^/  /'
  # Windows system DLLs only (OpenSSL: ws2_32, crypt32, advapi32, user32;
  # libcurl: ws2_32, bcrypt, secur32, iphlpapi ...) and the Universal CRT.
  OTHER="$(echo "$DLLS" | grep -E -v '^(api-ms-win-[a-z0-9-]+|ucrtbase|kernel32|advapi32|user32|gdi32|ws2_32|crypt32|bcrypt|secur32|iphlpapi|normaliz|wldap32|ntdll|shell32|ole32|rpcrt4|userenv)\.dll$' || true)"
  [ -z "$OTHER" ] || fail "hem-tool.exe imports a DLL that is not a Windows system DLL: $OTHER"
  echo "$DLLS" | grep -E -q '^(api-ms-win-crt-|ucrtbase\.dll)' \
    || fail "hem-tool.exe imports no Universal CRT — not a UCRT64 build"
else
  if readelf -d "$BIN" | grep -E -q '\((RUNPATH|RPATH)\)'; then
    fail "hem-tool has a RUNPATH/RPATH — it must find nothing beside itself"
  fi
  echo "shared objects:"; ldd "$BIN" | sed 's/^/  /'
  # The glibc family only: libc, libm, libpthread, libdl, librt, libresolv, the
  # dynamic loader and the kernel's vDSO.
  OTHER="$(ldd "$BIN" | awk '{print $1}' | sed 's|.*/||' \
    | grep -E -v '^(linux-vdso\.so\.1|(libc|libm|libpthread|libdl|librt|libresolv)\.so\.[0-9]+|ld-linux-x86-64\.so\.2)$' || true)"
  [ -z "$OTHER" ] || fail "hem-tool needs a library outside glibc: $OTHER"
fi

# --- no wolfSSL anywhere in a -static asset (ARCHITECTURE.md §1) --------------
# wolfSSL's API (wolfSSL_*) and wolfCrypt's (wc_*) by symbol and by string.
# (The plain name "wolfSSL" does occur: the startup message tells a
# dynamic-wolfSSL refusal from this one.)
if nm "$BIN" 2> /dev/null | grep -E -q ' (wolfSSL_|wolfCrypt_|wc_)[A-Za-z]'; then
  fail "wolfSSL symbols in $BIN"
fi
if strings -a "$BIN" | grep -E -q '(wolfSSL_|wolfCrypt_|(^|[^A-Za-z0-9_])wc_)[A-Za-z]'; then
  fail "wolfSSL names in the strings of $BIN"
fi
echo "no wolfSSL symbol or name in $BIN"

"$BIN" help > /dev/null
VER="$("$BIN" --version | tr -d '\r')"
# shellcheck disable=SC2001  # indenting a multi-line value
echo "$VER" | sed 's/^/  /'
echo "$VER" | grep -q -x "crypto:    OpenSSL ${OPENSSL_VERSION} (compiled) / ${OPENSSL_VERSION} (loaded), static" \
  || fail "--version does not report OpenSSL ${OPENSSL_VERSION} compiled and loaded, static"
echo "$VER" | grep -q -x "transport: libcurl ${CURL_VERSION} (compiled) / ${CURL_VERSION} (loaded), static" \
  || fail "--version does not report libcurl ${CURL_VERSION} compiled and loaded, static"

# --- package: one file + notices + README ------------------------------------
STAGE="$DIST/$NAME"
rm -rf "$STAGE"; mkdir -p "$STAGE/LICENSES"
cp "$BIN" "$STAGE/"
cp LICENSE "$STAGE/LICENSES/LICENSE.encedo-hem"
cp "$PREFIX/LICENSE.openssl" "$STAGE/LICENSES/LICENSE.openssl"
cp "$PREFIX/COPYING.curl" "$STAGE/LICENSES/COPYING.curl"
cp src/vendor/cjson/LICENSE "$STAGE/LICENSES/LICENSE.cjson"
cp src/vendor/bip39/LICENSE "$STAGE/LICENSES/LICENSE.bip39-wordlist"
# qrcodegen's MIT text exists only as the header block of qrcodegen.c (see its
# VENDORED.md); ship that block verbatim and FAIL if it is not there.
sed -n '1,/^ \*\/$/p' src/tools/hem-tool/vendor/qrcodegen/qrcodegen.c > "$STAGE/LICENSES/LICENSE.qrcodegen"
grep -q 'Permission is hereby granted' "$STAGE/LICENSES/LICENSE.qrcodegen" \
  || fail "qrcodegen MIT header not found"
if [ "$OS" = "windows" ]; then
  NEEDS="Windows system DLLs only - nothing to install beside it."
  TRUST="Certificates: the Windows certificate store (or --cacert FILE)."
  CONF="OpenSSL configuration: none - hem-tool reads neither openssl.cnf nor the
file OPENSSL_CONF names, so nothing on disk changes how it uses TLS."
else
  # The newest glibc symbol version it uses = the oldest glibc it runs on.
  GLIBC_MIN="$(objdump -T "$BIN" | sed -n -E 's/.*GLIBC_([0-9]+\.[0-9]+(\.[0-9]+)?).*/\1/p' | sort -V | tail -1)"
  [ -n "$GLIBC_MIN" ] || fail "no glibc symbol version in $BIN"
  NEEDS="the system C library only (glibc ${GLIBC_MIN} or later) - nothing to install
beside it."
  TRUST="Certificates: /etc/ssl/certs/ca-certificates.crt and /etc/ssl/certs (the
Debian/Ubuntu locations; elsewhere pass --cacert FILE)."
  CONF="OpenSSL configuration: /etc/ssl/openssl.cnf (or the file OPENSSL_CONF names),
as for the system's OpenSSL. One that leaves OpenSSL without the algorithms
hem-tool needs makes it stop with one message and exit code 12."
fi
cat > "$STAGE/README.txt" <<EOF
hem-tool ${LABEL} (${OS}, x86_64) - the Encedo HEM device CLI as one file.

OpenSSL ${OPENSSL_VERSION} (the crypto library and libcurl's TLS backend) and
libcurl ${CURL_VERSION} (HTTP/HTTPS only) are linked INTO hem-tool: it needs
${NEEDS}
(The -shared hem-tool asset of this release links wolfSSL dynamically and
needs the wolfSSL companion download beside it.)

${TRUST}
${CONF}

Usage: hem-tool help            (commands grouped by what they need)
       hem-tool help <command>  (per-command options)
       hem-tool --version       (versions built against and running)
Device URL via --url or EHEM_URL (default https://my.ence.do); passphrase via
EHEM_PASSPHRASE (--passphrase is deprecated: it stays in the shell history and
shows in the process list; it will be removed). Verify downloads against
SHA256SUMS:
  sha256sum -c SHA256SUMS      (Linux)
  CertUtil -hashfile <file> SHA256   (Windows)

Licenses: see LICENSES/ - hem-tool and the Encedo HEM C SDK are MIT; OpenSSL
(Apache License 2.0), libcurl (curl license), cJSON (MIT), the BIP39 wordlist
(MIT) and qrcodegen (MIT) are compiled in. Source:
https://github.com/KajtomatO/encedo-hem-c-api (${SRC_REF}); OpenSSL and curl
are their upstream releases ${OPENSSL_VERSION} and ${CURL_VERSION}.
EOF
if [ "$OS" = "windows" ]; then
  cat >> "$STAGE/README.txt" <<'EOF'

Windows: hem-tool.exe uses the Universal C Runtime (built in MSYS2 UCRT64) -
part of Windows 10 / Server 2016 and later; older Windows needs update
KB2999226.
EOF
fi

mkdir -p "$DIST"
( cd "$DIST" && rm -f "$NAME.tar.gz" "$NAME.zip" )
if [ "$OS" = "windows" ]; then
  ( cd "$DIST" && zip -qr "$NAME.zip" "$NAME" )
else
  ( cd "$DIST" && tar czf "$NAME.tar.gz" "$NAME" )
fi
echo "packaged: $DIST/$NAME.$EXT"

# --- smoke test: the archive alone, as a user unpacks it ----------------------
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
if [ "$OS" = "windows" ]; then
  unzip -q "$DIST/$NAME.zip" -d "$T"
else
  tar xzf "$DIST/$NAME.tar.gz" -C "$T"
fi
EXE="$T/$NAME/hem-tool$X"
[ "$(find "$T/$NAME" -maxdepth 1 -type f -name 'hem-tool*' | wc -l)" -eq 1 ] \
  || fail "the archive holds more than the one hem-tool file"
"$EXE" help > /dev/null
"$EXE" keys types > /dev/null
# The SDK initialises (REQ-TOOL-024) and the request then fails in the
# transport — exit 1 — on a port nothing listens on.
status_rc() {  # status_rc <env assignment...> — runs `status`; prints its stderr, returns its exit code
  local rc=0
  env "$@" "$EXE" --url https://127.0.0.1:1 status > /dev/null 2> "$T/err" || rc=$?
  tr -d '\r' < "$T/err" | sed 's/^/  /'
  return "$rc"
}
RC=0; status_rc EHEM_SMOKE=1 || RC=$?
echo "status against a closed port: exit $RC"
[ "$RC" -eq 1 ] || fail "expected exit 1 (initialised, then unreachable)"

# OpenSSL configuration (REQ-BUILD-009 rev 4): a file that activates only
# OpenSSL's null provider leaves an OpenSSL that reads it no algorithms at all.
cat > "$T/null-provider.cnf" <<'EOF'
openssl_conf = openssl_init
[openssl_init]
providers = provider_sect
[provider_sect]
null = null_sect
[null_sect]
activate = 1
EOF
CNF="$(native "$T/null-provider.cnf")"
RC=0; status_rc "OPENSSL_CONF=$CNF" || RC=$?
echo "with OPENSSL_CONF=$CNF (null provider only): exit $RC"
if [ "$OS" = "windows" ]; then
  # Windows reads no configuration: the run is the plain one.
  [ "$RC" -eq 1 ] || fail "the Windows one-file hem-tool read OPENSSL_CONF (exit $RC, expected 1)"
else
  # The control: Linux reads it — the SDK's self-test refuses the provider-less
  # OpenSSL, and the remedy names the configuration (REQ-TOOL-024 rev 5).
  [ "$RC" -eq 12 ] || fail "the Linux one-file hem-tool ignored OPENSSL_CONF (exit $RC, expected 12)"
  grep -q 'OpenSSL is linked into hem-tool' "$T/err" \
    || fail "the refusal does not name the OpenSSL configuration"
fi
echo "one-file hem-tool smoke OK: $NAME.$EXT"

# --- the static SDK library package, OpenSSL flavour -------------------------
"$ROOT/scripts/release/package-lib.sh" "$PREFIX" "$OS" "$DIST" "$LABEL" "$SRC_REF" static
