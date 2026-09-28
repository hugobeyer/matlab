# Mixtormat — IDs / Filters / Masks / Peeling Refactor Plan

## Goal

Refactor Mixtormat's child taxonomy and editor UX so each node has one clear responsibility:

- **IDs** create or modify Region IDs.
- **Filters** consume existing data and transform something else.
- **Masks** create coverage.
- **Generators** create structural surface data.
- **Effects** alter the material/surface.
- **Peeling** is procedural-only.
- **Pattern IDs** becomes IDs-only.

The end result should improve context menus, layer-stack readability, inspector clarity, instance behavior, group parity, Region-ID reuse, and future extensibility.

## Repo-audit corrections applied

Validated against current `main` during planning:

- `Random From IDs` is a mask/coverage node in the current runtime and belongs under **Masks**, not Filter.
- `BuildCombineIdControls()` exists, but Combine IDs is omitted from the child-inspector ownership visibility checks.
- `GetDisplayScopeDepth()` currently adds fake nesting to Combine IDs when an ID producer exists above it.
- the normal mask inspector still exposes the Texture / Layer Values source dropdown.
- Pattern currently emits more metadata than IDs + Gap; the existing relief path uses edge/ramp data.
- Pattern UV variation currently depends on Pattern-local metadata, so generic Cluster/Combine support needs a region bounds/center/orientation derivation path first.
- Peeling is already procedural in the compositor; remaining work is editor/registry/data cleanup and migration.
- group creation must receive an explicit procedural Peeling entry before asset-backed Peeling is removed.

---

# Phase 0 — Freeze semantics before changing UI

Define the intended ownership first.

## IDs

IDs create or modify Region IDs.

- Pattern IDs
- Cluster IDs
- Combine IDs

Rules:
- Pattern IDs and Cluster IDs are producers.
- Combine IDs consumes the nearest Region IDs and outputs modified Region IDs.
- IDs do **not** directly modify height, normal, roughness, AO, albedo, or UVs.
- Region-ID preview belongs here.

## Filters

Filters consume existing data.

Target ID-driven filters:
- HSV From IDs
- Ramp From IDs
- Random From IDs
- UV From IDs
- Relief From IDs

Rules:
- `From IDs` nodes read the nearest valid Region IDs above them.
- They never regenerate IDs.
- They should work after Pattern IDs, Cluster IDs, Combine IDs, and any other valid Region-ID producer already recognized by the compositor.

## Masks

Masks produce coverage.

Target submenu:

```text
Masks
├─ Texture Mask...
├─ Layer Values Mask
├─ Generated Mask
├─ Color ID Mask...
└─ Random From IDs
```

Rules:
- Texture Mask and Layer Values Mask are separate editor concepts.
- No user-facing `Source: Texture / Layer Values` dropdown.
- Generated Mask remains procedural.
- Color ID Mask remains distinct and owns ID/color-range selection.
- Random From IDs stays under Masks because it outputs 0..1 coverage and uses mask blend/shaping semantics.

## Generators

Generators create structural source data.

Existing example:
- Strata Carver

## Effects

Effects alter the material/surface.

Examples:
- Peeling
- Breakup
- Worn Edges
- Erosion
- Stain
- Runoff

Do not mix effect behavior into Pattern IDs or mask-source selection.

---

# Phase 1 — Context-menu / Add-menu refactor

## Target hierarchy

```text
Add
├─ Effect
├─ IDs
│  ├─ Pattern IDs
│  ├─ Cluster IDs
│  └─ Combine IDs
├─ Filter
│  ├─ HSV From IDs
│  ├─ Ramp From IDs
│  ├─ UV From IDs
│  └─ Relief From IDs
├─ Masks
│  ├─ Texture Mask...
│  ├─ Layer Values Mask
│  ├─ Generated Mask
│  ├─ Color ID Mask...
│  └─ Random From IDs
└─ Generators
   └─ Strata Carver
```

Apply the same organization to:
- layer RMB Add
- group RMB Add
- any equivalent creation menu

