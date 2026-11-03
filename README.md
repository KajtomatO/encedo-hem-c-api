# encedo-hem-c-api

A portable **C99 client library** (the *Encedo HEM C SDK*, symbol prefix
`ehem_`) for driving the [Encedo HEM](https://ence.do) network cryptographic
device over its REST/HTTPS API. Its first consumer is the `encedo-pkcs11`
module.

> **Status: 1.2.1 (2026-11-03)** — the latest release is
> [1.2.1](https://github.com/KajtomatO/encedo-hem-c-api/releases/latest)
> (M13 gate passed 2026-11-03; tested as `v1.2.1-rc2`).
> 1.2.1 is a **patch**: `hem-tool`, the command-line
> client, has moved to its own repository,
> [encedo-hem-tool](https://github.com/KajtomatO/encedo-hem-tool)
> (versions of its own; `c-v1.3.0`, released 2026-10-28, is built on this
> SDK's 1.2.0 package), so this repository builds, tests and releases the
> **SDK libraries only** — 8 release assets instead of 12 — and gains
> runnable [examples](examples/), the test support as a linkable target
> (`ehem::test-support`) with a gated `add_subdirectory` consumer shape,
> and `tls-probe` as the release job's certificate-classifier driver.
> Nothing under `include/` changes: the 1.0 ABI (SONAME `.so.1`) stands.
> The SDK covers the device API end to end: the full eJWT auth flow
> (passphrase and mobile push-confirmation), device initialisation and
> factory reset, key management
> (list/search/get/create/delete/update/import/derive), every crypto
> operation (ExDSA sign/verify, ECDH, HMAC, AES incl. wrap/unwrap,
> ML-KEM, ML-DSA, hardware random), system/logger/storage bindings and
> the TLS-lifecycle tooling (check-in, cert-install, TLS re-provisioning)
> — each binding verified against a real HEM (init and wipe in attended
> runs only). **1.2** (M12) added the crypto-library check at init
> (`EHEM_ERR_BACKEND_MISMATCH` instead of a crash on a foreign wolfSSL),
> `ehem_build_info_get`, the **OpenSSL 3 backend** beside wolfSSL, the
> library-variant and link-mode build switches, an installed
> `encedo-hem.pc`, and `ehem_ext_qr_payload` (the pairing QR payload the
> new Encedo Auth app checks).
> [docs/COVERAGE.md](docs/COVERAGE.md) maps every documented endpoint to
> its binding; what is still unbound (firmware/UI upgrade, factory
> provisioning, diagnostics) is parked in [BACKLOG.md](BACKLOG.md). See
> [ARCHITECTURE.md](ARCHITECTURE.md) for the design and
> [REQUIREMENTS-MANAGEMENT.md](REQUIREMENTS-MANAGEMENT.md) for how the
> work is tracked.

## Documentation

- **[docs/API-GUIDE.md](docs/API-GUIDE.md)** — start here: conventions
  (error model, ownership, auth modes, scopes, TLS, automatic
  recoveries), a per-header symbol index, and worked examples. The
  public headers in `include/ehem/` are the per-symbol reference — every
  contract is documented at the declaration, and a scripted gate keeps
  the guide's index complete.
- **[docs/COVERAGE.md](docs/COVERAGE.md)** — endpoint-by-endpoint
  conformance record against the device API documentation.
- **[KNOWN-ISSUES.md](KNOWN-ISSUES.md)** — firmware bugs and doc
  divergences the SDK works around (device behavior wins), plus the
  known issues of the released binaries.
- **[docs/RELEASING.md](docs/RELEASING.md)** — how a release is cut:
  the variants and their dependency prefixes, rc pre-releases, the final
  tag, the release notes, the checks.
- **[tests/README.md](tests/README.md)** — the test labels, the CI legs
  (both backends, the build switches), the backend-check seam, the
  installed-package and subdirectory consumer gates, `ehem::test-support`.
- **[BACKLOG.md](BACKLOG.md)** — unscheduled items (firmware/UI upgrade,
  provisioning, diagnostics) parked outside any milestone.
- **[examples/](examples/)** — `status.c` and `sign.c`, the runnable
  examples (explained in the guide's "Examples and hem-tool").
- **hem-tool** — the full command-line client, in its own repository since
  1.2.1: [encedo-hem-tool](https://github.com/KajtomatO/encedo-hem-tool)
  (see below).

## Dependencies

| Need | Purpose |
|------|---------|
| CMake ≥ 3.20 + CTest | build system |
| A C99 compiler (GCC/Clang on Linux, MinGW-w64 on Windows) | — |
| [libcurl](https://curl.se/libcurl/) dev | default HTTPS transport |
| [CMocka](https://cmocka.org/) dev | unit tests |
| wolfSSL dev — **or** OpenSSL ≥ 3.0 dev | crypto primitives (X25519, HMAC, PBKDF2, cert parsing, local verify); wolfSSL is the default, OpenSSL the alternative backend (`EHEM_CRYPTO_BACKEND=openssl`) |

cJSON is **vendored into the source tree** (no system install), so it is
not listed above.

### Install them automatically

```bash
# Linux (apt / dnf / pacman / zypper — apt is first-class)
./scripts/install-deps-linux.sh            # add --no-crypto to skip wolfSSL

# Windows (from an elevated PowerShell — bootstraps MSYS2 + MinGW-w64)
powershell -ExecutionPolicy Bypass -File scripts\install-deps-windows.ps1
```

Both scripts are idempotent and print a verification summary at the end.
They install wolfSSL; for the OpenSSL backend add `libssl-dev` (apt) or
`mingw-w64-ucrt-x86_64-openssl` (MSYS2 UCRT64).

## Build

```bash
cmake -B build            # configure (add -G Ninja for the Ninja generator)
cmake --build build       # compile static + shared libs and the tests
```

By default this produces both library variants:

- `libencedo-hem.so` / `libencedo-hem.dll` — shared, exports only `ehem_*`
- `libencedo-hem.a` — static (the safe path for embedding in a PKCS#11 module)

Useful configure options:

| Option | Effect |
|--------|--------|
| `-DCMAKE_BUILD_TYPE=Debug\|Release` | build type (defaults to `Debug` in a top-level build; a project adding the SDK as a subdirectory sets its own) |
| `-DEHEM_SANITIZE=ON` | build the suite with ASan + UBSan (non-MSVC) |
| `-DBUILD_TESTING=OFF` | build only the library, skip the tests |
| `-DEHEM_CRYPTO_BACKEND=wolfssl\|openssl` | the crypto library behind the SDK (default `wolfssl`; `openssl` = OpenSSL 3) |
| `-DEHEM_BUILD_SHARED=OFF` / `-DEHEM_BUILD_STATIC=OFF` | build and install only the other library variant (default: both) |
| `-DEHEM_DEPS_LINK=default\|static\|shared` | how libcurl and the crypto library link (default: as found) — use a fresh build directory when changing it |
| `-DEHEM_BUILD_EXAMPLES=OFF` | skip `examples/` (default: on in a top-level build, off when the SDK is a subdirectory of your project) |
| `-DEHEM_BUILD_TEST_SUPPORT=ON` | build `ehem::test-support` (the fake transport and the test seams) without the SDK's own tests — for a project that adds the SDK with `add_subdirectory` and tests against it (1.2.1) |

wolfSSL is GPL-licensed: configuring a build that would link it statically
prints a CMake warning naming the licensing consequence, and the release
scripts refuse to package such a build (ARCHITECTURE.md §1). The static
SDK package uses OpenSSL (Apache-2.0) instead — and so does
encedo-hem-tool's one-file binary, built on that package.

### Windows (MSYS2 / MinGW-w64)

Open the **“MSYS2 UCRT64”** shell (after running the install script) and build
exactly as above — UCRT64 is the environment CI uses; MSYS2 deprecated
MINGW64:

```bash
cmake -B build -G Ninja && cmake --build build
```

Or, to build **from an ordinary PowerShell prompt** (no MSYS2 shell needed),
use the helper scripts — they run cmake/ctest inside the MSYS2 UCRT64
environment for you, so libcurl/wolfSSL are found:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1   # configure + build
powershell -ExecutionPolicy Bypass -File scripts\test-windows.ps1    # ctest -L unit
```

A plain `cmake -B build` from PowerShell often picks up an unrelated
cmake/gcc on `PATH` (e.g. Strawberry Perl's), which has no libcurl and fails
with `Could NOT find CURL`. `build-windows.ps1` avoids that, and wipes a build
directory that was accidentally configured with the wrong compiler. Both take
`-BuildDir`, `-MsysRoot`, and `-Env` (ucrt64, the default, or the deprecated
mingw64 for an existing setup); `test-windows.ps1` takes
`-Label unit|integration|disruptive`. Pass `-?` for full help.

### Cross-compiling for Windows from Linux

```bash
cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw-w64.cmake
cmake --build build-win
```

## Run the tests

Tests are partitioned into CTest **labels** (see ARCHITECTURE.md §9):

```bash
# Unit suite — offline, no device, runs on every build:
ctest --test-dir build -L unit --output-on-failure
```

- **`unit`** — CMocka tests plus a scripted check that the shared library
  exports only `ehem_*` symbols. These never touch the network.
- **`integration`** — real-device round-trips over the whole surface,
  **gated on the `EHEM_TEST_URL` environment variable**. With it unset the
  integration tests skip, so a checkout without a device stays green:

  ```bash
  EHEM_TEST_URL="https://my-hem.example" EHEM_TEST_PASSPHRASE="…" \
      ctest --test-dir build -L integration --output-on-failure
  ```

- **`disruptive`** — mutates device availability or state (storage
  unlock/lock, a dangling broker registration, the key-generation matrix
  that stalls the dev firmware). Run deliberately and attended, never in
  unattended CI; requires both the label and `EHEM_ALLOW_DISRUPTIVE=1`.
  Device **wipe and initialisation have no test at all**, not even
  `disruptive` — they are verified in attended runs only (REQ-TEST-007);
  their offline unit tests still run under `unit`.

  ```bash
  EHEM_TEST_URL="https://my-hem.example" EHEM_TEST_PASSPHRASE="…" \
      EHEM_ALLOW_DISRUPTIVE=1 ctest --test-dir build -L disruptive
  ```

## Developer workflow (`./dev`)

`./dev` is an optional convenience wrapper over the same `cmake`/`ctest`
invocations shown above (and in CI) — it is developer tooling only, never
required: the plain commands keep working without it.

```bash
./dev build [--gcc|--clang|--both]   # configure + build (per-compiler build dirs)
./dev build --openssl                # the same on the OpenSSL backend (build-openssl/);
                                     # also --static-only, --shared-only,
                                     # --deps default|static|shared
./dev test                           # unit suite (ctest -L unit)
./dev test it [-d|--disruptive]      # integration (device); -d adds the disruptive label
./dev test all [-d]                  # unit + integration (+ disruptive with -d)
./dev test asan                      # ASan/LSan unit run (gcc)
./dev check                          # export-symbol + public-header gates only
./dev ci [--openssl]                 # mirror the CI matrix locally (gcc + clang)
./dev install-dependencies [--yes]   # apt (Linux) / pacman (MSYS2)
source <(./dev completions)          # bash tab-completion for the above
```

The default compiler is `gcc`; set `EHEM_DEV_CC=clang` to switch it. Commands
that need the device (`test it`, `test all`) use the `EHEM_*` variables when
set, otherwise auto-source a git-ignored `./hem.env` (and say so on stderr).
See `./dev help` for the full surface. (`./dev tool` left with hem-tool;
encedo-hem-tool's own `c/dev tool` runs the built tool with the device
environment loaded.)

## Download

**Latest:** [GitHub Releases](https://github.com/KajtomatO/encedo-hem-c-api/releases/latest).

The SDK library packages come from the release workflow
(`.github/workflows/release.yml`; how to cut a release:
[docs/RELEASING.md](docs/RELEASING.md)): every push to `main` uploads them as
workflow artifacts (version suffixed `+g<sha>`), and a `v*` tag publishes
them as a GitHub Release together with `SHA256SUMS` (a `v<version>-rc<N>`
tag publishes a pre-release; the release text is the annotated tag's
message). Linux and Windows (x86_64) each get — 8 assets per release:

| Asset | Contents |
|---|---|
| `encedo-hem-<version>-<os>-x86_64.{tar.gz,zip}` | the SDK as a **static library** on OpenSSL, with the `libcurl.a`, `libssl.a`, `libcrypto.a` it was built against and their headers, CMake package configs and `.pc` files — self-contained; `README.txt`, `LICENSES/` (MIT SDK, OpenSSL's Apache-2.0, curl, cJSON, BIP39 wordlist) |
| `encedo-hem-<version>-<os>-x86_64-shared.{tar.gz,zip}` | the SDK as a **shared library** (libcurl inside, **wolfSSL linked dynamically** — needs the wolfSSL asset below): headers, `libencedo-hem.so.*` / `libencedo-hem.dll` + `.dll.a`, CMake package config, `encedo-hem.pc`, `README.txt`, `LICENSES/` |
| `wolfssl-<wv>-<os>-x86_64.{tar.gz,zip}` | the wolfSSL companion: `libwolfssl.so.<N>` / `libwolfssl.dll`, wolfSSL's `COPYING` (GPLv2 or later), `BUILD-CONFIG.txt`, `README.txt` |
| `wolfssl-<wv>-stable-src.tar.gz` | the unmodified upstream source the wolfSSL library was built from (once per release) |
| `SHA256SUMS` | over all of the above |

**Which one do I want?**

- **Linking the SDK into your program:** `encedo-hem-…-x86_64` (no suffix)
  for a self-contained static link (`find_package(encedo-hem)` →
  `encedo-hem::encedo-hem-static`, or `pkg-config --static`; on Linux it
  reads `/etc/ssl/openssl.cnf`, on Windows no OpenSSL configuration at
  all), or `encedo-hem-…-shared` for the shared library, shipped with the
  `wolfssl-…` library beside it (`encedo-hem::encedo-hem`, or `pkg-config`).
  A different wolfSSL than the one the library was built against is refused
  at `ehem_global_init()` with `EHEM_ERR_BACKEND_MISMATCH` instead of a
  crash (1.2).
- **The command-line client:** `hem-tool` is released from
  [encedo-hem-tool](https://github.com/KajtomatO/encedo-hem-tool/releases)
  — one file per platform (OpenSSL and libcurl linked in, nothing to put
  beside it), built on the plain package of an SDK release.

**Until 1.2.0** the releases carried hem-tool as well (12 assets: the
one-file binary under the plain name, the wolfSSL build as `-shared`); from
1.2.1 the SDK and the tool are released separately, each with its own
version.

No published package links wolfSSL statically (licensing; ARCHITECTURE.md
§1): wolfSSL is always the separate download, with its source. Verify
downloads before running anything:

```sh
sha256sum -c SHA256SUMS            # Linux
CertUtil -hashfile <file> SHA256    # Windows
```

## Examples and hem-tool

`examples/status.c` (the device's status under system trust, with the
SDK's error detail) and `examples/sign.c` (passphrase login, a key found
by its label, a signature) are the SDK's runnable examples — public API
only, built with the SDK (`EHEM_BUILD_EXAMPLES`), explained in
[docs/API-GUIDE.md](docs/API-GUIDE.md#examples-and-hem-tool):

```bash
build/examples/status https://my-hem.example
EHEM_URL=https://my-hem.example EHEM_PASSPHRASE='…' build/examples/sign my-signing-key "hello"
```

**hem-tool**, the full-featured command-line client, lives in
[encedo-hem-tool](https://github.com/KajtomatO/encedo-hem-tool) since
1.2.1 (until 1.2.0 it was built and released from here). It drives every
binding through this public API — `hem-tool help` groups the commands by
what they need, `hem-tool --version` prints the SDK version it embeds —
and is released as one file per platform, built on the plain, static SDK
package of a release. Its requirements (the `REQ-TOOL-*` ids), tests and
history moved with it; the copies under `requirements/` here are frozen,
superseded records.

## Install

```bash
cmake --install build --prefix /your/prefix
```

Downstream CMake projects then consume the SDK via its exported target — the
package config finds libcurl and the crypto library (wolfSSL or OpenSSL, as
built) for the static target itself:

```cmake
find_package(encedo-hem CONFIG REQUIRED)
target_link_libraries(app PRIVATE encedo-hem::encedo-hem)         # shared
# or, for embedding:
target_link_libraries(app PRIVATE encedo-hem::encedo-hem-static)  # static
```

Other build systems use pkg-config (`lib/pkgconfig/encedo-hem.pc`, 1.2+,
relocatable with the install; `--static` adds libcurl and the crypto library
for the static library):

```bash
cc app.c $(pkg-config --cflags --libs encedo-hem)
```

## Layout

```
include/ehem/   public headers (ehem_ prefix) — the per-symbol reference
docs/           API-GUIDE.md · COVERAGE.md · RELEASING.md
src/            library sources (+ vendored cjson and BIP39 wordlist);
                crypto_shim_wolfssl.c / crypto_shim_openssl.c = the two backends
examples/       status.c · sign.c — the runnable examples (EHEM_BUILD_EXAMPLES)
tests/          unit/ · integration/ · disruptive/ · consumer/ (the installed-package
                gate and subdir/, the add_subdirectory consumer gate) · release/
                (tls-probe, the release job's classifier driver) · support/
                (ehem::test-support: fake transport, ext_sim, crypto_ref — the
                per-backend test oracle —, fixtures)
cmake/          MinGW toolchain, package config, encedo-hem.pc
scripts/        dependency installers, Windows build helpers, release/ (CI release
                builds: pinned deps, packaging and checks per variant)
.github/        workflows: ci.yml (unit CI), release.yml (release builds)
requirements/   REQ-*.md + generated TRACE.md
workplan/        todo/ · doing/ · done/ step files
```

## License

MIT — see [LICENSE](LICENSE). Written from scratch (no code derived from GPL
PKCS#11 implementations). wolfSSL is GPLv2-or-later/commercial
dual-licensed (per the pinned release's `COPYING`/`LICENSING`); binaries
that link it must comply accordingly — the `-shared` SDK package links it
dynamically, and no published package bundles it statically
(ARCHITECTURE.md §1, §12 risk 1). The static SDK package contains OpenSSL
(Apache-2.0) and libcurl (curl license); the notices are in each archive's
`LICENSES/`.
