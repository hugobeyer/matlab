# Mixtormat — Stack, Inspector & Preview Uniformity Spec

Complete technical reference and chunked execution plan for unifying the Layer Stack, Inspector, Shaders, and Preview shortcuts.
Designed for low-token, step-by-step execution without re-auditing the codebase.

---

## Chunk 1A — Retire Legacy `BooleanOps` Field Combine (DONE — Inspector UI only)

Done: `BuildBooleanOpsSourceMenu()` deleted, `OPERATION`/`FIELD COMBINE` cards removed, header
simplified, group renamed `SCATTER CARVE`. `Mode` already defaulted to `ScatterCarve`, so the
remaining UI is coherent. Still deferred: the runtime `EMixtormatBooleanMode` / field-combine
members, `MixtormatBooleanField.usf`, `FMixtormatBooleanFieldCS` and `AddBooleanFieldPasses`
are now unreachable from authoring but not yet deleted. `MixtormatChildCapabilities.cpp` still
branches on `BooleanOps.Mode != ScatterCarve` to publish the `Affected Area` output; that branch
is now dead and belongs in the same cleanup.

### Problem
Two Boolean systems currently coexist:
1. **New (`EMixtormatLayerChildType::Boolean`)**: Stack group container owning up to 2 child generators (`TARGET` and `SOURCE`, or 1 child in `SELF` mode), evaluated by `MixtormatBooleanStack.usf` (`AddBooleanStackPasses`) and inspected via `BuildBooleanStackControls()`.
2. **Legacy (`EMixtormatGeneratorType::BooleanOps`)**: A Generator union entry that has a `Mode` dropdown (`Scatter Carve | Boolean | Round | Bevel`) and a `FIELD COMBINE` card with a `Source` chip picker in `BuildBooleanOpsControls()`, evaluated by `MixtormatBooleanField.usf` (`AddBooleanFieldPasses`).

### Exact Changes
- **Inspector (`Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorGenerators.cpp`):**
  - `BuildBooleanOpsSourceMenu()` (L490–528): Remove.
  - `BuildBooleanOpsControls()` (L530–754): Remove the `OPERATION` card (`BooleanMode` dropdown) and `FieldCards` (`FIELD COMBINE` card with `FieldOperation`, `Source` chip, `FieldSourceScale`, `FieldBias`, `FieldAmount`, `FieldWidth`). Keep only `ScatterCards` (`SCATTER CARVE`, `GEOMETRY`, `DEPTH`, `CHAMFER`) and rename heading to `SCATTER CARVE`.
- **GPU & Gather (`Source/MixtormatShaders/`):**
  - `MixtormatGpuGeneratorPasses.cpp`: Remove `FMixtormatBooleanFieldCS` (L78–110), `AddBooleanFieldPasses` (L2482–2545), and the `Mode != ScatterCarve` branch in `AddGeneratorLayerPasses`. Keep `FMixtormatBooleanCarveCS` (`MixtormatBooleanCarve.usf`) and `FMixtormatBooleanStackCS` (`MixtormatBooleanStack.usf`).
  - `Shaders/Private/MixtormatBooleanField.usf`: Delete once `FMixtormatBooleanFieldCS` is removed.
  - `Compositing/MixtormatGeneratorGather.cpp`: Remove `BooleanOps` field-combine source resolution (`SourceChildId` → `SourceChildIndex`, `FieldOperation`, `FieldBias`, `FieldWidth`, `FieldSourceScale`, `FieldAmount`).
- **Runtime (`Source/MixtormatRuntime/Public/MixtormatGeneratorTypes.h`):**
  - Remove `EMixtormatBooleanMode` and the field-combine members on `FMixtormatBooleanOps` (`SourceChildId`, `FieldOperation`, `FieldWidth`, `FieldBias`, `FieldSourceScale`, `FieldAmount`).

---

## Chunk 1B — Convert Behavior Mode Dropdowns to `SMixtormatSegmentedControl` (DONE)

Done: `Combine` (`AddBehaviorFieldCompositionRows`) and Warp `Sampling Stage` are segmented
strips; both refuse a choice the runtime would reject rather than storing a dead value.
`Deform Mode`, `Feather Mode` and `Feather Side` were already segmented.

### Problem
Behaviors in `MixtormatInspectorGenerators.cpp` use `MakeMemberEnum` / `MakeDropdown` + `MakeChip` for 2–3 option switches, hiding options behind clicks.

### Exact Changes (`Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorGenerators.cpp`)
Reuse `SMixtormatSegmentedControl` (per `AgentDocs/NEW_TASKS/01_behavior_inspector_dropdowns.md`):
- **Shared Field Composition (`AddBehaviorFieldCompositionRows`, L1229):**
  - Replace `Combine` dropdown (`MakeDropdown` + `MakeChip`) with segmented control for supported blend operations.
