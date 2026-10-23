# Skin inspector audit — 2026-10-22

Static source audit requested by the user. No commands, builds, GPU profiling, runtime tests,
material asset reads, or implementation changes. Auto Height Scale has been removed.
Bindings alone do not establish visual correctness. The current scene and master graph were not inspected.

## Findings, in priority order

### 1. Vascularity is connected but strongly attenuated and not independently authorable

`Shaders/Private/MixtormatSkin.usf:953–1044` generates small branched capillary trees.
Their width, diffusion width, density and contrast are fixed constants: 0.014, 0.025,
0.6 and 0.35 respectively. Frequency is tied to `2 * TissueCells`.

The branch profile is reduced by diffusion and pixel integration, then taper/envelope,
random tree gain, spatial support, `1-exp(-Vessels)`, antialiasing, contrast 0.35,
`VascularAmount`, perfusion and inverse thickness. These factors compound.
`FieldCS` then uses Blood only for albedo absorption, MFP absorption and SSS amount.
It does not generate vessel height, normals, blue vein coloration or a connected anatomical network.

`Source/MixtormatRuntime/Private/MixtormatSurfacingTypes.cpp:42` sets Elephant vascular amount
at 0.02; the struct default is 0.12. This makes very low visible contrast plausible, but
is not a measurement of the user's scene. Raising amount cannot independently change
vessel size, density or diffusion. No vessel diagnostic is exposed in the Skin inspector.

**Recommendation:** make the intended vessel appearance explicit; expose vascular size,
width/visibility and a diagnostic before tuning contrast. Do not disguise the issue by
raising SSS or height globally. This is an implementation/authoring limitation, not a missing uniform.

### 2. Whole-surface tiling was discussed but never implemented

`MixtormatGpuSurfacingPasses.cpp:394` uses only layer Tiling × layer UV scale.
`DetailTiling` affects medium/fine detail, pores and grain, not all features.
There is no independent Surface Tiling control in `BuildSkinControls` or the Skin struct.

**Recommendation:** add one shared domain repeat multiplier for all Skin features while
keeping height gain independent. Do not reintroduce auto height scaling.

### 3. Cellular no longer controls all wrinkle topology

`SkinFoldFamily` blends marble and cellular forms using `Cellular`, but `SkinFineResidual`
unconditionally calls `SkinCellNetwork` for both medium and fine wrinkles. Cellular = 0
therefore does not remove cellular microdetail. The control still affects broad folds and
can indirectly affect micro amplitude through the broad-fold cue; it is not wholly disconnected.

**Recommendation:** restore an explicit, consistent topology policy for each wrinkle band.

### 4. Solver resolution leaks into shading independently of simulation

`MixtormatSkin.usf:1070–1078` derives material geometry probe spacing from `FoldState`
dimensions. Those dimensions depend on Solver Resolution even with zero iterations.
Where that spacing term dominates, changing mechanics resolution can alter roughness/fuzz
without changing the analytic resolved height. Bending additionally caps inverse-spacing²
at 256 in `MixtormatSkinSolve.usf`, so mechanics are not resolution-invariant.

**Recommendation:** decouple material probe spacing from mechanics texture dimensions;
address solver resolution dependence separately rather than calling it just a quality knob.

### 5. Curvature Strength leaks into tissue-direction response

`SeedCS` builds a combined tissue/upstream Hessian, then blends directors using
`max(TissueCoupling, CurvatureStrength)`. Increasing Curvature Strength can therefore change
tissue-driven direction even on a flat upstream substrate, when tissue anisotropy is present.

### 6. Optical custom controls bypass the shared binding presentation

`MixtormatInspectorSurfacing.cpp:142–213`: FuzzColor and MeanFreePath use direct custom
controls without `WrapParameterControl` or `GroupControlValueFor`. Albedo/Roughness ramps
explicitly use both. The custom optics controls do not display a group-driven effective
value or prevent edits to its overridden local value. Instances are guarded, group bindings are not.

### 7. Optical rendering remains unverified; fuzz is not proof of working SSS

SSS Amount and MFP reach Skin fields, `ApplySurfacingFields`, optical composition and
`DA_Optics.a` / `DA_Subsurface`. The expected master consumes RGB MFP in centimeters,
weighted by SSS amount. No disconnected SSS uniform was found.

