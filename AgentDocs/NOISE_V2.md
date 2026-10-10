# Mixtormat — Noise System V2

**Original Agent B branch:** `feature/noise-v2`, merged to `main` in PR #1 (`fb3ebbd`).  
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

## Noise V2.2 — Jaggedness redesign

The V2.1 jagged warp was a cusp-shaped derivative hack:

```hlsl
Jagged = sign(N.yz) * sqrt(abs(N.yz));   // RETIRED
```

That is a nonlinearity applied *to the gradient vector*. A square root of a field that crosses
zero puts a sharp cone at every crossing, and the result is not the gradient of any coherent
scalar field, so it read as artificial distortion with repetitive square artifacts rather than
as fractured geometry. It has been replaced, not refined.

### What Rock Formation actually does

Read from `Shaders/Private/MixtormatRockFormation.usf`:

- `RockZig` (`:232`) — piecewise-linear random zigzag along an edge: straight segments between
  random bend heights in −1..1. Straight-sided, so it produces **direction reversals (creases)**
  at every breakpoint rather than smooth curvature.
- `RockZigFbm` (`:241`) — three octaves of that zigzag with lacunarity, normalized.
- `RockJag` (`:255`) — the zigzag scaled into an edge's sideways displacement.
- `RockLeafSdJag` (`:856`) — applies it as `- RockJag(...) * L.JagSize` to the edge's signed
  distance line, i.e. it displaces a **contour position**, not a field value.
- Amplitude is a **steepness**: `RockSetup` (`:947`) divides `EdgeJag` by `jag_freq`, so changing
  scale never changes spikiness.

So the mechanism is: *displace a contour by a multi-scale, creased zigzag*. There is no reusable
"jagged noise" helper in Rock Formation — it is edge-local, and coupling Noise to Rock's GPU pass
would be wrong. The mathematical method is what gets adapted.

### The Noise adaptation: fold the domain

The per-pixel equivalent of "displace a contour by a creased zigzag" is to displace the domain by
the **gradient of a folded noise field**. `MixtormatNoiseV2Jagged` (in `MixtormatNoiseV2.ush`) folds
with `F(n) = 1 − |n|^Exponent`, which reflects the field on every zero-crossing of `n`. Each
crossing becomes a crease; `grad(F)` is smooth everywhere except across that crease network.

| Requirement | How the fold satisfies it |
| --- | --- |
| Angular, broken, fractured | Creases are direction reversals on a zero-crossing network — the same signature as `RockZig` |
| No artificial derivative spikes | No nonlinearity on the gradient. `\|grad(F)\| ≤ Exponent · \|grad(n)\|`, bounded by the noise's own Lipschitz constant |
| No repetitive square artifacts | `sqrt` is gone entirely; the crease comes from the fold's `sign()`, not from a cone |
| Tileable on both axes | Fold operates on `frac(UV)` against integer period `Pk`; every octave period is an integer |
| Seeded, resolution independent | Periodic gradient noise; gradient scaled into tile UV by `Pk` |
| Adjustable frequency and intensity | Frequency = existing `NoiseDistortionFrequency`; intensity = `NoiseDistortionJaggedness` |
| Meaningful variation across scales | `NoiseJaggedDetail` adds up to 4 crease octaves, standing in for `RockZigFbm` |
| Neutral at 0 | Early return on `Strength <= 0`; `WarpDomain` early-returns when both strengths are 0 |
| Usable alone or mixed | Independent additive term, exactly as before |

`NoiseJaggedSharpness` sets `Exponent = 1 + 3·Sharpness`. At 0 the fold slope is `−sign(n)`, which
flips across the crease at full width. Higher values make `|n|^(Exponent−1) → 0` **at** the crease,
so displacement dies on the crease instead of spiking — the crease narrows and stays angular.

The generator path already computes a two-forward-probe numerical Jacobian of `WarpDomain` whenever
either distortion term is active, so creased gradients are picked up without new plumbing. Both new
parameters are threaded through all four call sites.

### Documented visual change

Materials with a **nonzero** `NoiseDistortionJaggedness` change appearance. This is intentional and
is the point of the redesign. Jaggedness = 0 is bit-for-bit unchanged, and the smooth/curl distortion
term is untouched. Two new reflected fields are appended with identity defaults (`0`), so no existing
saved material, enum value or Region ID is affected beyond the jagged term itself.

