I'll start by mapping the project structure to understand the Mixtormat implementation.

This is a large codebase. Let me read the core runtime ramp/field files and the docs in parallel.

Now let me examine the generator/effect structs and locate the Height Color Ramp, Height Curve, and Height Blend implementations.

Key finding already: `FPublishedField::IsComplete()` has `default: return false` — Color may not be recognized. Let me read the compositor internals and the generator structs.

I have enough to complete the audit. Here is the static-inspection report.

---

# Mixtormat Field-Module Audit

Scope: static source only. No edits, no builds, no Unreal. HEAD inspected in-tree.

---

## 1. Current Color Ramp pipeline and the exact missing Base Color step

**Full trace of `EMixtormatPublishedFieldKind::Color`:**

| Stage | Location | State |
|---|---|---|
| Runtime struct | `FMixtormatGeneratorHeightColorRamp` (`MixtormatMaterial.h` L3430–3454) | Has `bEnabled`, `Ramp`, deprecated `OutputName`. Only a ramp — no scalar-source field. |
| Gather | `GatherGeneratorHeightModuleChild` (`Compositing/MixtormatGeneratorGather.cpp` L333–349) | Builds `FGeneratorHeightColorRampRenderData`, forces `OutputName = "Color"`. |
| GPU pass | `AddGeneratorHeightColorRampPass` (`MixtormatGpuGeneratorPasses.cpp` L853+; shader `MixtormatGeneratorHeightColorRamp.usf`) | Samples running signed height → RGB ramp → `PF_FloatRGBA`. Correct. |
| Publication | `AddGeneratorLayerPasses` (L2281–2294) | Registers `FPublishedField{Kind=Color, Texture=Color}` **and** `Bundle.NamedColors`. |
| Capabilities | `MixtormatChildCapabilities.cpp` L240–248 | `Color` exposed as `bCopyableAsField=true`, `FieldKind=Color`. Correct. |
| Copy/reference creation | `CopyChildOutput` (`MixtormatLayerClipboard.cpp` L151–159) + `MakePublishedFieldReference` (`MixtormatLayerChildren.cpp` L299–325) | Creates an `OutputReference` child with `Kind=Color`. Correct. |
| Reference resolution | `ResolveEarlierSource` (`MixtormatOutputReference.cpp` L17–20) expects `OutputName=="Color"` | Correct. |
| Compositor consumption | `AddOutputReferencePasses` (`MixtormatGpuGeneratorPasses.cpp` L2188–2244) | **Broken here.** |
| Layer albedo | `MixtormatComposite.usf` L951 / L1051 | No color-field path exists. |
| Preview | `AddDebugPreviewColorBlitPass` + `IsChildOutputPreviewTarget` | Works. |

**Exact missing step — two independent blockers, both in `AddOutputReferencePasses` L2201–2216:**

1. **`FPublishedField::IsComplete()` returns false for `Color`.** `MixtormatGpuCompositorInternal.h` L234–250 handles `RegionIds`/`UVMap`/`Flow`; `Color` falls into `default: return false` (L247–248). So line L2202 (`!Source->IsComplete()`) **drops every Color reference before the copy branch runs.** The `Color` branch at L2212–2216 is therefore dead code even in principle.
2. **No consumer.** Even with `IsComplete` fixed, the Color branch (L2212–2216) only copies the field into the registry and `continue`s. It never stores it in `LayerCtx`, and the composite shader never reads it. Contrast `UVMap` (L2207–2211) which sets `LayerCtx.ReferencedUV`.

Secondary: because `IsComplete()` is false, `SavePrefixSnapshot` (L205–211) silently omits Color fields and the prefix-resume loop never restores them — a copied Color from a cached prefix layer would vanish, not just render wrong.

---

## 2. Recommended smallest Color → Albedo implementation

Keep it a field reference, not a new node type. Four small edits:

1. **`FPublishedField::IsComplete()`** — add `case Color: return Texture->Desc.Format == PF_FloatRGBA;`.
2. **`FMixtormatLayerPassContext`** (`MixtormatGpuCompositorInternal.h` L1464 area) — add `FRDGTextureRef ReferencedColor = nullptr;` and reset it in `BeginLayer` (L1536–1540), mirroring `ReferencedUV`.
3. **`AddOutputReferencePasses`** (L2212–2216) — set `LayerCtx.ReferencedColor = Field.Texture;` and continue (drop the no-op comment).
4. **Composite**: add `ReferencedColorEnabled` + `Texture2D<float4> ReferencedColorField` to `FMixtormatCompositeCS` (`MixtormatGpuCompositor.cpp` L250–255), bind in `AddLayerCompositePass` (L1163–1168 pattern), and in `MixtormatComposite.usf` use the sampled `.rgb` as the albedo where `SampledBaseColor`/`FillColor` is currently chosen (L951–957, L1051).

