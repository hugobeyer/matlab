# Step 9: UI backlog, layer list and previews (side task)

Paste `00-Shared-Rules.md` above this. It's an editor-only task and can run alongside any step. Don't touch compositor maths except where item 5 needs the ID Group preview wired.

**Never delete a user-facing feature unless this prompt names it.**

## Where to look
- `Source/MixtormatEditor/Private/Widgets/SMixtormat_Layers.cpp`: rows, `BuildLayerRow`, the child rows, `IsSourceOfSelectedInstance`, hotkeys.
- `Source/MixtormatEditor/Private/UI/Layers/*`: `SMixtormatLayerRow`, the child-row widgets, badges.
- `Source/MixtormatEditor/Private/Widgets/SMixtormat_Preview.cpp`: `MakeChildOutputPreviewButton`, `GetChildPreviewOutputSet`, the debug preview state.
- `Source/MixtormatEditor/Private/Widgets/MixtormatChildCapabilities.cpp`: what each child can preview.
- Preview targeting in the compositor: `IsChildOutputPreviewTarget`, `PublishRegionIds`, and the ID Group passes in `MixtormatGpuPatternPasses.cpp`.
- Theme tokens: `MixtormatTokens`. Use these for every colour; no hard-coded colours.

## Items

### 1. First child row is not indented (bug)
- The first child under a layer sits flush with the layer; the following children indent correctly.
- Hugo's screenshot: Copper › **ID Group** sits flush, while its own children (Surface IDs, Pattern IDs) indent.
- Find the off-by-one or first-row special case in the depth/indent logic, and fix it so every child row indents by its depth.

### 2. ID Group cannot be previewed (bug)
- Neither the eye nor the **I** hotkey shows an ID Group's composed Region IDs, even with Surface IDs and Pattern IDs inside it.
- Check, in order:
  1. The ID Group capability row: its Region IDs output must be previewable and primary.
  2. That the group publishes its composed map under **its own** child index (`PublishRegionIds`), including for nested groups, rather than only its inner producers' maps.
  3. That `IsChildOutputPreviewTarget` matches the group's index, and that the region-ID preview blit runs for it.
  4. That the ID Group panel header has the eye button.
- Zero, one, multiple and nested producers must all preview.

### 3. I hotkey on instances and ID references
- When the selected child is an **instance** of an ID producer, or an **ID reference**, I must preview the IDs it resolves to: the source's map, the same as pressing I on the source.
- Resolve through the instance or reference to its source before choosing the preview target. The eye button on such a row should do the same.

### 4. Orange dot while previewing
- A row's circle dot (layer or child) turns **orange** while a debug or child-output preview is being shown from it.
- Source of truth: the current debug settings (layer index, child index, mode).
- It returns to normal as soon as the preview is off. Use a theme token, adding an accent token if none fits.

### 5. Instance → source highlight
- When an **instance** child is selected, make its source obvious. Pick the clearest of these, or combine them:
  - a highlight or gradient glow tint on the source row;
  - a glow or trace line along the hierarchy's outer left edge, from the selected instance back up to its source row.
- It must read at a glance, even with the source many rows away, and work across layers if the source is in another layer.
- Use theme tokens. It clears when the selection changes.

## Static checks
- Grep that every new widget state reads the existing selection and debug state, with no duplicated bookkeeping.
- dxc only if a shader changes; this task probably touches no shaders.

## Checklist for Hugo
- Every child row, including the first, indents by its depth.
- ID Group previews with eye and I: one producer, two producers, and nested.
- Selecting an instance or ID reference and pressing I shows the source's IDs.
- The row dot is orange only while that row's preview is on.
- Selecting an instance makes its source row obvious (glow or trace), and it clears on deselect.
