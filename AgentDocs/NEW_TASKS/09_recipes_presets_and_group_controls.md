# Recipes, user presets and group controls — implementation plan

Status: preset/recipe features plus independent group controls and typed spatial drivers are
implemented in source. The parent agent took over the interrupted Sol implementation and
completed integration/review. No builds, tests, diagnostics or Unreal launches were run.
See §11 for the current capability limits and handoff; earlier sections record intermediate status.
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

## 3. Design decisions (approved; historical alternatives retained below)

The user approved independent group-owned controls, nearest-rounded integer drivers,
explicit enum bins and scalar ramps as driver transfer functions. The implementation uses
those choices; it does not implement editable aliases or spatially mutable ramp stop arrays.
D3 is delivered as conservative rejection/reporting of external document references.

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
- [ ] Finish P0 inventory; implement remaining P1–P8 work (see exact status below).
- [ ] Obtain permission before executable validation in P9.

## 8. Historical ramp-only handoff — 2026-10-22 (superseded by §9)

The following records the initial delivery only. Use §9 for current implementation status.

This is a **partial delivery**, not completion of the requested feature plan. No shader,
module preset, recipe or group-control integration is claimed. Existing source was preserved.

| Phase | Actual status |
|---|---|
| P0 | Partial: ramp widgets, sanitizers, shared menu/help/style and current Editor dependencies inspected. Full subtype/address/driver capability inventory still outstanding. D1–D3 unchanged. |
| P1 | Partial: scalar/color user JSON store implemented. Module and recipe payloads, dependency declarations, metadata/category/preview fields and runtime envelope remain unimplemented. |
| P2 | Not implemented; D1 still requires alias versus independent macro approval. |
| P3 | Not implemented; D2 still requires integer quantization, enum mapping and ramp-driver meaning. Existing spatial driver execution is unchanged. |
| P4 | Not implemented. No module header preset dropdowns added. |
| P5 | Ramp implementation added: shared User Presets menu, typed round-trip, explicit X remap, color horizontal Flip. Existing built-in color presets remain. Runtime/editor acceptance is not executed. |
| P6 | Not implemented. Existing HSV palette data, inspector and shader readers are unchanged. |
| P7 | Not implemented. No capture, dependency resolution, identity remapping or insertion added. |
| P8 | Not implemented. No third gallery column, thumbnails or shipped recipe assets added. |
| P9 | Targeted source review only. No executable validation, diagnostics, commands or tests. |

### Exact changed-file manifest

All paths relative to the plugin root:

- `Source/MixtormatEditor/Private/Services/MixtormatPresetStore.h` — new typed ramp-store API.
- `Source/MixtormatEditor/Private/Services/MixtormatPresetStore.cpp` — new JSON encode/decode, validation, list/save/import/export.
- `Source/MixtormatEditor/Private/UI/Controls/SMixtormatRampEditor.h` — shared preset hooks and error state.
- `Source/MixtormatEditor/Private/UI/Controls/SMixtormatRampEditor.cpp` — themed preset menus, save-name entry and platform file dialogs.
- `Source/MixtormatEditor/Private/UI/Controls/SMixtormatColorRamp.h` — color preset hooks and horizontal Flip overrides.
- `Source/MixtormatEditor/Private/UI/Controls/SMixtormatColorRamp.cpp` — capture/apply and complete-stop horizontal reflection.
- `Source/MixtormatEditor/Private/UI/Controls/SMixtormatScalarRamp.h` — scalar preset hooks.
- `Source/MixtormatEditor/Private/UI/Controls/SMixtormatScalarRamp.cpp` — capture/apply, destination output-range and remapped-spacing checks.
- `AgentDocs/NEW_TASKS/09_recipes_presets_and_group_controls.md` — exact partial status and review handoff.

### Implemented storage and edit contracts

- One Editor service owns both ramp kinds; canonical Runtime ramp structs remain the payloads.
- User files resolve through `FPaths::ProjectSavedDir()` to `Mixtormat/Presets/<GUID>.json`.
  No excluded directory was inspected or edited by the agent; this is the runtime storage path.
