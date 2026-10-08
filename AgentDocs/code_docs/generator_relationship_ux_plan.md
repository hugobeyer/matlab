# Generator relationships — target-first implementation plan

Status: implementation specification, 2026-10-08. **Documentation only; not implemented.**

Primary decision: **target-first source picker + target-owned connection rows**. This replaces the confusing primary workflow, not merely its labels. Existing advanced authoring, serialized data, evaluation semantics, and unrelated UI remain intact.

Implementation baseline updated 2026-10-08: Noise Mask/Noise Gate and live `Noise Value from…`
source authoring now exist; all six generators can own deformation tools, including Cliff Strata.
The target-first Structural Warp picker, projected relationship rows, generator-local collapse,
and `LayerConnections` style fields in this plan **still do not exist**. Do not conflate the mask
source workflow with completion of this plan. See `noise_gate_flow_handoff.md` for delivered code,
UI availability findings, and source-review limitations.

**Availability requirement:** do not hide supported features through generator-name whitelists,
label simplification, or narrow-panel cleanup. Disable unavailable actions with a specific reason.
Valid reasons include missing compatible fields, unresolved/disabled producers, execution order,
instance ownership, or unsupported group authoring. If no concrete reason can be traced to the
current contract/reader, treat the gate as an issue to investigate, not a design requirement.
Retired parameters with no runtime reader may remain absent only with their retirement documented;
showing an inert slider is not restored functionality.

This document is the textual handoff. Implementation agents must not open, read, or interpret concept images. No screenshot interpretation is required. No tests, test harnesses, diagnostics, builds, commands, or Unreal launches are included or authorized by this plan. Completion is assessed through targeted source reading and manual static review only; that is not a claim of runtime correctness.

## 1. Authority, scope, and concept decisions

Read `AGENTS.md` and `AgentDocs/UI.md` before implementation. Source remains authoritative for runtime compatibility and existing behavior.

Design inputs reviewed:

- `Concepts/mixtormat-generator-ux/AGENT_HANDOFF.md`
- `Concepts/mixtormat-generator-ux/index.html`
- `Concepts/mixtormat-generator-ux/styles.css`
- `AgentDocs/chatgpt_mockup_ideas.md`
- `AgentDocs/groksflowui_mockups_ideas.md`

The HTML/CSS defines UX intent, not plugin APIs, output compatibility, or shipped measurements. The standalone HTML is an alternate packaging of a concept, not a second implementation target.

### Adopt

1. Begin with the receiving generator: `Warp using…`.
2. Choose a real source and typed output, then create a fully connected modifier in one edit.
3. Display that modifier beneath its receiving generator.
4. Display `Source → Operation → Target` in the inspector.
5. Keep owned tools visually distinct from incoming relationships.
6. Offer source navigation, source replacement, explicit repair, and restrained temporary highlights.
7. Offer local generator collapse for long stacks, using transient UI state.

### Correct before translating the concepts

- A generator appearing below another does not imply a connection, but authored order **does** control execution.
- Height Push cannot target Pebbles: its supported target is a later unscoped `StrataCarver`.
- Flow and UV Map are supported Structural Warp source kinds. Arbitrary Vector2, Slope, IDs, or Height are not substitutes.
- Grouped/composited outputs, copied settings, instanced settings, and explicit field references are different relationships.
- Do not add the mockup's invented Falloff/Sampling parameters.
- Do not copy the prototype's 27 px rows, 16 px ownership indent, CSS colors, or minimum 28 px menu rows into the plugin.
- Do not follow the concept handoff's testing instructions; this plan intentionally excludes testing.

### Deliver now versus defer

| Delivery | Scope |
|---|---|
| Primary implementation | Target-first Warp and Height Push pickers; atomic creation; target-owned rows; inspector summary; repair; collapse; style integration |
| Preserve | Advanced empty-module creation; endpoint editing; settings instances; clipboard; owned scopes; drag/drop; enable; output preview; undo/history |
| Defer | Two-selection marking menu; connection hotkeys; connection-section-only foldouts; generator-group operations; permanent wires; automatic reordering |

The existing Q marking menu is unrelated and stays unchanged. Do not introduce generator-child multiselect just to implement this feature.

This plan supersedes the **UX delivery choice** in `AgentDocs/code_docs/generator_interaction_ux_design.md`, not the underlying runtime/coordinate contracts. That earlier document contains historical source locations and defects that have since changed; do not implement its old defect list without checking current source.

## 2. Desired result, without images

### Creation

```text
Right-click Pebbles
  Warp using…
    Search sources…
    This layer
      Rock Formation / Flow Carve · Flow
      Rock Formation / Gravity Flow · Flow
    Earlier Generator Layer
      Cliff Strata / UV Grid · UV Map
    Unavailable
      Later Rock / Flow Carve · Flow — source runs after target
```

The displayed names are illustrative. Only actual capabilities and canonical eligibility may produce entries.

After choosing one compatible entry:

```text
Generator Layer 1
  Rock Formation                         GEN
      Flow Carve                         CARVE     owned tool
  Pebbles                                GEN
    Warp ← Rock Formation / Flow Carve · Flow    incoming connection
  Cliff Strata                           GEN
```

The operation and receiving generator are never guessed from adjacency. `Flow Carve` remains actually owned by Rock Formation. The connection row selects the existing Structural Warp payload, not Pebbles and not a fabricated child.

For a Strata target:

```text
Cliff Strata                             GEN
  Height Push ← Rock Formation · Height
```

### Inspector

```text
Rock Formation / Flow Carve · Flow
                  → Warp → Pebbles
[Go to source]  [Change source…]  [Change target…]

Existing Structural Warp controls
```

Use one line when width permits and two compact lines when it does not. This is not a new card system. Preserve the actual existing parameters and their conditional visibility.

### Disconnected or broken

```text
Pebbles
  Warp ← Choose source…

Pebbles
  Warp ← Source missing                  !

Structural Warp → Target missing         !
```

A uniquely identifiable same-layer target can retain a projected row even when the source is broken. A missing/ambiguous target cannot supply a trustworthy visual parent: retain the authored module row with a readable repair label. Never hide, erase, or silently rebind broken data.

## 3. Non-negotiable behavior contracts

### 3.1 Presentation is not execution

`FMixtormatLayer::Children` remains the flat authored execution array.

`FMixtormatLayerChild::ScopeOwnerChildId` retains its current real-ownership meaning. **Do not assign it to a structural target to obtain indentation.**

A module continues executing at its existing authored position. Target-owned rows are a nonserialized editor projection. Opening the hierarchy, expanding a generator, selecting a connection, or changing UI STYLE must not mutate authored arrays or request a composition solely for presentation.

### 3.2 Canonical eligibility

