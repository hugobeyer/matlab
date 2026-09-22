# Mixtormat Parameter System — Refactor & Rewrite Plan

Status: implementation in progress.
**Correction log (post external audit):** D5 originally described the transitional alias as
still present. In fact the alias and the 4-arg shim were already deleted from the runtime
header, but their users in `MixtormatGpuCompositorInternal.h` (~45 `DefaultFloat`
initializers) were never repointed — the Shaders module did not compile in that state.
Fixed: those initializers are now plain literals (they are write-before-read dead; the
per-family gather is the only observer). Phase 2 is therefore **done** as of this correction.
D10's location for `FMixtormatChildCapabilities` corrected to `MixtormatEditor/Private/Widgets/`.
**Correction log 3 (Phase 3 / S1 implemented):** the eight migrated-family gather blocks
are extracted to `MixtormatShaders/Private/Compositing/MixtormatEffectGather.{h,cpp}` as
pure moves (if-chain order preserved); `RequestComposeInternal` now dispatches one call per
family. `GetTextureRHI` is `inline` in `MixtormatGpuCompositorInternal.h` (namespace
`MixtormatGpuCompositor`); the file-static is deleted. `EffectFloat/EffectInt` are file-local
to the gather cpp (the identical `BreakupFloat/Int` lambdas folded into them). Stain and
Runoff take `bool& bHasMask` — both resolve into the mask chain. Grep gates verified: zero
`EffectFloat/EffectInt/BreakupFloat/BreakupInt` in the compositor cpp; zero
`FMath::Clamp(LayerEffect.` for migrated families (Strength's shared clamp and the Peel
fields remain by design). Phase 3.5 (budget-constant references in contract rows) is
deferred to S4: no named constants exist for the LayerBlur tap budget yet, and contract rows
(Runtime) cannot include Shaders-module headers — the dependency direction forbids it.
**Correction log 4 (Phase 6 complete; Phase 7 peeling started):** Phase 6 shader-facing
discovery is implemented: strict `.usf` scanner, bidirectional contract/tag
coverage test, derived C++ contract exclusions, and Parameter Info `Binding → shader file
(uniform)` display. The editor target built successfully and
`Mixtormat.Parameters.ShaderParamScanner` completed with `Success`.

Phase 7 peeling batch is implemented: peeling contract rows and matching `@param` annotations
were added for the existing runtime safety facts, and the procedural peeling gather was moved
to `MixtormatEffectGather.{h,cpp}` with contract sanitization. Phase 7 is not complete;
Stain and generator/mask-child families remain. S2 is complete; S3 from §6b remains undone.

**Correction log 5 (S2 complete):** `MixtormatGpuComposePipeline.cpp` now owns the render-thread
RDG dispatch, target registration, layer orchestration, snapshots, finalization, and completion
callback. Shader class definitions and shader-bound dispatch helpers remain in
`MixtormatGpuCompositor.cpp`. Target lifetime, pass order, ping-pong parity, event names,
public APIs, and the existing peeling gather were preserved. The user reported a successful
plugin build after the S2 syntax correction.

Scope: the whole parameter pipeline — declaration, inspector UI, addressing/binding,
authoring database, compositor gathering, shader contract — across `MixtormatRuntime`,
`MixtormatEditor`, `MixtormatShaders`.
Context for this plan: every file named below has been read and traced this session,
including the full type matrix (float/int/bool/enum/color/asset/string) and the complete
compositor gather region.

---

## 0. Principles (non-negotiable)

1. **The struct is the catalog.** Identity, type, default (CDO initializer), family
   (`Category`), and UI ergonomics (`UIMin`/`UIMax`/`Delta` meta) are discovered from the
   UPROPERTY. No second list of "which parameters exist" anywhere.
2. **The shader owns shader facts.** Hard bounds, normalization, saturation — the contract —
   live in one sparse runtime table (and later, `@param` annotations next to the uniforms
   they describe). UPROPERTY meta is never runtime truth; meta is editor-only.
3. **UI ranges are never restrictions.** Typed values always reach the shader. Only
   `HardMin/HardMax` restrict, only in `MixtormatParameterContracts::Sanitize*`.
4. **One canonical identity**: `(Owner, FName, ValueType)`. Display labels are presentation.
   Renames never touch FName, binding addresses, serialized fields, or uniforms.
