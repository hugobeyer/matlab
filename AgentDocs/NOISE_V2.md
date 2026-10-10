# Mixtormat — Noise System V2

**Agent B branch:** `feature/noise-v2` (started from `main` `f94c9e3`).  
**Scope:** Noise producer and Noise-specific reflected settings/Inspector; no Behavior V2 architecture or execution changes.  
**Validation:** static source inspection only. No builds, automated tests, Unreal startup, shader compilation, visual comparison or GPU profiling were authorized or performed.

## Pipeline

`FMixtormatNoise` (generator or inline Mask) → `ResolveNoiseRenderData` → `FMixtormatNoiseRenderData` → `MixtormatGpuNoisePasses.cpp` → `MixtormatNoise.usf` → signed generator Height or published typed Value/Gradient/IDs, or inline mask coverage. A shelf Noise root uses the generator Value publication, then the mask Value-to-Coverage path. No separate generator, mask or Source implementation of the algorithms.

- Existing `MixtormatGully.ush` supplies periodic gradient noise, analytic derivatives and existing curl. `MixtormatCellular.ush` supplies wrapped seeded cells/hashes.
- `MixtormatNoiseV2.ush` adds opt-in compact-support multi-impulse phasor, multiscale periodic curl vector evaluation, three-metric Worley/Jitter and mixed directional/curl domain warp.
- Phasor is appended as `EMixtormatNoiseType::Phasor = 9`; old Noise family numeric values 0–8 and all old fields are unchanged. `EMixtormatNoiseWorleyMetric` is a new property enum (0 Euclidean, 1 Manhattan, 2 Chebyshev).
- `NoiseDistortionStrength = 0` bypasses distortion exactly. `NoiseWorleyMetric = Euclidean` / `NoiseWorleyJitter = 1` delegates to the original Worley sampler, including pre-existing large periods.
- `MixtormatNoise.usf` computes warped gradients with a two-forward-probe numerical Jacobian only when distortion is nonzero, preserving the unwarped derivative path.
- The same noise-specific Inspector builder is used by generator Noise and inline Mask Noise. Phasor and Worley controls are conditional. The separate Distortion panel is shown for all families.
- Curl remains a `float2` helper used by domain warp. This branch **does not** publish Curl as FlowDirection, UV or a new generic Vector2 output. Future Behavior V2 typed field consumption must be coordinated.

## Completed stages

| Priority | Outcome | Compatibility |
| --- | --- | --- |
| P0 | Audited nine prior families, typed outputs, gather, mask, shelf, hashes, derivatives and GPU allocations | Existing Value/Height/IDs retained |
| P0 | Fixed F2/F2-F1 nearest-point gradient reuse and set gradient to zero where Worley Value saturates | **Intentional correction to published Gradient for old Worley F2/F2-F1 materials** |
| P1 | New tileable multi-impulse Phasor family with analytic gradients, direction, frequency, anisotropy and seeded local phase/orientation variation | Appended type, old Bars unchanged |
| P2 | Reusable 1–8 octave periodic Curl vector helper, no duplicate flow solver | Consumed by distortion, not separately published |
| P3 | Euclidean, Manhattan, Chebyshev, adjustable jitter, same stable cell-ID hashing | Defaults route through legacy implementation |
| P4 | Optional multiscale periodic domain distortion: directional / curl mixed at authored strength, direction, frequency and detail | Strength zero exact identity |
| P5 | Static review of RDG allocations and algorithmic costs | Profiling and shader validation pending |

## Before / after feature matrix

| Feature | Before | Noise V2 branch |
| --- | --- | --- |
| Gradient, Value, FBM, Ridged, Billow | All available | Same algorithms, default appearance preserved |
| Worley F1/F2/F2-F1 | Euclidean, jitter=1, stable IDs | Adds metric and jitter selectors, fixes incorrect gradients |
| Bars / Stripes | Global integer snapped cosine phase | Unchanged |
| Phasor | None | Authorable independent family, locally overlapping periodic impulses |
| Curl | Two-octave shared utility | Multiscale general vector utility and curl distortion |
| Domain warp | No Noise-local authoring | Shared noise family Distortion panel, zero-neutral |
| Generator / mask / shelf | Common Value producer / typed mask conversion | New families and controls use same producer, no duplicated noise implementation |

