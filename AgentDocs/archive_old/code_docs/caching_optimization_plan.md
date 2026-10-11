# Compositor Caching & Optimization Plan

Status (2026-09-27): phases 0, 1, 2, 3, 4 (Pattern/Surface/Cluster IDs) and 6 implemented, never compiled.
Phase 5 not done: no profile yet, and the copies are cheap next to the solves the caches now skip.
Generators (Fracture, Strata Carver) are not node-cached yet. Hugo verifies in the editor.

## Problem
- Every edit recomposes the entire stack at full res (2048 default), every frame of a drag.
- No cross-compose cache except the Craquelure network (`FMixtormatNetworkCache`).
- Reference layers build a fresh `FMixtormatGpuCompositor` per compose (`MixtormatGpuCompositor.cpp:1446`) -> 12 new render targets each time, recursive.
- Iterative solves (Fracture JFA RGBA32F, Strata distance, Cluster union-find, Relief JFA, Breakup, Peel) rerun from zero.
- No GPU stats, so cost per family is invisible.

## Phase 0 - Measure (small, first)
- Add `DECLARE_GPU_STAT` + `RDG_GPU_STAT_SCOPE` per family: LayerInput, RegionIds, Masks, Generators, each Effect, Composite, References.
- Add `TRACE_CPUPROFILER_EVENT_SCOPE` on `RequestComposeInternal` (game-thread gather, `LoadSynchronous`, history copy).
- Hugo: `stat gpu` / `ProfileGPU` while dragging one slider on the top layer and on the bottom layer. Record baseline.

## Phase 1 - Reference compositor reuse (quick win)
- Cache `FMixtormatGpuCompositor` per (source asset, resolution) on the owning compositor.
- Recompose a source only when its content hash changes (see Phase 2 hash); otherwise reuse `PendingOutputs`.
- Keep the cycle guard (`ActiveSources`). Drop cache entries on resolution change / asset delete.

## Phase 2 - Content hashing (foundation for all caching)
- `uint64 HashLayer(const FMixtormatLayer&, groups)`: struct bytes via reflection (`UScriptStruct` + `CityHash64`) over the effective layer, plus referenced asset identity + package save-count for surfaces/textures/compositions.
- Hash the *effective* (group-expanded) stack, not the authored one.
- Exclude editor-only fields that do not change pixels (names, UI expanded flags, selection).
- Prefix hash: `Prefix[k] = Combine(Prefix[k-1], HashLayer(k))` plus global inputs (resolution, `bRotateUV90`).

## Phase 3 - Layer prefix snapshot (the main win)
- After layer k finishes, optionally persist the carried state into pooled RTs (`IPooledRenderTarget` via `ConvertToExternalTexture`):
  - accumulation: BC, N, RAM, Height (write half of the ping-pong)
  - `RidgeTargets` write half (erosion carries it across layers)
  - `DriverSnapshots`, `HeightSnapshots`, `PublishedMaskOutputs` produced at or below k
- Snapshot only one layer: the one just below the selected layer (the edit point). Keyed on `Prefix[k]`.
- Next compose: if `Prefix[k]` matches, register the snapshot as the ping-pong read half and start the loop at k+1.
- Must verify first: no cross-layer read points upward (Drivers naming a layer above, height references, published outputs). If any can, include them in the hash of the reading layer or disable the skip for that stack.
- Debug preview / region pick targeting a layer <= k: skip the cache for that compose (or snapshot debug too).
- Memory: one snapshot ~ 5 targets; ~90 MB at 2048, ~360 MB at 4096. Budget-bounded like `FMixtormatNetworkCache`; never cache during bake.

## Phase 4 - Node result cache (generators & ID producers)
- Cache outputs of self-contained, expensive children keyed on (child hash, input hashes, resolution):
  - Region ID producers (Pattern, Surface IDs, Cluster, ID Group) -> R32 ID map + Pattern side outputs
  - Generators (Fracture, Strata Carver) -> height/normal outputs
  - Region distance / centre caches (already per-compose; promote to cross-compose)
- Inputs that read the composite below (Composite Below modes) use `Prefix[k-1]` as the input hash.
- One shared byte-bounded LRU (generalize `FMixtormatNetworkCache`), evict on resolution change.

## Phase 5 - Pass cost reductions
- Fracture JFA: RGBA32F -> pack to `PF_G32R32F` / `PF_R32G32_UINT` where channels allow; skip Footprints/Redistance when `Source == Generated` if unused (verify).
- Replace full-res `AddCopyTexturePass` in Erosion / Relief / Worn Edges with ping-pong swaps where the source is not read after the write (~20 copies today).
- Audit formats: RGBA32F only where precision is required; half elsewhere.
- Game thread: cache `LoadSynchronous` results per compose; avoid rebuilding render data for skipped layers.

## Phase 6 - Throttle
- Never queue a compose while one is in flight on the render thread; keep only the latest request (drop intermediates during a drag).

## Order & payoff
1. Phase 0 (measure)  2. Phase 1 (refs)  3. Phase 2+3 (prefix snapshot)  4. Phase 6 (throttle)  5. Phase 4 (node cache)  6. Phase 5 (pass trims)

## Verification checklist (Hugo)
- Output bit-identical with cache on vs off (add a console var `Mixtormat.DisableComposeCache 1`).
- Edit top layer: `stat gpu` shows lower layers skipped.
- Edit bottom layer: full recompose, same as baseline.
- Drivers, height references, Copy Output masks across layers still resolve.
- Reference layers update when the source asset changes and is saved.
- Debug preview / ID picker on a cached layer still correct.
- Resolution switch 1K/2K/4K: no stale images, memory released.
- Bake unaffected.
