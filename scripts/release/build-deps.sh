#!/usr/bin/env bash
#
# build-deps.sh — build the pinned release dependencies into a prefix:
#   wolfssl (default) — PINNED wolfSSL as a SHARED library and PINNED libcurl as
#     a STATIC library, for the hem-tool release binaries and the wolfSSL SDK
#     package (REQ-BUILD-005, REQ-BUILD-009). On Windows also the delay-load
#     import library of libwolfssl.dll (lib/libwolfssl.delay.a) that the release
#     hem-tool links, so a missing DLL is reported by hem-tool itself
#     (REQ-TOOL-026).
#   openssl — PINNED OpenSSL 3.5 LTS and PINNED libcurl (OpenSSL as its TLS
#     backend), both STATIC: the deps-openssl prefix of the -static release
#     variants (REQ-BUILD-009).
#
# implements: REQ-BUILD-005, REQ-BUILD-009 (the deps-openssl prefix),
#             REQ-TOOL-026 (the delay-load import library)
#
#   scripts/release/build-deps.sh <prefix> [wolfssl|openssl]
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
# OpenSSL is Apache-2.0, so the -static variants may link it statically; its
# LICENSE.txt is copied into the prefix and re-read on every pin bump, like
# wolfSSL's COPYING.
#
# Pinned by version AND sha256: both CI jobs build the same bytes, so the hash
# the wolfSSL asset's README quotes is a constant. Idempotent: a stamp file
# records the recipe; a prefix restored from a CI cache with the same stamp is
# reused as is. Any other stamp means a stale prefix and it is rebuilt from
# scratch — `cmake --install` never removes files, and a leftover libwolfssl.a
# beside libwolfssl.so (or a curl CMakeCache still pointing at it) is a link
# trap.
set -euo pipefail

PREFIX="${1:?usage: build-deps.sh <prefix> [wolfssl|openssl]}"
FLAVOUR="${2:-wolfssl}"
WOLFSSL_VERSION="${WOLFSSL_VERSION:-5.7.2}"     # pinned — bump deliberately, WITH its sha256
WOLFSSL_SHA256="${WOLFSSL_SHA256:-0f2ed82e345b833242705bbc4b08a2a2037a33f7bf9c610efae6464f6b10e305}"
CURL_VERSION="${CURL_VERSION:-8.10.1}"          # pinned — bump deliberately, WITH its sha256
CURL_SHA256="${CURL_SHA256:-d15ebab765d793e2e96db090f0e172d127859d78ca6f6391d7eafecfd894bbc0}"
# The 3.5 LTS line (REQ-BUILD-009; 3.0 LTS ended 2026-09-07). 3.5.9: sha256
# computed from the official download = the published .sha256, and its PGP
# signature verified against openssl-library.org/source/pubkeys.asc.
OPENSSL_VERSION="${OPENSSL_VERSION:-3.5.9}"     # pinned — bump deliberately, WITH its sha256
OPENSSL_SHA256="${OPENSSL_SHA256:-603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a}"
WORK="${WORK:-$PREFIX-src}"
# The recipe number changes whenever a cached prefix must not survive a recipe
# change (wolfssl r2: the Windows delay-load import library, REQ-TOOL-026;
# openssl r2: no OpenSSL configuration on Windows, REQ-BUILD-009 rev 4).
case "$FLAVOUR" in
  wolfssl) STAMP="wolfssl-${WOLFSSL_VERSION}-shared+curl-${CURL_VERSION}-static+r2" ;;
  openssl) STAMP="openssl-${OPENSSL_VERSION}-static+curl-${CURL_VERSION}-static+r2" ;;
  *) echo "build-deps.sh: unknown flavour '$FLAVOUR' (wolfssl|openssl)"; exit 1 ;;
esac

mkdir -p "$PREFIX" "$WORK"
PREFIX="$(cd "$PREFIX" && pwd)"
WORK="$(cd "$WORK" && pwd)"

if [ -f "$PREFIX/.stamp" ] && [ "$(cat "$PREFIX/.stamp")" = "$STAMP" ]; then
  echo "deps already built: $STAMP ($PREFIX)"
  exit 0
fi
rm -rf "$PREFIX" "$WORK/wolfssl-build" "$WORK/curl-build" "$WORK/libwolfssl.def"
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