5. **Asset compatibility**: changing defaults/meta/contracts never rewrites authored values
   in existing assets. (Pre-release materials are disposable; the rule still holds so the
   system stays correct after release.)
6. **No dead code, no transitional shims surviving a phase.** Every phase ends with the
   previous mechanism fully deleted, not deprecated in place.

---

## 1. Current state (verified by read, not memory)

### 1.1 Where each fact lives today

| Fact | Source of truth | Duplicates still present |
|---|---|---|
| Existence/name/type | Reflection (`FProperty`) | none |
| Family | `Category` meta prefix ("Breakup", "Erosion", …) | none |
| Default | CDO initializer | `FEffectRenderData` literals (dead post-gather); inspector literal args (fallback only) |
| UI min/max/snap | `UIMin/UIMax/Delta` meta | inspector literal args (fallback only) |
| Hard bounds / shader contract | `MixtormatParameterContracts` sparse table | compositor literals for **unmigrated** families (Peel, Stain, mask children); `MixtormatReliefScaling.h` for Layer-owner params (separate regime, see §4 Cross-cutting decisions) |
| Display label | LOCTEXT at call sites + authoring DB override | — |
| Authoring retunes | `Config/MixtormatParameterAuthoring.json` (pending → shipped → CDO) | session override (intentional) |

### 1.2 Migrated (on the new rails)

- Breakup (33), Erosion (14), Grade (13), LayerBlur (3), FlowWarp (9), Runoff (10),
  WornEdges (27) — contract rows + compositor `Sanitize*` calls.
- Erosion + Grade UPROPERTY annotations (`UIMin/UIMax/Delta`).
- Editor resolver (`MixtormatParameterUiMeta` = reflection), authoring DB (JSON,
  `IPluginManager` path, transactional save), MakeMemberSlider chain
  (pending → shipped → meta → literal), Parameter Info / Authoring panels, RMB Developer
  menu behind `Mixtormat.Developer.ParameterMeta`.
- Creation hooks (`ApplyChildCreationDefaults`, `AddEffectToLayer`, `AddEffectToGroup`)
  applying shipped DB defaults to genuinely-new children only.
- Tests: sanitize contract, meta completeness per migrated Category, authoring
  resolution/opt-in/creation/identity.

### 1.3 Verified defects / debt (the work list)

| # | Issue | Where |
|---|---|---|
| D1 | Authoring DB family-key mismatch: `WriteToString` uses `Category` prefixes ("Worn Edges"), `LoadFromString` validates against **enum names** ("WornEdges"). Saved WornEdges/LayerBlur/FlowWarp entries would be dropped as "unknown family" on reload. | `MixtormatParameterAuthoring.cpp` (both functions) |
| D2 | Resolver reads `ClampMin` as UI-min fallback but **not `ClampMax`** — Runoff-style fields (ClampMin/ClampMax) resolve only half their range from meta. | `MixtormatParameterUiMeta.cpp` GetReflectedUi |
| D3 | `FamilyNameOf` dead in authoring cpp | same file |
| D4 | Header doc drift: contract header says keys are "(Owner, FName)"; the key carries `ValueType`. | `MixtormatParameterDefinition.h` |
| D5 | ~~Transitional alias + 4-arg shim~~ **Resolved**: alias/shim deleted; the dangling `DefaultFloat` users in `FEffectRenderData` reverted to plain literals (write-before-read dead; gather is the only writer). Verified: zero `MixtormatParameterDefinitions::` references remain. | runtime header, `MixtormatGpuCompositorInternal.h`, `MixtormatGpuCompositor.cpp` |
| D6 | `ValueTypeForProperty` maps unknown property types to **Float** (silent wrong address) instead of "no address". | `SMixtormat_Parameters.cpp` |
| D7 | Enum interactive writes bypass bindings: every enum row writes its member directly; no `TryWriteLinkedEnum` → a Link on an enum parameter silently desyncs. Also three different enum control idioms (menu-chip, checkbox-as-2-enum, segmented) hand-built per family. | inspector builders, `SMixtormat.h` |
| D8 | Meta/literal disagreements after the parallel agent's edits (e.g. `ErosionDepth` meta 0..2 vs literal 0..4; `ErosionRadius` CDO 1 vs render-data literal 2; `StainGravity` CDO -1.0 vs render-data 1.0). Meta/CDO now win by design; literals must be reconciled or deleted. | inspector + render data |
| D9 | Monolith: all family gather blocks inline in `RequestComposeInternal` (~L2450–2990); `GetTextureRHI` is a file-static; mask-child gathers (13 blocks, L1554–2366) interleave `bHasMask`/palette logic. | `MixtormatGpuCompositor.cpp` |
| D10 | Precedents not yet converged: `FMixtormatMaskShaping` (the original clamp-vs-range split, with named unmigrated copies), `MixtormatReliefScaling.h` (Layer-owner bounds regime), `MixtormatEditor/Private/Widgets/MixtormatChildCapabilities.h` (discoverable-registry pattern). | runtime headers + editor widgets |
| D11 | Shader facts are prose comments in `.usf`; no machine-readable source for contract rows → table vs shader drift is caught only by eyeball. | `Shaders/Private/*.usf` |

