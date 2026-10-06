My line-numbering pipeline wiped out the code text, so I'll re-read that section with plain `sed`.Still auditing the code: I've traced the Color publish path and found where the per-layer flow runs. Next I'm checking how the layer's base color is composited.I've covered the compositor's Base Color path. Now I'm reading the shared ramp editor to find why points can't cross.I'm partway through the audit. The ramp-editor and Color path are mapped, and next I'm reading the mask-shaping code and the height-module GPU passes and gather.I'm partway through the audit and now checking how an output reference is gathered. After that come the layer Base Color fields and the inspector code.Almost done reading. I'm checking the design tokens, the widget's data sync and the reference-creation path, then I'll write the report.Last read: where "Copy Output" creates the reference and which gates keep these modules generator-only. Then I'll write the report.I've read the uploaded source statically, with no edits, patches, builds or tests. The unzip is in my scratch directory only. Line numbers refer to your files.

## 1. Current Color Ramp pipeline and the exact missing Base Color step

**Path today:**
- **Runtime struct:** `FMixtormatGeneratorHeightColorRamp` (`MixtormatMaterial.h:3430`) holds `bEnabled`, an `FMixtormatColorRamp` (up to 8 stops, Constant/Linear/Smooth) and a deprecated `OutputName`.
- **Gather:** `GatherGeneratorHeightModuleChild` (`MixtormatGeneratorGather.cpp:333`). It only runs for Generator layers, hard-codes the output name to `"Color"`, and builds the payload through `PrepareGpuPayload`, which sorts the stops and clamps colours to 0..1.
- **GPU pass:** `AddGeneratorHeightColorRampPass` (`MixtormatGpuGeneratorPasses.cpp:855`) reads the local `RunningHeight` and writes `PF_FloatRGBA`. The shader is `MixtormatGeneratorHeightColorRamp.usf`, calling `MixtormatEvalColorRamp`.
- **Publish:** `AddGeneratorLayerPasses` (`:2281-2294`) registers an `FPublishedField{Kind=Color}` under `{LayerId, SourceChildIndex, "Color"}` and does a preview blit. The `Bundle.NamedColors` write at `:2284` is never read, so it is dead.
- **Capabilities:** `MixtormatChildCapabilities.cpp:240-248` lists `Color` as copyable (`FieldKind=Color`). `:290-300` covers Color Reference children.
- **Reference creation:** `MixtormatLayerChildren.cpp:300-331` and `MixtormatLayerClipboard.cpp:113-158` create an `OutputReference` child.
- **Resolution:** `ResolveEarlierSource` (`MixtormatOutputReference.cpp:9-41`) maps Kind to the expected output name and requires a strictly earlier, enabled source layer. It never checks that the source child produces Color. `IdGather.cpp:101-126` gathers it and silently drops it inside ID-group scopes (`:109`).
- **Compose:** `AddOutputReferencePasses` (`:2188`) runs first in each layer (`ComposePipeline.cpp:821`).
- **Consumption:** none. The composite reads only `LayerBC` (or the resolved layer input) plus `FillColor`/`OverrideBaseColor`. `PublishedFieldKind::Color` is never read by the compositor.

**Missing steps. There are two blockers and then no consumer:**
1. `FPublishedField::IsComplete` (`CompositorInternal.h:234-250`) has no Color case, so it returns `false`. At `:2202` the Color reference is skipped (`!Source->IsComplete()` → `continue`), so it is never copied. The same check at `ComposePipeline.cpp:211` drops Color fields from prefix-cache snapshots.
2. Even when complete, the Color branch (`:2212-2216`) re-registers the field and `continue`s. Nothing is stored on `LayerCtx`, unlike `ReferencedUV` at `:2209`.
3. The composite has a side trap. `SampledBaseColor` is white unless `HasSurface || PreparedLayerMode==2` (`MixtormatComposite.usf:998`). Fill and Generator layers have no surface, so even a correctly resolved `LayerInputBC` would be ignored.
4. A Color Reference child's preview advertises a blit, but no pass exists for it.

## 2. Smallest Color → Albedo implementation

