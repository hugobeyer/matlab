# Icons

The Mixtormat icon set: what exists, where it lives, how to add one, and what is
still missing. Icons are **owned PNGs** — never `FAppStyle` glyphs (the
chevrons were once borrowed and changed weight with the editor theme; that
lesson is why this set exists).

## Where things live

| Thing | Location |
|---|---|
| Wrapper API — widgets call this | `Source/MixtormatEditor/Private/UI/Atoms/MixtormatIcons.h` / `.cpp` |
| Brush registration | `Style/MixtormatStyle.cpp` — `SetPngIcon` / `SetBrandArtwork` |
| Art | `Resources/Icons/*.png` — 64 px, white-on-transparent, sliced from one sheet at a uniform stroke weight |
| Brand art | `Resources/Icons/mixtormat-icon.svg`, `mixtormat-logo.svg` — vector, aspect-ratio locked |
| Per-role sizing/opacity | `EMixtormatIconRole` + `FMixtormatIconStyle` (`MixtormatTheme.h`), theme-authored |
| Brush sizes | `ControlLayout.IconBrushSize` (20), `IconBrushSizeLarge` (28), `ScalarRampIconSize` |

Rules:

- Widgets use `MixtormatIcons::Name()`; never `GetBrush(TEXT("Mixtormat.Icon..."))`.
- Style key format `Mixtormat.Icon.<PascalName>`; art file `Icons/<kebab-name>.png`.
- PNGs are white so the brush tint colours them.
- `Resources/Icons/*.png` is excluded from agent file scanning
  (`.zed/settings.json`); to see the art, open the folder or read the
  registration block in `MixtormatStyle.cpp`.

## Current set (57 wrappers + brand)

### Shell / top bar / general

| Wrapper | Key | PNG |
|---|---|---|
| `Save` | `Mixtormat.Icon.Save` | `save` |
| `SaveAs` | `Mixtormat.Icon.SaveAs` | `save-as` |
| `Settings` | `Mixtormat.Icon.Settings` | `settings` |
| `Add` | `Mixtormat.Icon.Add` | `add` |
| `Folder` | `Mixtormat.Icon.Folder` | `folder` — also the LOAD button glyph |
| `Documentation` | `Mixtormat.Icon.Documentation` | `documentation` |
| `Feedback` | `Mixtormat.Icon.Feedback` | `feedback` |
| `Search` | `Mixtormat.Icon.Search` | `search` |
| `Refresh` | `Mixtormat.Icon.Refresh` | `refresh` |
| `Overflow` | `Mixtormat.Icon.Overflow` | `overflow` |
| `Grip` | `Mixtormat.Icon.Grip` | `grip` |
| `ArrowUp` | `Mixtormat.Icon.ArrowUp` | `arrow-up` |
| `ArrowDown` | `Mixtormat.Icon.ArrowDown` | `arrow-down` |

### Viewport

| Wrapper | Key | PNG |
|---|---|---|
| `Cube` | `Mixtormat.Icon.Cube` | `cube` |
| `Sphere` | `Mixtormat.Icon.Sphere` | `sphere` |
| `Plane` | `Mixtormat.Icon.Plane` | `plane` |
| `Cylinder` | `Mixtormat.Icon.Cylinder` | `cylinder` |
| `Globe` | `Mixtormat.Icon.Globe` | `globe` |
| `Nodes` | `Mixtormat.Icon.Nodes` | `nodes` |
| `Camera` | `Mixtormat.Icon.Camera` | `camera` |
| `LightNeutral` / `LightSoft` / `LightDramatic` / `LightRim` | `Mixtormat.Icon.Light*` | `light-neutral` / `light-soft` / `light-dramatic` / `light-rim` |
| `QualityLow` / `QualityMedium` / `QualityHigh` | `Mixtormat.Icon.Quality*` | `quality-low` / `quality-medium` / `quality-high` |

### Layer stack

| Wrapper | Key | PNG |
|---|---|---|
| `Eye` / `EyeOff` | `Mixtormat.Icon.Eye` / `.EyeOff` | `eye` / `eye-off` |
| `ChevronDown` | `Mixtormat.Icon.ChevronDown` | `chevron-down` — disclosure open; gallery collapse |
| `ChevronRight` | `Mixtormat.Icon.ChevronRight` | `chevron-right` — disclosure shut |
| `Mask` | `Mixtormat.Icon.Mask` | `mask` |
| `Effect` | `Mixtormat.Icon.Effect` | `effect` — bolt |
| `Generator` | `Mixtormat.Icon.Generator` | `generator` — mountain |
| `Generated` | `Mixtormat.Icon.Generated` | `generated` — shoot |
| `Ids` | `Mixtormat.Icon.Ids` | `ids` — cluster; distinct from `Generated` |
| `LayerMaterial` | `Mixtormat.Icon.LayerMaterial` | `layer-material` — square |
| `LayerFill` | `Mixtormat.Icon.LayerFill` | `layer-fill` — circle |
| `Duplicate` | `Mixtormat.Icon.Duplicate` | `duplicate` |
| `Trash` | `Mixtormat.Icon.Trash` | `trash` |
| `Check` | `Mixtormat.Icon.Check` | `check` — menu icon-gutter tick |

