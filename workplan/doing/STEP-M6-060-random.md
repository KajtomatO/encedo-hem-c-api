---
id: STEP-M6-060
title: "ehem_random (encrypt-IV harvest) + hem-tool random"
milestone: M6
implements: ["REQ-OPS-002", "REQ-TOOL-010"]
traces:
  architecture: ["ARCHITECTURE.md#11-milestones", "ARCHITECTURE.md#8-hem-tool-cli"]
depends_on: ["STEP-M6-040"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** `ehem_random(ctx, kid, buf, len)` fills buf with device
hardware-RNG bytes via ⌈len/16⌉ AES128-CBC encrypt round-trips on the
caller's AES key (single-zero-byte payload, IVs concatenated — REQ-OPS-002
rev2 design), plus `hem-tool random <N> [--kid K] [--raw]` with transient
EHEMTEST-key orchestration in hem-tool-core.

**Notes:** SDK never creates/deletes keys (REQ-OPS-002); the tool does
(create AES-128 → harvest → delete, delete guaranteed on failure paths).
Rides the M6-040 binding internals — one token acquisition, NET-006
pacing between round-trips. N bounds 1..4096 tool-side; SDK len bound:
any nonzero size_t (each 16-byte chunk is one round-trip — document the
cost). Windows binary-safe --raw like keys pub/sign (_setmode).

**Definition of done**
- [ ] `ehem_random` exported, tagged `implements: REQ-OPS-002`; hem-tool
      `random` in hem-tool-core, tagged `implements: REQ-TOOL-010`
- [ ] Unit tests green (gcc+clang+asan): len 1..48 → ⌈len/16⌉ requests,
      IV concatenation byte-exact incl. partial tail, NULL/zero-len →
      EHEM_ERR_ARG no I/O; tool: hex/--raw output, create→harvest→delete
      sequence without --kid (delete asserted on injected failure too),
      N=0/N>4096/non-numeric → exit 2 no I/O
- [ ] Live: two ehem_random calls differ; each harvested IV matches the
      encrypt response IV; `hem-tool random 32` (no --kid) prints 64 hex
      chars, two runs differ, no EHEMTEST leftovers (REQ-TEST-003)
- [ ] Export + public-header gates green
