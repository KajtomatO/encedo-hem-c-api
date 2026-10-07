---
id: STEP-M10-050
title: "Registry: manual-recovery help section; settle the sub U/M question for config writes"
milestone: M10
implements: ["REQ-TOOL-019"]
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#11-milestones"]
depends_on: []
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** REQ-TOOL-019 rev 2 realised: `hem_command` gains a section
field (data, not prose); `hem_help_top` renders the three auth groups
and then a trailing **"manual recovery"** section holding `cert-install`
and `tls-recover` (and nothing else); per-command help still shows each
one's auth class. `test_registry.c` extended (section membership,
rendering order, the completeness walk).

**Notes:** Second deliverable — the open criterion in REQ-TOOL-019:
firmware `api_system.c:1060-1066` demands token `sub` U or M on EVERY
`POST /api/system/config`, which a mobile bearer (`sub=base64(kid)`)
cannot satisfy. Settle it by firmware read (the source of record) plus,
if the device cert happens to be expired, one live `cert-install
--mobile` probe; also read the reboot handler's role check. If
confirmed: `cert-install` and `tls-recover` become PASSPHRASE_ONLY in
the registry, the tool_auth chokepoint rejects `--mobile` for them with
the ext-pair message shape, and REQ-TOOL-018 gets a rev recording the
correction (its "every bearer-needing subcommand" claim narrows). Record
the answer in REQ-TOOL-019 and REQ-TOOL-018. REQ-TOOL-019 returns to
`verified` through §4.3 once the new test passes.

**Definition of done**
- [ ] Section field + rendering; `cert-install`/`tls-recover` appear only
      in the manual-recovery section; new M10 commands land in their
      auth groups as they arrive.
- [ ] test_registry.c: section membership + order + completeness green.
- [ ] sub U/M question answered and recorded (REQ-TOOL-019 criterion
      checked; REQ-TOOL-018 rev if the membership changed; registry auth
      classes match).
- [ ] `./dev ci` + gates green; `implements: REQ-TOOL-019` tag kept.
