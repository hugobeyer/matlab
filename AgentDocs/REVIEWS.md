I’ll read the repo rules and plan, then review the scoped shaders and CPU bindings read-only.


The Cliff endpoint risk is confirmed by its CPU binding. I’m checking Strata seam math and Erosion’s resolve path next, separating new issues from documented behavior changes.


## Findings

1. **P1 — Cliff reads an unwritten sweep row. Already documented; confirmed by CPU binding.**  
   `Mixtormat/Shaders/Private/MixtormatCliffStrata.usf:118–121` initializes `val`, but never writes `OutSweep[x,0]` before reading it during the backward sweep. With height 1, neither loop writes any output. CPU allocates fresh `SweepB` and binds it without clearing/copying at `Mixtormat/Source/MixtormatShaders/Private/MixtormatGpuGeneratorPasses.cpp:2540–2542`; the clear-value descriptor is not initialization. **Impact:** undefined first-row height, subsequently consumed by stage 8. **Fix:** explicitly seed `OutSweep[x,0]` from `SweepIn[x,0]`.

2. **P2 — Cliff’s backward horizontal sweep discards the forward result at the last column.**  
   `Mixtormat/Shaders/Private/MixtormatCliffStrata.usf:113–114` resets the endpoint to `RawHeight`, ignoring its already-relaxed `current`. For example, with `Reverse=0`, `CarveDepth=0`, heights `[0,1]` and fall `0.1`, the forward result `[0,0.1]` becomes `[0,1]`. Identical rows do not get repaired by the vertical sweep. **Fix:** initialize the backward endpoint from the forward result. This is a current sweep defect, not attributed to the recent Erosion conversion.

3. **P2 — Cliff distance sweeps do not propagate across the tile seam.**  
   `Mixtormat/Shaders/Private/MixtormatCliffStrata.usf:124–126` wraps ID-crossing detection, but stages 6/7 restart at each image edge without periodic propagation (`:130–137`). **Scenario:** a block spans the texture edge and its nearest boundary lies across that edge; the solver instead measures to a farther boundary inside the image. Seam/cavity outputs consume those inflated distances at `:143–144`. **Fix:** implement periodic distance propagation in both axes. No history attribution or performance claim.

4. **P2 — Geological Strata loses its shared seam height when a bed is undersampled.**  
   `Mixtormat/Shaders/Private/MixtormatStrataCarver.usf:373–381` lets `EdgeWidth` exceed 1 through `PixelT`. At `T=0`, `LowerBlend=0`, but `UpperBlend<1`, leaving an unwanted contribution from the *opposite* seam and this bed’s shelf. The adjacent bed generally computes a different limit. **Scenario:** high frequency, thin beds or strong folds at low resolution, even with Breakup/Lamination disabled. **Impact:** resolution-dependent height discontinuities despite shared interface heights. **Fix:** preserve endpoint constraints and handle undersampled beds separately from shoulder widening.

## Delivery and compatibility

- **Strata replacement remains a documented compatibility break:** `RampShape` is retired at `Mixtormat/Source/MixtormatRuntime/Public/MixtormatGeneratorTypes.h:109–111`; old appearance is not preserved. The plan explicitly acknowledges this at `Mixtormat/AgentDocs/GENERATOR_SHADER_IMPROVEMENT_PLAN.md:27–34`.
- **Erosion:** no additional definite conversion/binding defect found. Seed → initialized carve ping-pong → absolute-height resolve is connected correctly (`Mixtormat/Source/MixtormatShaders/Private/Effects/MixtormatErosionPasses.cpp:409–468`). Numerical equivalence to the former implementation remains unverified.
- **FlowWarp:** per-axis derivative normalization is mathematically appropriate (`Mixtormat/Shaders/Private/MixtormatFlowWarp.usf:65–89`), but is a behavior change for unequal radii/non-square outputs—not an output-preserving optimization.
- Cliff 1D dispatch/scan optimization, geological faults/pinch-outs, and weathering/water simulation remain proposals—not delivered fixes.

**Read-only static review only.** No edits, commands, git, tests, diagnostics, builds or images. Other agents’ assigned systems were excluded.



I’ll read the repository rules, then review the scoped shader and CPU contracts without changes.


The core bindings and ordered D/B composition look consistent. I’m checking publication timing and cache reuse for stale or missing source fields.


I found a dispatch regression: Height Push and Structural Warp also fall through into Peeling. I’m checking one related cache dependency before wrapping up.


## Findings

1. **P1 — Height Push / Structural Warp fall through into Peeling.**  
   `Mixtormat/Source/MixtormatShaders/Private/MixtormatGpuComposePipeline.cpp:973–980` skips the older height modules but omits both new types. They reach the default effect dispatch at **1028–1088**, using `FEffectRenderData`’s default Peeling settings. **Trigger:** a valid structural module on a generator layer containing an enabled Peeling effect; unintended peel contributions enter the composite. Even **Height Push Amount = 0** still gathers and falls through. **Fix:** skip both types alongside the existing height modules.