## Implemented vs deferred

| Priority | Item | Status |
| --- | --- | --- |
| P0 | Replace V2.1 jaggedness with Rock Formation-inspired fold | **Implemented** — `MixtormatNoiseV2Jagged`, new reusable helper |
| P0 | Jagged Sharpness / Jagged Variation controls | **Implemented** — appended, identity defaults, runtime → gather → bind → shader → inspector → JSON |
| P0 | Jagged Mix control | **Deferred** — the smooth and crease terms already add linearly; a mix control would be a redundant third parameter |
| P1 | Layered noise quality audit (Worley/Phasor/Bars/Gradient/Value) | **Audited, not changed** — see notes below |
| P1 | Worley Cell Depth rework | **Audited, not changed** — see notes below |
| P1 | Normalize button defect | **Investigated, no defect found** at the generator level; see notes below |
| P1 | Noise GPU scalar drivers | **Not implemented** — still blocked; see notes below |
| P2 | Conditional inspector visibility | **Partially** — new jagged rows collapse at zero; the broader per-family audit is deferred |
| P2 | Performance audit | **Partially** — octave count is bounded and period-capped; profiling not run |

### Audit notes (no code change made in these)

**Layered noise.** `MixtormatNoiseV2LayerOctave` (`MixtormatNoiseV2.ush:257`) already preserves F1 /
F2 / F2−F1 semantics per type, takes Region IDs from the base octave only, zeroes gradient where the
value saturates, and derives each octave's Bars wave vector by rounding `Wave·(Pk/BasePeriod)` to
stay tileable. The identified *quality* concern is real but is a tuning matter, not a correctness
defect: extra Worley octaves can speckle, and `LayerMix` currently only blends additional octaves
into the base rather than weighting them per-octave. Changing the blend math would change existing
authored appearance, so it was left alone rather than silently retuned.

**Worley Cell Depth.** The current implementation (`:239`) multiplies distance by
`1 + 0.75·Depth·valueNoise` and chain-rules the product. It is *not* cell-aware: it modulates
continuously rather than per-cell, so it will not produce genuinely distinct per-cell depth the way
a seeded-per-cell hash would. This is a genuine gap against the stated artistic intent and is the
strongest candidate for the next pass. It was not changed here because rewriting it alters authored
appearance for any material with nonzero Cell Depth, and that change should ship with its own
review rather than bundled into a jaggedness fix.

**Normalize.** `bNoiseNormalizeHeight` is intact at `MixtormatGeneratorTypes.h:1026`, flows through
`MixtormatGeneratorGather.cpp:302` into `ChildData.Generator.bNormalizeHeight`, is bound at
`MixtormatGpuGeneratorPasses.cpp:2935`, and the inspector checkbox is wired at
`MixtormatInspectorGenerators.cpp:1711`/`:1717`. The generator-level Normalize path therefore looks
correct by inspection. If the button in question is the **parameter-panel** Normalize action rather
than the generator's `bNoiseNormalizeHeight`, that is a different code path and was not reached.

**Noise GPU drivers.** Pixel-driven modulation of Noise scalars remains unimplemented, as
`NOISE_V2.md` already documented. The shared **Add Driver** popover may still be offered for numeric
Noise parameters whose binding the compositor ignores. That is a known honesty problem — either the
popover must be suppressed for unwired Noise parameters, or the bindings must be implemented. No
second driver framework was created.

## Noise family output ranges and the Normalize toggle

Audited every family against its native range. Two **separate** contracts are involved and must not
be conflated:

1. **Published `Value`** — each family keeps its native range. This is a documented contract that
   `MixtormatOutputReferences::NoiseValueKind` encodes on the CPU side, and mask coverage depends on
   it (`CoverageCS` maps signed families through `0.5·v + 0.5` and passes unsigned through
   unchanged). **This was deliberately left untouched.**
2. **Generator `Height`** — the shared signed contract, where zero is a neutral height.

| Family | Native range | Zero-centred? | Height mapping |
| --- | --- | --- | --- |
| Gradient | `±sqrt2 · noise`, bounded ≈ ±1 | Yes, by gradient-noise symmetry | as-is |
| Value | cell corners uniform in ±1, quintic-interpolated | Yes | as-is |
| FBM | amplitude-normalised sum, ≈ ±1 | Yes | as-is |
| Ridged | 0..1 (`1 − abs(noise)`) | **No** | `v·2 − 1` |
| Billow | 0..1 (`abs(noise)`) | **No** | `v·2 − 1` |
| Worley F1 / F2 / F1−F2 | 0..1 cell distance | **No** | `1 − v·2` (peak at cell centre) |
| Bars / Stripes | `cos` phase, ±1 | Yes | as-is |
| Phasor | fixed-amplitude normaliser `0.5/Components` | Yes, but see amplitude note | as-is |