Mirror how `ReferencedUV` already works:
1. Add a `case Color: return Texture->Desc.Format == PF_FloatRGBA;` to `IsComplete`. This fixes both the copy and the cache.
2. Add `FRDGTextureRef ReferencedColor` to `FMixtormatLayerPassContext` and reset it in `BeginLayer` (`:1466`/`:1538`).
3. In the Color branch of `AddOutputReferencePasses`, set `LayerCtx.ReferencedColor = Field.Texture;` and add the preview blit.
4. In `FMixtormatLayerInputCS` and `ResolveLayerInputCS` (`MixtormatComposite.usf:915`), write `OutputBC = ReferencedColorEnabled ? float4(Field.Load(Pixel).rgb, SourceBC.a) : SourceBC`. Sample at output space, not through the layer's tiling. The field is Request.Resolution, like the height fields.
5. In `ComposePipeline.cpp` right after `AddOutputReferencePasses`, call `AddLayerInputPass` if `ReferencedColor` is set. The composite already switches to resolved-input mode when `LayerInputBC` exists (`GpuCompositor.cpp:795`). Order matters because `AddLayerInputPass` early-returns once it has run.
6. Add a `HasReferencedColor` composite parameter and change `:998` to `HasSurface || PreparedLayerMode==2 || HasReferencedColor`.
7. Override rule: I'd have the reference win over `bOverrideBaseColor` (zero it at bind time) and show "from reference" on the Fill Color row.

Nothing in this path touches height. New UPROPERTYs are hashed automatically, because `GpuCompositor.cpp:1538` hashes the whole `FMixtormatLayer` reflectively.

**Can Color Ramp stay height-specific?** Only the input is coupled: `RunningHeight` at `:2283`, the Generator-layer gate, the `-1..1` default stops, and naming. The struct, math, payload and shader are already domain-agnostic. Smallest path:
- Keep the child type enum value and struct name, since they are serialized.
- Rename the display string to "Color Ramp".
- Move the domain onto the module or ramp.
- Later, add an optional `Source` address where none means running height.

Scalar sources currently live in three places: the local `RunningHeight`, the layer-local `GeneratorModuleHeights`, and the global `PublishedMaskOutputs` (R32F). One resolver function over those three is enough. Don't unify them yet.

## 3. Recommended Color Ramp UX

**What exists:**
- No selected-point state. The base class tracks only `DragPoint` and `HoverPoint` indices.
- Per-stop colour editing is only Ctrl+click opening a floating `SColorPicker` (`ColorRamp.cpp:138-181`). The lambda captures an index, which goes stale after a reorder.
- Left-click empty space adds a stop (colour sampled from the ramp). Right-click removes interior stops only.
- Colour stops can be dragged in X, including endpoints.
- Interpolation is selectable (3 modes).
- The inspector copies the ramp into the struct and refreshes the preview on every move. Neither Height Curve nor Color Ramp wires `OnBegin/OnEndInteractiveEdit`, unlike the mask Curve Bias at `InspectorMasks.cpp:1238`.
- There is no histogram anywhere in the source, and no reusable inline colour swatch control.

**Recommendation:**
- Put `SelectedPoint` in `SMixtormatRampEditorBase`. Set it on mouse-down (hit or insert), keep it after mouse-up, and shift it on remove.
- Add a one-line `[swatch][RGB/hex][picker]` row inside `SMixtormatColorRamp` itself, directly under the ramp. The inspector keeps a single `AddSliderRow`.
- The stop handles under the gradient bar already are the swatches. Make the selected one an accent ring.
- Defer presets and histograms. The histogram needs a new reduce pass plus a readback, and I found no UI readback path.

**Stale-widget concern:** both ramp widgets read the `Ramp` attribute only in `Construct`, and `BuildInspectorPanel` is built once (`SMixtormat_Shell.cpp:457`). Selecting a second Height Curve or Color Ramp child may show the first one's ramp. Check this, and add an attribute re-sync while not dragging.

## 4. Exact cause of the point-crossing limitation

- **Colour:** `SMixtormatColorRamp::ApplyPointDrag` (`ColorRamp.cpp:48-55`) clamps X to `[prev+1e-4, next-1e-4]`, with a comment saying the order must never change mid-drag.
- **Scalar:** `ScalarRamp.cpp:98-102` does the same with ±0.001. `InsertPointAt` (`:35-43`) also clamps between neighbours. Endpoints are X-locked (`Index>0 && Index<Num-1`).
- **Why it is shaped that way:** identity is the array index.
  - `DragPoint` is an index.
  - The scalar ramp keeps a parallel `EscapeArms` array.
  - The picker lambda captures an index.
  - `HitPoint` (`RampEditor.cpp:108-117`) compares X only, ignoring Y, and prefers the highest index. Overlapping points cannot be told apart.