- Schema 1 contains ID, name, kind, X domain, interpolation and typed numeric points/stops.
- Imports receive a fresh preset ID, enforce the importing ramp type and save without applying.
- Reads use temporary state, a 64 KiB file limit, depth limit and canonical point capacities.
- Invalid interpolation, nonfinite values, unsupported ranges and sanitizer-changing scalar
  endpoint/spacing data are rejected instead of silently corrected.
- Scalar Y values retain the canonical extended output range. A narrower destination disables
  application rather than clamping; remapped scalar spacing must remain valid too.
- User save rejects duplicate same-kind names; all writes reject existing destination files.
  There is intentionally no overwrite action. Publishing uses a sibling temporary file and move.
- Different X domains require the separate, explicitly labeled Remap X action. Y/color values
  and interpolation are preserved. No automatic normalization or fallback store was added.
- Preset application and Flip reuse begin/change/end edit callbacks, clear stale point selection
  and leave document/module identities untouched. Existing owners still own undo/dirty/preview.
  Callback lifecycle and actual undo/redo behavior require in-editor review, not assumed success.
- Color Flip mirrors complete stops with `X = DomainMin + DomainMax - X`, then sorts/sanitizes.
  Constant interpolation mirrors stop positions; discontinuities are not guaranteed to equal
  an exact reflected step function. It does not invert colors or reorder unrelated modules.
- New menus use `MixtormatMenu::FBuilder`, styled help, canonical styles and layout tokens.
  Errors appear in save-name menus and the next preset menu/help; graph layout is unchanged.
- No dependencies, package files, shader declarations or Runtime types were changed.

### Review-sensitive entry points

Line numbers reflect this handoff and may move during parent integration:

- `Services/MixtormatPresetStore.cpp:35` — `Private::Validate`; canonical sanitizer/range rejection.
- `Services/MixtormatPresetStore.cpp:106` — `Private::Read`; bounded JSON and transactional decode.
- `Services/MixtormatPresetStore.cpp:197` — `ExportRamp`; no-overwrite temporary-file publication.
- `Services/MixtormatPresetStore.cpp:255` — `SaveRamp`; naming and project-local storage.
- `Services/MixtormatPresetStore.cpp:275` — `ImportRamp`; fresh identity and kind validation.
- `UI/Controls/SMixtormatRampEditor.cpp:24` — `BuildSavePresetMenu`; commit/error handling.
- `UI/Controls/SMixtormatRampEditor.cpp:50` — `BuildUserPresetMenu`; domain gating and explicit remap.
- `UI/Controls/SMixtormatColorRamp.cpp:351` — `ApplyStoredPreset`; callback lifecycle.
- `UI/Controls/SMixtormatColorRamp.cpp:372` — `FlipHorizontally`; complete-stop reflection.
- `UI/Controls/SMixtormatScalarRamp.cpp:254` — `CanApplyPreset`; destination range/spacing checks.
- `UI/Controls/SMixtormatScalarRamp.cpp:280` — `ApplyStoredPreset`; scalar domain and edit callbacks.

Editor source paths above are under `Source/MixtormatEditor/Private/`.

### Genuine decisions versus unfinished implementation

- D1 blocks choosing a group-control ownership model, not the remaining independent UI/store work.
- D2 blocks new integer/enum/ramp spatial evaluation semantics, not ordinary ramp presets.
- D3 requires explicit resolution for recipes with outside references; no silent foreign GUIDs.
- Shipping an authored library still requires actual recipe assets and their dependencies.
- Remaining module presets, HSV replacement and recipe/gallery code are **unfinished work**, not
  falsely attributed to D1/D2. Parent review must not merge this as completion of the whole plan.
- Build, Unreal, diagnostics, tests and all other executable validation were explicitly forbidden
  and were **not run**. Compilation, file dialogs, disk round-trip and undo/preview are unverified.

## 9. Continuation delivery — module presets and closed group recipes

### Current phase status

