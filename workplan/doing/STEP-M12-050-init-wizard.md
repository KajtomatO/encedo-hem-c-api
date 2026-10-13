---
id: STEP-M12-050
title: "hem-tool init-wizard — no-echo prompt helper + attended prompt-driven initialisation over hem_init_device_run (init-device stays scripted)"
milestone: M12
implements: ["REQ-TOOL-028"]
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#9-testing-policy", "ARCHITECTURE.md#11-milestones"]
depends_on: []
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** `hem-tool init-wizard` walks an operator through device
initialisation — intro, one explained prompt per `cfg` field with the
Manager's default, hidden passphrase typed twice, 24 generated words
shown once and confirmed by re-typing three random positions (or an
existing mnemonic typed in), a summary behind the typed hostname, then
the **same** init flow as `init-device` (`hem_init_device_run`) including
the reboot question — exit codes 0–5 as `init-device`, **6** = cancelled
(zero traffic until the typed confirmation). A shared no-echo prompt
helper (termios / `SetConsoleMode`) is new to hem-tool-core.

**Notes:** New hem-tool-core files `prompt.{h,c}` (line reader over
injectable in/out/err streams so the unit test scripts the dialogue;
echo off via `tcgetattr`/`ECHO` on POSIX and `SetConsoleMode` without
`ENABLE_ECHO_INPUT` on Windows, restored on every return path incl. the
Ctrl-C handler where the platform allows; when stdin is not a tty the
helper reads a plain line and says so once on stderr) and `wizard.{h,c}`
(builds a `hem_init_opts` — `src/tools/hem-tool/init_cmd.h` — and calls
`hem_init_device_run(ctx, &opts)`, so the POST body equals `init-device`'s
by construction; the unit test still byte-compares through the fake
transport). Words: `ehem_mnemonic_generate` (REQ-AUTH-012); the three
backup positions come from a seeded source behind a test seam so the
scripted test knows them; a wrong word re-shows the words and retries.
Typed-hostname confirmation: reuse `wipe.c`'s pattern. Defaults are the
Manager's (`init_cmd.h`: ip 192.168.7.1/24, storage mode 81, disk0
8388608 …) — the prompts explain each field in one line (cite
`assets/build.js` where a default is the Manager's, never the legacy
files). Registry: `init-wizard`, auth `HEM_AUTH_NONE` (the passphrase is
input data, REQ-TOOL-021), section main; top-level help reaches 45/45
with STEP-M12-040 — `test_top_page_shape` is the tripwire. Resolve the
REQ's OPEN criterion at start and record it (rev bump): recommended —
`EHEM_URL`/`--url` is the only pre-fill; `EHEM_PASSPHRASE` and
`EHEM_MASTER_WORDS` are deliberately ignored (nothing secret from the
environment in an interactive flow; a re-run types again). Attended-only
(REQ-TEST-007): no live CTest; the end-to-end run on the second unit —
Linux and Windows consoles, echo-off behaviour included — is the gate's
(STEP-M12-140) and needs an uninitialised device (a wipe first; the user
decides at the sitting). ASCII-only strings (`check_ascii_strings`), `%s`
only; `SetConsoleMode` on `GetStdHandle(STD_INPUT_HANDLE)`; the MinGW
syntax check must cover the Windows branch.

**Definition of done**
- [ ] `prompt.{h,c}` helper: tty echo off/on (POSIX + Windows), non-tty
      plain-line path with the one-time stderr notice; unit test for the
      non-tty path reading lines verbatim.
- [ ] `wizard.{h,c}` + `init-wizard` registry entry (auth none, full
      per-command help); top-level help ≤ 45 lines.
- [ ] Unit (`verifies: REQ-TOOL-028`, scripted streams, fake transport):
      all-defaults run produces byte-identical init POST body to
      `init-device` with the equivalent flags; passphrase mismatch
      re-prompts; wrong backup word re-shows + retries; cancel at the
      summary → exit 6, zero transport I/O; existing mnemonic skips
      generation; the 24 words appear in no output after the one display;
      the passphrase appears in no output.
- [ ] Exit codes 0–5 map exactly as `init-device` (same verdict mapping),
      6 = cancelled; documented in the per-command help.
- [ ] REQ-TOOL-028 OPEN criterion (environment pre-fill) resolved and
      recorded (rev bump); the attended-run criterion stays open for the
      gate.
- [ ] `./dev ci` gcc + clang + ASan, ASCII gate, MinGW syntax check green;
      `implements: REQ-TOOL-028` tag.
