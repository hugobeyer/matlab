# Mixtormat — Group Layer and Mask Instance Plan

Status: Not implemented. Design only.

## Goal

Two features that share one mechanism.

**Group layer.** A folder in the stack that holds other layers and carries its own mask chain.
The group's resolved mask gates every layer inside it. Hiding the group hides its contents;
fading the group fades its contents; masking the group masks its contents. This is how a user
builds "the whole rust treatment, confined to the panel edges" without pasting the same mask
into six layers.

**Mask instance.** A mask child that points at another layer's resolved mask and follows it.
Edit the source, and every instance updates. This is how a user says "this dirt goes exactly
where the moss went", or "everywhere the moss did *not* go", without maintaining two copies of a
mask chain that will drift apart.

They share a mechanism because a group mask *is* an instanced mask: one layer's resolved mask
consumed by another. Building either one alone would build most of the other.

## What already exists

The pieces this plan needs are almost all present.

### The mask accumulator

Every layer resolves its ordered `Children` into one `CombinedMask`
(`MixtormatGpuCompositor.cpp:2532`). Mask, Generated, Craquelure and ColorId children each run a
compute pass that reads the accumulated mask, shapes its own signal through
`MixtormatShapeMask`, and blends with `MixtormatApplyMaskOperation`
(`MixtormatMaskOps.ush`). The accumulation ping-pongs on `MaskPassIndex`; the first child sets
`Initialize`, which makes it treat the previous value as zero.

`CombinedMask` then drives the layer's composite through a single funnel in
`MixtormatComposite.usf:162`:

```hlsl
float SamplePlacementMask(float2 UV)
{
	return HasMask != 0 ? LayerMask.SampleLevel(LinearWrapSampler, UV, 0.0f) : 1.0f;
}
```

That one function gates coverage, height blending, contact AO and border normals. Anything
multiplied in there applies to all of them consistently — which is exactly what a group mask
must do.

### A no-op composite that is already exact

`Parameters->Enabled` (`MixtormatGpuCompositor.cpp:3568`) is set from `Layer.bEnabled`. When it
is zero the composite shader takes a path that is an exact identity:

```text
ExistingCoverage = 0        -> Alpha = 0        -> every lerp weight is 0
NormalWeight     = 0        -> WeightedDetail = (0,0,1), and RNM with a neutral detail
                               returns the previous normal unchanged
ResultHeight     = PreviousHeightValue
EffectVisibility = 0
HeightDetailVisibility = 0
```

Critically, **a disabled layer still resolves its mask children**. Effect children are omitted
when render data is captured because filters such as Grade, Erosion and Chipping run after the
composite and would otherwise let a hidden layer modify the accumulated result. The composite's
`Enabled` uniform is zeroed while `CombinedMask` is still produced. A group layer is therefore
structurally identical to a disabled layer that nobody can enable, provided the group's effect
children are stripped as required below.

One branch `Enabled` does not gate: `WriteDebug` (`MixtormatComposite.usf:492`). It is set from
`DebugSettings.LayerIndex == LayerIndex` alone (`MixtormatGpuCompositor.cpp:3591`), so a selected
group will write the debug target. For `DebugMode == 5` that shows the group's own mask, which is
exactly what a user selecting a group would want. The other modes show signals derived from a
composite that did not happen. Debug preview is opt-in and per-selection, so leave it; if it
reads wrong in practice, restrict the group's debug modes to `LayerMask`.

### Snapshotting, already solved twice

`HeightSnapshots` (`MixtormatGpuCompositor.cpp:2531`) is the pattern this plan copies: a pre-pass
collects which layer indices are referenced, and the layer loop copies those layers' results
aside because the ping-pong targets get clobbered by the next layer.
`DebugMaskSnapshot` (`:3265`) does the same for a single mask under inspection.

### Why the composite pass cannot simply be skipped for a group

```cpp
const int32 WriteIndex = LayerIndex & 1;   // MixtormatGpuCompositor.cpp:3562
const int32 ReadIndex  = 1 - WriteIndex;
```

Ping-pong parity for BC, N, RAM, Height and Ridge is **derived from the layer's position**, not
from a running counter. A layer that skipped its composite would flip the parity for every layer
after it, and the fix would be copy passes for five render targets per group. Running the
group's composite as a no-op costs one dispatch that writes nothing and keeps every downstream
index correct. Take that.

## Data model

`MixtormatMaterial.h`.

