# Mixtormat — Master Roadmap

> Canonical planning document for current `main`.
>
> This replaces the separate Preview UX / Hotkeys, Current Priorities, Modularization Possibilities, Modules Audit, and the recent code-quality/performance audit as the working roadmap.
>
> Source wins over older planning notes. Preserve existing features, saved values, UI ranges, hard clamps, output behavior, and serialization unless a deliberate migration is approved.

---

## 0. Working principles

1. Fix correctness and serialization hazards before broad refactors.
2. Reuse existing clean seams instead of creating parallel systems.
3. Keep producers separate; share only responsibilities that are genuinely identical.
4. Do not redesign serialized payloads merely to reduce struct size without profiling and a migration plan.
5. Structural cleanup and performance work should meet at clear shared boundaries, not become one giant rewrite.
6. GPU optimization should follow measured hot paths; code duplication alone does not prove a performance problem.
7. UI should call reusable runtime/editor services. Slate should not own document mutation rules.
8. Tests stay until the behavior they protect has moved and stabilized; dead/duplicate tests are removed last.

---

# 1. Priority 0 — correctness, identity, and migration blockers

These should be resolved before large architectural work because they can corrupt state, produce stale output, or make later migrations unsafe.

## 1.1 Asset duplication and GUID identity

Audit and fix duplicated Mixtormat assets preserving serialized Layer / Child / Group GUIDs.

Target behavior:

- duplicated assets receive a new internal identity namespace;
- references inside the duplicate are remapped consistently;
- source and duplicate cannot collide in cross-reference lookups;
- editor-only duplication helpers must not be the only safe duplication path.

Likely boundary:

- runtime identity/remap helper;
- asset duplication hook such as `PostDuplicate` / `PostDuplicateProperties` where appropriate for UE 5.8;
- reuse the same remap logic later from clipboard/group/document operations.

## 1.2 Undo/history correctness

Verify and fix any child/layer creation or mutation paths that bypass edit history.

Known audit candidate:

- layer-child creation path versus group-child creation path.

Do not build a second undo system. This should feed the existing history model until DocumentOps is introduced.

## 1.3 Authored-value sanitization

Verify and fix:

- Stain finite-value sanitization;
- `StainIterations` upper bound;
- any other unbounded GPU iteration controls discovered while touching the same code;
- invalid stage/permutation paths should fail clearly rather than silently dispatch a no-op.

## 1.4 Ramp serialization/default check

Before the ramp becomes serialized data, verify how a newly-added nested `TArray`/ramp payload initializes on existing saved materials.

Required invariant:

- existing materials with no authored ramp must evaluate exactly as identity `(0,0) → (1,1)`.

If Unreal serialization does not materialize the default safely, add an explicit migration/defaulting path before shipping the ramp.

## 1.5 Shared placement serialization decision

Do **not** immediately move existing flat authored placement properties into a nested struct.

Current serialized paths such as:

- `Mask.TilingX`
- `Mask.TilingY`
- `Mask.Offset`
- rotation/flip fields

must not silently become defaults because their property path changed.

Preferred first step:

- keep authored fields flat;
- share render payload, bind helper, and inspector rows;
- only nest authored data if a deliberate custom-version migration is implemented.

---

# 2. Mask system foundation

This combines the current mask priorities into one architecture project:

- finish mask pipeline cleanup;
- scalar ramp / Curve Bias module;
- shared source placement;
- generalized scoped/gated mask producers;
- shared normalization/blending where the implementation is genuinely common.

Final conceptual pipeline:

```text
producer
  → normalize
  → input levels
  → scalar ramp / Curve Bias
  → balance / contrast / offset / invert
  → blend
```

Raw/debug output remains before shaping so diagnostic output is not modified by the ramp.

---

## 2.1 Close the existing shaping seam first

The current shaping path is already the correct shared boundary. Keep it.

Tasks:

- remove dead `bSkipNormalization` if still unused;
- close the remaining duplicated mask blend path so all applicable mask producers use the same blend helper;
- clarify `.usf` versus `.ush` ownership/naming where necessary;
- do not pull unrelated effect/generator mask operations into mask shaping.

Do not change legitimate producer-side signal construction such as Generated Mask biasing before the shaping funnel.

---

## 2.2 Reusable scalar ramp / Curve Bias

