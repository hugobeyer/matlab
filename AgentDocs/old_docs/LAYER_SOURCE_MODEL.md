# Layer source model

Status: **proposal.** Nothing here is implemented. Written to stop the "do we even need
layers?" question from being re-litigated every session.

Read with `AgentDocs/ARCHITECTURE.md` (modules) and `AgentDocs/BOOLEAN_OPS_MODULES.md`
(what the generator chain can produce).

---

## 1. The problem

Content enters Mixtormat in **three** places, and they share no vocabulary:

| Entry point | What it brings | Authored where | Consumed by |
|---|---|---|---|
| Gallery (bottom drawer) | authored materials | elsewhere, as assets | `Material` layers only |
| `Fill` layer | channels inline | in the layer | the layer itself |
| `Generator` layer | a module chain inline | in the layer | the layer itself |

The symptom the user hit: **a Generator layer has no compositing channels and no
substrate lerp.** That is not a missing feature on a layer type — it is a *source* that
was modelled as a layer type and therefore inherited none of the layer compositing
vocabulary.

Concretely, the same idea ("how does this meet what is below it") has two unrelated
enums today:

- `Material` / `Fill` layers → `EMixtormatCompositionMode` = `Replace | Coat`
- `Generator` layers → `FMixtormatHeightBlend::Op` = `Replace | Add | Subtract | Multiply | Min | Max`

Neither is wrong. Having both, on different layer types, is.

---

## 2. What a layer actually is

A layer earns its place for two reasons, and neither is about where content comes from:

1. **Scoping.** Masks, effects, IDs, behaviors and Booleans belong to a layer. Their
   ownership (`ScopeOwnerChildId`), their ordering, and their mask resolution are all
   layer-relative. Remove layers and you must reinvent grouping nodes to hold them.
2. **One composite step.** A layer meets the substrate exactly once, with one opacity and
   one mode. That is what makes "20 layers" tractable instead of "20 unordered nodes".

So: **keep the layer. The redundant thing is `EMixtormatLayerType`, not the layer.**

---

## 3. The proposal — one source row

A layer becomes: **a source + a child stack + one composite step.**

The source is a single row at the top of the layer's inspector:

```text
SOURCE   [ Authored Material | Fill | Generator ]
```

Everything below it is that source's own controls. This is not a new pattern — it is the
pattern the mask panel already uses:

```text
SOURCE   [ Texture | Noise | Layer Values ]     ← BuildLayerMaskControls
```

`SMixtormatSegmentedControl` + `ActiveIndex_Lambda` + `OnChosen`, with the panel below
showing only the selected source's rows. Same control, same idiom, one level up.

### What each source shows

| Source | Rows below the source row |
|---|---|
| `Authored Material` | gallery picker, tiling, UV placement, channel mode, normal source, channel overrides |
| `Fill` | the authored channel overrides (Base Color / Roughness / IOR / Metallic) |
| `Generator` | the module chain (the child stack), plus the generator's own output routing |

`EMixtormatLayerType` collapses into `EMixtormatLayerSource`. The existing per-type
inspector branches (`SMixtormat_Inspector.cpp`: the `GENERATOR LAYER` group vs the
non-generator section) become one panel with a source switch.

---

## 4. Source × capability

What each source provides, and what it is missing. This is the honest gap list.

| Capability | Authored Material | Fill | Generator |
|---|---|---|---|
| Base Color | from asset | authored | optional (`bGeneratorAlbedo`) |
| Normal | from asset | derived | derived from generated height |
| Roughness / AO / Metallic | from asset | authored | — |
| Height | from asset | authored | **the chain's output** |
| Region IDs | — | — | published by modules |
| Flow | — | — | published by modules |
| Named masks | — | — | published by modules |
| Substrate input | n/a (is the substrate) | `bFillPassThrough` | **none** |
| Substrate blend | `CompositionMode` | `CompositionMode` | `HeightBlend.Op` (height only) |

Two rows are the actual work: **substrate input** and **substrate blend**.

---

## 5. Substrate input (pass-through)

Today: `Data.bFillPassThrough = Layer.Type == Fill && Layer.bFillPassThrough;`

