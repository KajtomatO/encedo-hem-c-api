---
id: REQ-TOOL-028
title: hem-tool init-wizard — attended, prompt-driven device initialisation over the init-device flow
status: approved
priority: should
revision: 1
source: user note 2026-10-10 ("initialisation creator" — hand-hold the user through init; NOT designed then); shape decided by the user 2026-10-12 (M12 planning: a SEPARATE wizard command, `init-device` stays purely scripted; explains and prompts for every field with its default, passphrase hidden and twice, generates the 24 words and confirms the backup by re-typing a few, summary behind a typed confirmation, then the existing init flow incl. the reboot; needs a shared no-echo prompt helper); ARCHITECTURE.md §11 M12 part E (command name settled at decomposition: `init-wizard`); REQ-TOOL-021 (the flow it drives), REQ-AUTH-012 (the words), REQ-TEST-007 (attended-only); approved 2026-10-12 (M12 decomposition, user: "approve")
depends_on: ["REQ-TOOL-021", "REQ-AUTH-012", "REQ-TEST-007", "REQ-TOOL-020"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#9-testing-policy", "ARCHITECTURE.md#11-milestones"]
---

# hem-tool init-wizard

hem-tool SHALL provide `init-wizard`, an interactive command that
collects every `init-device` input through explained prompts and then
runs the **same** init flow (`hem_init_device_run`, REQ-TOOL-021) —
`init-device` itself stays scripted and unchanged.

- **Flow:**
  1. intro — what initialisation does, that the device must be
     uninitialised and reached over `http://`, and that the master
     secret cannot be rotated later;
  2. one prompt per `cfg` field with its explanation and the Manager's
     default in brackets, Enter = default (`user`, `email`, `hostname`
     have no default and re-prompt when empty; `ip A.B.C.D/N`,
     `storage-mode`, `disk0-size`, `origin`, `dnsd`, the three trust
     booleans, `gen-csr`, `ctx`);
  3. the passphrase, **hidden**, typed twice, re-prompted on mismatch
     (the device validates its content, as for `init-device`);
  4. the master secret: *generate 24 words* (default) or *type an
     existing mnemonic*; generated words are shown **once**, then the
     backup is confirmed by re-typing the words at three randomly
     chosen positions (a wrong word → the words are shown again, retry);
  5. a summary of every field (passphrase masked, words not shown)
     behind a typed confirmation — the hostname, as `wipe-device` does —
     then the init flow, asking whether to reboot now when the device
     requires it;
  6. the `init-device` result and next steps (as REQ-TOOL-021 prints
     them; no `recovery` hints — rev 3 of that REQ applies).
- **Exit codes:** 0–5 as `init-device` (same verdicts, same mapping);
  **6** = cancelled by the operator at any prompt (zero traffic until
  the typed confirmation).
- **No-echo prompt helper** in hem-tool-core (new to the tool): termios
  `ECHO` off / `SetConsoleMode` without `ENABLE_ECHO_INPUT`, restored on
  every return path including Ctrl-C where the platform allows; when
  stdin is not a terminal the helper reads a plain line (so the unit
  test can script the dialogue) and says so once on stderr.
- **Auth class:** none (the passphrase is input data, REQ-TOOL-021);
  registered in the no-credentials group; top-level help ≤ 45 lines
  (43 today + `keys types` + `init-wizard` = 45).
- **Attended-only** (REQ-TEST-007): no live CTest; the M12 gate sitting
  runs the wizard on the second unit and records the outcome here.

**Rationale:** `init-device` has 18 flags and a master secret whose loss
is unrecoverable — a scripted interface for an operator who does this
once. The wizard puts the explanations where the decisions are made and
makes the backup of the 24 words a step that cannot be skipped. Keeping
`init-device` untouched keeps automation stable (user decision
2026-10-12).

**Acceptance criteria:**
- [ ] Unit (hem-tool-core, scripted stdin/stdout/stderr, fake transport):
      a full run accepting every default produces exactly the init POST
      body that `init-device` produces for the equivalent flags
      (byte-compare through the fake transport); a passphrase mismatch
      re-prompts; a wrong backup word re-prompts after re-showing the
      words; cancel at the summary → exit 6 and zero transport I/O;
      typing an existing mnemonic skips generation.
- [ ] Unit: after the one display, the 24 words appear in no later
      output (the backup check prints positions only; the summary masks
      them); the passphrase never appears in any output.
- [ ] Unit: the no-echo helper's non-tty path reads lines verbatim; on a
      tty the echo state is restored after a normal and an early return
      (attended check on Linux and Windows consoles at the gate; recorded).
- [ ] Registry: `init-wizard` with auth class none and full per-command
      help (test_registry_complete); top-level help ≤ 45 lines.
- [ ] Attended (REQ-TEST-007), recorded here: date, device, the wizard
      run end to end on Linux and on Windows (the console echo-off
      behaviour included), then `status` + passphrase login.
- [ ] OPEN (settle at the step): whether `--master-words` /
      `EHEM_MASTER_WORDS` and `EHEM_PASSPHRASE` pre-fill the wizard's
      prompts (convenient for a re-run) or are deliberately ignored
      (nothing secret from the environment in an interactive flow).
