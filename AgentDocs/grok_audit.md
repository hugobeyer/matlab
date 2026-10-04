**Audited commit SHA:** `c63fd3bc7e4b370447a1cf1770f3ef820b59a098` (default-branch HEAD at time of audit).

**Repository production-code shape:** Clean 3-module Unreal plugin (UE 5.8):
- `MixtormatRuntime` (data model / UObjects / parameters; ~27 files, dominated by the 3935-line `MixtormatMaterial.h`).
- `MixtormatShaders` (Editor-only; gather + RDG GPU compositor + passes; ~43 C++ files + 63 `.usf`/`.ush`).
- `MixtormatEditor` (~210 files; Slate UI, hierarchy, inspector, preview, bake, style system).
Module dependency direction is correct and one-way: Runtime ← Shaders ← Editor. No Runtime→Editor or Runtime→Shaders leakage.

**Overall architecture assessment:** Coherent production architecture with a clear primary pipeline (authored `UMixtormatMaterial` → layer/children hierarchy with `ScopeOwnerChildId` scoping + instances → gather (Layer/Mask/ID/Generator/Effect) → compose requests + content hashing → RDG GPU passes → published outputs / OutputReference → preview / bake). Hierarchy, masking, IDs, generators, and effects are intentionally distinct categories with documented semantics. Caching is reflection-based and deliberately thorough. The largest risk areas are (1) iterative authored-parameter GPU work at 2K/4K, (2) residual dual theme systems, and (3) the sheer size of a few ownership files making end-to-end reasoning harder than necessary. Static inspection does not prove release-blocking crashes or data corruption.

**Highest-risk correctness issue (static):** None proven as P0. Strongest residual concerns are stale-GUID / instance / scoped-child recovery paths and possible cache-key vs. visual-result mismatches under hierarchy moves or source deletion (need runtime validation).

**Highest-value performance issue:** Authored-iteration simulation-style passes (Erosion, Craquelure grow, Generator Flow jumps, Rock Formation facets, etc.) that allocate full-resolution `PF_R32_FLOAT` / `PF_R32_UINT` intermediates and run sequential ping-pong loops whose cost scales linearly with iteration count × resolution². Caching mitigates recompute but not the cost of a cold or invalidated bake/preview.

**Highest-value architectural cleanup:** Complete deletion of the legacy theme path (`FMixtormatLiveTheme`, `DesignTokens`, `Palette`) after migrating remaining readers to `FMixtormatThemeStore` → resolved style → recipes. Second: tighten generator-layer channel/irrelevant-control surface so generators stay height/ID/structure producers.

**Release-safe from static inspection?** Yes, with the usual caveats that GPU resource lifetime, RDG assumptions, and complex GUID remapping under copy/paste/delete must be validated at runtime. No catastrophic static path to crash, memory corruption, or destructive serialization was proven.

**Major uncertainty requiring runtime:** Exact cache invalidation under hierarchy reorder + instance + scoped mask + OutputReference chains; real bandwidth cost of the iterative passes at 4K; whether any preview path forces production-quality intermediates.

---

### P0 — CORRECTNESS / CRASH / DATA CORRUPTION
**No P0 issue was proven by static inspection.**

---

### P1 — MAJOR ISSUES

**P1-01**  
**Files:** `Source/MixtormatShaders/Private/Effects/MixtormatErosionPasses.cpp`, `MixtormatCraquelurePasses.cpp`, `MixtormatGpuGeneratorPasses.cpp`, related `.usf`  
**Code:** Erosion loop (`Iterations` clamped 1–64, ping-pong `EroH`/`EroVel`), Craquelure grow iterations, Generator Flow jump/smooth/apply stages, Rock Formation facet iterations.  
**Observed:** Sequential full-resolution RDG passes with authored iteration counts; intermediate textures created as `PF_R32_FLOAT` / `PF_R32_UINT`.  
**Why it matters:** Cost scales as O(iterations × res²). At 4K + high authored iterations this dominates bake and can make interactive preview unusable if the same path is taken. Caching helps only on cache hits.  
**Recommended fix:** Keep the algorithms; add a quality/iteration scale factor that is lower for preview than bake, and consider narrower formats or early-out where possible. Document safe iteration ranges.  
**Confidence:** High (code is explicit).  
**Timing:** Before Release (preview path).  
**Evidence:** Proven from static inspection of the pass loops and texture creation.

