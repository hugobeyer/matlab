# Recipes, user presets and group controls — implementation plan

Status: planning complete; feature implementation not started.
Date: 2026-10-22.
Evidence: targeted static source review only. No commands, builds, tests or diagnostics run.
All paths below are relative to the Mixtormat plugin root. New paths are explicitly proposed.

## 1. Required outcome

Two connected feature tracks:

1. **Reusable content:** group recipes, a Recipes column in the bottom gallery, and user presets for every generator/effect/filter plus scalar/color ramps.
2. **Reusable controls:** expose controls from group members/modules on the selected group's inspector, including float, integer, enum and ramps; extend driver support beyond float where execution permits it.

User-confirmed UI:
- Group RMB → Create Recipe.
- Inserting a recipe creates an editable group containing its authored layers/modules.
- Ship an author-created recipe library with the plugin.
- Add a Recipes column to the bottom gallery, preserving the existing columns.
- Module preset dropdown sits on the **top module foldout**, right aligned beside the eye/debug controls.
- Add horizontal Flip to color ramps.
- Replace HSV From IDs' palette-array UI with a color ramp.

Do not confuse an editable recipe insertion with a live composition reference. Existing reference layers keep a source composition link; recipe insertion copies authored content and regenerates identities.

## 2. Verified starting points

| Area | Existing implementation | Gap |
|---|---|---|
| Group data | `Source/MixtormatRuntime/Public/MixtormatLayerTypes.h`: group identity, display name, enabled state, tint, children | No exposed controls; group bindings are explicitly reserved |
| Group membership | Same file: adjacent member layers; group children broadcast during effective-layer construction | Export must capture both the group record and its member layers |
| Composition insertion | `Source/MixtormatEditor/Private/Widgets/SMixtormat_Library.cpp`, `AddCompositionLayers` | Imports a whole composition, not a selected group recipe |
| Reference insertion | Same file, `AddReferenceLayerFromComposition` | Live reference semantics differ from editable recipe semantics |
| Gallery | Same file, `BuildBottomLibrary`: two splitter slots, one column fraction | Needs third column and updated sizing/state |
| Identity remapping | `Source/MixtormatRuntime/Private/MixtormatParameterBinding.cpp`, `RegenerateLayerIdentities` | External references remain external; future group exposures need remapping |
| Module persistence | Reflected child payloads in `MixtormatLayerTypes.h`; whole material save in `SMixtormat_Document.cpp` | No standalone typed user preset store identified |
| Header actions | `UI/Containers/SMixtormatInspectorGroup.*` and `SMixtormatInspectorCard.*` | Shared preset action must join existing actions, not replace them |
| Parameter types | `MixtormatParameterTypes.h`: Float, Int, Bool, Enum; enum TypeName | Ramps currently fall outside supported address value types |
| References | `MixtormatParameterBinding.*`: Follow and Link | Neither is an independent group-owned macro control |
| GPU drivers | `FMixtormatParameterDriver`: mask/region-ID signal and numeric remap/combine | Float-only availability; integer/enum/ramp semantics need definition |
| Color ramp | `UI/Controls/SMixtormatColorRamp.*`: built-in presets | No user file store or horizontal Flip |
| Scalar ramp | `UI/Controls/SMixtormatScalarRamp.*`: horizontal Flip | No general user preset API |
| HSV From IDs | `MixtormatIdTypes.h`: Palette; corresponding inspector/gather/compositor shader | Replace the whole palette contract, not only the UI |

Editor paths in that table are under `Source/MixtormatEditor/Private/` unless fully qualified.

## 3. Decisions required before affected code phases

### D1 — Meaning of group controls

Recommended first implementation: **editable aliases** of existing internal parameters.
- Expose to Group publishes an internal address with a label and ordering metadata.
- Editing the group control uses the existing source parameter edit path.
- Do not maintain two copies of the same authored value.
- Referenced/instanced sources retain their existing edit restrictions and link behavior.

Alternative: independently authored group values driving one or many targets. This requires group-owned addresses, value storage, fan-out, conflict policy and cycle detection. It is a larger feature, not an alias implementation detail. Confirm which is wanted before implementing P2.

### D2 — Integer, enum and ramp drivers