```cpp
UENUM: EMixtormatLayerType += Group

// FMixtormatLayer
UPROPERTY() FGuid LayerId;         // stable identity; fresh on create and on duplicate
UPROPERTY() FGuid ParentGroupId;   // invalid == top level

UENUM: EMixtormatLayerChildType += MaskInstance

USTRUCT FMixtormatMaskInstance
{
	bool                     bEnabled   = true;
	FGuid                    SourceLayerId;
	EMixtormatMaskBlendMode  BlendMode  = Replace;
	float                    Weight     = 1.0f;
	bool                     bInvert    = false;
	float                    Balance    = 0.5f;
	float                    Contrast   = 1.0f;
	float                    Offset     = 0.0f;
};

// FMixtormatLayerChild += FMixtormatMaskInstance Instance;
```

### GUIDs, not indices

`HeightReferenceLayerIndex` is an index, and it costs four remap helpers in
`SMixtormatInternal.h:149-214` — one for insert, delete, move, and a validator to run after each.
Every future structural edit has to remember to call them. A GUID reference needs none of that:
insert and move are invisible to it, and delete leaves a dangling reference that resolves to
"skip this child", which is the correct behaviour anyway.

Do not repeat the index pattern for these two features.

### A group owns a contiguous block

The array stays flat and stays in composite order. `ParentGroupId` is the only structural
information, and one invariant makes it tractable:

```text
[ group ][ member ][ member ][ nested group ][ its member ][ member ]
```

A group's members immediately follow it. Two consequences fall out for free:

- A group's index is always lower than every member's index, so the backward-reference rule
  below is satisfied by construction and no cycle check is needed.
- Reordering a group is block arithmetic, and a nested group's mask is always resolved before
  any layer that needs it.

`ValidateGroupStructure` enforces it after every structural edit: every `ParentGroupId` resolves
to a `Group` layer earlier in the array, members are contiguous, index 0 is neither a group nor a
member.

### Group children are masks only

A group holds `Mask`, `Generated`, `Craquelure`, `ColorId` and `MaskInstance` children. Not
`Effect`. An effect modifies surface data, and a group has no surface. The add menu omits them,
and the flatten step strips them defensively — an erosion effect on a group would write
`RidgeTargets` (`:4003`) and pollute the ridge signal for every layer above it.

## Composite

Three additions to `MixtormatGpuCompositor.cpp`, one to `MixtormatComposite.usf`.

### 1. `bMaskOnly` on `FLayerRenderData`

Set for `Group` layers. Forces `Parameters->Enabled = 0u`. That is the whole change — the
existing identity path does the rest, and the group's `CombinedMask` is produced by the child
loop that already runs.

### 2. `MaskSnapshots`

`TMap<int32, FRDGTextureRef>`, mirroring `HeightSnapshots` exactly, including its "only snapshot
what is actually referenced" filter — these are full-resolution `PF_R16F` and a stack of twelve
layers should not allocate twelve of them to use two.

The pre-pass collects indices referenced by a member's group, or by a `MaskInstance` child.
After a referenced layer's `CombinedMask` resolves, one pass writes the snapshot.

For a group the snapshot is not a plain copy:

```text
Snapshot = CombinedMask * ParentGroupSnapshot * GroupOpacity
```

A new `MixtormatMaskSnapshot.usf`, roughly twenty lines. Folding the parent in here is what makes
nesting free: every group's snapshot already carries its whole ancestry, so a member layer never
needs more than one group texture regardless of how deep it sits. Group opacity folds in for the
same reason — one place, and it composes.

### 3. `GroupMask` / `HasGroupMask` on the composite pass

```hlsl
float SamplePlacementMask(float2 UV)
{
	float M = HasMask != 0 ? LayerMask.SampleLevel(LinearWrapSampler, UV, 0.0f) : 1.0f;
	return HasGroupMask != 0 ? M * GroupMask.SampleLevel(LinearWrapSampler, UV, 0.0f) : M;
}
```

Multiplying into the funnel rather than injecting an extra mask child into the member's chain:

- No `MaskPassIndex == 0` edge case. An injected child would be the *first* child on a layer with
  no masks of its own, where `Initialize` makes the previous value zero and a `Multiply` blend
  would resolve to black.
- The layer's own mask debug preview keeps showing the layer's own mask.
- Zero extra dispatches per member. An injected child would cost one full-resolution compute pass
  on every layer in the group.

### 4. `Initialize` follows the first *compositing* layer

```cpp
// MixtormatGpuCompositor.cpp:3567, today
Parameters->Initialize = LayerIndex == 0 ? 1u : 0u;
```