**P1-02**  
**Files:** `Source/MixtormatEditor/Private/Style/*` (LiveTheme, DesignTokens, Palette, ThemeStore, ResolvedStyle, Recipes, StyleLocator) + many UI atom/container files that still reference the old path.  
**Code:** Dual theme systems coexist; ~557 references still touch legacy symbols.  
**Observed:** New architecture (`ThemeStore` → validation → resolved → recipes → painters) is present and intended as canonical, but production widgets continue to read legacy state.  
**Why it matters:** Prevents clean deletion of the old system, creates dual sources of truth, and makes live theme editing and reconstruction fragile.  
**Recommended fix:** Finish migration of every production reader, then delete `FMixtormatLiveTheme`, `DesignTokens`, `Palette` and related panels/tests. No backwards-compatibility requirement.  
**Confidence:** High.  
**Timing:** Before Release (UI stability).  
**Evidence:** Proven (file presence + grep counts of residual readers).

**P1-03**  
**Files:** `Source/MixtormatRuntime/Public/MixtormatMaterial.h` (`FMixtormatLayerChild`, `ScopeOwnerChildId`, instance fields), gather files (`MixtormatMaskGather.cpp`, `MixtormatIdGather.cpp`), Editor hierarchy/actions.  
**Code:** ScopeOwnerChildId semantics, instance SourceLayerId/SourceChildId, gather owner lookup.  
**Observed:** UI placement rules and gather ownership rules are both present and mostly aligned, but the two interpretations live in different modules. Complex combinations (scoped mask under instance under ID Group + OutputReference) are only statically verifiable with difficulty.  
**Why it matters:** A hierarchy the UI allows can in principle be evaluated differently by gather if recovery / orphan / reorder paths diverge.  
**Recommended fix:** Single canonical “effective owner / placement validator” used by both Editor and gather; add explicit recovery for stale GUIDs.  
**Confidence:** Medium (strong structural indication; full divergence not proven on every path).  
**Timing:** Before Release.  
**Evidence:** Strong static indication from cross-module ownership model.

---

### P2 — WORTHWHILE CORRECTIONS

**P2-01** Generator layers still surface / allocate / compose material-channel controls that are irrelevant to pure height/ID generators (`MixtormatMaterial.h` layer types + generator gather/passes). Clean the surface so a Generator primarily produces height/structure/IDs.

**P2-02** Oversized ownership files (`MixtormatMaterial.h` 3935 lines, `MixtormatGpuCompositor.cpp` 2228, `MixtormatGpuGeneratorPasses.cpp` 2066, several Editor Layer*/Inspector* files >1.5k). Split boundaries are already partially present; further mechanical extraction of pure data structs vs. logic would improve navigability without changing semantics.

**P2-03** Mask shaping / scalar-ramp / gate evaluation order is coherent but scanning and shaping can be repeated; generated vs authored masks are treated similarly yet not identical in every gather path. Consolidate shaping into one canonical path.

**P2-04** Ramp From IDs (and related ID consumers) already compute intermediate fields that could be published independently (gradient/strength/scalar) if the existing internal data justifies it; currently not exposed. Only publish if it removes duplicate work.

**P2-05** Residual transitional comments and compatibility naming in Material.h (legacy Automatic height source, enum append-only discipline, etc.). Harmless but increases cognitive load.

**P2-06** Preview vs bake quality settings share more infrastructure than ideal; some iterative passes do not clearly scale quality by context.

---

### P3 — MINOR / DEFERRED
- A few one-off shader helpers and effect-common utilities could be further unified.
- Thumbnail / async resource callbacks need the usual weak-ptr discipline (present in places, not exhaustively proven everywhere).
- Documentation/AgentDocs contain prototypes that are non-production.

---

