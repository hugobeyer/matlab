# Mixtormat Contextual Access / Viewport UX

## Goal

Add fast access to:

- sublayers
- generator modules
- scoped processors
- preview outputs
- viewport modes
- common actions

without turning Mixtormat into a large menu-heavy interface.

The interaction should be based primarily on:

**what is currently selected**

rather than exposing every available action at once.

---

# Main Recommendation

Use a **context-sensitive Spacebar search / action popup** as the primary fast-access system.

Do not begin with a radial marking menu.

Do not create a giant global command palette with hundreds of unrelated entries.

The popup should answer:

```text
What can I do to the thing I currently selected?
```

---

# Why Spacebar Fits Better Than RMB

The viewport currently already uses:

```text
LMB drag → orbit camera
RMB drag → rotate lighting
Mouse wheel → zoom
F → focus
Space / H → toggle overlay UI
Z → displacement
U / M → module preview cycle
V → channel preview cycle
Shift+V → return to Material
```

RMB is therefore already semantically occupied by viewport interaction.

A click-vs-drag RMB context menu is possible, but introduces:

- input ambiguity
- accidental menus during light rotation
- more gesture complexity
- harder discoverability
- poor scalability when module count grows

Recommendation:

**keep RMB for viewport interaction.**

Move the current Spacebar overlay toggle elsewhere, or retain `H` as the overlay shortcut and reserve Spacebar for contextual access.

---

# Interaction Model

Press:

```text
Space
```

Open a small popup near the cursor or centered over the viewport.

The search field is immediately focused.

The menu contents are filtered by selection.

No extra click should be required before typing.

Example:

```text
Space
type: ero
Enter
```

could resolve directly to:

```text
Add Gravity Erosion
```

when that action is valid for the current selection.

---

# Selection-Driven Behavior

## Generator layer selected

Show relevant root-level generator/module actions.

Example:

```text
Add to Selected
  Rock Formation
  Cliff Strata
  Cracks
  Height Blend
  Height Remap
  Color Ramp
  Noise
```

Do not show invalid material-layer-only or ID-only actions.

---

## Rock Formation selected

Show actions that make sense specifically after or under Rock Formation.

Example:

```text
Add to Selected
  Edge Thaw
  Jagged
  Shape Deform
  Generator Flow
  Flow Carve
  Noise

Preview Selected
  Height
  Region IDs
  Edge Distance
```

---

## Cliff Strata selected

Example:

```text
Add to Selected
  Gravity Erosion
  Edge Thaw
  Jagged
  Flow
  Noise
  Height Blend

Preview Selected
  Height
  Region IDs
  Block Seam
  Cavity
  Flow
```

Only show Flow once Cliff Strata actually publishes/supports it.

---

## Mask selected

Example:

```text
Add to Selected
  Blur
  Curvature
  Shaping

Preview Selected
  Mask
```

Do not show generator-only tools.

---

## ID child selected

Example:

```text
Add to Selected
  ID Group
  Boundary From IDs
  UV From IDs
  Relief From IDs

Preview Selected
  Region IDs
  Boundary
```

---

## Generic layer selected

Possible actions:

```text
Add to Selected
  Mask
  Effect
  Generator module where valid

View
  Material
  Base Color
  Normal
  Roughness
  Height
```

Again, filtered by layer type.

---

# Maximum Visible Complexity

The popup should intentionally remain small.

Recommended rule:

**approximately 8 visible rows maximum before search/filtering.**

Do not show every possible action at once.

If more actions are valid:

- rank them
- show the most important ones
- search reveals the rest
- optionally expose a single `More...` entry

Avoid multi-level cascading menus whenever possible.

---

# Maximum Sections

Use at most three visible sections.

Recommended:

```text
Add to Selected
Preview Selected
View
```

That is enough.

Avoid additional sections such as:

```text
Generators
Effects
Masks
Utilities
Debug
Diagnostics
Outputs
Viewport
Tools
Advanced
```

unless search is being used.

The selected object already provides most of the required categorization.

