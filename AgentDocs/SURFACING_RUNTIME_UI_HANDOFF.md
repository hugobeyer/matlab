# Surfacing → Skin: Runtime / artist UI handoff

## Status

Runtime, artist UI, Skin compute production and ordered GPU dispatch are implemented in source.
Only targeted source review was performed: no commands, git, builds, tests, diagnostics,
additional agents or binary access. This is **not a compiled or end-to-end completion claim**.

The three central Editor registry hooks are implemented; **no parent edits are needed there**.
The native generator/Behavior gate-order validation gap is now closed in Runtime. Parent owns
gather/shared render-data and pipeline integration; another agent owns preview completion/master
selection. Their integration is not certified by this handoff. Future features remain in the
separate plan backlog, distinct from the implemented producer below.

## Shared contract

`Source/MixtormatRuntime/Public/MixtormatSurfacingTypes.h` is the canonical contract.
All agreed field names, types and numeric defaults are unchanged. `EMixtormatSurfacingType::Skin`
is separate from `EMixtormatGeneratorType`. `FMixtormatSurfacing` contains only `bEnabled`,
`Type`, and `Skin`. `FMixtormatLayerChild::Surfacing` is reflected and its child enum value
is appended after Boolean. Parameter owner `Surfacing = 32` is also append-only.

- Skin's numeric metadata, help and compiled defaults live on its UPROPERTYs.
- Both ramps use unit X domains. Roughness defaults are 0.55 → 0.45 → 0.4.
- `Direction` is degrees; `CurvatureRadius` is UV; RGB `MeanFreePath` is centimeters.
- Amount zero must preserve all incoming channels. HeightScale is signed and may be zero.
- Write toggles control composition, not whether diagnostic fields are declared.
- No new source/target sockets or inspector routing dropdowns were added.

`MixtormatSurfacing::GetOutputs(Type)` / `PublishesField(...)` own these names and kinds:

| Output | Kind | Gate consumption |
|---|---|---|
| Height | ScalarSigned | Not implicitly converted to coverage |
| Color | Color | Not coverage |
| Roughness | Scalar01 | Scoped mask |
| AO | Scalar01 | Scoped mask |
| F0 | Scalar01 | Scoped mask |
| Fuzz | Scalar01 | Scoped mask |
| Pores | Scalar01 | Scoped mask |
| Compression | Scalar01 | Scoped mask |
| SSS | Color (display format) | Preview-only physical MFP; no copying, references or albedo/coverage use |

No SDF, Flow, UV or Region IDs are advertised for Skin. The existing SignedDistance preview
colorist is used for Height; the actual published field remains **ScalarSigned**.

## Completed central Editor hooks

- `Widgets/MixtormatChildCapabilities.cpp` includes the adapter and dispatches Surfacing to
  `GetSurfacingCapabilities(Child.Surfacing)`. Height is primary; the other diagnostics are
  secondary. Unit outputs support typed Copy Output and Paste as Gating Mask. Instancing uses
  the existing capability. Output declarations do not prove that GPU textures exist yet.
- `Widgets/SMixtormat_Parameters.cpp` registers `OwnerTypeForChild` → `Surfacing`,
  `OwnerPointer` → `&Child.Surfacing.Skin`, and `PayloadStructForChild` →
  `FMixtormatSkin::StaticStruct()`. The explicit Surfacing include is present.
- `Widgets/SMixtormat_Layers.cpp::RebuildLayerList` retains Surfacing entries in
  `CollapsedGeneratorAddresses`; existing procedural hierarchy disclosure is reused.

### Preview-only physical diagnostic

`FMixtormatSurfacingOutput::bReferenceable` defaults true; the exact output name **`SSS`**
sets it false. Its label is **`Mean Free Path (cm)`**, and its preview/storage kind stays
**Color**. This does not make scattering distance an albedo input.

- `MixtormatSurfacing::PublishesField(...)` declares diagnostics, including SSS.
- `MixtormatSurfacing::CanReferenceField(...)` is the consumption eligibility API; it rejects SSS.
- The Editor adapter leaves SSS previewable but sets both `bCopyableAsField` and
  `bCopyableAsMask` false. The existing Copy Output and paste validation paths honor those flags.
- Runtime `ResolveSurfacingOutputSource` and `ResolveEarlierSource` use `CanReferenceField`,
  rejecting direct/imported SSS references too. Actual albedo remains the output named **`Color`**.
  Both paths call `IsSurfacingSourceAvailable` on effective, resolved input arrays; neither copies
  or re-resolves payloads. They preserve the requested output name: `(Color, Color)` is eligible;
  `(Color, SSS)`, unknown names and wrong kinds fail.
  `ValidateDependency`/`ResolveSource` delegate to the Surfacing resolver. Published masks request
  `Scalar01`, so neither albedo Color nor MFP can be consumed as coverage.
