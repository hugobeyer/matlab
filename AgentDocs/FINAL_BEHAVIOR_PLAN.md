Mixtormat — Noise, Flow & Behavior Simplification
Complete Architecture and Implementation Plan

Repository: https://github.com/hugobeyer/matlab
Branch: feature/behavior-system-v2
Engine: Unreal Engine 5.8
Reviewed commit: e0958f2

Objective: Simplify Mixtormat's procedural system by treating Noise and Flow as two presets of the same Generator, making Behaviors consume fields automatically, and eliminating Inspector-based source/target connections.

The result should feel like a procedural layer stack, not a node graph.

1. Final Architecture

We establish three functional categories.

Category	Purpose	Examples
Generators	Produce height, vectors, IDs and other fields	Noise, Flow, Cracks, Rock Formation
Behaviors	Modify existing fields	Push, Distort, Deform, Carve
Masks	Control spatial influence	Noise Mask, ID Mask, Texture Mask

Generators produce information.

Behaviors modify that information.

Masks control where modifications occur.

Parent ownership and sibling order replace explicit field connections.

Example hierarchy
GENERATOR — Noise
│
├── Flow
│   └── Noise Mask
│
├── Flow
│   └── Mask
│
├── Push
│   └── Noise Mask
│
├── Distort
│   └── Mask
│
└── Deform
    └── Noise Mask

In this example:

Noise produces height, gradient and intrinsic flow.
First Flow adds directional variation.
Second Flow adds another directional contribution.
Push modifies height.
Distort reads the accumulated Flow and warps the generator's outputs.
Deform reads that Flow and modifies relief only.

Every operation uses its own parameters and masks.

There are no Inspector source or target selectors.

2. Noise and Flow Are the Same Generator

This is the central change.

Do not implement a second procedural Noise engine.

Both creation entries instantiate the existing Noise V2 Generator.

They share:

Noise families and algorithms.
Seeds, scale and placement.
Detail, roughness and lacunarity.
Worley and Voronoi functionality.
Phasor parameters.
Distortion and jaggedness.
Gradient calculations.
Curl calculations.
Tileability.
GPU implementation.

The differences are their output defaults and preview behavior.

Creation presets
Property	Noise preset	Flow preset
Generator implementation	Noise V2	Noise V2
Display name	Noise	Flow
Write Height	ON	OFF
Write Flow	OFF	ON
Compute scalar noise	Yes	Yes
Compute directional information	Available	Yes
Selected preview	Material	Arrows
Manual output preview	Preview key	Preview key

Both outputs can be enabled simultaneously.

A Flow node can write height if the user wants.

A Noise node can contribute Flow if the user wants.

These toggles change what is written, not which algorithms are available.

Important distinction

Even when Write Height is OFF, the internal scalar noise remains available for calculating gradients and directional variation.

Likewise, Write Flow OFF does not mean the node loses its Gradient or FlowDirection outputs.

It means the node does not add its Flow to the working field.

This also preserves Noise's existing ability to publish fields for other functionality.

3. Noise / Flow Inspector

The Inspector should be organized into clear sections.

A. OUTPUT
OUTPUT
────────────────────────────
Height                 [✓]
Flow                   [ ]

Height Scale           1.00
Height Bias            0.00
Normalize              [✓]

Write Height and Write Flow must be independent.

Height-specific controls should only appear when Height output is enabled.

Flow-specific controls should only appear when Flow output is enabled, or when editing its directional information through the existing preview controls.

The underlying scalar-noise settings remain available in both presets.

B. PATTERN

Reuse Noise V2.

PATTERN
────────────────────────────
Noise Type             Gradient

Seed                   1
Scale                  8.00
Detail                 4
Roughness              0.50
Lacunarity             2.00

Offset X               0.00
Offset Y               0.00

Keep the existing family-specific controls.

Do not expose irrelevant disabled parameters. Continue the Noise V2 convention of hiding parameters that a selected algorithm does not support.

