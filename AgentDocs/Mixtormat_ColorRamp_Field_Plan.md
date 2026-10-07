# Mixtormat — Color Ramp Field Plan

Companion to `Mixtormat_Field_Processing_Plan.md`. That plan covers turning
published Color into a real base-colour field (its Phase 4) and the field-registry
cleanup needed before Noise / Flow / UV (its Phase 10). This document covers what
the **Color Ramp module itself** must grow:

- a height source selector (running / specific module / layer / below-composite)
- a gate that can come from anywhere (mask chain, later a field reference)
- blend modes with a defined target
- placement under a specific generator module
- the shared contract that lets Flow, UV and ID fields feed ramps later

Not a node-graph redesign. The ordered child/module stack stays the model.

## Constraints

- No tests; do not add or recreate test infrastructure.
- Enums are serialized by value: **append, never reorder**. Prefer new fields
  over enum churn.
- Preserve serialized child/struct identity; display-name changes only.
- One source of truth per concern. No fallback paths, no duplicate compatibility
  layers (repo rule).
- Keep Color as RGB throughout. Do not route color through mask infrastructure.
- Do not build on `Bundle.NamedColors`; `PublishedFieldOutputs` is canonical.
- Ramp runs pre-composite (generator module contract). Do not try to make it
  read this layer's post-composite result.

---

## 1. Today — verified static audit

| Concern | File | Symbol | State |
|---|---|---|---|
| Payload | `Runtime/Public/MixtormatGeneratorTypes.h` | `FMixtormatGeneratorHeightColorRamp` (~L722) | `bEnabled`, `Ramp`, deprecated `OutputName`. No source, gate or blend |
| Child payload slot | `Runtime/Public/MixtormatLayerTypes.h` | `FMixtormatLayerChild::HeightColorRamp` (~L264) | Present |
| Gather | `Shaders/Private/Compositing/MixtormatGeneratorGather.cpp` | `GatherGeneratorHeightModuleChild` (~L371) | Packs ramp payload; `OutputName` hardcoded `"Color"` |
| Render data | `Shaders/Private/MixtormatGpuCompositorInternal.h` | `FGeneratorHeightColorRampRenderData` (~L985) | `OutputName`, `StopCount`, `Interpolation`, `Positions`, `Colors` |
| Shader class | `Shaders/Private/MixtormatGpuGeneratorPasses.cpp` | `FMixtormatGeneratorHeightColorRampCS` (~L734) | Binds stops + one height SRV + one colour UAV |
| Pass push | same file | `AddGeneratorHeightColorRampPass` (~L873) | Binds `RunningHeight` only |
| Call site | same file | `AddGeneratorLayerPasses` (~L2354) | Publishes `Color` field; preview hook (~L2365) |
| Shader | `Shaders/Private/MixtormatGeneratorHeightColorRamp.usf` | `MainCS` | `MixtormatEvalColorRamp(H, ...)`, 26 lines |
| Ramp eval | `Shaders/Private/MixtormatGeneratorHeightModules.ush` | `MixtormatEvalColorRamp` | Constant / Linear / Smooth |
| Capacity | `Shaders/Private/MixtormatColorRampCapacity.ush` | `MIXTORMAT_COLOR_RAMP_MAX_STOPS` | Shared CPU/GPU |
| Capabilities | `Editor/Private/Widgets/MixtormatChildCapabilities.cpp` | `GetChildCapabilities` (~L308) | `Color` output, previewable, copyable, not a mask |
| Inspector | `Editor/Private/Widgets/Inspector/MixtormatInspectorGenerators.cpp` | `BuildHeightColorRampControls` (~L986) | Ramp + presets + enable. No source/gate/blend |
| Placement | `Editor/Private/Widgets/Layers/MixtormatLayerChildren.cpp` | `CanAddGeneratorModule` (~L1476) | Requires `LayerType::Generator`, unscoped |
| Scope rule | `Runtime/Private/MixtormatChildScope.cpp` | `CanOwnScopedMasks` (~L38) | Effect / Generator / Generated / Craquelure / ColorId / RandomId — **not** the ramp |

### Established precedents to reuse