`Initialize` is what seeds the chain: at line 277 of the composite shader it replaces the
`Previous*` samples with constants, and at 473 it makes the layer establish the height rather
than blend into it. It also suppresses height blending (367, 375, 424) and generated features
(331), because a layer with nothing under it has nothing to read.

A mask-only layer at index 0 breaks that. The layer at index 1 would get `Initialize = 0` and
composite against the group's non-composite — height blending and generated features active where
they should be suppressed. The rule becomes:

```cpp
Parameters->Initialize =
	(LayerIndex == 0 || LayerIndex == FirstCompositingLayerIndex) ? 1u : 0u;
```

`FirstCompositingLayerIndex` is the first index with `bMaskOnly == false`. Index 0 keeps
`Initialize` unconditionally so a leading mask-only layer writes the seed constants instead of
reading a target nothing has written — `OutputBC`/`N`/`RAM` are only cleared when the stack is
empty (`:2311`), so a read there is the RDG ensure this plan is trying to avoid.

In a saved recipe index 0 is never a group, so this is identical to today's behaviour. It fires
only in the preview override arrays — which is exactly what makes solo work, below.

### 5. `MixtormatMaskInstance.usf`

`MixtormatMask.usf` minus the UV block: read `Texture2D<float> SourceMask` at the pixel's own UV,
`MixtormatShapeMask`, `MixtormatApplyMaskOperation`, same `saturate(lerp(Previous, Result,
Weight))` tail as every other mask node. Participates in `MaskPassIndex` like any other mask
child.

No tiling, offset, flip or rotation. The referenced mask is already in the composition's UV
space; re-tiling it would make "follows the source" false, which is the entire point of the
node. Shaping controls stay, because "where the moss is, but tighter" and "everywhere the moss is
not" are the two things people actually want.

### Backward references only

