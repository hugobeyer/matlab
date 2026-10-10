# Wrinkle and Curvature Pinch — implementation plan

Status: Phase 0 Sources shelf shell implemented (UI only: foldout below the layer stack, empty
state, disabled Add Source; no source storage, creation or evaluation). Everything else is
proposed, not implemented. Source review only; no commands, builds, tests or diagnostics.

## 1. Scope and user contract

Two distinct generator tools, displayed beneath their target:

- **Wrinkle**: Laplacian/curvature-guided height displacement, inspired by the Wrinkling prototype.
- **Curvature Pinch**: curvature-guided surface sliding adapted to a texture-space UV warp.

Both support the owner's own height via an immutable pre-operation snapshot. Neither reads
its own completed output recursively. External-source use consumes a completed Height output
from an eligible earlier layer generator or a dependency-evaluated shelf generator source, selected through
the planned source-first, Ctrl/Cmd-target-second workflow. Never bypass
existing structural validation to allow self-reference; local ownership is a separate execution case.

Inspector shows parameters only. Connections, repair and disconnection belong in row menus;
unavailable placements remain disabled with reasons. Preserve existing manual source creation.
Use existing scoped masks and Noise Gates; do not introduce a second masking system.
Preserve Copy as Instance/Paste, undo and parameter bindings within canonical placement rules.
Unsupported placements must remain disabled, not silently become local tools.

These are heightfield adaptations, not mesh folding, topology processing or collision solvers.
No promise of overhangs, volume preservation, thickness measurement or triangle protection.

## 2. Verified architecture and reuse

- `Source/MixtormatRuntime/Public/MixtormatEffect.h`: local generator tools are effect types;
  `MixtormatIsGeneratorFlowEffect` participates in ownership and classification.
- `Source/MixtormatShaders/Private/Compositing/MixtormatEffectGather.cpp`:
  `GatherGeneratorFlow` is the current local tool parameter path.
- `Source/MixtormatShaders/Private/MixtormatGpuGeneratorPasses.cpp`:
  `IsFlowToolChild` checks `ScopeOwnerSourceChildIndex`; tools run inside their owner.
- `Shaders/Private/MixtormatMaskCurvature.usf`: existing heightfield mean/Gaussian/principal
  curvature math. Its output is a shaped mask, not a raw signed curvature field.
- `AgentDocs/code_docs/generator_warp_output_alignment_design.md`: canonical immutable
  bundle pullback rules, typed companion handling and Noise-specific outputs.
- `AgentDocs/Prototypes/Wrinkling/wrinkling02.cl`: weighted Laplacian seed, normal-variation
  magnitude and curvature-dependent diffusion of that seed.
- `AgentDocs/Prototypes/Wrinkling/wrinkling01.cl`: side selection, mask shaping, bounded
  displacement along a normal/gravity blend.

Existing Shape Deform has a bulge/pinch control, but it is not the supplied neighbor-curvature
algorithm. Preserve that control and distinguish the new tool as **Curvature Pinch**.

## 3. Shared numerical contract

1. Capture signed Height before the tool; preserve its range and finite validity.
2. Derive all steering fields from that revision, using wrapped texture neighborhoods.
3. Define radius in tile-UV units; convert to per-axis sampling offsets for rectangular textures.
4. Document height-to-UV slope scale explicitly; do not inherit a mesh's average edge length.
5. Preserve raw signed curvature/Laplacian before any display mapping, gain or clamping.
6. Extract reusable derivative math locally if needed; preserve existing curvature mask behavior.
7. Strength zero and a fully zero mask are exact no-ops. Zero is never replaced by a default.
8. Use separate RDG input/output resources for iterations; no in-place neighborhood writes.
9. Bound iteration count, displacement and diffusion updates; reject non-finite calculations.
10. Gate each tool's applied delta/map once, without multiplying masks repeatedly in diffusion.

Proposed parameters below are UI concepts, not existing API names. Final reflected names and
ranges must follow the repository's metadata and parameter-authoring conventions.

## 4. Wrinkle algorithm

### First delivery: height-axis displacement

- Estimate a weighted signed Laplacian and a normal-variation magnitude from the source height.
- Shape the magnitude with curvature gain/power; keep the Laplacian's sign separately.
- Diffuse the Laplacian seed using curvature-dependent neighbor weights. Curvature and source
  height remain fixed during this solve, matching the prototype's seed-diffusion stage.
- Restrict the diffusion coefficient to a stable weighted update; clamping output alone is not
  a stability guarantee.
- Select Both / Peaks / Valleys before displacement. With neighbor-minus-center Laplacian,
  peaks are negative and valleys positive; verify labels against that exact convention.
