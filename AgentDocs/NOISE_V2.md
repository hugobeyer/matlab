# Noise System V2 — Agent B implementation and integration handoff

Base: `main` at `f94c9e3062939bdca87ceedbdc355adc4261fce1`.
Branch: `feature/noise-v2`.
Scope: noise-specific shaders only; no Behavior V2, serialized runtime, common compositor, inspector, or mask changes.

## Source-traced baseline

- `FMixtormatNoise` in `MixtormatGeneratorTypes.h` owns the nine existing serialized types and their defaults.
- `ResolveNoiseRenderData` sanitizes the payload. The generator gather transfers settings to `FMixtormatNoiseRenderStore`. `AddNoiseFieldPass` binds the same `MixtormatNoise.usf` for generator and inline-mask value-only production.
- `AddNoiseMaskPass` converts the raw signed/unsigned Value to 0..1 Coverage with a separate pass. Live and shelf Noise Value references use the typed published-field resolution rather than shader duplication.
- Main generator dispatch writes R32 Value, R32 signed Height, RG32 Gradient, and only for Worley R32 cell IDs. Flow is derived separately from completed Height, not from the heterogeneous Gradient.
- `MixtormatGully.ush` already provides `MixtormatPeriodicNoiseD` and `MixtormatPeriodicCurlField/Warp`; `MixtormatCellular.ush` already provides wrapped seeded cell points and hashes. Avoid duplicating them.

## Stage status

| Stage | Result | Files | Compatibility / limits |
| --- | --- | --- | --- |
| P0 audit | Source-traced all nine algorithms, hashes, period rounding, derivative meanings, producer paths, and texture allocations | No file required | Static inspection only; no visual/runtime proof of seam or quality |
| P0 Worley | Track the second-nearest feature delta; publish family-correct F2 / F2-F1 gradients and zero the derivative on saturated Value plateaus | `MixtormatNoise.ush`, `MixtormatNoise.usf` | **Intentional correction** to published Gradient for saved F2 / F2-F1 projects; Value, Height, IDs, seeds and controls unchanged |
| P1 Phasor shader core | Seeded, compact-support periodic Gabor/phasor impulses, local orientation/phase variation, 1–4 overlapping layers, frequency and anisotropy, analytic UV-space derivative | `MixtormatNoiseV2.ush`, included by `MixtormatNoise.ush` | Opt-in helper only; not an authorable NoiseType until shared runtime/UI coordination |
| P2 Curl shader core | Multi-octave periodic curl of the shared analytic gradient noise with deterministic seed, constant direction bias, and scalar strength | `MixtormatNoiseV2.ush` | Opt-in Vector2 helper, **not** a Flow/UV publication or a solver |
| P3 Worley shader core | Opt-in Euclidean / Manhattan / Chebyshev distances, jitter 0–1, nearest/second-nearest gradients, wrapped stable cell ID | `MixtormatNoiseV2.ush` | Existing full-jitter Euclidean path is directly delegated to legacy Worley |
| P4 Distortion shader core | Directional scalar or curl-driven periodic domain displacement, 1–8 octaves, strength and mix; strength zero returns UV exactly | `MixtormatNoiseV2.ush` | Opt-in coordinate helper; not attached to legacy evaluation/Gradient publication |
| P5 inspection | Legacy producer runs one value dispatch; generator also allocates Gradient and Height, and Worley alone allocates IDs. Inline masks use one value texture plus one conversion pass. Phasor multi-component cost scales up to 36 local impulses per pixel | No pass rewrites | No GPU timings; shader compile, 512/2048/4096 images, tests and runtime validation **not performed** |

## Findings and remaining risks