| Phase | Actual source status |
|---|---|
| P0 | Current reflected payloads, identity/remapping owners, history/preview lifecycle and gallery sizing inspected. D1/D2 are still open; neither was used to defer the independent work below. |
| P1 | Typed module and recipe records and JSON list/save/load/import/export added to the existing store. Uses canonical Runtime payloads, strict reflected types and a common user root. Ramp APIs preserved. No new dependencies or alternate stores. |
| P2/P3 | Unchanged; no group-macro/alias or integer/enum/ramp-driver semantics chosen. |
| P4 | Preset menus wired into 20 generator/behavior/height/Boolean top foldouts, eight effect foldouts and six mask/filter foldouts. Existing header actions preserved. Ten IDs-file header hooks remain for Terra/parent to insert as specified below. |
| P5 | No ramp widgets changed in this continuation. Shared store header extended; store implementation only gains a canonical root getter. Color Ramp consumer header gains module presets and its old built-in menu label becomes “Ramp Presets” to avoid duplicate labels. |
| P6 | Owned separately by Terra. None of Terra's six conversion files were edited by this continuation. No completion claim is made for that work. |
| P7 | Group RMB capture, bounded typed JSON persistence, conservative external-reference rejection/reporting, all-GUID remapping in one mapping pass, local height-index rebasing and append insertion added. One isolated undo record; the inserted group is selected. No inclusion/materialization wizard is claimed. |
| P8 | Third Recipes column, search/category filtering, shared cards, user/shipped badges, import/export/copy actions, refresh and persisted three-way sizing implemented. Shipped JSON discovery added; no actual shipped binary assets, thumbnails or thumbnail-authoring pipeline produced. |
| P9 | Targeted file reads/searches and static source review only. No commands, builds, tests, diagnostics, Unreal launches or executable validation. |

### Continuation changed-file manifest (23 files)

Paths are relative to the plugin root. This list is **this continuation's** manifest;
§8 retains the earlier ramp-widget manifest as historical context.

New files:

1. `Source/MixtormatEditor/Private/Services/MixtormatPresetJson.h`
2. `Source/MixtormatEditor/Private/Services/MixtormatPresetJson.cpp`
3. `Source/MixtormatEditor/Private/Services/MixtormatPresetStoreDocuments.cpp`
4. `Source/MixtormatEditor/Private/Services/MixtormatRecipeService.h`
5. `Source/MixtormatEditor/Private/Services/MixtormatRecipeService.cpp`
6. `Source/MixtormatEditor/Private/Widgets/SMixtormat_Presets.cpp`
7. `Source/MixtormatEditor/Private/Widgets/SMixtormat_Recipes.cpp`

Extended existing files:

8. `Source/MixtormatEditor/Private/Services/MixtormatPresetStore.h`
9. `Source/MixtormatEditor/Private/Services/MixtormatPresetStore.cpp`
10. `Source/MixtormatEditor/Private/UI/Containers/SMixtormatInspectorGroup.h`
11. `Source/MixtormatEditor/Private/UI/Containers/SMixtormatInspectorGroup.cpp`
12. `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorGenerators.cpp`
13. `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorEffects.cpp`
14. `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorMasks.cpp`
15. `Source/MixtormatEditor/Private/Widgets/SMixtormat.h`
16. `Source/MixtormatEditor/Private/Widgets/SMixtormat.cpp`
17. `Source/MixtormatEditor/Private/Widgets/SMixtormat_Library.cpp`
18. `Source/MixtormatEditor/Private/Widgets/SMixtormat_Shell.cpp`
19. `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerMenus.cpp`
20. `Source/MixtormatEditor/Private/Widgets/Gallery/SMixtormatSurfaceCard.h`
21. `Source/MixtormatEditor/Private/Widgets/Gallery/SMixtormatSurfaceCard.cpp`
22. `Source/MixtormatEditor/Public/MixtormatEditorSettings.h`
23. `AgentDocs/NEW_TASKS/09_recipes_presets_and_group_controls.md`

No ramp widget, shader, Runtime model, build/dependency file or binary content was edited.
`MixtormatPresetStore.cpp` only centralizes the unchanged user root for the new document kinds;
its existing ramp encoding, menus and application implementation are otherwise untouched.

### Module preset contract

- `FModulePreset` carries a canonical child payload, not a hand-maintained field mirror.
- Active payload selection covers every current child type except the retired authoring tags.
- Compatibility checks child type plus Generator Type, resolved Effect Type, Behavior Type or
  Output Reference field kind where applicable. Subtype selectors remain serialized settings.