- Define Both as both regions being affected, independently of signed displacement direction.
  Provide a bounded displacement range and Strength rather than infer direction from labels.
- Apply the scoped mask/Noise Gate and clamp the height delta to Maximum Displacement.
- Add the delta to target Height; do not move IDs, coordinates or coverage for a height-only edit.
- Invalidate/rederive outputs classified as height-derived; keep transported attributes unchanged.
- Publish final owner Height normally, before its ordinary layer contribution decision.

**Core controls:** Strength, Scale, Iterations, Fold Side, Curvature Influence,
Smoothness and Maximum Displacement. Advanced controls may expose curvature shaping and
signed displacement range; avoid showing every prototype constant initially.

Normal/gravity directional displacement is a later, explicit extension: it requires both UV
remapping and height displacement with a consistent map convention. Do not expose dead controls
or pretend height-only displacement reproduces the prototype's full 3D movement.
AO gating uses existing available mask inputs. Do not invent a thickness source.

## 5. Curvature Pinch algorithm

- Compute one consistent signed curvature field; the supplied VEX reads `@__curv` for the
  center and `curv` for neighbors, which must not be carried into the implementation.
- Sample a wrapped neighborhood. Positive center curvature weights lower-curvature neighbors;
  negative center curvature weights higher-curvature neighbors, as in the supplied code.
- Build a curvature-weighted target and a second target with baseline neighbor weights.
  The latter includes smoothing; it is not a separate topology-aware surface solver.
- Reconstruct local heightfield positions/normals with explicit units, then project the
  candidate movement onto the tangent plane. Translate its lateral component into UV motion.
- Convert forward sliding to the compositor's destination-to-source pullback convention.
  Do not apply the forward displacement directly as a sampling offset.
- Bound each step and total travel. Use local Jacobian/orientation checks to reduce or reject
  map folding; these are texture-warp safeguards, not mesh intersection guarantees.
- Start with frozen source curvature. Iterations integrate the map, not an undeclared geometry
  feedback loop. Adaptive curvature recomputation is outside the first delivery.
- Apply the gated map once to the target's immutable bundle, including Height/Coverage, IDs,
  random values, bed position, boundaries and producer-declared companions.
- Include Noise Value/Gradient in the existing aligned path; derive final height-based Flow
  at its normal publication stage, not from the stale unwarped height.

**Core controls:** Strength, Scale, Iterations, Curvature Pull, Surface Relaxation,
Damping and Maximum Travel. No mesh edge-length/triangle-angle/locking controls in this UI.
A flat field has no curvature pull; Surface Relaxation may still smooth it if enabled.

## 6. Authoring, source-only and cross-generator use

- Deliver local tools first through existing generator ownership; order with other tools matters.
- Name the dedicated area **Sources**, avoiding confusion with existing parameter drivers.
  It is a list, not a compositing stack: no blending or accumulated height between entries.
  Its presentation order is organizational only. Each generator publishes its own outputs.
- Include an **Influence Only** toggle on layer generators, default off for existing documents.
  On still evaluates tools and publishes completed outputs, but skips direct height contribution.
  Never zero Height Scale. Use one placement-specific flag, not duplicate contribution settings.
- Shelf generators are always source-only; do not show an Influence Only toggle there.
  They never write directly to material channels or inherit a layer's contribution behavior.
- Both locations use the same generator evaluation/output contracts. Source-only mode is useful
  for external steering but is not required for local tools. Audit normal/color/material writes too.
- Generator sources publish typed fields; global floats provide parameter values; reusable color
  ramps provide shared color mappings. Keep these distinct, without replacing parameter drivers
  or pretending floats/ramps are generator Height textures.
- External Wrinkle reads completed signed source Height, derives its delta in destination UV,
  and applies it to the target at a defined post-generation tool position.
- External Pinch derives a pullback map from completed source Height and applies it to the target.
- Reuse canonical typed output references and validation; do not repurpose Structural Warp's
  Flow/UVMap-only Source as Height or change Height Push's Strata-specific semantics.
- Before external implementation, settle one canonical relationship payload/execution path with
  the pair-selection work. Local and external modes must share numerical passes, not duplicate
  implementations or compatibility fallbacks.
- Existing later-target/earlier-source ordering remains for layer-stack generators. Shelf sources
  are evaluated by dependencies, not list positions; cycle detection and source validation are
  required before connecting them. Initially reject shelf reads of layer/composite outputs until
  explicit scheduling semantics are implemented. No implicit fallback to a layer source.
  Local snapshots do not relax external ordering.
- Support **Copy as Instance -> Paste into Sources** for compatible generators through the existing
  instance system. Share generator settings; retain a distinct placement identity and local ownership.
  Influence Only is placement-specific, never an instance-shared generator setting: a layer instance
  can contribute height while its shelf instance only publishes influence.
