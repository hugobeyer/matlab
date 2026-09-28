# Fracture math — pending visual validation

## Reference comparison

Read Houdini 22.0.430's live `sdfblend1/opencl1` kernel and installed
`houdini/ocl/include/sdf.h`, `sdfOpIntersectChamfer` (line 1027).
With negative-inside fields its operation is:

```
max(max(A, B), (A + B + radius) * sqrt(0.5))
```

The previous Mixtormat positive-radius formula matches this operation. Houdini's
operation is **not idempotent**: with A=B=0 and radius=0.25, it returns about
0.176777. Matching it exactly and leaving identical fields unchanged are
incompatible requirements. Neither the reference recipe nor this implementation
renormalizes after intersection.

`MixtormatSdfIntersectChamfer` retains the reference formula for positive radius;
zero radius intentionally selects ordinary `max(A,B)`.
Fracture now calls the explicitly named `MixtormatSdfIntersectChamferCorrelated`:

```
intersection = max(A, B)
reference = MixtormatSdfIntersectChamfer(A, B, radius)
result = intersection + min(reference - intersection, abs(A - B))
```

This is an intentional fracture-specific adaptation, not Houdini parity. By
inspection it is symmetric, returns A when A=B, never falls below the ordinary
intersection, and approaches the ordinary intersection continuously as B tends
to A. It limits the additional cut to field disagreement. It is not an exact SDF
and may change the appearance at equal-valued crossings of different fields.
The explicit contour offset in resolve remains separate from this operation.

## Ownership and face changes

- Parent IDs now come from the original pixel, never the displaced position.
- Existing parent boundaries and gaps constrain Generated mode too.
- Generated sub-piece IDs remain separate from parent IDs; no numeric ID blending.
- Topology's source-height probes reject any parent-ID/gap crossing in all four taps.
- Resolve checks all four taps against both parent and generated piece identity.
- Rejected samples retain the existing local-height/local-field policy.
- Gaps keep their source height and do not enter the face solve.
- JFA output discards non-owned records; field materialization also checks ownership.
- Source-height sampling was removed from boundary seeding along with its max shoulder.
- The face is now `Surface - Depth * (1 - FractureFace(U, Profile))`.
- This retains three broad slope sections and meets Surface exactly at U=1.
- It removes seed-selected shoulder discontinuities, not every possible artifact.
- Runtime binding contracts remain unchanged; no bound-parameter HLSL clamps were added.
- Fracture remains after Ramp relief and uses the shared height-derived normal path.

The topology probes precede generated-piece creation and are gated by upstream
region ownership. Resolve's resampling additionally gates generated-piece ownership.
JFA remains an approximation, not connectivity segmentation or an eikonal solve.
Repeated IDs in disconnected regions still share an owner key.

## Curl/fractal correction after screenshot review

The supplied Unreal normal/height/material images show repeated diagonal teeth
along broad faces; the supplied Houdini image shows the desired varied slopes.
The fracture shaders used fixed axis/diagonal one-dimensional bands, including
frequency multipliers of 2 and 3 in width, contour offset and profile. Increasing
Scale therefore increased coherent stripe frequency instead of producing 2D variation.

Those bands are now replaced in topology and resolve:

- Reuse the existing periodic analytic-gradient curl field from `MixtormatGully.ush`.
- Use three gradient-noise octaves, integer lacunarity 2 and roughness 0.18.
- Curl-warp the scalar field coordinates; seed resolve fields by piece ownership.
- Modulate topology curl displacement with low-roughness fractal noise.
- Reduce derived displacement relative to wavelength as Scale increases.
- Retain broad three-section faces, ID/gap gates, SDF blending and binding contracts.

This follows the curl/fractal direction of the reference; it does not reproduce
Houdini's alligator fractal, custom per-piece UVs or max-streak sampling exactly.
The displacement adjustment limits amplitude, not the full warp Jacobian; it is
not a proof that all folds or sample-rejection artifacts are eliminated.
Static search confirms no `FractureBand` calls remain. Shader compilation and
new low/high-Scale visual comparisons have not been performed.

## Published scalar outputs

Existing names and behavior remain available:

- `Fracture`: existing normalized absolute height change.
- `FaceProgress`: 0 at/outside the cut, 1 at the interior shoulder; 0 in gaps.
- `FractureHeight`: resolved height in layer-height units, without shader normalization.

These use the existing scalar preview/copy infrastructure, not numeric ID maps.
Face Progress is a broad field, not an inverted thin boundary/crack mask.
When the pass is bypassed by zero amount/depth or missing required IDs, Fracture
and Face Progress are zero and Fracture Height is the source height.
Height values outside 0..1 may not be represented faithfully by scalar-mask preview.

## Validation status

- Reference implementation was read, not inferred from node labels.
- Focused static review found no concrete binding/ownership defects.
- Editor diagnostics remain blocked by missing `CoreMinimal.h`.
- No builds, tests, terminal/git commands, or scene changes were run.
- User-provided Unreal images were reviewed and show diagonal artifacts in the previous patch.
- The curl/fractal correction has not been visually verified. Fracture is **not declared stable**.

### Required Unreal visual checks (not performed)

Use the same material, resolution, lighting and camera for comparisons:

1. Pattern IDs -> Ramp From IDs -> Fracture: inspect height, normal and Face Progress.
2. Use neighboring regions with strongly different ramps and heights, plus narrow gaps.
3. Confirm parent-ID edges and gap heights stay fixed in all three source modes.
4. Inspect tile seams and narrow pieces at high variation/width for sample-rejection steps.
5. Compare chamfer 0 and positive values; inspect equal-field crossings for pinching.
6. Check broad slope continuity and nearest-seed boundaries for residual ridges.
7. Confirm Amount=0 and Depth=0 leave the material unchanged.
8. Check output previews and Copy Output in the actual compositor ordering.

A Houdini screenshot alone cannot validate the modified Unreal shaders.

## Deferred until fracture is visually stable

Prepare a separate Chipping design using owner-local fields, boundary distance,
SDF sampling, runtime binding contracts and shared height-derived normal/AO handling.
No chipping or layer formation is implemented here. Global normal strength, optional
on-demand AO, the unrelated generator-AO issue, and the reported multi-layer/effect
performance regression remain untouched; no broad audit was performed.
