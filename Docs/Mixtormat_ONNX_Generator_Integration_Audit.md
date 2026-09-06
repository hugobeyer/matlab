# Mixtormat — ONNX Generator Integration Audit

**Purpose:** audit how ONNX-trained generators created in Houdini can be added to the existing Mixtormat plugin **without rebuilding the compositor, layer system, or current procedural effects**.

**Source audited:** `MixtorMat_sourcecode.zip` supplied in this conversation.

**Primary conclusion:** this is feasible, and the existing architecture already has a good insertion point. The first implementation should be a **Neural Mask / Pattern child** that produces a mask into the existing ordered mask accumulator. Do **not** replace the compositor or rewrite Erosion/Stain/Chipping first.

---

# 1. What I am proposing now

The goal is not:

```text
photo → AI → finished material
```

and not:

```text
text prompt → material
```

The useful architecture is:

```text
Houdini
    ↓
create/train procedural visual behavior
    ↓
export ONNX
    ↓
Mixtormat Neural Generator
    ↓
existing mask/effect/layer stack
```

Examples:

```text
Houdini-trained crack pattern
    → ONNX
    → Neural Mask child
    → drives Chipping / Fill / Stain / Peeling

Houdini-trained erosion placement
    → ONNX
    → Neural Mask child
    → drives existing Erosion placement

Houdini high-quality erosion solve
    → train input→height-delta model
    → ONNX
    → Neural Height Filter
    → modifies existing composited Height
```

This lets Houdini remain the **authoring/training laboratory** and Unreal remain the **fast inference + editable composition environment**.

---

# 2. Audit of the current Mixtormat architecture

## 2.1 The layer child system is already the correct extension point

Current enum:

`Source/MixtormatRuntime/Public/MixtormatMaterial.h:1149-1157`

```cpp
enum class EMixtormatLayerChildType : uint8
{
    Mask,
    Effect,
    Generated,
    Craquelure,
    ColorId
};
```

Current child storage:

`Source/MixtormatRuntime/Public/MixtormatMaterial.h:1159-1181`

```text
FMixtormatLayerChild
    Mask
    Effect
    Generated
    Craquelure
    ColorId
```

This means Mixtormat already supports heterogeneous ordered children.

A neural generator does **not** require a new layer architecture.

Recommended append-only addition:

```text
Neural = 5
```

Do not reorder existing enum values.

---

## 2.2 The current Generated Mask should NOT be overloaded with neural generators

`FMixtormatGeneratedMask` is specifically built from surface-derived signals:

`Source/MixtormatRuntime/Public/MixtormatMaterial.h:147-250`

Existing inputs include:

```text
Curvature
Direction
AO
Height
Ridge
```

and it already owns:

```text
Blend Mode
Weight
Invert
Balance
Contrast
Offset
```

However, the existing project documentation already identified an important design boundary:

> generated masks are derived from the surface beneath the layer; independent procedural patterns such as Craquelure deserve their own child type.

That same rule applies to neural generators.

Therefore:

```text
Generated Mask       = analytic, surface-derived signals
Craquelure           = independent hand-authored procedural generator
Neural Generator     = learned generator / learned surface response
```

Do not force ONNX into `FMixtormatGeneratedMask`.

---

## 2.3 The mask compositor already does almost everything a neural mask needs

The compositor collects a generated-mask child and converts its recipe settings to render data:

`Source/MixtormatShaders/Private/MixtormatGpuCompositor.cpp:1874-1911`

During RDG composition it dispatches the mask, then publishes the result into the same mask ping-pong chain:

`Source/MixtormatShaders/Private/MixtormatGpuCompositor.cpp:2676-2759`

The generated mask already receives the accumulated surface underneath:

```text
SurfaceNormal
SurfaceRAM
SurfaceHeight
SurfaceRidge
```

and writes:

```text
OutputMask
```

That is almost exactly the contract required for a learned surface-aware mask.

The neural branch can become:

```text
Current accumulated surface
        ↓
Texture → Tensor pack
        ↓
ONNX inference
        ↓
Tensor → Mask texture
        ↓
existing mask blend/shaping
        ↓
CombinedMask
```