The ramp is a reusable scalar module, not a one-off mask widget.

### Data

Introduce a reusable scalar ramp type, conceptually:

```cpp
FMixtormatRampPoint { X, Y }
FMixtormatScalarRamp
{
    bEnabled
    Points
    Interpolation
}
```

Requirements:

- identity default `{(0,0),(1,1)}`;
- fixed maximum point count for GPU packing;
- explicit zero-fill of unused shader slots;
- authored-only initially: ramp point arrays do not need to enter the current scalar parameter-binding system;
- endpoints protected in X/Y unless a later design decision explicitly changes the domain behavior.

### Evaluation

Use one evaluator for CPU/UI and GPU packing.

Interpolation modes:

- Linear;
- Spline using a monotone cubic method such as Fritsch–Carlson to avoid mask overshoot/hard clipping.

CPU-side responsibilities:

- ordering;
- duplicate-X handling;
- capacity cap;
- identity fallback;
- endpoint constraints;
- spline tangent generation.

GPU responsibility:

- evaluate the packed ramp cheaply.

### UI module

Reusable Curve Bias / scalar-ramp widget:

- LMB add point;
- LMB drag point;
- RMB delete point;
- interpolation buttons/icons;
- reusable in other scalar workflows later;
- widget owns interaction/hit testing only;
- widget must draw through the same evaluator used by composition.

Visual tokens should be centralized for:

- background viewport/grid;
- curve color;
- under-curve color/fill if used;
- curve thickness;
- point size;
- icon size/alignment;
- gaps/padding.

Do not route the point array through scalar-only `MakeMemberSlider`/parameter metadata.

---

## 2.3 Shared mask source placement

Only consolidate placement for producers that are actually equivalent.

Confirmed intended pair:

- texture-backed Mask;
- Color ID mask.

Share:

- render placement payload;
- GPU parameter declaration/binding;
- inspector rows;
- existing common UV transform path.

Keep separate:

- Generated Mask;
- Random ID Mask;
- Craquelure lattice scale;
- layer-level placement/hash behavior;
- pattern/UV-ID random-per-region transforms.

Do not alter `MixtormatUV.ush` simply to chase more abstraction if the existing UV path is already the shared mathematical core.

---

## 2.4 Generalize scoped/gated mask producers

Goal:

> Any valid mask-producing child should be usable as a scoped/gating mask under Grade, effects, and generators, rather than only the texture Mask child.

Preserve the existing `ScopeOwnerChildId` model. Extend it; do not replace it.

Create a shared gate predicate/helper instead of repeated `ContainsByPredicate` checks.

Conceptual API:

```text
mask producer evaluated into destination accumulator
```

Required behavior:

- scoped evaluation must write a local feature/gate accumulator, not the layer's normal combined-mask accumulator;
- producer-specific raw generation remains producer-specific;
- shaping/blend behavior remains shared;
- top-level producer passes must skip normal combined-mask output when operating as a scoped gate;
- gather scope resolution should not exist only inside the texture-mask branch.

Before implementation, resolve the Generated Mask layer-0 surface-valid behavior explicitly.

Craquelure note:

- mask contribution can participate in gating;
- its separate relief/post-composite behavior remains where it currently belongs.

---

# 3. Typed outputs, addressing, references, and dependencies

This is one subsystem, but not one monolithic registry.

The common responsibility is:

```text
address
  → capability
  → validation
  → dependency ordering
  → remap
  → publication lookup
  → preview/copy consumer
```

Keep typed payloads distinct where their contracts differ.

---

## 3.1 Consolidate address/remap operations

Current systems use several related address forms across runtime/editor/render paths.

Create one canonical helper/service for remapping an address pair:

- owner/layer ID;
- child ID;
- any output identifier where relevant.

Use it from:

- layer-group expansion;
- parameter bindings;
- clipboard/paste;
- duplication/document operations;
- later identity regeneration.

Do not keep four manually synchronized field-by-field remap walks.

---

## 3.2 Preserve genuinely different publication payloads

Do **not** blindly merge scalar-mask and typed-field registries.

Examples of legitimate distinctions:

- scalar mask publication;
- Flow bundle with smooth/validity information;
- UV publication;
- Region IDs with hashed-ID semantics and local same-layer consumption rules.

Consolidate the address/dispatch/validation layer, not the payload representation merely for symmetry.

---