A `MaskInstance` resolves only when `SourceIndex < ConsumerIndex`. A forward reference is skipped,
exactly as an unconfigured ColorId node is skipped today (`:1674` — "no map or no colours…
Dropping it entirely is what an unconfigured node should do").

This is not only an implementation convenience. `Generated` children read the accumulated
curvature, AO, height and ridge from the layers below them, so a later layer's mask is not a
value that exists yet when an earlier layer composites. There is no correct answer to give.

The cost is real and should be stated plainly: the natural phrasing "dirt only where the rust
isn't" fails when the dirt sits below the rust. The two answers are to reorder, or to put both in
a group and mask the group. Enforce the rule in the compositor, not the data model, so the picker
can list later layers **disabled with a reason** rather than pretending they do not exist — and so
that lifting the restriction later needs no migration.

The enabling condition for lifting it, when it comes up: a forward reference is exact whenever the
source layer's chain contains no `Generated` child. That is a one-line predicate over
`Layer.Children`, gating a mask prepass. Not phase one.

## UI

Every widget this needs already exists.

- **Group header** — `SMixtormatLayerRow` unchanged, with a folder glyph in the `Thumbnail` slot
  instead of an asset thumbnail, the same way a Fill layer puts an `SColorBlock` there.
- **Enclosure** — `SMixtormatLayerGroup` already draws the accent edge that encloses a layer's
  children. A group's member block gets the same treatment one level out.
- **`RebuildLayerList`** (`SMixtormat_Layers.cpp:985`) becomes a stack-based walk instead of a flat
  loop, nesting member rows inside their group's widget. Roughly forty lines.
- **Tokens** — one new `LayerGroupIndent`; reuse `LayerEdgeWidth`. No literals in widgets.
- **Badges** — `ForLayer` gains a `Group` case returning `GRP`; `KindForChild` returns `LINK` for a
  mask instance and `ForChild` shows its blend mode. Both fit `BadgeMaxCharacters`, which clips
  rather than widens.
- **Inspector** — a Group section showing name, enabled and opacity, and nothing else; the other
  sixty-odd properties are not meaningful on a group. A mask instance gets a source-layer combo
  plus the standard weight / invert / balance / contrast / offset rows every mask node has.
- **Source picker** — lists layers earlier in the array; excludes self and own ancestors; shows
  later layers disabled with the reason.

## Editing operations that need work

Each of these was checked against the current code, not assumed.

| Site | What breaks | Fix |
| --- | --- | --- |
| `DuplicateSelectedLayer` (`SMixtormat_Layers.cpp:61`) | Copies the struct verbatim, so the copy shares a `LayerId` | Fresh GUID. Duplicating a group deep-copies the block with fresh IDs, rewires `ParentGroupId`, and repoints internal instance refs at the copies |
| `DeleteSelectedLayer` (`:84`) | Leaves orphaned members | Delete the whole block. Instances pointing at deleted layers go dangling and are skipped — no remap, which is the GUID payoff |
| `MoveSelectedLayer` (`:106`), `HandleLayerDropped` (`:130`) | `±1` steps a group into its own member block | Move blocks whole; reject a move that lands a group inside itself |
| Solo (`SMixtormat.cpp:531`) | Builds a one-element array, so soloing a member drops its group mask and soloing a group shows nothing | See below |
| `ValidateHeightReferences` (`SMixtormatInternal.h:149`) | Range-checks only; a height reference to a group is meaningless | Add a type check |

### Solo

Solo builds a one-element override array (`SMixtormat.cpp:531`). For a member of a group it
becomes the ancestor chain outermost-first, then the member:

```text
[ outer group ][ inner group ][ member ]
```

with the member's `bEnabled` forced true and its `HeightReferenceLayerIndex` cleared, as today.
The groups are mask-only, so they contribute nothing but their masks, and the flatten step
recomputes each `GroupMaskLayerIndex` from the GUIDs — no index remapping, because the array is
rebuilt rather than edited. With the `Initialize` rule above, the member establishes the chain
itself and renders exactly as it would soloed outside a group, times the group mask.

The alternative — appending the group's mask children onto the soloed member's own `Children` —
is *not* equivalent and should not be used. The group's chain would blend against the member's
accumulated mask instead of against zero, so any group whose first mask child uses something
other than `Replace` would resolve differently soloed than composited.

Soloing a group solos its whole block.

Two things that need **no** change, checked rather than assumed:

- `HaveSameLayers` (`SMixtormat.cpp:54`) uses `CompareScriptStruct` on the reflected struct, so new
  `UPROPERTY` fields are compared automatically and undo keeps working.
- `HaveSameLayerStructure` (`:74`) compares child `Type` only, which already covers a new child
  type.

`MixtormatBakeService` reads the compositor's output targets, not the layer array, so bake follows
for free.

## Verification

A clean compile proves nothing here. Mask-only layers plus new RDG bindings is precisely the shape
that produces unwritten-resource ensures — the compositor already carries a comment about
`Mixtormat.MaskB` being read before anything wrote it (`:2331`).

Run `MixtormatCompositorTests` headless. New cases:

- A disabled layer with a post-composite filter is an exact identity.
- A group with no mask children is a pass-through: its members composite identically to the same
  stack with the group removed.
- A group whose mask is black hides its members exactly.
- Nested groups multiply.
- A stack containing a group produces byte-identical output for the layers *below* the group,
  proving ping-pong parity survived.
- A dangling instance reference is an identity.
- A forward instance reference is an identity.
- Soloing a member of a group matches soloing the same layer outside a group, times the group
  mask — the `Initialize` rule, which is the one change here that touches an existing code path
  every composite runs through.

## Order of work

1. Data model, `ValidateGroupStructure`, GUID assignment and migration (existing recipes get fresh
   `LayerId`s and no parents).
2. `MixtormatMaskSnapshot.usf`, `MaskSnapshots`, `bMaskOnly`, `GroupMask` and the `Initialize`
   rule in the composite. **This is a test-only milestone, not a shippable one** — with
   `RebuildLayerList` still a flat loop, a group draws as an ordinary row and its members as
   siblings, so there is nothing to look at. The compositor tests above are what proves it.
3. Nested stack rendering and the group inspector section. First point the feature is visible.
4. Editing operations — duplicate, delete, move, drop, solo.
5. `MixtormatMaskInstance.usf` and the mask instance child, which by this point is a new child type
   consuming machinery that already exists.

## Out of scope

- **Isolated groups.** A group that composites into its own buffer and then blends the result, the
  way a Photoshop group set to anything but Pass Through behaves. Real compositor work, and the
  pass-through behaviour above is what Substance Painter folders do and what people expect.
- **Forward references.** See the enabling condition above.
- **Child-level instances** — pointing at one specific mask child rather than a layer's resolved
  mask. Layer-level is more useful and needs one snapshot per layer instead of one per child. The
  `DebugMaskSnapshot` machinery is there if this comes up.

## Noticed in passing

`Source/MixtormatEditor/Private/Services/MixtormatLayerPreview.cpp` (553 lines) has no callers
anywhere, including tests. It drives a fixed-slot MID from the migration-only single-mask fields
(`Layer->MaskTexture`, `MaskTiling`, `MaskBalance`, `MaskContrast`, `bInvertMask`) that the child
stack replaced. `MixtormatLayerPreviewTests.cpp` tests the GPU compositor despite its name.
Unrelated to this plan; worth deleting separately.