The Noise Type selector can remain because it selects an algorithm, not another node or source.

C. MODE — Direction Generation

This is the new weighted system.

No source dropdown.

MODE
────────────────────────────
Height                 1.00
Slope                  0.00

Curl                   0.00
Constant               0.00

Angle                  0°
Strength               1.00

Each value represents a contribution to the generated Vector2 field.

Height

Uses the node's own noise height.

It calculates its gradient and produces directional vectors.

A value of 1 means full contribution from the node's own noise gradient.

Slope

Uses the current accumulated surface height.

The result follows the surface's downhill direction.

This is different from Height:

Height = this node's internal noise gradient.
Slope = the existing surface's gradient.

For example, a Flow node beneath Rock Formation could follow the rock's slopes rather than the Flow node's internal noise.

Curl

Generates rotational directional variation.

Use the existing Noise V2 curl functions.

This should not duplicate the curl implementation.

Constant

Adds a uniform directional vector.

Direction comes from Angle, with magnitude controlled by Constant.

For example:

Angle                  90°
Constant               1.00

Produces uniform directional movement.

Weighted combination

Conceptually:

Own    = DownhillGradient(OwnNoiseHeight);
Slope  = DownhillGradient(CurrentHeight);
Curl   = CurlNoise(UV);
Const  = float2(cos(Angle), sin(Angle));

GeneratedFlow =
    HeightWeight   * Own
  + SlopeWeight    * Slope
  + CurlWeight     * Curl
  + ConstantWeight * Const;

GeneratedFlow *= Strength;

Use consistently scaled directional bases so increasing Noise Scale does not accidentally multiply movement strength.

Preserve the magnitude of the final combined vector.

Do not normalize the combined result, because that would destroy the weights and cancellation between opposing directions.

A zero vector must remain an exact zero.

All calculations must remain tileable.

4. Flow Composition

Multiple Flow nodes must stack.

We need additive contribution and interpolation without introducing another source-selection interface.

COMPOSITION
COMPOSITION
────────────────────────────
Add                    1.00
Mix                    0.00

Strength               1.00

Two controls are sufficient.

Add

Adds the generated vector to the existing working Flow.

FlowOut = FlowIn + GeneratedFlow * Add;
Mix

Interpolates between existing Flow and the generated Flow.

FlowOut = lerp(FlowIn, GeneratedFlow, Mix);

Both can operate together.

With a child mask, the combined formula is:

float MixWeight = saturate(Mix * Mask);
float AddWeight = Add * Mask;

FlowOut =
    lerp(FlowIn, GeneratedFlow, MixWeight)
    + GeneratedFlow * AddWeight;

This produces predictable results:

Add	Mix	Result
1	0	Add generated vectors
0	1	Replace existing vectors
0	0.5	Interpolate halfway
0	0	Preserve previous Flow

A child mask modulates the new contribution without erasing the existing field outside the mask.

No blend dropdown is required.

Stacking example
Noise
│
├── Flow — Directional
│   Add: 1
│   Mix: 0
│
├── Flow — Curl
│   Add: 0.5
│   Mix: 0
│
├── Flow — Surface Slope
│   Add: 0.3
│   Mix: 0
│
└── Distort

Distort receives the accumulated result.

No manual connection.

5. Automatic Field Inheritance

Every Generator maintains working fields during evaluation.

Conceptually:

struct FGeneratorWorkingFields
{
    Height;
    Flow;
    Coverage;
    RegionIds;
    Boundary;
    NamedOutputs;
};

This is a conceptual runtime structure, not a requirement to replace the existing RDG bundle with precisely these members.

Initial state

A generator creates its native fields.

Noise already calculates Value, Gradient and FlowDirection.

Other generators may produce height and boundaries without a dedicated Flow output.

Where no native Flow exists, a valid height can provide a downhill-flow fallback.

Otherwise, Flow begins at zero.

The fallback must not invent motion on a flat field.

Behavior execution

Every child reads the working fields resulting from earlier siblings.