| Need | Precedent | Where |
|---|---|---|
| Source-module reference | `FMixtormatGeneratorHeightBlend::SourceLayerId` / `SourceChildId` → `SourceChildIndex` | `MixtormatGeneratorTypes.h` (~L614); render data in `MixtormatGpuCompositorInternal.h` (~L954) |
| Ordered source resolution | `MixtormatParameterBinding::ResolveEarlierSource` | `Runtime/Public/MixtormatParameterBinding.h` |
| Blend op enum | `EMixtormatGeneratorHeightOp` | `MixtormatGeneratorTypes.h` (~L600) |
| Scoped gate resolve | `HasScopedMasks` + `AddScopedFeatureMask` | `MixtormatGpuGeneratorPasses.cpp` (~L68, used ~L1325) |
| Below-layer accumulated height | `HeightTargets[1 - (LayerIndex & 1)]`, guarded by layer 0 | `MixtormatGpuSimulationPasses.cpp` (~L275, `SurfaceValid` ~L423) |
| Layer input height (pre-composite) | `LayerCtx.LayerInputHeight`; `AddLayerInputPass` runs before generators | `MixtormatGpuComposePipeline.cpp` (~L829, generators ~L836) |
| Published field kinds | `EMixtormatPublishedFieldKind` | `Runtime/Public/MixtormatOutputReference.h` (~L15) |

---

## 2. Possibilities

Ordered by value, cheapest first.

1. **Height source selector** — running chain (today) / a specific earlier
   module / layer input height / below-composite accumulated height. All four
   are readable where the ramp dispatches.
2. **Gate** — a scoped mask chain under the ramp (mask, generated, curvature,
   blur, craquelure, IDs). Gives "gate from anywhere" using existing nodes.
3. **Blend** — explicit op + amount against a defined target.
4. **Scope placement** — the ramp as a child of a specific generator module, so
   `Cliff Strata → Color Ramp` reads that generator explicitly rather than by
   chain order.
5. **Reference gate** — a scalar field reference (Scalar01 / signed) as the
   gate, from any publisher. Needs field-plan Phase 4 completeness work.
6. **Field-agnostic ramp** — same module, different scalar producer: Flow, UV,
   IDs. The ramp is already `scalar → RGB`; only the source plumbing is
   height-shaped.
7. **Ramp on non-generator layers** — deferred; see Open Decisions.

---

## 3. Blockers

| # | Blocker | Where | Fix required |
|---|---|---|---|
| B1 | Struct has no source / gate / blend fields | `FMixtormatGeneratorHeightColorRamp` | Add fields (source enum + ref ids, gate influence, blend op + amount) |
| B2 | Shader binds one SRV, no mask, no blend | `.usf` + `FParameters` | Add source selector, gate SRV + influence, blend inputs |
| B3 | `CanOwnScopedMasks` excludes the ramp | `MixtormatChildScope.cpp` | Add `HeightColorRamp`; editor + gather both read this |
| B4 | `CanAddGeneratorModule` rejects scoped placement | `MixtormatLayerChildren.cpp` | Relax for ramp under a Generator child |
| B5 | Ramp runs pre-composite | Generator contract | No composite-above read is possible; document and guard layer 0 for below-height |
| B6 | Colour field completeness | `FPublishedField::IsComplete()` | Needs an explicit `Color` case (field plan §9) — dependency for reference gates and prefix snapshots |
| B7 | Field kinds are append-only | `EMixtormatPublishedFieldKind` | Any new scalar kind appends only |
| B8 | Cache/key surfaces | compose hash + prefix cache | Audit that new fields participate in keys; stale prefix = wrong picture |
| B9 | IDs are `uint` fields | `LayerCtx.RegionIdMaps`, `PF_R32_UINT` | A separate binding path; see §5 ID semantics |
| B10 | Flow is `float2`, UV is `float2` | region/ID producer outputs | The ramp needs a scalar extraction step per source, not a new ramp |

---

## 4. Target contract — the should-be flow

```text
[Color Ramp module]
  Source ─┬─ GeneratorRunning      chain height (today's behaviour, default)
          ├─ ModuleRef              a specific earlier module's height
          ├─ LayerHeight            LayerInputHeight (exists pre-composite)
          ├─ CompositeBelow         accumulated height below this layer (guard layer 0)
          ├─ Flow          [future] Vector2 → scalar (length / angle / component)
          ├─ UVMap         [future] float2 → scalar (u / v / distance / angle)
          └─ RegionIds     [future] uint → scalar (stabilised id value or per-id hash)
      ↓
  Remap            optional normalize / range / balance / contrast
                   (reuse the signed remap payload when it lands — field plan Phase 5)
      ↓
  Ramp             scalar → RGB          (MixtormatEvalColorRamp, unchanged)
      ↓
  Gate             scoped mask chain: saturate(lerp(1, gate, MaskInfluence))
      ↓
  Blend            op + amount vs a defined target
                     · published Color field (today's contract)
                     · layer input albedo (only if a real input colour exists)
      ↓
  Publish Color    PublishedFieldOutputs → OutputReference(Color) → base colour
                   (field plan Phase 4 owns that half)
```