Requirements:
- reuse the same creation helpers for layers/groups where possible
- do not duplicate menu trees
- do not change compositor behavior in this phase
- do not invent fake node types just for menu labels

Likely files:
- `Source/MixtormatEditor/Private/Widgets/SMixtormat_Layers.cpp`
- `Source/MixtormatEditor/Private/Widgets/SMixtormat.h`
- shared menu helpers

Acceptance:
- IDs are no longer buried under Filter
- mask creation is grouped under Masks
- Generators remain separate
- layer/group menus match

---

# Phase 2 — Mask separation

## Goal

Remove the confusing user-facing mask source switch.

Today one normal Mask can expose `Texture / Layer Values` through a source dropdown. Keep runtime compatibility initially if useful, but stop presenting them as one casually switchable editor node.

## Texture Mask

Created by:
- `Masks > Texture Mask...`
- dragging a mask from the mask gallery

Both must create the same child path.

Inspector:

```text
TEXTURE MASK

Mask          [ asset picker ]
Weight

PLACEMENT
Tiling X
Tiling Y
Offset X
Offset Y
Flip U
Flip V
Rotate

SHAPING
Invert
Balance
Contrast
Offset
...
```

Use the same visual picker/gallery system used elsewhere, not a source-mode dropdown.

## Layer Values Mask

Created by:
- `Masks > Layer Values Mask`

Inspector:

```text
LAYER VALUES MASK

Channel       Luminance / R / G / B / Roughness
Weight

SHAPING
Invert
Balance
Contrast
Offset
...
```

Do not show:
- mask texture
- tiling
- UV offset
- flip
- rotation

## Color ID Mask

Created by:
- `Masks > Color ID Mask...`

Purpose:
- turn an ID map or color-ID texture into coverage
- support both exact ID picking and color-range thresholding
- keep this separate from `Random From IDs`, which generates values from Region IDs rather than selecting a range

Inspector:

```text
COLOR ID MASK

SOURCE
Source            [ ID map / Color ID texture picker ]

SELECTION
Mode              Exact ID / Color Range

Exact ID:
ID                [ picker / sampled ID ]

Color Range:
Color             [ color picker / eyedropper ]
Threshold         [ 0..1 ]
Width             [ 0..1 ]

SHAPING
Invert
Balance
Contrast
Offset

COMBINE
Blend Mode
Weight
```

Behavior:
- `Exact ID` selects one discrete ID and outputs 0/1 coverage.
- `Color Range` compares the sampled source color to the chosen color.
- `Threshold` controls the center/cutoff of acceptance.
- `Width` controls the softness/range around the threshold.
- The picker should support sampling from the current preview where practical.
- Do not expose a generic `Source` mode dropdown that mixes unrelated mask semantics.
- If the selected source already publishes discrete Region IDs, prefer exact integer ID comparison rather than reconstructing IDs from display colors.
- Color comparison must be deterministic and documented in the shader path; avoid hidden color-space conversions.

Implementation notes:
- reuse the existing Color ID mask child type if it already carries compatible serialized data
- append new serialized enum values only; do not reorder existing ones
- keep published-output/reference support compatible with the shared mask capability path
- preserve mask Blend Mode / Weight / Shaping semantics

Acceptance:
- user can pick an exact Region ID
- user can choose a color and adjust Threshold + Width
- exact ID mode is stable regardless of preview display color
- range mode produces soft coverage when Width > 0
- Color ID Mask instances preserve their selection mode and source

## Instance semantics

Keep Texture Mask and Layer Values Mask distinct in editor semantics because instance behavior depends on what the node represents.

A Texture Mask instance should remain a Texture Mask.
A Layer Values Mask instance should remain a Layer Values Mask.

Do not let an instance silently change semantic kind through a source dropdown.

## Implementation strategy

Short-term, both may continue using `FMixtormatMaskLayer` and the existing source enum if that reduces migration risk.

But:
- creation fixes the semantic source
- source dropdown disappears
- inspector is source-specific
- instance replacement validates semantic kind

Long-term, only split into separate serialized child types if the shared runtime struct becomes limiting.

