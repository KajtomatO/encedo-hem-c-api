---
id: REQ-SYS-005
title: Binding for /api/system/reboot (pulled forward from M7 for cert install)
status: approved
priority: should
revision: 1
source: user decision 2026-07-16 (cert-install needs reboot-to-apply); encedo-hem-api-doc system/reboot.md; encedo-hem-python-api system.py (token invalidation semantics)
depends_on: ["REQ-AUTH-003"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#6-protocol-bindings", "ARCHITECTURE.md#11-milestones"]
---

# Binding for /api/system/reboot (pulled forward from M7 for cert install)

The SDK SHALL provide `ehem_system_reboot(ctx)` — authenticated
`GET /api/system/reboot`. A reboot invalidates every token the device
issued, so the binding SHALL drop the context's token cache after a
successful call (credential material retained per REQ-AUTH-002 allows
transparent re-login afterwards).

Pulled forward from M7 because installing a TLS certificate
(REQ-SYS-004) only takes effect at boot — firmware v1.2.2 loads the TLS
server certificate exclusively from the flash key repo at start
(REQ-SYS-003 root-cause finding). The rest of M7's system group (shutdown,
upgrade, diag, selftest) stays in M7.

**Scope:** the device accepts `system:upgrade`, `system:config`, or
`system:shutdown` for reboot; the SDK SHALL request `system:config` so
cert-install works with a single scope acquisition.

**Acceptance criteria:**
- [ ] Sends authenticated GET with scope `system:config`; drops the token
      cache on success (fake-transport unit test: a subsequent
      authenticated call re-acquires).
- [ ] Errors map per REQ-API-003 (unit test).
- [ ] Live testing is disruptive-gated (CTest label `disruptive` +
      `EHEM_ALLOW_DISRUPTIVE=1`, never in the default run or CI —
      ARCHITECTURE §9) — a reboot interrupts the device for all users.