Reuse `MixtormatOutputReferences::EvaluateStructuralLinkForGather` and its status types. Preserve the editor's mixed projection matching gather: effective layers for the appropriate source/Warp target checks, and the binding-resolved destination copy for module/Push target checks.

Do not replace this with a name match, a simplified earlier-index check, a generic dependency validator, or a new UI-only compatibility table.

| Operation | Source contract | Target contract |
|---|---|---|
| Structural Warp | Completed generator-scope Flow or UV Map; same-layer earlier completed scope or eligible earlier layer | Later enabled unscoped generator in the same Generator layer |
| Height Push | Completed signed generator Height; same-layer earlier completed scope or eligible earlier layer | Later enabled unscoped `StrataCarver` in the same Generator layer |

Additional restrictions come from canonical statuses and current editor authoring gates. New target-first creation keeps the current rejection of instance targets and shared-group authoring. Existing instance/group data remains visible, and retains its existing edit restrictions.

Ordering is: **completed source scope → structural module → target generator**. A source tool being earlier than the target is insufficient when its owner scope is incomplete at the module.

Structural evaluation follows ordered dependencies. Do not add a generic graph/cycle engine or a fabricated cyclic status. Explain the canonical forward-order or scope error that actually exists.

### 3.3 Atomic creation

One selection creates one module, one source edge, and one target edge in one committed working-state edit. Use the existing custom `RecordEditHistory` mechanism; do not assume `FScopedTransaction` is the editor's history owner.

No temporary empty modifier in `WorkingLayers`. No `CreateStructuralModuleForTarget` followed by two `SetStructuralConnection` calls. No partial history record or intermediate preview.

### 3.4 Identity and repair

Callbacks carry owner/child GUIDs and output identity. Resolve indices at the moment an action executes. Never retain references into arrays that may rebuild.

Display names are labels, never identifiers. Duplicate GUIDs are ambiguity, not a reason to select the first match. Renaming a source must not change the reference. Missing sources are not rebound to similarly named children.

## 4. Existing owners: exact file map

All paths below are relative to the repository root. Symbols, rather than line numbers, are the stable navigation anchors.

### 4.1 Runtime/shader contract: read, do not redesign

| Existing file | Responsibility / implementation boundary |
|---|---|
| `Source/MixtormatRuntime/Public/MixtormatLayerTypes.h` | Flat layer children and actual `ScopeOwnerChildId`; no new serialized visual-parent fields |
| `Source/MixtormatRuntime/Public/MixtormatOutputReference.h` | `FMixtormatOutputReference`, `EStructuralLinkIssue`, `FStructuralLinkStatus`, evaluator declarations |
| `Source/MixtormatRuntime/Private/MixtormatOutputReference.cpp` | Canonical order, scope, enabled, typed-source and target rules |
| `Source/MixtormatShaders/Private/Compositing/MixtormatGeneratorGather.cpp` | Actual gather projection and ordered structural execution |
| `Source/MixtormatShaders/Private/MixtormatGpuGeneratorPasses.cpp` | Existing GPU behavior; no UX-driven execution rewrite |

No new Runtime dependency on Editor, enum reordering, asset migration, or shader dispatch changes belong to this relationship-presentation task. Preserve separately delivered Noise mask fields and the appended `MaskNoise` parameter owner; this boundary does not authorize reverting them or prohibit separately requested runtime/shader work.

### 4.2 Editor orchestration and behavior

| Existing file | Required work |
|---|---|
| `Source/MixtormatEditor/Private/Widgets/SMixtormat.h` | New picker/atomic-create declarations; small transient collapse/hover/projection state; preserve old wrappers |
| `Source/MixtormatEditor/Private/Widgets/MixtormatChildAddress.h` | Reuse stable addresses; add a consistent `GetTypeHash` only if needed for address-keyed UI state |
| `Source/MixtormatEditor/Private/Widgets/SMixtormat_Layers.cpp` | `RebuildLayerList`: rebuild/invalidate derived connection data alongside existing label/count caches |
| `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatStructuralConnections.cpp` | Shared editor adapter, candidate menus, labels, reasons, highlight roles, source navigation, repair |
| `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerActions.cpp` | Refactor `PrepareStructuralModuleForTarget`; new fully wired commit path; preserve legacy creation |
| `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerMenus.cpp` | `BuildGeneratedContextMenu`: primary target-first actions; move existing empty creation into a clearly named advanced submenu without removing it |
| `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerHierarchy.cpp` | Replace flat display iteration with projected descriptors; keep real address/index callbacks; adapt hierarchy paint |
| `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayersPrivate.h` | Reuse `FindSiblingRoot`, `FindSubtreeEnd`, enable/capability helpers; expose only necessary internal declarations |
| `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerChildren.cpp` | Preserve creation defaults, names, toggles, scoped ownership and removal; route new row actions to existing handlers |
| `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerDragDrop.cpp` | Map visual targets to authored boundaries; preserve `CanMovePublishedOutputs` and `StructuralLinksPreserved` |
| `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerClipboard.cpp` | Preserve copy/remap/instance/paste semantics; never treat projected connections as owned target subtrees |
| `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorGenerators.cpp` | Add shared relationship summary to Push/Warp panels; reuse endpoint menu/setter; keep parameters |

### 4.3 UI components

| Existing file(s) | Required work |
|---|---|
| `Source/MixtormatEditor/Private/UI/Layers/SMixtormatLayerChildRow.h` and `.cpp` | Retain shared row input/enable/menu behavior; add optional label-content, disclosure and hover hooks for composition |
| `Source/MixtormatEditor/Private/UI/Layers/SMixtormatLayerHierarchy.h` and `.cpp` | Continue paint-only hierarchy metadata; paint authored ownership separately from target-owned relation decoration |
| `Source/MixtormatEditor/Private/UI/Layers/SMixtormatLayerSurface.h` and `.cpp` | Reuse child recipe; add an opt-in suppression of active halo for connection presentation, default unchanged |
| `Source/MixtormatEditor/Private/UI/Layers/SMixtormatLayerIcon.h` and `.cpp` | Reuse the current enable affordance; do not create another enable-checkbox design |
| `Source/MixtormatEditor/Private/UI/Layers/MixtormatLayerBadges.h` and `.cpp` | Preserve kind/badge contracts for normal rows; omit redundant Warp/Push badge text only in the new connection presentation |
| `Source/MixtormatEditor/Private/UI/Menus/MixtormatMenuBuilder.h` and `.cpp` | Keep the current menu builder; extend optional tooltip routing if needed; no global menu replacement |
| `Source/MixtormatEditor/Private/UI/Menus/SMixtormatMenuItem.h` and `.cpp` | Reuse row rendering; optional hover/focus hooks for picker highlights; other callers default to unchanged behavior |
| `Source/MixtormatEditor/Private/UI/Containers/SMixtormatMenuPanel.h` and `.cpp` | Reuse existing popup surface, not browser/native Unreal chrome |
| `Source/MixtormatEditor/Private/UI/Atoms/MixtormatIcons.h` and `.cpp` | Use existing icon wrappers; no new icon library |