### Per-source semantics

| Source | Reads | Exists at dispatch | Notes |
|---|---|---|---|
| GeneratorRunning | chain height | yes | Today's input; keeps existing assets identical |
| ModuleRef | earlier module's signed height | yes | Restrict to earlier modules only (`ResolveEarlierSource` rule) |
| LayerHeight | layer input height | yes | `AddLayerInputPass` runs before generators |
| CompositeBelow | `HeightTargets[1 - (LayerIndex & 1)]` | yes | Layer 0 has none: gate off exactly like `SurfaceValid` |
| Flow | vector field | future | Length / angle / component; Flow is not a UV map |
| UVMap | absolute coordinates | future | Field is already at output resolution: do **not** re-apply the layer UV transform |
| RegionIds | id field | future | Needs a defined id → scalar rule; see Open Decisions |

---

## 5. Plan, in phases

Each phase is independently shippable and leaves existing assets identical.

### Phase A — Data + plumbing (height sources only)
- `FMixtormatGeneratorHeightColorRamp`: add `Source` (new appended enum:
  `Running / ModuleRef / LayerHeight / CompositeBelow`), `SourceLayerId`,
  `SourceChildId`, `SourceChannel` (reserved for Flow/UV extraction), and
  `SourceRange` semantics only if needed.
- `MixtormatGeneratorGather.cpp`: gather the enum + refs; resolve
  `SourceChildIndex` with the HeightBlend pattern; verify by texture presence,
  not by index (field plan §1 rule).
- `FGeneratorHeightColorRampRenderData`: carry the resolved source mode.
- `AddGeneratorHeightColorRampPass` / call site: bind the selected texture.
  Bind a valid dummy for RDG on every unused path.
- Inspector `BuildHeightColorRampControls`: a compact source dropdown.
- Keep the published field name `"Color"`; keep defaults = running chain.

### Phase B — Gate (scoped mask chain)
- `MixtormatChildScope.cpp` `CanOwnScopedMasks`: add `HeightColorRamp`.
- Add `MaskInfluence` to the payload + render data.
- Pass: `HasScopedMasks(Layer, SourceChildIndex)` → `AddScopedFeatureMask(...)`
  → bind as `Gate`; shader multiplies the ramped colour by
  `saturate(lerp(1, gate, influence))`.
- Inspector: reuse the standard scoped-mask vocabulary; no new widget.
- Acceptance: a Mask child under the ramp confines the colour exactly where
  the mask says, at influence 1.

### Phase C — Blend
- Add `ColorBlend` (new appended enum — Over / Multiply / Add / Screen /
  Difference as a first set) + `Amount` to the payload.
- Define the target explicitly in the shader: the module's own output (so
  Over is identity) versus the layer input albedo where a real input exists.
  Do not silently multiply two sources.
- Inspector: op dropdown + amount slider next to the ramp.
- Acceptance: Over at amount 0 is an exact pass-through of the ramped colour.

### Phase D — Scope under a generator module
- `MixtormatLayerChildren.cpp` `CanAddGeneratorModule`: allow a scoped ramp
  whose owner is a Generator child. Keep height Blender/Remap unscoped for now.
- `CanKeepScopedPlacement` / `CanAddScopedChild`: confirm the ramp is a legal
  scoped guest and owner; adjust only the ramp's row.
- Gather: `ScopeOwnerSourceChildIndex` is already resolved for modules;
  `ModuleRef` defaults to the owner when scoped.
- Acceptance: `Cliff Strata → Color Ramp` reads that generator's height with
  the ramp placed visually under it.

### Phase E — Reference gate (optional, after field-plan Phase 4)
- Accept a scalar field reference as the gate, resolved through
  `PublishedFieldOutputs` (Scalar01 / signed). Requires B6 first.
- One gate, one source of truth: a scoped mask chain **or** a reference, not
  both silently merging. The precedence rule goes in the doc and the tooltip.

### Phase F — Field-agnostic source contract
- Introduce one shared address for "scalar source":
  `{ FieldKind, OwnerLayerId, OwnerChildId, Channel/Mapping, bNormalize }`.
- Height sources become one instantiation of it. Flow / UV / IDs become more.
- Resist adding per-kind booleans to the ramp; that is the failure mode this
  phase exists to prevent.

