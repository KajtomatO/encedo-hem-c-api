---
id: STEP-M1-090
title: hem-tool skeleton + status subcommand
milestone: M1
implements: ["REQ-TOOL-001"]
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli"]
depends_on: ["STEP-M1-060", "STEP-M1-070"]
evidence:
  commits: []   # to be recorded at commit time (user runs commits)
  tests: ["verifies: REQ-TOOL-001 (manual smoke; see notes — no unit test for a main())"]
  notes: >
    src/tools/hem-tool/main.c: dependency-free argv parser + a command dispatch
    (single `status` command today, table-ready for `keys list`/`keys rm` in
    M3). Global options --url (falls back to EHEM_URL), --cacert FILE
    (→ EHEM_TLS_CA_FILE + ca_file), --insecure (→ EHEM_TLS_INSECURE); --cacert
    and --insecure are mutually exclusive. status prints hostname (if present),
    uptime, temp, storage list, inited/https (if present) and hardware/
    firmware/bootloader versions; on any failure it prints the ehem_last_error
    detail (message + http status + device payload) and exits nonzero. Includes
    ONLY <ehem/ehem.h> + <ehem/system.h> (greppable — no internal headers).
    Built as a static-linked self-contained binary (option EHEM_BUILD_TOOL,
    default ON). Verified on Linux (2026-07-15):
      no args / status w/o URL / bad flag combo → usage on stderr, exit 2
      --help → exit 0
      status --url https://example.com → EHEM_ERR_NOT_FOUND (HTTP 404) with
        device payload printed, exit 1
      status --url https://nonexistent.invalid → EHEM_ERR_UNREACHABLE
        ("Could not resolve host") , exit 1; EHEM_URL env honored likewise
      status --url http://127.0.0.1:8099 against a local mock HEM (canned
        status/version JSON) → full happy-path output:
          hostname dev-hem.local / uptime 90061 s / temp 41.0 C /
          storage disk0:unlocked, disk1:locked / inited yes / https yes /
          hardware PPA rev 2.2 / firmware Encedo nGINE FW v1.2.2 /
          bootloader Encedo Secure Bootloader v2.0.1 , exit 0
    Live dev-machine HEM run is the M1-100 gate.
reopened: []
cancelled: null
---

**Goal:** The `hem-tool` executable (in `src/tools/hem-tool/`) with a
`status` subcommand: connects via `--url`/`EHEM_URL`, applies TLS flags
(`--cacert <file>`, `--insecure`), prints live status and version fields,
exits nonzero with `ehem_last_error` detail on any failure.

**Notes:** Consumes only `include/ehem/` public headers — hem-tool doubles
as living documentation of the API and the manual driver for the M1 gate.
Argument parsing stays dependency-free (plain argv loop); subcommand
dispatch is structured so `keys list`/`keys rm` (M3) slot in without
rework.

**Definition of done**
- [x] `hem-tool status --url https://...` prints status (uptime, temp, storage, hostname when present) and version (hardware, firmware, bootloader) — *demonstrated against a local mock HEM (see notes)*
- [x] `EHEM_URL` honored when `--url` absent; neither present → usage message, nonzero exit — *env fallback works; missing URL → exit 2*
- [x] `--cacert` and `--insecure` map onto the context TLS options — *set EHEM_TLS_CA_FILE+ca_file / EHEM_TLS_INSECURE; mutually exclusive guard*
- [x] Unreachable/TLS/protocol failures print last-error detail and exit nonzero — *404→NOT_FOUND w/ payload, DNS→UNREACHABLE, both exit 1*
- [x] Only public headers included (greppable) — *only <ehem/ehem.h> + <ehem/system.h>*