No BC/N/RAM/H compositor redesign is necessary.

---

## 2.4 Existing effects can already consume the neural result

This is the most important reason to start with masks.

The current layer system already allows masks to occur before effects in the ordered child list.

A neural mask can therefore drive existing behavior without modifying that behavior.

Examples:

```text
Neural Mask
    ↓
Erosion

Neural Mask
    ↓
Stain

Neural Mask
    ↓
Peeling

Neural Mask
    ↓
Chipping
```

This means a first ONNX integration does **not** require rewriting:

- `MixtormatErosion.usf`
- `MixtormatStain.usf`
- `MixtormatChipping.usf`
- `MixtormatPeeling.usf`
- `MixtormatComposite.usf`

The learned model can simply decide **where** an existing effect acts.

---

## 2.5 Mixtormat's compositor is already RDG-based and editor-only

`FMixtormatGpuCompositor` owns the existing GPU composition path:

`Source/MixtormatShaders/Public/MixtormatGpuCompositor.h`

The plugin manifest currently marks:

```text
MixtormatRuntime   Runtime
MixtormatShaders   Editor
MixtormatEditor    Editor
```

`MaterialLab.uplugin:11-33`

That is useful.

The expensive ONNX texture generation can remain an **editor-time composition feature**, matching the current Mixtormat shader compositor and bake workflow.

We do not initially need neural inference in a packaged game's material shader.

---

# 3. The three useful ONNX paths

There are three different ML features worth considering.

They should not be mixed together.

---

# 3A. Neural Pattern Generator — strongest first use

## Use case

Generate independent procedural masks/patterns:

```text
pores
rust breakup
mold
crackle
flakes
speckles
stone breakup
fibers
grunge
organic cellular patterns
paint islands
weathering breakup
```

## Best Houdini technology: Neural Cellular Automata

Houdini 22 includes:

**ML Train Neural Cellular Automata**

https://www.sidefx.com/docs/houdini/ml/train_solutions/ml_trainneuralcellularautomata.html

TOP node:

https://www.sidefx.com/docs/houdini/nodes/top/ml_trainneuralcellularautomata.html

NCA overview:

https://www.sidefx.com/docs/houdini/copernicus/neural_cellularautomata.html

### Why this is unusually relevant to Mixtormat

SideFX explicitly designed this to:

> train a Neural Cellular Automata model to synthesize tileable pattern-based textures from a single target image.

The target itself does **not** need to be tileable.

The NCA wraps its borders and learns a tileable generative rule.

This is much closer to what Mixtormat needs than MatFormer-RL for basic pattern generation.

---

## Houdini NCA architecture

SideFX's current implementation uses:

```text
target texture
      ↓
train NCA
      ↓
16-channel latent cell state
      ↓
decoder
      ↓
visible texture
```

The NCA is trained at a coarse state resolution around `128 × 128`.

The decoder is separate from the NCA model and reconstructs finer visual detail.

Training exports ONNX checkpoints for:

```text
NCA core
Decoder
```

The model is iterative.

A pattern evolves through repeated application of the same learned update rule.

---

## Mixtormat use

For a mask model:

```text
blank/random cells
      ↓
NCA ONNX × N iterations
      ↓
16-channel cells
      ↓
decoder ONNX
      ↓
RGB pattern
      ↓
luminance / chosen channel
      ↓
existing Mixtormat mask shaping
```

Mixtormat then retains its existing:

```text
Balance
Contrast
Offset
Invert
Weight
Blend Mode
```

The neural network generates the **structure**.

Mixtormat remains responsible for the familiar artist controls.

---

## Variation

Houdini's NCA workflow exposes concepts such as:

```text
Seed
Update Rate
Rotation
Scale
Iterations
```

These can be represented as a small Mixtormat inspector instead of dozens of procedural parameters.

Important implementation caveat:

SideFX documents these controls at the COP wrapper level. They must **not** be assumed to all be literal ONNX tensor inputs.