- **The data layer is not the cause.** Both `Sanitize()` functions sort. `Evaluate` and `PrepareGpuPayload` sanitize a copy, and the GPU assumes sorted input. The clamp is purely an editor policy.

## 5. Best shared fix

Fix it once in `SMixtormatRampEditorBase`:
- Drop the neighbour clamps. Clamp only to the domain (interior to `[Min+ε, Max-ε]` for scalar, endpoints stay locked slots).
- After each `ApplyPointDrag`, run one insertion-sort step for the dragged element. Swap it with its neighbour while out of order, via a virtual `SwapPoints(a,b)`. The scalar override must also swap `EscapeArms`. Then set `DragPoint = SelectedPoint = HoverPoint = newIndex`.
- The array stays sorted at every frame. Every existing sorted-array assumption remains valid: the `InsertPointAt` scans, middle-click insert and neighbour-lerp, `SamplePolyline`, and spline tangents.
- Keep a minimum gap. Zero-width segments (`H=1e-6`) can blow up the spline and B-spline math. When the cursor is within the gap of a neighbour, choose the side by the cursor's sign relative to that neighbour. Crossing is then continuous.
- Make `HitPoint` use Y for scalar (a virtual `GetPointY`) and fall back to nearest, then selected.
- Use `SwapPoints` and a virtual X-limit hook instead of the old neighbour clamps. `Sanitize` stays as the safety net.

## 6. Height Curve → Height Remap

**Current state:**
- The struct is `{bEnabled, ScalarRamp Curve (-1..1, identity), Amount}`.
- The shader is `MixtormatEvalSignedScalarRamp`, then `lerp(H, Curved, Amount)`.
- The input is silently clamped to the curve domain. Generator heights can exceed ±1 after Height Scale and after additive modules, so values beyond ±1 flatten.
- There is no input range, normalize, contrast, balance, offset or invert.

**Why the widget looks wrong:** `PaintGrid` (`RampEditor.cpp:141-159`) hard-codes 0..1 as the canonical Y range. It shades everything below Y=0 when `ViewMin<0` and above Y=1 when `ViewMax>1`. On a -1..1 signed view the entire negative half is shaded as "out of range". The Y=0 line is also drawn as a minor grid line while the X=0 line is emphasized (`:174-180`).

**Recommendation:**
- Name: "Height Remap" as the display name. Keep the enum value `HeightCurve` and the struct for serialization. Move the math into a source-agnostic `FMixtormatScalarRemap` embedded in the struct, so "Scalar Remap" later is only a new input binding.
- New fields default to identity, so old assets load unchanged:
  - **Input range:** piecewise-linear about 0 (`x≥0 → x/InputMax`, `x<0 → x/-InputMin`), default -1..1. "Normalize input" should use the existing signed max-abs mode of `MixtormatFieldRange.usf` (modes 1/2), never mode 0, which would move zero.
  - **Curve:** unchanged.
  - **Contrast:** `y*Contrast` about the pivot 0.
  - **Balance:** `sign(y)*pow(|y|, exp)`, reusing the existing exponent mapping `exp2(|Balance-0.5|*2*6)`.
  - **Offset:** `y+Offset` (signed, default 0).
  - **Invert:** `-y`, applied last.
- Files: `FGeneratorHeightCurveRenderData` (`CompositorInternal.h:941`), `MixtormatGeneratorGather.cpp:317-332`, `FMixtormatGeneratorHeightCurveCS` and `AddGeneratorHeightCurvePass` (`GeneratorPasses.cpp:696/828`), the shader, and `BuildHeightCurveControls` (`InspectorGenerators.cpp:855`).

