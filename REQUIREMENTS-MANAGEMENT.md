# Requirements & Work Management (AI-Assisted)

How requirements and implementation work are managed for **encedo-hem-c-api**.
This document has two audiences: **engineers** and **Claude Code**. Sections
labeled **PROCEDURE** are step-by-step instructions meant to be executed
literally by either. What is being built is defined in
[ARCHITECTURE.md](ARCHITECTURE.md); this document defines how the building is
tracked.

## 1. Operating model

Division of labor — Claude produces, humans decide:

| Activity | Claude | Human |
|---|---|---|
| Draft / refine requirement text | does | reviews |
| Approve a requirement (draft → approved) | never | decides |
| Decompose a milestone into steps | proposes | approves before steps enter `todo/` |
| Implement a step, move it through folders | does, with evidence | may override |
| Mark a REQ implemented / verified | does, only during trace regeneration, from evidence | may override |
| Reject / supersede a requirement | never | decides |
| Impact analysis on a change | produces the report, then stops | approves the consequences |

Two design rules govern all metadata in this process:

1. **Declare only what cannot be discovered; discover everything else.**
   Requirement files declare their architecture anchors (a judgment call).
   Links to code, tests, and steps are *discovered* by scanning tags
   (§4.2) — they are never hand-listed, so they cannot go stale.
2. **Ground truth beats generated files.** `requirements/TRACE.md` is a
   report, not a database. If it disagrees with a fresh scan, it is stale —
   regenerate it.

Claims about external systems must cite the ground-truth sources listed in
§8 or a test against the real system — never memory or plausibility.

## 2. Repository layout

```
requirements/
  TEMPLATE.md                    requirement template (copy to start a new REQ)
  REQ-<AREA>-<NNN>-<slug>.md     one file per requirement; never renamed, never deleted
  TRACE.md                       GENERATED traceability matrix — do not hand-edit
workplan/
  TEMPLATE.md                    step template
  todo/                          approved steps, not started (freely editable here)
  doing/                         steps in progress (WIP limit: 2)
  done/                          completed steps (require evidence to enter)
```

## 3. Requirements

### 3.1 File naming and format

`requirements/REQ-<AREA>-<NNN>-<slug>.md`

<!-- Area codes are permanent once used; adding a new area later is fine,
     renaming an existing one is not. -->

| Area | Covers |
|---|---|
| `API` | public API surface: context lifecycle, memory/ownership conventions, error model, versioning/ABI |
| `AUTH` | auth & session engine: eJWT flow, KDF, token & scope caches, logout, mobile-app confirmation |
| `KEY` | key-management bindings: list/search/get/create/delete/import/update, KID/LABEL/DESCR handling |
| `OPS` | cryptographic operation bindings: sign/verify, ECDH, AES, HMAC, ML-KEM/ML-DSA, random |
| `SYS` | system/logger/storage bindings, incl. disruptive operations (reboot, firmware) |
| `NET` | transport layer: vtable, libcurl implementation, TLS trust, timeouts |
| `TOOL` | hem-tool CLI behavior, incl. protected-key removal policy |
| `TEST` | testing policy & infrastructure: suite gating, device policy, fixtures |
| `BUILD` | build system, packaging, dependency vendoring, CI |

`NNN` is three digits, sequential within an area, and **never reused** — not
even for rejected or superseded requirements. The filename never changes after
creation. Frontmatter fields are defined by
[requirements/TEMPLATE.md](requirements/TEMPLATE.md); the body is: normative
statement first, then **Rationale**, then **Acceptance criteria** (each
criterion independently checkable).

### 3.2 Statuses and transitions

`draft → approved → implemented → verified`, plus `needs-reverify`,
`superseded`, `rejected`.

