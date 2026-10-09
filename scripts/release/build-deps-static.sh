#!/usr/bin/env bash
#
# build-deps-static.sh — build PINNED wolfSSL and libcurl as STATIC libraries
# into a prefix, for the hem-tool release binaries (REQ-BUILD-005).
#
# implements: REQ-BUILD-005
#
#   scripts/release/build-deps-static.sh <prefix>
#
# libcurl is built with wolfSSL as its TLS backend (ARCHITECTURE.md §1: "wolfSSL
# may also serve as libcurl's TLS backend where we build libcurl ourselves")
# and HTTP/HTTPS only — every other protocol and optional dependency is off, so
# the only thing linked into hem-tool besides the C runtime is these two
# libraries. Both are built position-independent so the SDK's shared library
# links them too. Both builds use CMake (no autotools), so the same script runs
# on Linux, in the MSYS2 MINGW64 shell, and on a developer machine.
#
# Idempotent: a stamp file records the versions; a prefix restored from a CI
# cache with the same stamp is reused as is.
set -euo pipefail

PREFIX="${1:?usage: build-deps-static.sh <prefix>}"
WOLFSSL_VERSION="${WOLFSSL_VERSION:-5.7.2}"     # pinned — bump deliberately
CURL_VERSION="${CURL_VERSION:-8.10.1}"          # pinned — bump deliberately
WORK="${WORK:-$PREFIX-src}"
STAMP="wolfssl-${WOLFSSL_VERSION}+curl-${CURL_VERSION}"

mkdir -p "$PREFIX" "$WORK"
PREFIX="$(cd "$PREFIX" && pwd)"
WORK="$(cd "$WORK" && pwd)"

if [ -f "$PREFIX/.stamp" ] && [ "$(cat "$PREFIX/.stamp")" = "$STAMP" ]; then
  echo "deps already built: $STAMP ($PREFIX)"
  exit 0
fi

fetch() {  # fetch <url> <out>
  echo "fetching $1"
  curl -fsSL --retry 3 -o "$2" "$1"
}

GEN=(-G Ninja)
command -v ninja > /dev/null 2>&1 || GEN=()

# --- wolfSSL ---------------------------------------------------------------
# GitHub's tag archive (the file the release's .asc signs); it has no
# generated ./configure, hence CMake.
cd "$WORK"
if [ ! -d "wolfssl-${WOLFSSL_VERSION}-stable" ]; then
  fetch "https://github.com/wolfSSL/wolfssl/archive/refs/tags/v${WOLFSSL_VERSION}-stable.tar.gz" wolfssl.tar.gz
  tar xzf wolfssl.tar.gz
fi
# WOLFSSL_CURL: the option set libcurl's wolfSSL backend needs (SNI, ALPN,
# OpenSSL-compat layer, ...). The rest is what the SDK's crypto shim uses:
# X25519, Ed25519/Ed448 verify, ECC, SHA-512 (BIP39 seed), PBKDF2, AES key
# wrap, cert parsing/generation. Hardening stays on (default).
# -Wno-error: wolfSSL's CMake build treats warnings as errors and newer GCCs
# (13+) flag a benign stringop-overflow in tls.c — a third-party warning policy
# is not ours to enforce; the SDK's own -Werror build is unaffected.
cmake -S "wolfssl-${WOLFSSL_VERSION}-stable" -B wolfssl-build "${GEN[@]}" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DCMAKE_C_FLAGS="-Wno-error" \
  -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DWOLFSSL_CURL=yes -DWOLFSSL_CURVE25519=yes -DWOLFSSL_ED25519=yes \
  -DWOLFSSL_ED448=yes -DWOLFSSL_SHA512=yes -DWOLFSSL_PWDBASED=yes \
  -DWOLFSSL_AESKEYWRAP=yes -DWOLFSSL_KEYGEN=yes -DWOLFSSL_CERTGEN=yes \
  -DWOLFSSL_SNI=yes -DWOLFSSL_ALPN=yes -DWOLFSSL_EXAMPLES=no \
  -DWOLFSSL_CRYPT_TESTS=no > configure-wolfssl.log 2>&1 \
  || { tail -40 configure-wolfssl.log; exit 1; }
cmake --build wolfssl-build > make-wolfssl.log 2>&1 || { tail -40 make-wolfssl.log; exit 1; }
cmake --install wolfssl-build > /dev/null
cp "wolfssl-${WOLFSSL_VERSION}-stable/COPYING" "$PREFIX/COPYING.wolfssl"

# --- libcurl ---------------------------------------------------------------
cd "$WORK"
if [ ! -d "curl-${CURL_VERSION}" ]; then
  fetch "https://curl.se/download/curl-${CURL_VERSION}.tar.gz" curl.tar.gz
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
ls -1 "$PREFIX"/lib*/libwolfssl.a "$PREFIX"/lib*/libcurl.a