Notes:
- Do **not** route through `Mask`/grayscale; keep the RGBA texture intact. Do not touch `MixtormatEvalSignedScalarRamp`.
- Placement: apply the referenced color through the layer's own UV transform (same as `FillColor`), i.e. a per-pixel albedo override rather than a flat fill.
- This reuses the existing reference address/validation/UI unchanged. Nothing new is added to `EMixtormatPublishedFieldKind`.

---

## 3. Recommended Color Ramp UX

**Current widget infrastructure already present** (`SMixtormatColorRamp`):
- Selected/active point tracking: yes, but only transient (`DragPoint`, `HoverPoint` in the base).
- Point-specific color editing: only via **Ctrl-click → `OpenColorPicker`** (`SMixtormatColorRamp.cpp` L138–179). No persistent selection.
- Add/remove/move stops: yes (base editor L238–305).
- Interpolation: yes (toolbar).
- Preview updates: yes (`NotifyEdit`).

**Missing:** a persistent "selected stop" concept and an inline stop color editor.

**Recommended model (compact, scalable, no detached editor):**
- Promote a persistent `SelectedPoint` in the base editor (`SMixtormatRampEditorBase`), separate from `DragPoint`. Every paint already receives an "active" flag — reuse it for selection.
- Below the gradient bar, render a row of swatches (one per stop, in X order) using the same `ScalarRampPointSize` tokens. Clicking a swatch selects that stop; the selected swatch gets the accent border already used for active markers.
- Directly under the swatch row: one compact row of color controls (the existing `FLinearColor` RGBA sliders/label pattern used elsewhere in the inspector), bound to the selected stop. No popup.
- Keep Ctrl-click → picker as an optional accelerator. Remove the "picker is the only color path" dependency.
- Defer presets, histogram, and palette/perceptual modes (the last is a separate module, as you noted).

---

## 4. Exact cause of horizontal point-crossing limitation

Not one cause — the sorted-X assumption is **duplicated in three layers**, so crossing breaks all three:

1. **Drag clamp (per-widget).**
   - `SMixtormatScalarRamp::ApplyPointDrag` L98–102 clamps X to `[prev.X+0.001, next.X-0.001]` for interior points.
   - `SMixtormatColorRamp::ApplyPointDrag` L48–55 clamps X to neighbour bounds.
2. **Insertion clamp.**
   - `SMixtormatScalarRamp::InsertPointAt` L35–43 clamps the new X between neighbours.
   - `SMixtormatColorRamp::InsertPointAt` L26–32 does not clamp but relies on sorted scan.
3. **Struct sanitize + evaluation.**
   - `FMixtormatScalarRamp::Sanitize` (`MixtormatScalarRamp.cpp` L27–39): `Sort` by X, then force interior X between neighbours and pin endpoints.
   - `FMixtormatColorRamp::Sanitize` (`MixtormatColorRampMath.cpp` L17–21): `Sort` by X.
   - Evaluation (`SegmentAt` L49–56, `Evaluate` L59–118, GPU `MixtormatEvalSignedScalarRamp`) assumes the array is X-sorted and index-adjacent.

Because ordering is enforced in three places, removing any one clamp still leaves the others re-establishing order.

---

## 5. Best shared fix for point reordering

Fix once in the **base editor** (`SMixtormatRampEditorBase`), since it already owns `DragPoint` and the drag loop (`OnMouseMove` L286–290 calls `ApplyPointDrag(DragPoint, …)` then repaints).

Add a shared base helper, e.g. `ReconcilePointOrder(int32& InOutDragPoint)` that:
1. Removes the X clamp from both `ApplyPointDrag` overrides (let X move freely).
2. After the derived `ApplyPointDrag` writes X, re-sorts **by identity** (sort a parallel index array, not the payload array), then remaps `DragPoint` and `HoverPoint` to the dragged element's new index.
3. Leaves interpolation untouched — it already reads whatever sorted array it is given.

Consequences to handle in the same base change:
- **Selection follows drag**: remapping `DragPoint` in the base covers the drag; add the same remap to the new persistent `SelectedPoint` from item 3.
- **No duplicate X-order logic**: the derived `ApplyPointDrag` becomes "write X/Y only"; ordering lives in one place.
- **Endpoints**: keep endpoint pinning (scalar `Sanitize` L32–33, color `ApplyPointDrag` bounds) — endpoints do not cross, interior points do. Implement endpoint pinning in the base so both ramps share it.
- **`FinishPointDrag`/`EscapeArms`** (scalar, index-parallel arrays L109) must be permuted with the same remap or escape state drifts.

