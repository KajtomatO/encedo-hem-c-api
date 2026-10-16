# encedo-hem-c-api

A portable **C99 client library** (the *Encedo HEM C SDK*, symbol prefix
`ehem_`) for driving the [Encedo HEM](https://ence.do) network cryptographic
device over its REST/HTTPS API. Its first consumer is the `encedo-pkcs11`
module.

> **Status: 1.2.0 release candidate (2026-10-16)** — the latest release is
> [1.1.0](https://github.com/KajtomatO/encedo-hem-c-api/releases/latest)
> (2026-10-10). The SDK covers the device API end to end: the full eJWT
> auth flow (passphrase and mobile push-confirmation), device
> initialisation and factory reset, key management
> (list/search/get/create/delete/update/import/derive), every crypto
> operation (ExDSA sign/verify, ECDH, HMAC, AES incl. wrap/unwrap,
> ML-KEM, ML-DSA, hardware random), system/logger/storage bindings, and
> the TLS-lifecycle tooling (check-in, cert-install, TLS re-provisioning,
> and hem-tool's one-command `recovery`) — each binding verified against
> a real HEM (init and wipe in attended runs only).
> **New in 1.2** (M12): the SDK checks the crypto library it loads and
> refuses a foreign one (`EHEM_ERR_BACKEND_MISMATCH` — 1.1.0's hem-tool
> crashed on Ubuntu's wolfSSL), and reports how it was built
> (`ehem_build_info_get`); an **OpenSSL 3 backend** beside wolfSSL; build
> switches for the library variants and link modes; an installed
> `encedo-hem.pc`; hem-tool `--version`, `keys types` and `init-wizard`,
> and a message instead of a crash or a silent stop when its wolfSSL is
> wrong or missing (exit 12). Releases add a one-file **`-static`
> hem-tool** (OpenSSL, no companion library) and **SDK library
> packages** — see [Download](#download).
> [docs/COVERAGE.md](docs/COVERAGE.md) maps every documented endpoint to
> its binding; what is still unbound (firmware/UI upgrade, factory
> provisioning, diagnostics) is parked in [BACKLOG.md](BACKLOG.md). The
> 1.0 ABI is unchanged in 1.2 (SONAME `.so.1`, additive only). Next
> milestone: none planned yet — candidates are parked in BACKLOG.md.
> See [ARCHITECTURE.md](ARCHITECTURE.md) for the design and
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
  (both backends, the build switches), the backend-check seam and the
  installed-package gate.
- **[BACKLOG.md](BACKLOG.md)** — unscheduled items (firmware/UI upgrade,
  provisioning, diagnostics) parked outside any milestone.
- **`hem-tool`** — the bundled CLI is the living usage documentation:
  every binding is drivable from it (see below).

## Dependencies

| Need | Purpose |
|------|---------|
| CMake ≥ 3.20 + CTest | build system |
| A C99 compiler (GCC/Clang on Linux, MinGW-w64 on Windows) | — |
| [libcurl](https://curl.se/libcurl/) dev | default HTTPS transport |
| [CMocka](https://cmocka.org/) dev | unit tests |
| wolfSSL dev — **or** OpenSSL ≥ 3.0 dev | crypto primitives (X25519, HMAC, PBKDF2, cert parsing, local verify); wolfSSL is the default, OpenSSL the alternative backend (`EHEM_CRYPTO_BACKEND=openssl`) |

cJSON is **vendored into the source tree** (no system install), so it is
not listed above; the CLI additionally vendors the single-file qrcodegen
(tool-only — never part of the library).

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
| `-DCMAKE_BUILD_TYPE=Debug\|Release` | build type (defaults to `Debug`) |
| `-DEHEM_SANITIZE=ON` | build the suite with ASan + UBSan (non-MSVC) |
| `-DBUILD_TESTING=OFF` | build only the library, skip the tests |
| `-DEHEM_CRYPTO_BACKEND=wolfssl\|openssl` | the crypto library behind the SDK (default `wolfssl`; `openssl` = OpenSSL 3) |
| `-DEHEM_BUILD_SHARED=OFF` / `-DEHEM_BUILD_STATIC=OFF` | build and install only the other library variant (default: both) |
| `-DEHEM_TOOL_LINK=static\|shared` | how hem-tool links the SDK (default `static`; `shared` needs the shared variant) |
| `-DEHEM_DEPS_LINK=default\|static\|shared` | how libcurl and the crypto library link (default: as found) — use a fresh build directory when changing it |
| `-DEHEM_BUILD_TOOL=OFF` | skip hem-tool (library only) |

wolfSSL is GPL-licensed: configuring a build that would link it statically
prints a CMake warning naming the licensing consequence, and the release
scripts refuse to package such a build (ARCHITECTURE.md §1). The `-static`
release assets use OpenSSL (Apache-2.0) instead.

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

- **`disruptive`** — mutates device availability or state (reboot,
  certificate install, storage unlock/lock). Run deliberately and attended, never
  in unattended CI; requires both the label and `EHEM_ALLOW_DISRUPTIVE=1`.
  Example: `test_cert_install_live` (the `hem-tool cert-install` flow — it
  may reboot the device). Device **wipe and initialisation have no test at
  all**, not even `disruptive` — they are verified in attended runs only
  (REQ-TEST-007); their offline unit tests still run under `unit`.

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
                                     # --tool-link static|shared, --deps default|static|shared
./dev test                           # unit suite (ctest -L unit)
./dev test it [-d|--disruptive]      # integration (device); -d adds the disruptive label
./dev test all [-d]                  # unit + integration (+ disruptive with -d)
./dev test asan                      # ASan/LSan unit run (gcc)
./dev check                          # export-symbol + public-header gates only
./dev ci [--openssl]                 # mirror the CI matrix locally (gcc + clang)
./dev tool status                    # run the built hem-tool with the device env loaded
./dev install-dependencies [--yes]   # apt (Linux) / pacman (MSYS2)
source <(./dev completions)          # bash tab-completion for the above
```

The default compiler is `gcc`; set `EHEM_DEV_CC=clang` to switch it. Commands
that need the device (`test it`, `test all`, `tool`) use the `EHEM_*` variables
when set, otherwise auto-source a git-ignored `./hem.env` (and say so on
stderr). See `./dev help` for the full surface.

## Download

**Latest:** [GitHub Releases](https://github.com/KajtomatO/encedo-hem-c-api/releases/latest).

Prebuilt `hem-tool` binaries and SDK library packages come from the release
workflow (`.github/workflows/release.yml`; how to cut a release:
[docs/RELEASING.md](docs/RELEASING.md)): every push to `main` uploads them as
workflow artifacts (version suffixed `+g<sha>`), and a `v*` tag publishes
them as a GitHub Release together with `SHA256SUMS` (a `v<version>-rc<N>`
tag publishes a pre-release; the release text is the annotated tag's
message). Linux and Windows (x86_64) each get:

| Asset | Contents |
|---|---|
| `hem-tool-<version>-<os>-x86_64.{tar.gz,zip}` | hem-tool with libcurl compiled in and **wolfSSL linked dynamically** — needs the wolfSSL asset below; `README.txt`, `LICENSES/` (MIT SDK/tool, curl, cJSON, BIP39 wordlist, qrcodegen) |
| `hem-tool-<version>-<os>-x86_64-static.{tar.gz,zip}` *(1.2+)* | hem-tool as **one file**: OpenSSL and libcurl linked in, no companion library; `README.txt`, `LICENSES/` (the above + OpenSSL's Apache-2.0) |
| `encedo-hem-<version>-<os>-x86_64.{tar.gz,zip}` *(1.2+)* | the SDK as a **shared library** (libcurl inside, wolfSSL dynamic — needs the wolfSSL asset): headers, `libencedo-hem.so.*` / `libencedo-hem.dll` + `.dll.a`, CMake package config, `encedo-hem.pc` |
| `encedo-hem-<version>-<os>-x86_64-static.{tar.gz,zip}` *(1.2+)* | the SDK as a **static library** on OpenSSL, with the `libcurl.a`, `libssl.a`, `libcrypto.a` it was built against and their headers, CMake package configs and `.pc` files |
| `wolfssl-<wv>-<os>-x86_64.{tar.gz,zip}` | the wolfSSL companion: `libwolfssl.so.<N>` / `libwolfssl.dll`, wolfSSL's `COPYING` (GPLv2 or later), `BUILD-CONFIG.txt`, `README.txt` |
| `wolfssl-<wv>-stable-src.tar.gz` | the unmodified upstream source the wolfSSL library was built from (once per release) |
| `SHA256SUMS` | over all of the above |

**Which one do I want?**

- **Just the CLI, simplest:** `hem-tool-…-static`. One file, nothing to put
  beside it; TLS and crypto through OpenSSL. On Linux it needs glibc (its
  README names the oldest version) and reads `/etc/ssl/openssl.cnf`; on
  Windows it reads no OpenSSL configuration at all and uses the Windows
  certificate store.
- **The CLI on wolfSSL:** `hem-tool-…` plus the matching `wolfssl-…` asset;
  put the library next to the binary — hem-tool looks in its own directory
  first, then the system's library path (a system wolfSSL with the same
  soname and build configuration works too). From 1.2 a different wolfSSL
  is refused with one message and exit code 12, and on Windows so is a
  missing `libwolfssl.dll` (1.1.0 crashed on a different library, and on
  Windows stopped before its own code ran, exit `0xC0000135`).
- **Linking the SDK into your program:** `encedo-hem-…-static` for a
  self-contained static link (`find_package(encedo-hem)` →
  `encedo-hem::encedo-hem-static`, or `pkg-config --static`), or
  `encedo-hem-…` for the shared library, shipped with the `wolfssl-…`
  library beside it (`encedo-hem::encedo-hem`, or `pkg-config`).

No published package links wolfSSL statically (licensing; ARCHITECTURE.md
§1): wolfSSL is always the separate download, with its source. Verify
downloads before running anything:

```sh
sha256sum -c SHA256SUMS            # Linux
CertUtil -hashfile <file> SHA256    # Windows
```

## hem-tool

The bundled CLI drives every binding through the public API. Commands are
grouped by what they need — none (`status`, `checkin`, `init-device`,
`init-wizard`, `keys types`), any bearer
(`keys`, `sign`, `random`, `logs`, `selftest`, `reboot`, `ext list/login`),
or a passphrase only (`ext pair` — the device demands `sub="U"` for pairing
changes; `wipe-device` and `recovery` — config writes, `sub="U"`/`"M"`) — followed by a
"manual recovery" section holding the building
blocks of `recovery` (`cert-install`, `tls-recover`; passphrase only, since
config writes demand `sub="U"`/`"M"`):

```bash
hem-tool status                     # no --url needed: defaults to https://my.ence.do
hem-tool keys list --mobile         # any bearer command can push to the paired
                                    # phone instead of taking a passphrase
hem-tool help keys rm               # per-command help (== hem-tool keys rm --help)
hem-tool recovery                   # diagnose + fix TLS / certificate / clock trouble
hem-tool keys types                 # what each key type is for (no device needed)
hem-tool --version                  # SDK version; crypto library and libcurl,
                                    # compiled against vs loaded, and how linked
```

Device lifecycle (attended — both change the device's identity):

```bash
hem-tool wipe-device --wait         # factory reset; type the hostname to confirm
hem-tool --url http://<host> init-device --passphrase P \
         --user U --email E --hostname H --master-generate
                                    # personalise a wiped device: P becomes its
                                    # user password; the 24 master words are
                                    # printed ONCE - keep them
hem-tool --url http://<host> init-wizard
                                    # (1.2) the same, one explained question per
                                    # setting, passphrase typed twice, words checked
```

Connection comes from `--url`/`EHEM_URL` (default `https://my.ence.do` —
a one-line notice tells you when the default kicked in), credentials from
`--passphrase`/`EHEM_PASSPHRASE` or `--mobile` (push confirmation;
`--timeout SEC` bounds the wait; exit 13 = no answer, 14 = rejected on
the phone). `hem-tool --help` shows the grouped command summary.

Exit codes: 0 success, 1 failure (the message says which), 2 usage error;
command-specific codes are listed in each command's help (e.g.
`init-device`: 3 already initialised, 4 clock unset, 5 configuration
rejected; `init-wizard` adds 6 = cancelled). **12** (1.2) = the SDK could
not be initialised — the wolfSSL library loaded is not the one hem-tool
was built for, or (Windows) `libwolfssl.dll` is missing; for the `-static`
hem-tool, an OpenSSL configuration (`OPENSSL_CONF` / `openssl.cnf`, Linux)
left it without algorithms. The message names the versions and the fix.
hem-tool checks this before any command, so it never crashes on a foreign
library.

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
src/            library sources (+ vendored cjson and BIP39 wordlist, tools/hem-tool);
                crypto_shim_wolfssl.c / crypto_shim_openssl.c = the two backends
tests/          unit/ · integration/ · disruptive/ · consumer/ (installed-package
                gate) · support/ (incl. crypto_ref, the per-backend test oracle)
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
that link it must comply accordingly — the published hem-tool links it
dynamically, and no published package bundles it statically
(ARCHITECTURE.md §1, §12 risk 1). The `-static` packages contain OpenSSL
(Apache-2.0) and libcurl (curl license); their notices are in each
archive's `LICENSES/`.
