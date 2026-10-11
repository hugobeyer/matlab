# Readiness 02 — Generators, Scoped Masks, and IDs

**Branch:** `feature/behavior-system-v2`  
**Report IDs:** `GEN-01`, `GEN-02`, `GEN-03`, `GEN-04`, `GEN-06`, `UI-03`  
**Status:** Not started. Read `AGENTS.md` and `AgentDocs/GENERATORS.md` first; use `AgentDocs/COMPOSITION.md` for field/ID semantics.

## GEN-01 — Strata Carver controls that may be no-ops (P1, confirmed source)

**Reported evidence:** `MixtormatGpuGeneratorPasses.cpp` ~L2298–2303 binds `bHasScopedMask=false` and `bHasRegionIds=false` while Strata Carver “Mask & ID Influence” controls exist in `MixtormatInspectorGenerators.cpp`.

- Trace actual inspector parameters → gather → render data → dispatch → shader inputs.
- Reuse existing `HasScopedGeneratorMasks` / region-map information if available and valid in the current evaluation order.
- Make controls correspond to real consumed data. If a feature is currently unsupported, **propose** hiding the misleading control rather than silently deleting it.
- No replacement mask architecture; use canonical scoped masks and ID maps.

## GEN-02 — Accepted masks not consumed by generators (P1, confirmed source discrepancy)

**Reported evidence:** `MixtormatChildScope::CanOwnScopedMasks` permits all generators, but the audit found GPU mask consumers only for Noise and HeightColorRamp.

- Enumerate current generator types and trace scoped mask gathering to their per-generator output application.
- Prefer making existing accepted masks functional for Strata Carver, Rock Formation, Pebbles, Cracks and Cliff Strata; keep signed height and independent outputs consistent with module contracts.
- Reuse `AddScopedFeatureMask` and canonical mask composition where appropriate; avoid separate mask math per generator.
- If an owner cannot consume a mask, present the capability mismatch for a product decision before removing a placement or feature.
- Mask controls must operate on the **owned** generator contribution, not globally mask unrelated earlier fields.

**Trace:** `MixtormatChildScope.cpp`, `MixtormatMaskGather.cpp`, `MixtormatGeneratorGather.cpp`, `MixtormatGpuGeneratorPasses.cpp`, corresponding shaders, `MixtormatChildCapabilities.cpp`.

## UI-03 — Combine IDs cannot be authored (P1, confirmed absence in UI audit)

**Reported evidence:** `EMixtormatLayerChildType::CombineId` exists in Runtime; editor creation and Inspector routes were not found.

- Reuse existing ID child menu, child capabilities, row rendering, selection and Inspector patterns.
- Expose Combine IDs in the appropriate creation context and Inspector; preserve the ordered ID map contract. Avoid adding a separate output or source selector where stack inputs suffice.
- Do not conflate Region IDs with 0–1 mask coverage. Inspect the documented scoped Region-ID reference model before adding any automatic connections.
- Existing saved children must be selectable/editable, not treated as the generic layer Inspector.

**Trace:** `MixtormatIdTypes.h`, `MixtormatLayerTypes.h`, `MixtormatLayerChildren.cpp`, `MixtormatLayerMenus.cpp`, `SMixtormat_Inspector.cpp`, `MixtormatInspectorIds.cpp` (locate actual file), `MixtormatGpuPatternPasses.cpp`.

## GEN-03 — Removed Flow enum values and redirects (P2, likely)

**Reported evidence:** Four previously used flow-tool enum values may lack `CoreRedirects` in `Config/DefaultMixtormat.ini`.

- Compare actual serialized historical values with current reflected enums, not outdated documentation alone.
- Add necessary safe redirects only if source confirms a valid source/target type mapping.
- **Append, never reorder** serialized enums; do not restore duplicate legacy GPU systems just to satisfy an old name.
- Behavior V2 architectural decisions remain deferred until its focused audit.

## GEN-06 — Flow publication revisions (P2, needs investigation)

**Reported evidence:** `MixtormatGpuNoisePasses.cpp` ~L693–710 may publish `FlowDirection` at pre- and post-normalization stages.

- Trace what is generated, composited, cached and finally published by each enabled Noise/Flow preset.
- Keep one documented field contract for downstream consumers and preview. Account for flow-only output and zero contributions.
- Do not assume two writes are wrong until their consumers and timestamps in the pass order are identified.

## GEN-04 — Generator documentation drift (P2, confirmed)

**Reported evidence:** `AgentDocs/GENERATORS.md` names removed Structural Warp/Height Push/Flow symbols and inspector controls.

- Update the doc **only after related source behavior is understood**; describe the current generator, masking and Behavior boundaries.
- Retain historical plans as history; do not resurrect deleted implementation to match old prose.

## Boundaries

Do not redesign Behavior V2, introduce source/target menus, remove current generator kinds or overwrite Height/Flow/ID semantics. Preserve the six existing generator pipelines and their published outputs. No tests, automated validation, builds or Unreal launches unless the user asks.
