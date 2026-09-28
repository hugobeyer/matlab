I’ll read the two architecture docs and the compositor sources first, then inspect only the files those docs reference. I’ll return a proposal with decisions, open questions, and staged migration phases—without editing or running commands.

The handoff confirms S2 is structural only and explicitly preserves peeling gather behavior. I’m now reading the parameter decisions and the existing gather/pass data shapes that the proposal must preserve or replace.

The current design has two important constraints: nearest-earlier region lookup is still implicit, and published outputs are already shared by preview/copy/runtime only at the editor capability layer. I’m checking the exact scheduling and serialization boundaries now so the proposal can replace those mechanisms without changing asset meaning.

# Mixtormat Layer-Local Typed Graph Proposal

## Architectural baseline

- The artist-facing model remains an ordered layer stack.
- Each layer compiles into a transient typed graph.
- Serialized assets remain layer/child based.
- `SourceChildIndex` remains render-time only.
- `ChildId` remains the persistent identity.
- Peeling gather and behavior remain unchanged until explicitly migrated.
- S3, S4, and S5 remain deferred.

The current implementation already provides useful seams:

- `MixtormatEffectGather` is the pattern for family-local gathering.
- `FMixtormatLayerPassContext` contains the correct per-layer lifetime boundary.
- Breakup already produces field, IDs, gap, edge, and pieces separately.
- `ResolveMaskSourceTexture` is the correct shared source-resolution choke point.
- `FMixtormatChildCapabilities` is the beginning of a shared output descriptor system.
- `FindRegionIdsAbove` and the reverse scans in Combine ID are the main implicit-dependency mechanisms to remove.

---

# 1. Node and output contracts

## 1.1 Node contract

Every compiled node should have a descriptor containing:

| Area | Responsibility |
|---|---|
| Identity | Stable type key, version, instance identity |
| Family | Form, ID, mask, effect, composite |
| Inputs | Named typed ports, required/optional status |
| Outputs | Named typed outputs and publication metadata |
| Parameters | Reflection owner/category, not a manually listed parameter set |
| Neutral state | Explicit pass-through or identity behavior |
| Scheduling | Required execution domain and ordering constraints |
| Gating | How coverage or masks control the node |
| Compatibility | Legacy serialized payload adapter |
| Diagnostics | Missing-input and invalid-configuration behavior |

The descriptor should describe what the node means, not how its RDG passes are implemented.

## 1.2 Typed output contract

Outputs should not be represented as generic textures plus informal conventions.

Each output descriptor should specify:

- Semantic type.
- Storage type and format.
- Coordinate space.
- Sampling/filtering rules.
- Resolution requirements.
- Validity or gap behavior.
- Neutral value.
- Lifetime requirements.
- Whether it is previewable.
- Whether it is copyable or instancable.
- Whether it can be consumed by a given port type.
- Provenance: node instance, layer, and output identity.

Recommended semantic output families:

| Type | Examples |
|---|---|
| Coverage | Mask, gap, pieces, wear |
| Region IDs | Pattern regions, cluster regions, breakup pieces |
| Region metadata | Edge, gap, orientation, region UV, validity |
| Scalar field | Height, AO, roughness, distance, ramp |
| Vector field | Normal, flow, orientation |
| Color field | Base color, tint, albedo |
| Surface channel | Height, normal, AO, roughness, metallic, F0 |
| Surface snapshot | Composed surface before or after a graph boundary |
| Coordinate field | UV remap, center UV, transformed sampling basis |

Semantic types should remain distinct even when they use the same GPU format.

For example:

- A `RegionIds` output is not a scalar mask.
- A `Gap` output is coverage.
- A `Height` output is not interchangeable with an arbitrary scalar field.
- Region IDs must use point/nearest sampling and cannot be linearly filtered.
- Normal outputs require normal-specific composition rules.

## 1.3 Port compatibility

A consumer must declare the exact type it accepts.

Conversions should be explicit graph nodes:

- Region IDs → selected coverage through an ID-selection node.
- Height → AO through a derived-normal/AO node.
- Scalar field → mask only through an explicit scalar-to-coverage conversion.
- Region IDs → debug visualization through a preview conversion.

No consumer should silently reinterpret an incompatible output.

---

# 2. Per-layer dependency compilation

## 2.1 Compiled graph model

The compiler should build an immutable graph for each layer:

1. Resolve serialized children and instances.
2. Create ordinary and virtual nodes.
3. Resolve persistent references.
4. Add implicit layer inputs.
5. Validate port types and scopes.
6. Perform output-demand analysis.
7. Topologically order the required nodes.
8. Generate an execution schedule.
9. Produce immutable render data for the render thread.