`SMixtormatLayerConnector.*` is not an existing implementation file. The reference in `AgentDocs/UI.md` is stale; `AgentDocs/ICONS.md` documents its removal. Do not resurrect the legacy widget to implement data-flow wires.

### 4.4 Style ownership

| Existing file | Required work |
|---|---|
| `Source/MixtormatEditor/Private/Style/MixtormatDesignTokens.h` | Structural constants only; no duplicate mutable style defaults |
| `Source/MixtormatEditor/Private/Style/MixtormatTheme.h` | Add one small proposed `FMixtormatLayerConnectionMetrics` group |
| `Source/MixtormatEditor/Private/Style/MixtormatThemeSchema.cpp` | Register all new live-editable fields under Layers / Connections |
| `Source/MixtormatEditor/Private/Style/MixtormatResolvedStyle.h` and `.cpp` | Carry and resolve the new metric group; use existing palette/surface roles |
| `Source/MixtormatEditor/Private/Style/MixtormatRecipes.h` and `.cpp` | Reuse child/menu recipes; narrowly extend halo control if the current surface delegates it here |
| `Source/MixtormatEditor/Private/Style/MixtormatTypography.h` and `.cpp` | Reuse `LayerName`, `LayerSource`, `Menu`, and `Caption`; no new font family or weight system |
| `Source/MixtormatEditor/Private/Style/MixtormatThemeStore.h` and `.cpp` | Preserve schema-driven save/load/default/reset lifecycle; modify only if the new group requires explicit merge plumbing |
| `Source/MixtormatEditor/Private/Widgets/SMixtormat_Theme.cpp` | Preserve `RequestThemeRefresh` / `ApplyPendingTheme`; use existing reconstruction routing |
| `Source/MixtormatEditor/Private/Widgets/SMixtormatThemePanel.h` and `.cpp` | Reuse schema-driven controls; add no hardcoded parallel style panel |
| `Config/UIStyleTheme.json` | Add new registered fields only; do not normalize or overwrite existing saved theme values |

These paths are integration touchpoints, not a demand to edit every file. If a generic existing pipeline already handles a new schema field, leave that pipeline alone and document the verified route.

## 5. Proposed files and responsibility split

The following files **do not exist yet**. Names and types here are proposed, not claims about current APIs.

| Proposed files | Single responsibility |
|---|---|
| `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatStructuralConnectionModel.h` and `.cpp` | Transient connection context/status, typed source candidate collection, and reusable gather-equivalent projection |
| `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatStructuralConnectionProjection.h` and `.cpp` | Deterministic mapping from authored rows to visible target-owned connection descriptors |
| `Source/MixtormatEditor/Private/UI/Layers/SMixtormatStructuralConnectionRow.h` and `.cpp` | Compose a connection-specific label/state into the existing child-row shell; no model mutation or eligibility logic |
| `Source/MixtormatEditor/Private/UI/Menus/SMixtormatStructuralSourcePicker.h` and `.cpp` | Searchable, grouped source chooser using existing menu panel/item styling |

Do not split every label/helper into a new file. Keep localized labels/reasons/navigation orchestration in `MixtormatStructuralConnections.cpp`.

### Proposed transient records

`FMixtormatStructuralSourceCandidate`:

- Stable source owner/child address.
- Typed `OutputName` and `Kind`.
- Source generator-owner breadcrumb, display label, full tooltip/search text.
- Canonical edge/module status and translated unavailable reason.
- Availability and selected-state information derived from the current context.

`FMixtormatStructuralConnectionContext`:

- Existing module address, or a prepared insertion proposal with stable module identity.
- Operation type and target address.
- Effective layers plus the correctly binding-resolved destination view.
- No widget pointers and no references into `WorkingLayers`.

`FMixtormatProjectedChildRow`:

- Real authored address and authored child index for this rebuild.
- Presentation kind: ordinary / incoming connection / authored repair row.
- Optional visual parent address, visual relation depth, and authored scope depth kept separately.
- Module address, readable source/target labels, canonical status.
- Paint metadata and collapse visibility derived from visual topology.
- Explicit authored before/after insertion boundaries where meaningful.

These records are plain editor structs, not `USTRUCT` asset payloads. Do not serialize projection, counts, labels, collapse state, or hover into runtime objects.

## 6. Refactor the shared connection model first

Current `MixtormatStructuralConnections.cpp` already owns `FConnectionProjection`, source/target status text, candidate evaluation, duplicate-name labels, endpoint setters, incoming counts, and selected endpoint highlighting. Reuse these responsibilities; do not create a rival system.

### Extraction boundaries

1. Extract the gather-equivalent effective/resolved projection into the proposed model module.
2. Extract source enumeration from `BuildStructuralConnectionMenu` into typed candidate collection.
3. Keep translated `FText` labels/reasons in the editor adapter or pass them through one centralized formatter.
4. Keep working-state mutation, history, status, selection, and preview refresh on `SMixtormat`.
5. Keep creation defaults and insertion orchestration in `MixtormatLayerActions.cpp`.

Avoid an extraction that drags all of `SMixtormat` into the model module. A prepared proposal can supply the candidate collector with arrays and module identity; the collector does not need editor history or widgets.

### Shared source enumeration

For Warp, reuse `GetChildCapabilities` output declarations and the current `bCopyableAsField` plus Flow/UV Map filtering. Include Gravity Flow whenever the actual capabilities and canonical source rules allow it; do not special-case only Flow Carve by label.

For Push, preserve the existing generator Height source construction: `OutputName = Height`, `Kind = ScalarSigned`, then canonical evaluation. Do not assume Height Push enumeration is identical to Warp capability enumeration.

Both existing-module editing and target-first creation must use the same collector and status translator. The evaluation context differs; the meaning of a compatible source does not.

### Reasons

Translate current canonical statuses rather than inventing a second status enum:

- Unset source: `Choose source…`.
- Missing child/layer: `Source missing` / `Source layer missing`.
- Duplicate identity: `Source identity ambiguous` / `Target identity ambiguous`.
- Disabled producer/layer/target/reference: name the disabled part.
- Wrong kind: `Requires Flow or UV Map` or `Requires signed Height`.
- Wrong/incomplete source scope: distinguish unsupported owner from unfinished scope.
- Forward source: `Source runs after this operation`.
- Forward target: `Operation must run before target`.
- Wrong/scoped target: state the supported target restriction.
- Unavailable effect asset: `Source effect unavailable`.
- Unsupported group authoring: editor gate reason, not a fabricated runtime status.