| Transition | Who | Precondition |
|---|---|---|
| draft → approved | human only | — |
| approved → implemented | Claude, during §4.3 only | ≥ 1 `implements:` tag in the code root **and** every step whose `implements` lists this REQ is in `done/` |
| implemented → verified | Claude, during §4.3 only | ≥ 1 `verifies:` tagged test exists and the test suite passes |
| any → needs-reverify | Claude, during §6.2 only | impact-analysis report presented |
| needs-reverify → implemented/verified | Claude, during §4.3 | rework complete, evidence present again |
| any → superseded / rejected | human only | successor REQ referenced via `superseded_by` (for superseded) |

If a scan shows evidence has *disappeared* (a tag was deleted, a test
removed), Claude does not silently downgrade: it sets `needs-reverify` and
flags the item loudly in the coverage report.

### 3.3 Writing rules

- **Atomic**: one testable behavior per REQ. If "and" joins two obligations,
  split it.
- **RFC 2119**: SHALL / SHALL NOT / SHOULD / MAY, uppercase, exactly one
  SHALL-family keyword in the normative statement.
- **Cited**: `source` names where the requirement came from — a user decision
  with date, an `ARCHITECTURE.md` section, or an external source per §8.
- **Non-duplicative**: a REQ never restates another; use `depends_on`.
- **Edits**: any edit bumps `revision`; git history is the changelog. An edit
  that changes the *meaning* of an approved REQ resets `status` to `draft`
  (human must re-approve) and triggers the §6.2 procedure first. Typo and
  rationale-only edits just bump `revision`.
- **Supersede / split**: create the new REQ(s), set the old one to
  `superseded` with `superseded_by` filled. History is never rewritten.

## 4. Traceability

### 4.1 Link model

```
ARCHITECTURE.md anchors ←declared— REQ —discovered→ code tags (code root)
                                    │ —discovered→ test tags (test root)
                                    │ —discovered→ steps (frontmatter `implements`)
                                    └─ declared → depends_on (other REQs)
STEP —discovered→ commits (subject prefix "[STEP-…]")
```

### 4.2 Tag conventions

<!-- SETUP: the code root defaults to `src/` and the test root to `tests/`;
     adjust here and in §4.3 if this project lays out code differently. -->

| Where | Tag | Example |
|---|---|---|
| Code (`src/`) | `implements: REQ-<ID>` in a comment at the primary implementation point(s) | `/* implements: REQ-AUTH-001 */` |
| Tests (`tests/`) | `verifies: REQ-<ID>` in a comment adjacent to the test case | `// verifies: REQ-AUTH-001` |
| Commits | subject starts with `[STEP-<ID>]` | `[STEP-M1-010] add build skeleton` |

Multiple REQ IDs may appear comma-separated in one tag. Tag the few load-bearing
points, not every touched line. The greppable ground-truth patterns are
`implements: REQ-` and `verifies: REQ-`.

### 4.3 PROCEDURE: regenerate the trace matrix

1. Enumerate `requirements/REQ-*.md`; parse frontmatter.
2. Enumerate `workplan/{todo,doing,done}/STEP-*.md`; parse `id`,
   `implements`, `cancelled`, and note which folder each is in.
3. Grep the code root (`src/`) for `implements: REQ-` and the test root
   (`tests/`) for `verifies: REQ-`; collect `file:line` per REQ. (Both
   directories absent → empty sets, not an error.)
4. Check every declared architecture anchor resolves to a heading in
   `ARCHITECTURE.md`.
5. Apply status transitions per §3.2 (only the ones marked "during §4.3");
   edit the affected REQ files.
6. Overwrite `requirements/TRACE.md`: generation stamp, the matrix
   (REQ | title | status | priority | architecture | steps | code | tests),
   then the coverage report:
   - approved REQs with no code tag (unimplemented)
   - implemented REQs with no passing tagged test (unverified)
   - orphan tags (tags naming a nonexistent REQ)
   - steps in `done/` with empty `evidence` (rule violation)
   - broken architecture anchors
   - REQs in `needs-reverify`
7. Report the summary in chat.