---

## 6. Recommended Height Curve → Height Remap design

**Current state:** `FMixtormatGeneratorHeightCurve` (`MixtormatMaterial.h` L3396–3425) is just `{bEnabled, FMixtormatScalarRamp Curve (-1..1, three identity points), Amount}`. The GPU pass (`MixtormatGeneratorHeightCurve.usf`) does a single signed ramp eval plus `lerp(H, Curved, Amount)`. UI is one Amount slider + the shared scalar ramp editor (inspector L855–901). It is not visually wrong in math, but it exposes **no** normalize/balance/contrast/offset/invert/pivot controls — it is only a curve.

**Recommendation: name it `Height Remap`, keep it usable on generator height first, structure it around an explicit signed-domain contract.** Smallest migration that avoids a rebuild:

- Author the remap as **input min/max + normalize toggle + balance + contrast + offset + invert + curve + amount**, all operating on a signed domain with **pivot 0** (not 0.5).
- Signed semantics: `invert = -x` about pivot 0; contrast about 0; offset is signed; zero stays neutral unless offset explicitly moves it.
- Keep the existing `FMixtormatScalarRamp` as the `Curve` stage — it is already domain-agnostic and `-1..1`-authored.
- Rename in UI/child type only when the struct changes; keep `EMixtormatParameterOwnerType::HeightCurve` value stable, add a new owner enum value only if needed for bindings.

Do **not** build a generic node graph; this stays one ordered sublayer with a fixed internal stage order.

---

## 7. What mask-shaping code can safely be shared

`FMixtormatMaskShaping` (`MixtormatMaskShaping.h`) and `MixtormatShapeMask` (`MixtormatMaskOps.ush` L69–86) contain: `bNormalizeInput`, `InputMin/Max`, `CurveBias` (a `FMixtormatScalarRamp`), `bInvert`, `Balance`, `Contrast`, `Offset`.

**Safe to share (structure/stage order):**
- The *sequence* normalize → levels → curve → contrast → offset → invert.
- The `CurveBias` reuse — it is already the shared scalar ramp struct, domain-agnostic.
- The UI row-building pattern (`AddMaskShapingRows`, inspector L1191–1258) as a template.

**Must remain separate (0..1 semantics):**
- `MixtormatShapeMask` L76: `saturate((Incoming-0.5)*Contrast + 0.5 + Offset)` — pivot **0.5**, `saturate` clamps. Signed remap needs pivot **0**, no saturate, signed offset.
- Balance power curve L79–83 (`pow`) is 0..1-only and asymmetric at zero.
- Invert L85 (`1-x`) vs signed `-x`.
- The mask `InputMin/Max` UI range is fixed `0..1`.

**Recommendation:** introduce a **lower-level signed scalar-remap helper** (`MixtormatRemapScalarSigned(...)` in a new shared `.ush`, plus a mirrored CPU struct/`FMixtormatScalarRemap`) beneath both systems. `MixtormatShapeMask` then calls it in 0..1 mode; Height Remap calls it in signed mode. Do **not** make `FMixtormatMaskShaping` itself the shared base — its field ranges and pivot are mask-specific and would corrupt signed output if reused blindly.

---

## 8. Height Blend compatibility assessment

`FMixtormatGeneratorHeightBlend` (`MixtormatMaterial.h` L3351–3394) + `AddGeneratorHeightBlendPass` + `MixtormatGeneratorHeightBlend.usf`:

- **Source selection**: `SourceChildId` resolved at gather (`MixtormatGeneratorGather.cpp` L301–314) against **earlier children in the same layer only**. `SourceLayerId` exists on the struct but is **never read** — it is serialization dead weight / a false affordance. This is the main coupling to "generator children."
- **Operation semantics**: shader is clean and complete; missing-source neutrality is explicitly per-op (L31–67). Good.
- **Signed height assumptions**: operates on signed `-1..1`-ish fields, uses `MixtormatHeightSmoothMax`/`MixtormatHeightBlend`. Semantics are signed already.
- **Missing-source behavior**: correct and documented.
- **Ordering**: lives in `Layer.Children` authored order, same as generators. Good.
- **Too tightly bound?** Mildly. It is a *scalar combiner* whose only generator-specific aspect is "resolve source to an earlier sibling index." The math is source-agnostic.

