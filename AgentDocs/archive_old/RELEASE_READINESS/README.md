# Mixtormat 1.0 — Release Readiness Fix Tracker

**Repository:** https://github.com/hugobeyer/matlab  
**Branch:** `feature/behavior-system-v2`  
**Source:** 2026-10-11 release-readiness static audit (Composition, Generators/Masks/IDs, UI/UX, Performance/Release).  
**Scope:** Fix existing functionality before planning/implementing Color System V2.

**1.0 exclusion (2026-10-11, main):** Strata Carver is frozen and retired from new authoring.
Its layer, child and Sources creation entries are removed. GEN-01 and Strata-specific GEN-02
work are excluded from shipping requirements, not fixed. Preserve its types, shaders,
serialization and existing-asset compatibility; Cliff Strata remains in scope.

## Status and interpretation

- **Behavior System V2 was not audited.** No finding below is a substitute for a dedicated Behavior audit.
- The audit estimated ~65% overall readiness (±10%). That is an agent estimate, **not a measured release score**.
- The audit confirmed **no P0 blocker through static analysis**; P1 and P2 still need focused resolution.
- **Not started** means this tracker does not attest any fix. A newer branch commit may already address a finding; inspect the current source before editing.
- Confidence labels below follow the report: **confirmed**, **likely**, or **needs investigation**. “Likely” does not mean a proven runtime defect.
- Follow `AGENTS.md` and the relevant current `AgentDocs/` subsystem doc. No compilation, testing, launches, or new test infrastructure unless explicitly requested. The user owns runtime and build verification.

## Task documents

1. [01 — Composition and Identity](01_composition_identity.md) — `REL-01`, `COMP-01/02/03/04/06`
2. [02 — Generators, Masks, and IDs](02_generators_masks_ids.md) — `GEN-01/02/03/04/06`, `UI-03`
3. [03 — UI/UX](03_ui_ux.md) — `UI-01/02/04/05/06/07/08`
4. [04 — Performance and Release](04_performance_release.md) — `REL-02/03`, `PERF-01/02/03`, `MAINT-01/02`

## Work order

| Order | Finding(s) | Why first | Status |
|---|---|---|---|
| 1 | GEN-02 (shipping generators only) | Scoped masks must affect shipping generators | Not started; GEN-01 / Strata Carver excluded |
| 2 | REL-01, COMP-02 | Stable identities, copy/instance and grouped child references | Not started |
| 3 | COMP-01, COMP-04, COMP-03 | Predictable authored stack order, publication and compositing | Not started |
| 4 | UI-03, UI-01, UI-02 | Reachable ID functionality and consistent existing help UI | Not started |
| 5 | REL-02 | Explicit release-asset packaging plan; absence in Git alone is not a runtime failure | Not started |
| 6 | PERF-03, PERF-02 | Remove expensive repeated lookup/loading without changing semantics | Not started |
| 7 | GEN-03, GEN-06, UI-04 | Behavior-adjacent interfaces; isolate from full Behavior redesign | Not started |
| 8 | UI-05/06/07/08, PERF-01, COMP-06, REL-03, MAINT-01/02, GEN-04 | Polish, dead-code investigation and doc drift | Not started |
| 9 | Focused Behavior System V2 audit | Unassessed architectural area; do before Behavior merge/release claims | Deferred / unaudited |

This is an ordering guide, **not** permission to remove functionality or to rewrite working systems.

## Rules for agents

- Make a focused change only after checking the exact symbols, readers, references and latest branch source.
- Reuse canonical helpers, child capability rules, instance resolution, composition math and UI style tokens. No parallel implementation.
- Preserve existing UI features, public APIs and serialized enum values. Propose removal explicitly before deleting behavior; enum values are append-only.
- Do **not** address Color System V2, introduce source/target menus, or redesign Behavior ownership while fixing readiness.
- Record actual changes and remaining uncertainty in the relevant task document; then update the status in this tracker. Do not mark a report claim “fixed” just because code was edited.
- The audit's **confirmed completed** areas are preservation targets: existing height math, ping-pong parity, caching infrastructure, generator wiring, ID remapping, undo, DnD/menu behavior, and styled-help infrastructure.

## Report scope / constraints

The audit covered four of five areas. In particular, `COMP-03`, `GEN-06`, asset packaging behavior and performance impact need targeted investigation or user-side runtime checks. Do not convert these into assertions of reproduced defects. Color System V2 stays deferred until the existing ownership/masking/instance foundation is reliable.