- Parent must publish/preview SSS directly, not resolve it through a generic field-reference
  consumer. No generic Color/albedo consumption is promised for physical MFP.

### Lifecycle and preview source review

- Creation uses `CanAddSurfacingModule`, reflected defaults, canonical selection and history.
- Layer removal already removes a complete subtree. Group removal now does the same for
  Surfacing, preserves surviving selection by GUID, synchronizes selection lanes and clears
  bypass when the selected child is removed.
- Enable routes through `IsChildEnabled` / `SetChildEnabled` to `Surfacing.bEnabled`.
  `RefreshLayeredPreview` records ordinary edits and synchronizes instances; history is not
  a second Skin-specific mechanism. Instance payloads inherit source enable like other modules.
- Whole-child copy/duplicate, instance payload resolution, scoped identity remapping and
  group expansion reuse the existing code. Presets register both Surfacing payload and subtype.
- `SMixtormat_Preview.cpp::IsSurfacingPreviewReady` reuses `IsPublishedSourceEnabled`, resolves
  instance payloads in a scratch container and checks `HasValidSurfacingScope`.
- Surfacing publication eligibility rejects disabled owners/ancestors, empty enabled group
  membership, missing/cyclic instance chains, wrong source type and forward instance placement.
  Both selected-output readiness and scalar overlay targets use that check; selected bypass
  also rejects Skin scalar overlays. Generator-only Flow lookup remains unchanged.
- Skin preview retains the **instance placement** identity, never redirects to its source's
  substrate. Shared group previews keep the existing first-enabled-member effective child ID.
- These are authoring eligibility checks, not GPU cache/texture readiness. Publication and
  bypass invalidation still require parent GPU integration.
- Added helper includes/declarations and descriptor aggregate field order were source-reviewed.
  Compilation/UHT, interaction and runtime undo have not been executed or verified.

## Implemented Skin production and ordered composition

- `MixtormatSkin.usf` implements periodic multiscale tissue lobes, irregular crossing creases,
  cellular pores, grain and curvature-guided principal directions with low-confidence stabilization.
  Curvature radius is destination UV; rectangular targets and placement/lifted-PreUV footprints
  are explicit compute math, without raster derivatives. Analysis uses one UAV; raw fields use eight.
- `AddSkinSurfacePasses(Ctx, LayerCtx, Layer, Child, SubstrateHeight, PreUV=nullptr)` returns
  `FSurfacingFields {Height, Color, RAM, Optics, FuzzColor, Subsurface, Pores, Compression}`.
  These fields are raw: HeightScale is applied to Height; Amount, masks and write flags are not.
- `ApplySurfacingFields(Ctx, LayerCtx, Child, Fields, Gate, RunningHeight, AncestorAmount=1)`
  applies Amount and each ancestry gate once, adds signed height and resolves enabled material
  channels. Zero Amount retains input pointers; zero gate preserves texels. Metallic and material
  alpha are preserved. Height disabled/HeightScale zero preserves the running height pointer.
- RAM is roughness/AO/metallic/F0; Optics is F0/fuzz amount/fuzz roughness/SSS amount.
  FuzzColor is linear tint; Subsurface is physical RGB MFP centimeters, never albedo.
- `AddGeneratorLayerPasses` dispatches Surfacing distinctly from Generator. Native generator
  Behaviors/normalization finish before owned Skin reads the substrate. Nested Skin scopes finish
  once; already-combined scoped Noise is not added again. Height-less owners retain Skin subtrees.
  BooleanOps handles Skin before its early exit; Boolean stack masks gate the height operation,
  not each input operand again. Optical fields remain ordered Skin writes, not Boolean operands.
- Scoped Color Ramps are captured at their position relative to Skin. Pending batches retain
  legacy reverse compositing order, flush before an active Skin color writer, and are consumed
  once. Zero-Amount Skin creates no color-order barrier. Existing Height Curve remains a flat
  running-height operation; this work does not introduce a general ordered post-op graph.
- `PublishSurfacingFields` publishes the nine exact names above and direct selected-preview blits.
  Height is the completed scope's contribution; material fields are completed channel snapshots.
  Pores/Compression are this node's Amount/gate-weighted diagnostics, independent of write flags.
  Raw fields are not globally published or cached. Full-precision MFP is retained.

## Closed native gate-order gap

