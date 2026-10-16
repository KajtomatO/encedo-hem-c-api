# Tests

CTest labels partition the suites (ARCHITECTURE.md §9):

| Label | What | When it runs |
|-------|------|--------------|
| `unit` | Offline, fake transport, no network | Every build, both platforms, CI |
| `integration` | Real HEM device over HTTPS | Only when `EHEM_TEST_URL` is set (REQ-TEST-002) |
| `disruptive` | Mutates device availability/state (reboot / firmware) | Never automatically; needs `EHEM_TEST_URL` **and** `EHEM_ALLOW_DISRUPTIVE=1` |
| *(none — attended-only)* | Device wipe and device initialisation (`ehem_system_wipeout`, `ehem_device_init`, hem-tool `wipe-device` / `init-device` / `init-wizard`) | **No CTest at all**, not even `disruptive` (REQ-TEST-007, user decision 2026-10-07): attended runs only, recorded in the owning REQ and step; their fake-transport unit tests still run under `unit` |

## Running unit tests

```sh
cmake -B build -G Ninja
cmake --build build
ctest --test-dir build -L unit --output-on-failure
```

### Two crypto backends

The SDK builds on wolfSSL (default) or OpenSSL 3 (`-DEHEM_CRYPTO_BACKEND=openssl`,
REQ-BUILD-007); every suite runs on both — `./dev test <suite> --openssl` and
`./dev ci --openssl` build in their own `-openssl` directories, and CI has an
OpenSSL leg per platform. No test knows the backend: where a live test checks
the device's crypto locally (HKDF, AES-GCM, AES key wrap, SHA-256), it calls
`tests/support/crypto_ref.h`, implemented once per backend
(`crypto_ref_wolfssl.c` / `crypto_ref_openssl.c`) — the only test sources that
include a crypto library header. The `tests_backend_free` gate (label `unit`)
enforces that. MSYS2's wolfSSL has no AES key wrap, so `test_wrap_live`'s
byte-exact local cross-check is skipped there (it says so).

### Build switches and the backend check

CI runs the unit suite on ten legs: Linux gcc and clang, each on wolfSSL and
OpenSSL; Windows (MSYS2 UCRT64) on wolfSSL and OpenSSL; and on each platform
the two switch legs of REQ-BUILD-006 — static SDK only
(`-DEHEM_BUILD_SHARED=OFF`) and shared SDK only with hem-tool linked to it
(`-DEHEM_BUILD_STATIC=OFF -DEHEM_TOOL_LINK=shared`). The switch and OpenSSL
legs then install the build and assert what it links (`ci.yml`, "Assert the
link result"). `./dev build --static-only|--shared-only|--tool-link …|--deps
…|--openssl` builds the same sets locally, each in its own directory.

The refusal of a foreign crypto library (REQ-API-010) cannot be produced on
demand with a real library, so a test seam forces it: `test_backend_compat`
covers the refusal, its propagation through the implicit init paths and
idempotency across it; `test_tool_startup` covers hem-tool's message and
remedy for every backend and link mode (REQ-TOOL-024); and
`tool_startup_cli` drives a test build of the real `main.c`
(`hem-tool-forced-mismatch`) through the command line — exit 12 for every
command that needs the SDK, 0 for help and `--version`. The release jobs add
the real cases: the Windows hem-tool without `libwolfssl.dll`, and the Linux
`-static` hem-tool under an OpenSSL configuration that loads no algorithms.

### The consumer gate

`consumer_interface` (label `unit`, REQ-BUILD-008) checks the SDK as a user
gets it: it `cmake --install`s the build into `<build>/tests/consumer-gate/`,
checks the installed package config, export and `encedo-hem.pc`, then builds
and runs `tests/consumer/` (a few public calls, no traffic) against that
install — via `find_package` and via pkg-config, for the shared and the static
library, whichever the build carries. The static pkg-config consumer uses the
`pkg-config --static` closure where every library of it can be linked; on
Ubuntu, libcurl's private libraries have no linkable files, so there it links
the archive with the shared libcurl and wolfSSL instead and says so in the
test's log (`ctest -R consumer_interface -V`). On MinGW the shared consumers
link with `-Wl,--disable-auto-import`, so the DLL must be reachable without
the linker's auto-import. Native builds only. The release job runs the same
script against each unpacked SDK library package (`-DEHEM_PACKAGE=<dir>`,
`scripts/release/package-lib.sh`, REQ-BUILD-009): the shared one with the
wolfSSL companion library, and the `-static` one, whose OpenSSL and libcurl
archives make the full `pkg-config --static` closure linkable — there the
gate requires it.

## Running integration tests

Integration tests talk to a real Encedo HEM. They are **skipped** (reported by
CTest as *Skipped*, not failed) whenever `EHEM_TEST_URL` is unset — so a plain
`ctest` stays green on machines and CI without a device.

To run them against a device:

```sh
export EHEM_TEST_URL="https://<device-host>"     # required — enables the suite

# TLS trust for the device certificate (pick one; default is system trust):
export EHEM_TEST_INSECURE=1                       # skip verification (lab only)
# or
export EHEM_TEST_CACERT=/path/to/device-ca.pem    # verify against this CA/cert

# From M2 onward, auth-bearing tests also read:
# export EHEM_TEST_PASSPHRASE="..."

ctest --test-dir build -L integration --output-on-failure
```

The gating and device-context setup live in
`tests/support/integration_env.h` (one helper, not copy-pasted per test):
`ehem_require_test_url()` performs the skip, `ehem_test_ctx()` builds a context
from the environment.

`tests/integration/` holds one `test_*_live.c` per area — system and
check-in, auth and config, key management, every crypto operation, ExtAuth
and the notification broker, the logger and random; each names the REQs it
verifies in its header. `./dev test it` runs them with the device
credentials from `./hem.env`.

## Running disruptive tests

Disruptive tests mutate and **reboot** the device, so they carry a second gate
on top of `EHEM_TEST_URL`: `EHEM_ALLOW_DISRUPTIVE=1`. Without it they skip (exit
77), and they are excluded from `-L integration`. The helper
`ehem_require_disruptive()` (in `integration_env.h`) enforces both gates.

```sh
export EHEM_TEST_URL="https://<device-host>"
export EHEM_TEST_PASSPHRASE="..."
EHEM_ALLOW_DISRUPTIVE=1 ctest --test-dir build -L disruptive --output-on-failure
```

`test_cert_install_live` (REQ-TOOL-003) drives `hem-tool cert-install` end to
end: it is safe (exits "already current") when the device already serves the
cloud's certificate, but WILL install + reboot when it does not.

> Disposable-device policy (ARCHITECTURE.md §9): from M3, tests that create
> keys must use the `EHEMTEST` label prefix and must never touch the protected
> set (device TLS material, paired phone authenticators).
