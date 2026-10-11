I'll work from the zip as the repo snapshot. First I'll extract it to a scratch directory with normalized paths, then read the docs and source.Both prototypes read in full. Now the docs, starting with GENERATORS.md and COMPOSITION.md.Docs read. Now the Runtime data model, starting with `MixtormatLayerTypes.h` for child ownership and scope.`MixtormatChildScope.h` is not in the zip, so I'll infer its contract from its call sites. Next, how the editor treats scopes in `MixtormatLayerChildren.cpp`.The coupling is quantified: `StructuralWarp` appears at 38 switch or branch sites across 21 files, in a snapshot that also omits the Inspector. Now the generator and effect type definitions.`HeightSource`/`WarpSource` are flagged in the docs as needing source-truth review, so I'll check what actually reads them.The header comment saying consumers are "implemented separately" looks stale: `MixtormatGpuGeneratorPasses.cpp:2873` resolves both inputs. I'll read the main generator layer loop around there.The existing flow tools already use `ScopeOwnerChildId` (real ownership), while Push and Warp use sibling plus `TargetChildId`. Now the flow tool implementation itself.Now the shared render-data and bundle types in `MixtormatGpuCompositorInternal.h`.Now `MixtormatOutputReference.h`, the existing typed-field and reference contract.Now the gather side: how structural modules and flow tools get projected into render data.Now what "Fold", "Bend", "Curl" and "Distort" mean in the source today.Phasor noise does not exist anywhere in the snapshot. Curl exists in Flow Warp; I'll read it in full.Now the main flow shader, `MixtormatGeneratorFlow.usf`, in full.Now the structural warp shader and the shared warp include.Now the compose pipeline's child loop, scoping and demand logic.Now what each child declares it publishes, in `MixtormatChildCapabilities.cpp`.Now the editor side. The structural projection model and its header first.Now how the hierarchy widget renders depth, collapse and rows.Now parameter ownership and binding, which every new child type must plug into.I hit the tool limit before finishing, so this is **not** the full 10-section report and I have not written the report file. It is what I verified, labeled as confirmed, inference, or not yet inspected. I read the following in full:

- both wrinkling prototypes
- `MixtormatGeneratorFlow.usf`, `MixtormatStructuralWarp.usf`, `MixtormatGeneratorWarp.ush` and `MixtormatFlowWarp.usf`
- `GENERATORS.md` and `COMPOSITION.md`
- `AddGeneratorLayerPasses` and the flow-tool pass chain
- the output-reference validation logic
- the structural gather and the editor projection
- drag/drop scope validation

## Preliminary verdict (partly inference)

The direction is right, but I'd change two assumptions.

1. **Real nested ownership already exists in the data model.** `FMixtormatLayerChild::ScopeOwnerChildId` (`MixtormatLayerTypes.h:179`) is a parent pointer, with children stored as a contiguous pre-order subtree. Flow tools and masks already use it, to any depth:
   - `CanKeepScopedPlacement` (`MixtormatLayerChildren.cpp:693-714`) is the placement rule.
   - `GetScopeDepth` and `FindSubtreeEnd` (L152-183, L214-227) walk the chain and subtree.
   - Drag/drop moves whole subtrees and validates on copies (`MixtormatLayerDragDrop.cpp:885-967`).
   - The hierarchy widget already paints N-level rails and collapse (`MixtormatLayerHierarchy.cpp:103-132`, `SMixtormatLayerHierarchy.cpp` OnPaint).

   Height Push and Structural Warp are the odd ones out. They are siblings pointing at a target by GUID, and the gather and drag/drop explicitly refuse them as scoped children (`MixtormatGeneratorGather.cpp:361,380`; `MixtormatLayerDragDrop.cpp:897-908`). So V2 needs new child types and execution semantics, not a new container.
2. **A large Field abstraction is probably premature.** Typed kinds already exist: `EMixtormatPublishedFieldKind` has Flow, UVMap, Color, Scalar01, ScalarSigned, SDF, Vector2 and RegionIds (`MixtormatOutputReference.h:20-36`). What is missing is a Height kind (it is currently ScalarSigned named "Height") and a Mask kind. Masks live in a separate untyped `PublishedMaskOutputs` map (`MixtormatGpuCompositorInternal.h:1592-1593`).

## Confirmed architectural problems

- **Per-type switch tax.** `StructuralWarp` appears at 38 sites in 17 files of this snapshot, which omits the Inspector. Examples are `IsChildEnabled`/`SetChildEnabled` (`MixtormatLayerChildren.cpp:72-140`, with `checkNoEntry()` defaults) and the gather if-ladder (`MixtormatGpuCompositor.cpp:1712-1728`). Demand registration is also a per-type ladder (`MixtormatGpuComposePipeline.cpp:697-743`).
- **The projection is a workaround.** `BuildChildProjection` (`MixtormatStructuralConnectionProjection.cpp:87-330+`) rebuilds a visual tree from flat siblings. It is display-only, and its own header says its indices "never address an authored child array".
- **Validation exists only to compensate for sibling ordering.** `EvaluateGeneratorInputSource` (`MixtormatOutputReference.cpp:431-610`) hardcodes Push and Warp branches (L486-502). Its scope-completion walk (L572-588) says "a tool row before the module is not enough if its generator scope finishes later". Push targets Strata only (L636-652), while Warp accepts any generator. Real ownership removes this class of check.
- **Two deformation regimes that disagree.**
  - Flow tools resample height per tool and then remap the bundle (`MixtormatGpuGeneratorPasses.cpp:1772-1866`). Each Apply bilinear-resamples height and the next tool reads that result, so chained tools resample repeatedly (inference).
  - Structural Warp composes a displacement once, `D_new = d + D_old(x+d)` (`MixtormatGeneratorStructuralWarp.usf:49-53`), then pulls back the completed bundle after normalization (`MixtormatGpuGeneratorPasses.cpp:2938-2975`).
  - The comment at L1567-1569 says masks and IDs "stay undeformed", but L1847-1866 remaps them. The sole caller always passes a bundle (L2928), so the comment is stale.
