# Mixtormat — Behavior System Refactor

## Remaining implementation audit

**Repository:** [hugobeyer/matlab](https://github.com/hugobeyer/matlab)
**Branch:** `feature/behavior-system-v2`
**Latest commit:** `083630c`
**PR:** #5 — Not ready to merge
**Engine:** Unreal Engine 5.8

I reviewed the current branch against the refactor requirements.

**Overall status:** The core Behavior architecture exists, but the migration is incomplete. The largest remaining issues are GPU execution order, Flow outputs, traced Behavior functionality, parameter authoring, and legacy cleanup.

The previous build errors were addressed in `083630c`, but compilation of that commit has not been confirmed.

---

# P0 — Critical functionality

These should be completed before additional UI work.

### 1. Complete Behavior GPU execution order

**Status: Incomplete — critical architectural issue**

File:
`Source/MixtormatShaders/Private/MixtormatGpuGeneratorPasses.cpp`

Currently, execution is split between:

* `AddBehaviorFlowFieldPasses()`
* `ApplyGeneratorPostBehaviors()`

The first function processes Flow Fields and traced operations. The second processes ordinary Warp, Push, Carve, and Deform.

**Problem:** Traced operations execute before ordinary Behaviors, regardless of their authored position.

Required:

* [ ] Implement one ordered Behavior execution path.
* [ ] Execute all operations in their authored order.
* [ ] Flow Fields publish without modifying height.
* [ ] Traced Warp executes at its actual position.
* [ ] Traced Carve executes at its actual position.
* [ ] Traced Deform executes at its actual position.
* [ ] Preserve native-height processing before normalization.
* [ ] Support multiple Behaviors on one generator.
* [ ] Preserve correct previous/current height references.

**Target:** One generator-owned Behavior execution system, not two separate operation schedules.

### 2. Correct traced Deform behavior

**Status: Incorrect GPU semantics**

File:
`MixtormatGpuGeneratorPasses.cpp`

The current traced Flow executor modifies height and coverage and can remap the whole generator bundle.

For traced Deform, this contradicts the established Deform contract.

Required:

| Behavior   | Height   | Coverage | IDs      | Other outputs  |
| ---------- | -------- | -------- | -------- | -------------- |
| Warp       | Remap    | Remap    | Remap    | Remap          |
| Deform     | Remap    | Preserve | Preserve | Preserve       |
| Push       | Add      | Preserve | Preserve | Preserve       |
| Carve      | Modify   | Preserve | Preserve | Preserve       |
| Flow Field | Preserve | Preserve | Preserve | Publish fields |

* [ ] Separate Warp and Deform output handling.
* [ ] Never call whole-bundle remapping for Deform.
* [ ] Ensure traced Carve changes height only.
* [ ] Preserve named masks and generator IDs appropriately.

### 3. Fix Flow Field output publication

**Status: Partially implemented — concrete mismatches found**

Files:

* `MixtormatGpuGeneratorPasses.cpp`
* `MixtormatChildCapabilities.cpp`
* `MixtormatOutputReference.cpp`

The GPU publishes `FlowDirection` and `WarpedUV`.

However, capabilities advertise `WarpedUVGrid`, and several Behavior output descriptions lack explicit published field kinds.

The description defaults to `RegionIds`, which is incorrect for Flow and UV outputs.

Required:

* [ ] `FlowDirection` → typed `Flow`.
* [ ] `WarpedUV` → typed `UVMap`.
* [ ] `Influence` → typed `Scalar01`.
* [ ] `Validity` → typed `Scalar01`.
* [ ] `CarveMask` → typed `Scalar01`, when applicable.
* [ ] Use `WarpedUVGrid` only as a preview representation, unless explicitly adopted as the canonical output name.
* [ ] Ensure advertised outputs are actually published.
* [ ] Ensure Copy Output creates references with the correct types.
* [ ] Ensure source selectors discover these outputs.
* [ ] Validate output availability and resolution.

**This is a priority because an incorrectly typed Flow Field cannot reliably feed another Behavior.**

### 4. Finish traced Warp, Deform, and Carve

**Status: GPU implementation exists, but integration is incomplete**

Existing algorithms include:

* Jump-flood propagation
* Boundary direction generation
* Gravity steering
* Field smoothing
* RK2 tracing
* UV displacement
* Height deformation
* Groove/deposit tracing

These algorithms should be retained and integrated, not replaced with simplified approximations.

Required:

* [ ] Proper traced Warp displacement.
* [ ] Proper gravity-driven Warp.
* [ ] Proper boundary Shape Deform migration.
* [ ] Proper traced Carve/Deposit.
* [ ] Respect Behavior Strength.
* [ ] Respect scalar Strength drivers.
* [ ] Respect Influence inputs.
* [ ] Respect scoped masks.
* [ ] Respect generator coverage.
* [ ] Preserve negative Strength semantics.
* [ ] Preserve zero-strength identity.
* [ ] Preserve tile-aware tracing.

**Confirmed issue:** The traced executor uses Flow parameters but does not apply the normal Behavior Strength, Influence, and scalar-driver contracts consistently.

### 5. Finish Flow Field producer/consumer behavior

**Status: Partial**

A Flow Field should be usable independently of whether its generator is being warped.

Required:

* [ ] Flow Field can publish without deforming the generator.
* [ ] Warp can consume another Flow Field.
* [ ] Deform can consume another Flow Field.
* [ ] Flow Fields support multiple consumers.
* [ ] Valid output demands trigger required producer evaluation.
* [ ] Neutral producers do not disappear when their fields are required.
* [ ] Establish consistent disabled-producer semantics.
* [ ] Prevent feedback loops.
* [ ] Validate missing-source behavior.

The current GPU demand check handles `FlowDirection` and `WarpedUV`, but the full advertised output contract is not integrated.

### 6. Finish source dependency gathering

**Status: Incomplete**

File:
`Compositing/MixtormatSourceGather.cpp`

The existing shelf dependency path explicitly handles Warp, Push, and Deform.

Required:

* [ ] Add complete source-demand traversal for applicable Carve inputs.
* [ ] Include Influence dependencies where supported.
* [ ] Resolve typed Flow and UV inputs.
* [ ] Support compatible Sources shelf outputs.
* [ ] Guarantee producers execute before consumers.
* [ ] Reject invalid source ordering.
* [ ] Prevent cyclic dependencies.
* [ ] Keep source classification consistent between Runtime validation and GPU Gather.

The goal is one typed dependency system shared by all Behaviors.

### 7. Complete pre-generation execution

**Status: Partially implemented**

Existing functionality:

* Pre-generation Warp using published Flow/UV fields.
* Coordinate remapping before generator sampling.

Still required:

* [ ] Validate pre-generation field ordering.
* [ ] Ensure all six generators consume pre-generation coordinates correctly.
* [ ] Ensure coverage, boundary, and IDs remain synchronized.
* [ ] Confirm multi-Warp composition.
* [ ] Confirm tile wrapping.
* [ ] Confirm referenced-field availability.
* [ ] Prevent invalid combinations.

`OwnNativeHeight` currently requires post-generation execution. This is a deliberate restriction in the current implementation, not a missing compiler feature.

---

# P1 — Complete the authoring system

### 8. Finish traced Behavior Inspector controls

**Status: Major missing UI functionality**

File:
`MixtormatInspectorGenerators.cpp`

The Flow Field Inspector has controls for the migrated Flow settings.

However, the dedicated Warp, Deform, and Carve panels do not expose the complete traced-operation configuration.

The `bUseTracedFlow` authoring toggle is not exposed in those panels.

Required:

**Warp**

* [ ] Standard / Traced operation selection.
* [ ] Flow steering mode.
* [ ] Flow source.
* [ ] Trace length and steps.
* [ ] Warp strength.
* [ ] Gravity controls when applicable.

**Deform**

* [ ] Standard / Traced selection.
* [ ] Shape Offset.
* [ ] Bulge / Pinch.
* [ ] Direction controls.
* [ ] Trace controls where applicable.

**Carve**

* [ ] Standard SDF / Traced selection.
* [ ] Groove / Deposit.
* [ ] Depth.
* [ ] Width.
* [ ] Falloff.
* [ ] Trace length and steps.

**All Behaviors**

* [ ] Strength and Influence.
* [ ] Relevant masks and drivers.
* [ ] Proper source selection.
* [ ] Hide unsupported parameters rather than disabling irrelevant controls.

### 9. Improve Flow Field Inspector

**Status: Functional baseline exists**

Current UI exposes many of the Flow solver parameters.

Remaining:

* [ ] Conditional visibility based on Flow/Gravity mode.
* [ ] Conditional visibility based on Height/SDF steering.
* [ ] Separate production controls from operation-specific deformation controls.
* [ ] Expose supported field outputs clearly.
* [ ] Show invalid or unavailable source states.
* [ ] Make defaults consistent with parameter authoring JSON.
* [ ] Ensure copied and instanced Flow Fields display correct values.

The Flow Field Inspector should configure field production; Warp, Deform, and Carve should configure how fields are consumed.

### 10. Finish universal typed source selectors

**Status: Partial**

Required supported inputs:

| Behavior   | Compatible source                         |
| ---------- | ----------------------------------------- |
| Warp       | Flow, UVMap, Own Height Gradient          |
| Push       | ScalarSigned, Own Height, Previous Height |
| Carve      | SDF, Own Boundary                         |
| Deform     | Flow, UVMap, Own Height Gradient          |
| Flow Field | Generator boundary or height steering     |

Required:

* [ ] Correct typed filtering.
* [ ] Sources shelf compatibility.
* [ ] Published generator outputs.
* [ ] Published Behavior outputs.
* [ ] No invalid type conversions.
* [ ] No circular connections.
* [ ] Source-unset authoring.
* [ ] Clear invalid-source indicators.
* [ ] Correct selection after clipboard operations.

### 11. Complete parameter JSON integration

**Status: Incomplete**

File:
`Config/MixtormatParameterAuthoring.json`

Current Behavior definitions cover:

* Strength
* GradientReach
* CarveWidth

The 23 migrated Flow settings are not registered under Behavior Flow in this JSON.

The old `Generator Flow` section still contains Effect parameters.

Required:

* [ ] Move all relevant Flow metadata to Behavior Flow.
* [ ] Remove obsolete Effect Flow definitions.
* [ ] Register correct defaults.
* [ ] Register ranges, steps, and clamp behavior.
* [ ] Register enum parameter metadata where supported.
* [ ] Ensure Inspector uses authoring definitions.
* [ ] Ensure user-edited JSON persists and reloads correctly.

### 12. Complete parameter drivers

**Status: Partial**

Existing scalar-driver infrastructure supports:

* Behavior Strength.
* Gradient Reach.
* Referenced Flow Amount.
* Referenced Flow Trace Length.

However, the supported GPU driver sources and parameters remain limited.

Required:

* [ ] Extend drivers to supported Behavior scalar parameters.
* [ ] Allow appropriate Flow parameters to be driven.
* [ ] Support signed values correctly.
* [ ] Ensure neutral authored values can be activated by drivers.
* [ ] Ensure driven zero evaluates to exact identity.
* [ ] Apply drivers to traced operations.
* [ ] Keep parameter ownership correct.
* [ ] Ensure JSON metadata and driver popovers agree.
* [ ] Validate driver source ordering.
* [ ] Preserve driver bindings through copy/instance/group operations.

---

# P1 — Remove the remaining legacy architecture

This is still a substantial cleanup.

**There is no authored content requiring backward compatibility.** The old systems should be deleted once their functionality is accounted for.

### 13. Remove obsolete Generator Flow Effect architecture

**Status: Partially removed**

The most recent commits removed the old Effect Flow gather function and migrated the GPU parameter carrier.

Remaining code includes:

**Runtime**

`Source/MixtormatRuntime/Public/MixtormatEffect.h`

Still contains:

* Old ShapeDeform effect type.
* Old GeneratorFlow effect type.
* Old GravityFlow effect type.
* Old FlowCarve effect type.
* Old Generator Flow settings in `FMixtormatLayerEffect`.
* `MixtormatIsGeneratorFlowEffect()`.

**Editor**

`MixtormatLayerChildren.cpp`

* `IsGeneratorFlow()`
* `CanOwnGeneratorFlow()`
* Old Effect display names.
* Old generator-flow placement handling.

`MixtormatLayerMenus.cpp`

* Old generator-flow move restrictions.
* Old instance-source filtering.

`MixtormatLayerDragDrop.cpp`

* Old generator-flow drag/drop restrictions.

`MixtormatLayerActions.cpp`

* Old Effect compatibility checks.

Required:

* [ ] Remove obsolete Effect Flow parameters.
* [ ] Remove obsolete Effect Flow types and helpers.
* [ ] Remove obsolete UI handling.
* [ ] Remove old placement rules.
* [ ] Remove old instance restrictions.
* [ ] Replace applicable logic with Behavior ownership rules.
* [ ] Remove unused reflected parameter definitions.

**Exception:** Preserve the unrelated layer-level Flow Warp feature. It is not the generator-owned Flow architecture being replaced.

### 14. Remove obsolete structural architecture

**Status: Largely removed — final audit required**

The previous structural connection implementation files are absent from the current branch.

Remaining checks:

* [ ] No structural-connection execution.
* [ ] No structural target/source edge authoring.
* [ ] No old badges or highlights.
* [ ] No obsolete parameter owners.
* [ ] No obsolete shader registrations.
* [ ] No orphan references to Height Push or Structural Warp.
* [ ] No compatibility-only paths.

The old Height Push and Structural Warp shaders should be removed completely if they have no remaining live users.

### 15. Normalize naming conventions

**Status: Inconsistent**

The new Behavior architecture still contains old names such as:

* `GeneratorFlowAmount`
* `GeneratorFlowTraceLength`
* `GeneratorFlowWarpStrength`
* `GeneratorFlowShapeOffset`
* `GeneratorFlowCarveMode`

Some are now Behavior-owned despite their old names.

Required:

* [ ] Use Flow terminology for flow generation.
* [ ] Use Warp terminology for coordinate displacement.
* [ ] Use Deform terminology for height displacement.
* [ ] Use Carve terminology for relief removal/deposition.
* [ ] Rename obsolete data structures.
* [ ] Rename old GPU helper functions where appropriate.
* [ ] Update parameter metadata and documentation.

Rename shader files only after preserving and verifying their algorithms.

---

# P1 — Ownership and editing

### 16. Complete generator ownership validation

**Status: Partially implemented**

Required:

* [ ] Behavior always belongs to a generator.
* [ ] No orphan Behavior rows.
* [ ] No invalid generator references.
* [ ] No ownership cycles.
* [ ] No cross-generator implicit targeting.
* [ ] Correct deletion behavior.
* [ ] Correct enable/disable propagation.
* [ ] Correct movement restrictions.
* [ ] Correct operation ordering after moving.

### 17. Finish clipboard, groups, and instances

**Status: Implemented in part, not fully verified**

Files:

* `MixtormatLayerClipboard.cpp`
* `MixtormatLayerGroups.cpp`
* `MixtormatLayerDragDrop.cpp`
* `MixtormatParameterBinding.cpp`

Required:

* [ ] Copy individual Behaviors.
* [ ] Copy Behavior subtrees.
* [ ] Paste under another generator.
* [ ] Remap local source references.
* [ ] Preserve valid external references.
* [ ] Reject invalid references.
* [ ] Copy and paste scoped masks.
* [ ] Handle group expansion.
* [ ] Remap group-member references.
* [ ] Correct instance ownership.
* [ ] Preserve parameter-driver bindings.
* [ ] Validate undo/redo.
* [ ] Validate save/load.

An important detail: group expansion already remaps typed Behavior references. That implementation should be finished and verified rather than replaced unnecessarily.

---

# P2 — Final verification

### 18. Validate all generator families

Each Behavior must be checked with the six generator families:

| Generator      | Warp   | Push   | Carve                  | Deform | Flow Field |
| -------------- | ------ | ------ | ---------------------- | ------ | ---------- |
| Rock Formation | Verify | Verify | Verify                 | Verify | Verify     |
| Strata Carver  | Verify | Verify | Verify                 | Verify | Verify     |
| Pebbles        | Verify | Verify | Verify                 | Verify | Verify     |
| Cracks         | Verify | Verify | Verify                 | Verify | Verify     |
| Cliff Strata   | Verify | Verify | Verify supported modes | Verify | Verify     |
| Noise          | Verify | Verify | Verify supported modes | Verify | Verify     |

For generators without a suitable native boundary, boundary-dependent operations must be unavailable or fail closed. They must not invent a distance field.

### 19. Validate GPU and rendering correctness

* [ ] Unreal Engine 5.8 C++ compilation.
* [ ] UHT compilation.
* [ ] Shader compilation.
* [ ] RDG parameter binding.
* [ ] No missing required shader resources.
* [ ] Correct texture formats.
* [ ] Correct field extents.
* [ ] Correct UV coordinate conventions.
* [ ] Tileable output.
* [ ] Signed-height consistency.
* [ ] Correct zero-strength behavior.
* [ ] Correct negative-strength behavior.
* [ ] No unnecessary GPU dispatches.
* [ ] Stable behavior at 1K, 2K, and 4K.
* [ ] Performance profiling.

The earlier `FMixtormatFieldRangeCS::HeightGate` crash path now has an explicit binding in the source, but runtime confirmation is still required.

### 20. Final documentation and merge preparation

**Status: Outdated**

Files:

* `AgentDocs/BEHAVIOR_V2.md`
* `AgentDocs/GENERATORS.md`
* PR #5 description

The Behavior documentation still describes some already-removed Flow Effect paths as live.

It also contains outdated migration descriptions.

Required:

* [ ] Update the architecture to reflect actual source.
* [ ] Remove outdated implementation-status claims.
* [ ] Document Behavior types and stages.
* [ ] Document field publication.
* [ ] Document ordering.
* [ ] Document driver behavior.
* [ ] Document generator compatibility.
* [ ] Remove legacy authoring references.
* [ ] Update PR description.
* [ ] Complete final validation before merging.

---

## Recommended implementation batches

| Batch | Priority | Work                                                              |
| ----- | -------- | ----------------------------------------------------------------- |
| **A** | P0       | Compile latest changes and correct remaining compilation errors   |
| **B** | P0       | Unify GPU execution order and fix traced Deform/Warp/Carve        |
| **C** | P0       | Correct typed Flow outputs, demand, and source consumption        |
| **D** | P1       | Complete traced Behavior Inspectors and source selectors          |
| **E** | P1       | Migrate JSON metadata and complete parameter drivers              |
| **F** | P1       | Delete remaining legacy Effect Flow architecture                  |
| **G** | P1       | Finish group, instance, clipboard, and ownership behavior         |
| **H** | P2       | Shader/runtime verification, documentation, and merge preparation |

### What is already implemented

The following foundations do **not** need to be recreated:

* Generator-owned Behavior data model.
* Warp, Push, Carve, Deform, and Flow Field types.
* Pre-generation published-field Warp.
* Post-generation standard Behavior execution.
* Signed Push and SDF Carve shader paths.
* Flow solver integration.
* Behavior-owned Flow settings.
* Basic typed field inputs.
* Scalar-driver infrastructure.
* Initial Flow Field Inspector.
* Initial source selectors.
* Behavior clipboard and group remapping foundations.
* Removal of old structural-connection implementation files.
* Removal of legacy Generator Flow Effect gathering.

### Critical conclusion

**The main remaining problem is not the amount of legacy code. It is that the new Behavior system does not yet execute every operation through one consistent contract.**

I would finish **B and C first**, followed immediately by **D and F**.

That establishes a single functioning GPU architecture, makes the migrated operations accessible through the Inspector, and eliminates the old authoring system.

No further redesign is required.
