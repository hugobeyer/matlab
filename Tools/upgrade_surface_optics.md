# Shared optical master and bake integration (UE 5.8)

**Source-only implementation. Not executed, built, tested or visually validated here.**
The user runs the build, bake and Python inside Unreal Editor. No binary Content assets were
inspected or modified during implementation.

One shared material contract serves all layers, generators, effects and Surfacing. There is no
Skin-specific material, renderer, profile or shader path in this integration.

## Channel / asset contract

| GPU getter / input to bake | Linear payload | Surface field | Recipe field | Export suffix |
| --- | --- | --- | --- | --- |
| `GetOpticsOutput()` / `OpticsTarget` | R dielectric F0, G fuzz amount, B fuzz roughness, A SSS amount | `Optics` | `BakedOptics` | `_Optics` |
| `GetFuzzColorOutput()` / `FuzzColorTarget` | RGB fuzz tint, A reserved | `FuzzColorMap` | `BakedFuzzColorMap` | `_FuzzColor` |
| `GetSubsurfaceOutput()` / `SubsurfaceTarget` | RGB mean-free-path in **centimeters**, A reserved | `Subsurface` | `BakedSubsurface` | `_Subsurface` |

- The three new `UMixtormatSurface` fields are `TSoftObjectPtr<UTexture2D>`.
- `DefaultIOR` is unchanged, including when updating an existing baked surface.
- Optics components are in [0,1]. Tint/MFP must be finite and nonnegative, with no upper clamp.
- Tint and MFP are **not premultiplied** by their amounts. Black/zero are valid values.
- Canonical compositor F0 remains `RAM.a`; `Optics.r` mirrors it, not a second authored F0.
- Live `DA_RAMH` and baked `T_<name>_RAM` retain RGB roughness/AO/metallic and A F0.
- Layer-facing `T_<name>_RAMH` still repacks **height into alpha**; it never contains SSS thickness.
- Existing BC sRGB conversion, Normal, G16 Height and RAMH high-byte height packing are unchanged.
- Authored optical surface reimport preserves per-pixel F0 in `Optics`, independently of RAMH height.
- Without authored optics, Surface's three optical refs are cleared, not populated with allocation-only
  defaults. All three standalone exports and recipe `Baked*` refs are still written for inspection.

### Readback and storage

`ReadLinearTarget()` uses `ReadLinearColorPixels`, `RCM_MinMax` and `SetLinearToGamma(false)`.
Bake requires equally sized floating RGBA optical targets (`PF_FloatRGBA` or
`PF_A32B32G32R32F`). It checks pixel counts, finite/nonnegative values, Optics unit ranges and
`Optics.r == RAM.a` within `KINDA_SMALL_NUMBER` before creating outputs.

All three optical textures use `TSF_RGBA32F`, `TC_HDR_F32`, `SRGB=false`, retained alpha,
non-virtual sampling and simple-average mips without alpha-coverage rescaling. There is no
8-bit intermediary, UNorm range rescaling or BC6H alpha loss. This preserves the readback
precision, not precision already lost in a half-float GPU target. Cooked-platform format
support and memory cost still need user verification. These are Unreal texture assets,
not EXR/PNG files written to disk.

`FMixtormatBakeResult` exposes `Optics`, `FuzzColorMap`, `Subsurface`, and `Warnings`.
Success also requires RAMH and Surface. All new assets participate in collision/type checks,
created/updated path reporting, recipe references and saving.

## Integration status and parent contract

### Completed in owned Editor source (not executed)

- The sole service caller, `Widgets/SMixtormat_Document.cpp::ExecuteBake`, now retrieves and
  null-checks all seven targets before passing the three optical references after Height.
  Initial Bake and Re-bake share this path. Failure still restores preview resolution.
- After composition, the caller fills `FMixtormatBakeSettings::bHasAuthoredSurfaceOptics` from
  `SMixtormatPreviewViewport::HasCompositedSurfaceOptics()`. This is transient result metadata,
  default false, not a user setting. It is recomputed for each bake, not persisted/reused on Re-bake.
- Bake completion/failure UI includes `Result.Warnings`; counts and Content Browser reveal include
  all ten outputs. The bake settings dialog labels the three added textures and correct RAM alpha.
- `FMixtormatBakeService::HasSurfaceOpticsParameters()` is shared by bake and lit preview.
  It enumerates the selected material's underlying graph declarations, not MID override values.
  All three texture names plus the scalar enable must exist as **global** parameters.