---

# Naming

Use short names.

Good:

```text
Edge Thaw
Noise
Flow
Height
IDs
Cavity
Normal
Material
```

Avoid:

```text
Add Generator Height Processing Edge Thaw Module
Preview Current Generator Region Identifier Output
```

The context already explains the operation.

---

# Icons

Use existing Mixtormat icon language.

Examples:

```text
Generator icon
Effect icon
Mask icon
IDs icon
Flow icon
Preview/output icon
```

Do not introduce labels solely to compensate for unclear icons.

Icons should support recognition, not replace the short text.

---

# Ranking

Recommended action ranking:

1. actions directly valid for the selected child
2. actions that can be inserted under / after the selected child
3. preview outputs of the selected child
4. actions valid for the selected owner/layer
5. general viewport actions

Recent actions may move upward only inside their valid context.

Do not let global recency make irrelevant commands appear above selected-context actions.

---

# Search Aliases

Each action can have hidden search terms.

Example:

```text
Display: Edge Thaw
Aliases:
  chamfer
  round
  bevel
  weather
  soften
```

Example:

```text
Display: Gravity Erosion
Aliases:
  erosion
  gravity
  runoff
  vertical
  sediment
```

Example:

```text
Display: Preview Region IDs
Aliases:
  ids
  regions
  id
  preview
```

This keeps visible labels short while making search forgiving.

---

# Suggested Internal Action Descriptor

Build one reusable contextual action registry.

Conceptually:

```text
Action
{
    Id
    Label
    Icon
    SearchAliases
    Section
    Priority
    CanExecute(context)
    Execute(context)
}
```

Context should contain enough information to know:

```text
selected layer
selected child
selected child type
scope owner
layer type
available published outputs
current preview state
```

Both future context menus and the Spacebar popup should consume this same registry.

Do not create a second independent child-creation system.

---

# Reuse Existing Creation Logic

The contextual system should call existing functions such as:

```text
CreateChild(...)
CanCreateChild(...)
CanAddGeneratorModule(...)
CanAddGeneratorFlow(...)
AddGeneratorFlow(...)
existing preview functions
```

Do not duplicate:

- insertion rules
- scope rules
- enable/disable rules
- child initialization
- selection handling
- history recording
- preview refresh

The popup is only a new access surface.

---

# Reuse Existing Preview Logic

Current viewport preview actions already include:

```text
CycleModulePreview()
CycleChannelPreview()
ResetChannelPreview()
```

and there is already child output capability metadata.

The contextual popup should use those existing preview/output descriptions wherever possible.

Do not create a separate hard-coded list of outputs per generator inside the popup.

Preferred source:

```text
GetChildCapabilities(...)
```

or a shared action-building layer using the same metadata.

---

# Preview UX

Do not list every published field by default.

For each selected module, show only the most useful 2–4 outputs.

Example Rock Formation:

```text
Height
IDs
Edge Distance
```

Possible hidden/searchable extras:

```text
Top
Chamfer
Wall
Gap
Top Ramp
Chamfer Ramp
Wall Ramp
```

Example Cliff Strata:

```text
Height
IDs
Cavity
Flow
```

Other outputs remain searchable.

This keeps the initial popup readable.

---

# View Actions

Keep global viewport modes small.

Recommended visible defaults:

```text
Material
Height
Normal
```

Other channel previews remain searchable:

```text
Base Color
Roughness
AO
Metallic
F0
Fuzz
```

Do not make the initial popup a full debug-channel list.

---

# Search Examples

Examples of intended use:

```text
Space
"noise"
Enter
```

```text
Space
"thaw"
Enter
```

```text
Space
"ids"
Enter
```

```text
Space
"height"
Enter
```

```text
Space
"flow"
Enter
```

```text
Space
"material"
Enter
```

The user should not need to remember where an item lives in a submenu.

---

# Current Shortcut Conflict

At the audited state:

```text
Spacebar OR H
```

toggles overlay UI.

Recommendation:

