# Step 1 sanity report — 2026-10-01

**Stopped after numbered step 1. Generator-layer work has not started.**

## Scope and starting state
- Read `NEXT-Opus-Generator-Layer.md`, `00`, `02`, `03`, `06`, `08`, the Height Blending restore handoff, `01`, `MEMORY.md` and every file linked by its index.
- Hugo explicitly permitted read-only memory/git commands and dxc after the shared rules' execution restriction was identified.
- HEAD is `b380bc2`. The eight requested log entries were read.
- Initial status: only `AgentDocs/Prompts/NEXT-Opus-Generator-Layer.md` was untracked. There were no uncommitted source/shader edits to merge or preserve.
- No builds, Unreal launches, tests, staging or commits. No production feature was removed or changed.

## One real consistency fix
- `Source/MixtormatEditor/Private/Tests/MixtormatLayerPreviewTests.cpp`: explicitly set the shared Fill fixture's `HeightOp` to `Replace`, matching `SMixtormat::InitializeNewLayer`.
- Merely assigning `Type = Fill` does not change the shared struct's Max default. Under Max, the reference-comparison fixture's middle height 0.1 retains the preceding 0.8, so its incoming 0.5 cannot satisfy the existing blue-wins assertion. The fixture also expects the retained HMB + Replace height formula elsewhere.
- This changes only test setup; assertions and production defaults are preserved. Tests were not run.

## Shader checks
- All **45 `.usf` files** compile on scratch copies with `-T cs_6_0`, at **HV 2018 and HV 2021**.
- **208/208 dxc invocations passed**: 74 registered shader classes/entry points, all their declared permutations, plus the unregistered Flow shader entry point.
- The scratch `Platform.ush` is empty and virtual include paths are rewritten, as prescribed. This checks standalone HLSL syntax, not Unreal compilation or runtime behavior.
- Compared shader globals against the union of corresponding C++ parameter structs in both directions, including scalar/resource types and array widths. Entry-specific live resource bindings were checked from dxc assembly. No confirmed mismatch found.
- Palette arrays resolve to 8 through `FMixtormatColorIdMask::MaxColors` and `FMixtormatHsvIdFilter::MaxPaletteColors`, matching HLSL. These are qualified C++ constants, not missing shader parameters.
- Scratch recipe, compiler diagnostics, assembly and machine-readable audit: `Intermediate/AgentSanityStep1-20261001/`. They are ignored generated artifacts, not production additions.
- `MixtormatFlow.usf` has no shader registration or source reference. It still compiles at both HV versions. It was **not deleted**.

## Preservation confirmed
- `BuildHeightBlendControls` and `BuildLayerHeightOpMenu` are declared and defined.
- Height Blend, Contact AO and Border Normal debug enums and buttons remain.
- Both scalar driver slots remain; HeightBlendAmount uses the second slot (index 1).
- Height Op defaults to Max, Softness 0.1; editor-created Fill layers explicitly use Replace.
- Occupancy makes Min, Max and Difference act like Replace on bare ground.
- The retained HMB + Replace final-height expression matches the old HMB expression. HeightInfluence remains the final channel influence.
- Combine/Override remains independent of Height Op; do not make the segmented control implicitly change it.
- Badge headers already describe `H` as Height Blending on. No badge-header fix is needed.
- Removed generator blend enums and Amount properties have no remaining production references. `SmoothHeightMerge` survives only as an older test oracle, not live pipeline code.
- Typed output references, nested ID composition, Ramp/UV/Relief/Combine ID indexing, and the `ValidateInsert -> int32` fix remain. No confirmed dangling ID symbols or call/signature mismatches were found in the audited paths.
- Rock math and tuned defaults agree with 08 and the parked/OpenCL prototypes, allowing the intentional buffered Build and additional normalized-output stage. Height/Slope/Gap, masks/ramps, signed distance, stages 0–3, 10 cache slots and 13 memo slots remain. Rock leaf stride is 72 bytes (10 floats + 8 uints). No missing port math was identified.

## Preflight gaps — do not claim these are complete
1. **The promised HMB source/contrast/reference controls are not active in the current inspector.**
   - `BuildHeightBlendControls` shows source text, not a source picker; no HeightContrast or HeightReferenceLayerIndex controls.
   - The fields remain in data/gather/shader declarations, but complete-surface gathering sets `bDirectHeightComparison = !bNormalOnly` (`MixtormatGpuCompositor.cpp`, around 2999).
   - That shader path bypasses HeightSource and contrast shaping; comparison uses immediate PreviousHeight rather than the selectable ReferenceHeight.
   - These limitations already exist at `f43f02d`: the relevant inspector/gather paths were not removed by the Step 1/Rock/ID commits. Thus the handoff's statement that every listed control works cannot be confirmed, but treating it as a new regression and rewriting HMB would be speculative.
2. **`MixtormatIdGroupTests.cpp` is helpers only.**
   - No automation registration or RunTest is present. NEXT's description of this file as completed is inaccurate.
   - `Mixtormat.Compositor.HierarchicalIdGroup` is registered separately in `MixtormatChildOutputPreviewTests.cpp`; it was preserved.
   - No new test suite was invented or executed during this sanity pass.
3. **The old composition test oracle has not been updated to explicit Height Op.**
   - `MixtormatCompositionBlendTests.cpp` models exclusive Over/Merge/MaskBlend paths and contains no HeightOp/HeightSoftness references. It does not prove the new op matrix or occupancy behavior.
4. **External memory still contradicts the preservation rule.**
   - `generator-layer-roadmap.md` still says Height Blending was deleted and scalar drivers shrank to one slot.
   - No preserve-user-features feedback file is linked by MEMORY.md. This pass did not write outside the repo: shell permission was read-only except for scratch compiler outputs.
   - Before step 2, correct the external roadmap and save/index the feedback: never delete a user-facing feature unless the step explicitly names it; HMB remains independent of Height Op.
5. **C++ compilation cannot be certified from editor diagnostics.**
   - clang diagnostics cannot find Unreal's `CoreMinimal.h` and `Misc/AutomationTest.h`; ensuing unknown-type/macro errors are cascading setup errors.
   - Source/call-site inspection found no confirmed new C++ break, but no build was run and this is not a C++ compile pass.

## Restore-handoff items 1–5
- 1: required symbols cross-checked; no missing declarations found.
- 2: GeneratorPasses no longer reads the removed blend/Amount fields.
- 3: badge `H` comments are already corrected.
- 4: keep the composition segmented control and Height Op independent.
- 5: 00 and 01 already contain the preservation correction; external memory update remains open.

## Hugo checklist
- [ ] Resolve the inherited HMB source/contrast/reference discrepancy before claiming full preservation.
- [ ] Authorize updating the external memory; its deleted-HMB guidance must not drive step 2.
- [ ] Build locally; run Composition, hierarchical ID Group, layer-preview and runoff tests.
- [ ] Verify HMB/Contact AO/Border Normal, Max/Min Softness, and bare-ground Replace behavior.
- [ ] Compare Rock at 08 defaults with the Copernicus prototype, including row seams and cached edits.
- [ ] After review, authorize numbered step 2; Generator-layer/UV behavior is not implemented yet.
