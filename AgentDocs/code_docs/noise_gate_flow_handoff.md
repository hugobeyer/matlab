# Noise gates, reusable mask sources, and generator flow availability

Date: 2026-10-08. Status: implementation plus targeted source review, **not build/runtime/visual validation**. No tests, diagnostics, commands, builds, GPU captures, or image reads were performed. This is the handoff for subsequent UI agents.

## 1. What was implemented

### Inline Noise Mask / Noise Gate

- Mask source enum appends `EMixtormatMaskSource::Noise = 2`; existing values remain unchanged.
- `FMixtormatMaskLayer::Noise` embeds the same `FMixtormatNoise` settings used by generator Noise.
- `UsesNoise()` respects existing published-source precedence: an explicit published source wins.
- Layer/group add-mask menus offer `Noise Mask`.
- Right-click a supported mask owner, including Gravity Flow, exposes `Noise Gate`.
- The created child is an ordinary **Mask**, with `ScopeOwnerChildId` pointing to the actual owner, not an extra height generator.

```text
Rock Formation
  Gravity Flow
    Noise Gate          actual owned Mask child, Source = Noise
```

It gates this tool through the existing scoped-mask chain. It does not modify the whole layer unless authored as a layer-wide mask.

### Reuse an existing Noise Value

Every ordinary Mask, whether a layer mask or scoped gate, can choose `Source → Noise Value from…` and bind to an existing eligible Noise generator. This changes only the source edge; it preserves mask identity, scope, weight, blend, shaping, placement, and filter children.

Existing Noise `Value` output copying remains typed. Gate consumption converts copied scalar-field references into ordinary published-source Mask payloads; it does not reinterpret every typed reference as a Mask or replace other field consumers.

The chooser uses stable addresses, lists eligible sources first, disables unavailable choices with a reason, and revalidates when clicked. Shared-group masks are checked across effective member projections. Instances remain source-owned: break the mask instance to replace its source.

Explicit Texture / Layer Values / inline Noise selection clears published source GUIDs and output name. Merely setting `Source` would otherwise leave a published edge in control. Existing Noise settings and old texture settings are retained when switching source kinds.

### Coverage conversion, not raw-field destruction

Signed Noise Value maps to `saturate(0.5 * Value + 0.5)`; unsigned families use `saturate(Value)`. Nonfinite inputs become zero. The conversion writes a distinct R32_FLOAT texture.

Raw typed Value, Gradient, completed Height and Flow semantics remain unchanged. The mask resolver reads typed Value before legacy scalar aliases, so a raw signed alias cannot bypass coverage conversion.

Inline noise uses the existing Noise shader algorithm with identity source placement. Ordinary mask placement/shaping/normalization/blur/curvature/blending then apply through the existing mask pipeline. Mask placement controls stay visible for inline and published noise; only Layer Values keeps its existing placement exclusion.

Noise masks do not run height-combine passes. Their value-only shader permutation does not allocate Height, Gradient, or IDs. Generator Noise retains its full-output path.

### Parameter identity

`EMixtormatParameterOwnerType::MaskNoise = 25` was appended without renumbering existing owners. Nested Noise controls carry real owner/child GUIDs and resolve to `Child.Mask.Noise` through canonical parameter binding/introspection. They are not metadata-only invalid Generator addresses.

Generator Noise was also added to the existing generator owner/struct/default lookup so both reusable control paths use reflected metadata properly. Whole-mask instance settings remain inherited; existing local mask blend/invert override behavior is preserved.

### Flow tools on every current generator

`MixtormatCanOwnGeneratorFlow` now includes all six current types:

- Strata Carver
- Rock Formation
- Pebbles
- Cracks
- Cliff Strata
- Noise

They expose Shape Deform, Generator Flow, Gravity Flow and Flow Carve through the shared RMB menu. The scope is generators, not arbitrary material/fill layers.

Cliff already enters the common generator-flow GPU path with Height, Coverage, Region IDs and named companion fields. Enabling its ownership is not a new Cliff-only shader implementation: the existing common tool pass transforms height/coverage and remaps declared companions.

Noise and Cliff Strata have no trusted signed boundary texture. `MixtormatGeneratorHasFlowBoundary` encodes that field capability separately from whether they can own the tools. New tools under those owners start in Height mode. Signed Distance stays visible but unavailable, and the inspector explains the missing field; boundary-only deflection/shape-offset controls remain disabled. This is a real data-contract limitation, not a reason to hide deformation tools.

## 2. Exact code owners

Paths are relative to the repository root; `Source/` module prefixes are explicit below.

