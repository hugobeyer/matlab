# Mixtormat Modular Layer Graph — Audit Handoff

Status: architecture audit complete; S2 implemented and the user reports a successful build.

This document records four parallel vertical audits:

- Grade and Breakup effects
- Strata Carver generator
- Pattern IDs
- Masks and mask components

No behavior changes are proposed here. This is the handoff for the next architecture pass.

---

## 1. Target architecture

Mixtormat should remain layer-based for artists, while each layer behaves internally like a
small, typed node graph:

```text
Layer stack
  └─ Layer graph
       ├─ Form generators
       ├─ ID / topology producers
       ├─ Masks and reusable output references
       ├─ Effects / filters
       └─ Channel composite
```

The UI should stay Photoshop/Substance-Painter-like: simple rows, presets, previews, and
artist-friendly controls. The graph, dependency compiler, typed outputs, and RDG schedule
remain implementation details.

### Semantic families

- **Generators** create form fields: SDF, height, normal, AO, and related channels.
- **IDs** create structure: regions, cells, edges, gaps, fracture identity, and topology.
- **Masks** route or gate contributions and can consume reusable published outputs.
- **Effects / filters** modify an already-generated or already-composed surface.
- **Compositing** combines typed channels using explicit per-channel rules.

The compositor should eventually compile and schedule this graph. It should not contain every
family's parameter gather, dependency rules, output metadata, and pass switch.

---

## 2. Audit: Grade and Breakup effects

### Grade

Primary paths:

- `Source/MixtormatRuntime/Public/MixtormatMaterial.h`
- `Source/MixtormatEditor/Private/Widgets/SMixtormat_Inspector.cpp`
- `Source/MixtormatShaders/Private/Compositing/MixtormatEffectGather.cpp`
- `Source/MixtormatShaders/Private/MixtormatGpuEffectPasses.cpp`
- `Shaders/Private/MixtormatComposite.usf`
- `Shaders/Private/MixtormatGrade.usf`

Findings:

- Grade is gathered into `FEffectRenderData` and packed into the composite shader.
- Active Grade math lives in `MixtormatComposite.usf`.
- A standalone Grade shader/pass still exists but has no active call site.
- Grade capacity is duplicated between C++ and shader constants.
- Grade parameters are manually packed into anonymous `float4` arrays.
- Inspector fallback ranges still duplicate reflected metadata.
- Direct behavior coverage is weaker than Breakup coverage.

Recommended boundary:

- One Grade descriptor owns identity, parameters, channel effects, mask behavior, and capacity.
- One gather converter creates typed Grade data.
- One active pass implementation owns the shader path.
- Remove the unused duplicate implementation only after compatibility is confirmed.

### Breakup

Primary paths:

- `FMixtormatLayerEffect` in `MixtormatMaterial.h`
- `GatherBreakup()` in `MixtormatEffectGather.cpp`
- `QueuePendingBreakup()` and `AddBreakupPasses()` in
  `MixtormatGpuEffectPasses.cpp`
- `Shaders/Private/MixtormatBreakup.usf`
- `MixtormatChildCapabilities.cpp`

Findings:

- Breakup currently mixes topology generation, region IDs, gap/edge/pieces outputs,
  height modification, normal/AO/roughness shading, and published-output behavior.
- Breakup is classified as a Filter even though it performs structural post-composite work.
- It prepares isolated channels, composites, applies structural passes, then may composite again.
- Its outputs are already close to a reusable typed-output model.
- The current implementation has strong behavioral coverage but many manual integration points.

Recommended direction:

```text
Breakup IDs / topology
  ├─ Region IDs
  ├─ Gap
  ├─ Edge
  └─ Pieces

Breakup form operator
  └─ consumes the topology and writes height / normal / AO / roughness
```

Breakup should not remain one opaque effect family forever. Preserve its serialized facade while
splitting its compiled graph nodes later.

---

## 3. Audit: Strata Carver generator

Primary paths:

- `FMixtormatStrataCarver` in `MixtormatMaterial.h`
- `SMixtormat::BuildStrataCarverControls()`
- generator handling in `MixtormatGpuCompositor.cpp`
- `MixtormatGpuGeneratorPasses.cpp`
- `Shaders/Private/MixtormatStrataCarver.usf`

Findings:

- Strata is structurally a generator: it rewrites input height before masks and compositing.
- It uses a multi-dispatch SDF-like solve and a final height/normal result.
- Generator parameters bypass the migrated contract and shader-annotation system.
- Gather code duplicates defaults, finite guards, bounds, and derived values.
- Generator drivers can appear in the UI but are not consumed by the GPU gather.
- The UI Push range disagrees with runtime clamping.
- Region-ID demand does not recognize generator consumers, so a producer used only by Strata
  can be incorrectly culled.
- There is no focused Strata behavior test coverage.

Recommended generator contract:

```text
Generator input fields
  → solve fields / typed outputs
  → channel contribution
  → layer graph composite
```

A generator descriptor should declare its input channels, output channels, ID dependencies,
mask dependencies, neutral state, and solve stages. It should not be another branch in the
central compositor gather.

---

## 4. Audit: Pattern IDs and ID infrastructure

