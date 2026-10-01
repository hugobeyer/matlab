# Step 3: delete generator children, cross-layer IDs, Lattice generator

Paste `00-Shared-Rules.md` above this. Prerequisite: Steps 1 and 2 are merged.

Do this as three sub-steps (3a, 3b, 3c). Report after each one so Hugo can review in small pieces.

## 3a: delete generator children
- Remove `EMixtormatLayerChildType::Generator` and everything that only exists for it. It's unshipped: no migration, and no fallback.
  - The gather branch in `MixtormatGpuCompositor.cpp`.
  - `FChildRenderData::Generator`.
  - Child-mode `AddGeneratorPasses` and `AddGeneratorFieldPasses` (if Step 2 didn't already remove them).
  - `LayerCtx.GeneratorFields`, `bGeneratedHeight`, and the "a generator wrote this layer's height" `HasPackedHeight` override.
  - The add-menu entries for generator children.
  - The `EMixtormatChildCreation` generator values.
  - Child badges and child row names for generators.
  - `GetSelectedStrataCarver`/`GetSelectedGenerator` paths that resolve a *child*. Re-point them to the selected Generator *layer*.
  - Capabilities cases keyed on child type.
  - Flow-tool scoping to a generator child: they now scope to the Generator layer only.
  - Parameter bindings (`MixtormatParameterBinding.cpp`, `MixtormatParameterDefinition.cpp`, `SMixtormat_Parameters.cpp`) that reach generator payloads through a child must now reach them through the layer.
- Grep for zero remaining child-generator references.
- Update the tests and `Docs/index.html` (the "Generators" section now describes Generator layers).

## 3b: cross-layer IDs and the source picker
- **Today:** `LayerCtx.RegionIdMaps` is per layer, keyed by child index, and consumers use `FindRegionIdsAbove`, the nearest producer above them in the same layer. Drivers explicitly refuse a region source in another layer.
- **New:** a compose-wide `Ctx.PublishedIds`, keyed `{LayerId, ProducerChildIndex or INDEX_NONE for a layer-level producer, OutputName}`. Mirror `FPublishedMaskKey` / `PublishedMaskOutputs`.
  - Generator layers publish their bundle IDs here.
  - In-layer producers (Pattern IDs, Cluster, Combine, Breakup) publish here too.
- **Every ID consumer** gets an explicit **ID Source** property: a layer, plus a named output. The consumers are HSV/Random/Ramp/UV/Relief From IDs, the HSV region tint in the composite, scalar drivers with `bRegionSource`, and generator ID influence.
  - Default "Nearest above" keeps today's same-layer behaviour.
  - Otherwise use the picked source.
  - A source in a layer above the consumer, or one not produced yet, resolves to nothing (the consumer is culled). Never guess.
- **Pixel space:** IDs from another layer are in output space, which is fine. UV From IDs' centre cache (`RegionCentreCache`) must key on the source too.
- **UI:** an ID Source dropdown listing "Nearest above" plus every published ID output of the layers at or below, as "Layer name › Output label". Build it from the capabilities table.
- **Docs and tests.**

## 3c: Pattern IDs becomes a Lattice generator
- Add generator type **Lattice**, carrying today's Pattern IDs modes (Grid, Running Bond, Herringbone, Basketweave, Hex, Octagon + Square, Flagstone, Voronoi, Hopscotch, French Ashlar, Fracture Plates; see `EMixtormatPatternMode`).
- Port the Pattern IDs shader logic into the Lattice generator:
  - **Height:** pattern height, gap height, bevel.
  - **Coverage:** the tiles, with gaps as 0 when the layer bool is on.
  - **IDs, CentreUV, Orientation:** Pattern already writes 0 or pi/2.
  - **Named mask:** `Gap`.
- Add **Truchet** as a new mode: quarter-circle arc tiles with a random flip per cell, tileable, publishing IDs per tile.
- Delete the `PatternId` child type and all of its code: gather, passes, inspector, capabilities, badges, add menu, tests, docs. Anything that consumed Pattern-specific outputs (e.g. the UV From IDs Pattern quarter-turn special case) now reads the generic bundle orientation.
- Use dxc on the new or ported shader.

## Report and checklist for Hugo (per sub-step)
- **3a:** no generator children remain anywhere in the add menus; Generator layers still work; flow tools only appear under Generator layers.
- **3b:** a Random From IDs on a material layer picks "Rock layer › Rock IDs" and colours per rock; "Nearest above" still works inside one layer; picking a layer above resolves to nothing.
- **3c:** a Lattice layer in every mode, plus Truchet; the gaps are transparent with coverage on; UV From IDs on a layer above uses the Lattice IDs, CentreUV and orientation.