- Instance parameter sharing must not transplant owner/endpoint GUIDs into another placement.
  Owned tool/mask copying follows existing subtree and instance rules; do not claim recursive sharing
  without tracing those rules. Preserve unsupported-placement restrictions and dangling-source repair.

## 7. Implementation sequence

### Phase 0 — collapsible Sources area first (UI foundation)

This is the first implementation task, ahead of the new warp algorithms. Deliver the visible
area separately from functional source creation/evaluation; do not claim a working source system
when only the panel shell is present.

- Place a **Sources** foldout below the layer stack, in the same hierarchy pane, not in Inspector.
- Reuse existing layer/child row anatomy, typography, icons, selection/instance highlights,
  disclosure behavior and theme tokens. Reuse existing row widgets where their contracts fit;
  extract a small common presentation piece only if necessary, without coupling layer semantics.
- Reuse the existing foldout/header style. Expanded area shows a concise empty state and an
  Add Source action; keep it disabled with a reason until document-backed creation exists.
  Initially offer generators; add Global Float and Color Ramp when their binding paths are implemented.
- Keep the header reachable when the stack is long; constrain scrolling so the shelf cannot
  consume the whole hierarchy pane or squeeze the layer stack out of view.
- Collapse is editor UI state, not material evaluation state; closing the area never disables
  sources. Follow existing document/UI-state conventions and clear stale selection appropriately.
- Do not fabricate demo rows, hidden layers, fake generators or a second material stack.
- When functional rows arrive, generator sources have their tools/masks beneath them, not layer
  blend/compositing controls. Global floats are named-value rows; color ramps show a named gradient
  preview. Neither has generator children or layer compositing controls.
  Shelf sorting does not change evaluation order or accumulate one source into another.
- Selecting a functional source uses the existing parameter Inspector. Cross-area source/target
  selection must retain explicit source and target identities; do not infer them from panel focus.
- Provide explicit Paste as Instance into Sources for compatible generator clipboard payloads.
  No drag/copy shortcuts silently convert layer generators into shelf sources or vice versa.
  Keep unsupported actions disabled until their canonical address/ownership rules are implemented.

UI routing to inspect before editing: `Widgets/SMixtormat_Layers.cpp`,
`Widgets/SMixtormat_Shell.cpp`, `UI/Layers/SMixtormatLayerChildRow.*`,
`UI/Layers/SMixtormatLayerRow.*`, `UI/Containers/SMixtormatFoldoutHeader.*`,
and existing hierarchy selection/collapse behavior. Use shared design tokens/theme persistence.

### Phase 1 — functional generator sources, instances and Influence Only

- Confirm a document-owned source collection using existing generator payloads, stable GUIDs and
  typed output references. Extend canonical addressing/resolution; no parallel reference registry.
- Trace save/load, undo/redo, instance bindings, clipboard, gather and cache invalidation before
  enabling Add Source. Keep list presentation order separate from dependency scheduling.
- Reuse generator passes without layer composition; publish demanded outputs before consumers.
  One source may feed multiple targets. Detect cycles and do not reuse stale outputs after edits.
- Wire generator Paste as Instance through canonical sharing/resolution. Preserve placement-specific
  contribution, local owner/endpoint addresses and owned tool/mask restrictions across both locations.
- Implement Influence Only independently for layer generators, with default behavior unchanged.
- Enable shelf creation/selection/parameters only with document-backed data. Enable connections
  when typed resolution, validation and scheduling work end to end.
- Preserve Noise Gates, owned tools and valid instance/paste behavior in both locations.

### Later phase — global floats and reusable color ramps (HDA-like shared controls)

This extends Sources beyond generators, without making it a layer stack or renaming/replacing
existing parameter drivers. Generator sources ship first; global floats and color ramps follow.

- Inspect existing global-variable support, parameter bindings and active plans before implementation.
  Reuse canonical storage and binding resolution where available; do not create a second variable system.
- Add named document-level float parameters with stable identities, editable values, defaults and
  authoring ranges through existing metadata conventions. Names are labels, not fragile binding keys.
- Expose Add Source -> Global Float when functional. Reuse compact row/value controls and the
  existing parameter Inspector; no layer blending, height contribution or output-field menus for floats.
- Let compatible numeric parameters bind to these values through the existing parameter-driver UI.
  Keep binding behavior, ranges and instance override restrictions explicit; do not invent new sockets.
- One shared float may control multiple generators/operations, like a small HDA-style exposed control.
  This is not a full HDA/asset-packaging, expression-graph or arbitrary-type system in the first delivery.
