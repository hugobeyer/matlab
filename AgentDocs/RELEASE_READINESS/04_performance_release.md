# Readiness 04 — Performance, Packaging, and Maintenance

**Branch:** `feature/behavior-system-v2`  
**Report IDs:** `REL-02`, `REL-03`, `PERF-01`, `PERF-02`, `PERF-03`, `MAINT-01`, `MAINT-02`  
**Status:** Not started. Read `AGENTS.md` and `AgentDocs/ARCHITECTURE.md`; use `AgentDocs/COMPOSITION.md` for caching/data-flow questions.

## REL-02 — Required binary content absent from tracked tree (P1, confirmed packaging risk)

**Reported evidence:** `.gitignore` excludes `Content/`; tracked repository lacks assets while `MixtormatPaths.cpp`, preview viewport and bake service hard-reference `/Mixtormat/...` resources.

- Inventory runtime **asset path references only** in code and release packaging configuration; do not read or modify ignored `Content/` binary assets.
- Determine how the actual distribution artifact receives required preview/gizmo/bake assets. A Git-ignored folder is **not proof** that a local installation is missing content.
- Document one reproducible packaging/injection step and exactly which required asset families it supplies.
- Reuse existing canonical plugin path definitions. No second path resolver or unverified replacement asset paths.

**Trace:** `.gitignore`, `MixtormatPaths.cpp`, `SMixtormatPreviewViewport.cpp`, `MixtormatBakeService.cpp`, `.uplugin` and existing release scripts/config.

## PERF-03 — Per-binding owner lookup cost (P1, confirmed hot-path structure)

**Reported evidence:** `MixtormatParameterBinding.cpp::LocateOwner` repeatedly scans layers and children during compose.

- Scope an ephemeral GUID → owner/index map per compose or per immutable resolution scope.
- Reuse any existing authored/effective layer index map before adding another structure.
- Keep group remapping, instances and missing-reference fail-closed behavior identical.
- Avoid cross-compose map lifetime unless cache invalidation ownership is explicit.

## PERF-02 — Synchronous asset loads on interaction/compose paths (P1, confirmed call sites)

**Reported evidence:** `LoadSynchronous` appears in `MixtormatEffectGather.cpp`, `MixtormatGpuCompositor.cpp`, `MixtormatMaterial.cpp` and hierarchy thumbnail rendering.

- Classify each call by phase (editor initialization, hot composition, drag/thumbnail, bake).
- Prefer stable resolved object references or existing asset cache paths when safe, without stale UObject pointers.
- Preserve soft reference serialization and editor invalidation semantics; don't globally replace every synchronous load.

## PERF-01 — Full-resolution clear passes (P2, confirmed presence; cost unmeasured)

**Reported evidence:** ~11 validation-only clears are present in `MixtormatGpuComposePipeline.cpp`.

- Identify the lifetime and readers of each cleared texture.
- Consolidate/eliminate only clears provably redundant for initialized output state; preserve fail-closed behavior, resource transitions and deterministic previews.
- No speculative rewrite of the RDG graph or claims about speedup without runtime measurements.

## REL-03 — Duplicate preview material path definitions (P2, confirmed)

**Reported evidence:** Paths differ between `SMixtormat.cpp` and `MixtormatPaths.cpp`.

- Identify the canonical path source and all callers; unify through existing path declarations.
- Preserve required material assets and any intentional fallback semantics; no new local string literals.

## MAINT-01 — Unused competing layer preview implementation (P2, confirmed static)

**Reported evidence:** `Services/MixtormatLayerPreview.*` appears to have no callers outside itself.

- Inspect call sites and current preview pipeline. If genuinely unused, **propose** deletion for approval before removing a feature/API.
- Avoid adding a second preview or replacing the working current path.

## MAINT-02 — Core documentation drift (P2, confirmed)

**Reported evidence:** `FILE_MAP.md` line anchors drifted; `ARCHITECTURE.md` describes Grade/Runoff order incorrectly.

- Update `AgentDocs/FILE_MAP.md` and `AgentDocs/ARCHITECTURE.md` to current symbol/file locations and actual composition order after source inspection.
- Coordinate `AgentDocs/GENERATORS.md` refresh with [Readiness 02](02_generators_masks_ids.md); avoid two agents editing the same document concurrently.
- Do not rewrite archived audits to imply they describe current implementation.

## Release boundary

The existing `.uplugin` already reports 1.0.0/non-beta per the audit. Do not change versioning, remove features, run builds, package binaries or launch Unreal without the user's specific request. User owns build/runtime testing. Record any packaging dependency not provable from the tracked source as unresolved rather than as a confirmed missing executable asset.
