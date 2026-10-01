# Step 1: per-layer Height Op + Coverage

Paste `00-Shared-Rules.md` above this.

**Preservation correction:** retain the full Height Blending feature and its controls, independently of Height Op. Do not execute tests, builds, Unreal, shell, git, or dxc without explicit permission.

## Goal
Replace every implicit or ad-hoc height-combine rule with two explicit per-layer settings and one formula.

## Background (why)
Today a layer's height combine is derived implicitly, which makes it hard to predict:
- **"BLEND"** (`bSmoothHeightMerge`) is switched on implicitly by CompositionMode Replace + NormalBlendMode Combine. It is really a smooth **max**: colour ignores height, and the taller surface wins outright. Hugo reports "Blend isn't blending heights".
- **Height Mask Blending** (`bHeightBlendEnabled` plus `HeightSource`, `HeightThreshold`, `HeightRange`, `HeightContrast`, `HeightOffset`, `HeightBias`, `bInvertHeight`, `ConstantHeight`, `HeightBlendAmount`) is a separate toggle in which height decides visibility.
- **Generators** each carry their own blend enum: `EMixtormatGeneratorBlendMode` (Rock, Pebbles, Cracks) and `EMixtormatStrataBlendMode` (Strata).
- **The substrate** under layer 0 is height 0.5, so Min, Max and smooth-max on the first layer clip against a flat 0.5 plane.

## Target model
1. **`EMixtormatHeightOp`**, on `FMixtormatLayer`:
   - `Replace`
   - `Add` (signed about 0.5: `below + (layer - 0.5)`)
   - `Subtract` (`below - (layer - 0.5)`)
   - `Multiply` (`below * (layer * 2)`, so 0.5 is neutral; document this)
   - `Min`
   - `Max`
   - `Difference` (`abs(below - layer)`)
2. **`float HeightSoftness`**: the smooth min/max width for Min and Max, using the existing polynomial smax/smin with fillet (see `EvaluateHeightBlend` and the SmoothHeightMerge branch). 0 means a hard min/max.
3. **Height Blending remains independent.**
   - Disabled: coverage = opacity × placement mask × feature masks, as today.
   - Enabled: the existing Height Blending contest modulates coverage for **every** channel. Preserve `bHeightBlendEnabled`, height source, threshold, range, contrast, offset, bias, invert, constant height, amount, and their existing behavior.
   - Do not replace these controls with a reduced Coverage enum.
4. **One formula, for height:** `result = lerp(below, op(below, layer), coverage)`. Colour, roughness, AO, metallic and normal keep their existing coverage weighting, driven by the same coverage value.
5. **Empty ground.** Add an internal per-pixel occupancy map: R8 or R16F, ping-ponged on the layer index like the height targets, cleared to 0 for the substrate.
   - Each layer writes `max(occBelow, coverage)` for an enabled layer.
   - Where `occBelow == 0`, Min, Max, Difference and Height-coverage treat the layer as Replace, so the first layer behaves the same whatever op it uses.
   - Add, Subtract and Multiply keep 0.5 as their neutral.
   - This must not make layer 0 a special case; occupancy is the only mechanism.
   - Internal only; not published.

## Replace internal height-combine machinery (preserve user controls)
- `bSmoothHeightMerge` (render data, gather, shader param, shader branch).
- The implicit Replace + Combine derivation.
- Preserve `bHeightBlendEnabled` and every user-facing Height Blending field throughout data, gather, shaders, inspector, authoring, bindings, and tests.
- `EMixtormatGeneratorBlendMode` and `EMixtormatStrataBlendMode`, and every generator `BlendMode`/`*BlendMode`/`StrataBlendAmount` property, with their shader params and the switch blocks inside the generator shaders.
  - Generators now write their field into the layer input height **as Replace**: the generator output *is* the layer's height.
  - Combining with the stack below is the layer's Height Op.
  - Check how each generator currently mixes with `SourceHeight`, and keep any "Amount" that scales the generator's own relief, but no combine modes.
- The BLEND badge logic in `MixtormatLayerBadges.cpp`. Badges should show the Height Op (short labels: REP, ADD, SUB, MUL, MIN, MAX, DIF) and an "H" marker when Height Blending is on.
- Preserve the `HeightBlend` debug preview and its coverage visualization.

## UI
- Add Height Op and Softness controls; keep the existing Height Blending card (`BuildHeightBlendControls`), all its controls, and its preview eye.
  - Op dropdown.
  - Softness, visible only for Min and Max.
  - Height Blending toggle and the complete existing control set remain independent.
- Use the same widgets and helpers as the neighbouring cards (`MakeMemberEnum`, `MakeMemberSlider`, `AddCard`, `MixtormatRow::MakePair`).

## Shader work (`MixtormatComposite.usf`)
- Add a single `ApplyHeightOp(uint Op, float Below, float Layer, float Softness)`. Add a smooth min next to the existing smooth max.
- Restructure the end of `MainCS` so height is `lerp(Below, ApplyHeightOp(...), Coverage)`, with the occupancy override.
- Keep the existing `HeightInfluence` channel scale if it still makes sense (the result lerped by HeightInfluence); say so in the report.
- Write the occupancy output.
- Watch `PreparedLayerMode` 1 and 2 (the structure-effects re-composite in `ComposePipeline.cpp` around the `bPrepareStructure` path). Both must use the same op.

## Static checks
- Only with explicit execution permission: dxc at HV 2018 and HV 2021 for `MixtormatComposite.usf` (all entry points and permutations), plus every generator shader you touched.
- Grep for zero remaining hits of `bSmoothHeightMerge`, `SmoothHeightMerge`, `EMixtormatGeneratorBlendMode`, `EMixtormatStrataBlendMode`, and each deleted field name. Cover Source, Shaders, Config, Docs and Tests.
- Compare each touched shader's globals against its `SHADER_PARAMETER` list.
- Tests: rewrite `MixtormatCompositionBlendTests.cpp` and any other test touching removed fields so they assert the new ops (Replace, Add, Max on the first layer equals Replace, independent Height Blending). Keep them compiling; Hugo runs them.

## Docs
- Update the layer/height section of `Docs/index.html` to describe Height Op + independent Height Blending + empty ground.

## Report and checklist for Hugo
- Terse bullets: what changed, what was deleted, and any judgement calls.
- Checklist:
  - First layer: every op gives sensible heights, and Min/Max/Difference look like Replace.
  - Rock layer + material on top: Max, Add, and independent Height Blending (settles into lows).
  - Softness on Min and Max rounds the join.
  - Badges show the op.
  - Generators no longer have blend dropdowns.