**Result: every family was already zero-centred on the `Height` path**, so no family's signed height
was wrong. What *was* fragile is that the Height mapping was a hand-maintained `if/else` chain in
`MixtormatNoise.usf`, keyed on family and easy to omit when a family is appended — a new family would
silently arrive off-centre. That is now a single mapping, `MixtormatNoiseV2ValueRange` /
`MixtormatNoiseV2SignedHeight` in `MixtormatNoiseV2.ush`, which mirrors `NoiseValueKind` and defaults
to the signed case (no remapping needed). Behaviour is identical to the previous chain.

The same latent-drift hazard existed in `MixtormatNoiseV2LayerOctave`, whose dispatch used bare
integers `0/1/5/6/7/8/9` while the main dispatch used the `NOISE_TYPE_*` defines. Both now use the
defines, so the two dispatches cannot disagree about a family.

`MixtormatNoiseV2ValueRange` duplicates the Runtime enum mapping because a shader cannot include the
Runtime header. The duplication is deliberate and must be updated in both places.

### Phasor amplitude note (not changed)

Phasor is zero-centred but its normaliser is `0.5 / Components`, so its effective amplitude is well
below the ≈±1 of Gradient/Value/FBM/Bars. The same `NoiseHeightScale` therefore makes a Phasor read
noticeably weaker than the other signed families. This is a tuning inconsistency, not a correctness
defect, and correcting it would change the appearance of existing Phasor materials — flagged rather
than silently changed.

### Normalize toggle

`bNoiseNormalizeHeight` is intact and correctly wired end to end:
`MixtormatGeneratorTypes.h:1026` → gather `MixtormatGeneratorGather.cpp:302`
(`ChildData.Generator.bNormalizeHeight`) → bind `MixtormatGpuGeneratorPasses.cpp:2935` → inspector
checkbox `MixtormatInspectorGenerators.cpp:1711`/`:1717`. It operates on `Height` (already
zero-centred by the mapping above), which is the correct place for it. If the button reported as
inert is the **parameter-panel** Normalize action rather than the generator's `bNoiseNormalizeHeight`,
that is a separate code path and was not reached by this audit.

## Validation status

Static source review only. **No Unreal build, no shader compilation, no tests, no runtime or GPU
validation** was performed or is claimed. The shader, C++ and Slate changes are unverified by a
compiler. Reviewers should compile and visually check jagged tiling, crease behavior and gradient
quality before merging.

## Files changed

- `Shaders/Private/MixtormatNoise.ush` — Worley F2 gradient capture, Noise V2 helper include
- `Shaders/Private/MixtormatNoise.usf` — Phasor and metric Worley dispatch, distortion and source-frame gradient chain rule
- `Shaders/Private/MixtormatNoiseV2.ush` — new reusable algorithms
- `Source/MixtormatRuntime/Public/MixtormatGeneratorTypes.h` — noise-local enum and reflected parameters (Agent A coordination point)
- `Source/MixtormatShaders/Private/Compositing/MixtormatNoiseRender.h` — resolved render data
- `Source/MixtormatShaders/Private/MixtormatGpuNoisePasses.cpp` — bindings/sanitization, shared producer
- `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorGenerators.cpp` — generator/mask conditional controls
- `AgentDocs/NOISE_V2.md` — this report

## Noise V2.1 — layered families, jaggedness and authoring metadata

Feature branch `feature/noise-v2-layering`. New reflected parameters are appended with
identity/zero defaults; older materials and enum values remain unchanged.