```cpp
FMixtormatBakeService::Bake(
    Recipe, BaseColorTarget, NormalTarget, RAMTarget, HeightTarget,
    OpticsTarget, FuzzColorTarget, SubsurfaceTarget, ComposedSettings, Progress);
```

The viewport provides `GetCompositedOptics()`, `GetCompositedFuzzColor()`,
`GetCompositedSubsurface()` and `bool HasCompositedSurfaceOptics() const`.
The latter requires the compositor's authored-state flag AND all three targets, independently
of whether the current preview master can render them.

### Implemented in owned GPU source (not executed)

`FMixtormatGpuCompositor` now implements:

```cpp
UTextureRenderTarget2D* GetOpticsOutput() const;
UTextureRenderTarget2D* GetFuzzColorOutput() const;
UTextureRenderTarget2D* GetSubsurfaceOutput() const;
bool HasSurfaceOpticsOutput() const;
```

- Ping-pong `FTargetSet` now owns Optics (`PF_FloatRGBA`), FuzzColor and Subsurface
  (`PF_A32B32G32R32F`). Both optical halves are initialized for every request, including empty and
  legacy stacks. Linear transport defaults live in `MixtormatSurfaceOptics` in the public header:
  Optics=(.04,0,.5,0), FuzzColor=(1,1,1,0), Subsurface=(0,0,0,0). These do not activate optics.
- Target generations pin all three RT pairs. Surface soft maps load as a complete bundle and are
  pinned through submission; partial/unloadable/sRGB/virtual bundles fail explicitly. Their texels
  must follow the channel contract; allocation or successful loading does not validate pixel values.
- References resolve all three RHI targets and authored metadata from the exact successful child
  generation on the render thread. Source RAMH height never becomes F0: imported F0 comes from
  Optics.r unless IOR is overridden; live reference F0 comes from canonical RAM.a.
- `ResolveLayerInputCS` resolves all seven channels through the same source UV/region placement.
  Ordered Surfacing starts with effective Fill/override BC/RAM/F0; final composition does not
  overwrite those resolved values with Fill again. HSV/region tint/Grade remain layer finishing.
  Pass-through copies private inputs, including optics, without adding occupancy. Normal-only
  composition preserves underlying optics and occupancy.
- The material composite writes its exact final `Alpha` into a float32 coverage texture (seventh
  UAV). `CompositeOpticsCS` consumes that texture, not an independently reconstructed mask.
  Height competition, placement, effect keep, opacity and feature masks therefore agree. Prepared
  local materials use coverage=1 before relief; their later merge applies actual coverage once.
  Tint/MFP remain straight physical values, not multiplied by their amount for storage.
- Root Flow Warp reuses the existing spatial kernel through its float4 slots for optical channels.
  Every dispatch receives the original height, preserving identical height-selection and mask
  decisions. This adds two dispatches; it does not duplicate or approximate warp mathematics.
- Prepared-layer routing, prefix save/restore/demand checks and byte estimates carry all three maps.
  Executed optical writes/source presence accumulate authored metadata and cache it with the prefix.
  F0-only Surfacing counts when its layer F0 influence is positive. Disabled/zero-opacity/normal-only
  layers do not introduce authored optics; zero-valued authored fuzz/SSS still count.
- `HasSurfaceOpticsOutput()` is **completed-current-generation metadata**, false during a new
  request, after failure/resize or without authored data. It tracks authored contributions in the
  enabled graph, not a GPU visibility reduction: all-zero masks or later full occlusion do not
  erase authorship. This distinction avoids inferring authoring from pixel values.
- Finalization mirrors RAM.a into Optics.r after layer filters and final filters. Quarter-turn
  rotation uses the same exact integer texel mapping as BC/RAM/Height. Outputs transition to SRVs.
- `BindOutputs` checks the selected underlying material's actual global declarations. It binds the
  complete optical bundle only for a complete contract, enables only completed authored data, and
  leaves legacy fuzz bindings to the caller. Parameter presence still does not certify graph wiring.

### Parent integration still required before building

This pass deliberately does **not** modify the internal header, gather files or Editor files.
The following exact members are consumed by the owned GPU implementation:

```cpp
// FMixtormatComposeResources
FTextureRenderTargetResource* Optics[2] = {};
FTextureRenderTargetResource* FuzzColor[2] = {};
FTextureRenderTargetResource* Subsurface[2] = {};
TArray<TStrongObjectPtr<UTexture2D>> TexturePins;
bool bHasSurfaceOptics = false; // render-thread write; InFlight handoff before game-thread read

// FRenderRequest: FTextureRHIRef; FMixtormatComposeContext: FRDGTextureRef
// Same names/types/default initialization as the existing OutputBC[2] arrays:
OutputOptics[2]; OutputFuzzColor[2]; OutputSubsurface[2];

// FMixtormatComposeContext
bool bHasSurfaceOptics = false;

// FLayerRenderData
FTextureRHIRef Optics, FuzzColor, Subsurface;
bool bHasSurfaceOptics = false;

// FMixtormatLayerPassContext (reset each BeginLayer)
FRDGTextureRef LayerInputOptics = nullptr, LayerInputFuzzColor = nullptr, LayerInputSubsurface = nullptr;
FRDGTextureRef BelowOptics = nullptr, BelowFuzzColor = nullptr, BelowSubsurface = nullptr;
bool bSurfacingColor = false, bSurfacingRoughness = false, bSurfacingAO = false;
bool bSurfacingF0 = false, bSurfacingFuzz = false, bSurfacingSSS = false;

// FMixtormatPrefixCache::FEntry
TRefCountPtr<IPooledRenderTarget> Optics, FuzzColor, Subsurface;
bool bHasSurfaceOptics = false;
```

- Internal prefix `Store()` byte accounting must include all three optical pooled targets.
  The pipeline's pre-save estimate already includes them. Keep formats exact on restore.
- `FChildRenderData::Surfacing` must hold resolved `FMixtormatSurfacing`. Parent must insert its
  gather dispatch in `RequestComposeInternal` **after this pass hands back the compositor file**.
  Disabled ancestors, scope ownership, sanitized payloads and source cache identity remain gather's
  responsibility. `HasGeneratorModules()` currently includes Surfacing; the pipeline skips it in
  the later effect loop and invokes the existing ordered generator/Surfacing dispatcher.
- `FPublishedField::IsComplete()` must accept Color32 (`PF_A32B32G32R32F`) for physical SSS
  publications. SSS stays preview-only RGB cm, never Color/albedo input or signed Height.
- **Async preview completed in source:** `ComposeLayersWithDebug` now supplies a weak Slate
  `CreateSP` success callback, with immutable resolved legacy fuzz values. Only that callback binds
  the completed live result, re-evaluates actual master support plus authored availability, restores
  legacy fuzz when appropriate, and reapplies the **current** diagnostic channel, mask PostFX and
  displacement settings. It never queues another compose or emits a document-edit notification.
  A monotonic intent/submission serial, MID identity, pending-request check and selected-master-path
  check reject superseded work; an already-bound serial rejects duplicate/reentrant notifications.
  Direct library/document material switches cancel queued work and invalidate posted callbacks.
  Blocking bake invalidates earlier live callbacks, drains earlier work before reusing targets,
  and waits for its own render submission. That path is data-only: it neither selects a preview
  master nor registers a presentation callback; an invalid preview preference cannot block an
  otherwise valid bake. The existing owner restores live preview afterward. Failure does not invoke
  the compositor's success callback. No runtime validation is claimed.
- **Blur policy gap:** existing `FLayerSurfaceBlurRenderData` exposes only BC/N/RAM/Height weights.
  F0 blur is mirrored from RAM.a, but fuzz amount/roughness/tint and SSS amount/MFP are unblurred.
  The pipeline logs this limitation when optical data meets root Blur. Explicit optical channel
  controls/policy plus Runtime/gather/UI/blur-pass integration are still needed; no weights were
  invented and existing channel flags were not repurposed.
- Native generator/Behavior masks must reject references into Skin in that same unfinished native
  scope (evaluation precedes Skin). The Surfacing pass header reports this validation gap; its owner
  must resolve it. Behavior-on-Surfacing remains disabled, not silently implemented here.

These are build/integration requirements, not claims that the complete plugin currently compiles.
No commands, tests, diagnostics, Unreal execution or binary reads were performed.

### Legacy and upgraded material behavior

**The current default master remains valid and selected.** Default mode checks the existing
BC/Normal/RAMH/Height + legacy scalar/vector contract; it no longer requires the new parameters.
Default and Custom masters with no optical contract still bake all ten assets and visibly warn
that the material renders legacy channels/global fuzz, not the new per-pixel optical maps.
A partial/non-global optical contract is rejected before saving rather than silently approximated.

