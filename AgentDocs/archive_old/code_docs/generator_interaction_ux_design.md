# Generator interaction UX — design audit and options

Status: audit + options, 2026-10-07. Targeted source review only; no build, shader
compile, runtime or visual result is claimed. Source wins over this document.
Scope: the **interaction UX** of Height Push and Structural Warp only. No unrelated
UI redesign, no geological/shader behaviour change, no hidden fallback.

Read alongside `strata_structural_warp_design.md` and
`generator_warp_output_alignment_design.md` (the coordinate/ownership contracts
this UX must not disturb).

---

## 1. Current UX / data-flow problems (with source locations)

### 1.1 The connection is two inspector dropdowns, and nothing else

- Height Push: `BuildHeightPushControls` — `MixtormatInspectorGenerators.cpp` L823-878.
  Two `MakeDropdown` rows ("Source Height", "Target"), each a `MakeChip` opening
  `BuildHeightPushConnectionMenu` (L762-821).
- Structural Warp: `BuildStructuralWarpControls` — same file L959-1037; menus
  `BuildStructuralWarpConnectionMenu` (L880-957).
- The layer stack shows only a kind badge (`HPUSH` / `WARP`) —
  `MixtormatLayerBadges.cpp` `KindForChild` L312-313; `ForChild` returns empty for
  both (L241-247). Row name is just "Height Push" / "Structural Warp"
  (`MixtormatLayerChildren.cpp` L1395-1400).
- **Consequence:** the stack shows *what* a module is, never *what it drives* or
  *what drives it*. The relationship exists only inside the inspector.

### 1.2 Ordering is implicit and split across two rules

- Source must be an **earlier** child (same layer) or an earlier layer:
  `ResolveGeneratorInputSource` — `MixtormatOutputReference.cpp` L295-403;
  earlier-layer gate L333, same-layer `SourceIndex >= DestinationChildIndex`
  rejection L343-347.
- Target must be a **later** child in the **same** layer:
  - Height Push: resolved inline in gather (`MixtormatGeneratorGather.cpp`
    L345-357) and again in the editor menu (`MixtormatInspectorGenerators.cpp`
    L802-806).
  - Structural Warp: `ResolveStructuralWarpTarget` — `MixtormatOutputReference.cpp`
    L405-426 (`TargetIndex <= DestinationChildIndex` rejected L422).
- **Consequence:** the module row must sit *between* source and target, but the
  stack never says so. Dragging the module (or its source/target) can silently
  invalidate it, with no in-stack feedback. The failure mode is a silent no-op,
  not an error.

### 1.3 Height Push's source is an undeclared socket

- `GetChildCapabilities` (`MixtormatChildCapabilities.cpp`) declares **no**
  `Height` output for generators. The source menu synthesises
  `OutputName="Height", Kind=ScalarSigned` for every generator candidate
  (`MixtormatInspectorGenerators.cpp` L797-801) and relies on
  `ResolveGeneratorInputSource` for validity.
- Flow / UVMap sources *are* declared (`FlowDirection`, `WarpedUVGrid` —
  `MixtormatChildCapabilities.cpp` L261-271, L347-360), so they appear in the
  Outputs submenu and Copy Output. Height does not.
- **Consequence:** "completed signed Height" is invisible everywhere except the
  Height Push dropdown.

### 1.4 Structural Warp's source is a scoped sub-row, flattened into a list

- `ResolveGeneratorInputSource` requires the source to be an `Effect` that is
  **scoped** (`ScopeOwnerChildId` valid) under a generator that
  `MixtormatCanOwnGeneratorFlow`, with the owner completing earlier
  (`MixtormatOutputReference.cpp` L354-402).
- The menu flattens this into `"Layer / Child / Output"` rows
  (`MixtormatInspectorGenerators.cpp` L944-945).
- **Consequence:** the user picks a *flow tool that lives under a generator* from a
  flat list; the hierarchy relationship that makes it valid is erased in the
  dropdown.

### 1.5 The two target rules are enforced in different places

