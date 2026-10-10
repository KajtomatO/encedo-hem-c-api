#!/usr/bin/env bash
#
# build-deps.sh — build PINNED wolfSSL as a SHARED library and PINNED libcurl as
# a STATIC library into a prefix, for the hem-tool release binaries
# (REQ-BUILD-005).
#
# implements: REQ-BUILD-005
#
#   scripts/release/build-deps.sh <prefix>
#
# wolfSSL is shared because no published package may bundle it statically
# (ARCHITECTURE.md §1, user decision 2026-10-09 — wolfSSL is GPLv2-or-later /
# commercial): hem-tool links it dynamically and scripts/release/
# package-wolfssl.sh publishes the library as its own release asset, with its
# license text, this build's exact configure line and the corresponding source
# tarball. libcurl (curl license, permissive) is built with wolfSSL as its TLS
# backend (ARCHITECTURE.md §1: "wolfSSL may also serve as libcurl's TLS backend
# where we build libcurl ourselves") and HTTP/HTTPS only — every other protocol
# and optional dependency is off — and linked statically. Both builds use CMake
# (no autotools), so the same script runs on Linux, in the MSYS2 UCRT64 shell,
# and on a developer machine.
#
# Pinned by version AND sha256: both CI jobs build the same bytes, so the hash
# the wolfSSL asset's README quotes is a constant. Idempotent: a stamp file
# records the recipe; a prefix restored from a CI cache with the same stamp is
# reused as is. Any other stamp means a stale prefix and it is rebuilt from
# scratch — `cmake --install` never removes files, and a leftover libwolfssl.a
# beside libwolfssl.so (or a curl CMakeCache still pointing at it) is a link
# trap.
set -euo pipefail

PREFIX="${1:?usage: build-deps.sh <prefix>}"
WOLFSSL_VERSION="${WOLFSSL_VERSION:-5.7.2}"     # pinned — bump deliberately, WITH its sha256
WOLFSSL_SHA256="${WOLFSSL_SHA256:-0f2ed82e345b833242705bbc4b08a2a2037a33f7bf9c610efae6464f6b10e305}"
CURL_VERSION="${CURL_VERSION:-8.10.1}"          # pinned — bump deliberately, WITH its sha256
CURL_SHA256="${CURL_SHA256:-d15ebab765d793e2e96db090f0e172d127859d78ca6f6391d7eafecfd894bbc0}"
WORK="${WORK:-$PREFIX-src}"
STAMP="wolfssl-${WOLFSSL_VERSION}-shared+curl-${CURL_VERSION}-static"

mkdir -p "$PREFIX" "$WORK"
PREFIX="$(cd "$PREFIX" && pwd)"
WORK="$(cd "$WORK" && pwd)"

if [ -f "$PREFIX/.stamp" ] && [ "$(cat "$PREFIX/.stamp")" = "$STAMP" ]; then
  echo "deps already built: $STAMP ($PREFIX)"
  exit 0
fi
rm -rf "$PREFIX" "$WORK/wolfssl-build" "$WORK/curl-build"
mkdir -p "$PREFIX/src"

fetch() {  # fetch <url> <out> <sha256> — downloads unless present; ALWAYS verifies
  if [ ! -f "$2" ]; then
    echo "fetching $1"
    curl -fsSL --retry 3 -o "$2" "$1"
  fi
  echo "$3  $2" | sha256sum -c - > /dev/null \
    || { echo "sha256 mismatch: $2 (expected $3)"; exit 1; }
}

GEN=(-G Ninja)
command -v ninja > /dev/null 2>&1 || GEN=()

# --- wolfSSL (SHARED) --------------------------------------------------------
# GitHub's tag archive (the file the release's .asc signs); it has no
# generated ./configure, hence CMake.
cd "$WORK"
WOLFSSL_SRC="wolfssl-${WOLFSSL_VERSION}-stable.tar.gz"
fetch "https://github.com/wolfSSL/wolfssl/archive/refs/tags/v${WOLFSSL_VERSION}-stable.tar.gz" \
  "$WOLFSSL_SRC" "$WOLFSSL_SHA256"