- **Carve / Sculpt (`BuildBehaviorCarveControls`, ~L1200–1310):**
  - `CarveMode` → Segmented: `Standard | Traced`.
  - `TraceOperation` → Segmented: `Groove | Deposit`.
- **Warp (`BuildBehaviorWarpControls`, ~L1550–1600):**
  - `Stage` (`Sampling Stage`) → Segmented: `Before | After` (`PreGeneration | PostGeneration`).
  - `FlowMode` → Segmented: `Flow | Gravity` (keep `ModeMix` as a 0–1 slider).
- **Deform (`BuildBehaviorDeformControls`, ~L1815–1970):**
  - `DeformMode` → Segmented: `Standard | Traced`.
- **Feather (`BuildBehaviorFeatherControls`, ~L2035–2105):**
  - `Mode` → Segmented: `Blur | Eikonal`.
  - `Side` (`Feather Side`) → Segmented: `Both | Inside | Outside`.

---

## Chunk 1C — Remove Behavior Source/Target Pickers (Implicit Stack Consumption)

### Problem
Behaviors still expose node-graph-style `MakeDropdown` + `MakeChip` source selectors in the Inspector instead of reading from their parent Generator and sibling stack order (`AgentDocs/FINAL_BEHAVIOR_PLAN.md`).

### Exact Changes
- **Inspector (`MixtormatInspectorGenerators.cpp`):**
  - **Push (`BuildBehaviorPushControls`, L1295):** Remove `Signed Height Field` dropdown (`B->Height.Origin`). Push automatically modifies working height.
  - **Carve (`BuildBehaviorCarveControls`, L1236, L1296):** Remove `Signed Boundary / SDF` and `Steering Source` dropdowns; use owner's boundary and working field.
  - **Warp (`BuildBehaviorWarpControls`, L1504, L1576):** Remove `Direction Field` and `Steering Source` dropdowns; consume accumulated Flow from parent/preceding sibling `Flow` generators.
  - **Deform (`BuildBehaviorDeformControls`, L1857, L1961):** Remove `Direction Field` and `Steering Source` dropdowns; consume accumulated Flow.
  - **Flow Field legacy (`BuildBehaviorFlowFieldControls`):** Replaced by scoped `Flow` preset of `Noise` generator (`EMixtormatChildCreation::NoiseFlow`); remove redundant Flow Field behavior source menus.
- **Gather / Runtime (`MixtormatGeneratorGather.cpp`, `MixtormatGeneratorTypes.h`):**
  - Default/lock Behavior field origins to the parent generator's working height / accumulated flow / boundary without requiring explicit `PublishedOutput` GUIDs.

---

## Chunk 1D — Other Inspector Uniformity (Sublayers, Effects & IDs)

### Problem
Other Inspector panels (`MixtormatInspectorGenerators.cpp`, `MixtormatInspectorEffects.cpp`, `MixtormatInspectorMasks.cpp`, `MixtormatInspectorIds.cpp`) have dropdowns that either duplicate stack row badges or should be segmented buttons.

### Exact Changes
- **Duplicate Blend Mode Dropdowns (already on stack row badges):**
  - `ColorId` (`MixtormatInspectorIds.cpp` L688): `Blend` card `Mode` dropdown duplicates the row's blend badge.
  - `RampId` (`MixtormatInspectorIds.cpp` L1419): `Blend` dropdown in Inspector vs row badge.
- **2-Option Mode Dropdowns → Segmented Controls:**
  - `Craquelure` (`MixtormatInspectorMasks.cpp` L400): `Mode` (`Lattice | Propagated`).
  - `Surface IDs / Cluster` (`MixtormatInspectorMasks.cpp` L218): `Source` (`Layer Surface | Composite Below`).
  - `Stain` (`MixtormatInspectorEffects.cpp` L288): `Output Mask` (`Wet | Deposit`).
  - `LayerBlur` (`MixtormatInspectorEffects.cpp` L591): `Scope` (`Layer Coverage | Whole Composite`).
  - `Peeling` (`MixtormatInspectorEffects.cpp` L158): `Peel Type`.
  - `FlowWarp` (`MixtormatInspectorEffects.cpp` L679): `Blend` (`Replace | Min Height | Max Height`).
- **Generator Sublayer Source Chips (`MixtormatInspectorGenerators.cpp`):**
  - `HeightBlend` (`BuildHeightBlendModuleControls` L2182): Uses a `Source` module chip picker (`MixtormatGeneratorHeightBlend.usf`). Can either become a child/boolean operation or default to the preceding generator in stack order.
  - `HeightColorRamp` (`BuildHeightColorRampControls` L2457, L2481): Has `Source` (`Running | Module | Layer Input | Composite Below`) + `Module` chip picker.

---

