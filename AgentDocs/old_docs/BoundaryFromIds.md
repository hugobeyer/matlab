# Boundary From IDs

Boundary From IDs consumes the existing uint Region IDs map. It produces scalar fields only: no new IDs, height edits, automatic coverage, or material-channel behavior.

## Source resolution

`FMixtormatBoundaryIdFilter::RegionIdsSource` uses the existing `FMixtormatOutputReference`, with `Kind = RegionIds` and `OutputName = RegionIds`.

- An entirely unassigned address uses the existing layer-local, scope-aware `FindRegionIdsAboveWithIndex` semantics. The nearest authored producer shadows older maps if its map is unavailable; it does not silently read an older producer or another layer.
- An explicit address supports an earlier child in the same effective layer and scope, or a source in an earlier enabled layer through `MixtormatOutputReferences::ResolveEarlierSource`.
- Shared layer-group addresses are remapped by the existing effective-layer expansion.
- Same-layer maps are read from the current layer's ID map list; earlier-layer maps are read from the typed published-field registry.
- Partial, unavailable, disabled, wrong-kind, wrong-format, later, or wrong-resolution sources produce no fields. An explicit source never falls back to an unrelated map.
- Source resolution is isolated in `ResolveBoundarySource` (gather) and `ResolveBoundaryRegionIds` (render), to localize merges with future source API changes.

No preview RGB is read. Valid identity includes zero and sparse/full-width IDs; only `MIXTORMAT_INVALID_REGION` is invalid.

## Boundary detection and distance

The shared `MixtormatRegionFields.usf::SeedCS` has an explicit seed policy. Boundary From IDs uses `ValidOnlyEight`: N, S, E, W, NE, NW, SE, SW, wrapped at both texture edges. A valid pixel seeds only when a valid neighbor has a different raw uint ID. Invalid pixels neither seed nor create borders against valid pixels.

`AddRegionDistanceRecordPasses` reuses the existing toroidal Euclidean `JumpCS` jump-flood implementation: descending power-of-two strides, followed by an extra stride-one pass (JFA+1). It propagates nearest seed coordinates rather than dilating a binary outline. Coordinates are packed exactly in R32_UINT; the nearest-seed solve is approximate, not an exact Euclidean distance transform. Resolution must remain within the existing packed-coordinate texture limits.

Distance in output-pixel units is `d = toroidal_distance(pixel, nearest_seed) + 0.5`. The half-pixel offset places a straight raster ID interface halfway between adjacent pixel centers. Eight-neighbor raster seeds are not a subpixel reconstruction of arbitrary diagonal contours.

Relief From IDs keeps its existing `LegacyFour` seed policy, including its existing invalid-border semantics. Its region roots and reach/extent reduction are unchanged. Boundary From IDs requires neither root indexing nor an extent texture.

## Outputs and controls

All three outputs are R32_FLOAT scalar textures, published through the ordinary child mask-output registry and existing preview-eye/Copy Output infrastructure.

Define `band(d, r, s)` as:

- zero if radius `r <= 0`;
- `d <= r` when softness `s = 0`;
- `1 - smoothstep(r * (1 - s), r, d)` otherwise.

| Output | Definition |
| --- | --- |
| Boundary | `band(d, WidthPixels / 2, Softness)`; default full width 4 pixels. |
| Gap | `band(d, max(0, GapWidthPixels / 2 + GapBiasPixels), GapSoftness)`; default full width 8 pixels. |
| Distance | `saturate(d / DistanceRangePixels)`; optionally inverted. Default range 64 pixels. |

Softness feathers inward from the band's outer edge. Positive Gap Bias expands the radius, negative bias erodes it; it does not choose a region owner or shift a signed contour. Width zero produces an empty mask. Width/range values are absolute pixels at the requested output resolution, with no 2K/4K scaling multiplier.

Invalid pixels output zero for all fields, even when Distance is inverted. With no valid ID transitions anywhere, valid pixels output Boundary = 0, Gap = 0, Distance = 1 (or 0 when inverted).

There is no Signed Distance output: an interface between arbitrary labels has no stable natural inside/outside ownership. Roundness and diagonal-weight controls are omitted because the reused Euclidean pass has no corresponding meaningful settings.

## Cache and reuse

A compose-graph-wide nearest-seed record cache is keyed by source RDG texture identity, resolution, and seed policy. All Boundary From IDs children using the same ID texture/policy reuse the distance solve. Width, softness, gap bias, range, and inversion affect only a single resolve dispatch, which writes Boundary, Gap, and Distance together. The distance transform is never rerun once per output.

The existing compose prefix cache retains published outputs through its normal mask-output snapshots. The nearest-seed cache itself is graph-local: no RDG pointers persist across compose requests. A new compose that changes a consumer may rerun the transform; this implementation does not introduce a separate persistent distance-cache/storage system.

## Existing implementation audit / ID Group

- `MixtormatRegionFields.usf` already supplied Relief From IDs' wrapped JFA+1 records and per-region extent. Its record generation is now the shared helper, with policy-specific seeds.
- Region centre/bounds caches remain separate; Boundary does not need them.
- Pattern uses producer-specific analytic edge distances. Pattern topology and gap/relief behavior are unchanged.
- Rock uses a bounded analytic chunk-outline query with a natural solid/void sign, not a generic uint-ID transition distance. Rock behavior is unchanged.
- `MixtormatIdGroupBoundary.usf` scans eight rays out to `OutlineWidth` and treats any unequal ID (including invalid) as a binary border. It is not the same computation as the Euclidean, valid-only, soft fields added here. It was not changed.

ID Group can later call the shared record helper with the agreed seed policy and remap its Boundary from that record. Relief can share the same helper today while retaining its legacy policy; adopting valid-only eight-neighbor behavior there would be a separate intentional behavior change. No ID Group input composition or typed OutputReference redesign is included.

## Files

Added:

- `Docs/BoundaryFromIds.md`

Changed runtime:

- `Source/MixtormatRuntime/Public/MixtormatMaterial.h`
- `Source/MixtormatRuntime/Private/MixtormatParameterBinding.cpp`
- `Source/MixtormatRuntime/Private/MixtormatParameterDefinition.cpp`
- `Source/MixtormatRuntime/Private/MixtormatLayerGroups.cpp`

Changed editor:

- `Source/MixtormatEditor/Private/Widgets/SMixtormat.h`
- `Source/MixtormatEditor/Private/Widgets/SMixtormat.cpp`
- `Source/MixtormatEditor/Private/Widgets/SMixtormat_Layers.cpp`
- `Source/MixtormatEditor/Private/Widgets/SMixtormat_Inspector.cpp`
- `Source/MixtormatEditor/Private/Widgets/SMixtormat_Parameters.cpp`
- `Source/MixtormatEditor/Private/Widgets/MixtormatChildCapabilities.cpp`
- `Source/MixtormatEditor/Private/UI/Layers/MixtormatLayerBadges.cpp`

Changed shaders/render integration:

- `Source/MixtormatShaders/Private/Compositing/MixtormatGatherCommon.h`
- `Source/MixtormatShaders/Private/Compositing/MixtormatIdGather.cpp`
- `Source/MixtormatShaders/Private/MixtormatGpuCompositorInternal.h`
- `Source/MixtormatShaders/Private/MixtormatGpuComposePipeline.cpp`
- `Source/MixtormatShaders/Private/MixtormatGpuPatternPasses.cpp`
- `Shaders/Private/MixtormatRegionFields.usf`

No tests or builds were run. New test additions were removed at the user's request. Git was not invoked, so the checked-out branch was not independently verified.