if [ "$FLAVOUR" = wolfssl ]; then
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
      # DLLs only (both runtimes are permissively licensed). --output-def writes
      # the DLL's export list, the input of the delay-load import library below.
      # Its path in the Windows form: MSYS2 converts no argument that has a space
      # before its first slash, as this one, and the native linker cannot open
      # /d/a/... — it only notes that and links on, without the .def.
      WOLFSSL_LDFLAGS=("-DCMAKE_SHARED_LINKER_FLAGS=-static -Wl,--output-def,$(cygpath -m "$WORK/libwolfssl.def")")
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
    # uname says MINGW64_NT in every 64-bit MSYS2 environment; MSYSTEM and the
    # compiler's path tell UCRT64 from the deprecated MINGW64.
    [ -z "${MSYSTEM:-}" ] || echo "# MSYS2 environment: ${MSYSTEM} (compiler: $(command -v "${CC:-cc}" || command -v gcc))"
    printf '%q ' cmake -S "wolfssl-${WOLFSSL_VERSION}-stable" -B wolfssl-build "${GEN[@]}" "${WOLFSSL_ARGS[@]}"
    echo
  } > "$PREFIX/wolfssl-build-config.txt"
  cmake -S "wolfssl-${WOLFSSL_VERSION}-stable" -B wolfssl-build "${GEN[@]}" "${WOLFSSL_ARGS[@]}" \
    > configure-wolfssl.log 2>&1 || { tail -40 configure-wolfssl.log; exit 1; }
  cmake --build wolfssl-build > make-wolfssl.log 2>&1 || { tail -40 make-wolfssl.log; exit 1; }
  cmake --install wolfssl-build > /dev/null
  cp "wolfssl-${WOLFSSL_VERSION}-stable/COPYING" "$PREFIX/COPYING.wolfssl"
  case "$(uname -s)" in
    Linux*) ;;
    *)
      # REQ-TOOL-026: the delay-load import library — hem-tool links it instead of
      # libwolfssl.dll.a (CMake EHEM_WOLFSSL_DELAYLOAD_LIB), so the DLL is loaded
      # at the first wolfSSL call and a missing one reaches hem-tool's own
      # failure hook. The .def comes from the link above, and only from it: a
      # gendef fallback hid an unwritten .def until MSYS2 dropped gendef
      # (mingw-w64-tools) from its toolchain group, 2026-10-14.
      grep -q '^ *wolfSSL_Init\b' "$WORK/libwolfssl.def" 2> /dev/null \
        || { echo "the wolfSSL link wrote no export list to $WORK/libwolfssl.def"; exit 1; }
      dlltool --input-def "$WORK/libwolfssl.def" --dllname libwolfssl.dll \
              --output-delaylib "$PREFIX/lib/libwolfssl.delay.a"
      ;;
  esac