A complete declared optical contract receives all three maps, but enable=1 only when
`bHasAuthoredSurfaceOptics` is true. Otherwise enable=0 and the named legacy fuzz bindings remain
active (when the selected master declares them). Re-baking without authored optics clears the
Surface optical refs so allocation-only defaults do not activate optics on later surface reuse.
Recipe `BakedOptics`, `BakedFuzzColorMap`, `BakedSubsurface` still reference their exported textures.
Custom BC/Normal/RAM/Height mappings are unchanged; optical names are reserved for their own maps.

Lit preview uses **actual parameter declarations AND authored-state**, not non-null targets or
an arbitrary MID enable value. Missing/incomplete support retains legacy fuzz and logs an explicit
warning when authored optics cannot render. The Fuzz diagnostic graph explicitly reads Optics.G
for authored data even if the selected lit master cannot render optics; it is a data inspection,
not proof of lit-material support. Without authored data it uses the legacy scalar. F0 diagnostics
continue to read RAM.A. No new standalone SSS diagnostic mode was added.

**Remaining gap:** parameter declarations do not certify graph wiring or renderer capability.
The binary master was not inspected. Until the user runs the script and explicitly selects a
reviewed duplicate, the existing selected master does not gain per-pixel optical rendering.

## Safe Python workflow

Enable Unreal's Python Editor Script Plugin and Editor Scripting support as needed. This is an
interactive **Unreal Editor 5.8 Python** script, not a commandlet or external Python program.
Imports do nothing. There is no automatic master replacement, parent reassignment, project-setting
change, asset deletion or source-control operation requested by the script. Unreal's explicit
asset-save API itself can prompt for/check out the destination through the user's provider.

### 1. Inspect, then duplicate the current master

In Unreal's Python console (stock plugin mount shown):

```python
import sys
sys.path.insert(0, r"C:\Tools\MaterialLab\MatLab\Plugins\Mixtormat\Tools")
import upgrade_surface_optics as optics

source = "/Mixtormat/Materials/M_Mixtormat_Substrate"
destination = "/Game/MixtormatOptics/M_Mixtormat_SurfaceOptics"
report = optics.inspect_material(source)
duplicate = optics.duplicate_master(source, destination)
```

The source path follows `FMixtormatPaths::MasterMaterialObjectPath()`; adjust the mount if the
plugin was renamed. The `/Game/...` destination above is a **new example destination**, not an
asset this implementation has created. An existing unmarked destination is rejected. A matching
source-tagged duplicate is reused without overwriting it. Duplication is unsaved.

`inspect_material()` is read-only: names, classes, descriptions and real input/output pin names.
It requires only the inventory APIs, so inspection can still work if upgrade-specific APIs are
missing. It explicitly reports that inventory is not an automatic-upgrade eligibility check.
An already-upgraded source is refused before creating a new destination. Reserved name collisions
are checked across texture, scalar, vector and static-switch parameters before changing the graph.

### 2. Supply valid linear float default maps

The upgrader needs real Texture2D defaults so the material compiler can validate the samplers.
It does not create placeholder assets or silently pick arbitrary engine textures.

Bootstrap with a user-run bake using the current **Default** master. All optical textures still
export, while the result visibly warns that this legacy parent cannot render them. There is no
requirement to replace/reparent it before baking. Custom mode remains available with the four
existing mappings. The parent compositor must first supply initialized optical outputs, even for
legacy recipes; a missing target is an explicit integration error, not silently synthesized.

For example, after a bake named `OpticsSeed` into `/Game/MixtormatOptics`:

```python
textures = {
    "DA_Optics": "/Game/MixtormatOptics/T_OpticsSeed_Optics",
    "DA_FuzzColorMap": "/Game/MixtormatOptics/T_OpticsSeed_FuzzColor",
    "DA_Subsurface": "/Game/MixtormatOptics/T_OpticsSeed_Subsurface",
}
```

These paths are examples of **user-created bake outputs**. Substitute the actual paths.
All defaults must be linear, non-virtual HDR F32 textures with alpha retained. The graph's
`DA_UseSurfaceOptics` default is 0, so these defaults do not change the no-map appearance.
Preview/bake bindings activate the branch only with both a complete declared contract and a
complete **authored** optical bundle, never merely because textures have been allocated.

### 3. Upgrade the duplicate in memory