Before implementing the Unreal runner, inspect the exported ONNX signatures.

If Houdini performs part of the update-mask/rotation/scale logic outside the model, reproduce that lightweight preprocessing in Mixtormat or export a wrapper model.

---

## Version requirement

The integrated Neural Cellular Automata training workflow is documented as **Houdini 22.0**.

If the production environment is Houdini 21.x, use option 3B or a custom PyTorch NCA trainer until moving to H22.

---

# 3B. Neural Effect / Mask Generator — best for erosion, stain and wear

## Use case

Train a network to understand an input surface and output an effect mask.

Examples:

```text
height + normal
    → erosion placement

height + normal + AO
    → dirt accumulation

height + normal
    → chipped-edge mask

height + curvature
    → stone weathering

normal + height + roughness
    → stain/runoff mask
```

This is different from NCA.

The pattern is conditioned on the actual material underneath.

---

## Best built-in Houdini starting point: ML Train Style Transfer

Houdini's `ML Train Style Transfer TOP` is actually a generic image-to-image trainer.

Documentation:

https://www.sidefx.com/docs/houdini/nodes/top/ml_trainstyletransfer.html

SideFX describes it as a generic model for:

```text
input image → output image
```

It supports **paired training**, which is what Mixtormat should use.

Its generator is a U-Net and it can export the generator directly to ONNX.

It also supports baking pre/post-processing into the exported ONNX model.

---

## Training data can be generated entirely in Houdini

For example, build a high-quality erosion tool in COPs.

For each source material:

```text
INPUT
    clean height
    clean normal

HIGH-QUALITY HOUDINI SOLVE
    hydraulic / flow / curvature / custom OpenCL erosion

TARGET
    erosion mask
```

Generate thousands of variations.

Then train:

```text
surface features → desired erosion mask
```

In Unreal the expensive simulation is gone.

The ONNX model approximates the result.

This is one of the most useful ML patterns for Mixtormat because Houdini can use algorithms too expensive to run interactively in the plugin.

---

# 4. Pack features instead of training on pretty renders

For Mixtormat, do not begin with rendered spheres.

Use direct PBR/geometry information.

A minimal three-channel training representation can be:

```text
R = Height
G = Normal X mapped to 0..1
B = Normal Y mapped to 0..1
```

Alternative:

```text
R = Height
G = Curvature
B = AO
```

Target:

```text
R = effect mask
G = effect mask
B = effect mask
```

The repeated RGB target is useful because Houdini's built-in image training workflow naturally works with image channels.

Later, with custom PyTorch, use arbitrary channels:

```text
INPUT
Height
Normal X
Normal Y
Curvature
AO
Roughness
Ridge
Mask

OUTPUT
Mask
Flow X
Flow Y
Height Delta
```

---

# 5. A very useful future contract: neural field output

A model does not have to output only one mask.

A compact four-channel learned field would be much more powerful:

```text
R = effect / coverage mask
G = Flow X encoded 0..1
B = Flow Y encoded 0..1
A = signed Height Delta
```

Then a single model can describe:

```text
where
which direction
how much height changes
```

Examples:

```text
erosion
stain
peeling
slumping
weathering
directional scratches
```

This is a phase-2 feature.

Do not require it for the first integration.

---

# 6. Neural height-delta filters can replace weak procedural effects later

The first neural feature should only generate masks.

However, Erosion is a special case.

A mask can improve placement, but it cannot fix a poor underlying carve profile.

If the existing Erosion shader remains fundamentally too sharp, train the model to output a **height delta** instead.

Training:

```text
clean surface
    ↓
high-quality Houdini erosion
    ↓
target ΔH = erodedHeight - originalHeight
```

Inference:

```text
surface features
    ↓
ONNX
    ↓
ΔH
    ↓
HeightOut = HeightIn + Amount * ΔH
```

Normals can then be regenerated from `ΔH` using the same type of local derivative pass the plugin already uses for height-based effects.

This creates a generic:

```text
Neural Height Filter
```

without rewriting the whole compositor.

The model replaces only the expensive/poor procedural calculation.

