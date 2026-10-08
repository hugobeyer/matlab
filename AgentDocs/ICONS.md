# Icons

The Mixtormat icon set: what exists, where it lives, how to add one, and what is
still missing. Icons are **owned PNGs** — never `FAppStyle` glyphs (the
chevrons were once borrowed and changed weight with the editor theme; that
lesson is why this set exists).

## Where things live

- Wrapper API — widgets call this: `UI/Atoms/MixtormatIcons.h` / `.cpp`
- Brush registration: `Style/MixtormatStyle.cpp` — `SetPngIcon` / `SetBrandArtwork`
- Art: `Resources/Icons/*.png` — 64 px, white-on-transparent, one sheet, uniform stroke weight
- Brand art: `Resources/Icons/mixtormat-icon.svg`, `mixtormat-logo.svg` — vector, ratio-locked
- Per-role sizing/opacity: `EMixtormatIconRole` + `FMixtormatIconStyle` (`MixtormatTheme.h`)
- Brush sizes: `ControlLayout.IconBrushSize` (20), `IconBrushSizeLarge` (28), `ScalarRampIconSize`

## Rules

- Widgets use `MixtormatIcons::Name()`; never `GetBrush(TEXT("Mixtormat.Icon..."))`.
- Key format: `Mixtormat.Icon.<WrapperName>`; art file: `Icons/<kebab-name>.png`.
- PNGs are white so the brush tint colours them.
- `Resources/Icons/*.png` is excluded from agent file scanning
  (`.zed/settings.json`) — to see the art, open the folder or read the
  registration block in `MixtormatStyle.cpp`.

## Current set — 57 wrappers

Keys follow the rule above; only the PNG file is listed beside each wrapper.

### Shell / top bar / general

- `Save` · `save.png`
- `SaveAs` · `save-as.png`
- `Settings` · `settings.png`
- `Add` · `add.png`
- `Folder` · `folder.png` — also the LOAD button glyph
- `Documentation` · `documentation.png`
- `Feedback` · `feedback.png`
- `Search` · `search.png`
- `Refresh` · `refresh.png`
- `Overflow` · `overflow.png`
- `Grip` · `grip.png`
- `ArrowUp` · `arrow-up.png`
- `ArrowDown` · `arrow-down.png`

### Viewport

- `Cube` · `cube.png`
- `Sphere` · `sphere.png`
- `Plane` · `plane.png`
- `Cylinder` · `cylinder.png`
- `Globe` · `globe.png`
- `Nodes` · `nodes.png`
- `Camera` · `camera.png`
- `LightNeutral` · `light-neutral.png`
- `LightSoft` · `light-soft.png`
- `LightDramatic` · `light-dramatic.png`
- `LightRim` · `light-rim.png`
- `QualityLow` · `quality-low.png`
- `QualityMedium` · `quality-medium.png`
- `QualityHigh` · `quality-high.png`

### Layer stack

- `Eye` · `eye.png`
- `EyeOff` · `eye-off.png`
- `ChevronDown` · `chevron-down.png` — disclosure open; gallery collapse
- `ChevronRight` · `chevron-right.png` — disclosure shut
- `Mask` · `mask.png`
- `Effect` · `effect.png` — bolt
- `Generator` · `generator.png` — mountain
- `Generated` · `generated.png` — shoot
- `Ids` · `ids.png` — cluster; distinct from `Generated`
- `LayerMaterial` · `layer-material.png` — square
- `LayerFill` · `layer-fill.png` — circle
- `Duplicate` · `duplicate.png`
- `Trash` · `trash.png`
- `Check` · `check.png` — menu icon-gutter tick

### Hierarchy / indentation

- `ChevronUp` · `chevron-up.png`
- `ChevronDownBold` · `chevron-down-bold.png`
- `HierarchyRoot` · `hierarchy-root.png`
Tree/indent brushes are no longer registered or exposed by `MixtormatIcons`.
`SMixtormatLayerHierarchy` paints the active hierarchy rails and branches from theme metrics.
The unused brush-based row argument, lookup helper and legacy connector widget were removed;
existing image assets remain on disk. No tree/indent SVG replacements are needed.

### Scalar ramp

- `ScalarRampConstant` · `ramp-constant.png`
- `ScalarRampLinear` · `ramp-linear.png`
- `ScalarRampSpline` · `ramp-spline.png`
- `ScalarRampBSpline` · `ramp-bspline.png`
- `ScalarRampFrame` · `ramp-frame.png`
- `ScalarRampReset` · `ramp-reset.png`

### Brand (vector SVG — different registration)

- `Mixtormat.Brand.Icon` · `mixtormat-icon.svg` — source art 53.46 × 58.07, ratio preserved
- `Mixtormat.Brand.Logo` · `mixtormat-logo.svg` — source art 297.14 × 58.07
- Viewport watermark — same icon SVG, tinted muted, registered in `MixtormatStyle.cpp`

## Icon roles (`EMixtormatIconRole`, `MixtormatTheme.h`)

Closed set, append-only — each role authors `GlyphSize`, `ButtonSize`,
`HitSize`, `RestOpacity`, `HoverOpacity`, `DisabledOpacity`:

- `TopBar`
- `PanelToolbar`
- `PreviewToolbar`
- `LayerEye`
- `LayerDisclosure`
- `FoldoutDisclosure`
- `CardLeading`
- `Menu`
- `GalleryToolbar`

## Missing — to add later

Requested (bring the art over when ready):

- `Layers` · `layers.png` — LAYERS left-tab glyph
- `Library` · `library.png` — LIBRARY left-tab glyph
- `Global` · `global.png` — GLOBAL (variables) left-tab glyph
- `Squircle` · `squircle.png` — brand mark tile / rounded-square app frame (confirm exact shape intent)
- `Close` · `close.png` — X, panel/window close (theme panel, inspector overlay)
- `Minimize` · `minimize.png` — −, panel/window minimize (theme panel chrome)

Suggested, from gaps found while working on layout (add only if wanted):

- `ChevronLeft` · `chevron-left.png` — only Up/Down/Right exist; needed for leftward collapse directions
- `Pin` · `pin.png` — inspector overlay pin (placement cycle, D15)
- `Dock` · `dock.png` — dock-right action in the inspector placement control
- `VariableLink` · `variable-link.png` — a parameter driven by a global variable (globals plan)
- `VariableUnlink` · `variable-unlink.png` — break the variable link

## How to add one

- Art: 64 px white-on-transparent PNG → `Resources/Icons/<kebab>.png`; match the
  sheet's stroke weight (do not resize an existing glyph up).
- Register in `MixtormatStyle.cpp::Refresh()`:
  `SetPngIcon(TEXT("Mixtormat.Icon.<Name>"), TEXT("Icons/<kebab>"), FVector2D(IconBrushSize, IconBrushSize));`
  Vector brand art uses `SetBrandArtwork` instead.
- Declare + define the wrapper in `MixtormatIcons.h` / `.cpp` (one line each).
- Use the wrapper; pick the matching `EMixtormatIconRole` for size/opacity. If
  no role fits, append a role (never reorder) and add theme defaults + a schema
  entry.
- Keys and files are append-only in spirit: removing one means updating every
  callsite, so grep the key first.
