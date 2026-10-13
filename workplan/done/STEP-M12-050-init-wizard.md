---
id: STEP-M12-050
title: "hem-tool init-wizard — no-echo prompt helper + attended prompt-driven initialisation over hem_init_device_run (init-device stays scripted)"
milestone: M12
implements: ["REQ-TOOL-028"]
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#9-testing-policy", "ARCHITECTURE.md#11-milestones"]
depends_on: []
evidence:
  commits: ["7f516f5", "78a7940", "0d51b34", "4f697cd"]   # start; implementation; done; post-done release-build fix
  tests:
    - "tests/unit/test_wizard.c (verifies: REQ-TOOL-028) — 7 scripted dialogues over the fake transport: all defaults + typed mnemonic, generated words (seam) and explicit answers each POST byte-identical bodies to hem_init_device_run with the equivalent flags; passphrase empty/mismatch re-asks; wrong backup word re-shows the words; wrong hostname / end of input at six points → exit 6 with zero requests; invalid mnemonic re-asks without quoting it; 406 → 3; reboot question yes/no; no passphrase and no post-display word in any output"
    - "tests/unit/test_prompt.c (verifies: REQ-TOOL-028) — the non-terminal path verbatim (incl. the exactly-full-line boundary, CRLF, over-long, EOF, the one notice); on a POSIX pseudo-terminal: echo off during the hidden read (nothing echoed to the keyboard side), restored after the normal return, after Ctrl-D and after SIGINT (which still kills the process); 30/30 repeated runs"
    - "tool_startup_cli (check_tool_startup.cmake, verifies: REQ-TOOL-028 among others) — on the real main.c: `init-wizard --user alice` → exit 2; empty stdin → exit 6, nothing sent"
    - "test_registry / test_init_tool unchanged and green (top page 45 lines; init-device's exits and --reboot behaviour unchanged by the confirm_reboot hook)"
    - "./dev ci 54/54 gcc + clang; ./dev test asan 54/54; MinGW -fsyntax-only clean for prompt.c (Windows console branch), wizard.c, init_cmd.c, main.c, registry.c, test_wizard.c, test_prompt.c; prompt.c / wizard.c also compiled -O3 -Wall -Wextra -Werror with gcc and clang"
    - "2026-10-13, after done: the release workflow's Linux job (run 38407235207, ubuntu-22.04: GCC 11.4, cmocka 1.1.5, Release -O3) failed to compile test_prompt.c — cmocka 1.1.5 does not declare skip() noreturn (1.1.7 does), so GCC 11 saw `slave` used after a skip() that returns (-Werror=maybe-uninitialized). Fix 4f697cd: `return` after both skip() calls. Reproduced and re-checked with GCC 11.4 (conda-forge) + jammy's cmocka 1.1.5 header + Ubuntu's hardening defaults: the release configure's 139 compile commands clean after the fix (test_prompt.c the only failure before); a full GCC 11 Release build against the pinned prefix passes the unit suite 54/54; ./dev ci 54/54"
  notes: |
    OPEN criterion settled at start, as the step plan recommended (REQ-TOOL-028
    rev 2): --url / EHEM_URL is the only pre-fill. EHEM_PASSPHRASE and
    EHEM_MASTER_WORDS are ignored with one stderr notice; init-device's input
    flags are REFUSED with exit 2 (a scripted run belongs to init-device) —
    a slightly firmer reading than "ignored" for explicit flags, chosen so an
    operator never believes a flag took effect. FLAG FOR THE USER: this is
    the recommended default, reversible if a re-run convenience is wanted.
    prompt.{h,c} (hem-tool-core): hem_prompt_line / hem_prompt_secret over
    injectable in/out/err; echo off only on a terminal — POSIX termios
    (TCSAFLUSH, like getpass) with SIGINT/SIGTERM handlers that restore and
    re-raise; Windows SetConsoleMode(~ENABLE_ECHO_INPUT) on
    GetStdHandle(STD_INPUT_HANDLE) with a SetConsoleCtrlHandler restore. Off
    a terminal: plain lines, one "not a terminal" notice, and the prompt's
    line ended by the helper so transcripts stay one prompt per line.
    wizard.{h,c}: the explanations and defaults are Encedo Manager's own —
    index.html form texts ("Use Encedo backend service as a trusted source
    of current time", "Allow remote management ... reporting stolen devices,
    remote wipes", "Allow search keystore by description without required
    access token", the hostname/IP hints) and build.js values (cfg dnsd
    false / origin "*" / ctx 0; layeredStorageEncrSet → the six storage
    codes 81 83 161 163 80 160, offered as a list and enforced; disk slider
    2 MiB steps; ip 192.168.7.1/24). Generated words: shown once (numbered,
    4 per row), backup check by three random positions read hidden (C rand
    seeded from the clock — a check, not a secret), wrong → shown again.
    Typed mnemonic: hidden, validated with ehem_master_secret_from_mnemonic
    WITHOUT a ctx so no error detail can quote a word back. init_cmd gained
    hem_init_opts.confirm_reboot/confirm_arg (NULL for init-device, whose
    behaviour is unchanged); the wizard asks after the init reports
    reboot_required. Top-level help reaches exactly 45 lines.
    Bug found and fixed while testing: read_line reported a line of exactly
    capacity-1 characters as too long (its newline did not fit) — it now
    peeks the next character. Test race found and fixed: the pty typist
    polled for a transient "echo on" between two reads (microseconds) and
    hung the test — replaced by a pipe handshake + alarm(20) so a broken pty
    fails instead of hanging CI.
    Attended (REQ-TEST-007): the end-to-end run on the second unit on Linux
    and Windows consoles is the gate's (STEP-M12-140) — REQ-TOOL-028's
    attended criterion stays open.
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
- [x] `prompt.{h,c}` helper: tty echo off/on (POSIX + Windows), non-tty
      plain-line path with the one-time stderr notice; unit test for the
      non-tty path reading lines verbatim.
- [x] `wizard.{h,c}` + `init-wizard` registry entry (auth none, full
      per-command help); top-level help ≤ 45 lines.
- [x] Unit (`verifies: REQ-TOOL-028`, scripted streams, fake transport):
      all-defaults run produces byte-identical init POST body to
      `init-device` with the equivalent flags; passphrase mismatch
      re-prompts; wrong backup word re-shows + retries; cancel at the
      summary → exit 6, zero transport I/O; existing mnemonic skips
      generation; the 24 words appear in no output after the one display;
      the passphrase appears in no output.
- [x] Exit codes 0–5 map exactly as `init-device` (same verdict mapping),
      6 = cancelled; documented in the per-command help.
- [x] REQ-TOOL-028 OPEN criterion (environment pre-fill) resolved and
      recorded (rev bump); the attended-run criterion stays open for the
      gate.
- [x] `./dev ci` gcc + clang + ASan, ASCII gate, MinGW syntax check green;
      `implements: REQ-TOOL-028` tag.