Separate **document-level value links** from **spatial GPU drivers**:
- Integer/enum links select one authored value for the module.
- Spatial signals can vary per pixel; many integer/enum parameters select topology, pass count or shader permutations on the CPU.
- A type alone must never make a parameter spatially drivable.

Recommended spatial rules, pending approval:
- Int: evaluate numeric mapping, apply documented rounding, then clamp to the destination's valid range. Explicitly choose nearest/floor semantics before code.
- Enum: explicit signal bins selecting valid enum values; no arithmetic on raw enum ordinals and no interpolation between enum cases.
- Disable unsupported topology/permutation targets with a reason; do not silently ignore their drivers.
- Ramp exposure/linking means editing/copying the complete ramp value, including interpolation.
- A ramp as a driver transfer function is a separate meaning: scalar ramp maps signal to float; color ramp maps signal to color. Neither means spatially mutating a variable-length stop array.

Confirm whether “ramps too” means preset/exposed ramp values only, or also ramp-shaped driver transfer functions. P3's shader scope depends on this.

### D3 — External recipe dependencies

Internal connections must remap on insertion. For references outside the captured group, present a dependency report and require an explicit resolution: include an eligible dependency, materialize an instance where supported, or reconnect after insertion. Never silently drop a connection or leave a foreign GUID appearing valid.

Textures, substrate/surface assets and referenced compositions are asset dependencies, not embedded JSON data. Shipping must include those assets. Arbitrary user JSON cannot automatically make them portable.

## 4. Ownership and persistence design

### Runtime

Own serializable preset payload types and group exposure definitions. No Editor includes, file dialogs, registry scanning or Slate in Runtime.

Proposed new files:
- `Source/MixtormatRuntime/Public/MixtormatPresetTypes.h`: typed preset envelope/payload contract.
- `Source/MixtormatRuntime/Public/MixtormatGroupParameterTypes.h`: exposure identity, target and presentation metadata, if extraction improves the group model.

Reuse canonical generator/effect/filter/ramp structs. Do not mirror every module field into a hand-maintained second schema.

### Editor store

Proposed new files:
- `Source/MixtormatEditor/Private/Services/MixtormatPresetStore.h`
- `Source/MixtormatEditor/Private/Services/MixtormatPresetStore.cpp`
- `Source/MixtormatEditor/Private/Services/MixtormatRecipeService.h`
- `Source/MixtormatEditor/Private/Services/MixtormatRecipeService.cpp`

One store service owns listing, reading, validation, writing and import/export. The recipe service owns group capture/dependency closure/insertion, not another persistence implementation.

Proposed storage contract:
- Writable user presets: project-local user Saved area, resolved through Unreal path APIs; exact directory declared once in the store.
- Explicit JSON export/import for sharing and backups; never overwrite plugin authoring defaults.
- Shipped recipes: discoverable plugin assets with their referenced content packaged; shipped entries are read-only in the UI.
- User copies have their own identity; never edit a shipped item in place.
- Do not add dependencies until existing module dependencies/serialization facilities are checked. Ask before any new dependency.

Preset envelope:
- Schema version, stable preset ID, kind, name, description/category.
- Canonical child type and generator/effect subtype, or scalar/color ramp kind.
- Typed authored payload, dependency declarations and optional preview reference.
- Recipe payload: group record, ordered member layers and exposure definitions.
- No selection, debug visibility, expansion state, transient render caches or current widget pointers.

Load completely into temporary state; validate before changing the document. Reject unsupported schema/type combinations with actionable errors. Save via a temporary file and safe replacement; ask before overwriting an existing named preset. Validate file size, nesting/array limits, names and destination paths. No silent clamping that changes authored data without reporting it.

## 5. Phased delivery

### P0 — Inventory and settle contracts

Files:
- `Source/MixtormatRuntime/Public/MixtormatParameterTypes.h`
- `Source/MixtormatRuntime/Public/MixtormatLayerTypes.h`
- `Source/MixtormatRuntime/Public/MixtormatEffect.h`
- `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorGenerators.cpp`
- `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorEffects.cpp`
- Other inspector builders discovered through their module entry points.

Tasks:
1. Resolve D1–D3 with the user; record chosen semantics here.
2. Inventory every generator/effect/filter subtype and its top foldout builder.
3. Identify structural selectors versus spatially drivable parameters.
4. Inventory every GUID/address-bearing payload: bindings, driver sources, instances, output references, structural links and group membership.
5. Check current build dependencies and registry ownership without changing them.

