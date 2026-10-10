# Mixtormat — P0 Unified Field Contract

Branch: `feature/behavior-system-v2`. Engine: Unreal Engine 5.8.
Primary source of truth: `AgentDocs/FINAL_BEHAVIOR_PLAN.md` (sections 1–6, 9, 10, 14).
This document is the P0 deliverable: the single contract every Generator and Behavior
consumes from P1 onward. It defines semantics only; GPU writers and readers migrate in
P1/P2 and no existing texture layout has been reinterpreted.

Validation status: static source review only. No UHT/C++ build, shader compile or GPU
runtime validation has run. See section 9.

---

## 1. Canonical Flow representation

One shared vector-field contract for Noise, Generators and Behaviors
(FINAL_BEHAVIOR_PLAN section 9).

### 1.1 GPU/RDG target layout

| Component | Storage | Meaning |
|---|---|---|
| `VectorXY` | float4 `.rg` (PF_FloatRGBA) | Movement direction **and magnitude**. Not normalized by the pipeline. |
| reserved | float4 `.b` | Zero. Reserved for P1+; writers must write 0. |
| `Influence` | float4 `.a` | Spatial attenuation weight, 0..1. Separate concept from magnitude. |
| `Validity` | separate R16F texture | 1 where meaningful directional data exists, 0 elsewhere. |
| Signed distance | separate field | Its own texture/contract. Never packed into Flow, never read as generic magnitude. |

Rules:

- `VectorXY` carries magnitude. The final combined vector is **never normalized
  automatically**; zero magnitude must remain exactly zero.
- `Influence` is spatial attenuation (mask, reach, coverage). It shapes *where* a
  contribution applies, not how far it points.
- `Validity` answers *whether directional data is meaningful at this texel*. Invalid
  texels contribute nothing and never invent movement.
- Signed distance stays an independent field (the existing `SDF` /
  `(distance, validity)` boundary contracts). Arbitrary Vector2 outputs are not Flow
  without an explicit conversion algorithm.

### 1.2 Audit of the two existing paths

| Path | Current float4 layout | Current validity | Incompatibility |
|---|---|---|---|
| Noise `FlowCS` (`MixtormatNoise.usf`) | `.xy` = normalized downhill unit direction, `.z` = 0, `.w` = validity | `.w` channel **and** separate R16F texture | Normalized direction: magnitude is destroyed; validity duplicated in `.w` |
| Behavior flow solve (`MixtormatGeneratorFlow.usf` stage 2) | `.xy` = unit direction, `.z` = signed UV distance, `.w` = influence | separate R16F texture | Signed distance packed into the Flow float4; influence in `.w` |

These are not the same contract and must not be silently reinterpreted. Existing
texture channels keep their current meaning until each writer migrates.

### 1.3 Transition plan (no silent reinterpretation)

- **P1 (Noise writer):** `FlowCS` writes the canonical layout — `.rg` = downhill
  vector with magnitude (central-difference slope, not divided by its length),
  `.b` = 0, `.a` = 1 where valid / 0 where not; the R16F validity texture keeps its
  role. The published `Gradient` output keeps its heterogeneous family semantics and
  is not reinterpreted as Flow.
- **P2 (Behavior reader/writer):** the flow solve resolve stage writes the canonical
  layout; signed distance moves fully to the separate boundary/SDF field it already
  has; influence moves to `.a`. Consumers (RK2 trace, Warp/Deform/Carve) read the
  canonical layout from that point.
- Readers added in P1/P2 read only the canonical layout. Legacy readers keep working
  against their legacy writers until their writer migrates; no dual-format reader is
  created.

---

## 2. Noise / Flow output contract

Both creation presets instantiate the existing Noise V2 implementation
(`FMixtormatNoise` → `ResolveNoiseRenderData` → `MixtormatGpuNoisePasses.cpp` →
`MixtormatNoise.usf`). No second Noise engine.

### 2.1 Output flags (serialized on `FMixtormatNoise`)

| Property | Type | Default | Meaning |
|---|---|---|---|
| `bNoiseWriteHeight` | bool | `true` | Module contributes signed height to its Generator layer. |
| `bNoiseWriteFlow` | bool | `false` | Module adds its generated Flow to the working Flow field. |

Exact behavior:

- The flags are **independent**. Any combination is valid.
- `bNoiseWriteHeight = false` → zero height contribution. The internal scalar noise
  still evaluates wherever gradients or directional generation need it. Flow
  calculations must not trigger height normalization or add hidden relief; the
  Noise-specific centring/gate path runs only when height is actually written.
