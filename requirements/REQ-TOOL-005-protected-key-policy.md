---
id: REQ-TOOL-005
title: Protected-key classification policy (label-based)
status: approved
priority: must
revision: 1
source: ARCHITECTURE.md §8 (protected-key policy); user decision 2026-07-15 (disposable device EXCEPT TLS material and paired authenticators); encedo-hem-python-api wipe_keys.py (_PROTECTED_LABELS / _PROTECTED_LABEL_SUBSTRINGS — authoritative reference); approved 2026-07-16
depends_on: []
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli"]
---

# Protected-key classification policy (label-based)

hem-tool SHALL classify a key as **protected** exactly when its label is
exactly `TLS PrivateKey` or `TLS Certificate` (the device's own TLS
material), or its label contains `(Android)` or `(iPhone)`
case-insensitively (paired phone authenticators). All other keys are
unprotected.

The classification is by **label only** — a client-side convention per
ARCHITECTURE §8 — deliberately not by algorithm or type, since the same
algorithm legitimately appears on non-protected keys (wipe_keys.py
records this as the spec choice). The classifier is implemented once, in
the shared `hem-tool-core` static lib, so `keys list` (marking),
`keys rm` (guarding), and the tests all use the same function; it is
tool policy, not SDK behavior — the SDK's delete binding (REQ-KEY-004)
stays policy-free.

**Rationale:** deleting the TLS pair can make the device unreachable;
deleting a paired-authenticator key breaks phone login. goal.txt demands
the tool handle "protected" keys like the python example; wipe_keys.py
is the reference implementation of the rule.

**Acceptance criteria:**
- [x] Table-driven unit test over the classifier: both exact TLS labels
      (and near-misses like `tls privatekey`, `TLS PrivateKey 2` →
      unprotected), `(Android)`/`(iPhone)` substrings in any case at any
      position → protected, ordinary labels → unprotected. —
      test_classifier_table (17 rows + NULL) in tests/unit/test_keys.c.
- [x] One shared classifier function used by both `keys list` and
      `keys rm` (no duplicated label constants — grep-checkable). —
      `hem_key_is_protected()` in src/tools/hem-tool/keys.c; grep confirms
      the label constants appear only there in production code. `keys list`
      (M3-050) marks with it; `keys rm` (REQ-TOOL-006, M3-060) will guard
      with the same function.
