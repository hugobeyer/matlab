# Mixtormat — Behavior V2 Critical Audit & Fix Tracker

**Branch:** `feature/behavior-system-v2`  
**Source:** [Polyscan #6](https://github.com/hugobeyer/matlab/issues/6), [Behavior V2 PR #5](https://github.com/hugobeyer/matlab/pull/5)  
**Updated:** 2026-10-11

## Rules

- Follow `AGENTS.md`; source analysis and static review only. Compilation and runtime validation belong to the user.
- Track only Behavior V2-relevant defects, evaluation order, clipboard/group identity, Region ID propagation, and legacy-retention risks.
- Preserve working features and existing parameters; no cleanup without demonstrated relevance.
- Distinguish source-confirmed defects from unverified risks. No validation criteria or test plans.

## P0 — Clipboard supports the same nesting as creation

**Status:** Confirmed source inconsistency — open.  
**Files:** `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerClipboard.cpp`, `MixtormatLayerActions.cpp`

Behavior creation supports nested ownership (e.g. `Push → Flow → Mask`), while paste contains generator-only placement assumptions. Unify ownership eligibility for creation, copy, paste and duplication. Preserve entire subtrees, remap internal IDs and reject truly invalid destinations.

## P0 — Explicit structural source rejection reasons

**Status:** Confirmed source-level issue — open.  
**File:** `Source/MixtormatRuntime/Private/MixtormatOutputReference.cpp`

`EvaluateGeneratorInputSource` sometimes returns `INDEX_NONE` without classifying its rejection. Identify invalid owners, disabled sources, missing identities and forward dependencies without changing valid evaluation behavior or existing rejection precedence.

## P1 — Group expansion / effective child identity

**Status:** Targeted review; wrong output not proven.  
**File:** `Source/MixtormatRuntime/Private/MixtormatLayerGroups.cpp`

Group members must remap shared Behavior owners and internal source references to member-local child IDs. External references keep their original identities. Preserve existing Behavior socket remapping and fail-closed rules for unresolved ownership. Fix only specific mismatches.

## P1 — Fill Layer must preserve composite-below Region IDs

**Status:** New authoring/compositing requirement; implementation discrepancy not yet established.  
**Area:** Region ID compositing, source publication, Fill Layer composition.

A Fill Layer must **not reset or destroy accumulated Region IDs** merely because it contributes material properties.

- No authored Region IDs: pass through the composite-below IDs unchanged.
- New IDs: introduce IDs in the explicitly authored coverage without erasing unrelated IDs.
- Overwrite: replace pre-existing IDs only where explicitly requested and covered by the layer's mask.
- Downstream layers and Behaviors may source IDs from the composite below the Fill Layer.
- Respect the same identity/coverage behavior through groups.

Inspect canonical Region ID propagation and fill compositing before implementation. Avoid implementing this as a Behavior-only workaround.

## P1 — Shared published-source eligibility and evaluation order

**Status:** Targeted review; no verified runtime discrepancy.  
**Files:** `MixtormatGeneratorGather.cpp`, `MixtormatLayerChildren.cpp`, `MixtormatChildCapabilities.cpp`, `MixtormatOutputReference.cpp`

Align Editor placement and runtime gather rules for kind, scoped ownership, enabled state, earlier-producer ordering, nested Behavior dependencies and composite-below reads. A source accepted by authoring must be consumable at the correct stage.

## P2 — Audit remaining legacy structural paths

**Status:** Targeted review; Polyscan snapshot paths stale.

The report referenced `BuildChildProjection` and `PrepareStructuralModuleProposal`, but their original files were absent on the checked Behavior V2 branch. Inspect remaining active references and dispatch/menu/serialization ownership before removing anything; do not revive obsolete structures or remove active functionality without approval.

## Excluded

- Repeating the already completed Behavior GPU shader-binding audit without new evidence.
- Unreal compilation, runtime validation and test creation, per `AGENTS.md`.
- Unrelated Polyscan complexity and cosmetic refactors.

## Change log

- **2026-10-11:** Initial tracker created on the Behavior V2 branch. Added explicit Fill Layer preservation of composite-below Region IDs and ranked source findings.