Generator produces initial fields
              ↓
       Flow modifies vectors
              ↓
       Flow modifies vectors
              ↓
       Push modifies height
              ↓
       Distort reads Flow
              ↓
       Deform reads Flow
              ↓
       Final generator output

Each operation sees preceding changes.

It must not read values produced by later siblings.

This makes execution deterministic and avoids feedback loops.

Height and Flow dependencies

When a Flow node uses Slope, it reads the height available at that position in the stack.

If Push precedes it, the slope includes Push.

If Push follows it, the Flow uses the earlier height.

That makes ordering meaningful.

Prevent double application

A Generator's intrinsic FlowDirection is the initial Flow field.

Enabling Write Flow on that generator must not add that same initial field to itself a second time.

Write Flow controls the node's contribution to its containing evaluation context.

6. Behavior Definitions

These operations must retain distinct contracts.

Push

Purpose: Vertical signed-height modification.

Reads:

Current native height.
Its own parameters.
Child mask.

Writes:

Height only.

Example:

HeightOut = HeightIn + HeightDelta * Strength * Mask;

When operating on the parent's own height, HeightDelta can be that native height, making Push act as signed relief amplification.

Push does not implicitly transport anything along Flow.

We should not call it a flow operation.

Parameters

Strength, field-composition controls where applicable, and child masks.

No source selector.

Distort (current Warp)

Purpose: Distort the complete procedural pattern.

Reads:

Current Flow.
Current generator output bundle.
Child mask.

Writes or remaps:

Height.
IDs.
Coverage.
Relevant spatial fields.

Conceptually:

UV = TraceFlow(Flow, Strength, Scale);
Output = SampleEntireBundle(UV);

Use appropriate coordinate integration, including the existing RK2 implementation where needed.

Parameters

Strength, Scale or Trace Length, Integration Steps when tracing is active, and child masks.

The implementation must preserve tile seams and lifted UV winding.

Deform

Purpose: Distort relief while preserving companion fields.

Reads:

Current Height.
Current Flow.
Child mask.

Writes:

Height only.
UV = TraceFlow(Flow, Strength, Scale);
HeightOut = SampleHeight(UV);

Unlike Distort, it does not move the Region IDs or coverage.

This is directional relief deformation, not necessarily wrinkle generation.

Parameters

Strength, Scale or Trace Length, Integration Steps when required, and child masks.

Carve / Deposit

Purpose: Remove or add relief using a distance boundary or directional height gathering.

Reads:

Current Height.
Boundary/SDF, where available.
Flow for the traced variant.
Child mask.

Writes:

Height.
Optional Carve Mask diagnostic output.

The signed-boundary algorithm and existing traced groove/deposit algorithm are different modes of operation. Their functionality should not be silently discarded.

No arbitrary conversion from ordinary Noise height to an SDF is permitted.

If a required boundary does not exist, the operation must report a meaningful unavailable state rather than doing something unrelated.

7. Behavior Hierarchy Rules

With Flow becoming a Noise Generator preset, the structural hierarchy becomes simpler.

Allowed
Generator
├── Flow
│   └── Noise Mask
├── Flow
│   └── Mask
├── Push
│   └── Mask
├── Distort
│   └── Mask
├── Deform
│   └── Mask
└── Carve
    └── Mask

Flow is a scoped Generator child.

Push, Distort, Deform and Carve are Behaviors.

Masks belong to their immediate owner.

Disallowed
Push
└── Distort

Deform
└── Push

Flow
└── Distort

Behavior-under-Behavior nesting is no longer necessary.

Previously implemented nested Flow behavior support should be removed as part of the consolidation.

The only nesting we retain is nesting with a clear functional meaning, primarily Masks and their existing filters.

Dragging

Preserve drag-to-reparent for compatible children.

Center drop: change parent.
Edge drop: reorder.
Masks can move between compatible owners.
Behaviors remain siblings under a Generator.
Flow Generator presets can reorder alongside Behaviors.