Runtime defaults are 0.12 / 0.055 / 0.025 cm (1.2 / 0.55 / 0.25 mm); Elephant SSS Amount
is 0.2. Visibility depends on mesh scale, lighting and the actual material graph/render path.
Fuzz rendering establishes neither correct SSS wiring nor diffusion support. The prior
GBuffer explanation remains a hypothesis for this scene, not a confirmed diagnosis.

### 8. UI descriptions and conditional effects are misleading

- Softness directly changes tissue radius/amplitude, not fold-section softness as advertised.
- Elongation = 1 removes extra stretching; the base fold construction remains directional.
- Pore filtering includes a band fade to zero; it does not preserve area at all frequencies.
- Curvature Radius is divisor-floored in runtime contracts; zero cannot disable analysis.
- Controls remain editable when corresponding Write flags are off, without explaining why
  their visible channel does not respond. Cross-channel coupling means not all should simply
  be disabled; dependencies need explicit UI explanations.

## Complete control coverage

All listed scalar/int controls have gather and GPU bindings; no wholly unwired scalar was found.

| Inspector group / controls | Actual path and limitations |
|---|---|
| Amount | Apply-stage weight after scope masks; zero leaves incoming channels unchanged. |
| Height Scale | Signed gain in height resolve; also affects material curvature cues. Not a displacement-only visual slider. |
| Write Height, Color, Roughness, AO, F0, Fuzz, SSS | Apply-stage gates; raw field generation still executes for diagnostics. AO multiplies incoming AO; height adds; other channels interpolate. |
| Seed | Hash/noise seeds for tissue, folds, pores, pigment and vessels. |
| Tissue Cells | Tissue frequency, drift-related frequencies, vascular frequency and other coupled cues. Not isolated tissue size. |
| Fullness, Softness | Tissue amplitude/profile; Fullness also influences crease profiles. Softness limitation above. |
| Solver Iterations, Resolution | Correction solve budget/grid; zero iterations retains analytic height. Resolution/shading issue above. |
| Bending, Support | Solver rigidity/foundation; no simulation effect with zero iterations. |
| Fold Cells, Depth, Width, Direction | Broad/cross relief geometry; Width also affects wrinkle bands. |
| Compression, Tension | Seed load/stretch, solve strain, wrinkle/pore and vascular response. |
| Crossing | Adds cross relief and modifies fine gain; cross family does not get an independent solver director. |
| Tissue Coupling | Tissue-curvature direction and wrinkle/material response; intentionally cross-channel. |
| Marble Drift, Warp Scale, Warp Layers | Domain/phase wandering; layered advection is inactive when its drift contribution is zero. |
| Direction Variation, Elongation | Directional phase/advection construction; not a general UV rotation/scale pair. |
| Cellular | Broad-fold topology blend, not an off-switch for cellular microdetail. |
| Curvature Radius, Strength | Upstream analysis plus seed response; radius floor and direction leakage above. |
| Detail Cells, Detail Tiling | Microdetail frequencies; not global Skin tiling. |
| Pore Cells, Size, Depth, Variation | Pore lattice/ellipse/depth variation; support loop is capped and subpixel bands fade out. Depth zero retains pore diagnostic. |
| Grain | Fine noise relief amplitude. |
| Albedo Ramp, Pigment Variation | Samples pigment-driven ramp; at zero variation only its midpoint contributes, apart from vascular absorption. |
| Vascular Amount | Attenuated capillary absorption; no vein geometry or independent vessel sizing. |
| Thickness Variation | Tissue thickness affects relief, pigment, vessel diffusion and SSS. |
| Roughness Ramp, Cavity Roughness, Cavity AO | Tissue/material curvature and horizon occlusion; final roughness/AO saturate. |
| IOR | Converted to dielectric F0; requires Write F0. |
| Fuzz Amount, Roughness, Tint | Optical channels; amount additionally suppressed by cavities and tissue response. Tint custom binding issue above. |
| SSS Amount, Mean Free Path RGB | Optical amount and physical distances; custom binding and renderer uncertainty above. |

## Suggested repair order — not implemented

1. Vascular diagnostic and usable independent vessel controls.
2. Whole-surface tiling, without automatic relief rescaling.
3. Consistent Cellular behavior across wrinkle bands.
4. Remove mechanics-resolution dependence from material probes.
5. Correct curvature-direction coupling and custom optical binding behavior.
6. Verify SSS with actual output textures and master graph before changing render settings.
7. Correct tooltips and clearly indicate write-channel dependencies.