## Added parameters and shipped defaults

| Parameter | Default | Applicability |
| --- | --- | --- |
| `NoisePhasorFrequency` | `2.0` | Phasor |
| `NoisePhasorAnisotropy` | `0.0` | Phasor |
| `NoisePhasorPhaseVariation` | `0.5` | Phasor |
| `NoisePhasorOrientationVariation` | `0.35` | Phasor |
| `NoisePhasorComponents` | `2` | Phasor, 1–4 |
| `NoiseWorleyMetric` | `Euclidean` | Worley |
| `NoiseWorleyJitter` | `1.0` | Worley |
| `NoiseDistortionStrength` | `0.0` | All Noise, exact bypass |
| `NoiseDistortionFrequency` | `4.0` | Distortion |
| `NoiseDistortionOctaves` | `2` | Distortion |
| `NoiseDistortionRoughness` | `0.5` | Distortion |
| `NoiseDistortionLacunarity` | `2.0` | Distortion |
| `NoiseDistortionCurlMix` | `1.0` | Distortion, 0 directional to 1 curl |
| `NoiseDistortionDirection` | `0.0` | Directional distortion |

Existing parameters, serialized family numeric values, `bNoiseNormalizeHeight`, `NoiseHeightScale`, IDs and raw Value semantics are retained. Normalization remains downstream of raw Noise evaluation; new Noise Height follows the same signed-zero generator contract.

## Files changed

- `Shaders/Private/MixtormatNoise.ush` — Worley F2 gradient capture, Noise V2 helper include
- `Shaders/Private/MixtormatNoise.usf` — Phasor and metric Worley dispatch, distortion and source-frame gradient chain rule
- `Shaders/Private/MixtormatNoiseV2.ush` — new reusable algorithms
- `Source/MixtormatRuntime/Public/MixtormatGeneratorTypes.h` — noise-local enum and reflected parameters (Agent A coordination point)
- `Source/MixtormatShaders/Private/Compositing/MixtormatNoiseRender.h` — resolved render data
- `Source/MixtormatShaders/Private/MixtormatGpuNoisePasses.cpp` — bindings/sanitization, shared producer
- `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorGenerators.cpp` — generator/mask conditional controls
- `AgentDocs/NOISE_V2.md` — this report

## Static risks and merge gates

1. Unreal C++/HLSL/UHT compile compatibility remains unverified. The reflection fields and Shader parameters have been inspected by source only.
2. Phasor uses up to 9 local impulses × 4 components per pixel, each with phase trigonometry. It should be profiled at 512/2048/4096; the legacy Bars cost is unchanged.
3. Distortion-enabled generator fields evaluate additional warp probes for the chain-rule derivative. Numerically approximate; compare published gradients around seam/feature boundaries in a GPU preview before shipping.
4. Published `Gradient` still has heterogeneous family semantics: lattice/Phasor tile derivatives, Worley cell-distance gradients, and Bars phase direction. Do not reinterpret it as Flow, UV or a globally unified derivative semantic.
5. Fixed Worley F2/F2-F1 Gradient output differs in existing saved materials referencing that output; their Value/Height/RegionIds remain unchanged. Review downstream use before merge.
6. Noise output tileability is for periodic source UV. Arbitrary fractional/rotated **layer placement** can move the visible output seam; authored placement does not imply that destination edges always agree.
7. A separate public typed Curl Vector2 field requires agreement with Agent A on publication, reference resolution, source menu and interpolation. **No Behavior V2 change was made here.**
8. Coordinate runtime header edits with Agent A before integrating into the main development branch. This Noise branch is not merged.

**Merge readiness:** code is source-review ready; not build-verified or runtime-verified. Keep as feature branch until the above gates are closed.