No “Move to Layer” menu.

No reparenting controls in the Inspector.

8. Viewport Flow Preview

Selecting a Flow preset must automatically display arrows.

This is a required feature, not optional polish.

Flow selected

The normal material remains visible.

Directional arrows appear over it.

→ → ↗ ↑ ↑
→ ↗ ↑ ↖ ←
↓ ↘ → ↗ ↑
↓ ↓ ↙ ← ←
Visualization semantics
Visual property	Meaning
Arrow direction	Flow direction
Arrow length	Flow magnitude
Arrow visibility/opacity	Influence and validity
Arrow density	Adjustable, zoom-aware
Material underneath	Remains visible

Default arrows should visualize the accumulated Flow after the selected node, not a later node's result.

This makes editing stacked Flow nodes understandable.

Preview rules
Selection	Default viewport
Noise preset	Material
Flow preset	Material + arrows
Push	Material
Distort	Material
Deform	Material
Mask	Existing mask behavior

The existing preview key continues to expose individual published outputs.

Noise users can inspect Flow manually through that mechanism.

Flow users see arrows immediately.

The preview should support pinning while editing another operation.

Technical requirements

Do not use continuous CPU readback of a full-resolution flow texture.

Use a GPU-based or efficiently sampled overlay.

Arrows should remain meaningful at different resolutions and viewport zoom levels.

For 3D preview meshes, visualization must follow the material's UV mapping rather than displaying unrelated screen-space arrows.

Selection previews must not change the evaluated material.

9. Field Representation

This needs an explicit cleanup before the system is consolidated.

The current implementations use different conventions.

Noise's FlowDirection currently publishes a normalized downhill direction and validity.

The Behavior Flow solver packs direction, signed distance and influence into a float4.

These are not identical contracts.

Required canonical representation

Define one shared vector-field contract.

Conceptually:

Flow.VectorXY
Flow.Influence
Flow.Validity

The vector contains magnitude.

Influence and validity are separate concepts.

VectorXY: Actual movement direction and magnitude.
Influence: Spatial attenuation.
Validity: Whether the field contains meaningful directional data.

Signed distance should remain an independent field.

Do not overload it as generic Flow magnitude.

Do not reinterpret arbitrary Vector2 outputs as Flow without an explicit conversion algorithm.

Field production

The following calculations should be reusable:

Noise Scalar
    │
    ├── Raw Value
    ├── Signed Height
    ├── Gradient
    ├── Curl
    ├── Flow
    └── IDs (supported families)

One shared implementation should serve Noise, Flow and inline Noise Masks.

Calculate expensive outputs only when demanded, except where immediate preview or active evaluation requires them.

10. Correct GPU Evaluation Order

There is an existing implementation issue we must resolve.

Currently, Noise's FlowDirection publication happens after its post-generation Behavior evaluation.

That is too late for the automatic Flow inheritance we want.

Required order
1. Evaluate Generator native fields
         ↓
2. Calculate intrinsic Gradient / Flow
         ↓
3. Initialize working Height / Flow
         ↓
4. Evaluate scoped children in order
         │
         ├── Flow: modify working Flow
         ├── Push: modify Height
         ├── Distort: warp complete bundle
         ├── Deform: warp Height only
         └── Carve: modify Height
         ↓
5. Resolve final signed Height
         ↓
6. Apply existing normalization / scale contract
         ↓
7. Publish final outputs

There is one subtlety: Distort can change the spatial frame of the height and vector fields.

We must transport or regenerate directional data consistently after a coordinate remap.

Otherwise, later Deform nodes could consume directions in the wrong coordinate frame.

Height normalization

Preserve the established behavior:

Noise normalization keeps its existing centering rules.
Other generators retain their existing zero-preserving signed normalization contract.
Normalize OFF preserves raw signed height.
Write Height OFF must produce no height contribution.
Flow calculations must not accidentally trigger height normalization or add hidden relief.

