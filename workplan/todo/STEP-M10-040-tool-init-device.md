---
id: STEP-M10-040
title: "hem-tool init-device: check-in, cfg flags, master secret, init over http"
milestone: M10
implements: ["REQ-TOOL-021"]
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#11-milestones"]
depends_on: ["STEP-M10-020"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** `hem-tool init-device` in hem-tool-core (new `init_cmd.c/h`,
registry entry, auth class NONE with the "passphrase is input data"
wording): check-in (REQ-SYS-003, sets the RTC) → `ehem_device_init` with
the cfg from flags (`--user`, `--email`, `--hostname`, `--ip` required;
the rest defaulted like the Manager) and the master secret from
`--master-secret-hex` or `--master-generate` (OS RNG, printed once with a
keep-this warning) → print `instanceid`, `reboot_required`, PEM `csr`
(`--csr-out FILE`), next steps; `--reboot` reboots and waits when
required. Exit codes 0/1/2/3 (already initialised)/4 (RTC still unset)/5
(cfg rejected).

**Notes:** **Resolve at step start (REQ-TOOL-021 open criterion):**
master-secret format — raw 32-byte hex as drafted, or BIP39-compatible
with the Manager's 24-word mnemonic (needs a vendored English wordlist +
PBKDF2-HMAC-SHA512 seed step, tool-only; the Manager uses
`seedM.substr(1, 64)` of the hex seed — mirror exactly if chosen).
`--master-generate` must never print the secret twice or log it. The
command runs against `--url http://<host>` (wiped device). Attended-only
(REQ-TEST-007): no live test; the attended init is the second act of
STEP-M10-070.

**Definition of done**
- [ ] Command + registry entry + per-command help listing every cfg flag
      with its default and the master-secret irreversibility.
- [ ] Unit (tests/unit/test_init_tool.c, fake transport): full sequence
      with Manager-default cfg; missing required flag → exit 2 with zero
      traffic; 406 → 3; 403 after check-in → 4; 400 → 5; secret printed
      only by `--master-generate`.
- [ ] Master-secret decision recorded in REQ-TOOL-021.
- [ ] No live test added; `./dev ci` + gates green; `implements:
      REQ-TOOL-021` tagged.
