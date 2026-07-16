# encedo-hem-c-api

A portable **C99 client library** (the *Encedo HEM C SDK*, symbol prefix
`ehem_`) for driving the [Encedo HEM](https://ence.do) network cryptographic
device over its REST/HTTPS API. Its first consumer is the `encedo-pkcs11`
module.

> **Status: early development (0.1.0).** Milestone M1 is in progress: the build
> skeleton, the `ehem_ctx` surface, the transport layer, and the unauthenticated
> `system/status` + `system/version` bindings. See
> [ARCHITECTURE.md](ARCHITECTURE.md) for the full design and milestone plan, and
> [REQUIREMENTS-MANAGEMENT.md](REQUIREMENTS-MANAGEMENT.md) for how the work is
> tracked. What exists *today* is the CMake build, `ehem_version()`, and the
> unit-test harness.

## Dependencies

| Need | Purpose |
|------|---------|
| CMake ≥ 3.20 + CTest | build system |
| A C99 compiler (GCC/Clang on Linux, MinGW-w64 on Windows) | — |
| [libcurl](https://curl.se/libcurl/) dev | default HTTPS transport |
| [CMocka](https://cmocka.org/) dev | unit tests |
| wolfSSL dev *(from M2 on)* | crypto primitives for the auth flow |

cJSON and Argon2 are **vendored into the source tree** (no system install), so
they are not listed above.

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
- **`integration`** *(arrives in a later M1 step)* — real-device round-trips,
  **gated on the `EHEM_TEST_URL` environment variable**. With it unset the
  integration tests skip, so a checkout without a device stays green. To run
  them once they exist:

  ```bash
  EHEM_TEST_URL="https://my-hem.example" EHEM_TEST_PASSPHRASE="…" \
      ctest --test-dir build -L integration --output-on-failure
  ```

- **`disruptive`** — mutates device availability or state (reboot / firmware /
  wipe). Run deliberately and attended, never in unattended CI; requires
  both the label and `EHEM_ALLOW_DISRUPTIVE=1`.

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
include/ehem/   public headers (ehem_ prefix)
src/            library sources (+ vendored cjson/argon2, tools/hem-tool)
tests/          unit/ · integration/ · disruptive/ · support/
cmake/          MinGW toolchain, package config
scripts/        dependency installers
requirements/   REQ-*.md + generated TRACE.md
workplan/        todo/ · doing/ · done/ step files
```

## License

MIT — see [LICENSE](LICENSE). Written from scratch (no code derived from GPL
PKCS#11 implementations). Note wolfSSL, linked from M2 on, is GPLv3/commercial
dual-licensed; binaries that link it must comply accordingly.
