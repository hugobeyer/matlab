# Surfacing — new module integration and reuse plan

## Status and scope

**Historical integration plan; Skin is now implemented in source.** Current status and limits
are in [SURFACING_RUNTIME_UI_HANDOFF.md](SURFACING_RUNTIME_UI_HANDOFF.md) and
[SHADERS.md](SHADERS.md#skin-staged-tissue-folds-and-material-response). The reference remains
[Prototypes/skin.glsl](Prototypes/skin.glsl). Proposed identities and integration work below
record the original plan, not a current TODO list. Existing-producer follow-ups remain backlog.
Current work used delegated source review, but no builds, tests, diagnostics or commands.

User requirements:
- A distinct **Surfacing** authoring family, with **Skin** as its first type; more types will follow.
- Height plus material channels, not a height generator disguised by a different menu label.
- Seamless procedural fields, multiscale tissue, folds, bulges, fat, compression, and microdetail.
- Response to upstream composite curvature; relationships authored through hierarchy.
- No source/target dropdowns in the inspector.
- Preserve canonical UI/identity/channel infrastructure; reuse algorithm code only where it fits.
- Improve the Skin algorithms rather than copying weaker existing implementations for consistency.
- F0, per-pixel fuzz and SSS are now required implementation scope, not future placeholders.
- Evaluate independent detail tiling versus baking honestly, including their renderer requirements.
- Existing producers' follow-up features are a backlog, not permission to change their current look.

## 1. What a new module means here

There are two different meanings of module:

1. **Unreal compilation module:** `MixtormatRuntime`, `MixtormatShaders`, `MixtormatEditor`,
   registered in [../Mixtormat.uplugin](../Mixtormat.uplugin). Keep their dependency direction.
2. **Artist-facing module:** an `FMixtormatLayerChild` with its own child category and payload,
   gathered into render data and dispatched by the layer compositor.

Surfacing is a **new artist-facing module category**, alongside Generator, Behavior and Effect.
Do not add `Skin` to `EMixtormatGeneratorType` or hide it inside an existing effect payload.
It still needs Runtime data, Shaders execution and Editor authoring; a fourth Unreal DLL is not
needed to establish this category.

Proposed new identities, **not existing symbols**:
- `EMixtormatLayerChildType::Surfacing` — append without renumbering existing child values.
- `EMixtormatSurfacingType::Skin` — extensible subtype enum.
- `FMixtormatSurfacing` — enable state, subtype, common contribution settings and typed payload.
- `FMixtormatSkin` — Skin settings only, including shared ramp types.
- A dedicated Surfacing parameter owner, gather function, RDG pass entry and inspector builder.

Proposed focused files: `MixtormatSurfacingTypes.h`, `MixtormatSurfacingGather.h/.cpp`,
`MixtormatGpuSurfacingPasses.h/.cpp`, `MixtormatSkin.usf`, and
`MixtormatInspectorSurfacing.cpp`, in the corresponding existing module directories.

## 2. Verified registration path

There is no single self-registering module descriptor that wires all of this automatically.
The following owners must agree before the module is complete.

| Area | Existing owner / example | Surfacing work |
|---|---|---|
| Serialized child identity | `Source/MixtormatRuntime/Public/MixtormatLayerTypes.h` | Append category; add reflected Surfacing payload. Keep type separate from Generator. |
| Parameter addresses and defaults | `MixtormatParameterTypes.h`, `Private/MixtormatParameterBinding.cpp`, `Private/MixtormatParameterDefinition.cpp` in Runtime | Register owner, owner-to-struct/data resolution, defaults, runtime contracts and binding support. |
| Runtime scope validation | `Source/MixtormatRuntime/Private/MixtormatChildScope.cpp` | Add producer capabilities and permitted parent/child relationships; preserve scope order and disabled-ancestor checks. |
| Published outputs | `Source/MixtormatRuntime/Public/MixtormatOutputReference.h` and its implementation | Declare only real outputs; validate order, type and dependencies. Height is ScalarSigned, not SDF. |
| Gather | `Source/MixtormatShaders/Private/Compositing/MixtormatGeneratorGather.cpp`; caller in `MixtormatGpuCompositor.cpp` | Separate Surfacing gather; retain authored child index and scope; sanitize only mathematical constraints. |
| Render data and channel ownership | `Source/MixtormatShaders/Private/MixtormatGpuCompositorInternal.h` | Typed surface result plus explicit written-channel state, not a non-null texture inference. |
| Ordered GPU execution | `MixtormatGpuGeneratorPasses.cpp::AddGeneratorLayerPasses`, `MixtormatGpuComposePipeline.cpp::EnqueueCompose` | Integrate Surfacing at its actual hierarchy position; share ordered producer infrastructure without treating it as a generator subtype. |
| Shader registration | Existing `IMPLEMENT_GLOBAL_SHADER` sites and `MixtormatShadersModule.cpp` | Register Skin compute entry points using the existing `/Plugin/Mixtormat` shader mapping. |
| Creation and mutation | `Widgets/SMixtormat.h::EMixtormatChildCreation`, `Widgets/Layers/MixtormatLayerChildren.cpp` in Editor | Add Skin creation/defaults/name/selection/enable handling; use canonical history and preview refresh paths. |
| Menus and hierarchy | `Widgets/Layers/MixtormatLayerMenus.cpp`, `MixtormatLayerDragDrop.cpp`, shared child rows | Add Surfacing creation category; scope/reparent through hierarchy, not inspector routing selectors. |
| Output UI | `Widgets/MixtormatChildCapabilities.cpp` | Register copy/preview/instance capabilities only when the matching GPU and runtime paths exist. |
| Inspector | `Widgets/SMixtormat_Inspector.cpp`, `Widgets/Inspector/MixtormatInspectorGenerators.cpp` | Mount a dedicated Skin builder in applicable selection contexts; reuse shared group/card/row recipes. |
| Authoring metadata | `UI/Parameters/MixtormatParameterAuthoring.cpp`, `MixtormatParameterUiMeta.*`, `Config/MixtormatParameterAuthoring.json` | Ensure new properties resolve by owner and family; keep defaults and slider metadata canonical. |
| Presets | `Services/MixtormatPresetStoreDocuments.cpp::ModuleProperty`, `IsModuleCompatible` | Register payload and subtype compatibility; preserve destination identity, bindings and connections. |
| Copy/instances/groups | Runtime parameter binding/group expansion; Editor layer clipboard/drag-drop | Preserve stable identities and remap scoped ownership; do not advertise unsupported nesting or group placement. |
| Cache and previews | Compose hashes, node cache, published-field/prefix snapshots, debug passes | Include upstream state and owned subtree dependencies; restore all published outputs on a hit. |
| Preview and bake | `MixtormatGpuCompositor.cpp::BindOutputs`, Editor `Compositing/MixtormatBakeService.cpp` | Trace every exposed material channel to an actual consumed output. |

Source details worth retaining:
- `GatherGeneratorChild` already accepts modules on layers that own them; its `bGeneratorLayer`
  argument is not a dedicated layer-type test.
- `ResolveBehaviorGeneratorIndex` currently accepts **Generator** ancestors only.
  Adding a Surfacing menu item does not make Warp/Deform/Flow scopes execute for it.
- `CanCreateChild` and `AppendGroupChild` have explicit placement restrictions. Shared groups do
  not currently accept an ordinary Behavior through `AppendGroupChild`; do not promise this
  works without extending the entire identity/scope/execution contract.
- Module presets have an explicit payload switch. Reflection alone does not register a new type.
- Some architecture comments still say “Generator layers only”; use current executable checks
  rather than copying stale comments or the migration sections of older plans.

## 3. Composition contract to implement

### Read an immutable upstream snapshot

At Skin's position, distinguish:
- completed lower-layer composite height;
- current layer's resolved source height;
- preceding same-layer producer contributions;
- a scoped parent's completed local result when Skin is attached to that parent.

The existing `ResolveSubstrateHeight` in `MixtormatGpuGeneratorPasses.cpp` combines lower height,
layer input height and running generator deltas for Moss/Mycelium. Reuse/extract this resolution
logic where its units and ownership match; do not copy a second, subtly different resolver.
A root Skin module reads the preceding substrate. A scoped Skin uses its parent's local surface
within that scope. Scope completion must apply parent and Skin contributions exactly once.

Do not read Skin's own output or later siblings. Do not execute all generators first and all
Surfacing modules afterward: that changes what an interleaved stack means. Existing effects run
in their own phases; later same-layer Peeling is not magically available upstream to Skin.
Lower-layer effects are available through the completed lower composite.

### Write an explicit surface result

The current `FGeneratorBundle` contains Height, coverage, IDs, boundaries and associated fields;
it is not already a complete albedo/roughness/normal surface bundle. Existing `LayerInputBC`,
`LayerInputN`, `LayerInputRAM` and `LayerInputHeight` provide composition integration points.

Surfacing needs declared ownership for Height, BaseColor, Roughness, AO and any other supported
channel. Missing/disabled channels preserve the incoming channel. Black albedo and zero height
are valid authored values, not evidence that a producer is absent. Respect masks, channel
weights and layer composition once; keep Fill overrides from overwriting Skin's finished fields.
Preserve existing Color Ramp evaluation and compositing order rather than reordering it globally.

Use the shared height-derived-normal path. Explicit micro-normal detail must not be derived
again from the same height contribution at final composition. Preserve neutral normals when a
module writes only color or roughness. Do not let normal-only detail create layer occupancy.

SSS is now in scope: implement a real scattering-data contract through production, composition,
preview and bake. Keep channels extensible without dummy controls. RGB mean free path is a
physical distance, not albedo; physical thickness is not signed displacement. Fuzz needs spatial
amount/color/roughness, not only the current document-wide scalar/vector settings. See section 8.

## 4. Skin solve and artist controls

### Planned solve

1. **Substrate analysis:** wrapped multi-scale gradient and Hessian in destination UV space.
   Derivative distances and curvature response must have explicit units, independent of output
   resolution and with per-axis spacing for rectangular targets. Retain signed curvature,
   directional anisotropy and confidence; a scalar cavity value cannot provide a fold axis.
2. **Tissue:** periodic fat lobes, tissue thickness variation, softness and bulging.
3. **Response:** use curvature/directional cues plus authored compression/tension to shape folds,
   crown stretching and wrinkle suppression. Blend orientation without eigenvector sign flips;
   near-isotropic/flat areas have no reliable principal direction.
4. **Microdetail:** independent fine wrinkles, pores and grain with footprint-aware attenuation.
   Use compute-compatible derivatives/explicit neighboring evaluations, not GLSL `dFdx/dFdy`.
5. **Surface resolve:** linear albedo color ramp, roughness scalar ramp, cavity AO and supported
   reflectance controls. No lighting, ACES, sRGB conversion or screen-space dither in data maps.
6. **Composition/publication:** mask and combine the typed result at the correct scope; cache
   expensive geometry separately from inexpensive appearance remapping.

**Curvature is a geometric cue, not measured physical strain.** A static heightfield alone does
not determine compression or tension. Labels/help must call this a curvature-driven response,
with authored strength. Do not claim a tissue mechanics simulation or derive mesh curvature
from a texture when no mesh metric/rest-state input exists.

### Seamless construction, not edge repair

Reuse wrapped lattice identity and periodic noise foundations. All octave periods, field taps,
advected offsets, reductions and derived channels must honor the same tile topology.
Arbitrarily rotating UVs through a periodic lattice generally breaks the destination tile period.
Use tile-compatible directional construction or anisotropy of local periodic features, not a
`frac(rotatedUV)` seam or a clamp copied from the reference shader. Preserve coordinate winding
through warps and include the transformed footprint in filtering.

An upstream height/UV field with an authored seam remains a discontinuous input; wrapping its
sampler does not repair it. Skin should not silently change unrelated upstream source data.

### Inspector layout

Use the shared inspector group, `AddCard`, `MixtormatRow`, parameter widgets and theme metrics.
Proposed card order:

1. **SURFACE** — contribution strength and channel controls; preview/preset actions in the header.
2. **TISSUE** — fullness/fat, lobe size, softness, bulge; scalar profile ramp where useful.
3. **FOLDS & RESPONSE** — fold size/depth, compression, tension, direction and curvature response.
4. **MICRODETAIL** — wrinkle scale, pores, grain and shading/displacement split.
5. **ALBEDO** — shared color ramp, pigmentation variation; linear material values.
6. **ROUGHNESS & CAVITY** — shared scalar ramp and cavity response.
7. **REFLECTANCE** — dielectric IOR/F0 authoring with one canonical value, not competing controls.
8. **FUZZ** — spatial amount, tint and roughness; sheen, not geometric hair strands.
9. **SUBSURFACE** — scattering distance/tint controls mapped to a declared MFP contract.
10. **PLACEMENT** — macro placement and independent periodic micro-detail tiling.
11. **BLENDING** — existing scope/mask and channel contribution conventions, without duplicate gates.

Relationships are established by parentage, stack order and row actions. Inspector shows the
resolved context read-only if needed; no source/target dropdowns. Disable unavailable actions
with a reason. Numeric units, defaults, UI ranges and help live in canonical metadata.

## 5. Detail mapping, F0 and delivery limitations

**Baked microdetail and runtime detail mapping are not the same feature.**

- Implement macro and micro UV controls for the field solver; bake their combined result through
  the normal output path. Sub-texel detail must be filtered, not aliased into the maps.
- Keep the micro field separable so a reusable detail tile can later be exported without copying
  macro curvature into every repeated detail tile.
- Runtime detail mapping additionally needs independent material UV sampling, normal/roughness
  composition, mip behavior, amplitude metadata and bake/material-instance binding. Searching
  the inspected preview/binding paths did not establish an existing independent detail-map
  contract. Do not label a baked high-frequency texture “runtime detail mode”.
- The current `BindOutputs` binds `DA_BaseColor`, `DA_Normal`, `DA_RAMH`, `DA_Height`.
  Its code explicitly does **not** bind custom F0 parameters: it says the master derives F0 from
  BaseColor/Metallic. `MixtormatBakeService.cpp::CreateOrUpdateSurface` stores height in RAMH alpha
  and sets a scalar default IOR instead of retaining a per-pixel F0 map.
- Therefore authored per-pixel F0 is an integration task, not a delivered channel simply because
  the compositor can hold it. Preserve RAMH's height packing. Dedicated F0/SSS/detail outputs need
  real master support and explicit export bindings; never repurpose alpha silently.
- No binary master material was read or modified in this research. Master-graph support requires
  a separately permitted implementation path/user-side material work if source alone cannot supply it.

## 6. Reuse findings and later feature backlog

All items below are **proposed, not implemented**. Reuse foundations now only where needed by
Skin; adopt changed behavior in the other systems deliberately afterward.

### Craquelure

**Observed:** `MixtormatCraquelureGrow.usf` has seeded cellular growth, stress/toughness/flow
steering, collision termination and tile-wrapped state reads. `Effects/MixtormatCraquelurePasses.cpp`
separates seed/grow/resolve and jump-flood distance stages. Its current growth field is procedural;
the seed/growth parameter structs inspected do not consume composite surface textures.

**Reuse:** deterministic growth-state separation, distance-based final shaping, cache geometry
separately from width/appearance. Do not use the full growth solver merely to create Skin pores.

- [ ] CQ-1: Optional upstream curvature-response field for crack nucleation/orientation.
- [ ] CQ-2: Scalar response ramps for stress/toughness, via shared widgets/evaluation.
- [ ] CQ-3: Use crack distance/age to drive a coating Surfacing type's exposed color/roughness.
- [ ] CQ-4: Keep resolve-only edits out of growth keys; add substrate dependencies only when used.

### Peeling

**Observed:** `MixtormatPeelField.usf` reads surface normal/height/AO cues and shared curvature/
Eikonal helpers. `AddPeelingEffectPasses` seeds, propagates and resolves a reduced-resolution
front. `MixtormatPeeling.usf` applies the scoped mask to the resolved effect, not the growth seed.
Its coverage intentionally uses a narrow threshold band rather than generic soft alpha.

**Reuse:** substrate-aware analysis and seed/solve/resolve separation; existing feature-scope
mask plumbing. Preserve Peeling's specific threshold semantics instead of copying them to Skin.

- [ ] PE-1: Curvature/direction-driven lift and adhesion response using shared analysis.
- [ ] PE-2: Distinct intact coating, exposed substrate and lifted-rim material responses.
- [ ] PE-3: Ramp-driven edge roughness/color and coating-thickness output for future SSS.
- [ ] PE-4: Audit field-cache dependencies on completed/warped ID producers before adding inputs.
  The inspected key selects a generator's native `FieldKey`; it does not establish the full
  post-behavior producer identity. Treat this as a cache integration risk, not a reproduced bug.

### Moss generator

**Observed:** `MixtormatMoss.usf` separates substrate analysis, Gray–Scott growth and meso/micro
height. Stage 0 measures cavity, shelter, slope and gravity alignment. `AddMossPasses` separates
simulation and output caches and publishes Height, Coverage, Age and IDs. Analysis uses fixed
texel-radius depression taps and response gains; it is not a reusable principal-curvature tensor.

**Reuse:** the correct upstream-height snapshot, multi-scale substrate cues, separate simulation
resolution, stage-specific RDG bindings and simulation-versus-appearance caching.

- [ ] MO-1: Age/biomass/moisture-driven albedo ramp and roughness response.
- [ ] MO-2: Shared UV-scale analysis radius/response units for resolution-stable habitat cues.
- [ ] MO-3: Independent microshoot shading detail versus macro cushion displacement.
- [ ] MO-4: Publish real shelter/moisture/biomass fields where generated; later SSS/fuzz consumers
  must declare their material support rather than attach nonfunctional appearance controls.

### Noise generator

**Observed:** `MixtormatNoise.ush/.usf` supply periodic lattices, derivatives and family-specific
raw values. `AddNoisePasses` separately publishes Value and Gradient, supports Height off and
optional generated Flow. Gradient semantics differ by family and are in generator-domain space.

**Reuse:** periodic noise/hash math, typed raw field publication and explicit separation of a
pattern from its consumer's meaning. Never reinterpret every Noise Gradient as strain or Flow.

- [ ] NO-1: Footprint-aware octave/feature attenuation suitable for very fine surfacing.
- [ ] NO-2: Shared macro/micro periodic coordinate controls and exportable detail-field metadata.
- [ ] NO-3: Typed scalar-response consumption for Surfacing albedo/roughness, without duplicating
  Noise families or changing the raw Value contract.
- [ ] NO-4: Explicit derivative-frame conversion for consumers that need destination-space math.

### Rock Formation generator

**Observed:** `MixtormatRockFormation.usf` builds anisotropic Voronoi/BSP chunks into structured
buffers, evaluates fields, combines height and derives output gates. `AddRockFormationPasses`
retains Height, Top, Chamfer, Wall, edge distance, IDs, boundary and ramp fields, with further
normalized height/slope/gap outputs. These are already useful Surfacing inputs.

**Reuse:** region-consistent attributes, separate topology/field/resolve stages and typed masks.
Do not regenerate rock topology just to recolor it.

- [ ] RF-1: Lithology Surfacing subtype using Top/Chamfer/Wall and existing region identity.
- [ ] RF-2: Different fresh-break versus weathered-face color/roughness responses.
- [ ] RF-3: Curvature/shelter-driven patina, wetness and mineral microdetail.
- [ ] RF-4: Preserve material-field alignment through completed warps and scoped deformation.

### Cracks generator

**Observed:** `MixtormatCracks.usf` separates piece slip/tilt from non-positive crack delta and
uses periodic lattice identities. `AddCracksPasses` caches seven fields and applies chamfer later.
`CrackDistance` is a positive crack-cell attribute, distinct from the negative-inside UV boundary.

**Reuse:** width-versus-piece-height separation, identity-consistent regional variation, named
outputs and cheap post-field profile changes.

- [ ] CR-1: Curvature-driven opening/compression without changing piece topology every edit.
- [ ] CR-2: Crack-floor/rim/piece-top albedo and roughness responses through Surfacing.
- [ ] CR-3: Debris/wetness accumulation using real distance/cavity fields and response ramps.
- [ ] CR-4: Preserve distance units and all material fields through structural transport.

## 7. Implementation sequence and completion gates

- [ ] S1: Add distinct Surfacing runtime category/payload/parameter owner and registration hooks.
- [ ] S2: Integrate ordered hierarchy evaluation, upstream snapshots and channel ownership.
- [ ] S3: Implement periodic Skin analysis/tissue/response/micro/surface compute passes.
- [ ] S4: Integrate scoped masks, supported behaviors, ramps and typed output publication.
- [ ] S5: Finish inspector cards, hierarchy actions, presets, copy/instances/groups and undo.
- [ ] S6: Implement F0/fuzz/SSS production, composition, preview and lossless bake contracts.
- [ ] S7: Add an approved editor-side Substrate master setup; verify real inputs before wiring.
- [ ] S8: Review cache invalidation, zero-strength identity, output ordering and channel ownership.
- [ ] S9: Document user-side build, visual seam, nesting, bake/reimport and renderer checks.

No production-complete claim until creation, edits, dependencies, composition, publication and
baking are connected. User performs compilation and runtime testing; source review is not a
substitute. Unsupported renderer features must be reported rather than silently approximated.

## 8. Expanded optical scope: F0, fuzz and SSS

### Confirmed source limitations

- `MixtormatGpuCompositor.cpp::BindOutputs` intentionally does not wire custom F0 parameters.
- `MixtormatBakeService.cpp::CreateOrUpdateSurface` replaces RAM alpha with height and assigns
  `DefaultIOR = 1.5f`; per-pixel F0 is not preserved by that surface export path.
- `SMixtormatPreviewViewport.cpp::ComposeLayersWithDebug` binds `DA_FuzzInfluence` as one scalar
  and `DA_FuzzColor` as one vector. Bake uses the same document-wide values. This is not a
  spatially masked fuzz channel, regardless of what Skin's local mask contains.
- The inspected preview creates transient diagnostic materials, not a replacement Substrate
  master builder. Merely setting new `DA_*` names on a material instance cannot add graph inputs.

### Required end-to-end changes

1. Keep existing BaseColor/Normal/RAMH/Height contracts intact. Add explicitly owned optical
   outputs and matching surface-asset fields; do not silently reuse height alpha for optics.
2. F0: preserve linear dielectric reflectance through composition and baking; wire it into the
   Slab's specular parameterization without erasing the established metallic/color conversion.
   IOR authoring can derive dielectric F0 with