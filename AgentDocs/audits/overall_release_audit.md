# Mixtormat 1.0 Release-Readiness Audit

**Coverage note:** 4 of 5 areas audited (Composition, Generators/Masks/IDs, UI/UX, Perf/Release). **Behavior System V2 was not audited** — its agent failed twice per your instruction; treat all Behavior-related conclusions below as partial (they come incidentally from other areas).

**Scoped update — 2026-10-12:** Code-owned filtered Flow/UV diagnostic color is implemented in source (see §G). No build, shader compilation, runtime AA check or GPU profiling was run. Readiness estimates and unrelated findings below have not been re-audited.

---

## A. Overall readiness — ~65% (±10%)

| Dimension | Estimate | Why |
|---|---|---|
| Architecture | 80% | Clean Runtime←Shaders←Editor, solid caching; residual duplication/dead code |
| Feature completeness | 70% | Generators 6/6, effects 9/9 wired; dead controls, unreachable child types, Behavior V2 unverified |
| UI/UX | 65% | Feature-rich, but ~30 native-tooltip rule violations, persistence gaps |
| Maintainability | 70% | Good structure; docs drift, dead preview impl, stale routing line numbers |
| Release preparation | 45% | **No `Content/` in repo** despite hard asset deps; uplugin already claims non-beta 1.0.0 |

---

## B. Critical blockers — P0

**None confirmed by static analysis.** The closest candidates are the P1s below plus the unaudited Behavior V2 system (unknown risk).

---

## C. Important problems — P1

