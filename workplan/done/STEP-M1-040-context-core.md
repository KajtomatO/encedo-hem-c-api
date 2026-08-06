---
id: STEP-M1-040
title: Context core — ehem_ctx lifecycle, options, error enum, last-error
milestone: M1
implements: ["REQ-API-001", "REQ-API-002", "REQ-API-003", "REQ-API-004"]
traces:
  architecture: ["ARCHITECTURE.md#4-public-api--conventions"]
depends_on: ["STEP-M1-010"]
evidence:
  commits: []   # to be recorded at commit time (user runs commits)
  tests: ["verifies: REQ-API-001, REQ-API-002, REQ-API-003, REQ-API-004 (tests/unit/test_context.c)"]
  notes: >
    Public core in include/ehem/ehem.h: the full 14-value ehem_rc enum
    (EHEM_OK==0, append-only) with ehem_rc_str(); ehem_options (leading
    abi_size stamped by ehem_options_init() — the §4 size/version discipline,
    enforced via an EHEM_OPT_HAS offset guard so the struct can grow
    append-only) carrying connect/total timeouts (REQ-NET-004 defaults
    10s/30s), the three TLS trust modes (REQ-NET-003), a caller CA-file path,
    and a forward-declared ehem_transport override slot (REQ-NET-001);
    opaque ehem_ctx with ehem_ctx_create/destroy; ehem_error + ehem_last_error;
    idempotent ehem_global_init/cleanup. Internal src/context.h + context.c
    hold struct ehem_ctx and the ehem_ctx_fail()/ehem_ctx_clear_error()
    helpers that the transport and bindings will use. URL validation requires
    http(s):// + non-empty host → EHEM_ERR_ARG; CA_FILE mode without a path →
    EHEM_ERR_ARG; uninitialized options (abi_size 0) → EHEM_ERR_ARG;
    destroy(NULL) is a no-op; destroy zeroizes the struct (credential-wipe
    policy for M2). The single sanctioned file-scope mutable (REQ-API-002) is
    the g_global_ready guard in the global-init wrapper; curl_global_init lands
    in its body at STEP-M1-060. Verified on Linux (2026-07-15): GCC `ctest -L
    unit` 4/4 green (test_context 11 cases), unit suite clean under GCC
    -fsanitize=address,undefined, and `nm -D` on the .so exports only the eight
    ehem_* symbols. Two contexts shown independent (a failure on one does not
    surface on the other).
reopened: []
cancelled: null
---

**Goal:** `ehem_ctx` create/destroy with an options structure (URL,
timeouts, TLS trust mode, transport override slot), the complete `ehem_rc`
enum with `ehem_rc_str()`, `ehem_last_error` detail storage, and idempotent
`ehem_global_init`/`ehem_global_cleanup`.

**Notes:** The full 14-value enum ships now even though auth values only
become producible in M2+ — the ABI surface is fixed early (REQ-API-003
rationale). Options struct extensibility (how it may grow before 1.0) gets
a first answer here; keep it consistent with the §4 size/version
discipline note. All mutable state lives in the ctx struct (REQ-API-002).

**Definition of done**
- [x] Create/destroy with URL validation (`EHEM_ERR_ARG` on bad input); destroy(NULL) safe — *test_create_rejects_bad_args, test_destroy_null_is_safe*
- [x] Two contexts coexist independently (unit test) — *test_two_contexts_independent (isolated last-error state)*
- [x] `ehem_rc` complete; `ehem_rc_str()` covers every value (iterating unit test) — *test_rc_str_covers_every_value iterates EHEM_OK..EHEM_ERR_UNSUPPORTED*
- [x] `ehem_last_error` returns HTTP status/device payload/message; reset on success (unit tests, fake transport may be stubbed locally until STEP-M1-050 lands) — *test_last_error_set_and_reset via internal ehem_ctx_fail/clear_error*
- [x] Global init/cleanup idempotent (double-init/double-cleanup unit test) — *test_global_init_cleanup_idempotent*
- [x] Unit suite ASan/LSan-clean on Linux — *GCC -fsanitize=address,undefined, 4/4 green*
