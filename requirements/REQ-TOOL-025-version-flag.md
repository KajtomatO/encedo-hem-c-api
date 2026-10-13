---
id: REQ-TOOL-025
title: hem-tool --version — SDK version, crypto backend compiled-against and loaded versions, libcurl version
status: approved
priority: should
revision: 1
source: user decision 2026-10-12 (M12 planning: "`hem-tool --version` — the SDK version, the wolfSSL version compiled against and the one loaded, and the libcurl version (today `--version` is an unknown option)"); ARCHITECTURE.md §11 M12 part A; REQ-API-011 (the facts); approved 2026-10-12 (M12 decomposition, user: "approve")
depends_on: ["REQ-API-011", "REQ-TOOL-020"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#11-milestones"]
---

# hem-tool --version

hem-tool SHALL accept `--version` (and `version` as a command spelling,
like `help`) and print to stdout, exit 0, without touching the network
or requiring a successful SDK init: its own version (`ehem_version()` —
the tool and the SDK share the project version), the crypto backend name
with the version compiled against and the version loaded, and the
libcurl versions compiled against and loaded (REQ-API-011); the link
mode of the crypto library (`dynamic` / `static`, a compile-time fact of
the build) is shown with it.

Proposed output (exact text settled at the step):

```
hem-tool 1.2.0
crypto:    wolfSSL 5.7.2 (compiled) / 5.7.2 (loaded), dynamic
transport: libcurl 8.10.1 (compiled) / 8.10.1 (loaded), static
```

- When the loaded version differs from the compiled one the two values
  make it visible at a glance (the REQ-TOOL-024 refusal happens on the
  next real command, not here).
- The top-level help stays within its 45-line cap: `--version` is folded
  into the existing usage lines, not a new option line.

**Rationale:** 1.1.0 users could not tell which wolfSSL their binary was
built against or picked up — the first question in every foreign-library
report. Standard CLI hygiene; `--version` being "unknown option" today
was flagged by the user.

**Acceptance criteria:**
- [ ] `hem-tool --version` and `hem-tool version` print the three lines,
      exit 0, zero transport I/O; `hem-tool --version status` is still a
      version print (the flag wins, like `--help`) — unit smoke.
- [ ] Values equal what REQ-API-011 reports and `ehem_version()`; the
      output is ASCII (REQ-API-009 gate) and stable enough for scripts
      (first line `hem-tool <version>`).
- [ ] Works under a forced REQ-API-010 mismatch (unit) and on both
      backends — the OpenSSL CI leg shows `OpenSSL 3.x`.
- [ ] Top-level help ≤ 45 lines with the usage line amended
      (test_registry test_top_page_shape stays green).