| File | Responsibility |
|---|---|
| `Source/MixtormatRuntime/Public/MixtormatMaskTypes.h` | Appended Noise source, embedded settings, precedence |
| `Source/MixtormatRuntime/Public/MixtormatGeneratorTypes.h` | All-generator flow ownership; separate boundary capability |
| `Source/MixtormatRuntime/Public/MixtormatOutputReference.h` and `Private/MixtormatOutputReference.cpp` | `ResolvePublishedMaskSource`: completed earlier Noise Value routing |
| `Source/MixtormatRuntime/Public/MixtormatParameterTypes.h` | Appended MaskNoise owner identity |
| `Source/MixtormatRuntime/Public/MixtormatParameterBinding.h` and `Private/MixtormatParameterBinding.cpp` | Nested-owner mapping/introspection and Generator Noise support |
| `Source/MixtormatRuntime/Private/MixtormatParameterDefinition.cpp` | Reflected defaults for both Noise owner paths |
| `Source/MixtormatShaders/Private/Compositing/MixtormatMaskGather.cpp` | Asset-free inline Noise gather and canonical published source resolution |
| `Source/MixtormatShaders/Private/Compositing/MixtormatNoiseRender.h` | Shared sanitized Noise render data |
| `Source/MixtormatShaders/Private/Compositing/MixtormatGeneratorGather.cpp` | Reuses shared Noise settings resolution |
| `Source/MixtormatShaders/Private/MixtormatGpuNoisePasses.h` and `.cpp` | Shared field dispatch, value-only path, distinct coverage conversion |
| `Shaders/Private/MixtormatNoise.usf` | Value-only permutation and CoverageCS entry; existing algorithms retained |
| `Source/MixtormatShaders/Private/MixtormatGpuCompositorInternal.h` | Inline render payload and graph-local `NoiseMaskSources` |
| `Source/MixtormatShaders/Private/MixtormatGpuMaskPasses.cpp` | Inline/published source resolution before normal mask shaping |
| `Source/MixtormatShaders/Private/MixtormatGpuComposePipeline.cpp` | Registers typed Value demand before prefix reuse |
| `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatMaskSources.cpp` | New addressed Noise gate/source chooser and source commits |
| `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerActions.cpp` | NoiseMask creation/defaults/history; Height defaults for boundary-less flow owners |
| `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerChildren.cpp` | Noise source names and effective-position eligibility checks |
| `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerClipboard.cpp` | Typed Value reuse as a gating-mask payload |
| `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerMenus.cpp` | Noise Gate, Noise Mask and source action entry points |
| `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerHierarchy.cpp` | Mask source menu routing / presentation |
| `Source/MixtormatEditor/Private/UI/Layers/MixtormatLayerBadges.cpp` | Noise source badge |
| `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorGenerators.cpp` | Shared Noise pattern/placement builder and field-capability-aware flow controls |
| `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorMasks.cpp` | Inline controls, addressed source picker, preserved mask placement |
| `Source/MixtormatEditor/Private/Widgets/SMixtormat_Parameters.cpp` | Actual nested parameter addresses/introspection |
| `Source/MixtormatEditor/Private/Widgets/SMixtormat.h` and `SMixtormatInternal.h` | Declarations and source labels |

The recursive reflected composition hasher already traverses embedded Noise settings; no separate duplicate hash recipe was introduced. Converted Noise coverage cache is graph-local and must not retain RDG pointers across compose requests. Typed publication/demand remains the existing source for prefix restore.

## 3. New entry points

Editor:

- `CanCreateNoiseGate` / `CreateNoiseGate`
- `BuildMaskSourceMenuFor`
- `BuildMaskNoiseValueMenu`
- `CanSelectMaskNoiseValue` / `SelectMaskNoiseValue`
- `SelectMaskSource` / `GetMaskSourceLabel`
- `ResolveGatingMaskPayload`
- `BuildNoisePatternPlacementControls`

GPU:

- `ResolveNoiseRenderData`
- `AddNoiseMaskPass`
- `AddNoiseCoveragePass`

Runtime:

- `ResolvePublishedMaskSource`
- `MixtormatGeneratorHasFlowBoundary`
- `MixtormatParameterBinding::GetChildOwnerData`

Use these existing implementation symbols. Do not rebuild another Noise algorithm, default table, address format, or mask-source menu for the later relationship UI.

## 4. Scoped UI availability review: findings and reasons

This is a targeted generator/flow/mask source review, not a complete audit of all Mixtormat UI.