A disabled operation remains configured. Show `Disabled` separately from edge eligibility so valid stored endpoints do not disappear when the user switches the module off.

Do not promise GPU texture availability from structural eligibility alone.

## 7. Target-first popup: complete interaction specification

### Entry points

In `BuildGeneratedContextMenu`, add primary `Warp using…` for generator targets. Add `Height Push from…` for Strata targets; unsupported generator targets retain a disabled explanatory option rather than suggesting support.

Opening the popup must not dirty the material. Use the addressed target, not whatever selection happens to change while a submenu is open.

Retain existing `Add Structural Warp` and `Add Height Push` behavior under `Advanced → Add unconnected…`. Retain the existing layer-level add paths as well. This is a discoverability change, not removal of an authoring capability.

### Picker anatomy

Use the existing menu panel through `MixtormatMenu::FBuilder::Widget`, with a focused `SMixtormatStructuralSourcePicker` as its content. Do not render an additional nested popup plate inside that plate.

```text
Warp Pebbles using…                caption, noninteractive
[Search sources…]                 existing field/well styling
This layer                        muted caption
Rock Formation / Flow Carve · Flow
Rock Formation / Gravity Flow · Flow
Earlier Layer Name                muted caption
Cliff Strata / UV Grid · UV Map
Unavailable                       muted caption
Later Rock / Flow Carve · Flow — runs later
```

- One-click source/output selection; no second Apply step.
- Group by source layer, retaining actual layer order and source order within groups.
- Eligible entries appear before unavailable entries.
- Search matches layer, owner generator, source tool, and output name; search never changes compatibility.
- Each distinct output is a distinct entry identified by address + output name + kind.
- Display the generator/tool breadcrumb where it explains the source; do not repeat the layer prefix on every entry when the section already names it.
- Retain unrelated-output candidates as disabled explanations where useful (`No Flow or UV Map output`), but do not flood the menu with every scalar socket as a fake selectable Warp source.
- Disabled entries show a short inline reason and a full tooltip. A disabled action must not have an activation path.
- No compatible sources: show a noninteractive explanation and retain unavailable rows; do not create an empty module on selection of the explanation.
- If the target itself cannot be authored, explain why at the entry point and do not build misleading available source entries.
- Search row and captions remain visible while the source list scrolls.
- Escape closes without editing. Outside-click dismissal behaves like existing menus.
- Keyboard Up/Down navigates eligible rows; Enter activates the focused eligible candidate. Search retains text-edit semantics; navigation transfers deliberately to the candidate list.
- Hover/focus may highlight the source and target without changing persistent selection.
- Closing, activating, rebuilding, changing document, or destroying the picker clears its temporary highlight.

`FBuilder` currently exposes captions, items, submenus, separators, and arbitrary widgets, not search/candidate APIs. Add the source picker as focused composition; do not claim search already exists. `SMixtormatMenuItem` currently has no explicit picker hover/focus delegates. Any such hooks are new optional APIs with unchanged defaults for current callers.

### Delivered precedent to align with — Noise-valued mask sources

The noise/mask authoring work (`Widgets/Layers/MixtormatMaskSources.cpp`, `UI.md`) already implements a
smaller version of this pattern: `Noise Value from…` lists owner-qualified compatible existing
sources, disables the rest with a reason, revalidates GUID addresses at click time, and commits one
edit. Reuse its conventions for the relationship pickers rather than inventing a second vocabulary:
same disabled-with-reason style, same stable-address revalidation, same single-history commit. The
mask path covers a source *value* only; it does not project ownership rows, does not move authored
children, and does not replace this plan's target-owned display, which remains unimplemented.

## 8. Atomic preparation and commit

### Reuse current preparation

`PrepareStructuralModuleForTarget` already:

- Checks unique target layer/child identities.
- Requires an enabled Generator layer, enabled unscoped non-instance generator target.
- Enforces Strata-only Push targets.
- Locates the root boundary immediately before the target.
- Avoids splitting the previous owner's subtree via `FindSiblingRoot` / `FindSubtreeEnd`.
- Applies child creation defaults and link defaults.
- Inserts into proposed arrays, not working arrays.
- Builds effective/binding-resolved views and checks the target.
- Checks `PublishedOutputPlacementsValid` and `StructuralLinksPreserved`.

Extend this rather than reproducing these rules in the picker.

### Proposed API shape

Preserve the old `PrepareStructuralModuleForTarget` and `CreateStructuralModuleForTarget` signatures as wrappers. Introduce a distinctly named connected preparation/creation path or an internal overload receiving the selected typed source. Exact proposed names:

- `PrepareConnectedStructuralModuleForTarget(...)`
- `CreateConnectedStructuralModuleForTarget(...)`
- `BuildStructuralSourcePickerForTarget(...)`

The shared internal preparation routine accepts an optional proposed source; legacy creation deliberately leaves it unset. Connected creation requires fully eligible source **and** target, unlike legacy target-only preparation.

### Prepare for menu availability

1. Resolve the target and its legal insertion boundary.
2. Create one proposed module using `ApplyChildCreationDefaults` and `ApplyLinkDefaults`.
3. Insert it immediately before the target in a proposed copy.
4. Build the canonical evaluation context at that proposed module position.
5. Collect/evaluate candidates against that context.

Do not evaluate against an arbitrary existing Warp or against the target's index without the inserted module. Scope completion must be judged at the actual proposed operation position.

Building a picker creates no working-state module. Reuse its prepared baseline while enumerating candidates instead of rebuilding the entire effective layer stack separately for every label. Only candidate edge overrides/status work should vary where the canonical evaluator permits it.

### Prepare again when clicked

1. Capture only target GUIDs, operation type, source GUIDs, output name and kind.
2. Re-resolve the current target/source; reject missing or ambiguous identities.
3. Re-run defaults/insertion/projection against current working state.
4. Set the typed source identity fields and target GUID in the proposed module.
5. Preserve default Flow trace settings; do not copy source producer settings into placement controls.
6. Require canonical module/source/target eligibility and all existing placement-preservation guards.
7. On failure, retain working state/history/selection and show the canonical reason.

### Commit once

Follow the existing `CreateStructuralModuleForTarget` commit pattern:

1. Replace `WorkingLayers` with the validated proposal once.
2. Expand its layer and the target's new transient foldout.
3. Select the real module through existing selection lanes, without calling a selection helper that submits a second debug-preview refresh.
4. Refresh layered preview once using the existing commit path.
5. Reset history coalescing as the existing creator does; record exactly one edit.
6. Recalculate dirty/status state using existing helpers.
7. Rebuild hierarchy and sync inspector once.

