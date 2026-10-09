# Global Variables — Implementation Plan

Status: plan only, no code written. Verify line numbers against source before
acting; this document is a point-in-time design.

## Goal

A document-scope variable table ("cell") where the user creates named float
variables with sliders, and any float parameter (layer opacity, effect amounts,
generator values, ...) can be **driven** by a variable. Editing the variable
slider updates every driven parameter in the preview.

---

## 1. Semantics (decisions to confirm before Phase 1)

1. **Float-only v1.** The existing driver popover refuses non-float destinations.
   Global variables are uniform CPU values; unlike the existing GPU spatial
   drivers they must be allowed on every float property with a valid binding
   address (not just the two composite shader slots). Int/bool/enum are later.
2. **Reuse the existing driver struct.** `FMixtormatParameterDriver` already has
   `Combine` (Replace/Multiply/Add/Subtract/Min/Max/Lerp), `Amount`,
   `InputMin/Max`, `OutputMin/Max`, `bInvert`. A variable source reuses all of
   it, CPU-side. `Replace` = "the parameter follows the variable";
   `Multiply` = "the parameter is modulated by the variable". Match
   `Shaders/Private/MixtormatDriver.ush` exactly: saturate signal, invert,
   normalize/clamp input window (zero span → zero), lerp output range, combine,
   then lerp authored→combined by saturated Amount. `Lerp` uses signal as
   its own weight. At Amount=0 every mode returns the authored value.
3. **A binding is either a reference or an enabled driver in the editor.**
   `SetDriverSource` / `SetDriverEnabled` disable the reference. Serialized
   bindings still hold both fields: runtime must explicitly choose how to
   handle malformed assets with both enabled (prefer existing reference
   behavior or document a new precedence; do not silently change it).