So a `Fill` layer can inherit the composed substrate as its input, and a `Generator`
layer cannot. A generator chain always starts from zero — its height is a signed delta
around 0. The only way to make a generator react to what is below it is a
generator-specific hack (`HeightFollow` on Strata Carve), which is not a layer contract.

**Proposal:** make the input source a layer-level property, not a Fill-only bool.

```text
INPUT   [ Authored | Substrate ]
```

- `Authored` — today's behaviour for every source.
- `Substrate` — the layer's input is the composed result below it. For a generator chain
  this means the running height starts from the substrate instead of zero, so "carve into
  what is below" becomes expressible without a per-generator parameter.

This reuses the existing `bFillPassThrough` plumbing rather than adding a parallel path.

---

## 6. Substrate blend (per channel)

Only height has an op + amount today. Per channel:

| Channel | Substrate blend today | What it needs |
|---|---|---|
| Height | op + `Amount` + `Softness` | already correct — make it the model |
| Base Color | toggle only (`bGeneratorAlbedo`) | an amount |
| Region IDs | none | a **selector**, not an amount — IDs are discrete |
| Flow | none | a selector or a signed mix |
| Coverage / masks | none | an amount |

**Proposal:** one vocabulary. Either extend the `HeightOp` op+amount pattern to every
channel, or collapse `CompositionMode` and `HeightOp` into one set. Do not keep two.

Note the asymmetry that must survive: continuous channels (height, color, coverage) take
an amount; discrete channels (IDs) take a selector. Do not force a lerp onto IDs.

---

## 7. Gallery as a preset shelf

The gallery asymmetry ("the gallery brings authored materials, generators and fill don't")
is not a layer problem — it is that the gallery only holds *assets*.

**Proposal:** let the gallery hold **presets** as well as assets:

- authored materials (today)
- saved generator chains
- saved fill setups

Then all three sources are equally bringable, and the source row's picker can open the
gallery for any of them. This is the same feature already sketched as "Export as Mixtormat
Smart Material" in `archive_old/code_docs/hidden_features_and_ideas_roadmap.md`.

---

## 8. What this changes in code

| Today | After |
|---|---|
| `EMixtormatLayerType` = `Material \| Fill \| Generator` | `EMixtormatLayerSource` = `AuthoredMaterial \| Fill \| Generator` |
| `bFillPassThrough` (Fill only) | layer-level `InputSource` = `Authored \| Substrate` |
| `CompositionMode` (Material/Fill) + `HeightBlend.Op` (Generator) | one substrate-blend vocabulary |
| `SMixtormat_Inspector.cpp`: two branches by layer type | one panel, source switch at the top |
| `MixtormatLayerGather.cpp`: `bFill` / `bGenerator` from layer type | `bGenerator` from `HasGeneratorModules()` (already done), `bFill` from source |
| Gallery: materials only | gallery: materials + presets |

`FMixtormatLayer::HasGeneratorModules()` (added for generators-on-any-layer) is already the
right primitive for "does this layer run a module chain" and stays.

---

## 9. Phasing

**Phase 1 — source row (UI only, no data change).** Add the segmented source row to the
layer inspector and switch the existing panels behind it. `EMixtormatLayerType` keeps its
values; nothing serialized changes. This alone removes the "generator layer feels like a
different thing" problem.

**Phase 2 — input source.** Generalize `bFillPassThrough` to `InputSource`. Fill keeps its
meaning; Generator gains `Substrate`.

**Phase 3 — one substrate-blend vocabulary.** Collapse the two enums, extend the op+amount
pattern per channel, keep selectors for discrete channels.

**Phase 4 — gallery presets.** Generator chains and fill setups become gallery items.

---

## 10. Non-goals

- **Do not remove layers.** Scoping and the single composite step are load-bearing; a flat
  node list would need grouping nodes to replace them.
- **Do not lerp Region IDs.** They are discrete; they need a selector.
- **Do not unify by deleting `CompositionMode` without checking its readers.** `Coat` is a
  real behaviour, not a synonym for `Replace`.
- **Do not add a fourth entry point.** The point of this document is that there are already
  too many.