| Parameter | Default | Purpose |
| --- | --- | --- |
| `NoiseLayerMix` | `0` | For Gradient, Value, Worley F1/F2/F2−F1, Bars and Phasor: blend additional octaves into the original field. At 0 the original output path is unchanged |
| `NoisePhasorScale` | `1` | Scale the signed Phasor Value; resulting field is clamped to −1..1, with zero derivative on flat plateaus |
| `NoisePhasorBias` | `0` | Offset signed Phasor Value before the final clamp |
| `NoiseWorleyCellDepth` | `0` | Periodic quintic-interpolated lattice modulation of Worley distance; feature points/Region IDs do not move. Analytic gradient includes the distance × modulation derivative |
| `NoiseDistortionJaggedness` | `0` | Independent higher-frequency sharp periodic vector warp. Works with or without nonzero smooth/curl distortion Strength |

For single-frequency families the Inspector shows `Layer Mix`; Detail/Roughness/Lacunarity
are shown only when Layer Mix is active. For native FBM/Ridged/Billow the existing
octave controls always appear and preserve their prior behavior. Phasor's extra Scale/Bias
rows and Worley's Cell Depth are family-specific. Placement Direction is hidden for
non-directional families; Distortion Direction is hidden at full Curl Mix. Inactive
controls are **collapsed**, not left greyed out.

Octave periods are rounded to integers and capped through the existing
`MixtormatNoiseOctavePeriod` function. Worley IDs always come from the base octave,
not the finer octaves, preserving downstream ID references. The jagged warp is
periodic and independent of smooth/curl distortion strength; both zero means strict
identity. The existing shader-side finite-difference Jacobian handles both warp
sources when either is active.

### Parameter authoring, references and drivers

- `Config/MixtormatParameterAuthoring.json` now ships a `Noise` section for
  **Generator** and **MaskNoise** parameter definition keys, including both enums and
  every numeric Noise parameter relevant to each owner. It records reset/default,
  UI bounds and snap metadata. Noise generator Height Scale is excluded from MaskNoise
  because masks read raw Value, not signed module Height.
- These are ordinary reflected `UPROPERTY` fields accessed through existing
  `MakeMemberSlider`, `MakeMemberSliderInt`, and `MakeMemberEnum`, so they get
  standard right-click **Copy/Paste Reference**, **Parameter Info** and
  **Edit Authoring Setup → Save to Plugin Defaults**. The nested `MaskNoise` address
  resolver and binding type are already supported by the shared parameter system.
- **Important limitation:** the shared `Add Driver` popover may be offered for numeric
  Noise parameters, but the compositor currently evaluates per-pixel mask/ID drivers
  only for explicitly wired parameters (layer RoughnessInfluence/HeightBlendAmount,
  structural warp FlowAmount/FlowTraceLength). This Noise batch does **not** add
  GPU pixel-driver bindings for each Noise scalar. Parameter *references* are supported;
  do not describe Noise scalar *pixel drivers* as operational until dedicated shader
  signal plumbing and dependency/snapshot rules are implemented.

### Performance and validation

Optional octave layering and Phasor can be expensive at high detail. Distortion
with nonzero strength or jaggedness incurs additional warp work and Jacobian probes
for generator gradients. Source review only; no Unreal build, shader compile,
GPU validation, visual tiling comparison, or performance profile.

## Static risks and merge gates

1. Unreal C++/HLSL/UHT compile compatibility remains unverified. The reflection fields and Shader parameters have been inspected by source only.
2. Phasor uses up to 9 local impulses × 4 components per pixel, each with phase trigonometry. It should be profiled at 512/2048/4096; the legacy Bars cost is unchanged.
3. Distortion-enabled generator fields evaluate additional warp probes for the chain-rule derivative. Numerically approximate; compare published gradients around seam/feature boundaries in a GPU preview before shipping.
4. Published `Gradient` still has heterogeneous family semantics: lattice/Phasor tile derivatives, Worley cell-distance gradients, and Bars phase direction. Do not reinterpret it as Flow, UV or a globally unified derivative semantic.
5. Fixed Worley F2/F2-F1 Gradient output differs in existing saved materials referencing that output; their Value/Height/RegionIds remain unchanged. Review downstream use before merge.
6. Noise output tileability is for periodic source UV. Arbitrary fractional/rotated **layer placement** can move the visible output seam; authored placement does not imply that destination edges always agree.
7. A separate public typed Curl Vector2 field requires agreement with Agent A on publication, reference resolution, source menu and interpolation. **No Behavior V2 change was made here.**
8. Coordinate shared runtime header edits with Behavior V2 when it lands; Noise V2 itself merged into `main` in PR #1.

**Validation status:** source-review ready; merged Noise V2 remains unverified by local C++/shader builds or Unreal GPU runtime.