`ResolveSurfacingOutputSource` now computes the consumer's execution boundary, not just its row.
Native Generator/Behavior/Noise gates, including mask/filter wrappers, use the **outer native
generator's start**. Skin inside that unfinished generator cannot feed its native phase even
when its row appears earlier. This restriction is independent of current write flags, so toggling
height does not turn a previously accepted link into an execution-order cycle.

Skin and Color Ramp consumers stop at their own ordered scope. Existing strict source ordering,
source-scope completion, self/ancestor/forward rejection and exact output typing still apply.
`ResolvePublishedMaskSource` and `ValidateDependency` call the same resolver: authoring and gather
can share the decision rather than inventing a GPU-only gate exception. No ChildScope API change.

Source-reviewed cases (not executed tests):

| Source → consumer | Result |
|---|---|
| Skin within a generator → that generator's native gate | Reject |
| Earlier Skin sibling → native Behavior/scoped Noise gate in the same generator | Reject |
| Skin under one nested Noise → native gate under another Noise in the same generator | Reject |
| Earlier completed root Skin/generator-with-Skin → later native generator gate | Allow |
| Earlier completed Skin sibling → later Skin or Color Ramp gate | Allow |
| Own/ancestor/incomplete/forward Skin → gate | Reject |
| Earlier-layer eligible Skin scalar → native gate | Allow |
| Skin `Color` as Color → valid earlier-layer Color reference | Allow |
| Skin `SSS` as Color, or either RGB field as a scalar gate | Reject |

## Shared Runtime source availability

```cpp
bool MixtormatOutputReferences::IsSurfacingSourceAvailable(
    const TArray<FMixtormatLayer>& EffectiveLayers, int32 LayerIndex, int32 ChildIndex);
```

Declared in `MixtormatOutputReference.h`, implemented in its `.cpp`. This is a read-only predicate,
not another payload resolver. Inputs are group-expanded, instance/binding-resolved layers retaining
placement IDs, declared placement Type, scope IDs and instance source links. It never calls
`ResolveChildInstances` or `CopyChildPayload`, mutates settings, or accepts retained payload as
proof that its source exists. An already-erased authored Type cannot be reconstructed; callers
must retain it when resolving payloads rather than copying a wrong-type source over the placement.

- Checks the selected Skin and the complete instance/source-ancestry graph, not just the terminal
  payload. Each hop requires matching child type and `ClassifyInstancePlacement == Valid`.
- Rejects missing/half-written/self/forward/cyclic links, ambiguous layer IDs, invalid/duplicate
  child IDs in reached containers, disabled source layers, nodes and ancestors, unsupported
  Surfacing subtypes and invalid scope ownership.
- Reuses `HasValidSurfacingScope`, `ResolveOwnerIndex`, `FindIndexById` and the existing instance
  placement classifier. Visited addresses are `(layer index, child index)`, not child GUID alone.
- Traverses ancestor instance chains too, including enclosing Surfacing and Generator/Boolean
  owners and their sources' ancestry. Parent gather calls this **once per Skin**; separate calls
  for each Surfacing ancestor are unnecessary.
- Both `ResolveSurfacingOutputSource` and `ResolveEarlierSource` use this API before accepting
  Surfacing fields. Exact named Color/SSS eligibility and native gate-order validation remain
  separate checks; this predicate alone does not certify reference ordering or GPU readiness.

Group mapping was source-reviewed in `BuildEffectiveLayers`: shared sibling instance links follow
the member-local clone, and external group links map to the existing representative member clone.
`SourceLayerId`/`SourceChildId` and scope IDs are remapped there; the API consumes those effective
addresses directly. Unexpanded/dangling authored group IDs fail closed—no second group lookup or
representative-selection fallback was added. Group-disabled members remain disabled sources.

Source-reviewed rejection cases include a stale enabled payload after source deletion, an
intermediate disabled/mistyped source, a source under a disabled ancestor, a cyclic/forward edge,
and a broken instance chain on an otherwise enabled Surfacing ancestor. No tests were executed.

## Parent integration contract / remaining integration review

These are required shared seams, not claims that another agent's pending changes are absent.

- Gather `Child.Surfacing` directly into `FChildRenderData.Surfacing` as agreed; do not map to Generator.
- Resolve instances and group controls/direct references before gathering the Skin payload.
- `Layer.HasGeneratorModules()` now recognizes Surfacing: this is the existing ordered dispatcher predicate.
- Call `MixtormatOutputReferences::IsSurfacingSourceAvailable(EffectiveLayers, LayerIndex, ChildIndex)`
  before Skin gather/publication. It covers source chains and all ancestor instance chains.