Likely files:
- `MixtormatMaterial.h`
- `SMixtormat_Inspector.cpp`
- `SMixtormat_Layers.cpp`
- parameter binding
- clipboard/instance validation if required

Acceptance:
- gallery drag and Texture Mask creation share one implementation
- Layer Values Mask shows no irrelevant texture controls
- no Source dropdown
- instances preserve semantic kind

---

# Phase 3 — Make Pattern IDs IDs-only

## Goal

Strip UV and surface-treatment behavior out of Pattern IDs.

## Keep on Pattern IDs

Only fields that determine topology / boundaries:

- Pattern Mode
- Grid Mode
- Rows
- Columns
- Row Offset
- Jitter
- Swap Axes
- Rounding
- Gap Pixels
- Gap Random
- Gap Slide
- Seed
- Fracture Plates topology controls
- any mode-specific parameter that genuinely changes Region IDs or gap topology

## Move out of Pattern IDs

### UV behavior
- orthogonal / 90° only
- Rotation Min
- Rotation Max
- Scale Min
- Scale Max
- Offset
- Flip U
- Flip V

### Relief / surface behavior
- Height
- Height Random
- Profile
- Profile Random
- Feather
- Feather Random
- Feather Gain
- Gap Height
- edge/bevel height
- edge/bevel width
- Relative Width
- Variation
- Inset
- Roughness
- Roughness Amount
- AO
- AO Spread
- derived-normal contribution owned by this block

## Runtime rule

Pattern IDs should:
- publish Region IDs
- publish its existing Gap output where applicable
- not touch height
- not touch normals
- not touch roughness
- not touch AO
- not alter source UVs

## Inspector

Target:

```text
PATTERN IDS

PATTERN
Pattern Mode
Grid Mode
Rows / Columns
Row Offset
Jitter
Rounding

GAPS
Gap Width
Gap Random
Gap Slide

MODE-SPECIFIC
...

SEED
Seed
```

Keep the Region IDs preview eye.

Likely files:
- `Source/MixtormatRuntime/Public/MixtormatMaterial.h`
- `Source/MixtormatEditor/Private/Widgets/SMixtormat_Inspector.cpp`
- `Source/MixtormatShaders/Private/MixtormatGpuCompositor.cpp`
- `Source/MixtormatShaders/Private/MixtormatGpuCompositorInternal.h`
- `Source/MixtormatShaders/Private/MixtormatGpuPatternPasses.cpp`
- `Shaders/Private/MixtormatPatternIds.usf`
- parameter binding

---

# Phase 4 — Add UV From IDs

## Goal

Extract Pattern's per-region UV variation into a reusable Region-ID consumer.

Node:
`UV From IDs`

Reads:
- nearest valid Region IDs above it

Writes:
- source sampling/UV transform only

Controls:
- Enabled
- 90° Only / Orthogonal
- Rotation Min
- Rotation Max
- Scale Min
- Scale Max
- Offset
- Flip U
- Flip V
- independent Seed if required

Semantics:
- deterministic per ID
- does not generate IDs
- does not modify height/normal/roughness/AO
- downstream Region IDs stay unchanged

## Important audit

Current Pattern UV logic depends on Pattern-produced local metadata such as analytic cell centers/orientation.

Before promising Cluster IDs / Combine IDs parity:
- add or reuse a generic region bounds/center/orientation derivation pass
- generalize only if mathematically valid
- prefer a stable ID-driven transform that does not require Pattern-specific analytic centers
- if arbitrary-ID support is not sound yet, keep the feature Pattern-only and report the exact limitation instead of faking it

Acceptance:
- Pattern IDs -> UV From IDs
- Cluster IDs -> UV From IDs
- Pattern/Cluster -> Combine IDs -> UV From IDs
- disabling UV From IDs restores original sampling while IDs remain unchanged

---

# Phase 5 — Add Relief From IDs

## Goal

Extract Pattern's height/edge/roughness/AO treatment into a reusable Region-ID filter.

Node:
`Relief From IDs`

Reads:
- nearest valid Region IDs above it
- Pattern Gap output when available
- generic boundary / distance-field data derived from Region IDs when Pattern-specific edge metadata is unavailable