| Gate / missing action | Finding | Why / next action |
|---|---|---|
| Cliff flow tools missing | Stale shared ownership restriction; removed in this work | Common GPU path already accepts its height/coverage/companions |
| Noise masks missing | Creation/source plumbing was incomplete; implemented | Noise was already a reusable typed field producer, not inherently height-only |
| Signed Distance on Noise/Cliff | Keep disabled with explanation | Neither publishes a valid signed boundary; do not manufacture one from coverage |
| Gravity Seed Threshold | Disabled by design | Gravity retains motion on flat texels and bypasses seeded-flow selection; see flow inspector's `!bGravity` gate |
| Height Push on non-Strata targets | Canonical unsupported target | Only Strata has the bedding-shift consumer; later UI must show the reason instead of pretending support |
| Structural module group/scoped creation | Canonical/editor authoring restriction | Current ordered target/reference provenance is not generalized to shared groups; do not silently enable only the menu |
| Instance endpoint/source edits | Source-owned settings | Breaking the instance enables local editing; do not bypass inheritance |
| Noise Height Scale/Normalize in mask panel | Correctly absent | Mask consumes raw Value coverage; those controls affect generator Height, not Value |
| Mask placement for inline Noise | Must stay visible; corrected during review | It still affects sampled coverage, including values preserved from a former source |
| Output menu absent when no copyable outputs | Deliberate current capability gate | No output action exists there; later UI should explain unavailability if displaying a placeholder |
| Flow Influence/Validity or FlowCarve CarveMask copying | Preview capability alone is insufficient | Current descriptors mark these noncopyable and the flow pass previews them; they are not reusable scalar publication just because a texture exists |
| Cluster IDs creation | Hard-blocked with only “remains unavailable” in creator | No concrete technical rationale established in this review. Investigate; do not treat the comment as sufficient product policy or blindly enable it |
| Generator add menu versus shared-group creator | Availability mismatch to resolve | Menu uses `CanCreateChild`, while creator additionally requires `CanAddGeneratorModule` (layer-owned, unscoped Generator layer). A clickable no-op is not acceptable explanation |
| Strata RampShape | Retired serialized-only setting | Its shader algorithm was replaced; restoring the slider alone restores nothing. This is a historical compatibility decision, not a generic UI hide |

All genuine restrictions need specific disabled-state text/tooltips in the future UI. Unsupported publication/features need implementation before enabling; unexplained restrictions need an audit, not permanent hiding.

No broad feature enablement, Cluster-ID redesign, group structural authoring, legacy Strata restoration, or unrelated GPU algorithm change was made on the basis of this review.

## 5. Cross-plan status

`generator_relationship_ux_plan.md` remains a plan for Structural Warp / Height Push target-first creation and target-owned display. It is **not completed** by the Noise source picker.

Still proposed:

- Connected Structural Warp / Push atomic proposal API.
- Shared structural candidate model and display projection files.
- Projected connection rows under receiving generators.
- Generator-local collapse.
- LayerConnections style metrics/schema/readers.

Integrate this delivered mask workflow into that UI plan:

1. Keep Noise Gate in the actual owned subtree of its flow tool.
2. Show an inline Noise mask as locally owned settings, not a incoming external field link.
3. Show a published Noise Value source as explicit value routing; don't imply adjacency, copies or instances connect fields.
4. Preserve source menus, masks' local shaping/filters/placement, parameter drivers and copy/paste consumption.
5. Keep all-generator flow ownership; use field capabilities for mode-specific disabling.
6. Retain invalid/dangling references for repair; never auto-rebind by name.
7. Reuse current style primitives; these new mask controls did not add a separate palette/token system.

`AgentDocs/GENERATOR_SHADER_IMPROVEMENT_PLAN.md` is **partially implemented**, not wholly done. Its current source-status table separates existing Strata/Erosion changes from unimplemented Cracks/Noise guards, Cliff sweep/endpoint concerns, Rock prebuild, and future visual simulations. Its historical opt-in-vs-Strata-replacement conflict remains explicit. No measurements or numeric equivalence claims were added.

## 6. Rules for the next agent

Read `AGENTS.md`, `UI.md`, `GENERATORS.md`, `COMPOSITION.md`, this handoff, and the targeted source symbols. No concept image reads are needed.

Do not hide a supported action just to simplify presentation. If disabled, identify the exact missing field, source/order prerequisite, ownership restriction, unsupported publication, or retired reader. Keep full reasons accessible when compact labels truncate.

Do not invent boundary fields, treat Vector2 as Flow, force raw signed Noise into coverage globally, project display rows into real ownership, or copy incoming relationships with target-owned subtrees.

No tests, diagnostics, commands, builds, GPU captures, or automated validation are authorized. Implementation and source review do not establish a passing build, shader compilation, runtime, visual quality or performance result.