The rest of Mixtormat stays intact.

---

# 7. Recommended new Mixtormat asset

Do not put model details directly into every recipe child.

Create a reusable asset:

```text
UMixtormatNeuralGenerator
```

Conceptual fields:

```text
DisplayName
Category

GeneratorType
    PatternNCA
    SurfaceMask
    SurfaceField
    HeightDelta

PrimaryModel
DecoderModel        // NCA only

InputSemantic
OutputSemantic

InputResolution
StateResolution

DefaultIterations
DefaultSeed
DefaultUpdateRate

ModelVersion
ModelMetadataVersion
```

The asset owns the ML contract.

The recipe child owns artist variation.

---

# 8. Recommended recipe child

Append:

```text
EMixtormatLayerChildType::Neural = 5
```

Add:

```text
FMixtormatNeuralMask
```

Conceptual recipe fields:

```text
bEnabled
Generator

Seed
Scale
Rotation
Iterations
Variation

BlendMode
Weight
Invert
Balance
Contrast
Offset
```

This behaves like any other mask-producing child.

The model asset can evolve independently from individual material recipes.

---

# 9. Minimal source changes

## Runtime

### `Source/MixtormatRuntime/Public/MixtormatMaterial.h`

Add:

```text
Neural = 5
```

and:

```text
FMixtormatNeuralMask Neural;
```

Do not alter existing child enum values.

### New asset

Suggested:

```text
Source/MixtormatRuntime/Public/MixtormatNeuralGenerator.h
Source/MixtormatRuntime/Private/MixtormatNeuralGenerator.cpp
```

---

## Shader compositor

### `Source/MixtormatShaders/Private/MixtormatGpuCompositor.cpp`

Add one child branch analogous to:

```text
Generated
Craquelure
ColorId
```

Conceptually:

```text
if Child.Type == Neural
{
    create/obtain NNE model instance

    pack required surface textures → tensor buffer

    enqueue ONNX model in current RDG graph

    unpack output tensor → R16F mask

    run normal Mixtormat mask shaping/blend

    CombinedMask = result
}
```

No new composition architecture.

---

## Editor UI

Current child creation is centralized in:

```text
Source/MixtormatEditor/Private/Widgets/SMixtormat_Layers.cpp
```

Add:

```text
Neural Generator
```

beside:

```text
Generated Mask
Color ID Mask
Craquelure
```

Inspector work belongs in:

```text
SMixtormat_Inspector.cpp
```

The first inspector can be extremely small:

```text
Model
Seed
Scale
Iterations
Weight
Contrast
Balance
Invert
Blend Mode
```

---

# 10. Unreal NNE integration

Official Unreal Engine 5.8 documentation:

NNE overview:

https://dev.epicgames.com/documentation/unreal-engine/neural-network-engine-overview-in-unreal-engine

NNE:

https://dev.epicgames.com/documentation/unreal-engine/neural-network-engine-in-unreal-engine

`IModelInstanceRDG`:

https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/NNE/IModelInstanceRDG

`EnqueueRDG`:

https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/NNE/IModelInstanceRDG/EnqueueRDG

---

## Why `INNERuntimeRDG` fits the current compositor

Epic exposes three broad NNE interfaces:

```text
CPU
GPU
RDG
```

The RDG interface is specifically for neural inference whose input/output participates in the Render Dependency Graph.

Mixtormat is already composing through RDG.

Therefore the desirable path is:

```text
Mixtormat RDG texture
        ↓
pack compute pass
        ↓
FRDGBuffer tensor
        ↓
IModelInstanceRDG::EnqueueRDG
        ↓
FRDGBuffer output tensor
        ↓
unpack compute pass
        ↓
Mixtormat RDG mask/field texture
```

This avoids:

```text
GPU texture
→ CPU readback
→ ONNX
→ CPU result
→ GPU upload
```

for every preview.

---

# 11. One structural bridge IS required: Texture ↔ Tensor Buffer

NNE RDG binds tensors as `FRDGBufferRef`.

Mixtormat currently operates primarily on `FRDGTextureRef`.