- Height Push target = **Strata only**, duplicated in the editor menu
  (L802-806) and in gather (L349-351) with no shared resolver.
- Structural Warp target = any later enabled unscoped generator, via the single
  runtime resolver `ResolveStructuralWarpTarget`.
- **Consequence:** the two modules look identical in the UI but obey different,
  differently-owned rules.

### 1.6 Feedback is a chip label with no reason

- Chip text is only `"None"` / `"Unavailable"` / a name
  (`MixtormatInspectorGenerators.cpp` L844-845, L976-979). Unavailable menu
  entries are `.Enabled(false)` with no explanation (L817, L927, L952).
- A disabled module can still be configured (menus set `bEnabled=true` on a
  *copy* for candidate evaluation — L787-788, L906-907), which is correct, but
  the chip does not distinguish "not set" from "set but invalid" from "owner
  disabled".

### 1.7 Clipboard asymmetry (existing defect)

- `CopyChildSubtree` (`MixtormatLayerClipboard.cpp` L38-58) remaps
  `StructuralWarp.Source` (L48) and `StructuralWarp.TargetChildId` (L49-52) when
  they fall inside the copied subtree, but **does not remap**
  `HeightPush.Source` or `HeightPush.TargetChildId`.
- Paste placement has a Structural-Warp-only rule (`ResolvePasteInsertIndex`
  L210-218: must land on a Generator layer); Height Push has no equivalent.
- **Consequence:** copying a subtree containing a Height Push leaves its
  source/target pointing at the old GUIDs.

### 1.8 Cycles cannot occur — the failure is silent disable

`ResolveGeneratorInputSource` relies on strictly decreasing evaluation order
(source earlier, target later), so a socket cycle is impossible by construction
(`MixtormatOutputReference.h` L102-111). The real failure is *forward-order*: a
module placed after its target, or a source placed after the module, silently
no-ops. There is no cycle state to show because the model forbids one.

---

## 2. Viable interaction models

### Model A — Structural modules as scoped children of their target generator

Move Height Push / Structural Warp into the target generator's subtree (the way a
Blur sits under its mask, or a flow tool under its generator). Source stays an
explicit reference chip; ordering becomes position within the target's subtree.

| | |
|---|---|
| Pros | Target is unambiguous and visible (nesting). Reuses scope machinery, indent, `SMixtormatLayerConnector`, drag-into-owner drop target. "Where modules attach" becomes literal. |
| Cons | Changes authored ordering: modules currently run **before** the target in the flat chain and accumulate at their own positions; a child-of-target runs **after** in tree order, so a "pre-target" sub-slot must be invented. Scope today means "gates owner", not "runs before owner". Largest gather/compose change; hardest migration of saved ordering. |

### Model B — A generator "interaction group" (IdGroup pattern)

A new ordered child (e.g. `GeneratorInteraction`) that owns its modules as
**scoped children** and names its target **once**. Each module keeps its own
source chip; scoped masks become children of the module. This mirrors `IdGroup`
exactly: `EMixtormatLayerChildType::IdGroup` + `FMixtormatIdGroup`
(`MixtormatLayerTypes.h` L154, `MixtormatIdTypes.h` L282), ordered scoped
references via `AddIdGroupSource` (`MixtormatLayerActions.cpp` L763-775),
`InsertScopedChild` (`MixtormatLayerChildren.cpp` L585-595), and a drop target
(`SMixtormatIdGroupChildDropTarget`, `CanDropChildIntoIdGroup` /
`DropChildIntoIdGroup` — `MixtormatLayerDragDrop.cpp` L17-39, L927-953).

| | |
|---|---|
| Pros | Proven pattern, low conceptual risk. Target named once, not per module. Ordering is explicit and local. Scoped masks as children of the module is native. Flat-chain semantics of the target generator are untouched. Directly answers "ordering, what drives what, where modules attach". |
| Cons | New child type + payload + gather/compose branch + capabilities + badges + menus + clipboard. Still one target reference (but one, not two dropdowns). New concept to learn. |

### Model C — In-stack connection chips / links only

