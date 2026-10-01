# Step 4: Freeze up to here

Paste `00-Shared-Rules.md` above this. Prerequisite: Steps 1–3 are merged.

## Goal
Hugo can freeze the stack at layer N:
- Layers 0..N are composited once and cached.
- Those layers are locked: read-only, greyed out, with a lock icon.
- Every later edit only re-runs layers N+1 and up.
- Unfreeze restores normal editing.

## Why "up to here", and not one layer
A single layer reads what's below it: the height op, Composite Below sources, drivers, effects on the finished stack, and Strata Height Follow. Freezing it alone would go stale whenever something below changed. Freezing a prefix is always correct, because nothing below the freeze can change.

## Reuse what exists
- `FMixtormatPrefixCache` and `SavePrefixSnapshot` in `MixtormatGpuComposePipeline.cpp` already snapshot a layer prefix: BaseColor, Normal, RAM, Height, published outputs, and anything layers above can read. They're currently automatic, keyed by hash, and limited by a budget.
- Build Freeze on top of that: an explicit, pinned prefix entry that ignores the budget and the hash-driven invalidation while it's frozen.
- **Snapshot contents:** everything a layer above can read, including the Step 1 occupancy map, Step 3b `PublishedIds`, `PublishedMaskOutputs`, height snapshots, driver snapshots and the ridge targets. Check `SavePrefixSnapshot` for anything it already refuses to snapshot (ping-pong slots), and handle or report it.

## Data and UI
- Asset property `int32 FrozenThroughLayer = INDEX_NONE`. Decide whether to store it on the asset (it survives a save, but the cache doesn't) or keep it editor-session only. Recommend editor-session only, and report the choice.
- **Layer list:** a lock/freeze toggle on each layer row ("Freeze up to here"). Rows at or below N are greyed and show a lock.
- **Inspector:** for frozen layers, every widget is disabled, with a header banner reading "Frozen — Unfreeze to edit" and an Unfreeze button.
- **Block every edit path** to a frozen layer: inspector, drag-reorder into or out of the frozen range, delete, the add-child menu, drivers targeting a frozen layer's values, and parameter bindings.
- **Resolution:** the snapshot is tied to the composite resolution. If the preview or bake resolution changes, re-freeze automatically by recompositing the prefix once, and say so in a toast/log.
- **Memory:** report the snapshot's memory at 1K, 2K and 4K in the report (about 300 MB at 4K).

## Static checks
- Use dxc if any shader is touched; this step is probably C++ only.
- Grep that every mutation entry point checks the frozen state.

## Report and checklist for Hugo
- Report in terse bullets.
- Checklist:
  - Freeze at layer 3: editing layer 5 is fast, and layers 0..3 can't be edited.
  - Unfreeze: everything is editable again, and the result is identical.
  - Change the preview resolution while frozen: it re-freezes correctly.
  - Bake while frozen: the output matches an unfrozen bake.