## Chunk 2A — Flatten Context Menus, Unify Blur/FlowWarp & Kill "Noise Gate" (DONE)

### Problem
1. **Noise Gate duplication:** Context menus (`MixtormatLayerMenus.cpp` L218, L419) expose `Noise Gate` alongside `Noise Mask`. Hierarchy rows (`MixtormatLayerHierarchy.cpp` L1015) label scoped noise masks `NOISE · GATE` instead of `NOISE` or `MASK`.
2. **Nested `Masks >` submenu on Behaviors/Generators:** Right-clicking a Generator or Behavior (`BuildGeneratedContextMenu` L1503) hides masks inside a `Masks >` submenu instead of showing top-level child actions (`AgentDocs/NEW_TASKS/06_flatten_behavior_child_menus.md`).
3. **Blur & FlowWarp fragmentation:**
   - Masks have top-level `Add Blur`, `Add Curvature`, `Add Flow Warp · Targets This Mask` (`BuildMaskContextMenu` L1628–1650).
   - Effects have `Add Flow Warp · Targets This Effect` (`BuildEffectContextMenu` L1360).
   - Generators/Behaviors use `Feather` (`Blur | Eikonal`) and `Warp` (Behavior).

### Exact Changes
- **`Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerMenus.cpp`:**
  - In `BuildGeneratedContextMenu` (L1498–1506): Replace the `Masks` submenu on Behaviors/Generators with direct top-level items:
    - `Mask` (texture/gallery or default mask child)
    - `Noise Mask` (`EMixtormatChildCreation::NoiseMask`)
    - `Feather` / `Blur` (where supported by parent)
  - In `AddSharedChildMenuItems` (L417–421) and `BuildQuickControlsActionsMenu` (L208–234): Remove `Noise Gate` menu item; route noise mask creation through `Noise Mask`.
- **`Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerHierarchy.cpp`:**
  - L1015: Change `NOISE · GATE` label on scoped noise masks to consistent `NOISE` (or `STEERS` / `MASK` per parent role).

---

## Chunk 3A — Boolean Group & `IdGroup` Stack Uniformity (DONE — Badge & Unlock)

### Problem
1. **`Boolean` Group UX (`AgentDocs/BOOLEAN_STACK.md`):**
   - `FMixtormatBooleanStack::bLocked` defaults to `true`, which blocks dragging children out/in without unlocking first (`MixtormatLayerDragDrop.cpp` L733).
   - No `Group as Boolean` action when 2 compatible Generators are selected (`NEW_TASKS/03_shortcuts_and_multi_selection.md`).
   - Empty `TARGET` / `SOURCE` slots cannot be populated via simple drag-and-drop onto the Boolean parent row unless reparenting rules allow it.
2. **`IdGroup` Lack of Row Uniformity:**
   - `Boolean` puts its operation (`Union / Subtract / Intersect` + `Hard / Round / Bevel`) on the **stack row badge** (`MixtormatLayerBadges.cpp` L233, `BuildBooleanStackRowMenu` in `MixtormatLayerMenus.cpp` L40).
   - `IdGroup` returns an **empty badge** (`MixtormatLayerBadges.cpp` L221) and hides its `Operation` (`Difference / Pair / Max ID / Min ID`) and `Add Source` inside the Inspector (`MixtormatInspectorIds.cpp` L1879–1918).

### Exact Changes
- **`IdGroup` Row Badge (`MixtormatLayerBadges.cpp`, `MixtormatLayerHierarchy.cpp`, `MixtormatLayerMenus.cpp`, `MixtormatInspectorIds.cpp`):**
  - Return `DIFF`, `PAIR`, `MAX`, `MIN` in `MixtormatLayerBadges::ForChild` for `EMixtormatLayerChildType::IdGroup`.
  - Wire `.OnGetBadgeMenu` in `MixtormatLayerHierarchy.cpp` (L1736) for `IdGroup` to `BuildIdGroupFeatureMenu`.
  - Remove the `Operation` dropdown and `Add Source` dropdown from `BuildIdGroupControls()` in `MixtormatInspectorIds.cpp` (sources are already added via right-click `Add Source` on the `IdGroup` row in the stack or drag-and-drop).
- **Boolean Group Authoring (`MixtormatGeneratorTypes.h`, `MixtormatLayerDragDrop.cpp`, `MixtormatLayerActions.cpp`):**
  - Allow drag-reordering of `TARGET` and `SOURCE` children directly inside the Boolean parent in the stack (swapping their order swaps `TARGET` and `SOURCE` roles visually).
  - Support `Ctrl+G` / `Group` on 1 or 2 selected Generators to wrap them in an `EMixtormatLayerChildType::Boolean` parent container.

---

## Chunk 4A — Preview & Debug Shortcuts (`V`, `W`, `E`, `R` / `I`) (DONE)

