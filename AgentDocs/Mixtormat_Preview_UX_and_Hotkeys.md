# Mixtormat — Preview UX / Hotkey Cleanup

> Planning note based on current `main`. Keep implementation source-driven.

## Current hotkey ownership

Hotkeys are **not centralized in one command table** today.

### Main editor widget — `SMixtormat::OnKeyDown`
- `I` — toggle Region ID child-output preview when the selected child exposes Region IDs.
- `F2` / `F12` — rename current layer/group selection.
- `Ctrl/Cmd+Z` — undo; `Ctrl/Cmd+Shift+Z` — redo.
- `Ctrl/Cmd+Y` — redo.
- `G` — toggle bottom gallery.
- `Backspace` — reset hovered numeric control.

### Preview viewport client — `SMixtormatPreviewViewport::InputKey`
- `F` — frame/focus preview mesh.
- `Space` / `H` — toggle viewport overlay UI.
- `Z` — toggle displacement.
- `U` / `M` — cycle selected-module preview.
- `V` — cycle raw material channel preview.
- `Shift+V` — return raw channel preview to Material.
- Mouse wheel / mouse input — camera interaction.

### Preview capture — `SMixtormat::OnPreviewKeyDown`
- `Shift+V` — globally escape debug/channel preview and return to material.
- `Backspace` — reset hovered numeric control.

This split is currently intentional in places: viewport-camera shortcuts live in the viewport client while editor/document shortcuts live in `SMixtormat`. However, discoverability and preview-state ownership should be unified.

## Bugs to fix

### Blend-mode / enum preview invalidation
Changing a mask blend mode such as Replace/Add/Multiply can leave the active preview stale/broken until another slider change forces refresh.

Audit enum/dropdown writes generally, not only mask Blend Mode. Enum writes that affect composition must trigger the same preview/compositor invalidation path as numeric parameter edits.

### Stale preview after selection changes
A child/module preview can remain active after selecting another child, layer, group or material.

Debug preview should be selection-owned. If the active preview target is no longer valid for the current selection, clear it and return to Material/Final rather than displaying stale output.

## Preview interaction direction

Use one coherent selected-module preview model:

`selected child -> published/preview capabilities -> available outputs -> active preview output`

Suggested final gestures:
- `P` — cycle available previews for the selected module.
- `Shift+P` — cycle backward.
- `I` — Region IDs from the selected/nearest valid ID-producing chain.
- `X` — exit module/debug preview and return to Final/Material.

Before adopting these keys, reconcile/remove the temporary `U/M` module-preview aliases and ensure `X/P` do not conflict with editor text/numeric interactions. Do not add another independent preview state.

## Bottom-center viewport helper

Add a lightweight Blender/Houdini-style status hint at the bottom-center of the viewport, above the Gallery divider.

Examples:

`GENERATED MASK · MASK    P Next    Shift+P Previous    X Exit`

`ROCK FORMATION · REGION IDs    P Next    I IDs    X Exit`

Requirements:
- active module/output in normal/bright text;
- shortcuts in muted text;
- no large panel; optional subtle backing only for readability;
- only show relevant actions for the current selection/output;
- hidden when viewport overlay UI is hidden if that remains the established overlay behavior.

The viewport already exposes `GetPreviewModeLabel()` and currently shows a preview label above FOV. Reuse the same source of truth rather than creating another label state. The existing global `WorkingStatusText` is useful for transient messages but should not be the sole preview-mode indicator.

## Architecture follow-up

Preview refresh/state cleanup should remain priority 7 in the current plan, but this work should include:
1. central preview-state ownership;
2. selection-change invalidation;
3. consistent parameter/enum refresh;
4. capability-driven preview cycling;
5. Region-ID shortcut behavior;
6. bottom-center preview/help HUD;
7. removal of temporary/duplicate preview hotkeys once the final mapping is chosen.

A future centralized command/shortcut registry is reasonable for discoverability, but viewport camera input does not need to be forced into the same implementation mechanism as document commands. The important part is one documented key map and one preview-state model.