**Read-only variant** ("coverage check"): steps 1–4 + report only; no files
written, no status changes.

## 5. Implementation work (workplan)

### 5.1 Step files

`STEP-<Mx>-<NNN>-<slug>.md` — one file per step, living in exactly one of
`todo/`, `doing/`, `done/`. `Mx` is a milestone from the **Milestones**
section of `ARCHITECTURE.md`. `NNN` is numbered in gaps of ten (010, 020, …)
so insertions fit; IDs are never reused; the *filename* never changes — only
its folder does.

Frontmatter is defined by [workplan/TEMPLATE.md](workplan/TEMPLATE.md):
`id`, `title`, `milestone`, `implements` (REQ IDs), `traces.architecture`,
`depends_on` (step IDs), `evidence` (commits / tests / notes), `reopened`
(history), optional `cancelled`. Body: **Goal**, **Notes**, and a
**Definition of done** checklist.

Sizing: a step is at most ~one focused day and independently verifiable.
Steps SHOULD implement at least one approved REQ; pure chores (CI wiring,
formatting setup) MAY carry `implements: []` with a one-line justification in
Notes.

### 5.2 Status = folder location

- **`todo/`** — approved, not started. Files here may be freely edited or
  renumbered as understanding improves.
- **`doing/`** — in progress. WIP limit: at most **2** steps in `doing/`.
- **`done/`** — complete. A step may enter `done/` only when every
  Definition-of-done box is checked, `evidence.commits` is non-empty, and
  `evidence.tests` lists passing tags (or `evidence.notes` states why the
  step is untestable). Moves are `git mv`; the move commit is the audit
  record.
- **Reopen**: `done/ → todo/` via `git mv`, appending
  `{date, reason}` to `reopened`. Prior evidence stays in the file.
- **Cancel**: a step that will never be done stays in `todo/` with
  `cancelled: <reason>` set; cancelled steps are excluded from coverage.
  Step files are never deleted.

### 5.3 PROCEDURE: decompose a milestone (rolling wave)

Trigger: a human asks ("decompose milestone M2"), typically as the previous
milestone approaches done. Only one milestone is decomposed in detail at a
time.

1. Read the milestone's entry in the **Milestones** section of
   `ARCHITECTURE.md` and every architecture section it references.
2. Read all approved REQs; identify which this milestone realizes. If the
   milestone needs work that no approved REQ covers, draft the missing REQs
   first (§7) and get them approved — steps implement requirements, not
   improvisation (chore steps per §5.1 excepted).
3. Propose the step list as a table (id, title, implements, depends_on,
   rough size) in chat. **Stop.**
4. On human approval: create the step files in `todo/` from the template,
   one commit (`[Mx] decomposition`).
5. On the *first* decomposition also create one coarse placeholder step per
   remaining future milestone (`STEP-<My>-000-<slug>.md` in `todo/`). When a
   milestone's turn comes, its placeholder is cancelled (§5.2) and replaced
   by the detailed steps.
6. Regenerate the trace matrix (§4.3).

### 5.4 PROCEDURE: implement a step

1. Preconditions: the step is in `todo/`, everything in its `depends_on` is
   in `done/`, WIP limit not exceeded, not `cancelled`.
2. `git mv` the file to `doing/`; commit `[STEP-<ID>] start`.
3. Implement. Tag code `implements:` for each REQ in the step's
   `implements`; prefix every commit subject with `[STEP-<ID>]`.
4. Add or update tests tagged `verifies:` for those REQs; run them.
5. Fill `evidence`: commit SHAs, test tags, notes.
6. Check every Definition-of-done box (edit the file).
7. `git mv` to `done/`; commit `[STEP-<ID>] done`.
8. Regenerate the trace matrix (§4.3) and report.

## 6. Change management & impact analysis

### 6.1 What counts as a change

- Editing the normative statement or acceptance criteria of an
  approved/implemented/verified REQ.
- Editing `ARCHITECTURE.md` in a way that alters a decision or a section
  that REQs or steps trace to.
