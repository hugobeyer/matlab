# Architecture

Stable model/module documentation checked against source on 2026-10-24.
Moss and GPU-pass internals are under active rewrite and were not re-audited;
pass filenames/order below are navigation context, not a validation of that work.

## Modules

| Module | Type | Location | Responsibility |
|---|---|---|---|
| `MixtormatRuntime` | Runtime | `Source/MixtormatRuntime` | Data model, UObjects, parameter contract, identity/remap. No GPU, no Slate. |
| `MixtormatShaders` | Editor-only | `Source/MixtormatShaders` | Gather, RDG GPU compositor, compute passes. |
| `MixtormatEditor` | Editor-only | `Source/MixtormatEditor` | Slate UI, hierarchy, inspector, preview, bake, style. |

Declared in `Mixtormat.uplugin` and the module Build.cs dependencies. Required direction:
**Runtime ← Shaders ← Editor**. Never include Editor from Runtime or Shaders.

## Data flow

```
UMixtormatMaterial (asset)
  └─ TArray<FMixtormatLayer>            authored stack
       └─ TArray<FMixtormatLayerChild>  masks / IDs / generators / effects
  └─ TArray<FMixtormatLayerGroup>       groups (expanded to layers before render)

Editor (SMixtormat)
  → FMixtormatGpuCompositor::RequestCompose
       → prepare effective layers: group expansion, instances and direct references
       → gather (Compositing/Mixtormat*Gather.cpp)  authored → render data
       → EnqueueCompose (MixtormatGpuComposePipeline.cpp)  RDG passes
       → outputs: BaseColor / Normal / RAM / Height + optical maps / diagnostics
  → BindOutputs / preview viewport / FMixtormatBakeService
```

## Compose pipeline order (per layer)

```
Layer source height
  → AddLayerInputPass              resolve into output space
  → AddLayerHeightSmoothPasses     optional pre-smooth
  → ordered procedural modules    Material or Fill layers with generator-chain children
  → mask children, effects, AddLayerCompositePass
  → post-composite filters (Erosion, Breakup, Worn Edges, Grade, LayerBlur, Runoff)
→ AddFinalAOPass / AddFinalNormalPass   document-level, once
```

The authored layer kinds are **Material** and **Fill**, not a separate Generator type.
`FMixtormatLayer::HasGeneratorModules()` detects children handled by the procedural
chain, including generators, behaviors and height modules. Effects and scoped masks
have owner/stage-specific scheduling; not every effect runs after composition.
See `COMPOSITION.md` and `GENERATORS.md`; recheck detailed scheduling after the GPU rewrite.

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
| `EMixtormatLayerChildType` | `MixtormatLayerTypes.h` | serialized child taxonomy; preserve active numeric values |
| `EMixtormatEffectType` | `MixtormatEffect.h` | serialized effect taxonomy; preserve active numeric values |
| `FMixtormatLayerEffect` | `MixtormatEffect.h` | per-child effect payload |
| `EMixtormatGeneratorType` | `MixtormatGeneratorTypes.h` | serialized generator taxonomy; preserve active numeric values |
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
- **Editor** owns Slate, menus, dialogs, bake, style and working-document edit/history
  orchestration. Runtime helpers own shared model/reference rules.
- Pre-release retirement requires approval and updated consumers, per `AGENTS.md`;
  serialized enums are not an absolute prohibition on approved obsolete-entry removal.

## Related

- `FILE_MAP.md` — task → files, symbol → owner
- `COMPOSITION.md` — gather, passes, caching, references
- `SHADERS.md` — shader dirs and class↔file map