## 3.3 Mask dependency validation

Bring published-mask references closer to the stronger validation model used by typed field/output references.

Audit:

- forward references;
- disabled source nodes;
- cycles;
- ordering;
- scoped source resolution;
- missing source behavior.

Treat this as a behavior change, not just cleanup. Invalid references that previously silently resolved to an empty signal may now be rejected or reported.

---

## 3.4 Region IDs remain a special case where required

Do not flatten Region IDs into a generic earlier-layer-only rule if they legitimately support same-layer/local consumption before the layer ends.

Consolidate only the parts that are actually identical:

- capability declaration;
- address/remap rules;
- validation primitives;
- preview/copy metadata.

---

## 3.5 Height stays outside child typed-output publication for now

Current height composition is layer-level accumulation/reference behavior rather than a normal per-child published output.

Do not force Height into the typed child-output system unless a real child-addressed height producer/consumer contract emerges.

---

# 4. Document operations and identity outside Slate

This is one of the highest-value architectural refactors because it also removes repeated full-document validation/copy logic.

Create a reusable document-edit service, conceptually:

```text
MixtormatDocumentOps
```

Responsibilities:

- create child/layer/group;
- remove;
- duplicate;
- move/reorder;
- paste;
- placement/scope validation;
- reference remapping;
- identity regeneration;
- effective-document/effective-layer construction;
- structural validation.

Slate responsibilities become:

- gather user intent;
- call DocumentOps;
- update selection;
- request preview refresh;
- render UI.

Slate should not implement document rules independently in menus, drag/drop, clipboard, and hierarchy widgets.

---

## 4.1 Effective layer construction

Current `BuildEffectiveLayers` copies the full authored layer array before group expansion.

Refactor target:

- avoid full deep copies where possible;
- build an effective view/cache incrementally;
- cache deterministic effective child IDs rather than rebuilding MD5 inputs repeatedly;
- preserve authored data untouched.

Do this after the core DocumentOps boundary exists so the optimization has one owner.

---

## 4.2 History model

Current history deep-compares and copies the full layer/group state.

Near-term:

- preserve behavior;
- centralize mutation paths first.

Then consider:

- transaction/command deltas;
- dirty-region/structural versioning;
- more targeted coalescing;
- cheaper saved-state comparison.

Do not optimize dozens of widget call sites independently before mutation has one entry point.

---

# 5. Preview, invalidation, refresh, and hotkeys

Treat preview UX and refresh orchestration as one subsystem.

Single conceptual state flow:

```text
selection
  → selected child/module
  → capabilities
  → available preview outputs
  → active preview output
  → compositor invalidation/refresh
```

There should be one preview state model, not separate ad-hoc state for IDs, module previews, channel previews, and HUD labels.

---

## 5.1 Preview correctness

Fix:

- enum/dropdown writes that do not invalidate composition;
- mask Blend Mode changes leaving stale output;
- stale child/module preview after selecting another child/layer/group/material;
- debug preview target surviving after its owning selection becomes invalid.

Rule:

> If the active preview target is no longer valid for the current selection, return to Final/Material.

Numeric and enum edits that affect composition should enter the same refresh/invalidation path.

---

## 5.2 Capability-driven preview cycling

Preferred final gestures:

- `P` — next available preview for selected module;
- `Shift+P` — previous;
- `I` — Region IDs for selected/nearest valid ID-producing chain;
- `X` — exit child/debug preview to Final/Material.

Retire temporary/duplicate `U` / `M` module-preview aliases after the final mapping is adopted.

Retain viewport-specific controls where appropriate:

- `F` frame/focus;
- displacement toggle;
- raw material channel preview;
- camera/mouse input;
- overlay toggle.

Do not force camera controls into a document-command implementation just for centralization. The goal is one documented key map and one preview model.

---

## 5.3 Bottom-center preview/help HUD

Add a lightweight viewport hint above the Gallery divider.

Examples:

```text
GENERATED MASK · MASK    P Next    Shift+P Previous    X Exit
ROCK FORMATION · REGION IDs    P Next    I IDs    X Exit
```

Requirements:

- active module/output bright/normal;
- shortcuts muted;
- no large panel;
- show only relevant actions;
- hide with viewport overlay UI if that remains the overlay convention;
- reuse the existing preview-label source of truth such as `GetPreviewModeLabel()` rather than maintaining another state string.