| ID | Subsystem | Evidence | Problem / Impact | Recommendation | Confidence |
|---|---|---|---|---|---|
| REL-01 | Runtime identity | Repo-wide grep: zero matches for `PostDuplicate`/`RegenerateIdentity` in `Source\` | Asset duplication preserves all Layer/Child/Group GUIDs; roadmap §1.1 P0 never implemented; cross-asset GUID lookups can bind wrong | Add `PostDuplicate` identity remap helper | Confirmed absence; corruption scenario needs runtime |
| REL-02 | Packaging | `.gitignore` L10 ignores `Content/`; no `Content\` folder; `MixtormatPaths.cpp` L177–215, `SMixtormatPreviewViewport.cpp`, `MixtormatBakeService.cpp` hard-load `/Mixtormat/...` | Packaging from this tree ships preview/gizmo/bake that silently fail | Ensure release pipeline injects `Content/`; document required assets | Confirmed in repo; external pipeline unknown |
| UI-01 | Tooltips/style | ~30 sites: `.ToolTipText`/`SetToolTipText`/raw `SToolTip` (e.g. `MixtormatLayerHierarchy.cpp` L1068+, `SMixtormat_Library.cpp` L538+, `SMixtormatStatusDot.cpp` L14) | Violates AGENTS.md §5 mandate (`SMixtormatHelp`/`MakeStyledToolTip`); unstyled tooltips on core surfaces | Route sites through styled-help wrap | Confirmed |
| UI-02 | Shared controls | `SMixtormatIconButton.h` L42 has `_ToolTip`, but callers pass `.ToolTipText` (`SMixtormat_Preview.cpp` L301, `SMixtormatInspectorGroup.cpp` L222); `SMixtormatLayerIcon` has no help arg at all | Styled help receives empty text → never shows; intended tooltips dead | Switch callers to `.ToolTip`; add `_ToolTip` to LayerIcon | Confirmed |
| UI-03 | Inspector | `CombineId` in `MixtormatLayerTypes.h` L133–135; zero matches in MixtormatEditor (no creation menu, no inspector claim) | Runtime-complete child type unreachable; loaded asset would fall through to layer inspector | Ship-or-hide decision + defensive inspector claim | Confirmed |
| GEN-01 | Generators | `MixtormatGpuGeneratorPasses.cpp` L2298–2303 hardcodes `bHasScopedMask=false; bHasRegionIds=false` ("only satisfy the bindings"); sliders exist at `MixtormatInspectorGenerators.cpp` L100–108 | Strata Carver "Mask & ID Influence" sliders are silent no-ops promising behavior | Hide sliders or wire from `HasScopedGeneratorMasks`/`RegionIdMaps` | Confirmed |
| GEN-02 | Masks | `MixtormatChildScope.cpp` L329–347 accepts scoped masks on all generators; GPU consumers exist only for Noise (+HeightColorRamp) | Gates on Strata/Rock/Pebbles/Cracks/Cliff gather but are never read | Wire module gate or restrict `CanOwnScopedMasks` | Confirmed |
| COMP-01 | Layer ordering | `SMixtormat_Layers.cpp` `RebuildLayerList` L147–159 (index 0 first) vs `MixtormatGpuCompositor.cpp` L71–75 ("layer 0 = bottom"); drop target "above" inserts lower index | Panel top = render bottom; "place above" renders **under** target — inverted core interaction | Reverse display iteration only; never touch array order | Confirmed static; convention is product call |
| PERF-02 | Perf | `LoadSynchronous` in `MixtormatEffectGather.cpp` L53/L164, `MixtormatGpuCompositor.cpp` L1750/L1800, `MixtormatMaterial.cpp` L88–92, thumbnails (`MixtormatLayerHierarchy.cpp` L897–916) | Sync asset loads on compose/drag paths → hitches (roadmap §6.1 still open) | Cache resolved UObject* beside soft ptrs | Confirmed |
| PERF-03 | Perf | `MixtormatParameterBinding.cpp` `LocateOwner` L259–300 linear scan per binding; called every compose | O(bindings×layers) + O(children²) recomposed each frame → quadratic drag cost (§6.3 open) | Build `TMap<FGuid,int32>` index per compose | Confirmed |

---

## D. Improvements — P2 (selected)

- **BV2-adjacent / UI-04:** `EMixtormatBehaviorType` has 6 values; only 5 inspector panels mounted; legacy "Obsolete Flow" children fall through (`SMixtormat_Inspector.cpp` L505–510, `MixtormatLayerActions.cpp` L1484–1494). *Likely.*
- **COMP-02:** `BuildEffectiveLayers` remaps layer-level but not `Child.ParameterBindings` (`MixtormatLayerGroups.cpp` L262–322) → child drivers silently die when a layer is grouped. *Likely.*
- **COMP-03:** Effect relief/AO ignores layer mask under non-HeightBlend ops (`MixtormatComposite.usf` L1261–1269) — behavior flips with height-op switch. *Needs runtime verification.*
- **COMP-04:** Disabled-layer publication split: mask children publish, ID/generator outputs don't (`MixtormatGpuComposePipeline.cpp` L1003–1147 vs `MixtormatGpuPatternPasses.cpp` L955–959). *Likely.*
- **COMP-06:** Dead no-op `if (DebugMask == CombinedMask)` reset (`MixtormatGpuComposePipeline.cpp` L940–948) → LayerMask debug shows pre-chain mask. *Confirmed dead code.*
- **GEN-03:** 4 flow-tool enum values removed without CoreRedirects (`MixtormatEffect.h` L29–55 vs `DefaultMixtormat.ini`) — violates repo's own serialization rule. *Likely.*
- **GEN-04:** `GENERATORS.md` documents deleted symbols/files (HeightPush/StructuralWarp/Flow .usf, inspector controls) — AGENTS.md routes to it. *Confirmed.*
- **GEN-06:** Noise `FlowDirection` published at pre- and post-normalization revisions (`MixtormatGpuNoisePasses.cpp` L693–710, in-code acknowledged P2). *Needs runtime.*
- **REL-03:** Two competing preview-material paths: `SMixtormat.cpp` L85–87 vs `MixtormatPaths.cpp` L187–190. *Confirmed duplication.*
- **PERF-01:** ~11 validation-only full-res clears every compose (`MixtormatGpuComposePipeline.cpp` L419–614). *Confirmed presence.*
- **MAINT-01:** Dead competing preview impl `Services\MixtormatLayerPreview.*` (no external callers). *Confirmed.*
- **MAINT-02 / COMP-05:** `FILE_MAP.md` line anchors off by 270–516 lines; `ARCHITECTURE.md` compose-order wrong (Grade is in-composite, Runoff pre-composite). *Confirmed.*
- **UI-05/06/07/08:** hardcoded style literals; inspector placement/tab state not persisted (gallery is); Sources shelf has no reorder; `I` hotkey undiscoverable. *Confirmed.*

---

## E. Confirmed completed — do not redesign

- **Height math:** all `EMixtormatHeightOp` denominators guarded, shared kernel, ping-pong parity `LayerIndex & 1`, no off-by-one found.
- **Compose caching:** prefix/node/network caches with hash invalidation; RT reuse on unchanged res; final AO/normal gated.
- **Generators 6/6 end-to-end:** enum → gather → dispatch → .usf → inspector → capabilities → menus; Noise published outputs consistently wired.
- **ID remap/preservation:** `RegenerateLayerIdentities`, group expansion remaps, dangling refs fail closed.
- **Undo + sanitization:** `RecordEditHistory` reachable from child/group edits; contract-based `SanitizeFloat` present.
- **Context menus / DnD:** convention-consistent destructive-last menus; full drop-target suite; no no-op entries.
- **Styled-help infra itself** (`SMixtormatHelp`) — solid; extend, don't replace.
- **Shader registration, CoreRedirects for enum renames, uplugin module phases** — correct.

---

## F. Recommended implementation order

| # | Fix | Sev | Size |
|---|---|---|---|
| 1 | REL-02: guarantee `Content/` in release artifact + verify required assets | P1 | Small |
| 2 | REL-01: `PostDuplicate` GUID remap | P1 | Medium |
| 3 | GEN-01/02: hide or wire dead Strata sliders + restricted scoped-mask owners | P1 | Small |
| 4 | UI-01/02: route ~30 tooltip sites + 2 atom fixes through styled help | P1 | Medium |
| 5 | COMP-01: flip layer-list display order (display only) | P1 | Small |
| 6 | UI-03: ship-or-hide `CombineId`; UI-04: FlowField inspector claim | P1/P2 | Small |
| 7 | PERF-03: GUID→index map per compose | P1 | Medium |
| 8 | PERF-02: cache soft-ptr resolutions | P1 | Medium |
| 9 | COMP-02/04, GEN-03/06: binding remap, publication policy, enum redirects, flow publish order | P2 | Medium |
| 10 | Doc refresh (FILE_MAP, ARCHITECTURE, GENERATORS) + delete dead `MixtormatLayerPreview` | P2 | Small |
| 11 | **Full Behavior V2 audit + runtime pass** (see below) | ? | Large |

---

## G. Incremental update — filtered Flow/UV diagnostics (2026-10-12)

**Status: source implementation complete; compile/runtime acceptance pending.**

- New **`DA_DiagnosticColor`** binding supplies linear **premultiplied RGB + coverage A** in **mode 0 (mask/scalar) and mode 1 (Flow/UV)**. The existing `DA_DiagnosticFlow` raw binding remains unchanged; authored PBR outputs, mask normalization and mask ink color are preserved.
- `MixtormatDebugPreviewBlit.usf::MainDiagnosticColorCS` evaluates the original published field at producer resolution into a composition-resolution display texture. Flow/Vector2 uses direction hue and strength value; UVMap uses a warped checker/grid with contraction, expansion and fold tint.
- `MixtormatDiagnosticColor.ush` owns display styling, lifted-coordinate sampling, source-texel Jacobians, integrated periodic checker/grid coverage and 2×2 subpixel color/tint evaluation. Displacement is recovered before interpolation; stored values and multi-tile travel are not wrapped or rounded.
- `MixtormatGpuDebugPreviewPasses.cpp::AddDiagnosticColorPass` explicitly generates color mips after the compute write. `MixtormatGpuCompositor.cpp` allocates a mip-enabled linear float color target with trilinear wrapping; raw coordinate data does not receive color filtering.
- Mode 0 color allocation, generation and completed-owner binding no longer depend on raw Flow publication. The color pass consumes final normalized/rotated mask coverage, encodes the existing linear ink with alpha, then generates color mips. Missing mask coverage produces a transparent texture, not stale Flow color.
- `MixtormatViewportMaskPostFx.cpp` binds the completed owner-matched texture and composites premultiplied color in both built-in material modes. Mode 0 uses alpha once, without multiplying mask `Field.R` again; mode 1 retains `Field.R` as the availability gate. Existing artist-authored materials are not edited automatically: add `DA_DiagnosticColor`, replace the Custom-node visualization with its sampled color, and use premultiplied source-over blending. See `AgentDocs/UNIFIED_POSTFX.md` for the exact graph contract.
- This diagnostic path already has a code-built PostFX material when no authored override loads. That narrows the asset dependency for this path only; **REL-02 remains open** for other required preview/gizmo/bake assets.

**Selection/toggle correction — same scoped update:**

- W/E now use the existing workspace hotkey processor, not viewport-only focus. Text entry, modified/repeated keys and other editor windows retain their existing routing.
- An active diagnostic can be dismissed by its key without reselecting the original source. Stack selection retargets the active kind to the newly selected eligible source; unsupported selections clear it instead of pinning old output.
- Push exposes its existing ordered Flow snapshot. Layer/header E selection resolves a real enabled root generator's completed scope, not an invented global Flow sum. Group/header and layer selection clear stale selection lanes.
- Shared preview refresh (`FlushPendingPreviewRefresh`) reconciles selection changes so duplication, undo, and programmatic selection updates immediately retarget active diagnostics.
- Added `MixtormatDiagnosticOutputs::BehaviorMask` (`MainBehaviorMaskCS` / `AddBehaviorMaskDiagnosticPass`) so Warp, Push, and Deform expose their real evaluated scoped-mask × Influence gate to W.
- Compressed Flow vector visualization with an explicit brightness floor (`0.55`) and rational scaling so subtle flow magnitudes remain visible and distinguishable from black.
- UV recapture/binding now runs in `FMixtormatPreviewViewportClient::Tick`, after camera maintenance, instead of `SMixtormatPreviewViewport::Tick`. This ties mapping updates to the realtime viewport rather than Slate invalidation; it does not regenerate diagnostic color on camera movement.
- These fixes address input/selection and capture scheduling paths implicated in stuck diagnostics. Mesh-rotation attachment and parameter-driven GPU output behavior still require runtime inspection; they are not claimed verified.

**Remaining acceptance checks / limits:**

- Compile the new global shader, mip-generation dispatch and generated material graph in the target Unreal version.
- Inspect zoomed-out/oblique views, folded/multi-tile maps, mismatched producer sizes, quarter-turn rotation, zero/non-finite fields and same-owner recomposition.
- Texture-space filtering does **not** resolve the capture's binary mesh silhouette or guarantee alignment with temporal jitter/upscaling. Those remain separate viewport concerns.
- Profile the new active-diagnostic compute/mip work; no performance improvement or release-readiness increase is claimed.

---

## Final assessment

1. **Architecturally ready?** Mostly yes — module boundaries, caching, and data model are sound; identity/duplication and Behavior V2 are the open questions.
2. **Incomplete features:** Behavior V2 (unverified), Strata mask/ID influence, scoped masks on non-Noise generators, `CombineId` authoring, FlowField legacy handling, source-shelf reordering.
3. **Top 5 risks:** ① Behavior V2 unknown, ② missing `Content/`, ③ GUID duplication identity, ④ dead controls shipping as promised features, ⑤ quadratic compose cost on large stacks.
4. **Leave untouched:** height/composite math, caching layer, generator pipeline, ID remap, undo/sanitization, menus/DnD, styled-help infra.
5. **Shortest path:** items 1–6 above (all Small–Medium), then a Behavior V2 audit + one runtime QA pass; docs/dead-code at the end.
6. **Needs runtime testing:** layer-order convention intent, effect-vs-mask visibility, disabled-layer policy, FlowDirection staleness, GUID-duplication corruption, Content asset presence, actual GPU/CPU cost magnitudes, and **everything Behavior V2**.