# Tests

CTest labels partition the suites (ARCHITECTURE.md §9):

| Label | What | When it runs |
|-------|------|--------------|
| `unit` | Offline, fake transport, no network | Every build, both platforms, CI |
| `integration` | Real HEM device over HTTPS | Only when `EHEM_TEST_URL` is set (REQ-TEST-002) |
| `disruptive` | Mutates device availability/state (reboot / firmware / wipe) | Never automatically; deliberate, attended opt-in (added later) |

## Running unit tests

```sh
cmake -B build -G Ninja
cmake --build build
ctest --test-dir build -L unit --output-on-failure
```

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

The current integration test (`test_system_live`) performs the M1 status +
version round-trip against the device.

> Disposable-device policy (ARCHITECTURE.md §9): from M3, tests that create
> keys must use the `EHEMTEST` label prefix and must never touch the protected
> set (device TLS material, paired phone authenticators).