- Superseding or splitting a REQ.

Not a change in this sense: typo/rationale edits, adding a new independent
REQ, editing steps still in `todo/`.

### 6.2 PROCEDURE: impact analysis

Trigger: a human asks for it, **or Claude is about to make a §6.1 change —
in that case it stops and runs this first.**

1. Identify the changed items: REQ IDs and/or `ARCHITECTURE.md` anchors,
   from the edit or its diff.
2. Build the affected set:
   - REQs whose `traces.architecture` hit changed anchors;
   - REQs reachable from changed REQs via `depends_on` (transitively);
   - steps whose `implements` or `traces` hit the set;
   - code and tests tagged with affected REQs (grep);
   - which of those steps are in `done/`.
3. Produce the report: per affected item — what changed, why it is affected,
   proposed action (rework / re-verify / no action, with reason).
4. **Stop.** A human approves the report, possibly trimmed.
5. Apply the approved consequences: set affected REQs to `needs-reverify`
   (bumping `revision`); reopen affected `done/` steps per §5.2 or create
   new steps; make the underlying edit itself.
6. After rework and passing tests, the next §4.3 run restores statuses;
   report closure in chat.

## 7. Drafting requirements (brief)

Claude drafts REQs from three sources: decisions made in conversation (cite
the date), `ARCHITECTURE.md`, and the external sources listed in §8. Drafts
always start at `status: draft` and are presented for approval — never
self-approved. Ambiguities, conflicts between sources, and anything the
architecture marks as *needs verification* are stated in the draft
explicitly (usually as an open acceptance criterion), not smoothed over.
When a user makes a decision in conversation that constrains implementation,
Claude SHOULD propose capturing it as a REQ on the spot.

## 8. AI limitations & guardrails (brief)

- **Citations for external-system behavior.** Claims about the systems,
  protocols, or APIs this project integrates with must cite one of the
  ground-truth sources below, or a test against the real system — never
  memory or plausibility. When sources conflict, name which source says
  what.

  | Source | Authoritative for |
  |---|---|
  | the real HEM device (dev machine, later CI) | final arbiter for all device behavior; wins over every document |
  | Encedo Manager (implementation) | the auth flow: KDF choice and exact Argon2 parameters (user decision 2026-07-15) |
  | https://github.com/KajtomatO/encedo-hem-api-doc | HEM REST API endpoints, request/response payloads, error codes; its `DISCREPANCIES.md` records known doc/implementation divergences |
  | https://github.com/KajtomatO/encedo-hem-python-api | working client reference: auth/token caching behavior, TLS handling, `wipe_keys.py` protected-key policy |
  | `requirements/start_point/encedo-pkcs11/` (`hem.h`, `REQUIREMENTS-hem.md`) | the consumer contract: what encedo-pkcs11 needs from this SDK (HEM-SDK-1…9, error-condition set) |

  Precedence when sources conflict: device > Encedo Manager > API doc;
  conflicts are recorded in the affected REQ, not silently resolved.

- **No silent state changes.** Every status transition, folder move, or
  generated-file update appears in a commit and/or a chat report.
- **Uncertainty is propagated, not resolved by fiat.** Open questions stay
  visible as open acceptance criteria until actually verified.
- **Generated files are disposable.** `TRACE.md` must always be
  reproducible from a fresh scan; if it can't be, the scan wins.

## Appendix A: standing commands

| Say to Claude | Executes |
|---|---|
| "regenerate the trace matrix" | §4.3 |
| "requirements coverage check" | §4.3 read-only variant |
| "draft a requirement for \<topic\>" | §7 |
| "decompose milestone \<Mx\>" | §5.3 |
| "start step \<STEP-ID\>" | §5.4 steps 1–2 |
| "complete step \<STEP-ID\>" | §5.4 steps 4–8 |
| "impact analysis for \<REQ-ID / section\>" | §6.2 |