Therefore two small GPU utility passes are required:

```text
PackTextureTensorCS
UnpackTensorTextureCS
```

Example use:

```text
Height texture
Normal texture
RAM texture
      ↓
Pack
      ↓
float tensor buffer
      ↓
NNE
      ↓
output tensor buffer
      ↓
Unpack
      ↓
R16F mask
```

This is the main new GPU infrastructure.

Once it exists, every later neural operator can reuse it.

---

# 12. Model cache

Do not construct neural model instances every compose.

`FMixtormatGpuCompositor` already maintains persistent compositor-side state such as its craquelure network cache.

Add an analogous model cache:

```text
FMixtormatNeuralModelCache
```

Key by:

```text
UNNEModelData
runtime
input shape
model version
```

Model creation/setup can happen outside the render-pass loop.

Inference is then enqueued into the active RDG graph.

---

# 13. NNE risk that must be tested early

Epic currently labels NNE as Beta, and the dedicated RDG runtime is documented as Experimental.

Do not assume every Houdini-exported ONNX model will run in every NNE runtime.

The first engineering spike should verify:

```text
Houdini ONNX
    ↓
Unreal import
    ↓
selected NNE runtime CanCreateModel
    ↓
SetInputTensorShapes
    ↓
EnqueueRDG
```

Particularly verify the NCA decoder.

A SIREN/WIRE decoder may contain ONNX operations that one runtime supports and another does not.

If `NNERuntimeRDG` cannot execute a given model:

1. simplify/re-export the network with supported standard ONNX operations;
2. test another NNE runtime;
3. temporarily run low-resolution editor inference through CPU/GPU as a fallback.

Do this before building the full UI.

---

# 14. Why I would not start with direct full-surface neural generation

Avoid v1 models that output:

```text
Base Color
Normal
Roughness
AO
Metallic
Height
```

all at once.

That would fight the existing architecture rather than extend it.

The current compositor is already good at combining material channels.

Learned models should initially produce **control fields**:

```text
mask
flow
height delta
```

and let Mixtormat continue to own:

```text
layering
materials
roughness
normal composition
height composition
baking
```

This preserves editability.

---

# 15. Practical Houdini generator categories

## A. NCA patterns

Train from one visual target.

Best candidates:

```text
rust breakup
organic mold
pores
cellular stone
paint islands
grunge
crackle breakup
fiber clumps
mineral speckle
cloudy residue
```

Output:

```text
mask
```

---

## B. Paired learned masks

Generate training pairs using existing or experimental Houdini/OpenCL tools.

Best candidates:

```text
erosion placement
edge wear
cavity weathering
chipping
stain/runoff
peel initiation
moisture accumulation
deposition
```

Input:

```text
surface features
```

Output:

```text
mask
```

---

## C. Paired learned fields

Best candidates:

```text
erosion flow
stain flow
sediment flow
directional scratches
slumping
dripping
```

Output:

```text
mask + vector field
```

---

## D. Paired learned height delta

Best candidates:

```text
high-quality erosion
soft brick weathering
stone rounding
micro-chipping
surface recession
```

Output:

```text
signed ΔHeight
```

This is the route that can eventually replace the current Gully erosion algorithm while preserving the rest of Mixtormat.

---

# 16. Example: train a better Erosion model without redesigning Mixtormat

## Houdini

Build the best erosion result possible, with no concern about interactive speed.

It can use:

```text
OpenCL iterations
hydraulic transport
stain-like advection
curvature
height
normal
flow
thermal/talus logic
```

For each training example save:

```text
input:
    height
    normal.xy

target:
    final erosion mask
```

First model:

```text
[H, Nx, Ny] → erosion coverage
```

Unreal:

```text
Neural Erosion Mask
    ↓
existing Erosion placement
```

If placement improves but the carve itself remains too sharp:

Second model:

```text
[H, Nx, Ny] → ΔH
```

Unreal:

```text
Neural Height Filter
    ↓
simple height apply
    ↓
normal-from-height delta
```

This changes only Erosion's learned core, not the layer system.