Important:
- Pattern currently produces more than IDs + Gap for relief: edge/ramp metadata is used by the existing bevel/feather path.
- Do not delete that capability until Relief From IDs has a generic replacement.
- If exact parity cannot be reproduced from arbitrary IDs yet, preserve the internal metadata path temporarily while keeping the user-facing ownership split.

Writes:
- Height
- derived Normal
- Roughness
- AO

Does not replace Region IDs.

## Controls

```text
RELIEF FROM IDS

RELIEF
Height
Height Random
Profile
Profile Random
Feather
Feather Random
Feather Gain

EDGES
Gap Height
Edge Height
Width
Relative Width
Variation
Inset
Roughness
Roughness Amount
AO
AO Spread
```

## Gap ownership

Pattern IDs owns topology:
- Gap width
- Gap randomization
- slide/boundary placement
- anything deciding region membership

Relief From IDs owns appearance:
- Gap Height
- bevel/chamfer profile
- edge height
- edge roughness
- AO
- normals

Do not duplicate Gap Width in Relief From IDs.

## Compatibility

Must work after:
- Pattern IDs
- Cluster IDs
- Combine IDs
- Breakup Region IDs if Breakup is already a valid Region-ID producer

Use existing nearest-ID-producer semantics.

Acceptance:
- Pattern IDs alone changes IDs only
- Pattern IDs + Relief recreates old Pattern relief closely
- Cluster IDs + Relief works without Pattern-specific assumptions
- Combine changes the IDs Relief consumes

---

# Phase 6 — Combine IDs inspector and row UX

## Fix inspector bug

`BuildCombineIdControls()` already exists.

`BuildInspectorPanel()` currently omits `GetSelectedCombineId()` from the child-inspector ownership checks.

Add it to:
- child inspector visibility
- inverse normal-layer inspector visibility

Selecting Combine IDs must show:
- Mode
- Amount
- Passes
- Seed
- Region IDs preview eye

Use the existing shared capability/preview path.

## Remove fake indentation

Current `GetDisplayScopeDepth()` artificially adds depth to Combine IDs when an ID producer exists above it.

Remove that behavior.

Combine consumes nearest IDs by stack order, but it is not scope-owned.

Target:

```text
Pattern IDs
Cluster IDs
Combine IDs
```

same hierarchy level unless real `ScopeOwnerChildId` says otherwise.

## Clean row labels

Current redundant style:

```text
Pattern IDs     PAT    FILT
Cluster IDs     FILT   FILT
Combine IDs     CMB    MERGE
```

Target:

```text
Pattern IDs
Cluster IDs
Combine IDs             MERGE
```

Rules:
- remove redundant PAT / FILT / CMB
- Pattern and Cluster need no useless right badge
- Combine keeps dynamic `MERGE / SUB`
- no row-height increase
- prefer changes in `MixtormatLayerBadges::KindForChild / ForChild` over Slate one-offs

---

# Phase 7 — Peeling becomes procedural-only

## Goal

Finish the procedural-only Peeling transition.

The compositor already treats Peeling as procedural. This phase removes the remaining asset/UI/data compatibility surface so the editor and serialized model match the runtime.

Keep:
- `EMixtormatEffectType::Peeling` if required as identity
- procedural Peeling parameters
- Flat / Curled
- current procedural GPU passes/shaders
- seed/growth/height/AO/curl controls

Remove:
- asset-backed Peeling creation
- asset-backed Peeling execution
- old authored/map-based Peeling branches
- dead authored Peeling data/maps
- asset-only Peeling defaults
- picker/menu entries that create Peeling from an effect asset
- stale comments and dead tests

Creating:

```text
Effect > Peeling
```

must always create this same procedural child for both:
- layer RMB Add
- group RMB Add

and must create:

```text
Child.Type = Effect
Child.Effect.Effect = null
Child.Effect.ProceduralType = Peeling
```

Compatibility:
- do not preserve a second runtime path just because it existed
- if shipping assets still reference asset-backed Peeling, identify and migrate them first

---

# Phase 8 — Peeling Seed Mask vs gate cleanup