Exit: explicit capability matrix, complete capture contract and approved driver semantics.

### P1 — Implement the typed user preset store

Files: proposed `MixtormatPresetTypes.h`, `Services/MixtormatPresetStore.*`.
Reference existing owners:
- `Source/MixtormatEditor/Private/UI/Parameters/MixtormatParameterAuthoring.*`
- `Source/MixtormatEditor/Private/Widgets/SMixtormat_Document.cpp`

Tasks:
1. Implement typed encode/decode using existing serialization facilities where available.
2. Implement list/save/load/import/export and error reporting.
3. Keep module, ramp and recipe kinds distinct but use one store API.
4. Validate subtype and dependencies before applying anything.
5. Preserve destination identity, scope and existing downstream consumers when applying module settings.
6. Distinguish parameter-only module presets from full group recipes; module presets must not silently absorb or delete scoped child modules.
7. Document whether each dependency is included, referenced or requires reconnecting.

Exit: invalid/incompatible data cannot partially mutate a document; writable presets are separate from shipped content and parameter defaults.

### P2 — Group-exposed controls

Existing files:
- `Source/MixtormatRuntime/Public/MixtormatLayerTypes.h`
- `Source/MixtormatRuntime/Public/MixtormatParameterTypes.h`
- `Source/MixtormatRuntime/Public/MixtormatParameterBinding.h`
- `Source/MixtormatRuntime/Private/MixtormatParameterBinding.cpp`
- `Source/MixtormatEditor/Private/Widgets/SMixtormat_Parameters.cpp`
- `Source/MixtormatEditor/Private/Widgets/SMixtormat_Inspector.cpp`
- `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerHierarchy.cpp`
- `Source/MixtormatEditor/Private/UI/Parameters/SMixtormatParameterControl.*`

Proposed UI file: `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorGroupParameters.cpp`.

Tasks:
1. Implement the approved exposure model with stable exposure IDs.
2. Add Expose to Group to eligible parameter menus and an Add Control picker on group selection.
3. Restrict picker targets to member layers and group-owned modules; validate membership at edit time.
4. Render float, int, enum, scalar ramp and color ramp controls using existing widgets.
5. Use enum identity and valid declared values, not arbitrary integer conversion.
6. Preserve full ramp stops/interpolation and the source's domain/range contract.
7. Support label/order editing; use canonical parameter metadata for ranges/defaults.
8. Show broken/removed targets explicitly; never retarget by display name.
9. Integrate undo/redo, dirty state, preview invalidation and instance edit restrictions.
10. Extend duplication/import remapping to exposures and any approved group-owned bindings.

Exit: selecting a group edits its internal authored values; duplicate/insertion cannot redirect to the original group.

### P3 — Typed driver capability and evaluation

Files:
- `Source/MixtormatRuntime/Public/MixtormatParameterTypes.h`
- `Source/MixtormatRuntime/Private/MixtormatParameterBinding.cpp`
- `Source/MixtormatEditor/Private/Widgets/SMixtormat_Parameters.cpp`
- `Source/MixtormatEditor/Private/UI/Parameters/SMixtormatDriverPopover.*`
- `Source/MixtormatShaders/Private/MixtormatGpuCompositorInternal.h`
- `Source/MixtormatShaders/Private/Compositing/MixtormatGeneratorGather.cpp`
- `Shaders/Private/MixtormatDriver.ush`
- Additional gather/pass/shader consumers identified by the P0 capability inventory.

Tasks:
1. Introduce explicit per-parameter driver capabilities, shared by picker and execution validation.
2. Implement approved integer quantization/clamping consistently across CPU/GPU readers.
3. Implement enum mapping only for parameters whose execution can actually vary spatially.
4. Preserve same-enum-type checks for ordinary value references.
5. Include eligible group-owned sources; make group-only source selection possible.
6. Preserve existing float-driver semantics and reference/driver mutual-exclusion behavior unless explicitly changed.
7. If approved, implement scalar/color ramp transfer functions as separate typed driver outputs, using existing ramp evaluators and GPU limits.
8. Reject cycles/unavailable sources; preserve deterministic seeded ID behavior.