- Trace save/load, undo, rename, duplicate, instance sharing and dependent cache invalidation.
  Renaming must preserve bindings; missing/deleted sources require visible repair, not silent defaults.
- Deletion follows existing confirmation/repair rules. If computed parameter dependencies are allowed,
  integrate canonical cycle detection; do not add expression evaluation solely for this shelf.

#### Reusable color ramp sources

- Add **Add Source -> Color Ramp** as a named, document-owned mapping with a stable identity.
  A ramp maps a scalar to color; it is not a composited layer or a generator by itself.
- Reuse existing color ramp data, evaluation/interpolation semantics and `SMixtormatColorRamp`
  / ramp editor controls. Trace canonical runtime storage and consumers before defining bindings.
- Show a compact gradient preview in the source row; selecting it opens existing ramp controls
  in the parameter Inspector. Do not introduce a second ramp editor or interpolation model.
- Let compatible ramp consumers reference the shared ramp while retaining their own scalar input,
  input range and application settings. Editing the source updates every linked consumer.
- Do not automatically sample ramps into float/color parameter bindings or publish a color texture:
  those require an explicit compatible consumer/input contract, not implicit type conversion.
- Support explicit Copy as Instance/Paste into Sources for compatible ramp payloads through
  canonical sharing rules. Preserve existing local ramps and ordinary copy behavior.
- Trace stable references through save/load, undo/redo, rename, duplication, clipboard and caching.
  Missing/deleted ramps need visible repair; never silently substitute a default ramp.
- Shelf order and collapse have no effect on ramp evaluation. Owning a ramp in Sources never
  writes material color directly; only its consuming operation applies the evaluated mapping.

### Warp phases — implementation and relationship integration

1. Trace local effect ownership, render structs, hashing, metadata, menus, capabilities,
   inspector and instance validation. Confirm the canonical payload before adding fields.
2. Append new serialized types; add defaults, sanitization, reflected controls and gather data.
   Trace Runtime -> gather -> render declaration -> shader binding -> shader -> Inspector.
3. Add shared signed-height derivative/seed passes with wrapped sampling and explicit units.
4. Implement local Wrinkle seed/diffuse/apply; integrate height-derived output invalidation.
5. Implement local Curvature Pinch map integration and canonical typed bundle pullback.
6. Wire parameters-only Inspector, scoped masks/Noise Gates, previews and publication as needed.
7. Integrate external Height references after canonical pair authoring is ready; revalidate at
   activation and refresh, prune transient selection, and preserve row repair actions.
8. Audit every authored field in instance sharing, parameter bindings, cache/prefix keys and undo.
   Update current generator/UI docs only after corresponding behavior is actually implemented.

Keep shared derivative/math code focused. Use separate Wrinkle and Pinch dispatch functions;
do not rewrite unrelated Generator Flow stages or add a parallel output registry.

## 8. Review and user-run acceptance

Agent validation is targeted static review only. Builds/runtime checks require user consent.

Review: serialized enum ordering, defaults, null source handling, RDG resource lifetime,
zero-strength bypass, mask placement, hash coverage, instance addresses and typed output alignment.

User-run scenarios when authorized:
- Disabled/zero-strength/zero-mask cases leave output unchanged.
- Flat plane, ridge, valley and saddle confirm sign labels and direction.
- Tile seams, rectangular outputs and multiple resolutions confirm scale behavior.
- Extreme strength/iterations stay finite; map limits do not advertise collision guarantees.
- Tool ordering, Noise Gates, undo/redo and Copy as Instance/Paste retain existing behavior.
- Sources foldout remains reachable, follows row/theme styling and preserves collapse behavior.
- UI-only shelf clearly disables unimplemented actions; collapse never changes material evaluation.
- Saved sources survive reload/undo; list reorder does not change outputs or compose entries.
- Cross-area pair selection and invalid/cyclic references have clear, non-destructive behavior.
- Paste as Instance into Sources shares settings without changing layer contribution or endpoint GUIDs.
- Global float edits update bound parameters; rename/reload/undo preserve their stable bindings.
- Shared ramp edits update linked consumers; each retains its own scalar input and application settings.
- Ramp instances, interpolation, rename/reload/undo and cache invalidation preserve existing semantics.
- Shelf color ramps never contribute color directly or depend on list order.
- Influence Only generator produces steering without direct layer-height contribution.
- Shelf generators publish outputs without directly writing material height or other material channels.
- External pair creation handles invalid/deleted/disabled sources without guessing replacements.
- Pinch keeps IDs/masks aligned; Wrinkle preserves coordinates and updates height-derived data.

Open limitations: texture-space approximation, local map checks only, no topology/thickness
solver, no full normal/gravity Wrinkle movement in the first delivery. Performance and visual
quality remain unverified until user-authorized Unreal checks.
