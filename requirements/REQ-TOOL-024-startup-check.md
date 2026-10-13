---
id: REQ-TOOL-024
title: hem-tool initialises the SDK up front and refuses a foreign crypto library with a clear message and its own exit code
status: approved
priority: must
revision: 1
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

**Acceptance criteria:**
- [ ] Unit (hem-tool-core / main.c smoke, REQ-API-010 seam forcing a
      mismatch): `hem-tool status` prints one message with the backend
      name, both versions and the remedy, exits 12, and performs zero
      transport I/O; `hem-tool help`, `--help`, `--version`, `keys types`
      still succeed under the same forced mismatch.
- [ ] Without a forced mismatch every command behaves as before
      (existing unit suite green; `ehem_global_init` is called exactly
      once per run — the SDK's idempotency makes a second implicit call
      harmless).
- [ ] Demonstrated at the M12 gate on Linux: the release dynamic hem-tool
      in a directory WITHOUT the companion library, on a host whose
      `libwolfssl.so.42` is 5.6.6 → the message, exit 12, no crash
      (recorded with date and versions; this is REQ-API-010's gate
      criterion seen from the tool).
- [ ] Documentation: the exit code in the help page's exit-code note
      (page still ≤ 45 lines, REQ-TOOL-020) and in the release README.txt.