Exit: integer/enum options are not merely enabled UI on top of float-only execution. Unsupported targets explain why they are unavailable.

### P4 — Module preset dropdowns

Files:
- `Source/MixtormatEditor/Private/UI/Containers/SMixtormatInspectorGroup.*`
- `Source/MixtormatEditor/Private/UI/Containers/SMixtormatInspectorCard.*`
- `Source/MixtormatEditor/Private/UI/Menus/MixtormatMenuBuilder.h`
- `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorGenerators.cpp`
- `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorEffects.cpp`
- Other module inspectors inventoried in P0.

Proposed shared controller: `Source/MixtormatEditor/Private/UI/Parameters/MixtormatModulePresetMenu.*`.

Tasks:
1. Compose Presets dropdown into the top foldout HeaderAction, alongside existing eye/debug controls.
2. Preserve action order, hit targets and collapse behavior; clicking the menu must not toggle the foldout.
3. Provide compatible preset list, Save Preset, Import JSON and Export JSON.
4. Disable unavailable actions with styled explanations; preserve instance restrictions.
5. Apply through the canonical edit transaction path and refresh preview/controls.
6. Cover every registered generator/effect/filter subtype, including filters not represented by an effect payload.
7. Use design tokens and shared help; no local styling or native unstyled tooltips.

Exit: one consistent top-level dropdown per module; no replacement/removal of debug controls.

### P5 — Ramp preset integration and horizontal Flip

Files:
- `Source/MixtormatEditor/Private/UI/Controls/SMixtormatColorRamp.*`
- `Source/MixtormatEditor/Private/UI/Controls/SMixtormatScalarRamp.*`
- `Source/MixtormatEditor/Private/UI/Controls/SMixtormatRampEditor.*`
- `Source/MixtormatRuntime/Public/MixtormatScalarRamp.h`
- `Source/MixtormatRuntime/Private/MixtormatColorRampMath.cpp`
- `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorGenerators.cpp`

Tasks:
1. Keep built-in color presets; add user presets through P1's store.
2. Persist interpolation, positions and values; retain signed scalar domains and extended Y values.
3. Make cross-domain application explicit: exact-domain versus remap; do not silently normalize.
4. Add color Flip using X = domain minimum + domain maximum - X; move whole stops, sort and sanitize.
5. Reuse scalar edit callbacks/undo handling and clear stale point selection.
6. Document Constant interpolation behavior: mirrored stop positions need not produce an exact reflected step function at discontinuities.

Exit: ramps round-trip their authored data; Flip changes horizontal placement rather than color inversion.

### P6 — HSV From IDs color ramp

Files:
- `Source/MixtormatRuntime/Public/MixtormatIdTypes.h`
- `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorIds.cpp`
- `Source/MixtormatShaders/Private/Compositing/MixtormatIdGather.cpp`
- `Source/MixtormatShaders/Private/MixtormatGpuCompositorInternal.h`
- `Source/MixtormatShaders/Private/MixtormatGpuCompositor.cpp`
- `Shaders/Private/MixtormatComposite.usf`
- `Shaders/Private/MixtormatGeneratorHeightModules.ush`

Tasks:
1. Replace palette fields/UI and their active readers together; no deprecated palette compatibility layer.
2. Reuse FMixtormatColorRamp and its GPU payload/evaluation helpers.
3. Sample the ramp deterministically from the existing per-ID random signal.
4. Preserve tint-before-jitter, invalid-ID passthrough, scoped mask and existing winner ordering.
5. Preserve jitter-only behavior explicitly; default black-to-white ramp must not silently tint materials.
6. Represent flat tint with matching-color stops and expose the shared preset/Flip controls.
7. Update related metadata/help only after locating actual readers.

Exit: no old palette-array authoring path remains; behavior differences are documented and deliberate.

### P7 — Capture and insert group recipes

Files:
- Proposed `Services/MixtormatRecipeService.*`.
- `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerMenus.cpp`
- `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerClipboard.cpp`
- `Source/MixtormatEditor/Private/Widgets/SMixtormat_Library.cpp`
- `Source/MixtormatRuntime/Private/MixtormatParameterBinding.cpp`
- `Source/MixtormatRuntime/Private/MixtormatLayerGroups.cpp`