2. **P2 — Downstream Peeling caches pre-warp identity dependencies.**  
   `Mixtormat/Source/MixtormatShaders/Private/MixtormatGpuSimulationPasses.cpp:646–663` keys generator-sourced IDs using only Rock/Pebbles’ native `FieldKey` and the *previous layer’s* prefix. Structural Warp changes their published IDs without changing either key; **683–689** can therefore restore an obsolete peel field. **Trigger:** cache a same-layer Rock/Pebbles → Peeling setup with nonzero ID Influence, then change Structural Warp amount/mask. IDs move, but cached peeling remains aligned to the old revision. **Fix:** key against the completed ID revision, or disable this cache reuse for structurally remapped producers.

**Not findings:** documented raster/distance approximations, legacy centre/orientation inverses, and Noise’s source-frame Gradient transport.

Static review only; no edits, commands, git, tests, diagnostics, builds, or images. Excluded algorithms were not reviewed.


I’ll read the repository rules and shader docs, then trace the flow paths using file reads only.


I’ve traced the shared solve/apply path. Next I’ll check output consumers, boundary producers, and all six generator call sites.


I found two concrete integration bugs: scoped-mask previews get overwritten, and Cliff deposits leave `CliffCoverage` stale. I’m checking the final consumers and separating documented boundary limits from bugs.


## Findings

**1. P2 — Flow Carve deposits leave `CliffCoverage` stale.**  
At [MixtormatGpuGeneratorPasses.cpp:1815–1842](Mixtormat/Source/MixtormatShaders/Private/MixtormatGpuGeneratorPasses.cpp#L1815), apply updates `Bundle->Coverage`, but refreshes only the named `PebbleCoverage` alias. Flow Carve skips the bundle-remap branch entirely.
- **Trigger:** Cliff Strata → Height-steered Flow Carve → Deposit into uncovered texels.
- The shader extends coverage at [MixtormatGeneratorFlow.usf:595–598](Mixtormat/Shaders/Private/MixtormatGeneratorFlow.usf#L595), but `CliffCoverage` retains its original texture, registered at [lines 2549–2554](Mixtormat/Source/MixtormatShaders/Private/MixtormatGpuGeneratorPasses.cpp#L2549).
- **Impact:** Coverage previews and downstream copied masks omit deposited support.

**2. P2 — Generator-owned scoped-mask previews are overwritten.**  
[MixtormatGpuComposePipeline.cpp:884–885](Mixtormat/Source/MixtormatShaders/Private/MixtormatGpuComposePipeline.cpp#L884) resets `DebugMask` **after** generator flow evaluates its scoped masks.
- **Trigger:** Preview a nonuniform Mask/Noise Gate beneath an active flow tool.
- Its snapshot is assigned at [MixtormatGpuMaskPasses.cpp:635–642](Mixtormat/Source/MixtormatShaders/Private/MixtormatGpuMaskPasses.cpp#L635), then discarded. The later mask dispatch skips scoped children at [lines 972–975](Mixtormat/Source/MixtormatShaders/Private/MixtormatGpuMaskPasses.cpp#L972).
- **Impact:** Preview shows the layer mask—typically white—not the gate actually used by flow.

**3. P3 — Direction preview disagrees with tracing where smoothing cancels vectors.**  
[MixtormatGeneratorFlow.usf:408–415](Mixtormat/Shaders/Private/MixtormatGeneratorFlow.usf#L408) falls back to the raw direction when the smoothed vector length is ≤0.2. The preview instead directly colors the smoothed vector at [lines 612–620](Mixtormat/Shaders/Private/MixtormatGeneratorFlow.usf#L612).
- **Trigger:** Smooth opposing directions near a nearest-seed seam.
- **Impact:** Preview displays a cancelled/residual direction instead of the raw direction actually traced.

## Known limitations—not additional bugs

- Gravity boundary checks intentionally stop oversized near-boundary steps; published Flow references omit those checks. Documented at [GENERATORS.md:74–80](Mixtormat/AgentDocs/GENERATORS.md#L74).
- Cracks marks every piece interior negative ([MixtormatCracks.usf:476–480](Mixtormat/Shaders/Private/MixtormatCracks.usf#L476)). Gravity’s positive-deflection mode freezes known interiors, so Signed Distance steering is effectively inert there—not equivalent to Height steering.
- Noise and Cliff correctly remain Height-only; unsupported Signed Distance requests are skipped.

**Review scope:** Traced all six generators, gather/dispatch, masks, outputs and resource bindings. No additional missing initialization/binding defect found in the reviewed flow path. Static review only; no edits or execution. Excluded implementations were not audited.