### Problem
1. **`R` / `I` (Region IDs preview) is missing from the global input processor:**
   - `W` and `E` are handled in the workspace Slate input preprocessor (`SMixtormat.cpp` L142–154) AND in `FMixtormatPreviewViewportClient::InputKey` (`SMixtormatPreviewViewport.cpp` L459–479).
   - `R` / `I` is only in `SMixtormat::OnKeyDown` (`SMixtormat.cpp` L781–811). When focus is inside a child widget that consumes key down, `R` fails to fire.
   - Pressing `W` or `E` while `R` (`DebugPreviewMode::ChildOutput` with `RegionIds`) is active does not cleanly clear `DebugPreviewMode::ChildOutput` in `ToggleDiagnosticOverlayPreview` (`SMixtormat_Preview.cpp` L594), leaving conflicting preview states.
2. **`V` key never triggers `CycleSelectedModulePreview()`:**
   - `SMixtormat_Preview.cpp` (L618–746) implements `CycleSelectedModulePreview()` and binds it to `.OnCycleModulePreview` (L1313), which cycles the selected child's outputs when a module is selected and falls back to PBR channels when no module is selected.
   - However, `SMixtormatPreviewViewport.cpp` (L493–505) calls `Owner.CycleChannelPreview()` directly on `V`, completely bypassing `OnCycleModulePreview` — and `V` is not in the workspace input preprocessor (`SMixtormat.cpp` L140) so `V` only works when the 3D viewport has focus.

### Exact Changes
- **`Source/MixtormatEditor/Private/Widgets/SMixtormat.cpp` (Input Preprocessor, L142–155):**
  - Add bare `R` / `I` to the preprocessor (calling a shared `ToggleRegionIdPreview()` helper extracted from `OnKeyDown` L784–810).
  - Add bare `V` (and `Shift+V`) to the preprocessor so channel/module cycling works regardless of whether Layers, Inspector, or Viewport is focused.
- **`Source/MixtormatEditor/Private/Widgets/SMixtormat_Preview.cpp` (`ToggleDiagnosticOverlayPreview`, L592):**
  - When `W` or `E` activates `bMaskOverlayEnabled`, also clear `DebugPreviewMode = EMixtormatDebugPreviewMode::None` and `ChildPreviewTarget = FMixtormatChildPreviewTarget()` so `R` (Region IDs) and `W`/`E` swap cleanly just like `W` ↔ `E`.

---

## Chunk 4B — Allow Generators & Booleans on Any Layer (DONE)

Done: `FMixtormatLayer::HasGeneratorModules()` added; `Data.bGenerator` and `bGeneratorLayer`
now follow content rather than layer type; `bFill` stays tied to the layer type so a Material
layer with generators still composites as a Material layer. Editor gates relaxed in
`CanAddGeneratorModule`, `CanAddScopedFlowGenerator`, `CreateChild` (Behavior), `CanReadPublishedOutputAt`,
`ResolvePasteInsertIndex` and the Inspector's GENERATOR LAYER group.

### Problem
Generators, Behaviors, and Booleans are artificially gated to `Layer.Type == EMixtormatLayerType::Generator`, even though `FMixtormatLayer::Children` and `FLayerRenderData` are unified across all layer types.

### Exact Changes
1. **Editor Eligibility (`Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerChildren.cpp`):**
   - `CanAddGeneratorModule` (L1742) & `CanAddScopedFlowGenerator` (L1760): Remove `WorkingLayers[Target.LayerIndex].Type != EMixtormatLayerType::Generator` check so `Material` and `Fill` layers can add Generators and Booleans.
2. **Gather (`Source/MixtormatShaders/Private/`):**
   - `Compositing/MixtormatLayerGather.cpp` (L66): Set `Data.bGenerator = Layer.Type == EMixtormatLayerType::Generator || Layer.HasGeneratorModules()` (or check if any enabled `Generator`/`Boolean` child exists on the layer).
   - `Compositing/MixtormatGeneratorGather.cpp` (L25, L387, L707): Gate on `Data.bGenerator` (whether the layer has generator modules) rather than `Layer.Type == EMixtormatLayerType::Generator`.
   - `Compositing/MixtormatSourceGather.cpp` (L154, L170): Scan all enabled layers for generator/behavior dependencies, not only `EMixtormatLayerType::Generator`.
3. **GPU Pipeline (`Source/MixtormatShaders/Private/MixtormatGpuComposePipeline.cpp`):**
   - L986–1025: When a `Material` or `Fill` layer runs `AddGeneratorLayerPasses`, combine `LayerCtx.GeneratorBundle.Height` with the layer's base `LayerInputHeight` (or use it directly on a `Fill` layer) while publishing all generator Region IDs, Flow, and Masks to downstream children on that same layer.
