# Known issues

## Windows (MinGW) is shelved — wolfSSL curve25519 crashes on that platform

**Status:** open. **Decided:** 2026-07-16 (user). **Affects:** REQ-AUTH-001
(auth crypto), REQ-BUILD-002 (CI on Windows). The `windows-mingw` CI job is
**disabled** (`if: false` in `.github/workflows/ci.yml`); Linux (gcc/clang) CI
is unaffected and green.

### Symptom

On Windows, the M2 crypto unit tests (`test_crypto`, `test_ejwt`) crash with
`EXCEPTION_ACCESS_VIOLATION` the moment any X25519 operation runs. PBKDF2 and
HMAC-SHA256 (also wolfCrypt) work fine; only curve25519 crashes.

### Root cause (diagnosed 2026-07-16)

The crash is **inside the prebuilt MSYS2/MinGW wolfSSL DLL's own curve25519
code**, not in how the SDK calls it:

- The MSYS2 package is **wolfSSL 5.9.2**, where `sizeof(curve25519_key) == 128`
  (vs 112 in the Debian 5.6.6 build). An early theory was an ABI/struct-layout
  mismatch across the SDK↔DLL boundary.
- But the crash persists even through `wc_curve25519_generic`, which passes
  **only byte arrays** — no `curve25519_key` crosses the boundary. A struct-free
  diagnostic crashed *inside* `wc_curve25519_generic` before it could print.
- The faulting address is fixed inside `libwolfssl` across every attempt, and
  only curve25519 (heavy SP-math) is affected.

Conclusion: the packaged wolfSSL 5.9.2 build's single-precision math for
curve25519 is miscompiled/misconfigured for the Windows x64 ABI (most likely an
SP-assembly / stack-alignment problem). It is not fixable from SDK code.

The SDK's own X25519 code is correct: on Linux it reproduces the RFC 7748
§6.1 vectors and the full python-client eJWT fixture byte-for-byte
(`tests/unit/test_crypto.c`, `tests/unit/test_ejwt.c`), gcc + clang + ASan
clean.

### Paths to re-enable Windows

Either of these fixes the platform without touching SDK logic. Pick one when
Windows support is scheduled, then delete the `if: false` on the `windows-mingw`
job:

1. **Vendor a small portable X25519** (e.g. curve25519-donna, public domain):
   use it for keypair + ECDH only; keep wolfCrypt for PBKDF2/HMAC. Portable C —
   no asm, no prebuilt-DLL dependency — so it behaves identically everywhere and
   is fully verifiable against RFC 7748 offline. Trade-off: introduces vendored
   crypto (a departure from the "wolfSSL for all crypto" principle,
   ARCHITECTURE.md §1).

2. **FetchContent-build wolfSSL from source** with a MinGW-safe config (SP
   assembly disabled → portable C math), per ARCHITECTURE.md §12 risk 5.
   Trade-off: slower CI (builds wolfSSL unless cached) and more CMake surface;
   the exact flags need a Windows run to confirm.

Until then, the SDK builds and its crypto is exercised on Linux only.
