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

### Update (M7, 2026-07-18 → 22): repo debris NOT the cause

Two more data points weaken the "sustained load / heap fragmentation" reading:

- **2026-07-18:** a full `--reboot-each` sweep (device rebooted before every
  test binary) still wedged — per-binary reboots reset the firmware state yet
  the hang recurred. At that point the key repo was heavily churned (repo_stats:
  ~1560 logically-deleted slots, 99 fragmented), so "repo debris" was the
  leading hypothesis.
- **2026-07-22:** after a full device **wipe + re-init** (repo pristine: 0 keys,
  0 deleted, 0 fragmented) the device *still* wedged once, with near-zero
  traffic. That **rules repo debris out** as the primary driver. The remaining
  suspects are firmware-internal (the disabled watchdog + missing FreeRTOS hooks
  still turn any internal fault into a spin) and possibly plain-HTTP operation /
  missing TLS material during recovery windows. Root cause still needs the
  device UART.

### Mitigation (client-side, shipped)

- **Stall-retry:** `./dev test it` runs the integration suite with
  `ctest --repeat until-pass:${EHEM_TEST_REPEAT:-3}`; a test that hits a
  transient stall is re-run after the device recovers (the live tests self-clean
  via setup sweep + teardown cleanup, so re-runs are safe).
- **Pacing:** `ehem_options.request_pace_ms` (REQ-NET-006) throttles requests;
  the test harness sets it from `EHEM_TEST_PACE_MS`, defaulted to 150 ms for
  `./dev test it`. `EHEM_TEST_PACE_MS=0` disables it.
- **Per-test reboot (opt-in, M7 / REQ-TEST-005):** `EHEM_TEST_REBOOT_EACH=1`
  (or `./dev test it --reboot-each`) reboots the device before each test binary.
  A heavier hammer than pacing; it did NOT defeat the 2026-07-18 hang (see
  above), so it is a diagnostic aid, not a fix.

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

## OPEN — M7 firmware/doc divergences (fw v1.2.2): keymgmt + wrap + storage + logger

**Status:** open (firmware/doc bugs; the SDK ships correct behavior grounded in
the real device). **Affected:** the M7 keymgmt, cipher-wrap, storage, and logger
groups. All found + confirmed live 2026-07-18; upstream-doc / firmware filings.

### 1. `keymgmt/update` is a whole-record rewrite — an omitted `descr` CLEARS it

`api_post_keymgmt_update` rewrites the key's entire metadata record, so a body
with `label` but no `descr` wipes any stored DESCR — NOT "left unchanged" as
`keymgmt/update.md` claims. **SDK/tool handling:** the binding passes the
semantics through verbatim (documented in `ehem_key_update`); `hem-tool keys
update` re-sends the stored descr when `--descr` is omitted so the CLI is
least-surprise (REQ-KEY-007 rev2, REQ-TOOL-011 rev2). **Fix:** correct the doc,
or make the firmware merge omitted fields.

### 2. `keymgmt/derive` output is not externally reproducible

The derived key is NOT `HKDF-SHA256(ECDH-secret, "encedo-<type>")` as
`keymgmt/derive.md` describes: the repo's key-generation step applies an
undisclosed extra transformation to the seed (source `REPO_GenKey_*` absent from
the checkout). 85 candidate local reconstructions — including the doc-exact RFC
5869 pipeline over the raw wolfCrypt X25519 secret `CRYPTO_DeriveKey` provably
outputs — all mismatched the device MAC. Derivation IS deterministic on-device
(a repeat derive dedup-406s), so device↔device agreement holds; external
implementations cannot converge. Also: the doc's `keymgmt:derive` scope works,
but so does `keymgmt:gen` (the SDK uses gen for token sharing); and a
short-secret→long-key derive (e.g. X25519→SECP521R1) is rejected 406, so the
stale-stack HKDF-input quirk noted in the REQ is unreachable for cross-family
derives. **SDK handling:** `ehem_key_derive` documents "device-side agreement
only"; `test_derive_live` pins the mismatch so a doc-conformant firmware change
surfaces (REQ-KEY-009 rev2). **Fix:** document the real derivation, or make it
match the spec.

### 3. `keymgmt/import` — the 70-byte pubkey cap is dead code

The handler validates `pubkey` with `isvalid_base64(.., 66+4)`, nominally
capping decoded length at 70 bytes — but that length check never fires (the
misc.c `isvalid_base64` counter bug, same one that lets an over-long DESCR
through, STEP-M5-010). Live, an **800-byte MLKEM512** pubkey imported fine, and
the repo did not validate the ML-KEM material either. **SDK handling:**
`ehem_key_import` imposes no client-side pubkey cap; the device arbitrates
(REQ-KEY-008 rev3). Also observed: import dedup (406 on a duplicate pubkey)
appears to match material from keys that were imported AND DELETED once the
device reboots in between — the boot-time repo scan seems to index
non-compacted deleted slots. Recorded as a REQ-KEY-008 open criterion (the tests
now use per-run-unique material); confirm at leisure. **Fix:** repair the
length check; make delete+compaction drop the dedup index entry.

### 4. `cipher/wrap` HKDF info string is `"encedo-kek"`, not the documented `"encedo"`

`CRYPTO_Wrap`'s ECDH-derived-KEK HKDF uses info = `"encedo-kek"` ‖ ctx
(crypto.c:57 `CRYPTO_HKDF_CONTEXT_KEK`) — a THIRD literal, distinct from both
the doc/handler-comment's `"encedo"` and encrypt's `"encedo-aes"`. Proven by a
byte-exact match between the device wrap and a local `wc_AesKeyWrap` under
HKDF(shim ECDH secret) with that info. Unlike derive, this HKDF uses the
secret's real length, so wrap IS externally reproducible. **SDK handling:** the
`ehem_wrap` header documents `"encedo-kek"`; `test_wrap_live` pins it
(REQ-OPS-009 rev2). **Fix:** correct the doc.

### 5. `storage/unlock` + `storage/lock` read an uninitialized `sub` pointer

Both handlers evaluate `strcmp(sub, "M")` before `sub` is assigned (it is only
set inside the audit-log branch — api_storage.c:44-46 / :132-134): undefined
behavior on the scope-prefix-match path. It happened to be benign on this build
(two clean attended runs), but UB can shift with any firmware change. **SDK
handling:** the live test stays `disruptive`-labeled (UB + the unlock exposes
the microSD to the USB host) — REQ-SYS-010 rev2. **Fix:** assign `sub` before
the scope check.

### 6. `logger/{id}` files are pipe-delimited, not "JSON-like"

Downloaded audit-log files are a `# Encedo nGINE FW <ver>` header line followed
by pipe-delimited records (`seq|ts|type|result|…|sig|chain`, base64url fields),
not the "one JSON-like record per line" `logger/get.md` states. **SDK
handling:** `ehem_logger_get` returns the body verbatim (never parses it); the
header documents the real shape (REQ-SYS-009). **Fix:** correct the doc.

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