**Widget visuals (D):**
- Pass the canonical Y range into the base class via virtuals and shade only outside it.
- Emphasize both zero lines with the same Major style.
- Use "nice" ticks anchored on 0. The current quarter lines of the view band are arbitrary after auto-zoom.
- Point size 7px is small. Go to 8-9px with a 2px accent ring for the selected point. Selection is currently shown only while dragging.
- Curve thickness 1.6 is fine, maybe 2.0. The area fill to the zero baseline already suits signed data.
- Draw locked-X scalar endpoints as a distinct marker.
- Padding of 8px keeps the endpoints visible.
- The toolbar is fine. Don't add a histogram unless a readback path appears.
- The tokens live at `MixtormatDesignTokens.h:401-411`.

## 7. What mask-shaping code can be shared

`FMixtormatMaskShaping` holds `bNormalizeInput`, `InputMin/Max` (0..1 levels), `CurveBias`, `bInvert`, `Balance`, `Contrast` and `Offset`. It is embedded by five mask owners.

**Safe to share:**
- `FMixtormatScalarRamp` and `PrepareGpuPayload`.
- `AddNormalizeFieldPasses`, using the signed modes.
- The balance exponent mapping.
- The field-name and UI-meta pattern, as a sibling struct.
- The processing order: normalize, levels, curve, contrast/balance, offset, invert.

**Must stay separate (0..1 semantics):**
- `MixtormatShapeMask` (`MaskOps.ush:69`) saturates and pivots at 0.5. Calling it on a signed field would destroy the negative half.
- `MixtormatEvalScalarRamp` saturates X.
- `MixtormatRemapMaskInput` and `MixtormatBlendShapedMask` saturate.
- `1-x` invert.
- The 0..1 UI ranges and `CurveBias`'s default domain.

**One lower-level thing is worth adding now.** There are already three divergent Constant-interpolation rules:
- Mask HLSL: `X>=1.0`.
- Signed HLSL: `InputX>=B.x`.
- CPU: `X>=DomainMax`.

One shared `MixtormatEvalRampPoints(X,…)` in a single `.ush` would unify them. The mask version would keep its own saturate and identity shortcut around it.

## 8. Height Blend compatibility

- **Fits well:**
  - The shader is two `Texture2D<float>` operands plus `HasSource`, so it is already a scalar combiner.
  - The operations are sign-agnostic.
  - Order is explicit, and the source must be an earlier child (`GeneratorGather.cpp:304-314`).
- **Real bug (missing source):** `HasSource` is set from `SourceChildIndex != INDEX_NONE` (`GeneratorPasses.cpp:810`). The operand falls back to `RunningHeight` when the module lookup fails (`:2266-2272`).
  - This happens when the source is disabled, has a null height (`:2320`), or is not a Generator child.
  - Then Add gives 2A, and Subtract and Difference give 0.
  - This contradicts the documented neutral-operand behavior (`MixtormatMaterial.h:32-34`).
  - Fix: set `HasSource` from whether the lookup succeeded. It is a two-line change.
- **Other findings:**
  - `SourceLayerId` is never read.
  - The menu lists only Generator children (`InspectorGenerators.cpp:742`).
  - "Multiply" never reads the source. It scales by `Scale`, which is intentional but not what a generic combiner means. Add a true A*B operation by appending to the enum.
- **Coupling is small.** It is source resolution (`GeneratorModuleHeights.Find`), the menu filter and the naming. Don't generalize yet. Later, replace the lookup with one `ResolveScalarOperand` shared by Blend, Remap and Color Ramp.
- I did not read `MixtormatHeightOps.ush` (`MixtormatHeightBlend`).

## 9. Roughness and material channels (later)

Pick option 2 with an explicit target on the consumer side. The compositor's native unit is the resolved layer-input set, and it already has per-channel influence, override flags and fill values. `ResolveLayerInputCS` writes BC, N, RAM and Height. A scalar or colour output reference should overwrite or modulate one channel of that set, exactly like Color → `BC.rgb`.
- Skip per-channel modules. They would differ only in destination.
- Skip a purely implicit "interpreted by destination" rule. A Scalar01 or ScalarSigned field needs an explicit mapping.
- Keep Drivers (`LayerGather.cpp:72-112`) separate. They modulate scalar parameters from masks and are not per-pixel field transport.

## 10. Non-generator reuse

