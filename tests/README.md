# Tests

CTest labels partition the suites (ARCHITECTURE.md §9):

| Label | What | When it runs |
|-------|------|--------------|
| `unit` | Offline, fake transport, no network | Every build, both platforms, CI |
| `integration` | Real HEM device over HTTPS | Only when `EHEM_TEST_URL` is set (REQ-TEST-002) |
| `disruptive` | Mutates device availability/state (reboot / firmware / wipe) | Never automatically; needs `EHEM_TEST_URL` **and** `EHEM_ALLOW_DISRUPTIVE=1` |

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

The current integration tests cover the M1 status/version round-trip
(`test_system_live`), the check-in handshake + cert harvest
(`test_checkin_live`, REQ-SYS-006), authenticated config (`test_config_live`),
and the auth token flow (`test_auth_live`).

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
