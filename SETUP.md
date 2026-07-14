# Project template — setup checklist

This folder is a reusable, project-agnostic scaffold for AI-assisted
requirements and work management. Its contents are meant to live at the
**root of a new project repository**. This file (`SETUP.md`) is the
instruction sheet — delete it once setup is complete.

## Contents

```
SETUP.md                       this checklist (delete after setup)
REQUIREMENTS-MANAGEMENT.md     the process: requirements, workplan, traceability, change management
ARCHITECTURE.md                skeleton architecture document (fill in)
requirements/
  TEMPLATE.md                  requirement template
  TRACE.md                     generated traceability matrix (stub until first generation)
workplan/
  TEMPLATE.md                  step template
  todo/  doing/  done/         step folders (empty; .gitkeep placeholders)
```

## Setup steps

1. **Copy** everything in this folder to the new repository root.
2. **Find & replace** `<PROJECT_NAME>` in `REQUIREMENTS-MANAGEMENT.md` and
   `ARCHITECTURE.md` with the project's name.
3. **Fill `ARCHITECTURE.md`** — at minimum the *Decisions (fixed)* and
   *Milestones* sections; the process cannot run without milestones.
   Add domain sections as the design takes shape; requirement files will
   trace to their heading anchors.
4. **Define requirement area codes** in `REQUIREMENTS-MANAGEMENT.md` §3.1.
   The table ships empty on purpose: choose short uppercase codes that
   partition *this* project's requirement space, before drafting the
   first REQ.
5. **List external ground-truth sources** in `REQUIREMENTS-MANAGEMENT.md`
   §8 — the documents, specs, or APIs that claims about external systems
   must cite. If there are none, say so there explicitly.
6. **Check the code/test roots** in `REQUIREMENTS-MANAGEMENT.md` §4.2–4.3.
   The defaults are `src/` and `tests/`; change them if the project lays
   out code differently.
7. Delete this file, commit, then start: draft the first requirements
   (§7), get them approved, and decompose the first milestone (§5.3).