Keep the flat chain and the two references; render source → module → target as
compact chips/links on the module row, with drag-to-connect and in-stack
valid/invalid states. No data-model change.

| | |
|---|---|
| Pros | Smallest change; no serialization or migration. Reuses the connector vocabulary (`SMixtormatLayerConnector` L15-49). Directly fixes "cannot see ordering / what drives what". |
| Cons | Does not make "where modules attach" structural. Two references remain. The forward target is still not a real edge in the stack unless drag-to-connect adds a drop target on the target row. |

---

## 3. Recommended smallest model

**Recommend Model B (interaction group), with Model C as an optional first slice.**

- B is the smallest model that actually changes the *interaction* rather than only
  its *legibility*, and it does so by reusing a pattern already proven in this
  codebase (IdGroup) rather than inventing one. It keeps the target generator's
  flat-chain semantics, authored ordering, tiling, signed height, IDs, UVs and
  bed random/position untouched.
- C is a strictly smaller increment that can ship first if B is too much: it adds
  chips/links and states to the existing rows with no data-model change. It is a
  legibility fix, not an interaction fix.
- **Do not** recommend A: it changes authored ordering semantics and the meaning
  of scope, which the design docs explicitly protect.

Non-negotiables for any model: Height Push stays distinct from Structural Warp;
no hidden fallback (missing/invalid stays a no-op, as gather already does at
`MixtormatGeneratorGather.cpp` L344, L357, L375); Runtime → Shaders → Editor
ownership preserved.

---

## 4. Exact affected ownership points

### Runtime (`MixtormatRuntime`)
- `Public/MixtormatLayerTypes.h` — `EMixtormatLayerChildType` (append only, L163-164
  is the current tail); `FMixtormatLayerChild` payload slot (L272-276);
  `ScopeOwnerChildId` semantics (L175-179).
- `Public/MixtormatGeneratorTypes.h` — `FMixtormatGeneratorHeightPush` (L678-701),
  `FMixtormatGeneratorStructuralWarp` (L705-725). Legacy `HeightSource`/`WarpSource`
  sockets stay serialized and disabled (L965-972); do not activate them.
- `Public/MixtormatOutputReference.h` + `Private/MixtormatOutputReference.cpp` —
  `ResolveGeneratorInputSource` (L295-403), `ResolveStructuralWarpTarget`
  (L405-426), `ValidateDependency` cycle check (L118-293). A group model needs a
  group-aware resolver here; this is the single source of truth for validity.
- `Public/MixtormatParameterTypes.h` — child-creation enum (L41-45).
- `Public/MixtormatIdTypes.h` — `FMixtormatIdGroup` (L282) is the precedent, not a
  thing to change.

### Editor (`MixtormatEditor`)
- `Widgets/Inspector/MixtormatInspectorGenerators.cpp` — `BuildHeightPushControls`
  (L823), `BuildHeightPushConnectionMenu` (L762), `BuildStructuralWarpControls`
  (L959), `BuildStructuralWarpConnectionMenu` (L880).
- `Widgets/Layers/MixtormatLayerChildren.cpp` — `IsChildEnabled`/`SetChildEnabled`
  (L87-135), `ChildTypeForCreation` (L685), `GetLayerChildName` (L1395),
  `GetScopeDepth` (L153), `CanAddScopedChild` (L542), `CanKeepScopedPlacement`
  (L615).
- `Widgets/Layers/MixtormatLayerActions.cpp` — `CreateChild` (L1441), `AddIdGroupSource`
  (L763) as the pattern to copy.
- `Widgets/Layers/MixtormatLayerDragDrop.cpp` — drop-target pattern (L17-39, L927-953).
- `Widgets/Layers/MixtormatLayerMenus.cpp` — `AddCreationSections` (L898),
  `AddSharedChildMenuItems` (L396-448), `BuildMoveChildToLayerMenu` (L234).
- `Widgets/Layers/MixtormatLayerClipboard.cpp` — `CopyChildSubtree` (L38-58);
  `ResolvePasteInsertIndex` (L191-218).