---

# 6. CPU/editor hot-path performance

Do this after the shared boundaries above exist. Many current inefficiencies will disappear naturally when mutation, preview refresh, and asset access are centralized.

Prioritize the large costs first.

---

## 6.1 Remove synchronous asset loads from interactive paths

Eliminate `LoadSynchronous()` / equivalent forced deserialization from:

- compose gather paths where it can hitch interaction;
- per-paint/per-tick Slate attributes;
- gallery enumeration;
- validation paths repeatedly reached from interaction;
- preview state polling.

Preferred direction:

- pre-resolve/pin assets before compose;
- asset-registry tags for gallery metadata;
- cached editor state for Slate attributes;
- explicit invalidation on asset changes.

---

## 6.2 Reduce full-document copies

Targets include:

- effective layer construction;
- edit history;
- reference validation;
- clipboard validation;
- drag/drop validation;
- throttled preview state where full layer/group stacks are copied.

Centralizing these into DocumentOps comes first; optimize the shared implementation second.

---

## 6.3 Cache address/binding lookup

Current opportunities:

- GUID → layer index;
- GUID → child index/address;
- binding owner lookup;
- parameter address resolution;
- effective child ID;
- visited/reference traversal state.

Avoid repeated O(bindings × layers × children) scans on every compose or paint where stable structural maps can be reused until the document changes.

---

## 6.4 Slate paint-path cleanup

Only after higher-impact work:

- repeated stack traversal per row/per paint;
- repeated `FText`/`FString` creation;
- repeated font measurements;
- gradient painter allocations/resampling;
- repeated palette/map lookups;
- disk/file checks during paint.

These are real but secondary to synchronous loads and full-state copying.

---

# 7. GPU/pass infrastructure and performance

Keep two categories separate:

1. shared infrastructure/correctness;
2. measured hot-path optimization.

Do not rewrite all shaders into a generic framework merely to remove duplicated lines.

---

## 7.1 Shared pass infrastructure

Good candidates where behavior is genuinely identical:

- separable scalar blur helper;
- debug snapshot helper;
- common parameter fills;
- canonical hash functions;
- toroidal integer wrap;
- RNM;
- JFA primitives;
- common salt constants;
- scalar-field normalization;
- common mask shaping/binding;
- common typed output address helpers.

Avoid giant helper files with unrelated responsibilities.

---

## 7.2 RDG/resource cleanup

Audit and reduce:

- full-resolution `AddClearUAVPass` calls used only to satisfy initialization/validation where texture descriptors or guaranteed writes can safely replace them;
- unnecessary dummy textures/clears;
- repeated full-resolution copy-backs;
- redundant resource creation inside iteration loops;
- unconditional debug UAV bindings that create false dependencies.

Preserve RDG correctness. Do not remove a clear simply because it looks redundant without verifying all write paths.

---

## 7.3 Share expensive solved fields

High-value examples:

- RegionIndex solve reused by multiple consumers in the same node/frame;
- common iteration parameter blocks rather than rebuilding full structs on every stage;
- reusable field snapshots where multiple downstream passes consume identical data.

---

## 7.4 Shader hot paths

Prioritized audit candidates:

### Composite

Avoid recomputing the full height-blend contest repeatedly for neighboring gradient taps when only the scalar weight/height result is required.

### FlowWarp

Audit Jacobian evaluation that re-evaluates the full warp field multiple times per pixel.

### Peel

Avoid evaluating the full damage/adhesion stack at every neighboring sample when only the center requires the expensive terms.

### EdgeWear

Reduce repeated hash/noise-stack work before directional marching.

### Pebbles / Rock

Avoid repeated complete SDF evaluations for finite-difference gradients where analytic/cached/local alternatives are viable.

### Cracks

Cache duplicated neighborhood hash scans.

### Craquelure / iterative morphology

Profile wrapped-load counts and consider tiled/groupshared kernels only for confirmed bottlenecks.

---

## 7.5 Half precision / groupshared memory

Treat as profiling-driven opportunities, not blanket policy.

Candidates:

- mask-only intermediate fields;
- small local 3×3 / 5×5 wrapped kernels.

Do not introduce half precision where it changes IDs, normalization stability, height comparisons, or accumulated error behavior.

---

# 8. Editor/module cleanup