---

# 17. Example: neural Stain without replacing the current simulation

The current Stain solver already has good transport behavior.

A neural model could instead learn:

```text
where liquid should begin
```

or:

```text
where deposit should collect
```

Model:

```text
[Height, Curvature, AO] → Stain Source Mask
```

Then:

```text
Neural Mask
    ↓
Stain
```

The existing Stain transport still handles:

```text
flow
spread
absorption
drying
deposition
```

This is much lower-risk than replacing Stain with AI.

---

# 18. Example: learned crack family

Two alternatives:

## Existing Craquelure

Use the current hand-written system when artist controls are important.

## Neural crack texture

Train NCA against a crack family target.

Mixtormat controls:

```text
Seed
Scale
Rotation
Iterations
Contrast
```

Then use the output as:

```text
mask
```

It can drive:

```text
Fill layer
Chipping
Peeling
height carve
roughness
```

The two systems can coexist.

No need to delete Craquelure.

---

# 19. Baking requires almost no special neural work

Mixtormat's bake system consumes the compositor's final render targets.

If the neural generator writes into the compositor before the layer/effect result is published, the final:

```text
BC
Normal
RAM
Height
```

already contain its result.

Therefore baking does not need to understand ONNX.

That separation should be preserved:

```text
ONNX = preview/composition implementation detail
Bake = consumes final compositor outputs
```

---

# 20. Recommended implementation order

## Spike 0 — ONNX compatibility only

Do not touch the layer UI.

1. Export a tiny test ONNX from Houdini.
2. Import it into Unreal as `UNNEModelData`.
3. Create a model instance.
4. Run it through the chosen NNE runtime.
5. Verify numerical output.

Success condition:

```text
Houdini ONNX executes inside the plugin.
```

---

## Spike 1 — Texture/tensor bridge

Add:

```text
RDG texture → tensor buffer
tensor buffer → RDG texture
```

Test with an identity model.

Success condition:

```text
input mask == output mask
```

inside the existing compositor.

---

## Phase 1 — Neural Mask child

Add:

```text
EMixtormatLayerChildType::Neural = 5
FMixtormatNeuralMask
UMixtormatNeuralGenerator
```

Only output:

```text
R16F mask
```

Feed it into the existing mask accumulator.

This is the first real feature.

---

## Phase 2 — Houdini NCA pattern library

Train several models:

```text
Rust
Pores
Mold
Crackle
Grunge
Flakes
```

No surface input required.

Use them as neural masks.

---

## Phase 3 — Surface-aware paired models

Train:

```text
erosion placement
edge wear
stain source
chip source
```

from Houdini-generated pairs.

---

## Phase 4 — Neural field

Support:

```text
mask
flow.xy
auxiliary
```

Only after a real operator needs it.

---

## Phase 5 — Height Delta

Add a generic learned height filter for effects that cannot be represented well as placement masks.

This is where high-quality learned erosion belongs.

---

# 21. What should NOT be reworked

For the first implementation, leave these systems alone:

```text
FMixtormatLayer
existing Mask child
Generated Mask child
Craquelure
Color ID
Erosion settings
Stain simulation
Chipping
Peeling
Grade
BC/N/RAM/H composition
Bake Service
Surface importer
material library
protected master material
```

Only add the neural path alongside them.

---

# 22. Suggested class boundary

```text
MixtormatRuntime
│
├─ UMixtormatNeuralGenerator
└─ FMixtormatNeuralMask

MixtormatShaders
│
├─ FMixtormatNeuralModelCache
├─ TensorPackCS
├─ TensorUnpackCS
└─ NNE RDG inference bridge

MixtormatEditor
│
├─ Add Neural Generator menu item
├─ Neural Generator inspector
└─ debug preview
```

This respects the current module boundaries.

---

# 23. Build/dependency impact

Current build files do not depend on NNE.

Relevant existing files:

```text
Source/MixtormatRuntime/MixtormatRuntime.Build.cs
Source/MixtormatShaders/MixtormatShaders.Build.cs
Source/MixtormatEditor/MixtormatEditor.Build.cs
```