**Verdict:** it already fits a typed-scalar model well. It could later become a generic scalar combiner without behavior change — but **do not generalize now**. The only future-proofing worth doing cheaply is either removing the unused `SourceLayerId`, or (if you want cross-layer scalar fields later) wiring it to the field-reference resolver. Do not do the latter in this pass.

---

## 9. Roughness / material-channel recommendation (later)

Analysis only. Two viable models:

1. **Special-purpose modules** per channel — duplicates addressing, validation, UI, and preview per channel. Rejected.
2. **Generic scalar output interpreted by destination channel** — a scalar field published with a `Kind`, then the destination layer chooses which channel consumes it. This is the better fit.

**Recommendation:** extend the *existing* `OutputReference` mechanism so a destination layer picks the target channel, rather than adding new field kinds per material channel. Concretely, the destination-side consumption in item 2 already needs a `ReferencedColor`; the same slot can later be a `ReferencedScalar` bound to Roughness/Metallic/AO with a channel selector on the reference. The compositor already carries `RoughnessInfluence`, `AOInfluence`, `MetallicInfluence` per layer (`FLayerRenderData` L1081–1083) — the wiring point exists.

The current architecture suggests exactly option 2; option 1/3 would fight it. **No new enums now.** Roughness is not implemented.

---

## 10. Non-generator reuse feasibility

All three currently live under Generator layers, gated by `CanAddGeneratorModule` (`MixtormatLayerChildren.cpp` L1471–1476: layer must be `Type == Generator`, not a group, not scoped). Gather also early-outs on `!bGeneratorLayer` (`MixtormatGeneratorGather.cpp` L277).

| Module | Classification | Why |
|---|---|---|
| **Height/Scalar Remap** | **Easy to generalize now** | Pure scalar-in/scalar-out; no dependency on generator bundle. Only gate is the add-menu + `bGeneratorLayer` guard. Could run on any layer that has a scalar field. |
| **Color Ramp** | **Better to keep generator-only for now** | Its *producer* role (publishes a layer Color field) is only meaningful where a signed scalar source exists. Its *consumer* role (Base Color) is what matters next; that lives on the destination layer, not on the ramp. Keep producer generator-scoped until scalar fields are reusable. |
| **Scalar Blend (Height Blend)** | **Generator-only for now** | Source resolution is "earlier sibling in this layer"; making it layer-agnostic is the same work as cross-layer scalar fields. Bundle now, generalize with the scalar-field pass. |

Net: only the scalar remap is genuinely cheap to lift; the other two are blocked by the "scalar source" abstraction that does not exist yet, not by masking/UI.

---

## 11. Typed-field architecture risks to avoid now

Where the architecture is **already generic**: the address/kind/validation layer (`FMixtormatOutputReference`, `FPublishedFieldKey`, `ValidateDependency`, capabilities, copy/preview) is kind-driven and extends cleanly to new field kinds.

Where it is **still special-cased**:

- **`FPublishedField::IsComplete()` is a hardcoded format switch** (L234–250) with `default: false`. Every new kind must be added here or it silently vanishes. **Highest risk** — it is the current Color blocker and will bite `Scalar01`/`SDF`/`RegionIds`/`Vector2` identically.
- **Consumption is per-consumer ad-hoc**: `ReferencedUV` is a bespoke `LayerCtx` slot (L1464) rather than a typed field bag. Adding Color/Roughness/Scalar each means another slot + shader param. Risk of `LayerCtx` slot sprawl.
- **`FGeneratorBundle::NamedColors`/`NamedMasks` are `TMap<FName, FRDGTextureRef>`** (L1034–1037) with no type tag — name collisions across kinds are possible.
- **`ResolveEarlierSource` hardcodes the expected `OutputName` per kind** (L17–20: `"RegionIds"/"FlowDirection"/"WarpedUV"/"Color"`). New kinds must be inserted here too.
- **The `default: return false` in `IsComplete` and the name-switch in `ResolveEarlierSource` are two separate places** that must agree per kind. Future `ScalarSigned`/`Scalar01`/`SDF`/`RegionIds`/`Vector2` would be duplicated across both plus capabilities plus preview.

**Risks to avoid:** (a) don't add more untyped `LayerCtx` texture slots per channel — prefer one small typed reference set on the context that the composite reads; (b) don't let `NamedColors`/`NamedMasks` become the de-facto registry — the real one is `PublishedFieldOutputs`; (c) don't encode kind in `FName` strings; (d) don't add channel-specific enums before the destination-side consumer exists.

---

## 12. Files/functions likely to change in the implementation pass

