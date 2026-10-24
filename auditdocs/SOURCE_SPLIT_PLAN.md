# Source split priorities

2026-10-23 · Optional refactors, not release requirements. Source outlines reviewed only.
Paths below are relative to `Source/`.

| Priority | File | Useful split |
|---|---|---|
| 1 | `MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorGenerators.cpp` | Extract Behavior controls and source menus. |
| 2 | `MixtormatEditor/Private/Widgets/SMixtormat_Parameters.cpp` | Separate driver UI from references and authoring panels. |
| 3 | `MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorMasks.cpp` | Extract Blur controls/source selection if needed. |

Start with priority 1; defer the others until they help active work.
Preserve behavior and existing APIs. No splits implemented or tests run.
GPU-pass splitting is already underway; avoid overlapping GPU/compositor changes.