Apply and close any unsaved Material Editor graph edits on the duplicate before calling:

```python
optics.upgrade_duplicate(destination, textures, editor_changes_applied=True)
```

This opens/refreshes the duplicate's Material Editor. Refresh can discard unapplied editor-preview
edits, which is why explicit acknowledgement is required. Do not edit the graph while it runs.
The script creates tagged nodes, verifies connections, asks Unreal's compiler for errors, and
leaves the result **unsaved**. It never changes the source, referenced functions or existing instances.

Rerun the same call to verify/reuse those nodes, or provide new default texture paths. It does
not append a second optical network. It refuses changes to the monitored topology/settings and
refuses unowned parameter-name collisions instead of overwriting them. A failed first upgrade
attempt restores the five touched inputs and removes only nodes created by that attempt. If
restoration fails, the error explicitly says to discard unsaved work; no save is attempted.

### 4. Review, then explicitly save

Review both enable=0 and enable=1 in a **temporary material instance**, not by changing the
master's default enable value. Inspect displacement, normals, metal color, roughness, AO,
legacy/global fuzz and masked spatial fuzz/SSS. Then:

```python
optics.save_upgrade(destination, editor_changes_applied=True)
```

This rechecks the recorded graph, texture contract and compiler errors before saving **only the
duplicate**. Shader compile success does not validate renderer capability or the final look.
Project autosave/manual Save All is outside this script's control; do not save a failed duplicate.

Selecting this material is a **separate user decision**:

- Custom bake: choose the reviewed duplicate and retain the four existing texture mappings.
- Live layer preview: choose the reviewed duplicate in **Editor Preferences > Plugins >
  Mixtormat > Preview > Layer Preview Master**. This optional per-user setting is read on the next
  preview compose (for example, the next layer edit or recipe reselection); no restart is required.
  Empty retains the original `MasterMaterialObjectPath()`. Clear it explicitly to return to that
  master. Reparenting `MI_Mixtormat_Preview` alone does **not** select a live layer master.
  Invalid/unloadable explicit selections fail with an actionable log, not a silent substitute.
  The selected graph must declare global `DA_BaseColor`, `DA_Normal`, `DA_RAMH`, `DA_Height`;
  optical declarations must be complete if present. This picker uses the shared contract, not
  custom bake channel mappings. Diagnostic displacement settings follow the selected master.
  Raw library-material previews are unchanged. No source master or material instance is reparented.
- Default bake: `MasterMaterialObjectPath()` still resolves the original master. This change does
  not repoint it. The parent/user must explicitly decide how to select the approved shared master.

Use the same reviewed master for preview and **Custom** baking; do not introduce a Skin-only
copy/path. The existing bake Custom Master picker was bake-only, so the live preference is an
explicit separate selection, not a new material implementation. There is no remaining live master
selection blocker in source; parent GPU header/gather integration and user build/runtime review
are still required.

## Supported topology and exact wiring

Supported: one top-level, directly rooted Substrate Slab on an opaque, non-thin Surface material;
no root/Slab subsurface profile and no Nanite override. The Slab must use either no SSS or existing
profile-free Diffusion. The diffuse and F0 inputs must come directly from the same
`MaterialExpressionSubstrateMetalnessToDiffuseAlbedoF0` helper. There must be one top-level
`DA_RAMH` TextureSampleParameter2D for the shared sampling/UV configuration.

The script discovers actual input/output labels; only case/spacing normalization is allowed.
It does **not** use invented `slab.fuzz_amount`, `slab.sss_mfp`, writable ExpressionInput or
Substrate-specific pin APIs. The documented upstream-node getter returns a set, not a guaranteed
pin-indexed array. To preserve exact masks, the upgrader isolates one input at a time on a
**disposable expression copy**, refreshes the editor, and resolves that sole connection/output.
The retained probe input must then disconnect successfully and leave no upstream nodes; this
catches failed isolation even when one upstream node feeds several pins. Failed or ambiguous
readback aborts; there is no guessed-index fallback. This conservative
inspection can be slow on large graphs. It has not been exercised in Unreal here.

Generated wiring (`use = DA_UseSurfaceOptics`):