else
  # --- OpenSSL (STATIC) — the deps-openssl prefix (REQ-BUILD-009) -------------
  # no-shared: archives only; no-module: no loadable code either — the legacy
  # provider is built into libcrypto (providers/build.info), so nothing looks
  # for a module directory; no-tests: none of OpenSSL's own test programs.
  # libdir=lib everywhere (OpenSSL would pick lib64 on x86_64 Linux). The
  # OpenSSL directory is where the library looks for its configuration and
  # default certificates: /etc/ssl on Linux (Debian's certs/, other distros'
  # cert.pem; the distribution's openssl.cnf, root-owned). Windows (REQ-BUILD-009
  # rev 4, user decision 2026-10-15): NO configuration — no-autoload-config here
  # and CURL_DISABLE_OPENSSL_AUTO_LOAD_CONFIG in libcurl below, so neither
  # openssl.cnf nor OPENSSL_CONF is read; the transport loads the native
  # certificate store (CURLSSLOPT_NATIVE_CA). The compiled-in directories still
  # move off <prefix> — the runner's D:/a/... path, which a standard user could
  # create on another machine — to the administrator-only Common Files\SSL
  # (where OpenSSL's own Windows builds look), for whatever still asks for them
  # explicitly; given as make variables, which the compiler flags expand, as
  # the Windows form of the path is no path to MSYS2's perl. On Linux the
  # engines and modules directories (named by a configuration that loads a
  # provider by name) likewise leave <prefix> for root-owned ones. install_dev
  # + install_runtime = install_sw without creating those (empty) directories.
  # Built with the official Configure + make (perl): OpenSSL has no CMake build.
  cd "$WORK"
  OPENSSL_SRC="openssl-${OPENSSL_VERSION}.tar.gz"
  fetch "https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/${OPENSSL_SRC}" \
    "$OPENSSL_SRC" "$OPENSSL_SHA256"
  rm -rf "openssl-${OPENSSL_VERSION}"
  tar xzf "$OPENSSL_SRC"
  case "$(uname -s)" in
    Linux*)
      OPENSSL_ARGS=(linux-x86_64 --openssldir=/etc/ssl)
      SSL_DIRS=(/etc/ssl /usr/lib/engines-3 /usr/lib/ossl-modules)
      ;;
    *)
      OPENSSL_ARGS=(mingw64 no-autoload-config)
      WIN_SSLDIR="C:/Program Files/Common Files/SSL"
      SSL_DIRS=("$WIN_SSLDIR" "$WIN_SSLDIR/engines-3" "$WIN_SSLDIR/ossl-modules")
      ;;
  esac
  OPENSSL_DIRS=("OPENSSLDIR=${SSL_DIRS[0]}" "ENGINESDIR=${SSL_DIRS[1]}" "MODULESDIR=${SSL_DIRS[2]}")
  OPENSSL_ARGS+=(no-shared no-module no-tests --prefix="$PREFIX" --libdir=lib)
  {
    echo "# OpenSSL ${OPENSSL_VERSION} built by scripts/release/build-deps.sh on $(uname -sm)"
    echo "# $( (${CC:-cc} --version 2>/dev/null || gcc --version 2>/dev/null || true) | head -1 ); $(perl -e 'print "perl $^V"')"
    [ -z "${MSYSTEM:-}" ] || echo "# MSYS2 environment: ${MSYSTEM}"
    printf '%q ' ./Configure "${OPENSSL_ARGS[@]}"
    echo
    printf '%q ' make "${OPENSSL_DIRS[@]}"
    echo
  } > "$PREFIX/openssl-build-config.txt"
  ( cd "openssl-${OPENSSL_VERSION}" && ./Configure "${OPENSSL_ARGS[@]}" ) \
    > configure-openssl.log 2>&1 || { tail -40 configure-openssl.log; exit 1; }
  ( cd "openssl-${OPENSSL_VERSION}" && make -j"$(nproc)" "${OPENSSL_DIRS[@]}" \
      && make "${OPENSSL_DIRS[@]}" install_dev install_runtime ) \
    > make-openssl.log 2>&1 || { tail -40 make-openssl.log; exit 1; }
  cp "openssl-${OPENSSL_VERSION}/LICENSE.txt" "$PREFIX/LICENSE.openssl"
fi

# --- libcurl (STATIC; wolfSSL or OpenSSL as its TLS backend) -----------------
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
if [ "$FLAVOUR" = wolfssl ]; then
  # curl's FindWolfSSL picks the shared library (the only one in the prefix) and
  # records its absolute path in CURLTargets.cmake, so hem-tool's final link
  # imports libwolfssl rather than absorbing it.
  TLS_OPTS=(-DCURL_USE_WOLFSSL=ON -DCURL_USE_OPENSSL=OFF)
else
  # OpenSSL's archives from this prefix — never a system OpenSSL.
  TLS_OPTS=(-DCURL_USE_OPENSSL=ON -DCURL_USE_WOLFSSL=OFF -DOPENSSL_ROOT_DIR="$PREFIX"
            -DOPENSSL_USE_STATIC_LIBS=ON)
  case "$(uname -s)" in
    Linux*) ;;
    # libcurl asks OpenSSL for its configuration explicitly unless told not
    # to — no-autoload-config alone leaves that request (REQ-BUILD-009 rev 4).
    *) TLS_OPTS+=(-DCURL_DISABLE_OPENSSL_AUTO_LOAD_CONFIG=ON) ;;
  esac
fi
cmake -S "curl-${CURL_VERSION}" -B curl-build "${GEN[@]}" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DCMAKE_PREFIX_PATH="$PREFIX" -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DBUILD_CURL_EXE=OFF -DBUILD_TESTING=OFF \
  -DBUILD_LIBCURL_DOCS=OFF -DENABLE_CURL_MANUAL=OFF \
  "${TLS_OPTS[@]}" -DHTTP_ONLY=ON \
  -DCURL_USE_LIBPSL=OFF -DCURL_USE_LIBSSH2=OFF -DUSE_LIBIDN2=OFF \
  -DUSE_NGHTTP2=OFF -DCURL_ZLIB=OFF -DCURL_BROTLI=OFF -DCURL_ZSTD=OFF \
  -DENABLE_UNIX_SOCKETS=OFF "${CA_OPTS[@]}" > configure-curl.log 2>&1 \
  || { tail -40 configure-curl.log; exit 1; }