Opening/closing/searching a popup is not an edit. A failed proposal is not an edit. Do not introduce global history batching solely for this path if a local one-shot commit suffices.

## 9. Target-owned display projection

### Projection algorithm

1. Start with authored rows and real subtree ranges, including disabled and unresolved rows.
2. Index unique layer/child identities without resolving ambiguity by first match.
3. Identify structural modules and inspect the same effective/binding-resolved connection view used for status.
4. A module may project beneath a target when the target is a unique real same-layer unscoped generator, the module has an unambiguous unscoped authored identity, and moving its display block would not split/misrepresent an authored subtree.
5. Target identity and safe display ownership determine placement; source eligibility does **not** determine whether a row is visible.
6. Bucket projected modules by target GUID. Order each bucket by authored module execution index, not label/alphabetical order.
7. Emit ordinary generator rows in authored order. Immediately beneath each target, emit its incoming connection rows, then its actual owned rows in their original relative order.
8. Omit a projected module's original display position exactly once. Keep every authored child represented exactly once in the expanded projection.
9. If a module has an actual owned subtree, move its entire safe display block together and preserve the descendants' true ownership beneath the operation. Do not reparent its masks/tools to the target.
10. If identities, scope boundaries, group provenance, or effective payload mapping are ambiguous, retain the authored block as an explicit repair row. This is required representation of existing data, not silent compatibility fallback.

No shader/gather loop consumes this projection.

### Broken and read-only states

| Existing state | Presentation |
|---|---|
| Valid connection | Under target; full source breadcrumb |
| Source unset | Under identifiable target; `Choose source…` |
| Source missing/disabled | Under identifiable target; warning/status reason |
| Operation disabled | Keep row and endpoints; disabled appearance |
| Target disabled or wrong supported kind | Keep under the uniquely identifiable unscoped generator if otherwise safe; show canonical error |
| Source/module/target invalid order | Keep visible, explicit ordering warning; do not reorder |
| Target unset/missing/duplicate | Authored repair row with target action |
| Scoped/ambiguous structural module | Authored repair block; preserve real scope |
| Shared-group structural payload | Existing group location, read-only/unsupported explanation; no invented per-layer authored copies |
| Settings instance | Preserve instance identity/read-only controls; project only when resolved target maps uniquely and safely to the authored layer; otherwise retain authored row |

`GetStructuralIncomingCountLabel` currently counts active-valid relationships. Preserve that meaning. Do not silently redefine it as all stored rows.

Derive a separate **stored incoming relationship count** from the same projection for collapsed disclosure summaries. Name it distinctly in code and in tooltips. No competing independently gathered relationship model is permitted.

### Hierarchy painting

The current `ChildHierarchyPaint` / `GetDisplayScopeDepth` derives branches from authored scope. Keep this path for ordinary scopes, but no longer use flat next-child scanning to determine projected adjacency.

Use visible descriptors for last-sibling and ancestor-rail paint metadata. A connection receives a local incoming-direction marker, not an ownership trunk from the source.

Do not extend a source's scope branch all the way to a target. Do not draw yellow/orange full-layer rails or blue source-target loops. Authored ownership rails are legitimate and stay; permanent data-flow rails are not.

## 10. Compact row and typography specification

### Actual source defaults versus saved theme

The source defaults and checked-in authored theme differ. Widgets must use the **resolved theme**, not choose whichever hardcoded number looks nicer.

| Existing field | C++ default | `Config/UIStyleTheme.json` value at planning time |
|---|---:|---:|
| `LayerLayout.RowHeight` | 26 | 26 |
| `LayerLayout.ChildRowHeight` | 18 | 22 |
| `LayerLayout.GroupRowHeight` | 18 | 17 |
| `LayerLayout.PaddingX` | 0 | 8 |
| `LayerLayout.ChildIndent` | 28 | 38 |
| `LayerHierarchy.Indent` | 28 | 59 |
| `LayerHierarchy.Width` | 1 | 1 |
| `LayerHierarchy.ChildArmLength` | 8 | 7 |
| `LayerLayout.ItemGap` | 0 | 7 |
| `MenuLayout.Width` | 190 | 210 |
| `MenuLayout.RowHeight` | 20 | 20 |
| `MenuLayout.ItemInset` | 3 | 12 |
| `MenuLayout.PanelPadding` | 3 | 4 |
| `ControlLayout.LayerChildIconSize` | 16 | 16 |

Saved typography currently uses `Typography.LayerName.Size = 9`, `LayerSource.Size = 7.5`, and `Menu.Size = 9`. These are theme/font units, not a license to force browser CSS font sizes.

Do not change these global defaults or authored values as a side effect of this task. The concept's minimum 20 px hit square must not force rows to grow; at the current saved 22 px child height it fits, and smaller themes must keep nonoverlapping hit areas within their resolved row height.

### Row anatomy

Ordinary generator and owned-tool rows retain their present anatomy, icon, enable control, source text, badge, output access, and existing scope indent.

Incoming connection row:

```text
[enable]  [incoming chevron]  Warp ← source breadcrumb · Flow     [status if needed]
```

- Height = `Resolved.LayerLayout.ChildRowHeight`.
- Relation indentation = target's normal content indentation + proposed `LayerConnections.Indent`.
- Add proposed `LayerConnections.Inset` inside that relation block for subtle separation.
- Do not use `LayerHierarchy.Indent` as the relation step: the saved 59 px step is authored-scope styling and would consume excessive width for references.
- For the checked-in theme, a root generator's base padding is `8 + 38 = 46` before its internal controls. Relation padding becomes `46 + 16 + 4 = 66`; true owned-tool padding remains `46 + 59 = 105` at one authored scope level.
- Connection rows deliberately use a smaller reference inset than real owned tools. Text direction and breadcrumb distinguish them; they are not pretending to be another owned flow tool.
- Keep the enable control aligned within the relation's own row. It changes the actual module through the existing handler.
- Use `MixtormatIcons::ChevronLeft()` as a decorative incoming-direction marker, not a clickable collapse control. The text `←` remains the explicit relationship direction; do not substitute the settings-instance/variable-link glyph.
- Marker size inherits `ControlLayout.LayerChildIconSize`, constrained within row height.
- Operation/source main text uses `LayerName`; output suffix and reason/count text use `LayerSource`.
- One flexible text region, ellipsis, no row wrapping. Keep the operation word visible and truncate the source breadcrumb first.
- Tooltip contains full source layer/owner/tool/output, target, status, and authored execution-position explanation.
- No permanent trailing pair of endpoint dropdown badges on the new presentation.
- Do not repeat `Structural Warp`, `WARP`, and `Warp` in three columns. This presentation uses `Warp` once; normal badge contracts remain unchanged elsewhere.
- No always-visible ellipsis button: the existing right-click menu exposes actions. Inspector buttons remain explicit for discoverability.

