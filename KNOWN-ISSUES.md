# Known issues

## OPEN — Device stalls/hangs under sustained load (dev HEM, fw v1.2.2)

**Status:** open (device/firmware issue; client-side mitigation shipped
2026-07-17). **Affected:** all `integration`-labeled tests; surfaced running
the M5 suite.

### Symptom

The development HEM (my.ence.do, fw v1.2.2-DIAG) intermittently stops
responding. Two grades were observed: **transient stalls** — an operation
freezes for tens of seconds (one `SECP521R1` sign froze ~45 s) then recovers on
its own — and **hard hangs** — the device stops answering entirely and only a
physical power-cycle brings it back. The hard hang appeared after the full
integration suite; a single test rarely triggers it.

### What it is NOT (ruled out by reproduction, 2026-07-17)

Per-operation probes (create → list → get → sign → delete for all 23 firmware
key types, run twice; plus 60 back-to-back signs ≈ 240 TLS connections) all
completed with the device alive after every operation. So it is **not** PQC key
generation, **not** any single crypto operation, **not** SECP521R1 signing, and
**not** raw connection volume. It is an **intermittent stall under sustained
load** whose probability rises with total operation count / run length — which
is why it looked like "the matrix test" (the longest test = the most chances to
hit a non-recovering stall).

### Why a hang needs a physical reboot

- The **watchdog is disabled** in firmware (`user_board.c:342`,
  `WDT->WDT_MR = WDT_MR_WDDIS`), so nothing on-device recovers a hang.
- A real crash would self-recover: the fault handlers software-reset
  (`exceptions_sam.c`, HardFault → `rstc_start_software_reset(RSTC)`). Since the
  device instead stays dead, these are **hangs (spin/deadlock), not crashes** —
  the fault path is never reached.
- `configUSE_MALLOC_FAILED_HOOK`/`configCHECK_FOR_STACK_OVERFLOW` are enabled
  and `configASSERT` is active, but the application defines no
  `vApplicationMallocFailedHook` / `vApplicationStackOverflowHook` /
  `vAssertCalled` — so a malloc failure, stack overflow, or failed assert under
  load resolves to a spin instead of the self-resetting fault path.

The exact exhausted resource (heap fragmentation, a blocking flash/audit-log
write, a task stall) could not be pinned from the client — it needs the
device's debug UART captured during a hang.

### Mitigation (client-side, shipped)

- **Stall-retry:** `./dev test it` runs the integration suite with
  `ctest --repeat until-pass:${EHEM_TEST_REPEAT:-3}`; a test that hits a
  transient stall is re-run after the device recovers (the live tests self-clean
  via setup sweep + teardown cleanup, so re-runs are safe).
- **Pacing:** `ehem_options.request_pace_ms` (REQ-NET-006) throttles requests;
  the test harness sets it from `EHEM_TEST_PACE_MS`, defaulted to 150 ms for
  `./dev test it`. `EHEM_TEST_PACE_MS=0` disables it.

Neither prevents a rare hard hang (only the firmware can), but together they let
the suite pass reliably on the flaky device instead of failing on the first
stall.

### Upstream (firmware) fixes to file

- Re-enable the watchdog so a stall self-recovers via reset.
- Give `vApplicationMallocFailedHook` / `vApplicationStackOverflowHook` /
  `vAssertCalled` real bodies (log + reset, not spin).
- Investigate the stall itself under sustained API load.

## OPEN — Device clock runs ~8% fast; login breaks after ~12 h; check-in resyncs

**Status:** open (device/firmware issue; workaround known, SDK/consumer
recommendation below). **Affected:** every authenticated call — the login
starts failing device-wide once the drift accumulates.

### Symptom and measurements (dev HEM my.ence.do, fw v1.2.2-DIAG, 2026-07-17)

Every login returned **401 on the token POST** with correct credentials.
Root cause: the device RTC **gains ~8% continuously** — measured twice via
the challenge `exp` (= device_now + 60): ~77 min ahead after ~15.4 h of
uptime, and ~380 s ahead after ~80 min of uptime following a power-cycle.
Since STEP-M2-045 the SDK requests bearer `exp = local_now + 3600`; once the
device clock is more than that ahead, the requested expiry is already in the
device's past → the eJWT is rejected as expired. At ~8% drift that happens
roughly **12 hours after the last resync**.

The existing automatic recovery (REQ-NET-005 / REQ-AUTH-001) does NOT
trigger here: it keys on expired-cert TLS failures and on a challenge-GET
403, not on a token-POST 401.

### Workaround

`hem-tool checkin` (or `ehem_system_checkin()`): the check-in flow resyncs
the device clock as a side effect — verified twice on 2026-07-17 (challenge
`exp` returned to exactly now + 60 both times). Run it after any device
power-cycle and periodically on long-lived deployments.

### Recommendation for consumers / possible SDK feature (note 2026-07-17)

The **encedo-pkcs11 (Cryptoki) implementation should probably run a
check-in whenever a new session starts** (C_Initialize or first
C_OpenSession per slot) — it is one unauthenticated round-trip, it heals
both known device time/cert pathologies (this drift and the REQ-SYS-003
certificate rotation), and it makes the later authenticated calls
predictable. Possibly this belongs in the SDK itself as an **opt-in
option** (e.g. `ehem_options.checkin_on_login`, or extending the
REQ-NET-005-style auto-recovery to a single check-in + retry on a login
401) so every consumer gets it without re-implementing. Not decided —
candidate REQ for a future milestone (user note, 2026-07-17).