Non-issues, verified: strings do not exist as parameters; colors/asset-refs/FGuids are
structural by design; `PostLoad` does identity+group repair only (no param fixups to
preserve); `FMixtormatMask.cpp/Effect.cpp` are trivial.

---

## 2. Target architecture

```
UPROPERTY (struct)      → identity, type, family (Category), default (CDO),
                          UI ergonomics (UIMin/UIMax/Delta), documentation (tooltip)
MixtormatParameterContracts (runtime, sparse) → HardMin/HardMax, NormalizationScale,
                          bShaderSaturates  [later: generated/verified from @param]
Authoring JSON (editor) → label/default/UI/snap retunes (pending → shipped → compiled)
Compositor gather       → per-family functions; sanitize through the contract;
                          derived math stays local and explicit
Binding layer           → the typed addressing spine (Float/Int/Bool/Enum), unchanged
```

One identity everywhere: `(Owner, FName, ValueType)` via
`FMixtormatParameterDefinitionKey`. Family = `Category` prefix. No registration lists.

---

## 3. Phases

Each phase is independently shippable: build green, tests green, in-range rendering
pixel-identical. Order matters only where noted.

### Phase 0 — Correctness sweep (small, do first)

Files: `MixtormatParameterAuthoring.cpp`, `MixtormatParameterUiMeta.cpp`,
`MixtormatParameterDefinition.h`, `SMixtormat_Parameters.cpp`.

1. **D1**: canonical family spelling = Category prefix. `LoadFromString` stops validating
   against the enum; it accepts the section name verbatim (it is only a grouping key, and
   `ApplyAuthoringDefaults` already matches against Category). Keep a warning for a section
   that matches no known Category prefix. Delete the enum lookup.
2. **D2**: read `ClampMax` as UI-max fallback exactly like `ClampMin` (Runoff fields then
   resolve fully from meta).
3. **D3**: delete `FamilyNameOf`.
4. **D4**: fix the header comment to "(Owner, FName, ValueType)".

Tests: extend authoring resolution test with a "Worn Edges"-spelled section round-trip.

Risk: none beyond the normal. Rollback: trivial reverts.

### Phase 1 — Finish the annotations; make completeness green

Files: `MixtormatMaterial.h` (annotations only), inspector literals.

1. Annotate Breakup fields with `Delta` (most already carry `UIMin/UIMax`).
2. Annotate WornEdges / FlowWarp / LayerBlur / Runoff fields with
   `UIMin/UIMax/Delta` — values are the existing inspector literals, transcribed once.
3. Reconcile D8 disagreements in favor of meta: update the affected inspector literals
   (or delete literals for fully-annotated families — see Phase 2).
4. Completeness test already enforces this per Category prefix; extend its prefix list as
   families finish. Run it; fix everything it names.

Exit: completeness test green for all seven families; the mismatch warning silent in a
normal session.

Risk: a wrong annotation changes only the drag range, never results. Rollback: revert the
meta line.

### Phase 2 — Delete the transitional layer — **DONE (verified)**

Completed as of the correction log: the alias and 4-arg overload are deleted from the
runtime header; the compositor sanitize lambdas call `MixtormatParameterContracts`
directly (3-arg); the `FEffectRenderData` initializers are plain literals again.
Remaining in this phase's spirit (optional, next housekeeping pass):