- Only the active payload is exported; child identity, display name, scope, instance endpoints,
  parameter bindings and separate scoped child modules are never absorbed into a module preset.
- The parameter-only codec excludes GUIDs, output/parameter addresses, Behavior field sockets,
  published-source output keys, published output names and the Color Ramp input-source selector.
  Applying overlays a temporary destination copy, so those destination connections survive.
- Existing bindings remain bound; applying a preset does not secretly edit an authoritative
  linked source, break a Follow/Link or add a spatial driver. The menu explains this policy.
- Save/apply/export are disabled for instances; import only writes the compatible user store.
  Apply re-resolves its captured stable owner/child address and rechecks type/instance eligibility.
- All mutations happen after decode/asset checks. Instances are synchronized **before** the
  post-edit history snapshot. History coalescing is fenced before/after the action; dirty state
  and preview submission follow the existing workspace owners. Debug target identities are not reset.
- `PresetAction` is a new additive header slot after existing actions, before reset. Eye/debug/
  enable controls are not replaced. Boolean Stack receives the top foldout it previously lacked.

### Store, dependency and recipe contract

- Common root: `GetUserPresetDirectory()` resolves the existing project-local Saved user root.
  Ramps keep their original files; modules use `Modules/*.json`, recipes `Recipes/*.json`.
- Shipped discovery: `<PluginBase>/Resources/Presets/Modules/*.json` and
  `<PluginBase>/Resources/Presets/Recipes/*.json`. Discovery is direct-directory, not recursive.
  Shipped entries are read-only sources; Save User Copy creates a fresh preset identity.
- To author a shipped recipe, export a validated recipe JSON into that Recipes directory and
  distribute it with all declared Unreal assets and their transitive dependencies. JSON is not
  an asset pack. No packaging/cook or installed-distribution validation was run or claimed.
- Document schema 1 stores ID, kind, name, category, description, direct referenced assets and
  typed payload. Reflected objects carry exact canonical `$type` names; field/type/schema
  mismatches, unknown/missing fields, invalid enums and nonfinite numbers are rejected.
- Reads are bounded at 8 MiB, depth 64 and 4096 elements per array. Ramp payloads that would
  require canonical sanitizer changes are rejected, not silently truncated or clamped.
- Soft assets are decoded as paths only. Explicit save/import/apply/insertion validates expected
  asset classes and availability. Browsing JSON does not render thumbnails or load its assets.
- Files publish via sibling temporary files with no overwrite. Duplicate user names are rejected;
  corrupt files are reported without hiding otherwise valid gallery entries.
- A recipe snapshot contains exactly the group record and its ordered, contiguous member layers,
  including complete reflected payloads, substrates, group/member bindings and driver settings.
- Capture reports referenced assets before saving and saves **that same snapshot**. Labels are
  materialized as displayed text, not imported localization histories/string-table links; the
  capture UI explicitly states this. Source data is never rewritten.
- A valid GUID anywhere in the captured authored data must belong to the captured identity set.
  Owner/child pairs, published-mask pairs, scopes, local source ordering, parameter-reference
  sources and instance placement are additionally checked. Dormant/disabled outside GUIDs are
  conservatively rejected too; no field is silently cleared or foreign GUID left looking valid.
- Live composition sources, outside height references, duplicate identities, noncontiguous
  membership and an outside first-member Fill Pass Through substrate are rejected with a report.
- Insertion revalidates the entire temporary recipe and referenced asset classes. One GUID map
  rewrites group/layer/module identities and every reflected GUID-bearing address together.
  Height-reference indices are local in the file and offset to the append destination afterwards.
- Append insertion leaves existing layer indices and group membership unchanged. No global/final
  settings, source composition link or unrelated layer data is imported or reset.
- The insertion is one undoable authored mutation, with instance sync before its history snapshot,
  then standard preview invalidation and selection of the new group. It is not live referencing.
- Recipes reuse gallery cards with surface dragging explicitly disabled. The shared card default
  remains unchanged for Materials. Invalid operations report through existing menu/status surfaces.
- Splitter weights now persist in `UMixtormatEditorSettings`; original columns remain, with
  token-based minimum widths and wrapped recipe text. No new palette or native tooltip added.