### Phase G — Flow ramps
- Source extraction: length, angle, X, Y. Angle ramps must state their wrap
  convention (0..1 over a full turn).
- Flow work happens at output resolution; the ramp is a per-texel map.
- Do not chain Flow → UV implicitly; Flow is displacement, not coordinates.

### Phase H — UV ramps
- Source extraction: U, V, radial distance, angle.
- Field is already transformed: apply no second UV transform (field plan §12).
- Tiling/scale belongs to the field producer, not the ramp.

### Phase I — ID ramps
- Two distinct needs, plan both:
  1. **Colorize IDs** — per-id hash → colour. Not a ramp: a direct colour
     producer. Separate module; the ramp is the wrong tool.
  2. **Ramp over a scalar derived from IDs** — e.g. id normalised over the
     producer's id count, or a published per-id scalar (bed position, row
     index). This is the ramp's job.
- Do not overload `HeightColorRamp` with a uint input path. The shared address
  in Phase F is the seam; the binding path (`PF_R32_UINT`) is separate.

### Phase J — Ramp on non-generator layers (deferred)
- Only after A–I. An Effect-level ramp would run post-composite and could read
  the real composite; that is a different module with a different contract, not
  a flag on this one.

---

## 6. Open decisions

1. **Blend target.** Does the ramp ever write base colour directly, or only
   publish a field that a reference consumes? Field plan Phase 4 implies the
   latter is canonical; confirm before Phase C.
2. **ModuleRef vs scoped placement.** Both express "read that generator".
   Recommendation: scope placement is the authoring UI; `ModuleRef` is the data.
   A scoped ramp defaults its ref to its owner and the ref row is hidden.
3. **ID scalar rule.** Normalised id vs producer-defined per-id scalar. The
   second is more useful (bed position, row index) and already half-exists in
   generator bundles (`NamedMasks`). Prefer it; hash-colour stays in Colorize.
4. **Gate precedence** when both a scoped mask and a reference exist (§5 Phase E).
5. **`SourceChannel` name.** Reserved now so Flow/UV phases do not re-version
   the struct later.

---

## 7. Audit items to verify before coding (static)

- Does the compose hash / prefix cache key include the ramp's new fields?
  (`Shaders/Private/Compositing/MixtormatComposeHash.*`)
- Does `FPublishedField::IsComplete()` need `Color` before a reference-gated
  ramp can round-trip through prefix snapshots? (B6)
- Is `SourceChildIndex` for scoped ramps populated by
  `GatherGeneratorHeightModuleChild`, or only for top-level modules?
- Does `Copy Output` / clipboard treat a scoped ramp the same as a top-level
  one? (`Widgets/Layers/MixtormatLayerClipboard.cpp`)
- `HeightTargets` read-index convention on layer 0: confirm a black texture is
  bound, not an uninitialised one.

---

## Recommended order

```text
A (sources)      → B (gate) → C (blend) → D (scope under generator)
E (ref gate)     — after field-plan Phase 4
F (address)      → G (Flow) → H (UV) → I (IDs)
J                — deferred, separate module
```

A–C are self-contained and cover the original complaint: source choice, gating
and working blends, without touching the reference system.

## Reference files

- `AgentDocs/Mixtormat_Field_Processing_Plan.md` — Color as base field, published
  field completeness, signed remap payload, Flow/UV field semantics.
- `AgentDocs/COMPOSITION.md`, `AgentDocs/GENERATORS.md`, `AgentDocs/SHADERS.md`.
- `Runtime/Public/MixtormatGeneratorTypes.h`, `MixtormatLayerTypes.h`,
  `MixtormatOutputReference.h`, `MixtormatParameterBinding.h`,
  `MixtormatColorRamp.h`.
- `Runtime/Private/MixtormatChildScope.cpp`.
- `Shaders/Private/Compositing/MixtormatGeneratorGather.cpp`.
- `Shaders/Private/MixtormatGpuGeneratorPasses.cpp`,
  `MixtormatGpuCompositorInternal.h`, `MixtormatGpuSimulationPasses.cpp`,
  `MixtormatGpuComposePipeline.cpp`.
- `Shaders/Private/MixtormatGeneratorHeightColorRamp.usf`,
  `MixtormatGeneratorHeightModules.ush`, `MixtormatColorRampCapacity.ush`.
- `Editor/Private/Widgets/MixtormatChildCapabilities.cpp`,
  `Widgets/Inspector/MixtormatInspectorGenerators.cpp`,
  `Widgets/Layers/MixtormatLayerChildren.cpp`.