## Current distinction

The current `Peel Mask` picker is actually a seed input.

It influences where peeling begins through:
- `PeelSeedMaskWeight`
- seed threshold
- subsequent growth

A scoped child mask beneath Peeling is different:
- it gates where the resulting Peeling effect is allowed to operate

Do not collapse these concepts.

## Rename

`Peel Mask` -> `Seed Mask`

Suggested inspector:

```text
PEELING

SEEDING
Seed Mask          [picker]
Mask Influence
Adhesion
Noise Influence
Curvature Influence
AO Influence
Height Influence
ID Influence
...
```

The scoped child mask stays an effect gate.

Do not call both simply "Mask".

## Fallback audit

Current Seed Mask can fall back to accumulated child mask.

Audit this carefully.

Preferred new semantics:
- explicit Seed Mask -> seed input
- no Seed Mask -> procedural seed signals only
- scoped child mask -> gate only

If the old fallback couples seed and gate, migrate old recipes and remove that ambiguity for new data.

Do not remove Seed Mask entirely: it serves a different purpose from the scoped gate.

---

# Phase 9 — Group / instance / clipboard parity

Every new/refactored node must fit the existing child architecture.

Verify for:
- UV From IDs
- Relief From IDs
- Texture Mask
- Layer Values Mask
- Pattern IDs after split
- procedural-only Peeling

## Groups

Verify:
- create
- select
- inspect
- enable/disable
- drag/reorder where legal
- duplicate
- instance
- published refs where relevant

## Instances

Verify:
- Go to Source
- Break Instance
- Replace Source
- persistent instance banner
- semantic mask type preserved

## Clipboard

Verify:
- Copy
- Copy as Instance
- Paste
- layer/group parity

## Stable IDs

Verify:
- document regeneration
- composition import
- duplication
- group expansion
- published references

---

# Phase 10 — Auto-link paired X/Y parameters

## Goal

Reduce unnecessary manual references for parameters that normally move together.

Common paired controls:
- Tiling X / Tiling Y
- Scale X / Scale Y
- Offset X / Offset Y where symmetric control is useful
- other explicit XY pairs that already share the same semantic owner and range

Do not apply this blindly to every pair.

## UX

For eligible rows, expose a compact link control between X and Y:

```text
Tiling X     [ 4.0 ]  [link]  Tiling Y [ 4.0 ]
```

Default behavior:
- paired fields start linked when they are conceptually one value split across axes
- editing X while linked updates Y
- editing Y while linked updates X
- existing unequal authored values must not be silently overwritten on load

User actions:
- click the link icon to unlink
- once unlinked, X and Y edit independently
- optional context action: `Link X/Y`
- optional context action: `Clear Link`
- `Clear Link` removes only the pairing relationship; it must not clear either numeric value

## Reference / driver behavior

Where the existing parameter-reference system already supports one field driving another:
- use the same underlying reference mechanism rather than inventing a second dependency system
- auto-create the reference only for eligible paired fields and only when the pair is in linked state
- mark auto-created links so they can be removed cleanly without touching user-authored references
- never replace an explicit user-authored driver/reference silently
- if either axis already has an explicit external reference, do not auto-link over it
- breaking the pair must preserve both current resolved values

Preferred ownership:
- the visible link state belongs to the editor/parameter-binding layer
- runtime shaders should still receive ordinary resolved X/Y scalars
- no shader special case for "linked"

## Eligibility

Start with:
- Texture Mask Tiling X / Y
- layer/source UV Scale X / Y where both axes are intended to match by default
- generator/effect XY scale or tiling pairs that already use identical ranges and meaning

Audit before adding:
- UV offsets may intentionally differ and should not default-link unless the tool semantics clearly call for it
- anisotropic controls must remain independent
- min/max pairs are ranges, not XY pairs, and must never be linked this way

## Serialization

- prefer reusing the existing reference/driver serialization
- if a small editor-only flag is required to distinguish auto-link from user-authored reference, append it safely
- never reorder serialized enums
- migration must preserve existing unequal X/Y values as unlinked

## Acceptance