- `bNoiseWriteFlow = false` → the module does not add Flow to the working field. It
  **keeps** its published `Gradient` and `FlowDirection` outputs for preview and
  other consumers (published-output system, preview key, Sources shelf).
- Defaults are the Noise preset's identity state, so existing serialized assets load
  unchanged. New properties are appended UPROPERTYs; no enum or field value moved.

### 2.2 Neutral states

- Both flags off: the module publishes its typed outputs but contributes neither
  height nor working Flow.
- Height-only: working Flow is untouched by the module.
- Flow-only: height contribution is exactly zero; no normalization side effects.

---

## 3. Weighted MODE contract

Four simultaneous directional contributions. No dropdown.

| Weight | Input basis |
|---|---|
| `HeightWeight` | Downhill gradient of **this node's own** noise height. |
| `SlopeWeight` | Downhill gradient of the **accumulated** height from preceding operations. |
| `CurlWeight` | Existing Noise V2 curl vector (`MixtormatNoiseV2Curl`), reused, not duplicated. |
| `ConstantWeight` | Uniform Vector2 from `Angle`. |

Formula (FINAL_BEHAVIOR_PLAN section 3):

```
Own    = DownhillGradient(OwnNoiseHeight);
Slope  = DownhillGradient(CurrentHeight);
Curl   = CurlNoise(UV);
Const  = float2(cos(Angle), sin(Angle));

GeneratedFlow =
    HeightWeight   * Own
  + SlopeWeight    * Slope
  + CurlWeight     * Curl
  + ConstantWeight * Const;

GeneratedFlow *= Strength;
```

### 3.1 Conventions

- **Downhill sign:** downhill direction = negative surface gradient
  (`-grad(Height)`), matching the existing `FlowCS` and `HeightGradientCS`.
- **Angle orientation:** degrees, counterclockwise from +U in tile UV with V up;
  the GPU writes `(cos Angle, -sin Angle)` into the texture's V-down frame.
  0° points +U, 90° points "up" in the authored material.
- **Magnitude semantics:** each basis carries its natural magnitude; the combined
  vector is never normalized. Weights scale contributions linearly; opposing
  contributions cancel; a zero result stays exactly zero.
- **Resolution independence:** gradient bases are computed per UV unit (the
  existing `0.5 * OutputSize * delta` scaling), so changing texture resolution or
  Noise Scale does not multiply movement strength. Curl is already
  resolution-independent.
- **Tileability:** all bases derive from the periodic lattice/wrapped sampling the
  Noise V2 helpers already use; the combined field tiles exactly on the source UV.
- **Zero-weight identity:** a term with weight 0 contributes exactly nothing; with
  all weights 0 (or Strength 0) `GeneratedFlow` is exactly `float2(0, 0)`.
- **Invalid / unavailable inputs:** an invalid or flat basis contributes zero and
  sets validity 0 for its term — no invented movement, no NaN propagation. A
  combined vector is valid only where at least one contributing basis is valid;
  where none are, the generated contribution is `(0, 0)` with validity 0.

### 3.2 Serialized defaults (Noise preset)

`HeightWeight = 1`, `SlopeWeight = 0`, `CurlWeight = 0`, `ConstantWeight = 0`,
`Angle = 0`, `Strength = 1`, `Add = 1`, `Mix = 0`. Flow preset overrides
(Height 0, Curl 1, Write Height OFF, Write Flow ON) are applied by P1 creation
logic, not by different serialized defaults.

---

## 4. Flow composition contract

Exact evaluation order and formula (FINAL_BEHAVIOR_PLAN section 4):

```
GeneratedFlow = <MODE formula from section 3>;
GeneratedFlow *= Strength;

MixWeight = saturate(Mix * Mask);
AddWeight = Add * Mask;

FlowOut = lerp(FlowIn, GeneratedFlow, MixWeight) + GeneratedFlow * AddWeight;
```

`Mask` is the child mask (1 when absent). `Add` may be negative (subtract the
generated contribution); `Mix` is clamped by `saturate`.

### 4.1 Identity and neutral behavior

| Condition | Result |
|---|---|
| `Add = 0` and `Mix = 0` | Exact identity: `FlowOut = FlowIn`. |
| `Mask = 0` | Exact identity: both weights are 0. |
| `Strength = 0` | Generated contribution is exactly `(0, 0)`. With `Add = 0` this is identity; **with `Mix > 0` it intentionally interpolates the existing Flow toward zero** — `lerp(FlowIn, 0, MixWeight)`. Strength = 0 is *not* an identity guarantee when Mix is enabled. |
| No valid direction (validity 0 everywhere contributing) | Generated contribution is `(0, 0)`; no invented movement. With `Mix > 0` the same toward-zero interpolation applies, driven by the zero vector, not by an error state. |

