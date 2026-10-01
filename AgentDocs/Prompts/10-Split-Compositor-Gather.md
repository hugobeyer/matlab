# Step 10: split the compositor gather (pure move, no behaviour change)

Paste `00-Shared-Rules.md` above this. Run it **after Step 3a**, once the generator children are deleted. Nothing else may edit `MixtormatGpuCompositor.cpp` while this runs.

**Never delete a user-facing feature.** This step moves code; it does not change it.

## Why
- `FMixtormatGpuCompositor::RequestComposeInternal` in `Source/MixtormatShaders/Private/MixtormatGpuCompositor.cpp` is about 1,770 lines.
- It gathers everything (layer surface, Height Op, Height Blending, drivers, every child type, every generator payload, ID producers, cache keys) in one function.
- Every step edits it, which caused most of the agent collisions.
- The precedent already exists: `Compositing/MixtormatEffectGather.cpp`.

## Target
New files under `Source/MixtormatShaders/Private/Compositing/`, following `MixtormatEffectGather.cpp` for naming, headers and namespace:
- `MixtormatLayerGather.cpp`: the layer's surface/source, composition, Height Op and Softness, Height Blending (every field), scalar drivers, and `SourceCacheKey`.
- `MixtormatMaskGather.cpp`: Mask, Generated, Blur and Curvature children.
- `MixtormatIdGather.cpp`: Pattern, Cluster (Filter), Combine, ID Group, Color ID, HSV/Random/Ramp/UV/Relief From IDs, and output references.
- `MixtormatGeneratorGather.cpp`: one function per generator type (Strata, Cracks, Rock, Pebbles, and Lattice later), each including its `FieldKey` hashing. It's called from the Generator layer path. Child generators are gone after 3a.
- `RequestComposeInternal` keeps only the orchestration: request setup, the layer loop calling the gathers, group scopes, prefix and node cache, and publishing targets. Aim for about 200–300 lines.

## Rules
- **Move, don't rewrite.** Same statements, same order inside each moved block, same comments. Only add the function signatures and parameters the moved code needs.
- Shared lambdas used across blocks (`Finite`, hashers, `RegisterTexture` helpers) become small free functions in one shared header, e.g. `Compositing/MixtormatGatherCommon.h`.
- **No behaviour change.** Same defaults, same guards, same cache-key inputs and order. A changed hash input would silently invalidate or merge cache entries.
- Add the new files to the module's build the way `MixtormatEffectGather.cpp` is added. Check whether it's explicit or picked up automatically.

## Static checks
- For each moved block, diff the old and new text: changes may only be added parameters, or `Layer.` becoming `InLayer.`.
- Grep that every symbol the moved code uses resolves in its new file (includes, namespaces).
- dxc isn't needed; no shader changes.

## Checklist for Hugo
- Build.
- Run the Composition, ID Group, layer-preview, runoff and parameter-authoring tests.
- Open an existing material: it looks identical, and the cache still makes slider drags fast.