This is particularly important for a Flow-only Noise preset.

11. Inspector Cleanup

Remove Inspector-based source and target selection from this system.

That includes the remaining:

Current UI	Required change
Push: Signed Height Field	Remove source picker; use parent working height
Warp: Direction Field	Remove source picker; use accumulated Flow
Deform: Direction Field	Remove source picker; use accumulated Flow
Carve: Signed Boundary / SDF	Remove connection picker; use the owner's compatible boundary
Flow: Steering Source	Replace with weighted MODE controls
Traced Warp/Deform/Carve: Steering Source	Remove; consume working fields automatically
Behavior: Influence Field	Keep removed; use child Masks

Remove the corresponding obsolete menu builders and declarations when they have no remaining users.

Do not only hide the controls.

The runtime must no longer depend on those manually selected connections for normal Behavior evaluation.

Otherwise, we would create a simpler interface with nonfunctional operations behind it.

What remains in the Inspector

Only operation settings.

For example:

DISTORT
────────────────────────
Strength          1.00
Scale             0.10
Steps               16

MASK
Controlled by children

No source list.

No target list.

No “Choose source later.”

No redundant connection badges.

Existing published outputs can remain accessible through the preview/copy system, but they should not require Inspector routing for these operations.

12. Creation Menus and Defaults

The creation menu should present Noise and Flow independently.

ADD GENERATOR
────────────────────
Rock Formation
Cracks
Pebbles
Cliff Strata
Strata Carver
Noise
Flow

Both Noise and Flow instantiate EMixtormatGeneratorType::Noise.

They receive different initial settings.

Proposed preset defaults
Parameter	Noise	Flow
Write Height	ON	OFF
Write Flow	OFF	ON
Height weight	1.0	0.0
Slope weight	0.0	0.0
Curl weight	0.0	1.0
Constant weight	0.0	0.0
Direction Strength	1.0	1.0
Add	1.0	1.0
Mix	0.0	0.0
Auto arrows	OFF	ON

These are proposed defaults, not values already implemented.

The Flow preset starts with curl variation so it immediately produces a useful directional pattern instead of simply doubling the parent Noise's downhill field.

Users can set Curl to zero and raise Height or Slope to generate gradient-following Flow.

Preset identity

Creation preset identity should persist independently of output toggles.

If a user creates Flow and later enables Height, its name and automatic arrow-preview behavior should not unexpectedly change.

Both presets still use the same Generator implementation.

A small authoring-role property is sufficient; no second Noise engine is needed.

13. Implementation Files

The following are the principal files to modify.

File	Responsibility
Source/MixtormatRuntime/Public/MixtormatGeneratorTypes.h	Noise output flags, direction weights, composition, preset identity
Source/MixtormatRuntime/Public/MixtormatBehaviorTypes.h	Simplify Behavior and Flow definitions
Source/MixtormatRuntime/Private/MixtormatChildScope.cpp	Hierarchy validity and automatic ownership
Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerChildren.cpp	Allow scoped Flow Generator placement; remove obsolete recursive Behavior placement
Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerActions.cpp	Noise/Flow creation presets and defaults
Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerMenus.cpp	Creation and context menus
Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorGenerators.cpp	Weighted MODE controls, output toggles, source-picker removal
Source/MixtormatShaders/Private/MixtormatGpuNoisePasses.cpp	Shared Noise/Flow GPU dispatch
Source/MixtormatShaders/Private/Compositing/MixtormatNoiseRender.h	GPU render data
Source/MixtormatShaders/Private/Compositing/MixtormatGeneratorGather.cpp	Gather new fields and ordered evaluation data
Source/MixtormatShaders/Private/MixtormatGpuGeneratorPasses.cpp	Working Flow accumulation, Behavior execution
Shaders/Private/MixtormatNoise.usf	Noise/Gradient/Flow outputs
Shaders/Private/MixtormatNoiseV2.ush	Reuse Curl and Noise helpers
Shaders/Private/MixtormatBehaviorWarp.usf	Push, Distort, Deform and Carve execution
Source/MixtormatEditor/Private/Widgets/MixtormatChildCapabilities.cpp	Published outputs and preview capabilities
Source/MixtormatEditor/Private/Widgets/SMixtormatPreviewViewport.cpp	Directional arrow overlay
Source/MixtormatEditor/Private/Widgets/SMixtormat_Preview.cpp	Selection preview and preview-key behavior

