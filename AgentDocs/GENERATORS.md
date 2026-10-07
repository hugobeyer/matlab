# Generators

A generator rewrites a layer's **input height** before anything reads it, so the
layer composites as though the carved surface were authored. It is neither an
effect (post-composite filter) nor a mask (0..1 coverage).

## Registration / taxonomy

| Thing | File |
|---|---|
| `EMixtormatGeneratorType` | `Runtime/Public/MixtormatGeneratorTypes.h` (~L15) |
| `FMixtormatGenerator` (payload union) | `MixtormatGeneratorTypes.h` (~L853) |
| Per-generator structs | `FMixtormatStrataCarver`, `FMixtormatCracks`, `FMixtormatRockFormation`, `FMixtormatPebbles`, `FMixtormatCliffStrata`, `FMixtormatNoise` — all in `MixtormatGeneratorTypes.h` |
| Generator height sublayers | `FMixtormatGeneratorHeightBlend`, `FMixtormatGeneratorHeightCurve`, `FMixtormatGeneratorHeightColorRamp`, `FMixtormatGeneratorHeightPush` — `MixtormatGeneratorTypes.h` |
| `MixtormatCanOwnGeneratorFlow` | `MixtormatGeneratorTypes.h` (~L34) |

Current types: `StrataCarver`, `Cracks`, `RockFormation`, `Pebbles`,
`CliffStrata`, `Noise`. Enum is serialized by value — **append, never reorder**.

Generator-owned flow tools (`ShapeDeform`, `GeneratorFlow`, `FlowCarve`) live in
`EMixtormatEffectType` (`MixtormatEffect.h`), not here. They are valid only
scoped under a generator that can own them (`MixtormatCanOwnGeneratorFlow`:
Strata Carver, Rock Formation, Pebbles, Cracks) and rewrite its height before
combine. See `code_docs/generator_flow_interaction_audit.md`.

## Height Push (structural module)

`HeightPush` is an independent, append-only child type, not Height Follow, Height Blend,
Flow Carve or a coordinate warp. Author it as **source generator → Height Push → target
Strata generator**. Sources may also be generators in earlier layers; the typed `Height`
output is the completed signed module result after its tools, normalization and Height Scale.
The target is a later, enabled Strata generator in the same layer. Group placements and
other target kinds are not enabled yet; unavailable choices remain disabled.

`Amount` maps signed source height to a bedding-coordinate shift (one unit = one bed period).
Zero amount, disabled modules or missing/invalid connections do nothing; there is no implicit
source or target. Scoped masks gate only the push. Multiple pushes accumulate at their own
ordered positions before the target runs. The Strata shader applies the shift before folds
and chains its destination-space gradient into filtering; bed IDs/position/random are
regenerated together with relief. Existing composite-below Height Follow is unchanged.

Path: Runtime child/payload → `GatherGeneratorHeightModuleChild` →
`MixtormatGeneratorHeightPush.usf` → `MixtormatStrataCarver.usf` →
`BuildHeightPushControls` (source, target, amount, enable). Implementation added; build,
shader compilation and visual validation of Height Push have not been run by the agent.
Independent Warp-module ordering remains future work.

## Defaults / parameter metadata

- Compiled defaults: the struct initializers in `MixtormatGeneratorTypes.h`.
- UI ranges: `meta = (UIMin/UIMax/Delta)` on the UPROPERTYs, read by
  `Editor/Private/UI/Parameters/MixtormatParameterUiMeta.*`.
- Authoring overrides: `Config/MixtormatParameterAuthoring.json` via
  `MixtormatParameterAuthoring.*`.
- Hard bounds / sanitize: `Runtime/Public/MixtormatParameterDefinition.h`.

## Gather

`Shaders/Private/Compositing/MixtormatGeneratorGather.cpp`

- `GatherGeneratorChild` — switch on `Generator.Type`; resolves settings into
  `FGeneratorRenderData`; applies `PlacementKey` to `FieldKey` for cached types.
- `GatherGeneratorHeightModuleChild` — Height Blend / Height Curve / Height
  Color Ramp / Height Push sublayers.

## GPU dispatch

`Shaders/Private/MixtormatGpuGeneratorPasses.cpp`

- `AddGeneratorLayerPasses` — once per Generator layer; walks module children in
  authored order. Each generator builds native height, runs its flow tools, then applies
  shared signed normalization / Height Scale. Shared bundle stage 9 adds module height
  to running height without coverage gating; explicit Height Blend sublayers are separate.
  The final running height becomes `LayerCtx.LayerInputHeight`.
- Dispatch switch (~L2378): `AddStrataCarverPasses`, `AddRockFormationPasses`,
  `AddPebblesPasses`, `AddCracksPasses`, `AddCliffStrataPasses`, Noise.
- `AddGeneratorModuleCombine` (~L780).
- Distance solves run on a capped grid (`DistanceSolveMaxSize = 1024`).

## Shaders

`Shaders/Private/`: `MixtormatStrataCarver.usf`, `MixtormatCracks.usf`,
`MixtormatRockFormation.usf`, `MixtormatPebbles.usf`, `MixtormatCliffStrata.usf`,
`MixtormatNoise.usf`, `MixtormatGeneratorBundle.usf`, `MixtormatGeneratorFlow.usf`,
`MixtormatGeneratorHeightBlend.usf`, `MixtormatGeneratorHeightCurve.usf`,
`MixtormatGeneratorHeightColorRamp.usf`, `MixtormatGeneratorHeightPush.usf`,
`MixtormatGeneratorHeightModules.ush`,
`MixtormatGeneratorPlacement.ush`.

## Inspector

`Editor/Private/Widgets/Inspector/MixtormatInspectorGenerators.cpp` —
`SMixtormat::Build*Panel` and `BuildGeneratorFlowControls`.

## Published outputs

`Editor/Private/Widgets/MixtormatChildCapabilities.cpp` — `GetChildCapabilities`
is the single place a generator's previewable/copyable outputs are declared.