- Authoring allows layer roots, shared group roots, and children of Generator or Surfacing.
  Group expansion appends shared children after member-local children, with stable effective IDs.
- Invoke `AddGeneratorLayerPasses` for Surfacing-only layers too. It already owns ordered Skin
  dispatch, scope completion, typed publication and direct blits; do not dispatch Skin again.
- Preserve A/B identity at Amount zero, zero coverage and disabled channels. Do not overwrite Skin
  material outputs with Fill defaults or the old `bGeneratorAlbedo` / last Color Ramp policy.
- Use existing scoped mask gathering: Texture, Noise, Layer Values and eligible published outputs
  remain ordinary Mask children. No duplicate input-gate storage was introduced.
- `MixtormatOutputReferences::ResolveSurfacingOutputSource` checks exact output kind/name,
  source enable/ancestors, unique source identities, completion order and feedback rejection.
  `ResolvePublishedMaskSource` uses it for Surfacing scalar gates; `ValidateDependency` uses it
  for referenceable typed Surfacing outputs. **SSS is deliberately rejected by these consumers.**
- Parent mask execution must read Scalar01 fields as coverage without guessing names or channels.
  Publish `SSS` under its exact name for direct diagnostic preview only; do not coerce it to
  `Color` or use it as layer albedo.
- Use the shared availability API rather than trusting `ResolveChildInstances`: that resolver
  preserves stale payloads on broken chains and can copy a wrong type. Retain declared placement
  type/instance identity in effective arrays. Rejected Skin must not gather or publish outputs.
- Sanitize gather with `MixtormatParameterContracts::{SanitizeFloat,SanitizeInt32}` and the
  Surfacing owner. The runtime table supplies semantic bounds and reflected non-finite defaults.
  Add pass-specific period budgets/divisor guards there if required; do not silently rename fields.
- Sanitize ramps and physical optical data in gather; keep MFP in centimeters and no display transform.
- Initialize `LayerInputBC`, `LayerInputRAM`, `LayerInputOptics`, `LayerInputFuzzColor` and
  `LayerInputSubsurface` to effective inputs before dispatch, including Fill/overrides/references.
  Physical MFP must retain RGBA32F storage; `FPublishedField::IsComplete` must accept that for Color.
- Reset and consume `bSurfacingColor`, `bSurfacingRoughness`, `bSurfacingAO`, `bSurfacingF0`,
  `bSurfacingFuzz`, `bSurfacingSSS`. These indicate resolved channel ownership, not occupancy.
  Do not reapply Fill overrides, Skin Amount or scoped masks in the final compositor.
- Restore all nine already-published diagnostic fields on cache hits and retain optical inputs.
  Include Skin, masks, scope, ramps, write flags, inputs and upstream state in dependency hashes.
  Skin has no separate local node-cache entry; shared cache lifecycle remains parent-owned.
- Integrate physical F0, spatial fuzz amount/color/roughness, and SSS with the material agent's
  shared optical outputs. `Fuzz` is the scalar diagnostic only, not the full optical bundle.
- Connect diagnostic selection/bypass and cache readiness in the parent's preview integration.
- `MixtormatShaderParamScanner` assigns `MixtormatSkin.usf` tags to owner Surfacing; change that
  filename mapping if the actual shader is named differently. No scanner was run.

## Deliberately unsupported / remaining issues

1. **Behavior-on-Surfacing is not enabled.** Existing `ResolveBehaviorGeneratorIndex`,
   `ValidateBehaviorInputs` and GPU Behavior indexing require Generator payloads. Extending only
   a predicate would read the wrong payload. Add a producer-capability adapter plus real GPU
   Warp/Deform/Push/Carve/Feather semantics before enabling placement/menus. Skin shows disabled
   Behavior and Blur actions with reasons, not working-looking operations.
2. Surfacing-local Blur, Color Ramp post-ops, nested Flow and Boolean operand conversion are not
   advertised. Upstream Generator operations and layer-output Blur remain existing workflows.
3. `FLinearColor` is not a supported `EMixtormatParameterValueType` in the current binding system.
   FuzzColor and MeanFreePath are serialized, editable, copied/instanced and preset-owned, but
   they cannot be exposed as typed group controls or direct parameter references. All supported
   numeric/bool/ramp Skin fields use the canonical owner. MFP gets three numeric cm rows rather
   than a misleading color picker. Extending the shared color-value model needs coordinated
   Runtime + Editor work, not a Skin-only fallback or an unagreed contract rename.