Lower priority than correctness, shared runtime boundaries, and measured performance.

## 8.1 Parameter/inspector construction

Move toward metadata-driven reusable rows while preserving:

- current UI min/max;
- hard clamps;
- snapping;
- labels/tooltips;
- visibility rules;
- reset defaults.

Do not collapse distinct producer semantics merely because their widgets look similar.

## 8.2 Effect/child metadata

Aim for a clearer single source for:

- identity/type;
- capabilities;
- previewable outputs;
- copyable outputs;
- input/output declarations;
- control metadata where practical.

Do not introduce a Runtime → Editor dependency to reuse an editor-only capability table. Move shared metadata downward only when that module boundary is deliberate.

## 8.3 Asset registry service

Create an editor-side cached query service with explicit invalidation for:

- surfaces;
- masks;
- materials/compositions;
- library metadata.

Use registry metadata instead of loading every asset merely to obtain display names or basic card information.

## 8.4 Widget duplication

Clean up after the APIs stabilize:

- repeated selected-child getters;
- repeated enable-checkbox blocks;
- repeated slider wrappers;
- row rename/click/drag machinery;
- drag/drop ladders;
- repeated creator switch tables.

Prefer small typed helpers/tables over a mega generic widget framework.

---

# 9. Data-model redesign — explicitly deferred

Do **not** yet split `FMixtormatLayerChild`, `FMixtormatLayerEffect`, `FChildRenderData`, etc. into tagged variants solely because they carry inactive payloads.

Reasons:

- serialization migration;
- reflection/UPROPERTY behavior;
- undo/history;
- editor binding;
- saved material compatibility;
- large implementation blast radius.

Revisit only after:

- full-copy hot paths are reduced;
- real memory/copy cost is profiled on representative complex documents;
- a migration strategy exists.

The problem may become much smaller once the system stops copying the structures so often.

---

# 10. Feature work after architecture stabilizes

## 10.1 Lattice generator

Implement after:

- output contracts are stable;
- scoped/gated producer behavior is stable;
- shared mask shaping/ramp exists.

Do not build new generator plumbing against temporary mask/output architecture.

## 10.2 Freeze Up To Here

Implement after:

- dependency tracking is trustworthy;
- output/reference ordering is settled;
- effective-layer/document representation is stable;
- cache invalidation rules are clear.

Freeze should become a consumer of dependency/cache infrastructure, not invent its own parallel dependency system.

---

# 11. Tests and dead code — last cleanup phase

Keep useful boundary/regression tests while moving behavior.

After architecture settles:

- remove dead test files;
- remove duplicate test infrastructure;
- delete tests that only preserve deleted legacy paths;
- keep output/reference, group-remap, serialization, ramp, mask shaping, and dependency boundary tests;
- update tests that intentionally name remapped fields only when the underlying canonical helper changes.

Dead production code can be removed earlier when it is independently proven unused and has no serialization/compatibility role.

---

# 12. Ordered implementation roadmap

This is the recommended execution order.

## Phase A — blockers and mask foundation

1. Verify/fix duplicate-asset GUID identity behavior.
2. Fix missing history/correctness paths found by the audit.
3. Add Stain finite/iteration guards and verify invalid shader-stage behavior.
4. Verify ramp defaults on existing serialized assets.
5. Remove dead mask-normalization branch and close the remaining shared-blend duplication.
6. Implement reusable scalar ramp / Curve Bias data + evaluator + GPU shaping integration.
7. Implement the reusable ramp UI module using the same evaluator.
8. Consolidate mask placement render payload/bind/UI for texture Mask + Color ID while keeping serialized authored fields safe.
9. Generalize scoped/gated mask producers.

## Phase B — outputs and document model

10. Introduce one canonical address/remap helper.
11. Harden mask published-source dependency validation.
12. Consolidate shared typed-output address/validation/capability infrastructure without merging incompatible payload registries.
13. Create DocumentOps and move copy/duplicate/move/paste/scope/remap rules out of Slate.
14. Move identity regeneration/validation under the same document service.
15. Refactor effective-layer construction behind that boundary.

## Phase C — preview UX and orchestration

16. Centralize preview state ownership.
17. Route numeric + enum mutations through consistent preview invalidation.
18. Clear stale preview on selection changes.
19. Implement capability-driven `P / Shift+P / I / X` preview behavior.
20. Add bottom-center preview/help HUD from the existing preview label/source of truth.
21. Remove temporary/duplicate preview hotkeys.

