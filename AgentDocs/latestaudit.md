# Mixtormat Audit — Modularity / Perf / Architecture

The original audit reported 222 C++ files, 45 `.usf` files, and ~19k GPU-module lines. Those inventory counts were not reproduced in this review.

> **Sequential source review, 2026-10-03.** Confirmed mechanisms are distinguished from unverified counts, performance estimates, and architectural recommendations below. No commands, builds, profiling, or runtime tests were run. Subsequent implementation changes added the Erosion and Wet Stain safety caps and removed the unused standalone Grade path, as recorded below. Diagnostics for the Erosion change were blocked by missing Unreal headers.

---

## Main findings (severity not established by profiling)


**1. Runtime hard-bounds table contains no bounds** — `MixtormatRuntime/Private/MixtormatParameterDefinition.cpp:29-172`
**Confirmed:** every table entry passes `{}` or `Saturated()`; the latter sets only `bShaderSaturates`. Runtime contract `HardMin`/`HardMax` values remain unset. `DivisorFloor` (`:20`) is declared but unused. The original ~140-entry count was not reproduced.
→ The claim that these names are "never written anywhere in Source" was incorrect: the editor shader scanner writes its own tag bounds, and editor slider bounds also exist. Those do not populate the runtime contract table.
→ `SanitizeFloat` leaves finite values unclamped, but still replaces non-finite values with reflected defaults (`:334-348`). Shader-side epsilon guards and effect-specific clamps can provide additional protection; this is not evidence that every out-of-range value is unsafe.
→ Review each parameter's actual mathematical/workload requirements before adding runtime bounds. A `saturates` tag documents shader behavior, not necessarily a required CPU clamp. Removing the bounds mechanism would be a separate behavior/design decision, not an equivalent fix.

**2. Bound empty-state text performs an uncached surface query** — `MixtormatEditor/Private/Widgets/SMixtormat_Layers.cpp:180-185`
**Confirmed:** `Text_Lambda` calls `FMixtormatRegistry::GetSurfaces()` solely to test emptiness. Each evaluation performs a `FARFilter` query restricted to surface classes and configured library roots, resolves matching assets with `Asset.GetAsset()`, constructs entries, and sorts them with string conversions.
→ This is not a full-registry walk. `GetAsset()` can synchronously load unloaded assets; already-loaded assets are not necessarily loaded again. Exact Slate evaluation frequency and measured UI cost were not established. The parent widget is visible when no working material is open.
→ Cache the availability result or provide a lightweight existence query, with invalidation when library assets change. This is not necessarily a single-line fix, and "largest user-visible win" requires profiling.

---

## Additional findings (source evidence and limits)

| Area | Issue | Where |
|---|---|---|
| Runtime | **Confirmed for enabled RegionIds references:** `ResolveSource` calls validation that copies the layer array, resolves child instances, and repeatedly scans dependency nodes. Worst-case graph construction can be quadratic. Non-RegionIds references take a different path; this is not a copy for every child. Layer byte size and compose-call frequency were not measured. | `MixtormatOutputReference.cpp:88-272` |
| Runtime | **Confirmed:** `ComputeFuzz*` recursively resolve sources with `LoadSynchronous()`, without memoization or a depth cap. They do have active-path cycle guards. Repeated shared subgraphs can multiply traversal work; sufficiently deep unvalidated chains risk stack exhaustion. `Validate()` separately has cached depth checks and a 32-level limit. No runtime failure was reproduced. | `MixtormatMaterial.cpp:53-223` |
| Runtime | Address keys built via `FString::Printf` + 2 `FGuid::ToString` per hop; fresh `TSet<FString>` **per binding**; `LocateOwner` linear scans layers×children | `MixtormatParameterBinding.cpp:187,345,506,236` |
| Runtime | **Confirmed:** `PostLoad` repairs invalid/duplicate IDs using `FGuid::NewGuid()` and invokes group validation. Validation removes groups with no remaining member range, not arbitrary groups. Repairs may differ across loads of the same unrepaired asset; transaction/persistence implications need separate review. | `MixtormatMaterial.cpp:225-231`, `MixtormatParameterBinding.cpp:562-597`, `MixtormatLayerGroups.cpp:172-180` |
| Runtime | **Confirmed:** preview material/thumbnail properties are hard `TObjectPtr` references without editor-only guards. They can retain cook dependencies. Actual cooked inclusion, dependency-chain size, and the claim that they are never read at runtime were not verified; the properties are Blueprint-readable. | `MixtormatSurface.h:72-79`, `MixtormatMask.h:31-32` |

