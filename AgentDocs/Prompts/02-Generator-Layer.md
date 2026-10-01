# Step 2: Generator layer type

Paste `00-Shared-Rules.md` above this. Prerequisite: Step 1 (Height Op + Coverage) is merged.

## Goal
Add a new layer type, **Generator**, whose surface comes from a generator rather than from a material. It composites with the stack through Step 1's Height Op and Coverage, like any other layer.

## Background (why)
Generators (Strata, Cracks, Rock Formation, Pebbles) are currently **children** of a layer, and that causes three problems:
- They carve the layer's own input height.
- Their IDs must publish in the ID phase, before flow deformation.
- Each one invents its own blend rule.

Making them layers fixes all three. Step 3 then deletes the children.

## Data model
- `EMixtormatLayerType`: add `Generator` (currently `Material`, `Fill`). It's unshipped, so place it wherever is clean.
- A Generator layer owns exactly one `FMixtormatGenerator` payload: reuse the existing struct and its `Type` (StrataCarver, Cracks, RockFormation, Pebbles).
- A layer bool, `bGeneratorDrivesCoverage` (default **true**):
  - **On:** the generator's coverage (where it draws, e.g. the stones, not the gaps) multiplies into the layer's coverage, so the gaps show the layers below.
  - **Off:** the layer fills the tile, and the generator only contributes height.
- **Colour, roughness, metallic and AO** come from the layer's own fill/colour values, the same controls a Fill layer has.
  - Generators that publish named masks (Rock top/chamfer/wall, the Strata position, etc.) should be able to drive those values later.
  - For now, keep Fill-style constant values and note this as a follow-up.

## The bundle
Define `FGeneratorBundle` on `FMixtormatLayerPassContext`:

| Field | Required | Format |
|---|---|---|
| `Height` | yes | R32F; for Strata/Rock/Pebbles/Cracks this is the generator's field written as the layer's own height |
| `Coverage` | yes | R16F, 0..1 |
| `RegionIds` | optional | R32_UINT |
| `CentreUV` | optional | RG16F |
| `Orientation` | optional | R32F radians, convention of `MixtormatRotationMatrix` in `MixtormatUV.ush` |
| `NamedMasks` | optional | `TMap<FName, FRDGTextureRef>` |

- Every generator pass function writes into the bundle instead of chaining `LayerCtx.LayerInputHeight`.
- Field-only modes (`bFieldOnly`) and the `GeneratorFields` memo exist only because IDs had to publish early. Delete them if the new order makes them unnecessary.

## Pipeline order for a Generator layer (in `MixtormatGpuComposePipeline.cpp`)
1. Run the generator and fill the bundle.
2. Run the **flow tools** scoped to it (Shape Deform, Generator Flow, Flow Carve; see `HasActiveFlowTools` and `AddGeneratorFlowToolPasses`). They deform height and coverage *before* anything publishes.
3. Publish the bundle's IDs (`PublishRegionIds`) and named masks (`PublishedMaskOutputs`). They are now post-flow, which is the whole point.
4. Mask children and the child loop.
5. Composite: the layer's surface is the bundle height plus fill values, and its coverage is the layer coverage × (bundle coverage if the bool is on). Use Step 1's Height Op and Coverage.
6. Ordinary effects (erosion, worn edges, etc.) run after the composite, as on any layer.

- The **node cache** (`FMixtormatNodeCache` / `FMixtormatNodeCacheEntry`, used by Rock and Pebbles) must keep working for the generator field.
- **Strata reads `SourceHeight`** (Height Follow). On a Generator layer, "source" is the composite below. Read the previous height target, and note in the report that this makes Strata uncacheable.

## Editor
- **Add menu** (`SMixtormat_Layers.cpp`): "Generator Layer ▸ Strata / Cracks / Rock Formation / Pebbles".
- **Inspector:** a Generator layer shows the generator's existing panel (`BuildStrataCarverControls`, `BuildCracksControls`, etc.) and the Step 1 HEIGHT card. Keep the eye/preview buttons on the foldout header rows.
- **Capabilities** (`MixtormatChildCapabilities.cpp`): the outputs a generator publishes must now be available for a *layer*. Add a layer-level capabilities path, or reuse the generator case by probe. Keep a single source of truth.
- **Badges:** GEN plus the generator kind.
- Flow-tool children must be addable under a Generator layer (today they scope under a generator child).

## Not in this step
- Deleting generator children (Step 3).
- Cross-layer ID publishing and the source picker (Step 3).
- The Lattice generator (Step 3).

## Static checks
- dxc for every touched shader, both HV versions.
- Compare each touched shader's globals against its `SHADER_PARAMETER` list.
- Grep that every new property is gathered, bound and shown.

## Report and checklist for Hugo
- Report in terse bullets.
- Checklist:
  - Add a Generator layer for each kind.
  - With the coverage bool on, Rock gaps show the layer below; with it off, the layer is opaque.
  - Flow tools under a Generator layer deform the field, and the published IDs and masks follow the deformation.
  - Height Op Max and Coverage = Height work with a material layer on top.
  - The node cache still makes Rock and Pebbles edits fast.