```text
newF0 = lerp(Optics.r, existing helper BaseColor input, existing helper Metallic input)
Slab.F0 = lerp(existing helper F0 output, newF0, use)
Slab.FuzzAmount = lerp(original FuzzAmount input, Optics.g, use)
Slab.FuzzRoughness = lerp(original FuzzRoughness input, Optics.b, use)
Slab.FuzzColor = lerp(original FuzzColor input, FuzzColorMap.rgb, use)
effectiveMFP_cm = Subsurface.rgb * Optics.a
Slab.SSS_MFP = lerp(original Diffusion MFP (or zero for MSS_NONE), effectiveMFP_cm, use)
Slab.SubSurfaceType = MSS_DIFFUSION
```

The existing metalness helper, diffuse output and specular path remain. Colored conductor F0
is **not** replaced by a scalar dielectric map. Existing scalar/vector fuzz chains are preserved
on the disabled branch, not deleted or repurposed. Other Slab inputs and all root connections,
including displacement, are left untouched. Displacement settings are also compared before/after.
The Python `MaterialProperty` enum does not expose the displacement root pin, so the script does
not claim independent pin readback for it; it never reconnects any root pin.

The SSS amount **scales effective scattering distance**, reaching zero MFP/no scattering at zero.
It is not a separate opacity/coverage blend or an independent probability of scattering, and it
is not fed into the profile-only MFP Scale input. Physical thickness is not exported or inferred
from signed height. If a separate SSS weight at fixed radius is required, that needs an explicitly
approved different topology, not a hidden approximation in this script.

### Actionable refusal cases

- Unconnected legacy Fuzz Amount/Color/Roughness: connect their exact current defaults/values on
  the **duplicate** in Material Editor, apply/close, and rerun. Python's Slab properties do not
  expose trustworthy per-pin defaults; the script does not invent them.
- Unconnected helper BaseColor/Metallic: make the existing defaults explicit, preserving their values.
- Hidden/function-based, vertical, multi-Slab, custom F0 or profile topology: use the formula above
  as a manual integration recipe on the duplicate. Do not flatten/delete existing layers.
- Existing Wrap/Volume SSS: changing to Diffusion would alter legacy behavior; reconcile that
  design manually. The script does not silently switch scattering models.
- Missing Python capabilities/pins or ambiguous input masks: inspect the reported real pin names;
  wire the duplicate manually or add a reviewed native editor helper. No success/save is reported.
- A manually integrated graph is intentionally not certified/saved by this script's generated-node
  validator. Review and save it yourself in Material Editor.

## Renderer limits: required user check

Epic documents that Blendable GBuffer chooses one feature per pixel, with **Fuzz ahead of SSS**,
and does **not** support Diffusion with per-pixel MFP. The combined target therefore needs
**Adaptive GBuffer** on a supported platform/path, and enough closure/byte budget. Deferred or
path-traced Diffusion support does not imply all platforms support Adaptive. Other platforms can
simplify back to Blendable. Use Material Editor > Substrate to inspect simplification.

The script always warns about this. It does not claim to query or change project settings, and
neither parameter enumeration nor shader compile success proves combined fuzz/SSS rendering.

## Official APIs consulted (UE 5.8)

- [MaterialEditingLibrary](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/MaterialEditingLibrary?application_version=5.8): expression enumeration, pin names, connect/disconnect, expression duplication, active-editor queries, refresh, compiler error array.
- [MaterialExpressionSubstrateSlabBSDF](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/MaterialExpressionSubstrateSlabBSDF?application_version=5.8) and [MaterialSubSurfaceType](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/MaterialSubSurfaceType?application_version=5.8): `sub_surface_type`, profiles, `MSS_DIFFUSION`.
- [Metalness helper](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/MaterialExpressionSubstrateMetalnessToDiffuseAlbedoF0?application_version=5.8), [Material](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/Material?application_version=5.8), [MaterialProperty](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/MaterialProperty?application_version=5.8).
- [Texture parameter](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/MaterialExpressionTextureSampleParameter2D?application_version=5.8), [Texture](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/Texture?application_version=5.8), [compression](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/TextureCompressionSettings?application_version=5.8), [sampler types](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/MaterialSamplerType?application_version=5.8).
- [AssetTools](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/AssetTools?application_version=5.8), [EditorAssetSubsystem](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/EditorAssetSubsystem?application_version=5.8), [AssetEditorSubsystem](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/AssetEditorSubsystem?application_version=5.8): duplicate, metadata, explicit save, editor opening.
- [Substrate overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/overview-of-substrate-materials-in-unreal-engine?application_version=5.8): F0/metalness, MFP centimeters, profile interaction, Diffusion, Fuzz priority and GBuffer/platform limits.