### Upstream (firmware) fixes to file

- RTC gains ~8% — calibrate/fix the clock source.
- Consider tolerating a client `exp` beyond the intended lifetime by
  clamping instead of rejecting (defense against exactly this drift).

## OPEN — PQC endpoint response bugs (fw v1.2.2): decaps `alg` leak, mldsa-verify raw status

**Status:** open (firmware bugs; the SDK ships defensive handling for both).
**Affected:** `/api/crypto/pqc/mlkem/decaps` and `/api/crypto/pqc/mldsa/verify`.
Both found at STEP-M6-050 from firmware source and confirmed live 2026-07-17.

### 1. `mlkem/decaps` echoes an unwritten buffer as `alg`

`CRYPTO_MLKEM_Decaps` takes no `alg_used` parameter (crypto.h:27), but the
handler passes its scratch buffer anyway and serializes it into the response
(api_crypto.c:2177, 2195). The buffer last held the request's scope string,
so a live decaps answers with `"alg":"keymgmt:use:<kid-prefix>…"` — stale
stack content in a response field (the leaked value is the caller's own
scope today, but it is uninitialized-buffer serialization all the same).
**SDK handling:** `ehem_mlkem_decaps` treats `alg` as informational,
truncated, never interpreted (REQ-OPS-007).
**Upstream fix:** thread `alg_used` through decaps like encaps, or drop the
field from the decaps response.

### 2. `mldsa/verify` reports failure with a raw error code as the HTTP status

Every other crypto handler maps a failed operation to 406; the mldsa-verify
handler returns `CRYPTO_MLDSA_Verify`'s raw failure code straight into the
HTTP status line (api_crypto.c:2545 — no `ret = 406` mapping). An invalid
signature produced **`HTTP/1.1 795`** live (the doc's crypto/pqc/
mldsa-verify.md promises 406). **SDK handling:** `ehem_mldsa_verify` maps
ANY completed non-200/non-auth status — including out-of-range ones — to
`EHEM_ERR_DEVICE` with the raw status retrievable (REQ-OPS-008; unit tests
pin 65307/−229/100). **Upstream fix:** add the missing 406 mapping.

## RESOLVED — Windows (MinGW) X25519 crash: missing wolfCrypt_Init()

**Status:** resolved 2026-07-16 (same day it was shelved). **Affected:**
REQ-AUTH-001 (auth crypto), REQ-BUILD-002 (CI on Windows). The `windows-mingw`
CI job is **re-enabled**; 12/12 unit tests pass on Windows (MSYS2 MINGW64).

### Symptom (historical)

On Windows, the M2 crypto unit tests (`test_crypto`, `test_ejwt`) crashed with
`EXCEPTION_ACCESS_VIOLATION` the moment any X25519 operation ran. PBKDF2 and
HMAC-SHA256 (also wolfCrypt) worked fine; only curve25519 crashed. Two earlier
theories (curve25519_key struct-ABI mismatch, then "miscompiled SP-math in the
packaged DLL") did not survive debugging.

### Actual root cause

The SDK never called `wolfCrypt_Init()`. gdb named the full crash chain:

    wc_curve25519_generic → wc_InitRng → wc_LockMutex
      → ntdll!RtlEnterCriticalSection → fault (uninitialized CRITICAL_SECTION)

- The MSYS2 wolfSSL 5.9.2 DLL is built with **curve25519 blinding**
  (`WOLFSSL_CURVE25519_BLINDING`, upstream default since 5.8.2 for the C
  implementation), so **every** X25519 call runs the wolfCrypt RNG.
- The RNG's global mutex is a Windows `CRITICAL_SECTION`, which has no static
  initializer — it exists only after `wolfCrypt_Init()`. Entering it
  uninitialized dereferences a NULL `DebugInfo` → access violation.
- On Linux (pthreads) wolfSSL initializes its global mutexes statically, so
  skipping `wolfCrypt_Init()` went unnoticed there; Debian's 5.6.6 also
  predates blinding entirely. PBKDF2/HMAC never touch global mutexes, which is
  why only X25519 crashed.
- Proof: a 20-line standalone repro against the unmodified MSYS2 DLL crashes
  identically without `wolfCrypt_Init()` and reproduces the RFC 7748 §6.1
  vectors byte-for-byte with it. **The packaged wolfSSL is fine.**

### Fix (2026-07-16)

- `ehem_crypto_backend_global_init/cleanup` (`wolfCrypt_Init/Cleanup`) in
  `src/crypto_shim.{h,c}`, orchestrated by `ehem_global_init/cleanup`
  (REQ-API-002, same pattern as the curl transport backend).
- `acquire_token` (src/proto_auth.c) calls the idempotent `ehem_global_init()`
  before deriving credentials, so logins over a caller-supplied transport are
  covered too (the default curl transport factory already ran it).
- `test_crypto` / `test_ejwt` call `ehem_global_init()` in `main`.

### Residual notes

- The struct-free `wc_curve25519_generic` shim is **kept**: the MSYS2 package's
  installed `options.h` does NOT define `WOLFSSL_CURVE25519_BLINDING` even
  though the DLL was built with it, so consumer-visible wolfSSL struct layouts
  (`curve25519_key`: 128 bytes in-DLL vs 112 per headers) diverge from the
  DLL's. Caller-allocated wolfSSL structs remain an ABI hazard with prebuilt
  packages.
- That header/DLL config divergence is a reportable packaging/upstream issue
  (wolfSSL's CMake build enables blinding by default but omits the define from
  the generated `options.h`; MSYS2 ships the result).