rm -rf "wolfssl-${WOLFSSL_VERSION}-stable"
tar xzf "$WOLFSSL_SRC"
# GPLv2 §3 corresponding source: the exact bytes built from, their hash and the
# exact configure line stay in the prefix (cached with it); package-wolfssl.sh
# publishes them beside the library.
cp "$WOLFSSL_SRC" "$PREFIX/src/"
echo "$WOLFSSL_SHA256  $WOLFSSL_SRC" > "$PREFIX/src/$WOLFSSL_SRC.sha256"

# WOLFSSL_CURL: the option set libcurl's wolfSSL backend needs (SNI, ALPN,
# OpenSSL-compat layer, ...). The rest is what the SDK's crypto shim uses:
# X25519, Ed25519/Ed448 verify, ECC, SHA-512 (BIP39 seed), PBKDF2, AES key
# wrap, cert parsing/generation. Hardening stays on (default). These are the
# flags of the headers hem-tool compiles against — the installed options.h must
# describe the library a user drops next to the binary.
# -Wno-error: wolfSSL's CMake build treats warnings as errors and newer GCCs
# (13+) flag a benign stringop-overflow in tls.c — a third-party warning policy
# is not ours to enforce; the SDK's own -Werror build is unaffected.
WOLFSSL_LDFLAGS=()
case "$(uname -s)" in
  Linux*) ;;
  *)
    # MinGW: wolfSSL links Threads::Threads (= libwinpthread) and gcc's runtime;
    # fold both INTO the DLL so the published asset depends on Windows system
    # DLLs only (both runtimes are permissively licensed).
    WOLFSSL_LDFLAGS=(-DCMAKE_SHARED_LINKER_FLAGS=-static)
    ;;
esac
WOLFSSL_ARGS=(-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX"
  -DCMAKE_C_FLAGS="-Wno-error" -DBUILD_SHARED_LIBS=ON
  -DWOLFSSL_CURL=yes -DWOLFSSL_CURVE25519=yes -DWOLFSSL_ED25519=yes
  -DWOLFSSL_ED448=yes -DWOLFSSL_SHA512=yes -DWOLFSSL_PWDBASED=yes
  -DWOLFSSL_AESKEYWRAP=yes -DWOLFSSL_KEYGEN=yes -DWOLFSSL_CERTGEN=yes
  -DWOLFSSL_SNI=yes -DWOLFSSL_ALPN=yes -DWOLFSSL_EXAMPLES=no
  -DWOLFSSL_CRYPT_TESTS=no "${WOLFSSL_LDFLAGS[@]}")
{
  echo "# wolfSSL ${WOLFSSL_VERSION} built by scripts/release/build-deps.sh on $(uname -sm)"
  echo "# $( (${CC:-cc} --version 2>/dev/null || gcc --version 2>/dev/null || true) | head -1 ); $(cmake --version | head -1)"
  printf '%q ' cmake -S "wolfssl-${WOLFSSL_VERSION}-stable" -B wolfssl-build "${GEN[@]}" "${WOLFSSL_ARGS[@]}"
  echo
} > "$PREFIX/wolfssl-build-config.txt"
cmake -S "wolfssl-${WOLFSSL_VERSION}-stable" -B wolfssl-build "${GEN[@]}" "${WOLFSSL_ARGS[@]}" \
  > configure-wolfssl.log 2>&1 || { tail -40 configure-wolfssl.log; exit 1; }
cmake --build wolfssl-build > make-wolfssl.log 2>&1 || { tail -40 make-wolfssl.log; exit 1; }
cmake --install wolfssl-build > /dev/null
cp "wolfssl-${WOLFSSL_VERSION}-stable/COPYING" "$PREFIX/COPYING.wolfssl"