### Terra/parent integration: IDs top header hooks

Do not replace any existing `HeaderAction`. In
`Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorIds.cpp`, add this single
argument to each **top** `SNew(SMixtormatInspectorGroup)` chain, immediately after its Title:

```cpp
.PresetAction(MakeModulePresetButton())
```

Apply to these ten builders (not their nested cards):

- `BuildColorIdControls` — `ColorIdHeading`
- `BuildHsvFilterControls` — `HsvFilterHeading`
- `BuildPatternIdControls` — `PatternIdHeading`
- `BuildRampIdControls` — `RampIdHeading`
- `BuildUvIdControls` — `UvIdHeading`
- `BuildBoundaryIdControls` — `BoundaryIdHeading`
- `BuildReliefIdControls` — `ReliefIdHeading`
- `BuildIdGroupControls` — `IdGroupHeading`
- `BuildRandomIdControls` — `RandomIdHeading`
- `BuildOutputReferenceControls` — `OutputReferenceHeading`

The store reads the active HSV struct through reflection, so Terra's new canonical fields require
no palette-specific store branch. Existing saved schema-1 JSON from a different reflected layout
will be rejected explicitly rather than migrated. Terra's six owned files remain untouched here.

### Review-sensitive functions and remaining limits

Paths below are under `Source/MixtormatEditor/Private/`; line numbers may move during integration.

- `Services/MixtormatPresetJson.cpp:14` — `Included`: parameter-only exclusions/connection preservation.
- `Services/MixtormatPresetJson.cpp:35` — `TransferValue`: supported field kinds, enum/numeric bounds.
- `Services/MixtormatPresetJson.cpp:155` — `TransferStruct`: strict schema/type checking and ramp validation.
- `Services/MixtormatPresetJson.cpp:263` — `ValidateAssetClasses`: explicit-action asset/class validation.
- `Services/MixtormatPresetStoreDocuments.cpp:97` — `ReadEnvelope`: pre-parser file/depth bounds.
- `Services/MixtormatPresetStoreDocuments.cpp:148` — `Write`: no-overwrite publication.
- `Services/MixtormatPresetStoreDocuments.cpp:226` — `CaptureModule`: active typed parameter snapshot.
- `Services/MixtormatPresetStoreDocuments.cpp:239` — `ApplyModule`: temporary destination overlay.
- `Services/MixtormatRecipeService.cpp:28` — `Validate`: closed-set and address/placement checks.
- `Services/MixtormatRecipeService.cpp:131` — `Capture`: exact group closure and local height indices.
- `Services/MixtormatRecipeService.cpp:166` — `PrepareInsertion`: one GUID map and preflight.
- `Widgets/SMixtormat_Presets.cpp:90` — `BuildModulePresetMenu`: stable-address actions/history.
- `Widgets/SMixtormat_Presets.cpp:174` — `BuildRecipeCaptureMenu`: shown snapshot/dependency report.
- `Widgets/SMixtormat_Presets.cpp:202` — `InsertRecipe`: no document mutation before preflight succeeds.
- `Widgets/SMixtormat_Recipes.cpp:21` — `RefreshRecipeGallery`: store-only refresh and error reporting.
- `Widgets/SMixtormat_Recipes.cpp:99` — `BuildRecipeGallery`: third-column filtering and shared controls.

Remaining limits are explicit, not blockers attributed to D1/D2:

- IDs header hooks were subsequently integrated by Terra; see §10 for final integration status.
- Recipe insertion requires an existing working material. The menu disables insertion otherwise;
  card clicks report that a material must be created/opened first. No document/history is reset.
- No thumbnail generation/authoring pipeline or actual shipped library content was supplied.
- Recipe closure validation is not proof that every GPU field is available for every authored
  shader configuration. No shader/driver changes or new enum/ramp GPU support are claimed.
- D1/D2 group exposure/driver choices remain open and are not implemented by these flows.
- **No executable validation was performed.** Compilation, disk round-trip, platform dialogs,
  repeated insertion, undo/redo, responsive layout and actual preview results remain unverified.

## 10. Parent review and Terra integration