- **Color Ramp:** easy to generalize now. The pass is scalar texture in, float4 out. The work is the input selector and removing the Generator gate (`GeneratorGather.cpp:277`). Its consumer side (Color → Base Color) already works on any layer.
- **Scalar/Height Remap:** better kept generator-only for now. The pass is pure, but there is no scalar input slot on other layers, so "remap what?" is the open question.
- **Scalar Blend:** better kept generator-only for now. Operands come from a layer-local map.
- **Architecturally blocked:** nothing.
  - **Real blockers:** the three scalar registries, no scalar kinds in `EMixtormatPublishedFieldKind` (`OutputReference.h:10`), and child-type gating in the menu, gather and dispatch.
  - **Cost of any new child type:** about 15 sites. Extend existing child types instead.

## 11. Typed-field risks to avoid now

**Already generic:**
- `FPublishedFieldKey`, shared with masks.
- `FPublishedField` with a `Kind`.
- The prefix-cache save and restore, which is generic by `Kind` once `IsComplete` is true.
- The data-driven capability descriptors.
- Demand tracking.

**Special-cased and risky:**
- `ResolveEarlierSource` hard-codes the Kind → name mapping in nested ternaries (`:17-25`).
- Name literals (`"RegionIds"`, `"FlowDirection"`, `"WarpedUV"`, `"Color"`) are scattered across files.
- `IsComplete` fails closed with no log. Adding a kind and forgetting a case is exactly what hid the Color bug.
- Signed versus 0..1 is encoded in output-name strings (`GeneratorPasses.cpp:2351`). Don't add `ScalarSigned` until a domain travels with the field.
- Each consumer kind owns a single `LayerCtx` slot with last-writer-wins. That is fine now, but don't turn it into a map yet.
- `EMixtormatPreviewOutputKind` and `EMixtormatPublishedFieldKind` are mapped by hand.
- Enums are serialized by value, so append only.
- Scoped non-RegionIds references are dropped silently.

## 12. Files and functions likely to change

**Color → Base Color:**
- `MixtormatGpuCompositorInternal.h`: `FPublishedField::IsComplete`, `FMixtormatLayerPassContext`.
- `MixtormatGpuGeneratorPasses.cpp`: `AddOutputReferencePasses`.
- `MixtormatGpuCompositor.cpp`: `FMixtormatLayerInputCS`, `AddLayerInputPass`, composite binding.
- `MixtormatGpuComposePipeline.cpp`: the call after `:821`.
- `MixtormatComposite.usf`: `ResolveLayerInputCS`, the `:998` and `:1054` handling.
- Optional hint on the Fill Color row (`MixtormatInspectorLayer.cpp`).

**Ramp UX and reordering:**
- `SMixtormatRampEditor.h/.cpp` (selection, reorder, hit-test, canonical shading).
- `SMixtormatColorRamp.h/.cpp`: stop row, drop the stale index lambda.
- `SMixtormatScalarRamp.h/.cpp`: `ApplyPointDrag`, `InsertPointAt`, `EscapeArms`, middle-click insert.
- `MixtormatInspectorGenerators.cpp:903` and `:855`: wire interactive-edit delegates.

**Height Remap:**
- Struct and gather: `MixtormatMaterial.h` (`FMixtormatGeneratorHeightCurve`), `MixtormatGeneratorGather.cpp:317`.
- GPU side: `CompositorInternal.h:941`, `GeneratorPasses.cpp:696/828`, `MixtormatGeneratorHeightCurve.usf`, `MixtormatGeneratorHeightModules.ush`.
- UI, plus the naming strings in `LayerMenus.cpp:1062`, `LayerChildren.cpp`, `LayerBadges.cpp`: `InspectorGenerators.cpp:855`.
- Check the `HeightCurve` owner registration in `ParameterBinding.cpp` (`:81`, `:180`, `:546`) if the new fields should be drivable.

**Height Blend fix:** `GeneratorPasses.cpp:798-825` and `:2263-2275`.

**Optional ramp-eval unification:** `MixtormatMaskShaping.ush`, `MixtormatGeneratorHeightModules.ush`, `MixtormatScalarRampMath.cpp`.

**Not verified:**
- Sampler and colour space for the Color field versus the Base Color target. `Layer.BaseColor` is a `FLinearColor`, so linear is the working assumption.
- The stale-widget behavior in section 3.
- The internals of `MixtormatHeightBlend` in `MixtormatHeightOps.ush`.