- new eligible nodes can start with X/Y linked without extra setup
- changing either side updates the other while linked
- unlinking preserves both values
- `Clear Link` removes the automatic relationship cleanly
- explicit user references/drivers are never overwritten
- copied, instanced, grouped, and migrated children preserve valid link state
- no additional shader parameters are introduced only for link state

# Phase 11 — Parameter binding / driver audit

Moving Pattern fields to new children must not leave stale driver/reference addresses.

Update:
- parameter descriptors
- owner classification if needed
- parameter addresses
- reference remapping
- driver menus
- GPU driver compatibility

Special attention:
- fields moved from Pattern to UV From IDs
- fields moved from Pattern to Relief From IDs
- auto-created X/Y links must be distinguishable from explicit user-authored references
- unlink/clear must preserve current resolved values

Do not keep duplicate live Pattern parameters merely to preserve old addresses.

Migrate references if practical; otherwise report exact incompatibilities.

---

# Phase 12 — Serialization and migration

## Pattern migration

Old Pattern IDs may contain:
- topology
- UV variation
- relief
- roughness
- AO

Migration:

### Topology
Keep on Pattern IDs.

### UV
If old UV variation is active:
- create `UV From IDs`
- copy old UV fields

### Relief
If old relief/edge fields differ meaningfully from neutral defaults:
- create `Relief From IDs`
- copy old fields

Preserve authored ordering as closely as possible.

Typical migration:

```text
Pattern IDs
UV From IDs
Relief From IDs
```

If Combine IDs already exists, determine whether old behavior should be driven from pre-combine or post-combine IDs before inserting the migrated filters.

Do not guess.

## Enum safety

- append serialized enum values
- never reorder existing serialized enum values
- do not reuse numeric slots accidentally

## Peeling migration

If old asset-backed Peeling content exists:
- convert to procedural equivalent/defaults
- then remove old path

---

# Phase 13 — Preview / capabilities

Keep preview behavior centralized.

## IDs

Pattern IDs:
- Region IDs preview
- Gap remains copyable where already supported

Cluster IDs:
- Region IDs preview

Combine IDs:
- combined Region IDs preview

## Relief From IDs

Normal material preview is enough initially.

Only expose a scalar output if it already has a meaningful published output and can use the shared capability system.

## UV From IDs

No special debug UI required initially.

Do not add raw `V` UI.

Use:
- `GetChildCapabilities()`
- `GetChildPreviewOutputSet()`

Do not create another hardcoded preview table.

---

# Phase 14 — Tests

Add/update automation coverage.

## Menu taxonomy
- IDs submenu
- Filter submenu
- Masks submenu
- Generators submenu
- layer/group parity

## Pattern IDs
- changes Region IDs
- does not alter height/RAM/normal/roughness/AO alone

## UV From IDs
- deterministic per ID
- disable restores original mapping
- Pattern source
- Cluster source
- Combine source

## Relief From IDs
- Pattern source
- Cluster source
- Combine source
- changes height/normal/roughness/AO
- disable restores appearance while IDs remain

## Combine IDs
- inspector appears
- preview eye appears
- Pattern -> Combine
- Cluster -> Combine
- Combine -> Combine

## Masks
- Texture Mask creation
- gallery drag creates same semantic node
- Layer Values Mask has no placement controls
- Color ID Mask exact-ID picker
- Color ID Mask color-range Threshold + Width
- Random From IDs remains a coverage-mask node
- Texture instance stays texture
- Layer Values instance stays layer-values
- Color ID Mask instance preserves source and selection mode

## Peeling
- creation is procedural-only
- no effect asset required
- Seed Mask affects seeding
- scoped mask gates effect
- no hidden asset-backed branch
- no accidental gate-as-seed coupling after migration

## Groups / instances
- new types survive identity regeneration
- group instances
- group copy/paste
- Go to Source / Break / Replace

## X/Y auto-linking
- eligible new controls link by default where intended
- edit X updates Y while linked
- edit Y updates X while linked
- unlink preserves both values
- Clear Link removes only the relationship
- explicit external driver/reference blocks automatic linking
- copy/paste, duplicate, instance and group expansion preserve valid link state