The single exact-identity condition is `Add = 0 AND Mix = 0` (or `Mask = 0`).
This resolves the apparent contradiction between "Strength = 0 is neutral" and the
lerp formula: Strength = 0 neutralizes the *generated* contribution; Mix still
attenuates the *existing* field toward that zero contribution by design.

### 4.2 Evaluation order

1. Compute `GeneratedFlow` (MODE formula, section 3).
2. Scale by `Strength`.
3. Resolve `Mask` (child mask, 1 when absent).
4. Compute `MixWeight = saturate(Mix * Mask)`, `AddWeight = Add * Mask`.
5. Apply the combined formula once. Add and Mix are one expression, not two passes.

---

## 5. Automatic field inheritance

Working fields of a Generator evaluation (conceptual; the existing RDG bundle
remains the implementation vehicle):

`Height`, `Flow`, `Coverage`, `RegionIds`, `Boundary/SDF`, published named outputs.

### 5.1 Rules

- A child reads the working state produced by **preceding siblings only**. It never
  reads values produced by later siblings. Flat sibling execution in authored order;
  strictly decreasing read order makes feedback loops impossible.
- **Flow** modifies working Flow (section 4 formula).
- **Push** modifies Height only. No implicit horizontal transport.
- **Distort** remaps the complete spatial bundle (height, IDs, coverage, and the
  vector fields — see 5.3).
- **Deform** modifies Height only; IDs/coverage stay at authored placement.
- **Carve/Deposit** modifies Height (plus its optional diagnostic Carve Mask).
- Initial working Flow is the generator's intrinsic FlowDirection where it exists;
  otherwise a valid height provides the downhill fallback; otherwise zero. The
  fallback never invents motion on a flat field.
- A generator with `bNoiseWriteFlow = true` does not add its intrinsic FlowDirection
  to itself a second time: the intrinsic field *is* the initial working Flow; Write
  Flow governs the node's generated contribution to its containing context.

### 5.2 Snapshot and ordering

- Each Behavior evaluates against a snapshot of the working fields as of the end of
  the previous sibling. Writes go to the working state after the read snapshot is
  taken, so a node cannot observe its own output.
- Slope reads the height available at that stack position: a Push *before* a
  Slope-weighted Flow is included in the slope; a Push *after* is not. Ordering is
  meaningful and preserved by authored sibling order.
- Height normalization and Height Scale stay after all children (existing
  PostGeneration position); Flow math never triggers them.

### 5.3 Vector fields after Distort

Distort changes the spatial frame. After a coordinate remap:

- Vector fields are transport-sampled through the same remap (the existing
  `SourceFrameVector` semantic: sample the vector at the new coordinate; no implicit
  covector/direction transform is invented).
- Validity is resampled with the same filter as its vector, so invalid regions stay
  invalid after the remap.
- A later Deform therefore consumes directions in the post-Distort frame, which is
  the frame the accumulated working Flow lives in.

---

## 6. Noise and Flow preset identity

One generator type (`EMixtormatGeneratorType::Noise`), one implementation, two
creation presets. Preset identity is serialized as `EMixtormatNoisePreset
NoisePreset` on `FMixtormatNoise` (new enum, `Noise = 0`, so every existing asset
loads as Noise). The role persists independently of the output toggles: enabling
Height on a Flow does not change its name or preview behavior.

Expected defaults (applied by P1 creation logic):

| Parameter | Noise | Flow |
|---|---|---|
| Write Height | ON | OFF |
| Write Flow | OFF | ON |
| Height weight | 1.0 | 0.0 |
| Slope weight | 0.0 | 0.0 |
| Curl weight | 0.0 | 1.0 |
| Constant weight | 0.0 | 0.0 |
| Strength | 1.0 | 1.0 |
| Add | 1.0 | 1.0 |
| Mix | 0.0 | 0.0 |
| Selected preview | Material | Arrows (P5) |

Smallest compatible serialized representation: the preset enum plus the shared
parameter set above. No duplicate payload struct, no second generator type value,
no separate Flow implementation.

---

## 7. Reconciliation of earlier architecture

`AgentDocs/BEHAVIOR_V2.md` describes the currently implemented Behavior paths. The
following statements are superseded by `FINAL_BEHAVIOR_PLAN.md` + this contract;
BEHAVIOR_V2.md has been annotated accordingly and its migration sections now point
here:

1. **"A Behavior is the only way to rewrite a Generator's own output"** — under the
   final architecture Flow is a *Generator preset* (a field producer), not a
   Behavior. The `FlowField` Behavior type is legacy and is removed in P3.