**Color → Base Color**
- `Source/MixtormatShaders/Private/MixtormatGpuCompositorInternal.h` — `FPublishedField::IsComplete` (L234), `FMixtormatLayerPassContext` (L1439+), `BeginLayer` reset.
- `Source/MixtormatShaders/Private/MixtormatGpuGeneratorPasses.cpp` — `AddOutputReferencePasses` (L2188).
- `Source/MixtormatShaders/Private/MixtormatGpuCompositor.cpp` — `FMixtormatCompositeCS` params (L250), `AddLayerCompositePass` (L1163).
- `Shaders/Private/MixtormatComposite.usf` — albedo resolution (L951, L1051).

**Signed scalar remap / Height Remap**
- `Source/MixtormatRuntime/Public/MixtormatMaterial.h` — `FMixtormatGeneratorHeightCurve` (L3396).
- New shared helper (`.ush` + runtime struct) mirroring `MixtormatMaskOps.ush`/`MixtormatMaskShaping.h`.
- `Shaders/Private/MixtormatGeneratorHeightCurve.usf` (+ `MixtormatGeneratorHeightModules.ush`).
- `Source/MixtormatShaders/Private/MixtormatGpuGeneratorPasses.cpp` — `AddGeneratorHeightCurvePass`.
- `Source/MixtormatShaders/Private/Compositing/MixtormatGeneratorGather.cpp` — `GatherGeneratorHeightModuleChild` (L317–332).
- `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorGenerators.cpp` — `BuildHeightCurveControls` (L855).
- `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerBadges.cpp`, `MixtormatLayerChildren.cpp` — naming/labels.

**Shared ramp point reordering**
- `Source/MixtormatEditor/Private/UI/Controls/SMixtormatRampEditor.h` / `.cpp` (base: drag loop L286, new reconcile + selection).
- `Source/MixtormatEditor/Private/UI/Controls/SMixtormatScalarRamp.cpp` — `ApplyPointDrag` (L95), `InsertPointAt` (L35).
- `Source/MixtormatEditor/Private/UI/Controls/SMixtormatColorRamp.cpp` — `ApplyPointDrag` (L48), swatches/selection.
- `Source/MixtormatRuntime/Private/MixtormatScalarRamp.cpp` — `Sanitize` (L11).
- `Source/MixtormatRuntime/Private/MixtormatColorRampMath.cpp` — `Sanitize` (L5).

---

## Direct answers to the final-report list

1. **Color path / missing step:** pipeline publishes and copies correctly, but `AddOutputReferencePasses` drops the reference because `FPublishedField::IsComplete()` returns `false` for `Color`; and even past that, no `LayerCtx` slot or composite input consumes it.
2. **Smallest Color→Albedo:** add `Color` to `IsComplete` (PF_FloatRGBA), add `LayerCtx.ReferencedColor`, set it in the Color branch, bind it in the composite, sample it into albedo. Four edits, no new enums.
3. **Color Ramp UX:** persistent selected-stop in the base editor + inline swatch row + compact color controls under the gradient; keep Ctrl-click picker as accelerator.
4. **Crossing cause:** X-order is enforced in three duplicated places — both `ApplyPointDrag` clamps, both `InsertPointAt` clamps, and both `Sanitize` sort/interior-clamps; evaluation assumes sorted indices.
5. **Shared fix:** move ordering into `SMixtormatRampEditorBase` — free X, re-sort by identity, remap `DragPoint`/selection/escape-arrays, pin endpoints centrally.
6. **Height Remap:** rename/extend to an explicit signed remap (normalize, levels, balance, contrast@0, signed offset, invert=-x, curve, amount), keeping the existing scalar ramp as the curve stage; pivot 0.
7. **Shareable mask code:** the stage order, the `FMixtormatScalarRamp` curve stage, and the UI row pattern. Not shareable: 0.5 pivot, `saturate`, `pow` balance, `1-x` invert. Add a lower-level signed helper beneath both.
8. **Height Blend:** fits a typed-scalar model; math is source-agnostic. `SourceLayerId` is unused (same-layer sibling resolution only). Don't generalize yet.
9. **Roughness later:** generic scalar field interpreted by destination channel; reuse the OutputReference/`Referenced*` slot rather than per-channel enums. No code now.
10. **Non-generator reuse:** Scalar Remap easy now; Color Ramp and Scalar Blend better generator-only until scalar fields exist.
11. **Typed-field risks:** `IsComplete`'s `default:false`, the parallel `ResolveEarlierSource` name-switch, per-consumer untyped `LayerCtx` slots, and untyped `NamedColors/NamesMasks` maps.
12. **Files/functions:** listed in section 12 above.

No files were modified.