| Shaders | **Confirmed:** `CreateTarget` defaults to `PF_FloatRGBA`, which is four half-float channels: **8 B/pixel, 32 MiB per 2048² target**, excluding overhead. BaseColor, Normal, RAM, and Debug use that default; Height overrides it with `PF_R16F`. The original 16 B/pixel estimate was wrong. Total VRAM, measured bandwidth, and whether lower precision preserves output quality were not established. | `MixtormatGpuCompositor.cpp:448-461,1326-1336` |
| USF | **Partially confirmed:** repeated lowbias-style hash bodies exist, including Breakup, Gully, Pebbles, and an inline Erosion version. The original exact-copy count and claimed preview-desynchronization requirement were not fully reverified. Different seed composition and float conversion must be preserved during any extraction. | `MixtormatBreakup.usf:106`, `MixtormatGully.ush:20`, `MixtormatPebbles.usf:51`, `MixtormatErosion.usf:90-101` |
| USF | **Confirmed:** the seed branch calls `PeelPhi` five times at the center and four neighboring UVs. The asserted nested `PeelDamage`/32-sample cost and eight normalizations were not reverified. These are different sample positions; compiled instruction counts and safe reuse require further inspection/profiling. | `MixtormatPeelField.usf:990-1046` |
| USF | **Confirmed:** Erosion's hash locally wraps coordinates using `% Period`, then `(C + Period) % Period`. This is a negative-coordinate wrapping idiom, not proof that already-wrapped coordinates are redundantly wrapped. Period is constant (32); source modulo expressions do not establish 16 hardware integer divisions per pixel. | `MixtormatErosion.usf:90-101` |
| Editor | **Architectural concern, not a proven testability failure:** widget-owned editing logic may make isolated tests harder. "Untestable" and "tests only cover the compositor" are incorrect; library-path and parameter-authoring tests cover other behavior. The original method/header/implementation counts were not reproduced. | `SMixtormat.h`, `Tests/MixtormatLibraryPathTests.cpp`, `Tests/MixtormatParameterAuthoringTests.cpp` |
| Editor | **Confirmed:** reviewed registry methods query without a cache. `GetMasks()` resolves each matching mask/texture asset and may load generated thumbnails. Already-loaded assets need not reload. These are class methods, not free functions; the original function count and absence of all possible test seams were not reverified. | `Services/MixtormatRegistry.cpp:34-223` |

---

## Cross-cutting themes

**Shared shader helpers coexist with local implementations.** Repeated hashing and different 24-bit float conversions are confirmed. Breakup, Cracks, EdgeWear, Erosion, Gully, Stain, and StrataCarver use a divisor of `16777216`; Cellular, CraquelureGrow, Pebbles, PeelField, and RegionId use `16777215`. These lists are not exhaustive: GeneratorFlow and RockFormation also use `16777216`.

The conversions have different endpoint semantics: division by `16777216` yields `[0, 1)`, while division by `16777215` can yield 1. This is a difference, not by itself proof of accidental drift or a bug. Deduplication must preserve each caller's hash, seeds, wrapping, and conversion semantics unless a behavior change is explicitly intended.

**Not reverified:** the original wrap (10), RNM (4), periodic-noise (7), C++ address-remap (5), and debug-epilogue (8) duplication counts, or blanket claims about which `.ush` helpers are used.

**Architecture recommendation:** separating document editing and asset queries from Slate may improve isolated testing and caching. A lack of dependency injection does not make caching or testing impossible; extraction scope and payoff require further review.

**Original positive observations, not reverified:** editor style consistency (735 token references, zero external font constructions, 21 color literals including 14 in tests), UI component differentiation, asset migration quality, and preview-throttle quality. Treat these as original audit observations, not results of this source review.

---

## Resolved — unused standalone Grade path

- Removed `AddGradePasses`, its header declaration, `FMixtormatGradeCS` and its registration, and `Shaders/Private/MixtormatGrade.usf`.
- Preserved `QueuePendingGrade`, active parameter packing, and Grade math in `MixtormatComposite.usf`.
- The removed path graded the accumulated stack; the active path grades only the owning layer before blending. They were not behaviorally identical, and the unused path was not reconnected.
- Updated the shader-parameter drift test to recognize `GradeTonemapStrength` and `GradeGamma` as C++-packed composite parameters rather than requiring tags from the deleted shader.
- Source cleanup only; build and runtime Grade tests were not run.

## Resolved — iteration safety caps

- **Erosion:** the C++ wear-loop count is clamped to **1–64** in `MixtormatShaders/Private/Effects/MixtormatErosionPasses.cpp`. The same capped count is passed to the shader for step stability; the shader comment documents it.
- **Wet Stain (not Runoff):** the gathered iteration count is clamped to **4–128** in `MixtormatShaders/Private/Compositing/MixtormatEffectGather.cpp`. The dispatch loop and resolve pass consume that capped count.
- These are code safety nets only. UI ranges, UI hard clamps, and saved authored values were not changed. UI limits remain owner-controlled; UI ranges must not be promoted into runtime bounds automatically.
- The unbounded-iteration finding is resolved in source. The selected limits have not been benchmarked or runtime-tested.

## Suggested order

1. Review the empty-state surface query: use a lightweight query or a cached result with correct asset-change invalidation, then measure UI cost.
2. Review remaining runtime contract rows against shader math. Preserve the non-finite guard and distinguish safety bounds from artistic/UI ranges; any new cap requires an explicit decision.
3. Profile binding and RegionIds validation before adding typed address keys, owner indexes, or graph-result reuse. These are separate costs; one index does not automatically fix all findings.
4. Extract hash/wrap helpers only where behavior can be preserved and tested. No zero-performance-cost or mechanical-equivalence guarantee was established.
5. Consider document-editing and registry service extractions separately; these are architectural proposals, not verified fixes.