4. **Broken variable = driver contributes nothing.** A deleted variable leaves
   the authored value in place. This matches the existing driver rule
   (`MixtormatLayerGather.cpp` L68–74: "a Driver that cannot resolve contributes
   nothing and the parameter keeps its authored value").
5. **Variables are leaves.** No bindings on variables, so no cycle risk and no
   `Visiting` set interaction.
6. **Panel placement:** third tab in the left panel (LAYERS / LIBRARY /
   VARIABLES). Document-scope, sits beside the layer stack; `SMixtormatTabStrip`
   supports N options and `LeftSwitcher` takes another slot. Alternatives if
   rejected: bottom-library third column, or a floating window (theme-panel
   pattern).

---

## 2. Verified integration points

| Concern | Where | Notes |
|---|---|---|
| Document data | `MixtormatMaterial.h` — `UMixtormatMaterial` (Layers, LayerGroups, FinalSettings) | New `GlobalVariables` array lives here |
| Binding storage | `FMixtormatParameterBinding` (`MixtormatParameterTypes.h` L218) | Per layer/child; `Reference` + `Driver` |
| Driver struct | `FMixtormatParameterDriver` (L150) | Reused as-is + one new field |
| Driver source kinds | `EMixtormatDriverSourceKind` (L72) | Append `GlobalVariable` |
| CPU resolution | `ApplyDirectReferences` → `ApplyBindingSet` (`MixtormatParameterBinding.cpp` L498, L1173) | Currently applies references only; global-driver application is new |
| Compositor call site | `MixtormatGpuCompositor.cpp` L1545–1555 | `FMixtormatBindingScope{EffectiveLayers, Groups}` per layer |
| GPU driver gather | `MixtormatLayerGather.cpp` L68–115 | Filters spatial `CombinedMask` / `RegionIds` for **two fixed shader slots** (`RoughnessInfluence`, `HeightBlendAmount`); global drivers must be resolved on CPU, not added to those slots |
| Editor state | `SMixtormat.h` — `WorkingLayers`/`SavedLayers`, `WorkingLayerGroups`/`SavedLayerGroups`, `WorkingFinalSettings` | Mirror pattern for `WorkingVariables`/`SavedVariables` |
| Undo | `FEditHistoryState` (`SMixtormat.h` L157–165) | Snapshots Layers, Groups, bRotateUV90, FinalSettings — **must add variables** |
| Preview funnel | `FlushPendingPreviewRefresh` (`SMixtormat.cpp` L628–778) → `SetPreviewLayers` | Already builds `PreviewOverrideLayers` copies |
| Bake | `ExecuteBake` (`SMixtormat_Document.cpp` L493) → `ComposeLayersAtResolution` | Must pass variables |
| Save | `SaveWorkingMaterial` (`SMixtormat_Document.cpp` L248–298) | `MaterialAsset->Layers = WorkingLayers;` pattern |
| Driver UI | `BuildDriverSourceMenu` / `BuildParameterDriverPopover` (`SMixtormat_Parameters.cpp` L1675, L1799) | Add a "Global Variables" section |
| Editor binding lookup | `FindParameterBinding` (`SMixtormat_Parameters.cpp` L272) | No change needed |

---

## 3. UI style & theming

Rule (AGENTS.md §5): no local styling. Everything reads from
`MixtormatTokens` (`Style/MixtormatDesignTokens.h`) and
`FMixtormatThemeStore::GetResolved()`.

### Reused tokens (no new ones planned)

| Need | Token / source |
|---|---|
| Row height / gaps | `RowHeight`, `RowGap`, `SliderRowGap`, `LayerItemGap` |
| Panel header | `LayerStackHeaderHeight` (same header shape as the layer stack) |
| Insets | `LayerRowInsetLeading/Trailing`, `CardPadding`, `CardGap`, `PanelPadding` |
| Buttons / icons | `ButtonHeight`, `IconButtonSize`, `ToolbarLabelPadding` |
| Scroll | `ShellLayout.ScrollbarThickness` + `Mixtormat.ScrollBar` style |

### Reused style-set entries

`Mixtormat.Panel`, `Mixtormat.GroupButtonText`, `Mixtormat.LayerSource`,
`Mixtormat.MutedText`, `Mixtormat.PrimaryButton`, `Mixtormat.TopButton`,
`Mixtormat.ScrollBar`.

### New theme properties: none planned

The panel is standard rows. If a genuinely new metric appears (e.g. a variable
row height distinct from existing rows), it goes in a metrics struct in
`MixtormatTheme.h`, a `NUM(...)` entry in `MixtormatThemeSchema.cpp`
`BuildProperties()` with the correct refresh mode (`StyleRefresh` for
construction-cached row metrics, matching `LayerLayout.*`; `Paint` for colors),
and an authored value in `Config/UIStyleTheme.json`. Never a local constant.

### Driven-state visuals

Reuse the existing driven badge (`IsParameterDriven`) and the
`SMixtormatStatusDot` / `SMixtormatBadge` atoms. No new visual vocabulary.

### Theme reconstruct

The panel is rebuilt like every region. Its authored state (`WorkingVariables`)
lives on `SMixtormat`, so it survives. `TransferLayoutState` tracks scroll boxes
**by traversal index**, not by stable panel ID: a new scroll box can shift later
indices. Add the third tab carefully; test scroll restoration after a theme
reconstruct, and use stable IDs if the index-based transfer proves incorrect.

---

## 4. Component modularity & file architecture

### Module boundaries (one-way: Runtime ← Shaders ← Editor)

| Layer | Owns |
|---|---|
| Runtime | `FMixtormatGlobalVariable`, the array on `UMixtormatMaterial`, driver kind + `SourceVariableId`, `ApplyVariableDrivers`, `EnsureStableIds` |
| Shaders | Threading the variable array through the compositor (no shader changes) |
| Editor | Working/Saved state, history, panel, driver authoring UI, save/load |

Never include Editor from Runtime or Shaders.

### Widget decomposition rule (observed convention)

- Reusable atoms/rows/containers are their own `SCompoundWidget` classes under
  `UI/` (Atoms, Rows, Containers, Controls, Primitives) or `Widgets/<Area>/`.
- One-off panel content is `SMixtormat::Build*` member functions in the
  `SMixtormat_*.cpp` translation units (e.g. `BuildLayerStackPanel`,
  `BuildLayerRow` in `SMixtormat_Layers.cpp`).

For variables v1: panel + row are member builders in the new
`Widgets/SMixtormat_Variables.cpp`. Extract `UI/Variables/SMixtormatVariableRow.*`
as a widget class only if the row gets reused (e.g. inside the driver popover).

### State ownership

- `WorkingVariables` / `SavedVariables` on `SMixtormat` — document state.
- The panel is a stateless view; the runtime struct is the single source of
  truth for value/range/default.
- Naming: `FMixtormatGlobalVariable`, `EMixtormatDriverSourceKind::GlobalVariable`,
  `SMixtormat::BuildVariablesPanel`, file `SMixtormat_Variables.cpp`.
- Copyright header + tabs; `LOCTEXT_NAMESPACE "SMixtormat"`.

---

## 5. Reuse inventory

| Need | Reuse | Where |
|---|---|---|
| Slider row | `SMixtormatSlider` + `MixtormatRow::MakePair` | `UI/Controls/SMixtormatSlider.*`, `UI/Rows/SMixtormatRow.*` |
| Middle-click reset to default | `SMixtormatSlider::OnMouseButtonDown` | `SMixtormatSlider.cpp` L213 |
| Backspace reset | `NumericResetBindings` + `ResetHoveredNumericControl` (requires registration; not automatic for a directly built slider) | `SMixtormat.cpp` L473 |
| Rename | `FMixtormatEntryCommit` for commit/cancel; existing F2/F12 `BeginRenameSelection` targets layer/group only, so variables need explicit focus/selection wiring | `UI/Controls/MixtormatEntryCommit.*`, `SMixtormat.cpp` L549 |
| Context menu | `MixtormatMenu::FBuilder` | `SMixtormatInternal.h` L21 |
| Panel container | `SMixtormatInspectorGroup` / `SMixtormatInspectorCard` | `UI/Containers/` |
| Header + count row | Layer stack pattern | `SMixtormat_Layers.cpp` L261–282 |
| Empty state | Layer stack pattern | `SMixtormat_Layers.cpp` L231–259 |
| Footer actions | `SMixtormatGroupAction` | `SMixtormat_Layers.cpp` L314+ |
| Add button | `SMixtormatIconButton` + `MixtormatIcons::Add()` | `UI/Atoms/` |
| Third tab | `SMixtormatTabStrip` (N options already) | `UI/Controls/SMixtormatTabStrip.*` |
| Driven indicator | `IsParameterDriven` + `SMixtormatStatusDot` / `SMixtormatBadge` | `SMixtormat_Parameters.cpp` L384 |
| Stable IDs | `EnsureStableIds` | `MixtormatParameterBinding.cpp` L576 |
| Property-checked write | `WriteResolvedValue` pattern | `MixtormatParameterBinding.cpp` L450 |
| Theme values | `FMixtormatThemeStore::GetResolved()` + tokens | `Style/` |

**Not reusable:** the parameter slider helpers (`MakeSlider`,
`MakeMemberSlider`) bind to `FMixtormatParameterAddress`. The variable row binds
to `FMixtormatGlobalVariable` directly — reuse the underlying `SMixtormatSlider`
widget, not the parameter binding helpers.

---

## 6. Phase 1 — Runtime data model + resolution

### Data model

- New struct `FMixtormatGlobalVariable` in `MixtormatMaterial.h` (document-scope,
  next to `FMixtormatFinalSettings`):
  - `FGuid VariableId`
  - `FName Name` (display name; rename-safe because addressing is by ID)
  - `float Value`, `float Min = 0`, `float Max = 1`, `float Default = 0`
- `UMixtormatMaterial::GlobalVariables` — `TArray<FMixtormatGlobalVariable>`,
  `UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Variables")`.
- `EMixtormatDriverSourceKind::GlobalVariable` — **append only** (serialized by
  value).
- `FMixtormatParameterDriver::SourceVariableId` — new `FGuid` UPROPERTY
  (append; existing assets deserialize with an invalid GUID = no source).
- `FMixtormatBindingScope` / `FMixtormatMutableBindingScope`
  (`MixtormatParameterBinding.h` L19–66): add an optional
  `const TArray<FMixtormatGlobalVariable>* Variables = nullptr` with a
  three-argument constructor. Defaulted null keeps every existing construction
  compiling unchanged (same trick as `Groups`).
- Generate a stable `VariableId` when creating a variable; repair invalid or
  duplicate IDs on load, without altering valid IDs. Since `SourceVariableId`
  is a **separate field**, variables need not join the layer/group owner-ID
  namespace or `FMixtormatParameterAddress`. Avoid extending address owner
  enums unless variable-to-variable references are explicitly in scope.

### Resolution

- New runtime function in `MixtormatParameterBinding`:
  `ApplyVariableDrivers(const FMixtormatBindingScope&, FMixtormatLayer&)` — for
  each binding with `Driver.bEnabled && SourceKind == GlobalVariable`:
  1. Find the variable by `SourceVariableId`; missing → skip (authored value
     stays).
  2. Apply the exact spatial driver chain from `MixtormatDriver.ush`:
     saturate → invert → remap input → remap output → combine → wet/dry Amount.
  3. Combine with the authored value; `Replace` follows the variable only
     when Amount is 1 (Amount 0 preserves the authored value).
  4. Write into the transient copy via the existing `WriteResolvedValue`-style
     path (property-checked).
- Resolve at the existing compositor copy boundary, before layer hashing and
  gather. The result must not mutate `WorkingLayers` or source asset layers.
  Decide explicit precedence for the malformed both-enabled case above.
- For editor display, `GetEffectiveFloatParameter` currently calls
  `TryResolveFloat` on the authored scope, which follows references only;
  either show the authored slider value (and explain the driven preview) or
  compute a read-only effective value without changing slider reset/write
  semantics. Do not claim that this helper already displays driven results.

### Pipeline plumbing

Pass the variable array through the same channel as groups:

1. `SMixtormatPreviewViewport::SetPreviewLayers` + `ComposeLayersAtResolution`
   (`SMixtormatPreviewViewport.cpp` L602, L661) — new parameter.
2. `FMixtormatGpuCompositor::RequestCompose` (both overloads, L1376/L1387) +
   `RequestComposeInternal` (L1409) — new parameter, threaded into the
   `FMixtormatBindingScope` at L1545.
3. Extend `SMixtormatPreviewViewport::FPendingCompose`, `SubmitCompose`,
   `ComposeLayersWithDebug`, and `FlushPendingCompose` so deferred requests own a
   **copy** of their variable values, just as they copy Layers/Groups; never
   retain a pointer into mutable editor state across queued frames.
4. Call sites: `FlushPendingPreviewRefresh` (pass `WorkingVariables`),
   `ExecuteBake` (pass `WorkingVariables`), `PreviewSelectedSurfaceWithDisplacement`
   (empty array — library surface, no document), and the two internal
   reference-composition calls (L1604, L1622 — pass `Source->GlobalVariables`
   so a referenced composition's own variables drive its own layers).

Rejected alternative: resolving editor-side into `PreviewOverrideLayers` before
the viewport call. Fewer signatures, but two resolution points, bake needs its
own copy, and referenced compositions still need the compositor path. The
signature churn above is mechanical and compiler-enforced.

**No `.usf`/`.ush` edits needed**, provided the CPU implementation matches
`MixtormatDriver.ush` and resolves before gather. The two-slot spatial driver
path remains unchanged; do not route uniform globals through texture slots.

---

## 7. Phase 2 — Editor state + variables panel

### State

- `TArray<FMixtormatGlobalVariable> WorkingVariables` + `SavedVariables` on
  `SMixtormat` (`SMixtormat.h`).
- `FEditHistoryState` gains `TArray<FMixtormatGlobalVariable> Variables`;
  `RecordEditHistory`, `ApplyEditHistoryState`, `ResetEditHistory`,
  `IsCurrentStateSaved` updated. **Without this, undo corrupts the table.**
- Value changes call `RefreshLayeredPreview()` so existing active-timer and
  compose-in-flight coalescing applies; label/range edits refresh only if they
  change effective values. `RefreshLayeredPreview()` calls
  `SyncChildInstances()` on every edit and defers expensive history copies
  during interactive scrubs. Avoid rebuilding the panel per tick; bind the
  displayed slider value and rebuild rows only on add/remove/duplicate.

### Panel (GLOBAL page in shared left overlay)

- GLOBAL currently exists as the third `LeftSwitcher` page in `SMixtormat_Shell.cpp`.
  D31 moves that existing page into the shared Layers/Library/Global overlay; do not
  add another cell or splitter slot. Historical workspace handoff: `AgentDocs/old_docs/overlay-workspace-handoff.md` (archived).
- The existing active-page/rail state should continue to select GLOBAL; preserve the
  page widget and scroll state while the shared overlay is hidden or another page is active.
- New translation unit `Widgets/SMixtormat_Variables.cpp` (follows the
  `SMixtormat_*.cpp` convention; header declarations in `SMixtormat.h`).
- Row: name (rename via `FMixtormatEntryCommit`), slider (`SMixtormatSlider`),
  value readout. Middle-click reset to `Default` (existing slider convention).
- Header: add-variable button; empty state text when the table is empty.
- Row context menu (`MixtormatMenu::FBuilder`): Rename, Duplicate, Delete,
  Set Range (Min/Max), Set Default.
- Reuse: `MixtormatRow::MakePair`, `SMixtormatInspectorGroup`/`Card` for the
  container, `MixtormatDesignTokens` for spacing. No local styling.
- Delete: confirm only if the variable is referenced by any binding (scan
  bindings for `SourceVariableId`); otherwise delete silently.

---

## 8. Phase 3 — Driver authoring UI

- `BuildDriverSourceMenu` (`SMixtormat_Parameters.cpp` L1675): add a
  "Global Variables" section listing variables by name; selecting one calls a
  new `SetDriverVariableSource(FMixtormatParameterAddress Target, FGuid VariableId)`
  which sets `SourceKind = GlobalVariable`, `SourceVariableId`, clears
  `SourceLayerId`/`SourceChildId`, and enables the driver.
- `BuildParameterDriverPopover` (L1799): when the source is a variable, hide the
  spatial-only rows (`IdMapping`, `Seed`, `SpecificId`, `IdRangeMin/Max`,
  `IdRandomMin/Max`) and keep Combine, Amount, remap, Invert.
- `IsParameterDriven` checks `bEnabled` only: ensure missing variables show a
  broken-source state rather than appearing as a working connection. Update
  `SourceText` (currently scans layers only), `OutputText` (currently says Mask
  for any non-RegionIds source), `BuildDriverOutputMenu` and tooltip text; hide
  Output for a global source. Switching source back to spatial must clear the
  obsolete `SourceVariableId`; clearing the driver already resets the struct.
- Design decision: the parameter slider keeps showing the **authored** value
  (consistent with spatial drivers, which cannot be read back); the popover
  shows the variable's current value and the computed result. Clarify that
  CPU-resolved preview/bake output may differ from the authored slider value.

---

## 9. Phase 4 — Persistence, bake, imports

- `SaveWorkingMaterial` / `SaveWorkingMaterialAs` / `OpenWorkingMaterial` /
  `NewWorkingMaterial` / `StartNewMaterial`: copy the array and the `Saved*`
  mirror; update `CurrentHistoryState`.
- Bake: driven values resolve before gather, so baked outputs reflect the
  variables with no bake-side change beyond passing the array (Phase 1).
- Referenced compositions: handled by passing `Source->GlobalVariables` in the
  compositor's internal compose calls.
- "Add composition layers" import (`AddCompositionLayers`): **must preserve
  variable behavior in v1**. Either import referenced variables and remap
  `SourceVariableId` when inserting layers, or decline the import of bound
  layers with an actionable message. Silently dropping their effect is not
  acceptable. Copy/reference variants also need explicit policy and tests.
- `MixtormatCompositionReferences::Validate`: no change (variables add no
  composition cycles).

---

## 10. Files touched

| Module | File | Change |
|---|---|---|
| Runtime | `MixtormatMaterial.h` | `FMixtormatGlobalVariable`, `GlobalVariables` array |
| Runtime | `MixtormatParameterTypes.h` | Driver kind append, `SourceVariableId` |
| Runtime | `MixtormatParameterBinding.h/.cpp` | Scope extension, `ApplyVariableDrivers`, `EnsureStableIds` |
| Shaders | `MixtormatGpuCompositor.h/.cpp` | Thread variables through compose |
| Editor | `SMixtormatPreviewViewport.h/.cpp` | Thread variables through compose entry points **and pending request snapshots** |
| Editor | `SMixtormat.h` | Working/Saved state, history, panel + setter declarations |
| Editor | `SMixtormat.cpp` | History record/apply, preview funnel |
| Editor | `SMixtormat_Shell.cpp` | Existing GLOBAL page, now hosted by D31's shared left overlay |
| Editor | `SMixtormat_Variables.cpp` (new) | The panel |
| Editor | `SMixtormat_Parameters.cpp` | Driver source menu, setter, popover rows |
| Editor | `SMixtormat_Document.cpp` | Save/load/new, bake call |
| Shaders | — | none |

---

## 11. Risks

1. **Undo hazard** — `FEditHistoryState` must include the table (Phase 2), or
   undo silently reverts variables while layers keep their driven values.
2. **Enum discipline** — `EMixtormatDriverSourceKind` is serialized by value:
   append only. Same for any new owner type if one is ever added.
3. **Cache correctness/performance** — resolve into the copy before hashing
   (`MixtormatGpuCompositor.cpp` L1544–1561), so variable edits change the
   affected layer/prefix hash. `GatherLayerSourceCacheKey` is called after the
   same resolution. Reference compositor hashes its source UObject at L1583:
   verify the new UPROPERTY participates in that hash. Avoid hashing the
   entire variable table into every layer cache key; unchanged/unbound layers
   should retain their cache hits.
4. **Signature churn** — ~10 mechanical edit points; the compiler finds them all.
5. **Broken-variable fallback** must match the existing driver rule exactly
   (contribute nothing, keep authored value).
6. **Float-only** — the popover and `SetDriverSource` already refuse non-float;
   keep that gate for variable sources too.
7. **Theme reconstruct** — authored state survives; scroll restoration by
   tree index is fragile when inserting the new scroll box. Test or fix it.
8. **JSON is not bindings** — `Config/MixtormatParameterAuthoring.json`
   contains editor authoring defaults/ranges/snaps keyed by owner/property
   (e.g. `Effect.BreakupRelief`); actual per-document driver bindings are
   reflected UPROPERTY fields in layers/children and are saved in the
   `UMixtormatMaterial` asset. Variable value/range/default belong in the
   asset, NOT this JSON. Do not add `GlobalVariable` to the JSON unless
   exposing a new reflected parameter owner for developer authoring; v1
   deliberately does not. For driven destination values, retain existing
   authoring clamps/hard bounds and test out-of-range input behavior.

---

## 12. Verification checklist

- [ ] Create a variable; drive a layer Opacity with `Replace`; scrub the
      variable slider → preview updates live.
- [ ] `Multiply` combine at Amount=1: opacity = authored × remapped variable;
      at Amount=0: authored. `Replace`, `Lerp`, inverted and zero-width input
      windows match `MixtormatDriver.ush`.
- [ ] Save → reload → variables, ranges and drivers intact.
- [ ] Undo/redo across variable edits, driver edits and layer edits.
- [ ] Delete a driven variable → authored value returns, no crash.
- [ ] Bake reflects driven values.
- [ ] Referenced composition with its own variables drives its own layers.
- [ ] Theme reconstruct (UI STYLE panel) preserves the variables panel state
      and scroll position of every existing panel.
- [ ] Empty state and add/rename/duplicate flows.
- [ ] Variable drives a float generator/effect parameter (outside the two
      spatial composite slots); confirm cache invalidation on change.
- [ ] Rapid slider scrubbing: latest value wins, bounded compose queue, one
      undo step per drag; no per-tick panel rebuild.
- [ ] JSON authoring defaults/ranges and clamps on driven destinations remain
      unchanged; bindings and variable data persist in `.uasset`, not JSON.
- [ ] Import/copy/reference of compositions with variable-driven layers has
      no silent behavior loss.

---

## 13. Phases and effort

| Phase | Content | Size |
|---|---|---|
| 1 | Data model + CPU resolution + pipeline plumbing | Medium |
| 2 | Editor state, history, variables panel | Medium |
| 3 | Driver authoring UI | Small |
| 4 | Save/load, bake, imports, polish | Small–medium |

Suggested order: 1 → 2 → 3 → 4. Phase 1 is independently testable (resolve a
variable into a layer copy) before any UI exists.
