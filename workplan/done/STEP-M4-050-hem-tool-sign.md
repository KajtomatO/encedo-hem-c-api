---
id: STEP-M4-050
title: "hem-tool sign — sign a file/stdin message with a device key"
milestone: M4
implements: ["REQ-TOOL-008"]
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli"]
depends_on: ["STEP-M4-010", "STEP-M4-030"]
evidence:
  commits:
    - "15dea73 — hem-tool sign (REQ-TOOL-008)"
  tests:
    - "tests/unit/test_sign_tool.c — b64/hex/raw outputs (raw = exact sig bytes); explicit --alg → NO get (3-request assert) + byte-exact body; omitted --alg → ONE get riding the same token (4 requests), Ed25519 + SHA256WithECDSA defaults asserted; non-signing family (CURVE25519) → exit 1 naming the type, no sign request; --in FILE binary-safe (NUL byte → b64 YQBi) + unreadable → 2 with 0 requests; empty/oversized msg, oversized sigctx, missing kid/passphrase → 2 with 0 requests; 403/406 → exit 1; --sigctx in body"
  notes: >
    hem-tool-core sign.{h,c}: hem_sign_opts {passphrase,kid,alg,in_path,
    sigctx,format,out/err/in} + hem_sign_run. Message read BEFORE any
    network (cheap input errors); binary-safe (fopen "rb"; _setmode
    _O_BINARY on stdin AND on stdout for --raw under _WIN32). Default-alg
    table per REQ-TOOL-008 (P-256/K1→SHA256WithECDSA, P-384→SHA384, P-521→
    SHA512, ED25519→Ed25519, ED448→Ed448); the lookup get rides the same
    per-KID token as the sign (no extra login; proven by the 4-request
    unit assert). keys.c helpers promoted to shared hem_tool_kid_ok /
    hem_tool_fprint_b64/_hex (keys.h). main.c: `sign KID` command, --alg/
    --in/--sigctx options, --hex/--raw shared with keys pub, cert notice
    suppressed in raw mode. LIVE DEMO (my.ence.do, 2026-07-16): created
    EHEMTEST ED25519 + SECP256R1(ExDSA) keys; `hem-tool sign --in msg.txt`
    (default alg note printed) → 64B raw + 72B DER sigs; `keys pub --raw`
    → 32B/33B(compressed) pubkeys; BOTH signatures verified with OPENSSL
    (independent of our shim): Ed25519 pkeyutl "Signature Verified
    Successfully", ECDSA dgst -sha256 "Verified OK"; cleanup via
    `keys rm --label-prefix 'EHEMTEST tool demo' --yes` (2 deleted).
    NEW live vocabulary observation: created keys list as
    ATT,PKEY,ExDSA,ED25519 / ATT,PKEY,ExDSA,SECP256R1 — classifier
    handles both (tolerant + family tokens). ./dev ci 21/21 gcc+clang;
    asan clean; sign.c+main.c MinGW cross-syntax clean (-Werror).
reopened: []
cancelled: null
---

**Goal:** `hem-tool sign <kid> [--alg ALG] [--in FILE] [--sigctx STR]
[--hex|--raw]` reads the message (stdin default, 1..2048 bytes), signs
via `ehem_sign`, writes the signature to stdout as padded base64
(`--hex`/`--raw` variants). `--alg` omitted → fetch type via
`ehem_key_get`, classify (REQ-KEY-006), pick the family's canonical
selector (P-256/K-256→SHA256WithECDSA, P-384→SHA384WithECDSA,
P-521→SHA512WithECDSA, ED25519→Ed25519, ED448→Ed448); non-signing family
→ exit 1 naming the type. Exit 0/1/2 per tool conventions.

**Notes:** Orchestration in `hem-tool-core`. The default-alg get rides
the same per-KID token as the sign (REQ-OPS-001 shared cache) — one
extra request, no extra login; `--alg` given verbatim skips the fetch.
Read stdin/file in binary mode (Windows: `_setmode`/`"rb"` — CRLF must
not corrupt the message; see M2-070 portability gotchas). `--raw`
signature output = exact bytes, prose to stderr.

**Definition of done**
- [x] Unit (fake transport): `--in` and stdin messages sign; b64/hex/raw
      outputs correct; explicit `--alg` → no get request; omitted →
      exactly one get then sign (request-sequence asserts); default
      selector per family matches the documented table. (test_sign_tool
      7 cases.)
- [x] Unit: non-signing family (CURVE25519 fixture) → exit 1, no sign
      request; empty/oversized msg, unreadable `--in`, sigctx > 255,
      missing kid/URL/passphrase → exit 2, no sign request; 403 → exit 1
      scope message; 406 → exit 1. (test_sign_non_signing_family,
      test_sign_usage_errors, test_sign_device_errors.)
- [x] Live: sign a small file with EHEMTEST ED25519 + SECP256R1(ExDSA)
      keys; signatures verify locally (proven with OPENSSL — independent
      of our shim; see evidence; the shim-verify variant repeats at the
      M4 gate).
- [x] MinGW clean (binary stdin/stdout via _setmode; cross-syntax check
      -Werror green); ASan/LSan clean (asan 21/21).