### Surface and states

Reuse `SMixtormatLayerSurface` child gradients and typography. Add no CSS palette, new luminous borders, or additional glowing panel.

- Default: existing child surface; distinction comes from text structure and reference inset.
- Hover: existing child hover recipe.
- Selected: existing selected child surface and crisp edge; suppress the active halo for connection presentation only.
- Ordinary rows: existing active glow and instance-source appearance remain unchanged.
- Disabled: existing disabled opacity; retain readable endpoint labels.
- Invalid: muted status marker plus explicit tooltip/inspector reason, never color alone.
- Use a text `!` at the existing secondary text size if no appropriate owned warning icon is available. Do not invent an existing `Warning()` API or borrow Unreal icons.
- Source/target highlight: existing narrow accent/modified bars, no animated bloom and no wires.

### Narrow hierarchy

Do not widen the hierarchy or alter splitters automatically. Priority is enable → direction/operation → truncated source → compact status. Drop redundant output suffix text only when the full output remains in the tooltip/inspector; do not remove the source address information from the model.

Leave ordinary row columns alone. Relationship-specific layout should not shrink every child's name area.

## 11. Collapse, counts, and source navigation

Generator-local disclosure is **new**. Existing layer/group expansion does not provide it automatically.

- Add a disclosure only to a generator with actual owned rows and/or projected incoming rows.
- Reuse existing chevron wrappers and `LayerDisclosure` icon-role treatment; do not create a second disclosure style.
- Default expanded: current owned rows remain visible unless the user explicitly collapses them.
- Clicking disclosure collapses that generator's displayed incoming rows and real owned subtree only.
- A projected module is hidden visually, not disabled or removed.
- A collapsed target shows a compact `N incoming` label only when it has relationships. Tooltip distinguishes stored count, active-valid count, and issues.
- Expanded targets do not repeat incoming-count clutter when the rows already explain it.
- Keep collapse state transient, address-keyed, and scoped to the current document/editor session. Do not add asset properties or persist it to theme JSON.
- Prune orphaned state after deletion/replacement; avoid index keys.
- Explicit selection/navigation to a hidden child expands its containing layer/group and applicable generator foldout.
- Plain collapse does not clear the current inspector subject or composition state.

Proposed state: `CollapsedGeneratorAddresses` plus a single optional transient hovered/focused structural relation. If a `TSet<FMixtormatChildAddress>` is used, add `GetTypeHash` consistent with owner type, owner GUID, and child GUID. Do not key only by child name or array index.

`Go to source` resolves the stored source, selects the real source tool, expands actual containers, and scrolls it into view using the hierarchy's existing scrolling ownership. If scroll support is missing, add an addressed row lookup at rebuild rather than guessing screen coordinates. A missing source disables navigation and leaves `Change source…` available.

Cross-layer sources include layer name in full labels. Duplicate display names use the existing ordinal disambiguation plus owner/layer breadcrumb as necessary; do not print raw GUIDs as the primary name.

## 12. Inspector, editing, disconnect, and repair

Reuse these existing wrappers:

- `BuildHeightPushControls`
- `BuildHeightPushConnectionMenu`
- `BuildStructuralWarpControls`
- `BuildStructuralWarpConnectionMenu`
- `BuildStructuralConnectionMenu`
- `SetStructuralConnection`

Add one shared relationship-header builder in `MixtormatStructuralConnections.cpp`, consumed by both inspector panels. The header derives source, operation, target, and reason from the same connection model as the hierarchy.

Keep source/target editing in the inspector. Labels can become readable `Change source…` / `Change target…`, but the stored endpoint functionality remains. Existing instances retain their disabled edit gates; the summary does not bypass them.

Connection context menu order:

1. `Go to source` — disabled with reason if unresolved.
2. `Change source…` — shared candidate picker in existing-module context.
3. `Change target…` — existing target menu/context with canonical checks.
4. `Disconnect source` — existing setter clear semantics, no automatic removal.
5. Existing enable, copy, instance, duplicate, outputs/preview, and relevant advanced actions.
6. Existing removal action, still using the normal removal handler.

Disconnect source invalidates the stored source identifiers as the current setter does. Do not reset unrelated Flow placement settings, change the target, or remove the modifier. Target-disconnect remains available through advanced endpoint editing.

Removal/deletion is user-triggered through existing behavior. This plan does not authorize deleting data during migration or automatically deleting linked modifiers when their target disappears.

Retain all actual Push controls, including Amount and any existing panel parameters. Retain Warp Flow Amount, Trace Length, Flow Steps, enable, UV Map behavior, and existing conditional groups. Do not expose Flow trace sliders as if UV Map used the same integration path.

For invalid order, show `Source → operation → target must follow execution order` with the canonical specific reason. Provide navigation to the offending endpoint and existing manual reorder controls. Do not silently move layers/modules or add an automatic reorder path.

## 13. Drag/drop, clipboard, selection, and history

### Addressed events

Projected rows carry real module addresses. A visual row number is never a child index.

- Selection selects the real module.
- Enable changes that module's existing enabled state.
- Context menu is built for that module.
- Source navigation selects the real source.
- Output-preview shortcuts remain gated by actual capabilities.
- Normal rows retain Shift + right-click output access, drag behavior, badges and tooltips.

### Drag/drop semantics

Projection cannot silently redefine moving a target as moving all incoming operations.

- A generator drag moves its actual authored subtree only, as today.
- Its incoming projected rows are not part of that subtree.
- Existing ordering-preservation guards can reject a move that would break a valid relationship; show their reason.
- Dragging a connection operates on its actual authored module/subtree, never changes its target by visual nesting.
- Drag tooltip must state: `Move execution operation; target unchanged`.
- Drop markers represent real before/after root boundaries, not a virtual insertion inside the target.
- Dropping around a projected row maps explicitly to that module's authored boundaries and identifies the execution destination in the tooltip.
- A projected descendant cannot be used to bypass ID-group/scope gates.
- Retargeting is an explicit menu/inspector action, not an accidental consequence of drop indentation.
- Preserve layer/group reorder guards, `CanMovePublishedOutputs`, `StructuralLinksPreserved`, and `PublishedOutputPlacementsValid` where already applicable.

Do not auto-carry modifiers with target moves. That changes current execution behavior and can affect other consumers.

### Clipboard / settings sharing

Current `CopyChildSubtree` already remaps both Height Push and Structural Warp endpoints when their referenced identities lie inside the copied real subtree. Keep that implementation; the earlier audit's missing-Push-remap defect is historical.

