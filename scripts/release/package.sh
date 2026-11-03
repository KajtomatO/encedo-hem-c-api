#!/usr/bin/env bash
#
# package.sh — the SDK-only release orchestrator (REQ-BUILD-010): build the SDK
# as a Release tree against a prefix from build-deps.sh, run the unit suite on
# that build (the consumer gate included), prove the REQ-NET-005 expired-
# certificate classifier against real handshakes (tests/release/tls-probe;
# Linux), then package the library asset(s) of the variant and write SHA256SUMS.
#
# implements: REQ-BUILD-010
#
#   scripts/release/package.sh <prefix> <linux|windows> [dist-dir] [dynamic|static]
#
# Output in <dist>:
#   dynamic (the default; <prefix> from build-deps.sh wolfssl — wolfSSL SHARED,
#     libcurl static): encedo-hem-<label>-<os>-x86_64-shared.{tar.gz|zip}, the
#     shared SDK library (package-lib.sh), and wolfssl-<wv>-<os>-x86_64.{tar.gz|zip},
#     the companion library it loads — its own asset, never inside another
#     (ARCHITECTURE.md §1: wolfSSL is GPLv2-or-later / commercial, the SDK is
#     MIT); on Linux also wolfssl-<wv>-stable-src.tar.gz (package-wolfssl.sh).
#   static (<prefix> from build-deps.sh openssl — OpenSSL and libcurl both
#     STATIC): encedo-hem-<label>-<os>-x86_64.{tar.gz|zip} — the PLAIN name: the
#     static SDK with the OpenSSL and libcurl archives inside is the default
#     asset and the wolfSSL build carries -shared (user decision 2026-10-19;
#     package-lib.sh static).
# Either run then rewrites SHA256SUMS over every archive in <dist> — the two
# runs of a release job share one <dist>. Until the SDK's 1.2.1 these runs also
# built hem-tool (the one-file binary under the plain name, the wolfSSL one as
# -shared); the CLI is released from encedo-hem-tool since, built against the
# plain package of an SDK release. On a tag build (RELEASE_TAG=v…) the tag must
# be v<project version> or v<project version>-rc<N> (a pre-release) — anything
# else fails here, before anything is built.
set -euo pipefail

PREFIX="$(cd "${1:?usage: package.sh <prefix> <linux|windows> [dist] [dynamic|static]}" && pwd)"
OS="${2:?linux|windows}"
DIST="${3:-dist}"
VARIANT="${4:-dynamic}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

fail() { echo "package.sh: $*"; exit 1; }

VERSION="$(sed -n -E '/^project\(encedo-hem/,/\)/{s/^[[:space:]]*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/p}' CMakeLists.txt | head -1)"
[ -n "$VERSION" ] || fail "cannot read the project version from CMakeLists.txt"
SHA="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
if [ -n "${RELEASE_TAG:-}" ]; then
  # v<version> = a release; v<version>-rc<N> = a pre-release of that version
  # (REQ-BUILD-010), published as a GitHub pre-release by release.yml.
  if [ "$RELEASE_TAG" != "v$VERSION" ] \
     && ! { [[ "$RELEASE_TAG" =~ -rc[1-9][0-9]*$ ]] && [ "${RELEASE_TAG%-rc*}" = "v$VERSION" ]; }; then
    fail "tag $RELEASE_TAG does not match the project version: expected v$VERSION or v$VERSION-rc<N>"
  fi
  LABEL="${RELEASE_TAG#v}"
  SRC_REF="tag ${RELEASE_TAG}"
else
  LABEL="${VERSION}+g${SHA}"
  SRC_REF="commit ${SHA}"
fi

write_sums() {
  ( cd "$DIST" && find . -maxdepth 1 \( -name '*.tar.gz' -o -name '*.zip' \) -printf '%f\n' \
      | sort | xargs -r sha256sum > SHA256SUMS )
  echo "SHA256SUMS:"; sed 's/^/  /' "$DIST/SHA256SUMS"
}

