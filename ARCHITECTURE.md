# <PROJECT_NAME> — Architecture

<!-- SETUP: this is a skeleton. Fill the sections below; at minimum
     "Decisions (fixed)" and "Milestones" must have real content before the
     requirements/workplan process (REQUIREMENTS-MANAGEMENT.md) can run.
     Add, rename, or reorder domain sections freely — requirement files
     trace to heading *anchors*, so keep headings stable once REQs
     reference them (renaming a heading breaks its anchor). -->

This document defines **what** is being built and the decisions that shape
it. How the work is tracked — requirements, steps, traceability, change
management — is defined in
[REQUIREMENTS-MANAGEMENT.md](REQUIREMENTS-MANAGEMENT.md).

## 1. Decisions (fixed)

<!-- Decisions already made and not up for re-litigation: language,
     platforms, key libraries, protocol versions, deployment model.
     One bullet per decision, with a short "because". REQs cite these. -->

- <decision> — <why>

## 2. Context & constraints

<!-- The problem, the environment the system runs in, and the external
     constraints (performance envelopes, protocol limits, compliance)
     that shape the design. This is where "why is it built this way"
     answers live. -->

## 3. Component overview

<!-- The major components/modules and the responsibilities and boundaries
     between them. A diagram or a table both work. -->

## 4. <domain section>

<!-- Add one section per significant design area (data model, protocol
     handling, concurrency, error handling, configuration, security, …).
     These sections are the anchor targets for REQ `traces.architecture`
     entries. -->

## 5. Directory layout

<!-- Planned source tree. Keep the code root and test root consistent
     with REQUIREMENTS-MANAGEMENT.md §4.2 (defaults: src/, tests/). -->

```
src/
tests/
```

## 6. Milestones

<!-- SETUP (required): ordered milestones M1, M2, … Each entry: a short
     name, what exists and demonstrably works when it is done, and — where
     applicable — a "gate": an end-to-end proof that must pass before the
     next milestone starts. Workplan steps are decomposed from these
     entries one milestone at a time (REQUIREMENTS-MANAGEMENT.md §5.3). -->

- **M1** <name>: <what exists and demonstrably works when done>
- **M2** <name>: <…>

## 7. Risks & open questions

<!-- Numbered list of unknowns that could change the design, each with
     what would resolve it. Items here typically surface in REQs as open
     acceptance criteria until verified. -->

1. <unknown> — <how it gets resolved>