Tasks:
1. Add Create Recipe to group RMB; capture that group and its ordered members, not the entire document.
2. Include authored group modules, member modules, substrates, bindings, driver configuration, ramps and exposed controls.
3. Resolve/report external dependencies using D3 before saving.
4. Validate insertion completely before mutation; allocate fresh identities in one mapping pass.
5. Remap membership, scoped parents, instances, parameter addresses, output/structural references and exposures.
6. Insert as one undoable operation and select the new group so exposed controls are immediately visible.
7. Do not import unrelated global final settings into an existing document. Report any recipe dependence on global settings.
8. Preserve contiguous membership; do not invent nested groups without a supported runtime model.

Exit: inserting the same recipe twice produces independent editable groups with intact internal links.

### P8 — Recipes gallery and shipped library

Files:
- `Source/MixtormatEditor/Private/Widgets/SMixtormat_Library.cpp`
- `Source/MixtormatEditor/Private/Widgets/SMixtormat.h`
- `Source/MixtormatEditor/Private/Services/MixtormatRegistry.*`
- `Source/MixtormatEditor/Private/Widgets/Gallery/SMixtormatSurfaceCard.*`
- `Source/MixtormatEditor/Private/Widgets/Gallery/SMixtormatGalleryScrollBox.*`
- Existing gallery state/style owners located from GalleryColumnFraction and GalleryLayout references.

Tasks:
1. Add Recipes as a third column with search/category filtering and shared gallery cards.
2. Extend column sizing/persistence; retain Materials and the existing mask column.
3. Keep compact-width behavior usable, without hiding unavailable actions.
4. Distinguish shipped read-only recipes from user-owned recipes.
5. Insert through P7; do not accidentally route clicks to live reference insertion.
6. Generate/store thumbnails through an explicit authoring action, not an expensive render on each browse.
7. Package shipped recipe assets and declared dependencies through the existing Unreal content workflow.
8. Refresh gallery after save/import without rebuilding unrelated document state.

Exit: shipped and user recipes are discoverable and insert correctly through the new column.

### P9 — Integration review and authorized validation

Static review is permitted now. Builds, Unreal launches, diagnostics and tests require explicit user consent under repository rules. Do not add a test harness without consent.

Manual acceptance checklist, to execute only when authorized:
- Module/ramp JSON save, restart, load and explicit export/import.
- Wrong subtype, malformed file, unknown schema, missing asset and failed write leave state unchanged.
- Preset apply preserves module identity and downstream references.
- Header menu remains right aligned beside debug icons, collapsed and expanded.
- Group controls cover float/int/enum/ramps and respect invalid/instanced targets.
- Integer boundaries and enum non-contiguous values follow the agreed mapping.
- Unsupported spatial targets remain disabled with reasons.
- Color Flip and signed scalar ramp domains round-trip correctly.
- HSV defaults remain jitter-only; seeded behavior remains deterministic.
- Recipe insertion twice has no links back to the original group or the other insertion.
- Group-level controls, group-owned modules and internal driver sources survive save/reopen.
- Undo/redo restores complete authored state for apply/expose/insert operations.
- Shipped library works without developer source directories and missing dependencies are reported.
- Three-column gallery remains usable at narrow widths.

## 6. Implementation order and boundaries

Sequence: P0 → P1 → P2/P3 → P4/P5 → P6 → P7 → P8 → P9.
P4 and P5 can proceed independently after the store contract stabilizes. Recipe capture must account for final group-control/driver schemas before shipping content.

Do not mark a phase complete from UI availability alone. Each phase includes serialization, edit lifecycle, reference handling and actual execution where applicable.

No automatic migration stubs, fallback stores, duplicate compatibility layers, new packages or unrelated UI redesign. Preserve working behaviors outside the explicitly replaced HSV palette feature.

## 7. Current handoff

- [x] Audit reusable foundations and missing capabilities.
- [x] Record requested top-foldout preset placement and third gallery column.
- [x] Write phased implementation plan with owners and acceptance criteria.
- [ ] Approve group control ownership (alias versus independent macro).
- [ ] Approve integer/enum spatial mapping and ramp-driver meaning.
- [ ] Implement P0–P8 after the affected decisions are resolved.
- [ ] Obtain permission before executable validation in P9.