- `Widgets/MixtormatChildCapabilities.cpp` — declare the Height socket / group
  outputs (generator case L145-257).
- `UI/Layers/MixtormatLayerBadges.cpp` — badge for the group/module (L279-316).

### Gather (`MixtormatShaders/Private`)
- `Compositing/MixtormatGeneratorGather.cpp` — `GatherGeneratorHeightModuleChild`
  (L329-392).
- `MixtormatGpuCompositor.cpp` — child dispatch loop `RequestComposeInternal`
  (L1658-1700).
- `MixtormatGpuComposePipeline.cpp` — `EnqueueCompose` published-field demand
  (L688-701).

### GPU (`MixtormatShaders/Private`)
- `MixtormatGpuGeneratorPasses.cpp` — `AddGeneratorLayerPasses` (L2595+; modules
  run at their authored position; state keyed by `TargetChildIndex`),
  `AddStrataCarverPasses` (L1299-1366; consumes `GeneratorStructuralDisplacements`
  / `GeneratorHeightPushFields` by `Child.SourceChildIndex`),
  `AddStructuralWarpCoordinates` (L1191).
- Shaders: `MixtormatGeneratorHeightPush.usf`, `MixtormatGeneratorStructuralWarp.usf`,
  `MixtormatStrataCarver.usf`, `MixtormatGeneratorWarp.ush`,
  `MixtormatGeneratorHeightModules.ush`.

---

## 5. Migration / compatibility concerns

- **Enum:** `HeightPush` and `StructuralWarp` are already appended
  (`MixtormatLayerTypes.h` L163-164). Any new group type appends after them;
  never reorder (serialized by value).
- **Saved references:** assets store `HeightPush.Source` (layer + child + output +
  kind), `HeightPush.TargetChildId`, `StructuralWarp.Source`, `TargetChildId`.
  Any model that relocates these must keep the fields readable (dual-read) or
  migrate on load. Do not silently reinterpret a stored reference.
- **Legacy sockets:** `HeightSource` / `WarpSource` remain serialized,
  disabled-by-default (`MixtormatGeneratorTypes.h` L965-972). Do not activate them
  as an implicit warp or mix with layer-wide `ReferencedUV` (design doc §1).
- **Clipboard:** `CopyChildSubtree` must remap Height Push source/target — this is
  a defect today, independent of the chosen model.
- **Paste placement:** Height Push needs the same Generator-layer-only rule
  Structural Warp already has.
- **No hidden fallback:** missing/invalid connections stay no-ops; gather already
  returns early (L344, L357, L375). Do not add a default source or target.

---

## 6. Visual feedback states

| State | Meaning | In-stack / chip |
|---|---|---|
| Valid | Source resolves earlier, target resolves later, both enabled | Chip shows `Source / Output` and `Target`; connector line to the target row |
| Disabled | Module `bEnabled=false` (still configurable) | Row dimmed; chip keeps showing the stored connection |
| Missing | Stored GUID resolves to nothing | `Unavailable` + reason (source gone / target gone) |
| Forward-order | Source resolves after the module, or target resolves before it | Invalid; message "must be earlier" / "must be later" |
| Cyclic | Not reachable in the current model (strictly decreasing order) | Say so; do not invent a cycle state. If a group model adds any same-level reference, reuse `ValidateDependency`'s active-path check (`MixtormatOutputReference.cpp` L276-292) |

---

## 7. Open decisions for approval

1. **Model:** B (interaction group) as recommended, C (chips only) as a first
   slice, or A (children of target)?
2. **Height Push target:** keep Strata-only, or widen? If kept, unify the
   duplicated editor/gather rule behind one resolver.
3. **Height socket:** promote generator Height to a declared capability output so
   it appears in Outputs / Copy Output like Flow and UVMap?
4. **Target ownership:** does the group own the single target, or does each module
   keep its own?
5. **Scoped masks:** move them under the module (as the brief suggests), or leave
   them where they are?
6. **Clipboard:** fix the Height Push source/target remap now, or with the chosen
   model?
7. **Labels / naming:** group name, module row names, chip wording.