Unreal's current NNE documentation says the base module dependency is:

```text
NNE
```

and the RDG API is declared in:

```cpp
#include "NNERuntimeRDG.h"
```

The actual runtime plugin used for RDG must also be enabled and validated.

Keep NNE dependencies isolated to the smallest modules possible.

If `UMixtormatNeuralGenerator` directly stores `UNNEModelData`, Runtime will need to know about NNE.

If that dependency is undesirable, the neural asset may store a `FSoftObjectPath`, but a typed `UNNEModelData` reference is cleaner and safer.

---

# 24. Houdini → Unreal model package

Do not ship a naked `.onnx` with no metadata.

Each learned generator should have a small sidecar contract.

Example:

```json
{
  "name": "ErosionWear_v1",
  "type": "surface_mask",
  "version": 1,
  "input_width": 256,
  "input_height": 256,
  "input_channels": [
    "height",
    "normal_x",
    "normal_y"
  ],
  "output_channels": [
    "mask"
  ],
  "input_range": [0.0, 1.0],
  "tileable": true,
  "recommended_iterations": 1
}
```

NCA:

```json
{
  "name": "RustCells_v1",
  "type": "nca_pattern",
  "version": 1,
  "latent_channels": 16,
  "state_resolution": 128,
  "core_model": "RustCells_core.onnx",
  "decoder_model": "RustCells_decode.onnx",
  "recommended_iterations": 96,
  "tileable": true
}
```

The Unreal asset can ingest/copy this metadata.

---

# 25. Agent-assisted workflow

An agent can help without redesigning the product.

## Audit/export agent

Tasks:

```text
Inspect exported ONNX files.
List tensor names.
List input/output shapes.
List opset.
List operators used.
Check dynamic axes.
Check model size.
Run ONNX checker.
Run ONNXRuntime reference inference.
Generate Mixtormat model metadata.
```

---

## Houdini training agent

Tasks:

```text
Build TOP wedges.
Generate paired data.
Check broken/duplicate outputs.
Pack feature maps.
Create train/test splits.
Launch NCA or Style Transfer training.
Track checkpoints.
Export ONNX.
Create contact sheets of results.
```

---

## Unreal integration agent

Tasks:

```text
Add append-only Neural child.
Add model asset.
Add NNE dependencies.
Implement model cache.
Implement RDG tensor pack/unpack.
Implement EnqueueRDG path.
Add debug preview.
Add minimal inspector.
Add compatibility/error messages.
```

---

## Validation agent

Tasks:

```text
Compare Houdini inference and Unreal inference.
Measure max/mean tensor error.
Benchmark 128/256/512 inference.
Test repeated Seed values.
Test tile seams.
Test bake parity.
Test model reload.
Test missing/unsupported model behavior.
```

---

# 26. Agent implementation rules

Use the following brief for an implementation agent.

```text
PROJECT:
Mixtormat ONNX Neural Generators

GOAL:
Add learned procedural mask generation to the existing Mixtormat child stack without redesigning
the compositor, material recipe, existing effects, or bake system.

DO NOT:
- replace the existing layer architecture
- remove Generated Mask
- remove Craquelure
- replace Erosion/Stain/Chipping in phase 1
- modify the protected master material
- introduce text prompting
- generate full PBR surfaces in phase 1
- renumber existing serialized enums
- use CPU texture readback as the final architecture

PHASE 1:
- append EMixtormatLayerChildType::Neural
- create UMixtormatNeuralGenerator
- create FMixtormatNeuralMask
- import/use UNNEModelData
- run inference through NNE RDG if compatible
- create reusable texture↔tensor RDG packing passes
- write the neural output into the existing mask ping-pong chain
- reuse current mask BlendMode/Weight/Invert/Balance/Contrast/Offset behavior
- reuse current debug mask preview
- preserve bake behavior

FIRST PROOF:
Use a trivial or identity ONNX model before integrating an NCA model.

SECOND PROOF:
Run one Houdini-generated neural mask model.

THIRD PROOF:
Run one NCA pattern model.

VALIDATION:
Houdini/ONNXRuntime/Unreal outputs must be compared numerically before adding more features.
```