- Copy target generator: its real owned children follow; visually incoming connections do not become new owned children.
- Copy connection: copy the actual modifier/subtree; preserve/remap references according to existing rules.
- Duplicate/instance keep existing insertion and eligibility checks.
- Settings sharing is displayed separately from field connection status.
- Read-only instance endpoint summaries must not become writable through the new picker.
- Paste remains tied to an authored address/boundary, not a projected display index.

### History and cache lifecycle

`RebuildLayerList` currently clears structural label/count caches. Extend that same lifecycle for projection and new labels; do not add a second independent invalidation mechanism.

- Recompute derived labels/status after rename, endpoint edit, enable, source effect change, reorder, group expansion/remapping, paste, deletion, load and history restore through existing rebuild paths.
- Theme changes rebuild geometry/style, not field eligibility semantics.
- Hover needs paint invalidation, not a composition rebuild or history entry.
- Generator disclosure needs hierarchy/layout rebuild, not composition refresh.
- Undo/redo restores authored endpoints; projection is regenerated from restored state.
- Clear picker/highlight state on document replacement and destruction.
- If a UI-triggered rebuild can destroy the hovered widget, clear its transient highlight before rebuilding.
- Do not keep captured raw module/source pointers in open popup delegates.

## 14. UI STYLE integration: exact proposed token contract

### Reuse before adding

Reuse:

- `LayerLayout.ChildRowHeight`, `PaddingX`, `ChildIndent`, and ordinary `ItemGap`.
- `LayerHierarchy` for real ownership rails only.
- `ControlLayout.LayerChildIconSize`, existing disabled-label opacity.
- `LayerDisclosure` and `Menu` icon roles.
- Existing child/menu surface recipes and palette roles `Text`, `TextMuted`, `Accent`, `Modified`.
- Existing structural-highlight geometry where its current readers fit.
- Typography roles `LayerName`, `LayerSource`, `Menu`, `Caption`.
- Existing inspector row/group/well layout.

No `ConnectionRowHeight`, duplicate font-size fields, hardcoded colors, second palette, new glow system, or generic `Spacing.Small` abstraction.

### New metric group

Proposed C++ type: `Mixtormat::FMixtormatLayerConnectionMetrics`.

Proposed theme/resolved member: `LayerConnections`.

Register the following fields under UI STYLE **Layers → Connections**. These identifiers are new; keep them stable once authored themes use them.

| Proposed schema ID | Default | Range / step | Current reader to add |
|---|---:|---|---|
| `LayerConnections.Indent` | 16 | 0–40 / 1 | Projected relation indentation, never real ownership |
| `LayerConnections.Inset` | 4 | 0–12 / 1 | Connection content inset |
| `LayerConnections.TextGap` | 3 | 0–12 / 0.5 | Operation/breadcrumb/output/status spacing |
| `LayerConnections.PickerWidth` | 300 | 220–480 / 1 | Source picker desired width, bounded by available work area |
| `LayerConnections.PickerListMaxHeight` | 260 | 100–600 / 1 | Scrollable candidate list, excluding fixed caption/search area |

Use `EMixtormatThemeRefreshMode::StyleRefresh` consistently with current LayerLayout/Hierarchy registrations, unless the implemented readers can safely use a narrower existing refresh mode. The current theme refresh path promotes transitional StyleRefresh to reconstruction; do not bypass it.

This small group is sufficient for independent compactness. Do not register controls with no reader. If an additional tunable becomes demonstrably necessary, document its specific geometry/reader before adding it.

### Registration → resolution → reader → persistence

For each new field:

1. Define one default in `MixtormatTheme.h`, in the new metrics struct.
2. Add `LayerConnections` to `FMixtormatTheme`.
3. Add the corresponding metrics member to `FMixtormatResolvedStyle`.
4. Copy/clamp it through `ResolveTheme` in `MixtormatResolvedStyle.cpp`, respecting schema bounds.
5. Register the exact dotted ID, label, category, range, step, and refresh mode in `MixtormatThemeSchema.cpp`.
6. Associate the new block with the existing layer locate-target mechanism, as current layer properties do.
7. Read only `FMixtormatThemeStore::GetResolved().LayerConnections` in the current row/picker/projection readers.
8. Add defaults for these IDs to the `layers` section of `Config/UIStyleTheme.json` without changing existing values/order unnecessarily.
9. Trace existing schema-driven load/merge/save/reset to ensure the group is included. Missing new properties inherit registered defaults; old saved IDs retain their behavior.
10. Live edits flow through the existing theme panel change delegate and refresh path. No panel-specific shadow copy of the same values.

Theme store/panel edits are conditional on their actual generic plumbing. Do not add special-case JSON loaders if the existing schema pipeline handles the new fields.

### Constants and surface control

Only genuinely fixed structural values belong in `MixtormatDesignTokens.h`. Prefer existing constants for hairlines and marker bars; do not add mutable `MixtormatTokens` copies of `LayerConnections` fields.

Connection halo suppression is a presentation option on the existing surface/recipe, defaulting to ordinary behavior for all existing callers. It should skip the selected active halo for this role while keeping selected child fill/crisp edge and any independently meaningful instance-source marker. Do not globally set `Layer.ActiveGlow` to zero or change saved theme keys.

No fresh warning color is required: status text plus the existing palette is enough. Do not hardcode a red/teal CSS color to imitate the prototype.

## 15. Cleanup and migration boundaries

### Local cleanup that belongs to this implementation

- Combine duplicate source enumeration and eligibility evaluation into the shared model.
- Combine duplicate relationship summary labels/status formatting into one adapter.
- Reuse the current child-row shell rather than duplicate input/enable/menu code.
- Reuse menu surfaces/items rather than create an alternate popup design.
- Replace `None → None` badges in **projected connection presentation** with readable relation labels/actions.
- Keep endpoint badge builders for remaining ordinary/advanced presentations until their readers are traced; do not delete an API merely because the new main path no longer calls it.
- Keep one gather-equivalent projection implementation; remove its replaced local duplicate only after all current callers are redirected.
- Adapt hierarchy paint comments so authored-scope decoration and relation projection are explicitly distinct.
- Correct the stale legacy connector entry in `AgentDocs/UI.md` when the implementation documentation is updated.
- Update `AgentDocs/UI.md` with new entry points, projection semantics, style IDs, and nonserialized generator collapse.

### Do not clean up here

- Runtime child enums, legacy saved payloads, shader paths, or unrelated effects.
- Current Gravity Flow generation/trace semantics.
- Existing scene/viewport/Q marking-menu behavior.
- Unrelated UI STYLE literals elsewhere in the plugin.
- Old theme fields, deprecated icon assets, compatibility entry points, or dead-looking helpers without reader evidence and user approval.
- Historical documents or concept files by deletion/replacement.