- **Producer and consumer are fused in the flow tools.** Stages 0, 1, 2, 7 and 9 produce a field; stage 3 consumes it, switching on `Mode` for Shape Deform, Generator Flow, Flow Carve and Gravity (`MixtormatGeneratorFlow.usf:528, 561, 576`). The flow field is packed `(dir.xy, signedDist, influence)` (L346, L405), and Flow needs smooth and validity companions (`FPublishedField::IsComplete`).
- **Stale header comment.** `MixtormatGeneratorTypes.h:964-967` says `HeightSource`/`WarpSource` consumers are "implemented separately". They are consumed at `MixtormatGpuGeneratorPasses.cpp:2873-2886`. The docs' request for a source-truth review is justified.
- **Stringly-typed capabilities.** `GetChildCapabilities` patches field kinds by output name (`MixtormatChildCapabilities.cpp:354-368`). Influence, Validity and CarveMask are previews only, not typed fields.
- **Flow producers are scarce.** `GeneratorPublishesField` says only Noise emits Flow (`MixtormatOutputReference.cpp:59-79`). Curl exists only inline in Flow Warp, Peeling, Generated Mask and Craquelure, and phasor noise does not exist anywhere.

## Reusable infrastructure

- Displacement composition and Jacobian (`MixtormatGeneratorWarp.ush:52-77`)
- `RemapCompletedGeneratorBundle` with its field-semantic descriptors (`FGeneratorBundle`, `MixtormatGpuCompositorInternal.h:1124-1190`)
- The flow solve stages
- Scoped masks via `AddScopedFeatureMask`
- Output references and the Sources shelf
- The slope-to-normal transform in Flow Warp (L140-152)
- The Jacobian-fold visualizer (`MixtormatGeneratorFlow.usf:641-675`)

## Terminology risk

"Fold" currently means four things:

- `BreakupFold`, a post-composite lip raise (`MixtormatEffect.h:890`)
- Strata's geological folds (`StrataFoldD`, `MixtormatStrataCarver.usf:135`)
- A non-positive-determinant UV map (`MixtormatGeneratorFlow.usf:660`)
- The prototype's fold mask

V2 should rename before adding a Fold Behavior.

## Wrinkle prototypes

- **`wrinkling01.cl`:**
  - The weighted Laplacian is `w = avg/edge_len` (L76-78).
  - Curvature is `1-exp(-gain·k)` (L96).
  - Curvature-dependent diffusion has speed `0.5(ci+cj)` (L112-145).
  - It is iterative and ping-pong. In a heightfield the 8-neighbor weights become 1 vs √2, and no mesh topology is needed.
  - Line 86 double-weights the normal-variation term. That is invisible on uniform edges but matters at diagonals, so decide deliberately rather than port it.
- **`wrinkling02.cl`:** it uses no neighbors at all. It is a pointwise remap with `fold_side` and `mask_mode`, plus AO and thickness weighting. It then displaces along blended normal and gravity (L102-134).
- **Heightfield adaptation:** displacement along a blended normal and gravity vector has lateral components. In a heightfield that is a UV displacement plus a height add. The `_curv`, `_shadow` and `_ao` inputs come from passes not in either file.
- **Existing features:** it is neither Fold (Breakup's lip raise) nor Shape Deform (boundary-normal offset).
- **My lean (inference):** a Wrinkle field producer, meaning curvature and Laplacian fields, plus a Behavior that consumes it.

## Smallest slice (preliminary inference, not yet verified)

- One generator, probably Noise, because it needs only a single dispatch and already supports Structural Warp as a target.
- One new scoped Behavior child, "Warp".
- Under it, two field children (for example Own-Height Flow plus a second flow) and one scoped Mask.
- Execution reuses the `MixtormatGeneratorStructuralWarp.usf` displacement composition and the completed-bundle remap.
- The old Structural Warp path stays untouched.

This proves ownership, field composition and the GPU path without touching the other five generators.

## Not yet inspected, and gaps in the zip

- **Not in the zip:** the Inspector sources, `MixtormatIdTypes.h`, `MixtormatHeightTypes.h`, `MixtormatChildScope.h` (so `MaximumScopeDepth` is unknown), the Bundle, HeightPush and Cracks shaders, and `MixtormatGully.ush`, which defines the curl noise.
- **In the zip but unread:** `GENERATOR_SHADER_IMPROVEMENT_PLAN.md`, `flow_generation_core.md`, `generator_warp_output_alignment_design.md`, `UI.md`, and the Strata, Noise and mask shaders.
- **In the zip, partly read:** `MixtormatParameterBinding.cpp` (I only surveyed the owner enum), group expansion, clipboard remapping, `MixtormatLayerActions.cpp`, and the source/effect gathers.

So the migration matrix, parameter-binding and clipboard analysis, caching and resource-lifetime detail, and the three UI concepts are not done.

Do you want me to continue in a new turn, reading those remaining files and writing the full audit as a single `.md` file? Or should I prioritize a specific section, such as the migration matrix or the slice design?