export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
EXTRA=()
RUN_ENV=()
case "$VARIANT" in
  dynamic)
    BUILD="${BUILD_DIR:-build-release}"
    grep -q -- '-shared+' "$PREFIX/.stamp" 2>/dev/null \
      || fail "$PREFIX was not built by scripts/release/build-deps.sh (shared wolfSSL)"
    BACKEND="wolfSSL $(pkg-config --modversion wolfssl) (shared) + libcurl $(pkg-config --modversion libcurl) (static)"
    BUILD_OPTS=()
    if [ "$OS" = "windows" ]; then
      # The test executables and the probe find libwolfssl.dll on PATH (the
      # prefix's bin/).
      RUN_ENV=(PATH="$PREFIX/bin:$PATH")
    else
      # Executables that link the shared SDK (the integration tests) make ld
      # resolve libencedo-hem.so's NEEDED libwolfssl.so.N: -rpath-link names
      # the prefix — link-time only, never recorded in a binary. Without it ld
      # silently falls back to a system libwolfssl.so.N of the same soname
      # where one exists (a dev host) and fails where none does (the CI
      # runner) — the 2026-10-10 first-run failure. At run time the tests and
      # the probe find the prefix's library through LD_LIBRARY_PATH.
      EXTRA+=("-DCMAKE_EXE_LINKER_FLAGS=-Wl,-rpath-link,$PREFIX/lib")
      RUN_ENV=(LD_LIBRARY_PATH="$PREFIX/lib")
    fi
    ;;
  static)
    BUILD="${STATIC_BUILD_DIR:-build-release-static}"
    grep -q -- '^openssl-.*-static+curl-.*-static+' "$PREFIX/.stamp" 2>/dev/null \
      || fail "$PREFIX was not built by scripts/release/build-deps.sh <prefix> openssl"
    BACKEND="OpenSSL $(pkg-config --modversion libcrypto) (static) + libcurl $(pkg-config --modversion libcurl) (static)"
    # The static SDK only, on the OpenSSL backend; EHEM_DEPS_LINK=static makes a
    # dependency found as a shared library a configure error (REQ-BUILD-006).
    BUILD_OPTS=(-DEHEM_CRYPTO_BACKEND=openssl -DEHEM_DEPS_LINK=static -DEHEM_BUILD_SHARED=OFF)
    if [ "$OS" = "windows" ]; then
      # -static: libgcc, libwinpthread and every archive into the test
      # executables — the link a consumer of the plain package makes.
      EXTRA+=(-DCMAKE_EXE_LINKER_FLAGS=-static)
    else
      EXTRA+=(-DCMAKE_EXE_LINKER_FLAGS=-static-libgcc)
    fi
    ;;
  *) fail "unknown variant '$VARIANT' (dynamic|static)" ;;
esac

# --- build Release with the tests; the unit suite on that build --------------
# libcurl comes from the prefix's CURLConfig.cmake — a static-only install, so
# no FindCURL switch is needed. BUILD_TESTING stays ON: the suite, the consumer
# gate (REQ-BUILD-008; package mode follows in package-lib.sh) and
# tests/release/tls-probe come from this tree.
echo "=== $VARIANT: $BACKEND -> $BUILD"
cmake -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$PREFIX" \
  ${BUILD_OPTS[@]+"${BUILD_OPTS[@]}"} ${EXTRA[@]+"${EXTRA[@]}"}
cmake --build "$BUILD"
env ${RUN_ENV[@]+"${RUN_ENV[@]}"} ctest --test-dir "$BUILD" -L unit --output-on-failure

# --- REQ-NET-005: the expired-certificate classifier against real handshakes --
# tls-probe (public API only) under this build's TLS backend: the expired leaf
# must read tls_expired=1, the not-yet-valid one tls_expired=0 with the refusal
# on the date. Linux only: the check serves the leaves with `openssl s_server`
# and mints them with python3-cryptography, which the Windows job does not
# carry; ports 8443/8444 for the wolfSSL build, 8445/8446 for the OpenSSL one.
if [ "$OS" = "linux" ]; then
  PORT=8443; [ "$VARIANT" = static ] && PORT=8445
  echo "=== $VARIANT: REQ-NET-005 classifier (tls-probe, $BACKEND)"
  env ${RUN_ENV[@]+"${RUN_ENV[@]}"} \
    "$ROOT/scripts/release/check-expired-classifier.sh" "$BUILD/tests/release/tls-probe" "$PORT"
fi

# --- the assets --------------------------------------------------------------
if [ "$VARIANT" = dynamic ]; then
  # The wolfSSL companion asset first: package-lib.sh smoke-tests the shared
  # SDK beside it, exactly as a user unpacks the two.
  "$ROOT/scripts/release/package-wolfssl.sh" "$PREFIX" "$OS" "$DIST"
  "$ROOT/scripts/release/package-lib.sh" "$PREFIX" "$OS" "$DIST" "$LABEL" "$SRC_REF" shared
else
  "$ROOT/scripts/release/package-lib.sh" "$PREFIX" "$OS" "$DIST" "$LABEL" "$SRC_REF" static
fi
write_sums