Current status supersedes the intermediate handoffs in §8–9.

Implemented in source:
- User JSON presets for modules and scalar/color ramps; explicit import/export and no overwrite.
- Right-aligned module preset actions on top foldouts, including all ten IDs headers from §9.
- Color horizontal Flip and explicit cross-domain ramp preset application.
- HSV From IDs canonical color ramp across Runtime, inspector, gather, render payload and shader.
- Group RMB capture and independent editable-group insertion with GUID remapping.
- Third Recipes gallery column, filtering and shipped/user JSON discovery.

Terra's additional changed-file manifest:
- `Source/MixtormatRuntime/Public/MixtormatIdTypes.h`
- `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorIds.cpp`
- `Source/MixtormatEditor/Private/Widgets/SMixtormat.h` (retired palette helper declarations)
- `Source/MixtormatShaders/Private/Compositing/MixtormatIdGather.cpp`
- `Source/MixtormatShaders/Private/MixtormatGpuCompositorInternal.h`
- `Source/MixtormatShaders/Private/MixtormatGpuCompositor.cpp`
- `Shaders/Private/MixtormatComposite.usf`

Review performed by the parent:
- Read typed serializer, storage application, recipe capture/validation/remapping/insertion,
  menu integration, ramp callbacks and gallery construction.
- Checked existing dependency declarations: DesktopPlatform, Json and Projects already exist.
- Traced HSV ramp gather/bindings/evaluator and inspected its inspector edit lifecycle.
- Found and fixed ramp listing failure isolation: one malformed file no longer hides valid
  entries or blocks unrelated saves. Errors remain visible; invalid files are not changed/deleted.
- Requested and reviewed Terra's HSV ramp edit lifecycle correction: explicit interactive history
  hooks plus disabled instance editing and callback-level instance checks.
- No executable proof is claimed for compilation, storage round-trips, shaders or undo behavior.

Outstanding work:
- P2: group-exposed controls (alias versus independent group-owned macro is unresolved).
- P3: integer/enum spatial evaluation and whether ramps are driver transfer functions.
- Authored shipped recipe content and visual thumbnails; current gallery entries are text cards.
- Build and in-editor acceptance require explicit permission under repository rules.

Important delivered restrictions:
- Recipes with external document references are rejected with a report, not silently disconnected.
- Referenced assets must be installed; JSON does not embed texture/substrate assets.
- Insertion requires an existing material document.
- Module presets preserve destination identities, bindings, connections and scoped children.
- HSV tint defaults to zero so the default black-to-white ramp preserves jitter-only behavior.

## 11. Direct takeover — group controls and typed drivers

Supersedes §8–10 for P2/P3. The interrupted Sol session had written substantial source before
cancellation. The parent retained that work, traced integration and completed focused fixes
without launching another implementation agent.

### Delivered group-control behavior

- Group selection exposes Add Control plus compact wells for independent float, integer, enum,
  scalar-ramp and color-ramp values. Values are owned by the group, not aliases. See §12 for
  the revised creation, labeling and assignment interface.
- Create a blank supported control or Expose Member Parameter; enum identity is taken from
  a real internal enum property. Parameter context menus also offer Expose to Group.
- Attach multiple compatible member-layer or group-module targets to one control.
- Edit label, enable state, order and reset value; detach targets or remove a control.
- Runtime applies overrides to transient render copies; the target's authored base survives.
- Ordinary references read effective group-driven values. Instances, conflicting links/drivers,
  duplicate ownership, missing targets and incompatible enum/ramp domains are rejected/reported.
- Controls target leaf parameters only, not each other; no group-control cycles or guessed aliases.
- Full ramp payloads/interpolation persist. Height Color Ramp retains its signed stored domain;
  its existing module inspector still presents its unit-coordinate view.
- Control identities and target addresses persist in documents/recipes and remap on insertion,
  composition copying and parent moves. Reflection-based history includes all control data.
- Undo/redo preserves a selected group header while that group still exists and rebuilds its
  controls. Undoing past group creation still clears the missing selection.