```text
H → toggle overlays
Space → contextual action popup
```

This is a cleaner division:

```text
H = Hide/show UI
Space = Do something
```

Do not leave both Space and H bound to overlays if Space becomes the main contextual-access key.

---

# RMB Recommendation

Do not make RMB the primary access method.

Possible future optional behavior:

```text
RMB click without drag → tiny contextual menu
RMB drag → lighting rotation
```

But only implement this if there is a strong need later.

If added, it should reuse the same contextual action registry and show an even smaller action subset.

Example:

```text
Preview
Add
Focus
```

No separate menu architecture.

---

# Marking Menu Recommendation

Do not implement a radial marking menu in the first pass.

Reasons:

- harder to read
- harder to search
- poor scaling
- introduces muscle-memory burden
- radial slots become unstable as context changes
- context-dependent action availability would move items around
- more UI work than value for current Mixtormat scale

A marking menu could later be added for 4–8 extremely stable expert actions, but it should not become the main module browser.

---

# Readability Rules

The popup should follow these constraints:

```text
Max 3 sections
~8 visible rows
Short labels
One icon per row
No deep nesting
No duplicated commands
No disabled clutter
Invalid actions hidden
Search always focused
Selection-specific results first
```

This is the key part of the UX.

The goal is not to expose everything.

The goal is to expose the **next sensible action**.

---

# Suggested First-Pass UI

Example with Rock Formation selected:

```text
┌─────────────────────────────┐
│ Search actions...           │
├─────────────────────────────┤
│ ADD TO SELECTED             │
│  Edge Thaw                  │
│  Flow                       │
│  Flow Carve                 │
│  Noise                      │
├─────────────────────────────┤
│ PREVIEW SELECTED            │
│  Height                     │
│  IDs                        │
│  Edge Distance              │
├─────────────────────────────┤
│ VIEW                        │
│  Material                   │
└─────────────────────────────┘
```

No giant descriptions.

No secondary labels unless genuinely necessary.

---

# Suggested First-Pass Implementation Scope

## Phase 1

Create contextual action descriptors.

Sources:

- selected layer/child
- existing creation functions
- existing child capabilities
- existing preview functions

## Phase 2

Create compact searchable popup.

Requirements:

- keyboard focus on open
- fuzzy/simple text filtering
- arrow-key navigation
- Enter executes
- Escape closes
- mouse click works
- max readable height

## Phase 3

Move:

```text
Space → popup
H → overlay visibility
```

## Phase 4

Add contextual ranking.

## Phase 5

Optional:

- recent actions
- tiny RMB click menu
- favorite actions

Do not implement these before the core popup feels correct.

---

# Relevant Existing Files

## Viewport input

- `Source/MixtormatEditor/Private/Widgets/SMixtormatPreviewViewport.cpp`
- `Source/MixtormatEditor/Private/Widgets/SMixtormatPreviewViewport.h`

Current important input handling lives in `FMixtormatPreviewViewportClient::InputKey()` and `InputAxis()`.

## Creation menus

- `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerMenus.cpp`

Relevant existing functions include:

- `BuildAddGeneratorsMenu(...)`
- generator flow menu construction
- mask/effect add menus

## Child creation / validation

- `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerChildren.cpp`

Relevant existing concepts include:

- `CreateChild(...)`
- `CanCreateChild(...)`
- `CanAddGeneratorModule(...)`
- selected-child resolution
- scoped-child insertion

## Preview output capabilities

- `Source/MixtormatEditor/Private/Widgets/MixtormatChildCapabilities.cpp`

Use this as the source of truth for previewable outputs where possible.

---

# Target End State

The interaction should feel like:

```text
Select something
→ press Space
→ see only actions that make sense for it
→ type a few letters if needed
→ Enter
```

The user should not have to remember:

- which submenu contains a processor
- which generator supports which flow tool
- which output is previewable
- where a module is categorized

Mixtormat should infer that from the current selection.

The system should stay compact enough that opening it does not feel like opening an editor-wide command palette.
