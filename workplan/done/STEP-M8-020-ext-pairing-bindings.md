---
id: STEP-M8-020
title: ExtAuth pairing bindings — ehem_ext_init/validate/mac + live simulated pairing
milestone: M8
implements: ["REQ-AUTH-006"]
traces:
  architecture: ["ARCHITECTURE.md#5-auth--session", "ARCHITECTURE.md#6-protocol-bindings"]
depends_on: ["STEP-M8-010"]
evidence:
  commits: ["c636298", "72a459a"]
  tests: ["verifies: REQ-AUTH-006 — tests/unit/test_ext.c (7 cases: wire shapes, bearer, epk/pid client-side validation with zero traffic, missing-field PROTOCOL, 403 SCOPE_DENIED, 406 DEVICE naming slots/dedup, 409, NULL-safe frees); tests/integration/test_ext_pair_live.c (full simulated pairing live, 2/2 stable)"]
  notes: "LIVE (my.ence.do fw v1.2.2, post power-cycle + checkin): init request-JWT sig verified with ECDH(eph,eid) — construction + STANDARD-base64 claims confirmed; validate code == local HMAC recipe; dedup re-pair → 406 empty payload; mac verified locally; EXTAID key cleaned up. NEW FW QUIRK pinned: /keymgmt/get omits descr for the ext-paired key (list/search return the full EXTAID+pid blob; get also shows bare type CURVE25519 vs list's ECDH,CURVE25519) — REQ-AUTH-006 rev 2. Unit 34/34 gcc+clang+ASan; export/header gates green; proto_ext.c MinGW cross-syntax clean. Session note: the pre-power-cycle device stall interrupted the first live attempt (KNOWN stall mode)."
reopened: []
cancelled: null
---

**Goal:** `ehem_ext_init`, `ehem_ext_validate`, `ehem_ext_mac` public
bindings (scope `auth:ext:pair` via ensure_token; typed info structs +
free functions), unit-tested against the fake transport and live-proven
by a full simulated pairing on the real device.

**Notes:** new `src/proto_ext.c`; public declarations join
`include/ehem/auth.h` (the ext endpoints are the auth doc group — no
new header). Pre-validate epk/pid lengths → `EHEM_ERR_ARG` before I/O.
Live test (integration label): init → sim reply (EHEMTEST label; echo
the request `jti`) → validate → verify `code` locally via the shim
recipe → `ehem_key_get` shows the EXTAID key → delete; separately probe
mac and verify locally; record the base64 alphabet of code/nonce/mac
(std vs url) in REQ-AUTH-006. Probe the 406 double-pairing (dedup)
case with the SAME sim identity before cleanup. Per-run-unique material
everywhere else (repo dedup, M7-072 lesson).

**Definition of done**
- [x] Unit: shapes, scope, 401/403/406/409 mapping, arg validation
      (fake transport)
- [x] Live simulated pairing green incl. local `code` verification and
      EXTAID cleanup; mac verified locally; base64 variant recorded in
      REQ-AUTH-006 (STANDARD, padded)
- [x] 406 dedup-vs-slots probe result recorded in REQ-AUTH-006 (dedup →
      406 empty payload, wire-identical to slot exhaustion)
- [x] `./dev ci` + asan green; export/header gates green; tags
      `implements:`/`verifies: REQ-AUTH-006` placed; evidence filled