## Phase D — CPU/editor performance

22. Remove synchronous asset loads from compose/UI/gallery interactive paths.
23. Replace repeated full-document validation/copies with shared DocumentOps queries/caches.
24. Cache GUID/address/binding lookups by document structural version.
25. Optimize history/effective-state copies only after the new boundaries are stable.
26. Clean remaining per-paint allocations/traversals.

## Phase E — GPU/pass performance

27. Consolidate high-value shader helpers and shared pass utilities.
28. Remove verified unnecessary full-resolution clears/dummies/copy-backs.
29. Share RegionIndex/other expensive solved fields.
30. Optimize Composite height-neighbor evaluation.
31. Optimize FlowWarp, Peel, EdgeWear, Cracks, Pebbles/Rock hot paths from profiling.
32. Evaluate groupshared/half precision only where measured and numerically safe.

## Phase F — editor cleanup and features

33. Consolidate inspector/parameter-row construction where behavior is actually shared.
34. Introduce cached asset registry service.
35. Consolidate remaining child/effect metadata and repetitive widget machinery.
36. Lattice generator.
37. Freeze Up To Here.
38. Delete obsolete/dead/duplicate tests and final dead code.

---

# 13. What should not be combined

Keep these boundaries explicit.

### Mask shaping vs producer generation

Shared:

- normalization;
- input levels;
- ramp;
- balance/contrast/offset/invert;
- blending.

Producer-specific:

- curvature/AO/height signal mixing;
- craquelure lattice/growth;
- random ID logic;
- texture sampling;
- exact ID semantics.

### Placement vs procedural scale

Texture/Color-ID UV placement can share a model.
Craquelure cell scale, generator-space controls, and per-region random UV transforms are different concepts.

### Scalar masks vs typed fields

Share addressing and validation.
Do not force scalar masks, flow bundles, UV, and Region ID payloads into one identical storage object.

### Preview state vs viewport camera input

One preview model and one documented key map.
Viewport camera controls can remain owned by the viewport client.

### Modularization vs optimization

A shared helper can improve maintainability without making anything faster.
A performance claim should be profiled unless the cost is structurally obvious, such as synchronous loads or full-resolution GPU copies.

---

# 14. Current end-state architecture

The intended shape after this roadmap is:

```text
Authored Material / Layer / Child Data
        │
        ├── DocumentOps
        │     ├── identity
        │     ├── create / remove / move / duplicate / paste
        │     ├── scope validation
        │     ├── reference remap
        │     └── effective document view
        │
        ├── Address / Dependency System
        │     ├── capabilities
        │     ├── typed output references
        │     ├── validation
        │     └── publication lookup
        │
        ├── Gather
        │     └── cached typed render data
        │
        ├── GPU Producer Passes
        │     ├── masks
        │     ├── IDs
        │     ├── generators
        │     └── effects
        │
        ├── Shared Field Modules
        │     ├── placement
        │     ├── normalization
        │     ├── mask shaping
        │     │     └── Curve Bias / scalar ramp
        │     ├── blur / filters where common
        │     └── publication/debug helpers
        │
        ├── Compose / Cache
        │
        └── Editor
              ├── inspectors driven by reusable APIs
              ├── one preview-state model
              ├── one refresh/invalidation orchestrator
              └── Slate as UI, not document logic
```

---

# 15. Definition of done for this roadmap

The consolidation is successful when:

- all mask producers use one shaping/ramp contract;
- scoped gates are producer-agnostic where semantically valid;
- placement duplication is removed without breaking saved assets;
- output/reference addresses have one remap/validation foundation;
- document mutations no longer live independently across Slate widgets;
- duplicate assets cannot collide by GUID;
- preview state cannot become stale after selection/enum changes;
- preview hotkeys and HUD reflect one capability-driven model;
- interactive paths no longer perform avoidable synchronous asset loads;
- large document copies are reduced/cached rather than repeated by every UI path;
- expensive GPU solved fields and full-resolution copies are not needlessly repeated;
- new features such as Lattice and Freeze build on shared infrastructure instead of adding another special-case path;
- obsolete planning docs can be deleted because this file contains their surviving decisions, risks, ordering, and exclusions.
