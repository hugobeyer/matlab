# Handoff: finish the Height Blending restore, then resume Step 8 (Rock)

Paste `00-Shared-Rules.md` above this.

**Never delete a user-facing feature unless the step names it.**

**Sanity update (2026-10-01):** see [Step 1 report](REPORT-Step1-Sanity.md). Shader checks pass; inherited HMB source/contrast/reference limitations and incomplete test coverage prevent claiming full restoration. Items 1–4 below are checked; the external feedback-memory update remains open. Generator-layer work has not started.

## What happened
- Sonnet's Step 1 deleted Hugo's Height Blending features. All of them are intact in HEAD (`f43f02d`), and the deletions are only uncommitted.
- Claude restored them by rebuilding files from HEAD, keeping three things from Step 1:
  - the generator blend-enum and Amount removal (agreed);
  - the explicit per-layer **Height Op** + **Height Softness**, at the top of the COMPOSITION card;
  - **occupancy**, the empty-ground map: Min, Max and Difference fall back to Replace on bare ground.

## Model now
- **Height Blending (HMB):** unchanged from HEAD: `bHeightBlendEnabled`, source, strength, contrast, biases, invert, contact AO, border normal, smoothing, reference layer, driver slot 2, debug modes.
- **Height Op:** replaces only the implicit BLEND smooth merge.
  - Default is Max with Softness 0.1 (the old BLEND look). Fill layers default to Replace (the old OVER).
  - With HMB on and Op = Replace, the output is HEAD's HMB exactly. With HMB on and another op, the contest decides coverage and the op decides height (see the end of `MainCS` in `MixtormatComposite.usf`).

## Done (by static check only)
- **Rebuilt from HEAD and patched:**
  - `MixtormatComposite.usf`: dxc OK on all entry points at HV 2018 and 2021; params match the C++.
  - `MixtormatGpuCompositor.cpp`
  - `MixtormatGpuCompositorInternal.h`
  - `MixtormatGpuComposePipeline.cpp`
  - `MixtormatMaterial.h`
  - `SMixtormat_Layers.cpp`
  - `SMixtormat_Inspector.cpp`: generator panels taken from Sonnet's version; Height Op row added at the top of COMPOSITION.
  - `Docs/index.html`
- **Reverted to HEAD:** MaskPasses, SMixtormatInternal.h, SMixtormat.cpp, Preview, PreviewViewport, LayerGroups.h, GpuCompositor.h, ClusterIds.usf, EffectPasses, and the 4 tests (`EComposition::Blend/Over` renamed to `Combine/Override`).
- **Kept from Sonnet:** the badges (op labels, plus " H" when `bHeightBlendEnabled`), `BuildLayerHeightOpMenu`, `GeneratorPasses.cpp`, the Cracks/Pebbles/Strata shaders, and the JSON/ini edits.

## Still to do
1. **Grep/compile sanity, by reading:**
   - Every symbol that Sonnet's kept files reference must exist: `BuildHeightBlendControls`, `EMixtormatDebugPreviewMode::HeightBlend/ContactAO/BorderNormal`, `MaxScalarDrivers`, `FLayerRenderData::HeightOp/HeightSoftness`.
   - Check `SMixtormat.h` declares both `BuildHeightBlendControls` and `BuildLayerHeightOpMenu`.
2. **`GeneratorPasses.cpp` vs the restored Internal.h:** the generators no longer read BlendMode/Amount. Confirm there are no leftover references.
3. **Badge comment headers** (`MixtormatLayerBadges.h`, `SMixtormatLayerRow.h`) mention "Coverage = Height"; reword them to "Height Blending on".
4. **Composition segmented control:** decide whether the Combine/Override choice should still set HeightOp. It doesn't now; ops are independent.
5. **Prompts:**
   - Edit `00-Shared-Rules.md` and `01-...md`: never delete user-facing features unless named.
   - Save a feedback memory saying the same.
6. **Resume Step 8 (Rock):**
   - The new shader is parked at `AgentDocs/Prototypes/MixtormatRockFormation_Step8.usf.txt` and is complete.
   - Follow `08-RockFormation-Upgrade.md`. Remaining C++ work:
     - `FMixtormatRockFormation` properties and defaults (see the table in 08). Remove Slope, ChamferBias, Warp, WarpScale, Bend, Fault, Normalize/RemapLow/RemapHigh. Add `EMixtormatRockHeightMode` {Raw, Analytic, Measured}.
     - The gather, plus the hash skip list (HeightScale, HeightMode).
     - `FRockFormationRenderData`.
     - `FMixtormatRockFormationCS` params to match the `.usf` globals: new `EdgeTags` buffers; `FRockLeafStride` = 10 floats + 8 uints.
     - `ResolveRockLayout`: style maps 0..1 to table positions 1..2.5.
     - Measured mode: `AddNormalizeFieldPasses(...,0,1)`.
     - The inspector groups.
     - The JSON `Generator.RockSkew` default (0.5).
   - dxc stages 0, 1 and 2.

## Checklist for Hugo
- Height Blending card and all its controls are back and behave as before.
- Height Op is at the top of COMPOSITION; a new layer defaults to Max 0.1, a fill to Replace.
- First layer with Max or Min looks like Replace (bare ground).
- Contact AO, Border Normal, Smoothing and the Reference layer all work; their eye previews work.
