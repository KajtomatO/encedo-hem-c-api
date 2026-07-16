# Known issues

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