---

# 27. Recommendation

The best route is now:

```text
DO NOT build "AI Mixtormat" broadly.

Build one reusable Neural Generator child.
```

Then use **two Houdini training families**:

### Family 1 — Pattern models

```text
Houdini 22 NCA
single texture target
→ tileable ONNX pattern
→ Mixtormat Neural Mask
```

### Family 2 — Surface-aware effect models

```text
Houdini procedural/high-quality solver
→ paired training data
→ U-Net ONNX
→ Mixtormat Neural Mask / later Neural Height Delta
```

This makes Houdini a generator authoring environment.

It lets you invent a pattern or expensive effect in COPs/OpenCL once, train it, and bring a compact learned approximation into Mixtormat.

Most importantly, it fits the code you already have instead of replacing it.

---

# 28. Suggested first experiments

## Experiment A — NCA rust breakup

```text
Target:
one good rust/corrosion breakup mask

Train:
Houdini 22 ML Train Neural Cellular Automata

Export:
core.onnx
decoder.onnx

Unreal:
Neural Mask child

Validation:
tile seam
seed variation
scale
cost
```

---

## Experiment B — learned brick erosion placement

```text
Inputs:
Height
Normal X
Normal Y

Target:
mask from a high-quality Houdini erosion solve

Train:
paired ML Train Style Transfer

Export:
erosion_mask.onnx

Unreal:
Neural Mask → Erosion
```

This determines whether ML placement alone fixes enough of the current erosion problem.

---

## Experiment C — learned erosion height delta

Only if Experiment B is insufficient.

```text
Inputs:
Height
Normal X
Normal Y

Target:
finalH - sourceH

Unreal:
Neural Height Filter
```

This is the likely long-term replacement for the current sharp gully calculation, while retaining all existing layer and effect infrastructure around it.

---

# References

## Houdini — NCA / pattern generation

- Neural Cellular Automata overview  
  https://www.sidefx.com/docs/houdini/copernicus/neural_cellularautomata.html

- ML Train Neural Cellular Automata recipe  
  https://www.sidefx.com/docs/houdini/ml/train_solutions/ml_trainneuralcellularautomata.html

- ML Train Neural Cellular Automata TOP  
  https://www.sidefx.com/docs/houdini/nodes/top/ml_trainneuralcellularautomata.html

- Neural Cellular Automata Core COP  
  https://www.sidefx.com/docs/houdini/nodes/cop/neural_cellularautomatacore.html

- Neural Cellular Automata Decode COP  
  https://www.sidefx.com/docs/houdini/nodes/cop/neural_cellularautomatadecode.html

## Houdini — image-to-image effect training

- ML Train Style Transfer TOP  
  https://www.sidefx.com/docs/houdini/nodes/top/ml_trainstyletransfer.html

- ONNX Inference COP  
  https://www.sidefx.com/docs/houdini/copernicus/onnx_inference.html

- ML overview  
  https://www.sidefx.com/docs/houdini/ml/overview.html

## Unreal Engine 5.8 — NNE

- Neural Network Engine  
  https://dev.epicgames.com/documentation/unreal-engine/neural-network-engine-in-unreal-engine

- NNE Overview  
  https://dev.epicgames.com/documentation/unreal-engine/neural-network-engine-overview-in-unreal-engine

- `IModelInstanceRDG`  
  https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/NNE/IModelInstanceRDG

- `IModelInstanceRDG::EnqueueRDG`  
  https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/NNE/IModelInstanceRDG/EnqueueRDG

## Related research

- MatFormer-RL project page  
  https://yiweihu.netlify.app/publication/matformerrl/

- ProcMatRL code  
  https://github.com/adobe-research/ProcMatRL

MatFormer-RL remains relevant to later automatic procedural fitting, but for the immediate goal of **authoring reusable learned patterns/effects in Houdini and executing them in Mixtormat**, Houdini's NCA and paired image-to-image ONNX workflows are more directly applicable.