No asset migration is required because source/target fields, flat execution order, and actual scopes are unchanged. Existing materials render through the same gather/compositor path. A broken existing link remains broken-but-readable until the user explicitly repairs it.

## 16. Ordered implementation phases

### Phase 1 — Shared connection context and candidates

Files: proposed model pair; `MixtormatStructuralConnections.cpp`; limited `SMixtormat.h` declarations.

Deliver:

- Extract gather-equivalent context without simplifying its mixed projection.
- Share typed enumeration, labels and canonical unavailable reasons.
- Keep existing endpoint menus/setter working through the refactored adapter.
- Keep all mutation/history outside the model.

Source-review completion: old edit menus and new candidates refer to one eligibility source; no UI-only runtime rule copy; no asset changes.

### Phase 2 — Fully wired preparation/commit

Files: `MixtormatLayerActions.cpp`, `SMixtormat.h`, connection adapter/model.

Deliver:

- Shared legal insertion/default preparation.
- Candidate evaluation at the prepared module's real position.
- Revalidated atomic connected creation.
- Unchanged legacy target-only creation wrappers.

Source-review completion: one working-array replacement, one preview/history commit, no temporary empty working-state module, no silent reordering.

### Phase 3 — Primary source picker and target context menus

Files: proposed picker pair; `MixtormatLayerMenus.cpp`; narrowly extended menu builder/item if necessary.

Deliver:

- Primary `Warp using…` and Strata-compatible `Height Push from…`.
- Grouped/searchable available and unavailable entries.
- Explicit reasons, one-click activation, keyboard dismissal/navigation.
- Advanced unconnected creation retained.
- Temporary addressed endpoint highlighting, cleared on close.

Source-review completion: no invented outputs, no click-time stale pointers/indices, no unavailable activation, no material edit on popup open/search/cancel.

### Phase 4 — Display projection and connection rows

Files: proposed projection/row pairs; `MixtormatLayerHierarchy.cpp`; child row/surface/hierarchy components; `SMixtormat_Layers.cpp`.

Deliver:

- Every expanded authored child represented once.
- Target-owned relation rows, authored scope distinction, explicit repair rows.
- Correct visible hierarchy paint and real address routing.
- No permanent data-flow lines.
- New row suppresses redundant badges/halo without affecting normal rows.

Source-review completion: projection cannot write runtime ownership or execution order; unresolved/read-only/group states do not disappear.

### Phase 5 — Inspector, repair, collapse, navigation and existing operations

Files: inspector generators; connection adapter; hierarchy/actions/children; child address; drag/drop/clipboard only where visual-address integration needs it.

Deliver:

- Shared source → operation → target summary.
- Source navigation and existing source/target editing.
- Explicit disconnect/repair with preserved parameters.
- Address-keyed generator collapse and distinct stored versus active-valid counts.
- Real-boundary drag/drop and unchanged copy/instance semantics.
- History/document-rebuild cache cleanup.

Source-review completion: visual ownership never becomes clipboard/removal ownership; collapse/hover are UI-only; current selection and all authored controls remain accessible.

### Phase 6 — UI STYLE plumbing and documentation cleanup

Files: theme/schema/resolved/config; conditional store/panel/recipe plumbing; `AgentDocs/UI.md`.

Deliver:

- Complete five-field Layers / Connections style contract.
- Current reader for every registration.
- Theme load/save/reset/live refresh integration.
- Existing authored theme values untouched.
- Documentation reflects delivered source, not anticipated implementation.

Implement the required style definitions/readers alongside the consuming phases; this final phase finishes lifecycle tracing and documentation, not a period of hardcoded temporary widget styling.

Source-review completion: no inline palette/row sizing, no duplicate token source, no inert style controls, no global density/glow change.

## 17. Static handoff checklist — no testing

This is a source-reading checklist, not a test matrix or authorization to run tools/commands.

- [ ] Primary creation begins at the target and produces both endpoints in one committed edit.
- [ ] Popup selection identity includes source owner/child, output name, kind and target identity.
- [ ] Preparation and click-time revalidation preserve complete-source-scope ordering.
- [ ] Warp accepts canonical Flow/UV Map only; Push uses signed Height and Strata target rules.
- [ ] Gravity Flow is discovered through capabilities when eligible, not a bespoke label branch.
- [ ] Effective/binding-resolved status matches gather's existing mixed projection.
- [ ] Authored arrays/scopes/enums/assets/shaders are not migrated for visual indentation.
- [ ] Every expanded authored row appears exactly once; malformed links remain repairable.
- [ ] Multiple incoming rows retain authored operation order.
- [ ] Cross-layer and duplicate-name labels preserve stable identity and full tooltip breadcrumbs.
- [ ] Disabled modules keep configured endpoints; warnings explain missing/disabled/forward states.
- [ ] Read-only instances/shared-group payloads cannot bypass current authoring gates.
- [ ] Generator collapse uses transient addresses and does not disable or delete children.
- [ ] Stored incoming count and active-valid count are not conflated.
- [ ] Drag/drop and paste route to authored boundaries, not projected indices.
- [ ] Copying a target does not copy its projected incoming modules as owned children.
- [ ] Existing removal/subtree behavior is preserved; migration deletes nothing.
- [ ] Inspector keeps actual parameters, conditional groups, edit gates and output shortcuts.
- [ ] Source navigation expands/scrolls the actual source without rebinding anything.
- [ ] Hover/collapse/style edits do not create history or unnecessary composition refreshes.
- [ ] Projection/labels/highlights rebuild or clear on history restore and document replacement.
- [ ] Every new style ID has definition, schema, resolved member, current reader and persistence route.
- [ ] Current saved theme density/colors/fonts/owned hierarchy indent are preserved.
- [ ] No permanent relationship rails, new glow layer, image-dependent instructions or external dependencies.

## 18. Implementation-agent handoff

Implement this plan in the listed order. Read targeted source symbols before editing; no concept image reads and no delegated image interpretation. The examples in this document replace image analysis.

Keep changes behavior-preserving and local to the connection authoring/presentation workflow. Refactor duplicated connection logic where specified, but do not redesign the compositor, serialized model, history system, or whole layer panel.

All types/APIs/files explicitly marked **proposed** need implementation. Do not call them as existing APIs. When a detail depends on Slate/engine signatures, read available source/documentation before using it instead of guessing.

Stop and ask before changing runtime compatibility, enabling shared-group structural authoring, moving operations automatically, changing target-copy ownership, removing advanced paths, or deleting existing authored data/features.

Do not add or run tests, diagnostics, commands, builds, or Unreal launches. Finish with a concise report of implemented files, preserved behavior, and source-review limitations. Do not claim visual or runtime validation.
