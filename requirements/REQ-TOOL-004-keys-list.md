---
id: REQ-TOOL-004
title: hem-tool keys list — inventory with protected-key marking
status: verified
priority: must
revision: 1
source: ARCHITECTURE.md §8, §11 (M3 gate = goal.txt tool milestone); requirements/start_point/goal.txt ("a tool that will list keys"); encedo-hem-python-api wipe_keys.py --list (reference behavior); approved 2026-07-16
depends_on: ["REQ-TOOL-001", "REQ-TOOL-005", "REQ-KEY-001"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli"]
---

# hem-tool keys list — inventory with protected-key marking

hem-tool SHALL provide a `keys list` subcommand that walks the device's
full key repository (REQ-KEY-001 full walk) and prints, per key: kid,
label, and type, with protected keys (REQ-TOOL-005) visibly marked
(`[PROTECTED]`), preceded or followed by a summary line with the total
and protected counts — mirroring `wipe_keys.py --list`. The subcommand
is strictly read-only. Connection and credentials follow the existing
tool conventions (`EHEM_URL`/`--url`, `EHEM_PASSPHRASE`/`--passphrase`);
exit 0 on success, 2 on usage/environment errors, 1 on runtime failure
with an actionable message.

**Rationale:** first half of the goal.txt tool milestone and the M3 gate
demo; also the operator's view for choosing `keys rm` targets. Like
cert-install, the listing/formatting logic lives in the shared
`hem-tool-core` static lib so the unit test and the CLI share one code
path.

**Acceptance criteria:**
- [x] Against the fake transport: multi-page repo prints every key once,
      protected entries marked, summary counts correct (unit test on the
      hem-tool-core function). — test_keys_list_marks_and_counts (12 keys
      over 2 pages, 3 protected marked, "12 key(s), 3 protected").
- [x] Missing URL/passphrase → exit 2 with a usage message; auth failure →
      exit 1 with the mapped error text (unit tests). —
      test_keys_list_no_passphrase (exit 2, no I/O), test_keys_list_auth_failure
      (exit 1); missing URL → make_ctx exit 2 (verified via the CLI).
- [x] Read-only: no request other than auth + list is issued (unit test
      asserting the request sequence). — test_keys_list_marks_and_counts
      asserts exactly 4 requests (login 2 + 2 list pages).
- [x] Live: `hem-tool keys list` against the dev device shows its keys
      with the TLS pair marked `[PROTECTED]` (M3 gate, manual/integration). —
      run 2026-07-16 (see step evidence): 6 keys, `TLS PrivateKey` /
      `TLS Certificate` / `SM-S938B (Android)` marked `[PROTECTED]`,
      "6 key(s), 3 protected".
