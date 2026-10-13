---
id: REQ-TOOL-024
title: hem-tool initialises the SDK up front and refuses a foreign crypto library with a clear message and its own exit code
status: approved
priority: must
revision: 2   # 2026-10-13 (STEP-M12-020): message text and init placement settled; unit and documentation criteria recorded. No meaning change — approved status kept
source: ARCHITECTURE.md §11 M12 part A ("hem-tool refuses up front — hem-tool calls ehem_global_init explicitly at startup (today it calls only the cleanup) and exits with a clear message and an exit code of its own"; user decision 2026-10-10); KNOWN-ISSUES.md "Release hem-tool with a foreign libwolfssl crashes"; REQ-API-010 (the SDK check), REQ-API-011 (the versions for the message); approved 2026-10-12 (M12 decomposition, user: "approve")
depends_on: ["REQ-API-010", "REQ-API-011", "REQ-TOOL-001"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#11-milestones"]
---

# hem-tool initialises the SDK up front and refuses a foreign crypto library

hem-tool SHALL call `ehem_global_init()` before any other SDK call — right
after argument parsing, before any output or traffic — and, when it
fails, exit with **code 12** after one stderr message naming the cause:
for `EHEM_ERR_BACKEND_MISMATCH` the crypto backend, the version hem-tool
was built against, the version it loaded (REQ-API-011) and the remedy
(put the library from the release's `wolfssl-<v>-<os>-x86_64` asset next
to the binary; or use the `-static` hem-tool, REQ-BUILD-009); for any
other code the `ehem_rc_str()` name.

- Today `main.c` calls only `ehem_global_cleanup()` at the end; the init
  happens implicitly inside the first `ehem_ctx_create()`, deep in a
  command, and a mismatch crashes before any check (1.1.0).
- Exit code **12** is new and reserved for "the SDK could not be
  initialised" (today's codes: 0, 1, 2 = usage/environment, 3–9 per
  command, 13/14 mobile). It is distinct from 2 so that scripts can tell
  a broken installation from a wrong command line. REQ-TOOL-026 (Windows
  missing DLL) uses the same code — one "install the companion library"
  class.
- `help`, `--help`, `--version` (REQ-TOOL-025) and `keys types`
  (REQ-TOOL-027) run **before** the init and work on any library — the
  user can always read the help and see what hem-tool expected.
- The message is ASCII (REQ-API-009) and the exit code is documented in
  the top-level help's exit-code note and in the archive README.

**Rationale:** a crash with "stack smashing detected" (or 0xC0000135 on
Windows) tells the operator nothing; the SDK can now detect the
condition (REQ-API-010), and the tool is where the human reads the
answer. Doing it first also removes the only implicit-init path in the
tool.

**Settled at STEP-M12-020 (2026-10-13).** Placement in `main.c`: argument
parsing and its usage errors (exit 2, including the REQ-TOOL-019 auth-class
check, now run before the URL resolution) → `--version` / `version` → the
help pages → **`hem_tool_global_init()`** (hem-tool-core `startup.c`) → the
REQ-TOOL-017 URL notice → dispatch. So the refusal is the first and only
output of a refused run. `HEM_TOOL_EXIT_INIT` (= 12) lives in `startup.h`
for REQ-TOOL-026's hook. The message (stderr, ASCII, `%s` only):

```
error: hem-tool cannot use the wolfSSL library it loaded (EHEM_ERR_BACKEND_MISMATCH)
  wolfSSL: built against 5.7.2, loaded 5.6.6
  remedy: put the wolfSSL library from the release asset wolfssl-5.7.2-linux-x86_64
          next to hem-tool, or use the -static hem-tool (1.2.0 and later),
          which needs no separate crypto library
```

Equal versions (the struct self-test refused a same-version library built
with other options) append `- same version, incompatible build` to the
second line; a non-wolfSSL backend's remedy names that library instead of
the companion asset; any other init failure prints
`error: the SDK could not be initialised: <ehem_rc_str>` — exit 12 in every
case. An unknown command under a refused library also exits 12 (the
command is not looked up before the init — the installation is the
problem either way).

**Acceptance criteria:**
- [ ] Unit (hem-tool-core / main.c smoke, REQ-API-010 seam forcing a
      mismatch): `hem-tool status` prints one message with the backend
      name, both versions and the remedy, exits 12, and performs zero
      transport I/O; `hem-tool help`, `--help`, `--version`, `keys types`
      still succeed under the same forced mismatch.
      *(2026-10-13, STEP-M12-020 — all but `keys types`, which joins the
      pre-init commands with STEP-M12-040 and is added to the same check
      there: `tests/unit/test_tool_startup.c` (the message for both seam
      modes, exactly one `error:` line, ASCII) and `tool_startup_cli`
      (`check_tool_startup.cmake`) on the REAL `main.c` — a test build
      whose constructor (`tool_force_mismatch.c`) forces the refusal from
      `EHEM_TEST_FORCE_MISMATCH`: `status` → exit 12, empty stdout, no
      default-URL notice before the message; `keys list` → 12; `help`,
      `--help`, `status --help`, `help keys rm`, `--version`, `version`
      → 0; zero I/O shown by the URL `http://127.0.0.1:1` — the same run
      without the seam reaches the transport and exits 1.)*
- [x] Without a forced mismatch every command behaves as before
      (existing unit suite green; `ehem_global_init` is called exactly
      once per run — the SDK's idempotency makes a second implicit call
      harmless).
      *(`./dev ci` 51/51 gcc + clang, ASan 51/51; one explicit call in
      `main.c`; usage errors keep exit 2 under the seam — `version extra`,
      no command, `--mobile cert-install`.)*
- [ ] Demonstrated at the M12 gate on Linux: the release dynamic hem-tool
      in a directory WITHOUT the companion library, on a host whose
      `libwolfssl.so.42` is 5.6.6 → the message, exit 12, no crash
      (recorded with date and versions; this is REQ-API-010's gate
      criterion seen from the tool).
- [x] Documentation: the exit code in the help page's exit-code note
      (page still ≤ 45 lines, REQ-TOOL-020) and in the release README.txt.
      *(The top page's closing line now reads ``command options: `hem-tool
      help <command>`; exit 12 = crypto library missing or wrong`` — 43
      lines, test_registry asserts `exit 12`; `scripts/release/package.sh`
      README.txt paragraph "hem-tool checks the wolfSSL it loaded …" and
      the job log runs `hem-tool --version`.)*
