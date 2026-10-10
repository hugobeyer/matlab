# Icons

The Mixtormat icon set: what exists, where it lives, how to add one, and what is
still missing. Current glyph registrations use **owned SVGs** — never `FAppStyle` glyphs (the
chevrons were once borrowed and changed weight with the editor theme; that
lesson is why this set exists).

## Where things live

- Wrapper API — widgets call this: `UI/Atoms/MixtormatIcons.h` / `.cpp`
- Brush registration: `Style/MixtormatStyle.cpp` — `SetSvgIcon` / `SetPngIcon` / `SetBrandArtwork`
- Art: `Resources/Icons/*.png` — historical inventory; current registrations are owned SVGs.
- Brand art: `Resources/Icons/mixtormat-icon.svg`, `mixtormat-logo.svg` — vector, ratio-locked.
- Per-role sizing/opacity: `EMixtormatIconRole` + `FMixtormatIconStyle` (`MixtormatTheme.h`).
- Ramp toolbar plates: `FMixtormatScalarRampButtonTheme` via `SMixtormatIconButton`.
- Brush sizes: `ControlLayout.IconBrushSize` (20), `IconBrushSizeLarge` (28), `ScalarRampIconSize`.

## Rules

- Widgets use `MixtormatIcons::Name()`; never `GetBrush(TEXT("Mixtormat.Icon..."))`.
- Key format: `Mixtormat.Icon.<WrapperName>`; art file: `Icons/<kebab-name>.png`.
- PNGs are white so the brush tint colours them.
- `Resources/Icons/*.png` is excluded from agent file scanning
  (`.zed/settings.json`) — to see the art, open the folder or read the
  registration block in `MixtormatStyle.cpp`.

## Icon inventory

The original inventory below retains historical PNG filenames. Current registrations in
`MixtormatStyle.cpp` use SVG glyphs, including Save, SaveAs, Overflow and Add. Source registration
is authoritative; this note does not claim final-size artwork has been visually verified.

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
- `PreviewToolbar`
- `LayerVisToggle`
- `LayerDisclosure`
- `FoldoutDisclosure`
- `Menu`
- `GalleryToolbar`
- `NavigationRail` — left-column icons, independent of toolbar sizes

UI Style exposes per-role glyph, button and hit sizes plus rest/hover/disabled opacity.
`LayerVisToggle` additionally exposes rest, hover and disabled blend modes. It paints the
owned `Squircle()` SVG: hidden/off is a muted squircle, while disabled uses black coverage
with its disabled blend (Soft Light by default). `DisabledOpacity` does not affect this role
or `LayerDisclosure`; their disabled paint path is blend-driven. Non-Normal layer-mark blends
use an approximate resolved row backdrop, not the fully painted row surface.

## Previously missing — now registered in source

The original missing-icon requests are retained here with their current SVG registrations:

- `Layers` · `layers.svg` — LAYERS left-tab glyph
- `Library` · `library.svg` — LIBRARY left-tab glyph
- `Global` · `global.svg` — GLOBAL (variables) left-tab glyph
- `Squircle` · `squircle.svg` — brand mark tile / rounded-square app frame (confirm exact shape intent)
- `Close` · `close.svg` — X, panel/window close (theme panel, inspector overlay)
- `Minimize` · `minimize.svg` — −, panel/window minimize (theme panel chrome)

These previously suggested icons are also registered in source:

- `ChevronLeft` · `chevron-left.svg` — leftward collapse directions
- `Pin` · `pin.svg` — inspector overlay pin (placement cycle, D15)
- `Dock` · `dock.svg` — dock-right action in the inspector placement control
- `VariableLink` · `variable-link.svg` — a parameter driven by a global variable (globals plan)
- `VariableUnlink` · `variable-unlink.svg` — break the variable link

## Flow and warp SVGs (2026-10-10)

Six supplied 64px SVG assets are registered without altering their artwork.
Names distinguish operations that **produce/steer flow** from operations that **warp/push**:

- `FlowDirection` — `flow-direction.svg`: Generator Flow / Flow Carve
- `FlowGravity` — `flow-gravity.svg`: Gravity Flow
- `WarpDeform` — `warp-deform.svg`: Shape Deform
- `WarpNoise` — `warp-noise.svg`: reserved for explicit noise-driven warp (not the Noise generator)
- `WarpPush` — `warp-push.svg`: Height Push
- `WarpStructural` — `warp-structural.svg`: Structural Warp / Flow Warp

Existing generic generator, effect, and noise icons remain available.

## How to add one

- Art: 64 px white-on-transparent PNG → `Resources/Icons/<kebab>.png`; match the
  sheet's stroke weight (do not resize an existing glyph up).
- Register in `MixtormatStyle.cpp::Refresh()`:
  `SetPngIcon(TEXT("Mixtormat.Icon.<Name>"), TEXT("Icons/<kebab>"), FVector2D(IconBrushSize, IconBrushSize));`
  Current glyphs use the equivalent `SetSvgIcon` registration with vector artwork.
  Vector brand art uses `SetBrandArtwork` instead.
- Declare + define the wrapper in `MixtormatIcons.h` / `.cpp` (one line each).
- Use the wrapper; pick the matching `EMixtormatIconRole` for size/opacity. If
  no role fits, append a role (never reorder) and add theme defaults + a schema
  entry. `PanelToolbar` and `CardLeading` were removed; do not revive them.
- Keys and files are append-only in spirit: removing one means updating every
  callsite, so grep the key first.