1. Keep `DefaultFloat` in the contracts API — the editor resolver and tests use it via
   reflection. Render-data initializers never touch it any more.
2. For fully-annotated families: delete the per-family inspector literal args (call sites
   pass the meta-driven values only) — optional now, mandatory never; literals are harmless
   fallbacks once they match.

### Phase 3 — Compositor gather extraction (structural)

Files: new `MixtormatShaders/Private/Compositing/MixtormatEffectGather.{h,cpp}`,
`MixtormatGpuCompositor.cpp`, `MixtormatGpuCompositorInternal.h`.

1. `GetTextureRHI` → `inline` in `MixtormatGpuCompositorInternal.h` (namespace
   `MixtormatGpuCompositor`); delete the file-static.
2. New `MixtormatEffectGather` with one function per migrated family, pure moves of the
   verified blocks:
   `GatherGrade, GatherErosion, GatherLayerBlur, GatherFlowWarp, GatherWornEdges,
   GatherRunoff, GatherBreakup, GatherStain(FEffectRenderData&, const FMixtormatLayerEffect&,
   bool& bHasMask)`.
   - Sanitizer helpers become file-local (`EffectFloat/EffectInt`).
   - Derived math stays inside each function, local and explicit (cell counts, size range,
     strata count, radians conversion).
3. `RequestComposeInternal` replaces each block with the call. The `FPending*` deferral
   machinery, ordering, and mask-child blocks stay put (scheduling ≠ gathering).
4. Pattern to follow: `ResolveRegionUVBinding`/`FRegionUVBinding` (L505–625) is the file's
   own existing example of an extracted, typed gather — new gather functions mirror that
   shape (context struct in, fields written, derived math local).