4. No runtime-independent detail texture system is claimed. DetailTiling repeats baked microdetail.
5. Rendering, bake/reimport, optical master support, seam quality and runtime undo interactions
   remain unverified until the user's separately authorized build/runtime checks.

## Implemented file list

Paths are relative to `Mixtormat/`.

### Runtime

- `Source/MixtormatRuntime/Public/MixtormatSurfacingTypes.h` — new reflected contract and output API.
- `Source/MixtormatRuntime/Private/MixtormatSurfacingTypes.cpp` — canonical typed outputs.
- `Source/MixtormatRuntime/Public/MixtormatLayerTypes.h` — appended category/payload and ordered predicate.
- `Source/MixtormatRuntime/Public/MixtormatParameterTypes.h` — appended parameter owner.
- `Source/MixtormatRuntime/Private/MixtormatParameterBinding.cpp` — flat Skin owner data/struct binding.
- `Source/MixtormatRuntime/Private/MixtormatParameterDefinition.cpp` — defaults and semantic contracts.
- `Source/MixtormatRuntime/Public/MixtormatChildScope.h` — Surfacing scope API.
- `Source/MixtormatRuntime/Private/MixtormatChildScope.cpp` — mask ownership and fail-closed scopes.
- `Source/MixtormatRuntime/Private/MixtormatLayerGroups.cpp` — fail-closed shared Surfacing scope expansion.
- `Source/MixtormatRuntime/Public/MixtormatOutputReference.h` — typed resolution and read-only availability API.
- `Source/MixtormatRuntime/Private/MixtormatOutputReference.cpp` — full instance/ancestry availability,
  typed/name/order/coverage checks and native gate-order rejection.

### GPU production and dispatch

- `Shaders/Private/MixtormatSkin.usf` — curvature analysis and eight raw periodic Skin fields.
- `Shaders/Private/MixtormatSurfacingApply.usf` — gate product, material/height apply, diagnostics.
- `Source/MixtormatShaders/Private/MixtormatGpuSurfacingPasses.h` — interfaces and shared integration contract.
- `Source/MixtormatShaders/Private/MixtormatGpuSurfacingPasses.cpp` — RDG production/apply/publication.
- `Source/MixtormatShaders/Private/MixtormatGpuGeneratorPasses.cpp` — ordered scope dispatch and ramp barriers.

### Editor authoring

- `Source/MixtormatEditor/Private/Widgets/SMixtormat.h` — creation/getters/menu/panel declarations.
- `Source/MixtormatEditor/Private/Widgets/SMixtormat_Inspector.cpp` — dedicated child inspector mount.
- `Source/MixtormatEditor/Private/Widgets/MixtormatChildCapabilities.cpp` — central Surfacing registration.
- `Source/MixtormatEditor/Private/Widgets/SMixtormat_Parameters.cpp` — three canonical owner/address hooks.
- `Source/MixtormatEditor/Private/Widgets/SMixtormat_Layers.cpp` — preserve Surfacing collapse state.
- `Source/MixtormatEditor/Private/Widgets/SMixtormat_Preview.cpp` — authoring/instance/scope preview eligibility.
- `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorSurfacing.cpp` — new shared cards/controls.
- `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerChildren.cpp` — creation defaults, selection,
  naming, enable, scopes and typed reference/gate validation.
- `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerActions.cpp` — creation guard/history and removal.
- `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerHierarchy.cpp` — glyph/procedural child row.
- `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerMenus.cpp` — distinct Surfacing → Skin menus.
- `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatSurfacingCapabilities.h` — central-registry adapter.
- `Source/MixtormatEditor/Private/Services/MixtormatPresetStoreDocuments.cpp` — payload/subtype presets.
- `Source/MixtormatEditor/Private/UI/Parameters/MixtormatShaderParamScanner.cpp` — owner metadata mapping.

### Icon and documentation

- `Source/MixtormatEditor/Private/UI/Atoms/MixtormatIcons.h` — Surfacing symbol.
- `Source/MixtormatEditor/Private/UI/Atoms/MixtormatIcons.cpp` — dedicated brush lookup.
- `Source/MixtormatEditor/Private/Style/MixtormatStyle.cpp` — SVG brush registration.
- `Resources/Icons/surfacing.svg` — exact copy of generator.svg, ready for user replacement.
- `AgentDocs/SURFACING_RUNTIME_UI_HANDOFF.md` — this handoff.

Generic whole-payload copy/instance, subtree identity remapping, group expansion, preset JSON and
history snapshots were reused rather than adding a second Surfacing serialization mechanism.
The existing layer clipboard implementation did not require changes.