Primary Runtime owners:
- `Source/MixtormatRuntime/Public/MixtormatGroupParameterTypes.h`
- `Source/MixtormatRuntime/Public/MixtormatGroupControls.h`
- `Source/MixtormatRuntime/Private/MixtormatGroupControls.cpp`
- `Source/MixtormatRuntime/Public/MixtormatParameterTypes.h`
- `Source/MixtormatRuntime/Public/MixtormatParameterBinding.h`
- `Source/MixtormatRuntime/Private/MixtormatParameterBinding.cpp`
- `Source/MixtormatRuntime/Public/MixtormatLayerTypes.h`
- `Source/MixtormatRuntime/Private/MixtormatLayerGroups.cpp`

Primary Editor integration:
- `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorGroupParameters.cpp`
- `Source/MixtormatEditor/Private/Widgets/SMixtormat.h`
- `Source/MixtormatEditor/Private/Widgets/SMixtormat.cpp`
- `Source/MixtormatEditor/Private/Widgets/SMixtormat_Inspector.cpp`
- `Source/MixtormatEditor/Private/Widgets/SMixtormat_Parameters.cpp`
- `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerHierarchy.cpp`
- `Source/MixtormatEditor/Private/Services/MixtormatRecipeService.cpp`
- Ramp consumers in `Widgets/Inspector/MixtormatInspectorGenerators.cpp`,
  `MixtormatInspectorMasks.cpp` and `MixtormatInspectorIds.cpp` display group-driven ramp values
  and prevent edits to an overridden local ramp.

### Delivered spatial driver behavior and explicit capability limits

A group control can edit a uniform authored integer/enum even when that property cannot vary
per pixel. The spatial capability registry is deliberately narrower:

| Type | Implemented spatial reader | Evaluation |
|---|---|---|
| Integer | Published Flow placement `BehaviorFlow.FlowSteps` on Warp/Deform | Numeric mapping/combine, nearest rounding with ties toward +infinity, then clamp to 1–64 steps |
| Enum | Eikonal Feather `BehaviorFeatherSettings.Side` on supported generator/behavior parents | Increasing upper-inclusive signal bins, final threshold 1, exact declared enum values |
| Scalar ramp | Supported float/int/enum drivers | Optional transfer curve after invert/input window, before output mapping or enum bins |

- Unsupported topology, pass-count and shader-permutation selectors stay disabled with a reason.
- Float chains keep their original evaluation when the transfer curve is disabled.
- Scalar curve Y values outside 0–1 are preserved for numeric output mapping; enum bin inputs
  are normalized to 0–1. Enum drivers do not perform ordinal arithmetic or interpolate enum cases.
- Driver source picking validates execution order and includes compatible group-owned ID producers.
- The shared GPU transfer pass is cached per driver/source during composition, not persisted.
- Flow tracing has a real third driver slot for Steps; Feather has a real per-pixel Side signal.
- Invalid/unavailable signals retain the authored value rather than supplying an arbitrary enum.
- Typed driver controls include a shared scalar ramp editor with user presets, bin editing and
  validation messages. Numeric Amount/Combine/output controls are disabled for enum drivers.

Primary driver files:
- `Source/MixtormatRuntime/Public/MixtormatParameterDrivers.h`
- `Source/MixtormatRuntime/Private/MixtormatParameterDrivers.cpp`
- `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorDriverTypes.cpp`
- `Source/MixtormatEditor/Private/UI/Parameters/SMixtormatDriverPopover.cpp`
- `Source/MixtormatShaders/Private/MixtormatGpuDriverPasses.cpp`
- `Source/MixtormatShaders/Private/MixtormatGpuCompositorInternal.h`
- `Source/MixtormatShaders/Private/Compositing/MixtormatGeneratorGather.cpp`
- `Source/MixtormatShaders/Private/Compositing/MixtormatLayerGather.cpp`
- `Source/MixtormatShaders/Private/MixtormatGpuGeneratorPasses.cpp`
- `Source/MixtormatShaders/Private/MixtormatGpuCompositor.cpp`
- `Source/MixtormatShaders/Private/MixtormatGpuComposePipeline.cpp`
- `Shaders/Private/MixtormatDriverTransfer.usf`
- `Shaders/Private/MixtormatDriver.ush`
- `Shaders/Private/MixtormatFlowTrace.usf`
- `Shaders/Private/MixtormatBehaviorFeather.usf`

