---
id: STEP-M12-135
title: "rc1 feedback — asset names (the one-file hem-tool and the static SDK are the plain assets, the wolfSSL builds get -shared), init-wizard hostname default my.ence.do, --passphrase deprecated; backlog notes; v1.2.0-rc2"
milestone: M12
implements: ["REQ-BUILD-009", "REQ-BUILD-005", "REQ-TOOL-024", "REQ-TOOL-028", "REQ-TOOL-020"]
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#11-milestones"]
depends_on: ["STEP-M12-130"]
evidence:
  commits: []
  tests: []
  notes: null
reopened: []
cancelled: null
---

**Goal:** the three fixes the user asked for after testing `v1.2.0-rc1`
on Linux and Windows (user decisions 2026-10-19), in M12 before 1.2.0:
(1) the published names say that the one-file build is the default —
`hem-tool-<label>-<os>-x86_64` is the OpenSSL build with every
third-party library linked in and no companion library, and
`hem-tool-<label>-<os>-x86_64-shared` the wolfSSL build that needs the
`wolfssl-<wv>-<os>-x86_64` asset; the SDK library packages follow
(`encedo-hem-<label>-<os>-x86_64` = the static library on OpenSSL,
`…-shared` = the shared library on wolfSSL); every script, workflow text,
README, the REQ-TOOL-024 refusal remedy and the docs say so; (2)
`init-wizard` offers `my.ence.do` as the `hostname` default (REQ-TOOL-017's
product convention), `user` and `email` still have none; (3) `--passphrase`
is marked DEPRECATED in every help page and hem-tool prints one `warning:`
line on stderr when the flag is used (it stays in the shell history and
is visible in the process list; it will be removed in a later release —
`EHEM_PASSPHRASE` stays, a safer entry is a BACKLOG item); the user's six
notes from the test are in BACKLOG.md unanalysed; `v1.2.0-rc2` (the user
tags) publishes the 12 assets under the final names.

**Notes:** The names reverse the 2026-10-12 decision "today's hem-tool
asset name stays and new variants get a suffix" (REQ-BUILD-009's
`source`, ARCHITECTURE §11 M12 part C): the user wants the suffix on
the special one, and the special one is the build that needs a second
download. From 1.2.0 the plain `hem-tool-<version>-<os>-x86_64` is a
different thing than 1.1's (README "Download" and the release text say
so). §6.2 impact report approved in chat 2026-10-19 (plan approval):
REQ-BUILD-009 (names — its normative content), REQ-BUILD-005 (its
archive is renamed, nothing else), REQ-TOOL-024 (the remedy names the
one-file asset), REQ-TOOL-028 (the hostname default), REQ-TOOL-020 (the
shared options list and the warning) → `needs-reverify` with the new
revision until the rework is proven: the unit suite restores
TOOL-020/024/028 to verified; BUILD-005/009 return to implemented on the
rc2 run (no `verifies:` tag — workflow-proven, like BUILD-002).
ARCHITECTURE §11 records the reversal, §8 the two tool decisions. No
done step is reopened: the rename is a decision taken after rc1, not a
defect of STEP-M12-120/130. Internal names stay — `package.sh … static`,
`package-static.sh`, `package-lib.sh static`, the build directories,
the artifact names `release-<os>-x86_64`; only the archive names and
the words about them change. The refusal remedy prints the asset name
from `ehem_version()` + `HEM_RELEASE_OS` (`%s` only — MinGW), ASCII.
The top help page must stay within the 45-line budget of
test_top_page_shape. `check-expired-classifier.sh` passes the passphrase
through the environment so the job log carries no warning. The user
pushes, lets CI go green, tags `v1.2.0-rc2` (annotated; message drafted
in the scratchpad) and may delete the rc1 pre-release (RELEASING).
`ubuntu-latest` stays (user decision 2026-10-19; GitHub migrates it to
Ubuntu 26 from the same day — a failing leg is fixed forward).

**Definition of done**
- [ ] Published names per the table in REQ-BUILD-009 rev 7:
      `package.sh` / `package-static.sh` / `package-lib.sh` produce
      `hem-tool-<label>-<os>-x86_64{,-shared}` and
      `encedo-hem-<label>-<os>-x86_64{,-shared}`; their README texts,
      `release.yml`'s texts, README "Download" (with the 1.1 → 1.2 name
      note), RELEASING, tests/README, KNOWN-ISSUES, API-GUIDE and
      ARCHITECTURE §11 agree; no `x86_64-static` name is left outside
      dated records.
- [ ] REQ-TOOL-024: the dynamic remedies name the one-file hem-tool asset
      (`hem-tool-<version>-<os>-x86_64`); the linked-in remedies do not;
      test_tool_startup + tool_startup_cli green; REQ text rev 7.
- [ ] REQ-TOOL-028: `init-wizard` shows `hostname [my.ence.do]` and Enter
      takes it; `user`/`email` still re-prompt; test_wizard green; rev 3.
- [ ] REQ-TOOL-020: `--passphrase` reads DEPRECATED in the top page and in
      per-command help; one `warning:` line on stderr when the flag is
      used, none without it (test_registry, tool_startup_cli); README,
      the archive READMEs and ARCHITECTURE §8 say so; rev 2.
- [ ] BACKLOG.md carries the six user notes of 2026-10-19 as items,
      unanalysed.
- [ ] `./dev ci` and `./dev ci --openssl` green (gcc + clang); CI green on
      `development`; `v1.2.0-rc2` run green and the pre-release carries
      the 12 assets under the new names (run id + names recorded in
      REQ-BUILD-009's tag-path criterion); the §6.2 statuses restored and
      TRACE regenerated.