# --- libcurl (STATIC, wolfSSL backend) ---------------------------------------
cd "$WORK"
fetch "https://curl.se/download/curl-${CURL_VERSION}.tar.gz" curl.tar.gz "$CURL_SHA256"
if [ ! -d "curl-${CURL_VERSION}" ]; then
  tar xzf curl.tar.gz
fi
CA_OPTS=()
case "$(uname -s)" in
  Linux*)
    # The Debian/Ubuntu bundle + the hashed CA directory; distros without the
    # bundle file still have the directory. Overridable per call with
    # --cacert (EHEM_TLS_CA_FILE).
    CA_OPTS=(-DCURL_CA_BUNDLE=/etc/ssl/certs/ca-certificates.crt -DCURL_CA_PATH=/etc/ssl/certs)
    ;;
  *)
    # Windows: the native store is loaded at run time (CURLSSLOPT_NATIVE_CA,
    # set by the SDK's transport on _WIN32); no bundled file.
    CA_OPTS=(-DCURL_CA_BUNDLE=none -DCURL_CA_PATH=none)
    ;;
esac
# curl's FindWolfSSL picks the shared library (the only one in the prefix) and
# records its absolute path in CURLTargets.cmake, so hem-tool's final link
# imports libwolfssl rather than absorbing it.
cmake -S "curl-${CURL_VERSION}" -B curl-build "${GEN[@]}" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DCMAKE_PREFIX_PATH="$PREFIX" -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DBUILD_CURL_EXE=OFF -DBUILD_TESTING=OFF \
  -DBUILD_LIBCURL_DOCS=OFF -DENABLE_CURL_MANUAL=OFF \
  -DCURL_USE_WOLFSSL=ON -DCURL_USE_OPENSSL=OFF -DHTTP_ONLY=ON \
  -DCURL_USE_LIBPSL=OFF -DCURL_USE_LIBSSH2=OFF -DUSE_LIBIDN2=OFF \
  -DUSE_NGHTTP2=OFF -DCURL_ZLIB=OFF -DCURL_BROTLI=OFF -DCURL_ZSTD=OFF \
  -DENABLE_UNIX_SOCKETS=OFF "${CA_OPTS[@]}" > configure-curl.log 2>&1 \
  || { tail -40 configure-curl.log; exit 1; }
cmake --build curl-build > make-curl.log 2>&1 || { tail -40 make-curl.log; exit 1; }
cmake --install curl-build > /dev/null
cp "curl-${CURL_VERSION}/COPYING" "$PREFIX/COPYING.curl"

echo "$STAMP" > "$PREFIX/.stamp"
echo "built: $STAMP → $PREFIX"
[ ! -e "$PREFIX/lib/libwolfssl.a" ] \
  || { echo "a STATIC libwolfssl.a is in the prefix — wolfSSL must be shared"; exit 1; }
case "$(uname -s)" in
  Linux*)
    ls -l "$PREFIX"/lib/libwolfssl.so* "$PREFIX/lib/libcurl.a"
    readelf -d "$PREFIX/lib/libwolfssl.so" | grep -E 'SONAME|NEEDED'
    ;;
  *)
    ls -l "$PREFIX/bin/libwolfssl.dll" "$PREFIX/lib/libwolfssl.dll.a" "$PREFIX/lib/libcurl.a"
    DLLS="$(objdump -p "$PREFIX/bin/libwolfssl.dll" | awk '/DLL Name:/ {print tolower($3)}' | sort -u)"
    echo "libwolfssl.dll imports:"
    # shellcheck disable=SC2001  # indenting a multi-line value
    echo "$DLLS" | sed 's/^/  /'
    if echo "$DLLS" | grep -E -q 'libgcc|winpthread'; then
      echo "libwolfssl.dll depends on a MinGW runtime DLL — not shippable on its own"; exit 1
    fi
    ;;
esac