The graph should be derived on every compose request or cached by authored graph identity and relevant layer inputs.

## 2.2 Explicit dependency edges

New graph dependencies should use:

- Layer identity.
- Persistent child/node identity.
- Stable output identity.

They should not use:

- Nearest earlier producer.
- Child array position.
- Producer type scans.
- Raw output-name comparisons spread across modules.
- Producer availability inferred from whether a pass happened to run.

The existing authored order remains meaningful, but only through explicit compatibility rules.

## 2.3 Legacy dependency adapter

For existing assets, the adapter translates old behavior into explicit edges:

- A mask’s `PublishedSourceLayerId`, `PublishedSourceChildId`, and output name become an output reference.
- `ScopeOwnerChildId` becomes an explicit scope/gate relationship.
- A legacy ID consumer without a named source receives an edge to the producer selected by the current authored-order rule.
- Existing nearest-producer behavior is compiled once, rather than rediscovered independently by each pass.
- The resulting edge is recorded in diagnostics as a legacy implicit dependency.

This preserves current assets while ensuring the runtime scheduler only consumes explicit graph edges.

## 2.4 Demand propagation

Demand analysis should begin from:

- Final surface channels.
- Debug previews.
- Region-ID picking.
- Published outputs.
- Copy/instance references.
- Scalar drivers.
- Explicit downstream node inputs.

Demand then propagates backward through typed inputs.

This replaces the current collection of special cases that demand Pattern, Breakup, Cluster, Driver, and Combine outputs independently.

A node may be skipped only when:

- It has no demanded output.
- Its outputs are not required for a demanded downstream node.
- Its neutral state is correctly represented.

A demanded output must never disappear because its producer was not recognized by a separate scan.

## 2.5 Execution domains

The graph should express execution domains rather than relying on the central compositor’s family switches.

Suggested domains:

1. **Layer input**
   - Resolve source textures, UV transforms, and layer values.

2. **Topology and ID**
   - Produce region IDs, gap, edge, breakup topology, and related fields.

3. **Form pre-composite**
   - Modify layer input fields before masks and surface compositing.
   - Strata Carver remains here.

4. **Mask**
   - Resolve, shape, filter, and combine coverage.

5. **Surface composite**
   - Combine typed surface channels.

6. **Form post-composite**
   - Apply effects requiring the accumulated surface.
   - Breakup relief, erosion, worn edges, and similar operations belong here.

7. **Publication and preview**
   - Expose already-produced typed outputs without creating separate producer logic.

A node may have multiple execution stages, but those stages must be represented as separate compiled operations or virtual nodes.

---

# 3. Channel compositing contracts

## 3.1 Separate channel domains

Compositing rules should be defined by channel domain, not by a single universal blend enum.

Recommended domains:

- Color.
- Normal.
- Height.
- Coverage.
- AO.
- Roughness.
- Metallic.
- F0.
- Packed RAM compatibility output.

The existing separation between mask blend modes and color blend modes is correct and should be expanded.

## 3.2 Explicit blend rule

Every surface contribution should declare:

- Target channel.
- Blend operation.
- Contribution weight.
- Coverage/gate input.
- Neutral behavior.
- Whether the operation is order-dependent.
- Whether it requires a previous surface snapshot.

Examples:

- Base color: normal, multiply, screen, HSL operations.
- Normal: RNM combine or override.
- Height: additive, replace, min/max, authored height blend.
- AO: multiplicative or explicit replace.
- Roughness: additive, replace, min/max.
- Coverage: replace, add, subtract, multiply, min/max.

A mask blend rule must never be reused for a surface channel merely because both are scalar.

## 3.3 Neutral values

Neutral behavior belongs to the operation, not only the data type.

Examples:

- Normal RNM identity: flat normal.
- Multiplicative coverage identity: white.
- Additive height identity: zero delta.
- Surface pass-through: previous surface unchanged.
- Optional output: explicit absence, not a black texture with undocumented meaning.

This resolves the current ambiguity where generated-mask initialization can disagree with its declared Multiply behavior.

---

## 3.4 Interpolation reference

Review this when defining interpolation and transition behavior for height, AO, masks, and channel blends:

- [Inigo Quilez — interpolation reference](https://iquilezles.org/articles/hwinterpolation/)
- [Inigo Quilez — texture repetition reference](https://iquilezles.org/articles/texturerepetition/)
- [Inigo Quilez — smooth minimum reference](https://iquilezles.org/articles/smin/)
- [Inigo Quilez — fBm SDF reference](https://iquilezles.org/articles/fbmsdf/)

# 4. Breakup split: topology/IDs and form operation

Breakup should compile into two logical nodes sharing the same serialized facade.

## 4.1 Breakup topology node

Inputs:

- Layer-space source.
- Optional explicit upstream region IDs.
- Placement/gate input.
- Breakup topology parameters.

Outputs:

- Breakup field.
- Generated region IDs.
- Gap.
- Edge.
- Pieces.
- Optional topology validity metadata.

This node runs before the mask and surface consumers that need its outputs.

It publishes outputs even when the form amount is zero if those outputs are demanded by:

- A mask.
- Worn Edges.
- Combine IDs.
- Preview.
- Copy/instance references.

## 4.2 Breakup form node

Inputs:

- Breakup field.
- Breakup region IDs.
- Gap/edge/pieces where needed.
- A surface snapshot or current composed height.
- Explicit effect gate.

Outputs:

- Height delta or resulting height.
- Normal contribution.
- AO contribution.
- Roughness contribution.
- Optional coverage contribution.

This node runs at the appropriate post-composite boundary.

## 4.3 Compatibility behavior

The legacy Breakup child remains serialized as one child.

The compatibility adapter maps it to:

- One virtual topology node.
- One virtual form node.
- One shared published-output identity.

This preserves:

- Existing Breakup parameters.
- Existing child IDs.
- Existing mask ownership.
- Existing copy/instance references.
- Existing authored order.

Worn Edges should consume Breakup’s `Edge` or `RegionIds` output through an explicit edge rather than relying on a later pass seeing the correct pending array.

---

# 5. Replacing fragile Combine ID behavior

## 5.1 Explicit input

Combine ID should have an explicit `RegionIds` input port.

It must not discover its source by scanning:

- Earlier children.
- The nearest region producer.
- Special Breakup cases.
- Current publication order.

Legacy assets with no explicit source receive an adapter-generated edge using current nearest-producer semantics.

## 5.2 Opaque region identity

Region IDs should be treated as opaque labels.

The graph must prohibit:

- Numeric interpolation.
- Generic min/max.
- Coverage blending.
- Treating ID values as stable spatial coordinates.
- Assuming one producer’s numeric ID namespace is compatible with another producer’s namespace.

A transformed region output receives a new region namespace and provenance.

## 5.3 Combine output

Combine should declare:

- Input region namespace.
- Output region namespace.
- Operation mode.
- Determinism policy.
- Validity/gap behavior.
- Number of solve rounds or equivalent quality parameter.
- Whether the output preserves or intentionally changes region identity.

The current `Amount`, `Seed`, `Passes`, and subtract mode remain serialized parameters. Their exact mathematical behavior should be documented before replacing the shader algorithm.

Until that behavior is documented, the migration should preserve the current implementation behind the new typed contract rather than reinterpret it.

---

# 6. Cluster ID design

Cluster IDs should become a first-class ID producer rather than a special Filter case.

## 6.1 Explicit source port

The current `LayerSurface` versus `CompositeBelow` choice should resolve to an explicit source:

- `LayerSurfaceInput`.
- `CompositeBelowSurface`.

The bottom-layer fallback should be represented as a valid substrate input, not as a hidden special case.

## 6.2 Region output contract

Cluster should produce:

- Opaque region IDs.
- Region validity.
- Optional region statistics or debug metadata.
- Deterministic provenance based on node identity, source identity, and parameters.

IDs should not depend on incidental authored child positions.

## 6.3 Determinism

The cluster solver should define deterministic tie-breaking:

- Canonical region key.
- Stable union/root selection.
- Stable seed hashing.
- No dependence on unordered GPU atomic outcomes.

Display/picking IDs may use a separate presentation mapping, but runtime region identity must remain logically stable.

## 6.4 Consumers

Random ID, HSV From IDs, Ramp From IDs, Color ID, Relief From IDs, and drivers should consume the explicit Cluster output or another explicit region output.

They should not ask for “the nearest available region map.”

---

# 7. Reusable copy and instance output references

## 7.1 Canonical output reference

The internal reference key should be:

- Source layer identity.
- Source node/child identity.
- Stable output identity.

The current `FPublishedMaskKey` using child index and `FName` should become an internal compatibility representation only.

Output labels remain presentation. Output identity must not change when labels are localized or renamed.

## 7.2 Published output registry

The compiler should produce one registry from output descriptors.

The same registry should drive:

- Runtime output publication.
- Inspector output menus.
- Preview selection.
- Region-ID picking.
- Copy Output.
- Instance Output.
- Clipboard validation.
- Dependency validation.

This replaces the current split between `FMixtormatChildCapabilities`, runtime publication maps, and raw output-name literals.

## 7.3 Type-safe reuse

A copied or instanced output must declare its expected type.

Examples:

- A `Gap` output can feed a coverage mask.
- A `RegionIds` output cannot directly feed a coverage mask.
- A `RegionIds` output can feed an explicit ID-selection node.
- A `Normal` output cannot be used as a height input without a conversion node.

## 7.4 Instance semantics

Instances should reuse source graph content while preserving placement-local data:

- Local mask blend mode.
- Local shaping and inversion.
- Local gate.
- Local UV placement.
- Local output selection.

The source node’s identity, parameters, and output descriptors remain shared.

Instance cycles must be diagnosed during graph compilation.

---

# 8. Uniform mask and effect gating

## 8.1 Separate gate from operation

Every mask-capable node should expose a typed coverage input called its gate.

The gate is separate from:

- Node enable state.
- Effect amount.
- Mask shaping.
- Surface blend mode.
- Placement texture.

The standard effect evaluation model becomes:

1. Resolve source channels.
2. Resolve gate coverage.
3. Generate the effect candidate.
4. Apply the candidate to the source using the declared channel rule.
5. Interpolate or blend using the gate and authored amount.

## 8.2 Gate precedence

For new graph nodes:

1. Explicit node gate reference.
2. Explicit scoped mask graph.
3. Explicit placement mask.
4. Layer coverage, if the node declares layer-gated behavior.
5. White identity coverage.

The descriptor declares which policies are allowed.

This removes per-family decisions such as whether a placement mask yields to a scoped mask or whether a generator starts from the layer mask.

## 8.3 Scope behavior

Scoped masks become subgraphs owned by a node:

- They start from the declared scope identity.
- They cannot accidentally feed back into the ordinary layer mask chain.
- Their output is a normal typed coverage output.
- Blur and curvature remain ordered filter nodes.
- Scope ownership uses stable node identity, never array position.

Legacy Strata and peeling scope behavior should be represented through compatibility policies so current behavior is preserved.

## 8.4 Mask chain initialization

The compiler must explicitly initialize a mask chain according to its operation:

- First `Multiply` operation starts from white.
- First `Replace` operation uses its declared replace semantics.
- Empty mask graph resolves to white coverage.
- Missing optional mask resolves to white.
- Missing required published output produces a diagnostic and uses a documented compatibility fallback.

No node should infer initialization from whether it happens to be the first pass dispatched.

---

# 9. Parameter discovery without manual registration lists

The existing parameter decisions should remain authoritative.

## 9.1 Reflection owns authored facts

Reflection remains the source for:

- Property identity.
- Value type.
- Serialized field.
- CDO default.
- Category/family.
- UI metadata.
- Tooltip/documentation metadata.

The canonical definition identity remains:

- Owner.
- Property name.
- Value type.

A graph instance adds the node/child address around that definition. It does not replace the definition identity.

## 9.2 Descriptors own graph facts

Node descriptors own:

- Input/output ports.
- Output semantics.
- Neutral state.
- Scheduling constraints.
- Gate policy.
- Compatibility mapping.
- Which reflected parameter owner/category applies.

Descriptors must not manually enumerate every parameter.

## 9.3 Shader contracts own safety facts

Shader contracts or `@param` annotations own:

- Hard bounds.
- Finite-value policy.
- Normalization scale.
- Saturation requirements.
- Divisor floors.
- Array capacities.
- Shader-specific invariants.

UI ranges remain advisory.

## 9.4 Gather conversion

A family gatherer should:

- Discover authored values through the typed binding layer.
- Sanitize through the shared contract system.
- Compute derived values locally.
- Convert to the node’s typed GPU parameter block.
- Never reintroduce per-family literal defaults or separate registration lists.

This should apply equally to:

- Effects.
- Generators.
- IDs.
- Masks.
- Mask filters.

The graph descriptor identifies the owner; reflection supplies the parameter set.

---

# 10. Reflection, shader contracts, and descriptors

These three systems should have non-overlapping responsibilities:

| System | Owns |
|---|---|
| Reflection | Authored schema and serialization |
| Node descriptor | Graph semantics and dependency contracts |
| Shader contract | GPU safety and binding invariants |
| Gather converter | Typed runtime conversion |
| Compiler | Dependency resolution and scheduling |
| UI | Presentation and authoring |

The validation relationship should be:

1. Reflection discovers the authored properties.
2. The descriptor declares which graph node owns them.
3. Shader annotations/contracts validate GPU-facing assumptions.
4. Completeness tests ensure every authored shader-facing value has a valid path.
5. The compiler rejects missing ports, incompatible outputs, and invalid references.

No central “all node parameters” registration list should be introduced.

---

# Open questions

These should be resolved before implementing the graph compiler.

1. **Combine subtract semantics**
   - What exact region operation does the current subtract mode represent?
   - Is it union removal, boundary subtraction, or a field-based split?

2. **Region ID stability**
   - Must IDs remain stable across resolution changes?
   - Or only stable for a fixed resolution and graph configuration?

3. **Forward cross-layer references**
   - Should references to later layers be rejected?
   - Or should the compiler support delayed stack snapshots?

4. **Persistent graph serialization**
   - Should compiled graphs remain transient initially?
   - Recommendation: yes; serialize only authored layer data and explicit references.

5. **Surface snapshot granularity**
   - Should post-composite effects consume the whole surface bundle?
   - Or only the channels declared by their input contract?

6. **Legacy implicit references**
   - Should they remain permanently supported?
   - Recommendation: support them for existing assets, but emit diagnostics and prevent new implicit references.

7. **Output descriptor ownership**
   - Should descriptors be C++ family descriptors, generated metadata, or a combination?
   - Recommendation: graph/output metadata may be descriptor-defined; parameter lists remain reflection-discovered.

---

# Staged migration plan

## Phase A — Freeze compatibility rules

Document and test:

- Append-only enum requirements.
- Child ID and source reference behavior.
- Scope ownership behavior.
- Published output aliases.
- Current mask initialization.
- Current nearest-region behavior.
- Peeling gather and pass behavior.
- Breakup zero-amount publication behavior.

No runtime architecture change yet.

## Phase B — Define contracts and diagnostics

Define:

- Typed channel taxonomy.
- Output descriptor schema.
- Node descriptor schema.
- Gate policies.
- Neutral values.
- Blend rules.
- Graph diagnostics.
- Legacy output-name aliases.

This phase must complete before mechanical file decomposition.

## Phase C — Shadow graph compilation

Compile a graph from existing serialized layers, but continue using the current render schedule.

Compare:

- Required outputs.
- Producer demand.
- Region dependencies.
- Published output availability.
- Mask scopes.
- Generator ordering.
- Post-composite effect ordering.

Any mismatch is reported without changing pixels.

## Phase D — Replace internal references

Introduce the compiled reference registry:

- Resolve published masks through typed output references.
- Resolve region consumers through explicit graph edges.
- Retain legacy adapters for old serialized references.
- Keep `FPublishedMaskKey` and child-index data only at the compatibility boundary.

This is the first phase that removes implicit runtime lookup.

## Phase E — Migrate ID producers and consumers

Migrate in this order:

1. Pattern IDs.
2. Cluster IDs.
3. Breakup topology outputs.
4. Combine IDs.
5. ID consumers such as Random, HSV, Ramp, Relief, and drivers.

Each producer should be validated through:

- Output demand tests.
- Missing-input diagnostics.
- Deterministic ID tests.
- Preview/copy/runtime descriptor tests.

## Phase F — Split Breakup semantically

Keep the serialized Breakup facade while compiling:

- Breakup topology/IDs.
- Breakup form operation.

Verify:

- Zero-amount ID publication.
- Gap/edge/pieces reuse.
- Worn Edges ordering.
- Height/normal/AO/roughness results.
- Scoped mask behavior.
- Existing Breakup previews.

## Phase G — Migrate masks and effects to shared gates

Move mask filters, generated masks, and effects to:

- Explicit coverage gates.
- Ordered mask subgraphs.
- Shared output references.
- Typed surface contributions.
- Descriptor-driven neutral behavior.

Peeling remains preserved until it receives its own explicit migration decision.

## Phase H — Extend parameter discovery

After graph contracts are stable:

- Apply the shared owner/binding path to generators.
- Apply it to ID nodes.
- Apply it to masks and mask filters.
- Add shader annotations/contracts.
- Remove remaining duplicate gather defaults and bounds.

## Phase I — Runtime cutover

Enable graph scheduling per family behind a controlled compatibility switch.

For each family:

- Compare old and graph schedules.
- Compare output demand.
- Compare in-range rendered results.
- Compare preview and copy outputs.
- Verify serialized assets without upgrades.

Only after all families are validated should the old central family scans be removed.

## Deferred work

The file decomposition items S3, S4, and S5 should not begin as standalone work.

They should occur only after:

- Typed contracts are agreed.
- Shadow compilation matches current behavior.
- Breakup and ID dependencies are explicit.
- Peeling preservation is verified.
- The graph scheduler has a stable ownership boundary.

This keeps the next step architectural rather than mechanical.