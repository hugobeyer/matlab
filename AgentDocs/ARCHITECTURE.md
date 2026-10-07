# Architecture

## Modules

| Module | Type | Location | Responsibility |
|---|---|---|---|
| `MixtormatRuntime` | Runtime | `Source/MixtormatRuntime` | Data model, UObjects, parameter contract, identity/remap. No GPU, no Slate. |
| `MixtormatShaders` | Editor-only | `Source/MixtormatShaders` | Gather, RDG GPU compositor, compute passes. |
| `MixtormatEditor` | Editor-only | `Source/MixtormatEditor` | Slate UI, hierarchy, inspector, preview, bake, style. |

Declared in `Mixtormat.uplugin`. Dependency direction is one-way and enforced:
**Runtime ← Shaders ← Editor**. Never include Editor from Runtime or Shaders.

## Data flow

```
UMixtormatMaterial (asset)
  └─ TArray<FMixtormatLayer>            authored stack
       └─ TArray<FMixtormatLayerChild>  masks / IDs / generators / effects
  └─ TArray<FMixtormatLayerGroup>       groups (expanded to layers before render)

Editor (SMixtormat)
  → MixtormatLayerGroups::BuildEffectiveLayers   flatten groups
  → MixtormatParameterBinding::ResolveChildInstances / ApplyDirectReferences
  → FMixtormatGpuCompositor::RequestCompose
       → gather (Compositing/Mixtormat*Gather.cpp)  authored → render data
       → EnqueueCompose (MixtormatGpuComposePipeline.cpp)  RDG passes
       → render targets: BaseColor / Normal / RAM / Height / Debug
  → BindOutputs / preview viewport / FMixtormatBakeService
```

## Compose pipeline order (per layer)

```
Layer source height
  → AddLayerInputPass              resolve into output space
  → AddLayerHeightSmoothPasses     optional pre-smooth
  → AddGeneratorLayerPasses        Generator layers only; rewrites input height
  → mask children, effects, AddLayerCompositePass
  → post-composite filters (Erosion, Breakup, Worn Edges, Grade, LayerBlur, Runoff)
→ AddFinalAOPass / AddFinalNormalPass   document-level, once
```

Generators rewrite the layer's **input** height before anything reads it.
Effects are filters over the already-composited layer. Masks produce 0..1
coverage and join the mask chain. See `COMPOSITION.md` and `GENERATORS.md`.

## Canonical types (Runtime)

The runtime data model is split by domain. `MixtormatMaterial.h` is now a small
asset/aggregation header; it includes `MixtormatLayerTypes.h`, which pulls in the
rest. Dependency flow:

```
ParameterTypes  HeightTypes  MaskTypes  IdTypes  GeneratorTypes  Effect(types)
        \            |           |          |           /
         +-----------+-----------+----------+----------+
                              |
                         LayerTypes
                              |
                      MixtormatMaterial
```

| Type | File | Notes |
|---|---|---|
| `FMixtormatLayer` | `MixtormatLayerTypes.h` | one stack entry; `LayerId`, children, height/UV/feature fields |
| `FMixtormatLayerChild` | `MixtormatLayerTypes.h` | union-ish payload: Mask/Effect/Generator/Id/… by `Type` |
| `FMixtormatLayerGroup` | `MixtormatLayerTypes.h` | group; shared children broadcast onto members |
| `UMixtormatMaterial` | `MixtormatMaterial.h` | the asset; `Layers`, `LayerGroups`, baked outputs |
| `FMixtormatFinalSettings` | `MixtormatMaterial.h` | document-level final AO/normal settings |
| `EMixtormatLayerChildType` | `MixtormatLayerTypes.h` | child taxonomy (append-only) |
| `EMixtormatEffectType` | `MixtormatEffect.h` | effect taxonomy (append-only) |
| `FMixtormatLayerEffect` | `MixtormatEffect.h` | per-child effect payload |
| `EMixtormatGeneratorType` | `MixtormatGeneratorTypes.h` | generator taxonomy (append-only) |
| `FMixtormatGenerator` | `MixtormatGeneratorTypes.h` | generator payload union |
| `EMixtormatHeightOp`, `FMixtormatHeightBlend` | `MixtormatHeightTypes.h` | shared height-combine block |
| `FMixtormatMaskLayer`, `FMixtormatGeneratedMask`, `FMixtormatColorIdMask`, `FMixtormatRandomIdMask`, `FMixtormatCraquelure` | `MixtormatMaskTypes.h` | mask payloads |
| `FMixtormatClusterFilter`, `FMixtormatPatternFilter`, `FMixtormatIdGroup`, … | `MixtormatIdTypes.h` | ID pipeline payloads |
| `FMixtormatParameterAddress`, `FMixtormatParameterBinding` | `MixtormatParameterTypes.h` | parameter/reference/driver infrastructure |
| `FMixtormatParameterContract` | `MixtormatParameterDefinition.h` | sparse hard bounds / normalization / saturate |
| `FMixtormatOutputReference` | `MixtormatOutputReference.h` | published-field reference |
| `FMixtormatMaskShaping` | `MixtormatMaskShaping.h` | shared mask shaping block (embed) |

## Boundaries

- **Runtime** owns identity (`FGuid`), serialization, sanitization, and the
  parameter contract. It must not know about Slate, RDG, or shader files.
- **Shaders** owns gather + GPU. It reads Runtime types and writes render
  targets. It must not include Editor headers.
- **Editor** owns Slate, menus, dialogs, bake, style. It calls Runtime helpers
  and the compositor; it does not own document mutation rules.

## Related

- `FILE_MAP.md` — task → files, symbol → owner
- `COMPOSITION.md` — gather, passes, caching, references
- `SHADERS.md` — shader dirs and class↔file map
