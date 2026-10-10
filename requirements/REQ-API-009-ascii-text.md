---
id: REQ-API-009
title: The SDK's and hem-tool's own text is 7-bit ASCII
status: draft
priority: should
revision: 1
source: user report 2026-10-10 (first Windows test of the release binary — "Printed info have strange characters": a UTF-8 em dash shown as "ÔÇö" in the console); ARCHITECTURE.md §4 (error detail), §8 (hem-tool)
depends_on: ["REQ-API-004"]
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#4-public-api--conventions", "ARCHITECTURE.md#8-hem-tool-cli"]
---

# The SDK's and hem-tool's own text is 7-bit ASCII

Every string and character literal in the SDK and hem-tool sources
(`src/`, `include/`; vendored third-party code excluded) — the SDK's
error-detail messages (REQ-API-004) and everything hem-tool prints —
SHALL consist of 7-bit ASCII characters only.

**Rationale:** a Windows console using a legacy code page (cp437, cp852 —
the default for many locales) renders UTF-8 multibyte characters as
mojibake: hem-tool's " — " printed as "ÔÇö" on the first Windows test of
the release binary (2026-10-10). The SDK's messages also end up in
consumers' logs and consoles (encedo-pkcs11) whose encoding the SDK cannot
control. ASCII renders identically everywhere, with no console mode
switching. Text that comes FROM the device (labels, user names, hostnames)
is passed through unchanged and is not covered. Comments are not covered
either — they never reach a user.

**Acceptance criteria:**
- [x] The non-ASCII literals in `src/` (88 em dashes, 1 arrow — SDK and
      hem-tool) are replaced with ASCII ("-", "->") (2026-10-10).
- [ ] The `ascii_strings` CTest gate (`unit` label;
      `tests/unit/check_ascii_strings.c`, a comment-aware lexer) fails on a
      non-ASCII byte in any string or character literal under `src/` or
      `include/` (vendored code excluded), and is green on Linux and
      Windows CI.
- [ ] Attended: hem-tool's output in a Windows console with a legacy code
      page shows no mojibake (e.g. `hem-tool status` with no `--url`).
