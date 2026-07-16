---
id: REQ-BUILD-003
title: JSON handled by vendored cJSON
status: verified
priority: must
revision: 1
source: ARCHITECTURE.md §1 (vendored cJSON decision)
depends_on: []
supersedes: null
superseded_by: null
traces:
  architecture: ["ARCHITECTURE.md#1-decisions-fixed", "ARCHITECTURE.md#10-directory-layout"]
---

# JSON handled by vendored cJSON

All JSON encoding and decoding SHALL use the cJSON sources vendored at
`src/vendor/cjson`, adding no system JSON dependency.

**Rationale:** cJSON is two files, MIT-licensed, stable — vendoring removes
a whole class of packaging friction on Windows/MinGW. cJSON types stay
internal: they appear in neither public headers (REQ-API-001 opacity) nor
the shared library's export table (REQ-API-006).

**Acceptance criteria:**
- [ ] `src/vendor/cjson/` contains upstream cJSON.c/cJSON.h with version
      and license recorded (file header or a VENDORED.md note).
- [ ] The build uses only the vendored copy — no `find_package`/pkg-config
      lookup for a system cJSON.
- [ ] cJSON types appear in no public header.
