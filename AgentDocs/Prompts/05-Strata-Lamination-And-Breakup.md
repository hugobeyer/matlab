# Step 5: Strata, carved laminae and edge-only breakup

> **Status 2026-10-01:** the shader-side fix is already done in `MixtormatStrataCarver.usf`:
> - Breakup has been removed from S and now only bites the crest, using 1D chips along the strike.
> - Laminae are now subtract-only grooves that fade before the face.
>
> What remains for this step is exposing the constants as parameters: `LaminaCount`, `LaminaWidth` and `BreakupScale`. They replace `MIXTORMAT_STRATA_LAMINAE`, the 0.12 groove width and `MIXTORMAT_STRATA_BREAKUP_SCALE`. Read the current shader first and keep its behaviour.

Paste `00-Shared-Rules.md` above this. Run it after Steps 1–4. Strata is a Generator layer by then; port these notes to wherever its shader lives (`Shaders/Private/MixtormatStrataCarver.usf` today).

## Current model (v2)
- Each bed is a dip-slope ramp from base to crest, then a face down to the next bed's base.
- Bed coordinate: `S = dot(UV, BedWave) + Bend*... + Breakup*noise + HeightFollow*...`. The bed index and `T` (0..1 up the bed) come from S.
- `Verticality` sets the face share of the bed; `RampShape` sets the ramp curve.

## Hugo's two complaints
1. **Lamination** should look like lines **carved into the ramp**. Today it's a ± sine added over the whole bed, which reads as waviness.
2. **Breakup** should **only rag the face/chamfer edge**. Today it offsets S everywhere, so it blobs and distorts the soft ramp as well.

## Fix 1: carved laminae
- Subtract only. Laminae are thin grooves cut into the surface, never bumps.
- Each groove is narrow, with a V or U profile at each lamina line: `groove = 1 - smoothstep(0, Width, distance to nearest line)` in lamina-phase units.
- Cut depth: `Strata -= Lamination * groove`. Lamination is now a depth, in the same units as the bed rise.
- **Ramp only.** Fade the grooves to 0 across the face, so the face stays clean. Use the existing ramp/face split (`T < RampEnd`).
- Keep the per-bed whole-number cross-bedding (it tiles); it now tilts the grooves.
- Controls:
  - `Lamination`: groove depth.
  - `LaminaCount`: lines per bed, an int. It replaces the `MIXTORMAT_STRATA_LAMINAE` define.
  - `LaminaWidth`: groove width as a fraction of the spacing.
  - `CrossBedding`: unchanged.
- The minimum groove width is one texel (convert to T units the same way `PixelT` does), so a groove never aliases into a stair. This is an additive term, not a clamp.

## Fix 2: breakup on the edge only
- Remove `Breakup * noise` from S. The ramp and T must be untouched by breakup.
- Instead, rag the **edge**: the point where the ramp ends and the face starts.
  - `n` = a periodic noise in 0..1.
  - `FaceStart = RampEnd - Breakup * n * <bed-relative scale>`. Breakup only eats back into the crest, never pushes the face out.
  - For `T` in `[FaceStart, RampEnd)`, the height follows the face curve, starting from the ramp height at `FaceStart`, so it stays continuous.
  - The ramp before `FaceStart` is identical to having no breakup.
- **Not blobby:**
  - Use noise stretched along the strike (long, thin chips along the edge), not isotropic blobs.
  - Isotropic periodic noise can't be stretched along an arbitrary strike and still tile. Instead, use 1D periodic noise along the strike coordinate `dot(UV, BedPerp)`, which advances by a whole number per tile, so it tiles. Add a little isotropic detail on top.
  - Add a per-bed phase offset so neighbouring beds don't chip in step.
- Controls:
  - `Breakup`: how far chips bite into the crest, in beds.
  - `BreakupScale`: chips per bed along the strike, an int (tiles).
- The face itself stays as Verticality sets it; only its top edge moves.

## Static checks
- dxc for `MixtormatStrataCarver.usf` at HV 2018 and HV 2021.
- Compare the shader globals against the `SHADER_PARAMETER` list.
- Update every touch point: the struct in `MixtormatMaterial.h`, the render data, the gather, the pass parameters, the inspector rows (pair LaminaCount with LaminaWidth, and Breakup with BreakupScale) and `Docs/index.html`.
- Delete the `MIXTORMAT_STRATA_LAMINAE` and `MIXTORMAT_STRATA_BREAKUP_SCALE` defines once they're replaced.

## Checklist for Hugo
- Lamination at 0.3: crisp grooves cut into each ramp, none on the faces, and no waviness.
- Cross Bedding at 2: the grooves fan per bed.
- Breakup at 0.2: only the crest edges get ragged, in long chips along the strike. The ramps stay smooth, and there are no blobs.
- Breakup at 0 is identical to before.
- All of the above stays seamless at Direction 0, 45 and 90.