---

# Phase 15 — UI polish and docs

## Layer rows

Target compact stack:

```text
Pattern IDs
Cluster IDs
Combine IDs           MERGE
UV From IDs
Relief From IDs
Texture Mask
Layer Values Mask
Generated Mask
Color ID Mask
Random From IDs
Peeling
```

Avoid badges that only repeat the node name.

## Documentation

Update public docs at:

`https://hugobeyer.github.io/mixtormat/`

Document:
- new Add-menu taxonomy
- IDs concept
- From IDs concept
- Pattern IDs topology-only behavior
- Combine IDs
- UV From IDs
- Relief From IDs
- mask types
- Color ID Mask exact-ID vs color-range workflow
- Random From IDs as a coverage mask
- procedural-only Peeling
- Seed Mask vs scoped effect gate
- linked X/Y parameter behavior and how to unlink/clear it

---

# Recommended implementation batches

Do not do this as one giant patch.

## Batch A — UI taxonomy and obvious bugs

1. Reorganize Add menus
2. Fix Combine inspector visibility
3. Remove fake Combine indentation
4. Clean ID row labels
5. Rename Peel Mask -> Seed Mask
6. Remove user-facing Mask Source dropdown

Low architectural risk.

## Batch B — Mask separation

1. Texture Mask creation path
2. Layer Values Mask creation path
3. Color ID Mask picker / exact-ID / color-range modes
4. Random From IDs moved under Masks
5. dedicated inspectors
6. instance semantic validation
7. group parity

Keep runtime storage compatible initially.

## Batch C — Pattern split

1. classify Pattern fields
2. make Pattern GPU path IDs-only
3. add UV From IDs
4. add Relief From IDs
5. move inspectors
6. update bindings

This is the main architecture change.

## Batch D — Migration

1. migrate old Pattern UV values
2. migrate old Pattern relief values
3. migrate old Peeling assets
4. validate saved recipes

## Batch E — Procedural-only Peeling cleanup

1. remove asset-backed code
2. remove dead authored-map fields
3. clean registry/menu exposure
4. separate seed semantics from gate semantics
5. remove ambiguous seed fallback if migration permits

## Batch F — X/Y auto-linking

1. identify eligible XY parameter pairs
2. reuse reference/driver infrastructure
3. add link/unlink UI
4. preserve explicit user-authored references
5. add migration and regression tests

## Batch G — Release validation

1. automation tests
2. UE build
3. shader compile
4. BuildPlugin/package
5. fresh-project install
6. group/instance regression
7. docs update

---

# Non-goals

Do not use this refactor to:
- redesign the compositor globally
- add raw V UI
- add unrelated debug views
- rewrite group flattening
- rewrite clipboard architecture
- rewrite stable-ID architecture
- change Pattern topology algorithms unless necessary to isolate ID generation
- introduce unnecessary serialized enums
- retain dead asset-backed Peeling purely for hypothetical compatibility

---

# Definition of done

The refactor is complete when:

1. Pattern IDs produces Region IDs/gap only.
2. Cluster IDs produces IDs only.
3. Combine IDs modifies IDs only.
4. UV From IDs consumes IDs and changes source mapping.
5. Relief From IDs consumes IDs and changes height/normal/roughness/AO.
6. Masks are clearly split into Texture / Layer Values / Generated / Color ID / Random From IDs.
7. Color ID Mask supports exact-ID picking and color-range Threshold + Width.
8. No Texture/Layer Values Source dropdown remains in the normal mask inspector.
9. Peeling is procedural-only.
10. Peeling Seed Mask is clearly distinct from a scoped effect gate.
11. Combine IDs inspector and eye preview work.
12. ID rows no longer use fake hierarchy or redundant labels.
13. Eligible X/Y pairs can auto-link, unlink, and clear without overwriting explicit user references.
14. Layer/group/instance/clipboard behavior remains consistent.
15. Old Pattern UV/relief data migrates without silent loss.
16. UE build, tests, shader compile, and package succeed.
17. Public docs match the new model.
