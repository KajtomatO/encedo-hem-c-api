# Tests

CTest labels partition the suites (ARCHITECTURE.md §9):

| Label | What | When it runs |
|-------|------|--------------|
| `unit` | Offline, fake transport, no network | Every build, both platforms, CI |
| `integration` | Real HEM device over HTTPS | Only when `EHEM_TEST_URL` is set (REQ-TEST-002) |
| `disruptive` | Mutates device availability/state (reboot / firmware) | Never automatically; needs `EHEM_TEST_URL` **and** `EHEM_ALLOW_DISRUPTIVE=1` |
| *(none — attended-only)* | Device wipe and device initialisation (`ehem_system_wipeout`, `ehem_device_init`; encedo-hem-tool's commands over them likewise) | **No CTest at all**, not even `disruptive` (REQ-TEST-007, user decision 2026-10-07): attended runs only, recorded in the owning REQ and step; their fake-transport unit tests still run under `unit` |

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
(`-DEHEM_BUILD_SHARED=OFF`) and shared SDK only (`-DEHEM_BUILD_STATIC=OFF`;
the unit tests then link a private, never-installed static build of the
SDK objects). The switch and OpenSSL legs then install the build and assert
what it links (`ci.yml`, "Assert the link result"). `./dev build
--static-only|--shared-only|--deps …|--openssl` builds the same sets
locally, each in its own directory. Every leg also compiles `examples/`
(REQ-API-007) and `tests/release/tls-probe`. The Linux legs run on
`ubuntu-latest` and the Windows legs on MSYS2's current packages, so a new
compiler reaches the matrix before the dev host has it: MSYS2's GCC 16
(2026-10-13, `-Wunterminated-string-initialization`) and Ubuntu 26.04's
clang (2026-11-09, `-Wuninitialized-const-pointer`, new in clang 21) each
failed a leg on code the host compilers accepted — fixed at the source both
times, never by relaxing `-Werror` or pinning the image. Only the release
build jobs pin a runner (`ubuntu-22.04`, the glibc floor).

The refusal of a foreign crypto library (REQ-API-010) cannot be produced on
demand with a real library, so a test seam forces it: `test_backend_compat`
covers the refusal, its propagation through the implicit init paths and
idempotency across it. hem-tool's message and remedy — and the real cases
the release job used to run against its binaries — are encedo-hem-tool's
since the SDK's 1.2.1, together with the tool's tests.

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
`scripts/release/package-lib.sh`, REQ-BUILD-010): the `-shared` one with the
wolfSSL companion library, and the plain-named static one, whose OpenSSL and
libcurl archives make the full `pkg-config --static` closure linkable — there
the gate requires it.

### The subdirectory consumer gate and `ehem::test-support`

`tests/support/CMakeLists.txt` builds the test support — the fake transport,
the simulated ExtAuth authenticator, `crypto_ref` (behind
`EHEM_TEST_SUPPORT_CRYPTO_REF`) and the header-only fixtures — as the target
`ehem_test_support`, alias **`ehem::test-support`**, whenever
`EHEM_BUILD_TEST_SUPPORT` is on (default: `BUILD_TESTING`). It PUBLIC-includes
`tests/support` and `src/` (the hidden seams: `ehem_auth_test_set_clock`,
`ehem_auth_test_set_kdf_iters`, `ehem_ext_test_set_poll_interval`,
`ehem_crypto_test_force_mismatch`) and PUBLIC-links the static SDK, so a test
that lists it alone reaches the hidden symbols. A project that adds this
repository with `add_subdirectory` (encedo-hem-tool does, for its tests) sets
`BUILD_TESTING OFF`, `EHEM_BUILD_SHARED OFF` and `EHEM_BUILD_TEST_SUPPORT ON`
as normal variables first (REQ-BUILD-011).

`subdir_consumer` (label `unit`, `tests/consumer/check_subdir_consumer.cmake`)
proves that shape on every leg: it configures `tests/consumer/subdir/` — a
scratch project doing exactly that, with `EXCLUDE_FROM_ALL` — with the
build's generator, compiler, backend and prefix path, checks that the SDK
brought no test and no program target along, builds the consumer (it links
`encedo-hem::encedo-hem-static` + `ehem::test-support`, calls a seam and drives
one request through the fake transport) and runs it.

### Release-job helpers

`tests/release/tls-probe` (built with `BUILD_TESTING`, not a CTest) makes one
request to a URL under a given CA file and prints the SDK's verdict —
`rc=… tls_expired=… detail=…`. `scripts/release/check-expired-classifier.sh`
drives it against local endpoints serving an expired and a not-yet-valid
leaf, under both TLS backends of the release builds (REQ-NET-005,
REQ-BUILD-010).

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
and the notification broker, the logger; each names the REQs it verifies in
its header. `./dev test it` runs them with the device credentials from
`./hem.env`. (hem-tool's live tests moved to encedo-hem-tool with the tool.)

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

The three disruptive tests: `test_storage_live` (REQ-SYS-010, the storage
unlock/lock probe — the v1.2.2 handlers read an uninitialised pointer, so a
hard fault would self-reset the device), `test_notify_register_live`
(REQ-AUTH-008, a broker registration that stays dangling) and
`test_keygen_matrix_live` (REQ-TEST-004, quarantined here because it
reproducibly stalls the dev firmware — KNOWN-ISSUES). hem-tool's
`cert-install` live test moved to encedo-hem-tool with the tool.

> Disposable-device policy (ARCHITECTURE.md §9): from M3, tests that create
> keys must use the `EHEMTEST` label prefix and must never touch the protected
> set (device TLS material, paired phone authenticators).