1. **Confirmed and fixed:** F2 and F2-F1 returned the F1 gradient, despite their different distance functions.
2. **Existing semantic compromise:** Worley publishes cell-width directional derivatives, while lattice families publish tile-UV analytic derivatives and Bars publishes a unit phase direction. The output type is generic `Vector2`; downstream consumers must not infer Flow, UV or a common derivative scale.
3. **Worley clamp:** Values outside 0..1 are constant after saturation, and P0 now publishes a zero gradient there. Exactly at distance 0 or 1 and at closest-feature ties, the analytic derivative is undefined; the shader picks a stable convention.
4. **Period and placement:** Native lattice evaluation is periodic in both axes and uses UV, not texel coordinates. Arbitrary generator placement (especially fractional scale or rotation) can alter where the boundaries of a *placed* tile lie; it is not proof that transformed output always matches at unit-square edges.
5. **Phasor cost:** 9 cell samples per component, up to four components, with transcendentals. Avoid forcing this cost onto existing Bars. Profile GPU instruction count when authorized.
6. **Domain distortion gradient:** Warping sample positions requires a Jacobian when publishing a transformed analytic Gradient. No such publication is wired in this branch; do not reuse an unwarped derivative as a warped derivative.
7. **Unverified:** Shader compilation, physical output, deterministic cross-resolution comparisons, border-seam images, profiler counters, mask/shelf visual parity and high-frequency aliasing require permission to run builds/tests/runtime validation.

## Before / after feature matrix

| Feature | Before (main) | This branch |
| --- | --- | --- |
| Legacy nine noise modes | Generator, inline mask, shelf typed Value | Preserved, same authored controls |
| Legacy Worley Value/Height/IDs | F1, F2, F2-F1 | Unchanged |
| Legacy Worley Gradient | Incorrect F1 reuse in F2/F2-F1; nonzero plateau gradients | Correct F2/F2-F1 direction and zero plateau gradients |
| Generalized phasor | Only existing Bars cosine wave | Opt-in periodic impulse phasor helper, not yet UI-exposed |
| Curl | Shared two-octave curl in Gully | Reused foundation; opt-in multi-octave curl field |
| Worley metrics/jitter | Euclidean, fixed jitter=1 | Opt-in metrics + jitter helper |
| Domain warp | Shared curl helper in Gully | Opt-in mixed directional/curl periodic domain warp |

## Proposed shared-system work — NOT changed on Agent B branch

Coordinate these with Agent A or the owner of serialized types before authoring:

- Append `Phasor` to `EMixtormatNoiseType` (existing indices 0–8 unchanged); classify its Value as signed in `MixtormatOutputReferences::NoiseValueKind`.
- Append reflected `NoisePhasorFrequency=2.0`, `NoisePhasorAnisotropy=0`, `NoisePhasorPhaseVariation=0.5`, `NoisePhasorOrientationVariation=0.35`, `NoisePhasorComponents=2`. These are **proposed defaults**, not present or persisted in the runtime.
- For new Worley editing: `NoiseWorleyMetric=Euclidean(0)`, `NoiseWorleyJitter=1.0`; neither has been added to `FMixtormatNoise`.
- For domain warp: `NoiseDistortionStrength=0.0`, `NoiseDistortionFrequency=4.0`, `NoiseDistortionOctaves=2`, `NoiseDistortionRoughness=0.5`, `NoiseDistortionLacunarity=2.0`, `NoiseDistortionCurlMix=1.0` (all proposed, none serialized). Direction should be explicit and sampled periodically.
- Expose an independent typed `Vector2` output (suggested name `Curl`) only after the published-field declarations, validation, gather and source menus all agree; **never** publish it as `FlowDirection` or UV implicitly.
- Trace any additions end-to-end: runtime `FMixtormatNoise` → reflected parameter metadata / instance owner → `ResolveNoiseRenderData` → `FMixtormatNoiseRenderData` → GPU pass uniforms → `MixtormatNoise.usf` dispatch → OutputReferences/value type → inspector → masks/shelf/reference compatibility → copy/paste/undo/cache/hash.
- After coordination, integrate new algorithms into the existing generator and mask producer dispatch, preferably behind explicit appended family values and zero-neutral distortion, without a separate incompatible pipeline.

## Merge readiness

- **P0 shader fix:** ready for a source-level review, but not validated or GPU-compiled.
- **P1–P4 helpers:** ready for API review, **not feature-complete authoring**. Do not describe these algorithms as available in UI or existing saved materials.
- **Full Noise V2:** not merge-ready until shared-schema coordination, dispatch integration, and authorized validation.