### Direct takeover fixes and static review

- Fixed integer-to-float promotion in group-control hard-bound validation.
- Require complete control metadata/default-value validity before it can drive a target.
- Added finite-input protection to the integer quantization helper.
- Made integer output-mapping slider ranges follow the execution capability, not float-only ranges.
- Added scrolling for typed driver panels and long group target menus, using the shared scrollbar
  style and centralized `ParameterPopoverMaxHeight` in `Style/MixtormatDesignTokens.h`.
- Preserved group inspector selection across value undo/redo and cleared pending interactive state.
- Traced group expansion → instance/reference resolution → gather, plus control ID remapping,
  recipe closure validation, shader parameter arrays and driver snapshot demands.
- Confirmed the HSV ramp edit guard resolves shared group children through `ResolveChild` when
  the layer index is `INDEX_NONE`; group-owned modules need no alternate resolution path.
- Reviewed typed-driver controls and the transfer-pass CPU/shader parameter declarations.
- Source review only: compilation, UI interaction, shader compilation, disk round-trip and actual
  undo/redo behavior are not execution-validated. No tests or diagnostics were run.

Remaining delivery and validation:
- User-authored shipped recipe assets and their dependencies are still required.
- Gallery entries currently use text cards; thumbnail generation/authoring is not implemented.
- Build/editor acceptance requires explicit permission, not inferred from implementation approval.

## 12. Group-control UI revision

Replaces the initial card-per-control presentation and bare type menu:

- One label inside each control well; no repeated card title or permanently visible rename field.
- Unlabeled enable toggle at the right. Disabled controls retain their values and RMB actions.
- RMB → Edit Label / Control Settings opens a draft popup, styled like the driver editor.
- Add Control opens that popup with label/type/default and numeric scrub Min/Max. Create/Apply
  commits one edit; Cancel/window close leaves the document unchanged. Settings preserve the
  current value and connections; Reset uses the authored default.
- The popup is an owned modal window so selecting type/enum/ramp-preset menus cannot dismiss
  an uncommitted draft. It reuses `SMixtormatDriverPopover` with a dedicated shared width token.
- Primary connection workflow: RMB the actual member parameter → Assign to Group Control →
  New Control or an existing control. The same menu supports detaching the current assignment.
- Creating from a parameter derives its label, type, value, range and ramp domain. Enum controls
  require this path so the declared enum choices remain authoritative.
- Root-network, generated-mask, mask-shaping, Height Remap, Height Color Ramp and HSV ramp
  inspectors now expose their parameter context menus too. Empty-space RMB bubbles to the menu;
  point RMB retains deletion behavior.
- Counts/connections move to RMB. The optional Attach Target picker filters by type/enum identity
  and no longer prints raw reflected struct names. Actual validation errors remain visible.
- Fixed the group-selection header refresh and suppressed the empty selection badge.

Files:
- `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorGroupParameters.cpp`
- `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorGroupControlMenus.cpp`
- `Source/MixtormatEditor/Private/UI/Parameters/SMixtormatGroupControlPopover.h`
- `Source/MixtormatEditor/Private/UI/Parameters/SMixtormatGroupControlPopover.cpp`
- `Source/MixtormatEditor/Private/UI/Parameters/SMixtormatDriverPopover.h`
- `Source/MixtormatEditor/Private/UI/Parameters/SMixtormatDriverPopover.cpp`
- `Source/MixtormatEditor/Private/Style/MixtormatDesignTokens.h`
- `Source/MixtormatEditor/Private/Widgets/SMixtormat.h`
- `Source/MixtormatEditor/Private/Widgets/SMixtormat_Parameters.cpp`
- `Source/MixtormatEditor/Private/Widgets/SMixtormat_Inspector.cpp`
- `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerHierarchy.cpp`
- `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorGenerators.cpp`
- `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorMasks.cpp`
- `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorIds.cpp`
- `Source/MixtormatEditor/Private/UI/Controls/SMixtormatRampEditor.cpp`

Validation: targeted source review only. No commands, builds, tests, diagnostics or Unreal launch.
The compact rows and popup have not yet been visually or interactively verified in the editor.
