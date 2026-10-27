---
id: STEP-M12-135
title: "rc1 feedback — asset names (the one-file hem-tool and the static SDK are the plain assets, the wolfSSL builds get -shared), init-wizard hostname default my.ence.do, --passphrase deprecated; backlog notes; v1.2.0-rc2"
milestone: M12
implements: ["REQ-BUILD-009", "REQ-BUILD-005", "REQ-TOOL-024", "REQ-TOOL-028", "REQ-TOOL-020"]
traces:
  architecture: ["ARCHITECTURE.md#8-hem-tool-cli", "ARCHITECTURE.md#11-milestones"]
depends_on: ["STEP-M12-130"]
evidence:
  commits: ["b344fe6", "8dd915f"]   # start, implementation; the done commit follows
  tests:
    - "Local 2026-10-19: ./dev ci 56/56 gcc + clang (wolfSSL) and ./dev ci --openssl 56/56 gcc + clang, the docs gate, the export baseline and the consumer gate among them. New/changed assertions — test_tool_startup (verifies: REQ-TOOL-024 rev 7): every dynamic remedy contains 'one-file hem-tool' and 'hem-tool-<ehem_version()>-' + '-x86_64 (1.2.0 and later)', the linked-in remedies neither; test_wizard (verifies: REQ-TOOL-028 rev 3): the all-defaults runs press Enter at hostname and the body still equals init-device's with --hostname my.ence.do, the prompt reads 'hostname [my.ence.do]: ', user/email have no bracketed default, test_required_fields_reprompt sees 'user is required' and 'email is required' once each and never 'hostname is required', the explicit run types dev.ence.do over the default; test_registry (verifies: REQ-TOOL-020 rev 2): the top page renders '--passphrase PW' with DEPRECATED and 'set EHEM_PASSPHRASE instead' and stays at 45 lines (the <= 45 budget — exactly at it; the two-line wording first tried made 46 and was shortened); tool_startup_cli: '--passphrase x keys types' and '--passphrase=x keys types' exit 0 with exactly one 'warning:' line on stderr, the plain 'keys types' run keeps stderr empty"
    - "Scripts: bash -n on package.sh, package-static.sh, package-lib.sh, package-wolfssl.sh, check-expired-classifier.sh; release.yml parses (PyYAML). The published names can only be proven by a release run: the user's v1.2.0-rc2 (box 6)"
    - "CI run 39320511927 (push of 8dd915f to development, 2026-10-19): all 10 legs green — Linux gcc, clang, gcc OpenSSL, clang OpenSSL, static-only, shared-only; Windows MinGW, OpenSSL, static-only, shared-only. The tag push ran CI once more: 39320626785, 10/10."
    - "Release run 39320626797 (the user's annotated v1.2.0-rc2 on 8dd915f, 2026-10-19): Linux job 3:15, Windows job 12:39 — both prefixes cold, a tag run cannot read development's caches — every build, package and smoke step green, the two Linux classifier steps included; publishing job 0:12: assemble (sha256sum -c, duplicate-name check), release notes, publish — green. Release 417736562, prerelease, 12 assets: hem-tool-1.2.0-rc2-{linux,windows}-x86_64{,-shared}.{tar.gz,zip}, encedo-hem-1.2.0-rc2-{linux,windows}-x86_64{,-shared}.{tar.gz,zip}, wolfssl-5.7.2-linux-x86_64.tar.gz, wolfssl-5.7.2-windows-x86_64.zip, wolfssl-5.7.2-stable-src.tar.gz, SHA256SUMS (11 lines naming exactly those archives; read back through the API, kept in the scratchpad under rc2-dl/); the release text = the rc2 annotation + the checksums; no -static name anywhere."
  notes: |
    Per-command help does not list the shared options — it points at
    `hem-tool --help` — so the DEPRECATED text lives on the top page only
    (REQ-TOOL-020's criterion says so); the stderr warning fires on every
    use of the flag, including `--help`/`--version` runs that carry it.
    The top page is now exactly 45 lines, the test's budget: the next
    shared option needs a REQ-TOOL-020 decision on the budget.
    A grep sweep for `x86_64-static` / `-static hem-tool` leaves only
    dated records (workplan/done, REQ evidence, ARCHITECTURE's 120
    paragraph, TRACE history) and REQ-TOOL-026's link flag.
    rc2 tag message drafted: scratchpad release-notes-1.2.0-rc2.md
    (rc1's text + a "Changed since rc1" section), for `git tag -a
    v1.2.0-rc2 --cleanup=whitespace -F <file>` on development after CI.
    Done 2026-10-19: rc2 published the final names. The §6.2 statuses
    were restored by hand (the TRACE generator only reports transition
    candidates): REQ-BUILD-005 rev 6 and REQ-BUILD-009 rev 8 implemented,
    REQ-TOOL-020 rev 3, REQ-TOOL-024 rev 8 and REQ-TOOL-028 rev 4 verified;
    the new criteria of TOOL-020/028 ticked with their records. The rc1
    pre-release 414975403 and its tag still exist — deleting them is the
    user's call (RELEASING). The cold Windows tag build took 12:39 (rc1:
    8:07); RELEASING's figure widened accordingly.
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
- [x] Published names per the table in REQ-BUILD-009 rev 7:
      `package.sh` / `package-static.sh` / `package-lib.sh` produce
      `hem-tool-<label>-<os>-x86_64{,-shared}` and
      `encedo-hem-<label>-<os>-x86_64{,-shared}`; their README texts,
      `release.yml`'s texts, README "Download" (with the 1.1 → 1.2 name
      note), RELEASING, tests/README, KNOWN-ISSUES, API-GUIDE and
      ARCHITECTURE §11 agree; no `x86_64-static` name is left outside
      dated records.
- [x] REQ-TOOL-024: the dynamic remedies name the one-file hem-tool asset
      (`hem-tool-<version>-<os>-x86_64`); the linked-in remedies do not;
      test_tool_startup + tool_startup_cli green; REQ text rev 7.
- [x] REQ-TOOL-028: `init-wizard` shows `hostname [my.ence.do]` and Enter
      takes it; `user`/`email` still re-prompt; test_wizard green; rev 3.
- [x] REQ-TOOL-020: `--passphrase` reads DEPRECATED in the top page (the
      per-command pages point at it for the shared options — see the
      notes); one `warning:` line on stderr when the flag is
      used, none without it (test_registry, tool_startup_cli); README,
      the archive READMEs and ARCHITECTURE §8 say so; rev 2.
- [x] BACKLOG.md carries the six user notes of 2026-10-19 as items,
      unanalysed.
- [x] `./dev ci` and `./dev ci --openssl` green (gcc + clang); CI green on
      `development`; `v1.2.0-rc2` run green and the pre-release carries
      the 12 assets under the new names (run id + names recorded in
      REQ-BUILD-009's tag-path criterion); the §6.2 statuses restored and
      TRACE regenerated.
