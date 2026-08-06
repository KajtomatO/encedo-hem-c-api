# encedo-hem-c-api

A portable **C99 client library** (the *Encedo HEM C SDK*, symbol prefix
`ehem_`) for driving the [Encedo HEM](https://ence.do) network cryptographic
device over its REST/HTTPS API. Its first consumer is the `encedo-pkcs11`
module.

> **Status: 1.0 track.** The SDK covers the device API end to end: the
> full eJWT auth flow (passphrase and mobile push-confirmation), key
> management (list/search/get/create/delete/update/import/derive), every
> crypto operation (ExDSA sign/verify, ECDH, HMAC, AES incl. wrap/unwrap,
> ML-KEM, ML-DSA, hardware random), system/logger/storage bindings, and
> the TLS-lifecycle tooling (check-in, cert-install, full recovery) —
> each binding live-verified against a real HEM.
> [docs/COVERAGE.md](docs/COVERAGE.md) maps every documented endpoint to
> its binding; the deliberately-unbound remainder (device provisioning,
> the firmware-upgrade family) is recorded there and scheduled post-1.0.
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
  divergences the SDK works around (device behavior wins).
- **`hem-tool`** — the bundled CLI is the living usage documentation:
  every binding is drivable from it (see below).

## Dependencies

| Need | Purpose |
|------|---------|
| CMake ≥ 3.20 + CTest | build system |
| A C99 compiler (GCC/Clang on Linux, MinGW-w64 on Windows) | — |
| [libcurl](https://curl.se/libcurl/) dev | default HTTPS transport |
| [CMocka](https://cmocka.org/) dev | unit tests |
| wolfSSL dev | crypto primitives (X25519, HMAC, PBKDF2, cert parsing, local verify) |

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

## Build

```bash
cmake -B build            # configure (add -G Ninja for the Ninja generator)
cmake --build build       # compile static + shared libs and the tests
```

This produces both library variants:

- `libencedo-hem.so` / `libencedo-hem.dll` — shared, exports only `ehem_*`
- `libencedo-hem.a` — static (the safe path for embedding in a PKCS#11 module)

Useful configure options:

| Option | Effect |
|--------|--------|
| `-DCMAKE_BUILD_TYPE=Debug\|Release` | build type (defaults to `Debug`) |
| `-DEHEM_SANITIZE=ON` | build the suite with ASan + UBSan (non-MSVC) |
| `-DBUILD_TESTING=OFF` | build only the library, skip the tests |

### Windows (MSYS2 / MinGW-w64)

Open the **“MSYS2 MINGW64”** shell (after running the install script) and build
exactly as above:

```bash
cmake -B build -G Ninja && cmake --build build
```

Or, to build **from an ordinary PowerShell prompt** (no MSYS2 shell needed),
use the helper scripts — they run cmake/ctest inside the MinGW64 environment
for you, so libcurl/wolfSSL are found:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1   # configure + build
powershell -ExecutionPolicy Bypass -File scripts\test-windows.ps1    # ctest -L unit
```

A plain `cmake -B build` from PowerShell often picks up an unrelated
cmake/gcc on `PATH` (e.g. Strawberry Perl's), which has no libcurl and fails
with `Could NOT find CURL`. `build-windows.ps1` avoids that, and wipes a build
directory that was accidentally configured with the wrong compiler. Both take
`-BuildDir`, `-MsysRoot`, and `-Env` (mingw64|ucrt64); `test-windows.ps1` takes
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

- **`disruptive`** — mutates device availability or state (reboot / firmware /
  wipe). Run deliberately and attended, never in unattended CI; requires
  both the label and `EHEM_ALLOW_DISRUPTIVE=1`. First such test:
  `test_cert_install_live` (the `hem-tool cert-install` flow — it may reboot
  the device).

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
./dev test                           # unit suite (ctest -L unit)
./dev test it [-d|--disruptive]      # integration (device); -d adds the disruptive label
./dev test all [-d]                  # unit + integration (+ disruptive with -d)
./dev test asan                      # ASan/LSan unit run (gcc)
./dev check                          # export-symbol + public-header gates only
./dev ci                             # mirror the CI matrix locally (gcc + clang)
./dev tool status                    # run the built hem-tool with the device env loaded
./dev install-dependencies [--yes]   # apt (Linux) / pacman (MSYS2)
source <(./dev completions)          # bash tab-completion for the above
```

The default compiler is `gcc`; set `EHEM_DEV_CC=clang` to switch it. Commands
that need the device (`test it`, `test all`, `tool`) use the `EHEM_*` variables
when set, otherwise auto-source a git-ignored `./hem.env` (and say so on
stderr). See `./dev help` for the full surface.

## hem-tool

The bundled CLI drives every binding through the public API. Commands are
grouped by what they need — none (`status`, `checkin`), any bearer
(`keys`, `sign`, `random`, `logs`, `selftest`, `cert-install`, `reboot`,
`tls-recover`, `ext list/login`), or a passphrase only (`ext pair` — the
device demands `sub="U"` for pairing changes):

```bash
hem-tool status                     # no --url needed: defaults to https://my.ence.do
hem-tool keys list --mobile         # any bearer command can push to the paired
                                    # phone instead of taking a passphrase
hem-tool help keys rm               # per-command help (== hem-tool keys rm --help)
```

Connection comes from `--url`/`EHEM_URL` (default `https://my.ence.do` —
a one-line notice tells you when the default kicked in), credentials from
`--passphrase`/`EHEM_PASSPHRASE` or `--mobile` (push confirmation;
`--timeout SEC` bounds the wait; exit 13 = no answer, 14 = rejected on
the phone). `hem-tool --help` shows the grouped command summary.

## Install

```bash
cmake --install build --prefix /your/prefix
```

Downstream CMake projects then consume the SDK via its exported target:

```cmake
find_package(encedo-hem CONFIG REQUIRED)
target_link_libraries(app PRIVATE encedo-hem::encedo-hem)         # shared
# or, for embedding:
target_link_libraries(app PRIVATE encedo-hem::encedo-hem-static)  # static
```

## Layout

```
include/ehem/   public headers (ehem_ prefix) — the per-symbol reference
docs/           API-GUIDE.md · COVERAGE.md
src/            library sources (+ vendored cjson, tools/hem-tool)
tests/          unit/ · integration/ · disruptive/ · support/
cmake/          MinGW toolchain, package config
scripts/        dependency installers
requirements/   REQ-*.md + generated TRACE.md
workplan/        todo/ · doing/ · done/ step files
```

## License

MIT — see [LICENSE](LICENSE). Written from scratch (no code derived from GPL
PKCS#11 implementations). Note wolfSSL is GPLv3/commercial
dual-licensed; binaries that link it must comply accordingly.