Primary paths:

- `FMixtormatPatternFilter` in `MixtormatMaterial.h`
- `SMixtormat_Inspector.cpp`
- `MixtormatGpuPatternPasses.cpp`
- `FPatternIdRenderData` and `FPatternIdPassOutput`
- `Shaders/Private/MixtormatPatternIds.usf`
- `MixtormatChildCapabilities.cpp`
- child preview and picking code

Findings:

- Pattern IDs already produce several useful typed fields: IDs, UV, ramp, edge, gap,
  and orientation.
- Producer/consumer resolution currently depends on hardcoded scans and nearest-child rules.
- Demand propagation misses important cases, including Exact ID, Strata ID influence, and
  cross-layer references.
- Pattern still combines topology, legacy UV treatment, relief, edge shading, and AO controls.
- Legacy Pattern UV and new UV From IDs require special arbitration.
- ID producer lists are repeated in scheduling, drivers, previews, and UI.
- Combine IDs are too tightly coupled to authored child order and producer availability.

Recommended ID architecture:

- Compile a typed region graph per layer.
- Give every producer explicit output descriptors.
- Give every consumer explicit input dependencies.
- Resolve dependencies before pass scheduling.
- Represent cross-layer outputs as graph inputs, not implicit scans.
- Make Copy/Instance Output use the same output descriptor as preview and runtime consumption.

Pattern should eventually be an ID producer plus optional separate treatment nodes, while
serialized legacy fields remain behind an adapter.

---

## 5. Audit: Masks and mask components

Primary paths:

- `FMixtormatMaskLayer`, `FMixtormatGeneratedMask`, and related structs in `MixtormatMaterial.h`
- `MixtormatMaskShaping.h`
- `MixtormatGpuMaskPasses.cpp`
- `MixtormatMaskOps.ush`
- `MixtormatChildCapabilities.*`
- published-output resolution in `MixtormatGpuCompositorInternal.h`

What is already good:

- Shared shaping exists in `FMixtormatMaskShaping` and `MixtormatShapeMask`.
- Source resolution is centralized.
- Published outputs have a reusable key and capability concept.
- Scoped ownership is explicit through `ScopeOwnerChildId`.

Findings:

- Adding a mask component still requires many central switch edits.
- Published outputs use inconsistent production phases.
- A published output can be pasted before its producer and silently resolve to zero.
- Same-layer Worn output cannot feed the ordinary mask loop because it is produced later.
- Blur and curvature are flattened into separate phases, so authored interleaving is lost.
- Generated Mask defaults to Multiply while first-chain initialization behaves like zero,
  producing a likely first-mask identity bug.
- Shared shaping sanitation differs between mask families.
- Output names are repeated as raw `FName` literals across capabilities, publishing,
  preview, clipboard, and tests.

Recommended mask architecture:

```text
Mask source / generator output
  → shaping
  → optional ordered filters
  → typed mask contribution
  → channel or coverage blend
  → published outputs
```

Mask components should be reusable graph nodes. Their outputs should be copyable and instancable
without producer-specific compositor branches.

---

## 6. Parameter-system implications

The parameter refactor remains necessary, but it should support the layer graph rather than
become another registration system.

Principles:

- Reflection remains the catalog for authored identity, type, defaults, and UI metadata.
- Shader contracts remain the source of hard safety facts.
- UI ranges never clamp authored values.
- Generator, ID, mask, and effect owners must use the same addressing and contract path.
- Shader-facing metadata should be generated or discovered from the node descriptor/schema,
  not manually registered in several modules.
- A parameter should be declared once and automatically become available to gather, UI,
  authoring, contracts, and shader binding validation.
- Remapping, normalization, and clamping must remain explicit and editable by developers.
- Safety sanitization should only protect non-finite values, divisors, array bounds, and
  genuine shader invariants.

The next parameter phase should extend the current migrated Effect path to Generator, ID, and
Mask owners only after their graph descriptors and channel contracts are defined.

---

## 7. S2 handoff status

S2 is complete:

- `MixtormatGpuComposePipeline.cpp` owns render-thread graph orchestration.
- `MixtormatGpuCompositor.cpp` retains shader class definitions and shader-bound dispatch code.
- Resource lifetime, target ownership, pass order, ping-pong parity, and event names were preserved.
- Peeling gather behavior was preserved.
- Public entry points and external APIs were unchanged.
- The user reports the plugin build succeeded after the S2 syntax fix.

S2 is a structural foundation only. It does not implement the layer graph architecture.

---

## 8. Recommended next work

1. Freeze serialized compatibility requirements.
2. Define typed channel contracts and neutral/identity behavior.
3. Define typed published-output descriptors and dependency edges.
4. Design the per-layer graph compiler and validation errors.
5. Split Breakup into topology/ID and form/operator concepts.
6. Rework Combine IDs around explicit graph dependencies.
7. Fix generator and ID demand propagation through compiled dependencies.
8. Make mask filters ordered graph nodes with reusable output references.
9. Extend the parameter system through descriptors, not central switches.
10. Only then split remaining pass and inspector files around those boundaries.

Do not start S3–S5 as mechanical decomposition until the graph contracts are agreed.