Also inspect the existing hierarchy drag/drop, clipboard, effective-group expansion, instance handling and JSON parameter serialization paths.

Do not create a replacement framework without first checking where the existing functionality can be reused.

14. Implementation Order
P0 — Establish one field contract

Required before feature work.

Define the canonical Flow representation.
Separate direction/magnitude from influence and validity.
Define height and vector output flags.
Define the weighted direction-generation formula.
Define Add/Mix composition and zero-strength behavior.
Define how working fields are inherited.
Define Noise/Flow preset identity.
Remove contradictory design assumptions from the Behavior V2 plan.

Completion condition: One documented contract for every Generator and Behavior to consume.

P1 — Implement Noise/Flow presets
Extend FMixtormatNoise.
Add Write Height and Write Flow.
Add Height, Slope, Curl and Constant weights.
Add Angle, Strength, Add and Mix.
Add a Flow creation preset.
Reuse Noise V2 algorithms.
Support Flow-only operation without writing height.
Preserve Noise Mask evaluation.
Add serialization and correct defaults.

Completion condition: Noise and Flow share an implementation and produce the correct independently enabled outputs.

P2 — Implement automatic Flow accumulation
Initialize native working fields.
Publish intrinsic Flow early enough for Behaviors.
Evaluate Flow children in authored order.
Combine their vectors using Add/Mix.
Apply child masks to contributions.
Make Distort and Deform consume the accumulated field.
Make Slope read the current earlier Height.
Preserve vector magnitudes and coordinate frames.
Prevent read-after-write feedback.
Preserve Height when all Flow nodes have Write Height OFF.

Completion condition: Multiple Flow nodes modify one shared working field, and subsequent Behaviors use it automatically.

P3 — Simplify Behaviors and hierarchy
Keep Push as signed-height modification.
Keep Distort as complete-bundle transport.
Keep Deform as height-only transport.
Preserve Carve and Deposit functionality.
Remove Behavior-under-Behavior requirements.
Support Flow Generator children as siblings of Behaviors.
Preserve nested Masks and mask filters.
Remove redundant Flow Field Behavior authoring.
Remove obsolete Flow computation duplicates.
Fix drag/reparent, copy/paste and instances for the new structure.

Completion condition: The hierarchy works without manual field connections.

P4 — Rebuild the Inspector
Add independent output toggles.
Implement weighted MODE controls.
Implement Add/Mix controls.
Hide irrelevant controls dynamically.
Remove all Behavior source/target dropdowns.
Remove the associated unused source-picker code.
Use parent fields automatically in runtime.
Keep existing parameter drivers working for numeric controls.
Ensure every exposed parameter actually affects evaluation.

Completion condition: No source or target routing is necessary anywhere in these Inspector panels.

P5 — Viewport arrows
Detect selection of the Flow preset.
Display arrow visualization automatically.
Visualize the accumulated field after that selected Flow.
Encode vector magnitude and validity.
Apply child-mask influence visually.
Support zoom-aware sampling.
Support the existing preview key.
Support pinning.
Keep material visualization visible beneath arrows.
Ensure selecting ordinary Noise does not force arrows on.

Completion condition: Every active Flow node can be inspected visually without enabling a separate preview mode.

P6 — Cleanup and validation

Remove dead code rather than maintaining parallel systems.

Because this branch has no authored production material that requires migration, there is no reason to retain obsolete authoring interfaces solely for compatibility with nonexistent content.

However, preserve required reflected enum identities and serialized field contracts wherever changing them would cause technical corruption.