2. **Typed field sockets with published references (`Direction`/`Height`/
   `Influence` source routing)** — replaced by automatic field inheritance
   (section 5). No Inspector target/source routing exists in the final architecture;
   socket removal and Inspector cleanup are P3/P4 work.
3. **Behavior flow float4 packing (direction, signed distance, influence)** —
   superseded by the canonical layout in section 1; migration in P2.
4. **FlowDirection published after post-generation Behaviors** — the required order
   (FINAL_BEHAVIOR_PLAN section 10) evaluates intrinsic Gradient/Flow *before*
   scoped children; the current late publication is the known defect P2 fixes.

Nothing in P0 removes working Behavior functionality. Migration and cleanup
assignments:

| Phase | Work |
|---|---|
| P1 | Noise/Flow presets, output toggles, MODE weights, canonical Noise Flow writer, creation-menu defaults. |
| P2 | Working-field initialization, early intrinsic Flow publication, ordered Flow accumulation with Add/Mix + masks, Distort/Deform consume accumulated Flow, canonical Behavior flow writer, Slope reads current height. |
| P3 | Remove Behavior-under-Behavior nesting and the legacy `FlowField` Behavior path; Flow Generator children as siblings of Behaviors; drag/clipboard/instance fixes; remove obsolete flow duplicates. |
| P4 | Inspector rebuild: output toggles, MODE/Add/Mix controls, remove all source/target pickers and their dead code. |
| P5 | Viewport arrows. |
| P6 | Dead code removal, doc updates. |

---

## 8. What P0 changed

- `Source/MixtormatRuntime/Public/MixtormatGeneratorTypes.h`
  - New `EMixtormatNoisePreset` enum (`Noise = 0`, `Flow = 1`).
  - `FMixtormatNoise`: appended `NoisePreset`, `bNoiseWriteHeight`,
    `bNoiseWriteFlow`, `NoiseDirectionHeightWeight`, `NoiseDirectionSlopeWeight`,
    `NoiseDirectionCurlWeight`, `NoiseDirectionConstantWeight`,
    `NoiseDirectionAngle`, `NoiseDirectionStrength`, `NoiseFlowAdd`,
    `NoiseFlowMix`. All defaults are the Noise preset identity state.
- `AgentDocs/FIELD_CONTRACT_P0.md` — this document.
- `AgentDocs/BEHAVIOR_V2.md` — reconciliation annotations (section 7 above).

No GPU dispatch, shader layout, Inspector, hierarchy or creation-menu change was
made in P0. Existing texture channels are untouched.

---

## 9. Validation status and what requires local UE 5.8 validation

Performed: static source review of the runtime types, gather, GPU passes and
shaders named in the plan; consistency check of this contract against
`FINAL_BEHAVIOR_PLAN.md` sections 1–6, 9, 10, 14.

Not performed (requires local Unreal Engine 5.8):

- UHT/C++ compilation of the appended reflected properties and new enum.
- Shader compilation (no shader was changed; listed for completeness).
- Save/load round-trip of the new properties (expected safe: appended UPROPERTYs
  with identity defaults; verify with an existing asset).
- Undo/redo, clipboard, instance and group parity with the new properties.

---

## 10. P1 implementation notes (clarifications only)

Recorded where P1's implementation fixed or sharpened this contract. No rule above
changed.

1. **Publication names.** The generated MODE field publishes under `GeneratedFlow`
   (kind `Flow`, canonical layout, with its own validity texture). The intrinsic
   `FlowDirection` keeps its legacy normalized-direction layout and every existing
   consumer until P2 migrates them. Two distinct outputs; the intrinsic vectors are
   never applied twice.
2. **Write Height OFF and intrinsic FlowDirection.** A height-less Noise module has
   no completed (normalized/scaled) height, so its intrinsic `FlowDirection`
   diagnostic derives from the native field instead. Same output name and consumers;
   the diagnostic reflects the only height the module has.
3. **Composition op granularity.** `FlowComposeCS` applies the Add/Mix formula
   component-wise over the canonical float4 (VectorXY, reserved, influence). The
   composed validity texture is 1 where either operand carries directional data.
4. **Slope input contract.** The generated-flow pass carries a `SlopeHeight` texture
   input plus a `UseSlopeHeight` flag. Until P2 binds the preceding working-height
   snapshot the flag is 0 and the basis is an exact zero — never the node's own
   height, which is the Height basis. The Slope Inspector control stays hidden until
   the input is real; Add/Mix controls stay hidden for the same reason.