cmake --build curl-build > make-curl.log 2>&1 || { tail -40 make-curl.log; exit 1; }
cmake --install curl-build > /dev/null
cp "curl-${CURL_VERSION}/COPYING" "$PREFIX/COPYING.curl"

echo "$STAMP" > "$PREFIX/.stamp"
echo "built: $STAMP → $PREFIX"
if [ "$FLAVOUR" = wolfssl ]; then
  [ ! -e "$PREFIX/lib/libwolfssl.a" ] \
    || { echo "a STATIC libwolfssl.a is in the prefix — wolfSSL must be shared"; exit 1; }
  case "$(uname -s)" in
    Linux*)
      ls -l "$PREFIX"/lib/libwolfssl.so* "$PREFIX/lib/libcurl.a"
      readelf -d "$PREFIX/lib/libwolfssl.so" | grep -E 'SONAME|NEEDED'
      ;;
    *)
      ls -l "$PREFIX/bin/libwolfssl.dll" "$PREFIX/lib/libwolfssl.dll.a" \
            "$PREFIX/lib/libwolfssl.delay.a" "$PREFIX/lib/libcurl.a"
      DLLS="$(objdump -p "$PREFIX/bin/libwolfssl.dll" | awk '/DLL Name:/ {print tolower($3)}' | sort -u)"
      echo "libwolfssl.dll imports:"
      # shellcheck disable=SC2001  # indenting a multi-line value
      echo "$DLLS" | sed 's/^/  /'
      if echo "$DLLS" | grep -E -q 'libgcc|winpthread'; then
        echo "libwolfssl.dll depends on a MinGW runtime DLL — not shippable on its own"; exit 1
      fi
      # UCRT64: the C runtime is the Universal CRT, never msvcrt.dll.
      echo "$DLLS" | grep -E -q '^(api-ms-win-crt-|ucrtbase\.dll)' \
        || { echo "libwolfssl.dll imports no Universal CRT — not a UCRT64 build"; exit 1; }
      if echo "$DLLS" | grep -q -x 'msvcrt.dll'; then
        echo "libwolfssl.dll imports msvcrt.dll — not a UCRT64 build"; exit 1
      fi
      ;;
  esac
else
  # The -static variants link all of it in: archives only, no wolfSSL anywhere,
  # libcurl on OpenSSL.
  ls -l "$PREFIX/lib/libssl.a" "$PREFIX/lib/libcrypto.a" "$PREFIX/lib/libcurl.a"
  if find "$PREFIX" \( -name '*.so' -o -name '*.so.*' -o -name '*.dll' -o -name '*.dll.a' \
                      -o -iname '*wolfssl*' \) | grep -q .; then
    echo "a shared object, a DLL or a wolfSSL file is in the OpenSSL prefix"; exit 1
  fi
  grep -q -E -- '-lssl|-lcrypto|openssl|libssl' "$PREFIX/lib/pkgconfig/libcurl.pc" \
    || { echo "libcurl is not built on OpenSSL"; exit 1; }
  # The directories the library carries, from its own report: the chosen ones,
  # never <prefix> (REQ-BUILD-009 rev 4).
  "$PREFIX/bin/openssl" version -v -d -e -m
  DIRS_SEEN="$("$PREFIX/bin/openssl" version -d -e -m | tr -d '\r')"
  DIRS_WANT="$(printf 'OPENSSLDIR: "%s"\nENGINESDIR: "%s"\nMODULESDIR: "%s"' "${SSL_DIRS[@]}")"
  [ "$DIRS_SEEN" = "$DIRS_WANT" ] \
    || { printf 'OpenSSL carries other directories than chosen:\n%s\n' "$DIRS_SEEN"; exit 1; }
  case "$(uname -s)" in
    Linux*) ;;
    *)
      # No configuration on Windows: neither OpenSSL nor libcurl loads one.
      grep -q '^# *define OPENSSL_NO_AUTOLOAD_CONFIG' "$PREFIX/include/openssl/configuration.h" \
        || { echo "OpenSSL is not built with no-autoload-config"; exit 1; }
      grep -q '^#define CURL_DISABLE_OPENSSL_AUTO_LOAD_CONFIG 1' "$WORK/curl-build/lib/curl_config.h" \
        || { echo "libcurl is not built with CURL_DISABLE_OPENSSL_AUTO_LOAD_CONFIG"; exit 1; }
      ;;
  esac
fi