Review the legacy Flow implementations carefully before deleting them. Preserve algorithms that have no equivalent in the new pipeline.

Update:

AgentDocs/BEHAVIOR_V2.md
AgentDocs/NOISE_V2.md
Relevant architecture and evaluation-order documentation.

Do not mark anything validated without running it.

15. Acceptance Tests

The implementation is complete only when these behaviors are verified.

Test	Expected result
Create Noise	Height output enabled; normal preview
Create Flow	Height disabled; Flow enabled
Select Flow	Arrows automatically visible
Select Noise	Arrows not automatically visible
Preview Noise Flow	Existing preview mechanism displays its vectors
Disable Height on Noise	Height contribution becomes zero
Enable Height on Flow	Both outputs contribute correctly
Flow with Height weight	Follows its internal noise slopes
Flow with Slope weight	Follows the existing surface slopes
Flow with Curl weight	Produces rotating vectors
Flow with Constant weight	Produces uniform directional vectors
Flow A + Flow B	Contributions accumulate
Flow Mix = 1, Add = 0	Replaces preceding field inside the mask
Flow Add = 0, Mix = 0	Exact identity
Mask beneath Flow	Attenuates that Flow contribution only
Mask beneath Distort	Attenuates Distort, not the working Flow
Distort after Flow	Uses accumulated Flow
Deform after Flow	Uses accumulated Flow; leaves IDs fixed
Push after Flow	Changes height without implicit horizontal transport
Flow after Push, Slope enabled	Uses the modified height
Reorder Flow nodes	Evaluation changes predictably
Flat height, slope-only Flow	Zero vector, no invalid values
Tile edges	Seamless scalar and vector fields
Undo/redo	Restores settings, hierarchy and results
Save/reload	Presets and output toggles persist
Clipboard/instance/group	Ownership and evaluation remain correct

Additionally, verify Unreal 5.8 UHT compilation, C++ compilation, GPU shader compilation, preview rendering, all Noise families, masks, parameter drivers and performance.

16. What We Are Explicitly NOT Building

This refactor must not grow into another connection system.

Out of scope:

Inspector source dropdowns.
Inspector target dropdowns.
Manual Flow-to-Behavior connections.
A second Noise engine.
A separate duplicated Flow solver.
Arbitrary Behavior nesting.
A node graph.
Elevation or 3D physical-force simulation.
Automatic physical erosion.
A new generic Vector2 routing framework.
Maintaining old and new Flow implementations in parallel indefinitely.

Existing typed outputs, Instances, Sources shelf and published-output infrastructure should remain available for their other established purposes. We are removing the need to manually wire these Generator/Behavior operations, not deleting unrelated functionality.

17. Final User Experience

A practical example:

GENERATOR — Rock Formation
│
├── Flow — Curl
│   Strength: 0.4
│   Add: 1.0
│   └── Noise Mask
│
├── Flow — Downhill
│   Slope: 1.0
│   Strength: 0.7
│   Add: 0.5
│
├── Distort
│   Strength: 0.25
│   Scale: 0.08
│
├── Deform
│   Strength: 0.15
│   Scale: 0.04
│   └── Noise Mask
│
└── Push
    Strength: 0.20

The experience:

Click Flow: See arrows and edit their directions.

Add another Flow: Combine another field without connections.

Drag Flow above or below another Flow: Change accumulation order.

Click Distort: Adjust how strongly the existing Flow warps the material.

Click Deform: Adjust relief-only displacement.

Add a Noise Mask beneath either: Control spatial influence.

Click Push: Adjust vertical relief.

No connections need to be authored.

Final architectural decision

Noise and Flow are two presets of one field Generator.

Flow is data production, not deformation.

Push, Distort, Deform and Carve are operations, not field generators.

Masks control the influence of their immediate owner.

Fields inherit automatically through hierarchy and execution order.

MODE uses weighted Height, Slope, Curl and Constant contributions—not source dropdowns.

Selecting Flow automatically displays its vectors.