### A. GPU PERFORMANCE TOP 10
1. **Erosion iterative ping-pong** (`MixtormatErosionPasses.cpp` + `.usf`) – linear in iterations (≤64) × res², R32 intermediates. Affects both. Cache mitigates recompute only. Scale iterations for preview; consider early-out. Priority: high.  
2. **Craquelure grow + distance/relief** (`MixtormatCraquelurePasses.cpp`) – multi-pass iterative growth. Same scaling.  
3. **Generator Flow jump/smooth/resolve/apply** (`MixtormatGpuGeneratorPasses.cpp`) – multi-stage sequential.  
4. **Rock Formation + facet iterations** (same file) – R32 height + IDs + edge distance.  
5. **Strata carver / multi-layer generator combination** – multiple full-res fields.  
6. **Pattern / Surface / Region ID generation + consumers** (PatternPasses, SurfaceId, ReliefId, UvId) – multiple R32_UINT maps.  
7. **Mask shaping + blur + curvature chains** (`MixtormatGpuMaskPasses.cpp`, `MaskShaping`) – repeated when many scoped masks.  
8. **Final normal / AO / packing** – necessary but bandwidth-heavy at 4K.  
9. **Runoff / FlowWarp / Breakup / WornEdges** – additional full-res passes.  
10. **Field range normalize / reduce utilities** – small but frequent.  

Caching (prefix + content hash) is effective when keys are stable; cold or hierarchy-mutation paths pay full cost.

---

### B. ARCHITECTURE TOP 10
1. Finish theme migration → delete legacy.  
2. Canonical effective-owner / placement validator shared by Editor + gather.  
3. Generator surface cleanup (no irrelevant material channels).  
4. Single shaping/gate evaluation path.  
5. Explicit preview-quality vs bake-quality iteration/format scaling.  
6. Further extraction of pure data from the giant Material.h.  
7. Published-output capability table made fully data-driven and consistent.  
8. Reduce repeated hierarchy/mask scans in Editor actions.  
9. Unify ID-consumer intermediate publication decisions.  
10. Document (and enforce) the “generators rewrite height before composite” invariant everywhere.

---

### C. THINGS THAT ARE ALREADY GOOD
- **Module boundaries and dependency direction** are clean and correct.  
- **Content-hash caching** (`MixtormatComposeHash`) is deliberately thorough (reflection + asset identity + change stamps, skips display names).  
- **Child type taxonomy** and comments in `MixtormatMaterial.h` clearly separate Mask / Effect / Generated / Generator / ID producers/consumers / OutputReference with serialization-safety discipline.  
- **ScopeOwnerChildId + instance model** is a coherent ownership design.  
- **Height blend ops** are unified between layers and generator modules.  
- **RDG usage** is modern and event-named; resource pinning and game/render thread hand-off show care.  
- **Enum append-only policy** for serialized child types is correctly documented and followed.

These should not be rewritten for cleanup’s sake.

---

### D. DELETE / KEEP / REFACTOR
| Item | Classification | Reason |
|------|----------------|--------|
| `FMixtormatLiveTheme`, DesignTokens, Palette, LiveThemePanel | MIGRATE THEN DELETE | Residual readers still exist; target is single ThemeStore architecture |
| ThemeStore / ResolvedStyle / Recipes / Schema / Locator | KEEP | Canonical new system |
| ComposeHash + prefix caches | KEEP | Correct design |
| Generator-as-layer abstraction | KEEP (with surface cleanup) | Intentional and documented; not a forced abstraction |
| Giant Material.h data structs | REFACTOR (extract) | Ownership clarity |
| Transitional “Automatic” height source etc. | MIGRATE THEN DELETE once migration complete | Compatibility only |
| AgentDocs / old prototypes | EXCLUDE (non-production) | |

---

### E. CROSS-SYSTEM RISKS
1. **Editor drag/drop/reorder → ScopeOwnerChildId → Mask/ID Gather → Compose cache**  
   Failure mode: visual change without key change, or valid-looking stale textures after source disable/delete.  
2. **Copy/paste → GUID remapping → instance Source* → OutputReference → cache**  
   Orphaned or cross-layer references that still resolve to previous data.  
