# Mixtormat — Flow / Sculpt shader handoff for Terra

Branch: `feature/behavior-system-v2` (changes committed directly).

The GPU operation contract now separates Flow generation, vector tracing, height-only transformation, signed Push and traced Sculpt. This document overrides the older Flow/SDF steering and Carve/Deposit UI sections in `FINAL_BEHAVIOR_PLAN.md`.

## Available shader paths

- `MixtormatNoise.usf` / `FMixtormatNoiseGeneratedFlowCS`: Noise V2 Flow generator. MODE = Height + Slope + Curl + Force, with signed independent weights and signed Strength. `Force` starts at `Angle`; `Gravity` / Surface Follow bends Force using the accumulated Height's downhill gradient. No SDF. `FlowAdd` supports negative values; `FlowMix` is 0..1.
- `MixtormatFlowTrace.usf` / `FMixtormatCanonicalFlowTraceCS`: independent canonical Flow RK2-to-UV conversion. Signed Flow magnitude is preserved. This is the automatic accumulated-Flow path for modern Warp/Deform.
- `MixtormatBehaviorSculpt.usf` / `FMixtormatBehaviorSculptCS`: traces accumulated Flow through the current Height, gathering height extrema. **Signed Depth**: positive cuts, negative raises, zero is neutral. It does not use SDF, source selection or Groove/Deposit mode. Produces height plus a diagnostic mask, currently published under the compatibility key `CarveMask`.
- `MixtormatBehaviorWarp.usf` / `SignedPushCS`: Push remains a signed Height-only operation, never traces Flow. The obsolete `SignedCarveCS` entrypoint and C++ registration have been removed.
- Warp remaps the full generator bundle; Deform remaps Height only. Traced Deform and traced Warp use the working Flow and dedicated canonical Flow-trace pass. Standard Deform remains available.

## UI/serialized parameter mapping

**Flow (Noise V2):**

| Desired label | Existing backing property | Notes |
|---|---|---|
| Height | `FMixtormatNoise::NoiseDirectionHeightWeight` | Signed |
| Slope | `NoiseDirectionSlopeWeight` | Signed; preceding Height |
| Curl | `NoiseDirectionCurlWeight` | Signed; reuse Noise V2 curl |
| Force | `NoiseDirectionConstantWeight` | Signed; historical serialized name retained until UI migration |
| Angle | `NoiseDirectionAngle` | Degrees, 0 = +U |
| Gravity / Surface Follow | `NoiseDirectionGravity` | **New**; default 0, nonnegative |
| Strength | `NoiseDirectionStrength` | Signed |
| Add | `NoiseFlowAdd` | Signed |
| Mix | `NoiseFlowMix` | 0..1 |

GPU render data and shader uniform now call the Constant contribution `ModeForceWeight`. Do not recreate a Constant UI row. A rename of the underlying serialized `NoiseDirectionConstantWeight` needs corresponding parameter-binding/authoring metadata changes, not just a partial C++ rename.

**Sculpt:** The current serialized `EMixtormatBehaviorType::Carve` is temporarily the backing enum for Sculpt. Rename its editor label/menu entry to **Sculpt**. Expose `Behavior.Flow.Depth` as a signed slider; retain `Behavior.Strength`, `Flow.Width`, `Flow.TraceLength`, `Flow.TraceSteps` and `Flow.Falloff`. Remove the Standard/Traced group, Groove/Deposit group, Steering Source dropdown, SDF source picker, boundary width, and obsolete Carve Mode UI. No special-purpose SDF source is needed. `Depth` and `TraceLength` retain parameter drivers.

**Deform:** Keep Standard / Traced. Remove Signed Distance steering and Shape Offset. Standard retains existing height-gradient/working-Flow behavior. Traced consumes the ordered accumulated Flow. No SDF option. `StandardTraceLength/Steps` apply to Standard; `TraceLength/Steps` to Traced.

**Warp:** Works on the entire procedural bundle, with automatic Flow. Do not present SDF steering. Existing ModeMix data is retained for UI cleanup; modern tracing does not launch another Flow solve.

**Push:** Signed Height only, masks and supported Height composition. Remove nested Flow Field creation and routing affordances.

**SDF:** Retained as independent native generator field for masks, color, boundary effects, and a *future separate* distance-to-Height shaping chain. Do not add SDF to Flow MODE, Deform, Warp or Sculpt.

## Remaining removal work

The old `MixtormatGeneratorFlow.usf` and `FMixtormatGeneratorFlowCS` still exist for the obsolete `FlowField` Behavior and for the legacy explicit Flow-reference stage. The automatic canonical Flow path, standard/traced Deform and new Sculpt no longer use that solver. **Do not delete the shader file yet without first eliminating those remaining calls and updating the editor creation/preview/field-reference paths.** The old Carve/Deposit shader mode may therefore still exist inside that legacy file but is not used by Sculpt.

The editor still has some pre-refactor controls and old labels; Terra owns their removal. The shader/CPU changes were reviewed by source inspection only; UE5.8 compilation, shader binding, visual behavior and runtime performance have not been verified. Do not claim the old solver is fully removed or the new path build-tested.