### Hierarchy / indentation

| Wrapper | Key | PNG |
|---|---|---|
| `ChevronUp` / `ChevronDownBold` | `Mixtormat.Icon.ChevronUp` / `.ChevronDownBold` | `chevron-up` / `chevron-down-bold` |
| `HierarchyRoot` | `Mixtormat.Icon.HierarchyRoot` | `hierarchy-root` |
| `Indent1` / `Indent2` / `Indent3` | `Mixtormat.Icon.Indent*` | `indent-1` / `indent-2` / `indent-3` |
| `TreeElbow` | `Mixtormat.Icon.TreeElbow` | `tree-elbow` |
| `TreeBranchDotted` | `Mixtormat.Icon.TreeBranchDotted` | `tree-branch-dotted` |
| `TreeTee` | `Mixtormat.Icon.TreeTee` | `tree-tee` |
| `TreeCross` | `Mixtormat.Icon.TreeCross` | `tree-cross` |

### Scalar ramp

| Wrapper | Key | PNG |
|---|---|---|
| `ScalarRampConstant` | `Mixtormat.Icon.ScalarRampConstant` | `ramp-constant` |
| `ScalarRampLinear` | `Mixtormat.Icon.ScalarRampLinear` | `ramp-linear` |
| `ScalarRampSpline` | `Mixtormat.Icon.ScalarRampSpline` | `ramp-spline` |
| `ScalarRampBSpline` | `Mixtormat.Icon.ScalarRampBSpline` | `ramp-bspline` |
| `ScalarRampFrame` | `Mixtormat.Icon.ScalarRampFrame` | `ramp-frame` |
| `ScalarRampReset` | `Mixtormat.Icon.ScalarRampReset` | `ramp-reset` |

### Brand (vector SVG — different registration)

| Key | File | Notes |
|---|---|---|
| `Mixtormat.Brand.Icon` | `Icons/mixtormat-icon.svg` | source art 53.46 × 58.07, ratio preserved |
| `Mixtormat.Brand.Logo` | `Icons/mixtormat-logo.svg` | source art 297.14 × 58.07 |
| Viewport watermark | (same icon SVG, tinted muted) | registered in `MixtormatStyle.cpp` |

## Icon roles (`EMixtormatIconRole`, `MixtormatTheme.h`)

Roles are the closed set that sizes/opacifies glyphs per location — append-only:

`TopBar`, `PanelToolbar`, `PreviewToolbar`, `LayerEye`, `LayerDisclosure`,
`FoldoutDisclosure`, `CardLeading`, `Menu`, `GalleryToolbar`.

Each role authors `GlyphSize`, `ButtonSize`, `HitSize`, `RestOpacity`,
`HoverOpacity`, `DisabledOpacity`.

## Missing — to add later

Requested (bring the art over when ready):

| Name | Suggested file | Why |
|---|---|---|
| `Layers` | `layers.png` | LAYERS left-tab glyph |
| `Library` | `library.png` | LIBRARY left-tab glyph |
| `Global` | `global.png` | GLOBAL (variables) left-tab glyph |
| `Squircle` | `squircle.png` | brand mark tile / rounded-square app frame (confirm exact shape intent) |
| `Close` | `close.png` | X — panel/window close (theme panel, inspector overlay) |
| `Minimize` | `minimize.png` | − — panel/window minimize (theme panel chrome) |

Suggested, from gaps found while working on layout (add only if wanted):

| Name | Suggested file | Why |
|---|---|---|
| `ChevronLeft` | `chevron-left.png` | only Up/Down/Right exist; needed for leftward collapse directions |
| `Pin` | `pin.png` | inspector overlay pin (placement cycle, D15) |
| `Dock` | `dock.png` | dock-right action in the inspector placement control |
| `VariableLink` | `variable-link.png` | a parameter driven by a global variable (globals plan) |
| `VariableUnlink` | `variable-unlink.png` | break the variable link |

## How to add one

1. Art: 64 px white-on-transparent PNG → `Resources/Icons/<kebab>.png`; match
   the sheet's stroke weight (do not resize an existing glyph up).
2. Register in `MixtormatStyle.cpp::Refresh()`:
   `SetPngIcon(TEXT("Mixtormat.Icon.<Name>"), TEXT("Icons/<kebab>"), FVector2D(IconBrushSize, IconBrushSize));`
   — vector brand art uses `SetBrandArtwork` instead.
3. Declare + define the wrapper in `MixtormatIcons.h` / `.cpp` (one line each).
4. Use the wrapper in widgets; pick the matching `EMixtormatIconRole` for
   size/opacity. If no role fits, append a role (never reorder), add theme
   defaults + a schema entry.
5. Keys and files are append-only in spirit: a removal means updating every
   callsite, so grep the key first.