3. **Generator → published ID → Ramp/UV/Relief From IDs → gate mask → Effect**  
   Duplicate evaluation or gate order change after hierarchy mutation.  
4. **Theme edit → ThemeStore vs residual LiveTheme reader → widget reconstruct**  
   Inconsistent live styling.  
5. **4K bake → iterative R32 passes → packing**  
   Memory/bandwidth spikes if intermediates are not released promptly.

---

### F. RELEASE BLOCKERS
- **Proven blockers:** None from static inspection.  
- **Strongly recommended before release:** Theme legacy deletion after migration; preview iteration scaling for the top iterative passes; validation of GUID recovery under complex hierarchy mutations.  
- **Safe to defer:** Further file splitting, minor helper unification, extra published intermediates that are not currently required.

---

### G. RECOMMENDED ORDER OF WORK
1. **Correctness / stale-reference / cache hazards** – GUID recovery, effective-owner canonicalization, cache-key audits under hierarchy mutation (affects Runtime + Shaders gather + Editor actions).  
2. **Smallest-risk high-value GPU wins** – preview vs bake iteration/quality scale factors on Erosion/Craquelure/Generator Flow/Rock (Shaders only).  
3. **Hierarchy/scoping coherence** – single placement validator.  
4. **Generator / gate / output surface cleanup**.  
5. **Legacy theme deletion** (after reader migration).  
6. **Editor cleanup** (repeated scans, large-file extraction).  
7. **Deeper optimizations** (format narrowing, intermediate reuse) only after profiling confirms the static hotspots.

Prefer incremental patches. Do not rewrite the gather/compositor or the child taxonomy; they are fundamentally sound.

---

### H. COVERAGE LEDGER

**Fully inspected (structure + key headers + critical paths + cross-references):**
- Entire `Source/MixtormatRuntime/` (all .h/.cpp, especially `MixtormatMaterial.h` hierarchy/children/scoping/IDs/generators, OutputReference, Parameter*, Mask*, Effect, ScalarRamp, LayerGroups, Surface).  
- Module Build.cs files (all three).  
- `Mixtormat.uplugin`.  
- Shaders module structure: all Compositing/ gather + ComposeHash, GpuCompositor entry, GeneratorPasses / EffectPasses / MaskPasses high-level structure, iterative loops, texture creation patterns.  
- Theme subsystem file set and residual usage counts.  
- Editor top-level structure, largest ownership files (Layer hierarchy/actions/children, Inspector*, Preview, BakeService), Style directory.

**Partially inspected:**
- Individual effect pass implementations beyond the iterative control flow and texture formats (Breakup, WornEdges, FlowWarp, Grade, LayerBlur, Runoff, Simulation, Pattern, Relief/UV/Surface ID, NormalHeight).  
- Full body of every large Editor .cpp (SMixtormat*, LayerMenus, Parameters, etc.) – structure and ownership boundaries examined, every line not.  
- Every `.usf`/`.ush` beyond parameter blocks and known hot loops.  
- Config/, Resources/, Docs/, Tools/ – only where they affect architecture (plugin metadata, icons).

**Not fully inspected (individual production files / families):**
- Exhaustive line-by-line of every one of the ~210 Editor files and every shader body.  
- All unit-test bodies (present under Editor/Tests).  
- Every possible cross-reference from UI parameter limits into serialized values into shader loops.

**Excluded as non-production:**
- `.git/`, AgentDocs/, Docs/ui-prototype, any Binaries/Intermediate/DerivedData/.vs/.idea (none present in the shallow clone), the Pass4 patch file.

**Anything whose behavior cannot be proven statically:** Exact runtime cost of iterative passes, precise cache-hit rates under complex hierarchy edits, RDG resource lifetime under concurrent preview+bake, asynchronous thumbnail hazards under rapid selection changes.

The audit followed staged file-family sweeps (Runtime → Shaders gather/compositor/passes → Editor hierarchy/theme/preview → cross-system traces). Context limits prevented a pure line-by-line of all 343 production files; the ledger above is accurate and does not claim complete coverage of every line.