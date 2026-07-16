---
id: STEP-M4-050
title: "hem-tool sign — sign a file/stdin message with a device key"
milestone: M4
implements: ["REQ-TOOL-008"]
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli"]
depends_on: ["STEP-M4-010", "STEP-M4-030"]
evidence:
  commits: []
  tests: []
  notes: null
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
- [ ] Unit (fake transport): `--in` and stdin messages sign; b64/hex/raw
      outputs correct; explicit `--alg` → no get request; omitted →
      exactly one get then sign (request-sequence asserts); default
      selector per family matches the documented table.
- [ ] Unit: non-signing family (CURVE25519 fixture) → exit 1, no sign
      request; empty/oversized msg, unreadable `--in`, sigctx > 255,
      missing kid/URL/passphrase → exit 2, no sign request; 403 → exit 1
      scope message; 406 → exit 1.
- [ ] Live: sign a small file with EHEMTEST ED25519 + SECP256R1(ExDSA)
      keys; signatures verify locally with wolfCrypt (feeds the M4 gate
      demo).
- [ ] MinGW clean (binary stdin); ASan/LSan clean.