5. Budget constants authority: `MixtormatCompositeCS::MaxLayerGrades`, `MaxScalarDrivers`,
   `MaxRegionPalette` (and LayerBlur's per-axis target count) are the *source* of several
   hard bounds. During extraction, contract rows and asserts should reference those
   constants rather than restate magic numbers, so shader budget and contract cannot drift.
4. Mask-child gathers (13 small blocks, L1554–2366) stay inline this phase; they interleave
   with palette/`bHasMask` logic and are small. Extract as a follow-up only if a family
   migration touches them anyway.

Exit: `RequestComposeInternal` reads as walk + dispatch; every family gather is one
call. No behavior change (pure moves). Pixel-diff the Breakup/Erosion previews as the
verification.

Risk: transcription slips. Mitigate: move blocks verbatim; keep function order identical
to the original if-chain.

### Phase 4 — Type-system hygiene (enum/bool parity)

Files: runtime binding, editor inspector, new generic control.

1. **D6**: `ValueTypeForProperty` returns "invalid" for unmapped types; rows for such
   properties get no state dot, no menu, no authoring entry.
2. **D7a**: add `TryWriteLinkedEnum(Scope, Address, int64 Value)` + `TryResolveEnum`,
   mirroring the existing typed write/resolve; enum menu lambdas route through it.
3. **D7b**: one generic `MakeMemberEnum(Label, Resolve, Member)` — builds entries from
   `StaticEnum` (or the property's UEnum via reflection), applies the authoring label
   override, routes writes through the linked path. Convert the existing hand menus
   (Tonemap, blend modes, operations, PeelType) onto it; delete the checkbox-as-enum
   special case (PeelType becomes a real two-entry menu).
4. Bool: keep `MakeMemberToggle`; no additional machinery.

Exit: all four value types have symmetric read/write/resolve/link paths and one control
idiom per type.

Tests: an enum Link test (destination follows source after a menu write) — the case that
silently broke.

Risk: menu conversions touch many call sites; mechanical. Rollback per menu.

### Phase 5 — Converge the three bounds regimes (Layer + MaskShaping)

Files: `MixtormatParameterDefinition.*`, `MixtormatReliefScaling.h`, `MixtormatMaterial.h`,
`MixtormatMaskShaping.h` adopters.

1. Add Layer-owner contract rows (HeightBoost 0..`MaxHeightBoost`, NormalStrength
   0..`MaxNormalStrength`, FuzzInfluence 0..1, IOR…) — but keep `MixtormatReliefScaling`
   functions as the façade the compositor calls, now backed by the table instead of inline
   constants. One bounds home, readable call sites.
2. Embed `FMixtormatMaskShaping` in `FMixtormatGeneratedMask`, `FMixtormatCraquelure`,
   `FMixtormatColorIdMask` (its own header names these as the remaining copies). Serialization
   note: pre-release asset wipe means the flat fields can be removed outright — no
   `DeprecatedProperty` ceremony needed.
3. Extend the editor resolver to Mask/Generated/Craquelure/ColorId/Generator owners (the
   owner-struct switch is the only thing blocking `TryResolveUi` beyond Effect today).
4. MaskShaping's `MixtormatMaskShapingRange` constants migrate into `UIMin/UIMax/Delta`
   meta on the embedded struct; the namespace is deleted.

Exit: three bounds regimes → one table (+ one façade); shaping declared once.

Tests: completeness test extended to the new owners/categories; shaping range test.

Risk: serialization shape change for the shaped structs — safe pre-release (user has
wiped materials); would need `DeprecatedProperty` migration post-release.

### Phase 6 — Shader-facing discovery (`@param` annotations) — **DONE**

Verified: editor build succeeded and `Mixtormat.Parameters.ShaderParamScanner` passed.

Files: `Shaders/Private/*.usf`, new editor parser (`MixtormatShaderParamScanner.*`),
contract table, tests.

1. Annotate uniforms where the math lives:
   `// @param BreakupNormalStrength hardmin=0` /
   `// @param BreakupDensity saturates` /
   `// @param PeelAOStrength normalize=8` /
   `// @param BreakupFoldWidth hardmin=1e-4 divisor`
   — uniform name on the previous line, authored FName + facts in the tag.
2. Small strict parser (editor): scans plugin `.usf` once, builds
   `UniformName → {ParamId, HardMin/HardMax, Saturates, Normalize}` + uniform-name map.
3. A verification test: parser output vs the runtime contract table must match **exactly**
   (every tag has a row, every row has a tag, values equal). The hand table becomes a
   compiled artifact verified against the shaders, and Parameter Info gains real
   `Binding → MixtormatBreakup.usf` data from the same map.
4. Un-tagged uniforms that map to a contract row = test failure; tagged names that match no
   authored property = test failure.

Exit: shader↔C++ drift is a named test failure. Runtime keeps using the table — no file IO
in the sanitize path.

Risk: annotation rot → caught by the test. Parser fragility → bounded by the strict
grammar. Do this **after** Phase 3 so tags can be written once per family during its
gather migration instead of twice.

### Phase 7 — Peeling, Stain, generator/mask-child migration

Files: contract rows, compositor blocks (now easily extracted via the Phase-3 pattern),
annotations, render-data literal reverts.

1. Peeling (~40 params): contract rows from the shader (Phase 6 annotations written during
   this pass), gather extraction, annotations, DB reachable immediately.
2. Stain: decide pass-through (current) vs bounds; annotate or leave contract-free.
3. Mask-child families (Generated, Craquelure, ColorId, Cluster, Hsv, Random, Ramp,
   Pattern, UvId, ReliefId, CombineId) + `FMixtormatStrataCarver`: migrate gathers into
   the gather file, extend the editor resolver's owner switch, extend the completeness
   test prefixes.
4. `MixtormatLayerPreview` / `BakeService` `DA_*` material parameters: **out of scope** —
   different system (UMaterial instance parameters); document as such.

Exit: every authored scalar in the plugin is discoverable, contract-verified, and
authorable through one system.

### Phase 8 — Final cleanup

- Delete the inspector literal args entirely for annotated families (signature shrink to
  `(Label, Resolve, Member)` + tooltip), or keep them as verified fallbacks — decide with
  the code in front of us; both are defensible.
- Remove `ReportLiteralMismatch` once no literals remain.
- Update `Documentation.html`/`priorities.md` references to the new structure.

---

## 4. Cross-cutting decisions already made (do not relitigate)

- **Defaults affect untouched fields globally** (delta-serialization): accepted, standard
  UE behavior. Pre-release materials are disposable; explicitly-authored values are
  recorded and never move.
- **No `.usf` text rewriting** from UI; shader facts enter only via annotations read by
  the editor/test.
- **UI max ≠ hard max**, permanently. Any future feature that wants to restrict must add
  a contract row, never widen a slider's meaning.
- **Units contract**: every default in the system (CDO initializer, contract, authoring DB)
  is in **stored member units**. Only the legacy inspector literal is in UI units and only
  it is multiplied by `ValueScale`. Never scale a canonical default by `ValueScale` — that
  double-scales (Hue Shift, degrees-vs-signed, is the exemplar row).
- **Developer surface gating**: the entire Developer menu + authoring UI is editor-module
  only and behind `Mixtormat.Developer.ParameterMeta` (default 0). Packaged builds contain
  none of it. This gate stays regardless of how broad the tooling gets.
- **Registration lists are banned.** Discovery is reflection (+ shader annotations +
  Category). The brief-lived `Register` API and per-family definition files were removed
  in favor of this and must not come back.

## 5. Verification protocol (every phase)

1. Build: editor target, zero new warnings.
2. Automation: `Mixtormat.Parameters.*` (sanitize, completeness, authoring) green.
3. Render check per touched family: same input → same pixels (in-range values only; typed
   out-of-range values follow the documented contract change if any). Render-thread behavior
   can be verified without screenshots via the existing in-file render test precedent
   (`FMixtormatPrompt2RegionUVTest` in MixtormatGpuCompositor.cpp) — a gather extraction
   phase should add one such test per extracted family.
4. Grep gates at phase exits: Phase 2 — no `MixtormatParameterDefinitions::`;
   Phase 3 — no `FMath::Clamp(LayerEffect.` for migrated families; Phase 6 — every
   contract row has a matching `@param`.

## 6. Suggested sequencing from here

Phase 0 + Phase 1 are a single sitting. Phase 2 follows immediately (small). Phase 3 is
the first structural one and unblocks everything else; Phase 4 is independent and can
interleave. Phase 6 lands right after Phase 3, before Phase 7, so Peeling writes its tags
once. Phases 5 and 7 are the bulk; Phase 8 is the broom.

## 6b. File decomposition schedule (correlated to phases)

Splits ride along with the phase that already touches the file — never as standalone
cleanup passes. "Pure move" = no behavior change; verify per §5.

| # | Split | Rides with | Risk |
|---|---|---|---|
| S1 | `MixtormatEffectGather.{h,cpp}` — 8 effect-family gather blocks out of `RequestComposeInternal`; `GetTextureRHI` → inline in internal header | **Phase 3** (this IS the phase) | **DONE**; pure move verified |
| S2 | `MixtormatGpuComposePipeline.cpp` — RDG dispatch/targets/layer orchestration out of the monolith | **Phase 3**, immediately after S1 | **DONE**; pure move |
| S3 | `MixtormatGpuCompositeShaders.h` — the four composite shader classes (compositor L121–442) | **Phase 3** | Pure move |
| S4 | `MixtormatGpuEffectPasses.<Family>.cpp` — Craquelure (~800), Erosion (~420), WornEdges (~300), Breakup (~260), Grade, LayerBlur, FlowWarp out of the 3,130-line passes file | **Phase 7** per family (migrate + split in the same pass) | Mechanical; shader-class-per-file preserves the rebuild-one-file property |
| S5 | Inspector per-family panels: `Build*Controls` + menu builders out of `SMixtormat_Inspector.cpp` (~3,900) | **Phase 7** per family | Pure move |
| S6 | `MixtormatParameterBinding.cpp` → identity (`EnsureStableIds/Regenerate*/ResolveChildInstances/Classify*`) vs addressing (`Locate*/Read/Write/Resolve/Try*`); collapse the duplicated 17-case const/mutable owner switch | standalone, anytime after Phase 4 | Low; watch unity-build registrar-style name clashes |
| S7 | `SMixtormat_Parameters.cpp` → address builder + `TryReadAuthoredScalar` out (menus/panels stay) | standalone, anytime | Pure move |
| S8 | `MixtormatGpuCompositorInternal.h` → `MixtormatGpuRenderData.h` (the `F*RenderData` structs) vs pass contexts/helpers | **after Phase 7** (context churn settles) | Low; data structs have no downward deps |
| S9 | `SMixtormat.h/.cpp` god-widget decomposition (parameter-row templates → own header; shell/document/inspector triad) | **deferred** — architecture project, not cleanup | Highest |
| S10 | `MixtormatMaterial.h` (~3,400-line USTRUCT monolith) split per concern | **deferred** — highest churn, every module includes it; readability already served by the parameter system | Highest |
| S11 | `MixtormatCompositorTests.cpp` per family | rides with S4 | Trivial |

Do NOT split: `MixtormatParameterDefinition.{h,cpp}` (small, single-purpose),
`MixtormatMaskShaping.h` / `MixtormatReliefScaling.h` (exemplars of right-sized),
internal-header pass contexts (would circularize).

## 6c. Modular layer-graph handoff

The four vertical audits are recorded in `Docs/modular_layer_graph_audit_handoff.md`.
They establish the next architectural boundary without changing the current serialized model:

- Generators produce form fields and channel contributions.
- IDs produce topology and typed structural outputs.
- Masks route, shape, gate, and publish reusable outputs.
- Effects and filters operate on generated or composed channels.
- Layers remain the artist-facing stack; each layer gains an internal compiled dependency graph.
- `MixtormatGpuCompositor` should become a graph compiler/scheduler rather than the catalog of
  every family, parameter, output, and dependency.

Parameter-system consequences:

1. Reflection remains the authored parameter catalog.
2. Shader contracts remain the source of hard safety facts.
3. Generator, ID, and Mask owners must use the same typed addressing and contract path as Effects.
4. New family descriptors should provide parameter metadata, channel inputs/outputs, neutral state,
   mask behavior, and dependencies together.
5. Do not add manual registration lists for parameters already declared by reflection or shader
   descriptors.
6. UI ranges remain artist guidance; sanitization is limited to real shader invariants.
7. Define channel-combine contracts before extending parameter completeness to new families.

S2 is complete and provides the render-thread scheduling seam. S3–S5 remain deferred until the
layer graph contracts are designed.

## 7. Working rules for the implementing agent

Learned the hard way in the session that produced this plan — follow these and the
phases go smoothly; skip them and expect broken builds:

1. **Read before editing.** Never edit from memory of an earlier read or an earlier edit.
   Line numbers in this doc are approximate and WILL have shifted by the time work starts;
   re-locate every region with a fresh read or grep first. Re-read a file after your own
   earlier edits to it before making the next one.
2. **Verify edits landed as intended.** After each batch, re-read the edited region. At
   least three edits this session silently mangled adjacent lines (brace imbalance, a
   mangled paint call, a literal "..." placeholder) and were only caught by re-reads.
3. **Grep-verify at every exit.** After deleting a mechanism, grep its name across the
   whole `Source/` tree and require zero matches before declaring the phase done.
4. **One mechanism per commit-sized batch.** The alias deletion that broke the build was
   safe alone; it became a breakage because its users were migrated piecemeal across
   later turns while a doc claimed otherwise.
5. **The code wins over this doc.** If a statement here disagrees with what a fresh read
   shows, the read is right — update the doc (see the correction log at the top).
6. **Behavioral invariants to re-check after every phase:** in-range rendering is
   pixel-identical; typed values outside the drag range still reach the shader; Reset and
   the modified-stripe agree with each other; serialized assets are byte-compatible.
7. **Build + run `Mixtormat.Parameters.*` automation tests before claiming any phase
   complete.** The editor user builds; the agent does not — so state exactly which test
   names and grep gates the human should run.

### Where the session's trace knowledge lives

- Fact-location matrix, defect register D1–D11, migrated inventory: §1 (this doc).
- Full type matrix (float/int/bool/enum/color/asset/string × declaration → shader):
  traced in conversation; re-derivable from `MixtormatParameterBinding.cpp`
  (`PropertyMatchesAddress`, `ReadLocalValue`, `WriteResolvedValue` — all four value
  types), `SMixtormat_Parameters.cpp` (`ValueTypeForProperty`), and the inspector builders.
- Compositor gather blocks + extraction plan: §3 Phase 3, verified against
  `MixtormatGpuCompositor.cpp` (outline + key regions).
- Unannotated families' UI literals (needed for Phase 1): still present as fallback
  arguments at each `Build*Controls` call site in `SMixtormat_Inspector.cpp` — transcribe
  from there, don't guess.
- Open decision pending human input: `ErosionDepth` shipped drag range — meta says 0..2,
  old literal said 0..4; meta wins by default, change the meta if 4 is wanted.
