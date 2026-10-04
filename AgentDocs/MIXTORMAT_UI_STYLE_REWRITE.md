# Mixtormat UI Styling System — Full Rewrite Plan

**Target:** Unreal Engine 5.8 / Slate  
**Scope:** MixtormatEditor visual system only  
**Rewrite policy:** **clean break**  
**Legacy policy:** **none**  
**Saved-theme compatibility:** **none**  
**Migration policy:** **none**  
**Primary source of truth:** `Docs/ui-prototype/`

---

# 0. Rewrite mandate

This is not a migration of the current style system.

This is a **replacement**.

Do not preserve the existing LiveTheme schema, compatibility aliases, old persisted token names, dead controls, old category metadata, or legacy deserialization behavior.

There are no user themes worth preserving.

The rewrite may:

- delete obsolete style tokens
- rename style concepts
- replace the theme JSON schema
- remove old compatibility paths
- remove hidden legacy controls
- delete derived colors that should become recipes
- replace the current UI STYLE registry
- replace painter-specific blend logic
- replace the current font helper
- reorganize style files
- change internal style keys

It must **not** change Mixtormat product behavior.

Preserve:

- layer model
- drag/drop
- Before / Into / After group drop behavior
- selection
- transactions
- parameter writes
- masks
- IDs
- effects
- generators
- clipboard
- preview functionality
- menu actions
- gallery selection
- gallery zoom
- splitters
- editor commands
- workspace behavior

The rewrite is for:

> **visual architecture, theming, compositing, typography, geometry, and authoring controls**

---

# 1. Why rewrite

The current system has accumulated too many local visual decisions.

The main problems are structural:

- large numbers of state-specific color tokens
- multiple painters implementing similar compositing differently
- blend choices with partial or incorrect readers
- important blend layers hidden because the existing painter cannot express them
- color tokens used where the prototype actually uses blend + opacity + saturation
- theme categories inferred from strings and prefixes
- UI STYLE exposes implementation details instead of component recipes
- typography still mixes Mixtormat and `FCoreStyle`
- Inter is not yet a true file-backed Mixtormat typography system
- multiple widgets build their own visual state logic
- geometry, color, state, typography, and compositing are mixed together
- hot paint paths still resolve high-level theme information themselves
- future animation would require repeating the same state logic again

The target architecture should be:

```text
small semantic palette
        ↓
component recipes
        ↓
resolved runtime style
        ↓
shared painters
        ↓
thin widget adapters
```

Not:

```text
hundreds of disconnected values
        ↓
widget-specific interpretation
        ↓
manual compositing
```

---

# 2. Core design principle

A Mixtormat surface should be described by **how it is constructed**.

Example:

```text
Well
├─ Base = Ground
├─ Shade = Black
├─ Shade Blend = Multiply
├─ Shade Vertical Ramp = 0.10 → 0.24
├─ Border = Hairline
├─ Border Opacity = 0.16
├─ Radius = 2
└─ Hover modifier
```

Not:

```text
WellTopColor
WellBottomColor
WellHoverTopColor
WellHoverBottomColor
WellBorderColor
WellHoverBorderColor
WellDisabledColor
...
```

The same rule applies to fills, toggles, foldouts, cards, tabs, segmented controls, top-bar buttons, layer rows, child rows, group rows, menus, preview actions, and gallery tiles.

---

# 3. Hard rules

## 3.1 No legacy

Delete:

- old theme compatibility aliases
- legacy-hidden theme fields
- old numeric-as-bool compatibility
- old persisted-key acceptance
- old version-1 migration code
- old unused token registrations

If an old token no longer belongs in the new system, remove it.

## 3.2 No dead controls

Every UI STYLE editor must have a real production reader.

No exposed values with no reader, fake blend dropdowns, width controls that do not control layout, color swatches that are later ignored by a recipe, or font choices that do not change the actual Slate font resource.

## 3.3 No duplicated compositing math

There must be one implementation for:

- Normal
- Additive / Plus Lighter
- Multiply
- Soft Light
- Saturation
- source-over alpha

Widgets do not reimplement them.

## 3.4 No per-widget theme interpretation

Widgets should not decide which palette color is “close enough”, which blend should be used, how a saturation token maps to a state, what hover color to derive, or how a CSS falloff should be interpreted.

That belongs to the style system.

## 3.5 No permanent Tick for styling

Theme changes rebuild resolved style.

Widget state changes read resolved style.

No permanent `Tick()` just to keep text or brushes synchronized.

## 3.6 No arbitrary local styling literals

A layout or visual literal is acceptable only when it is intrinsic Slate plumbing, mathematically derived, or the prototype itself has no authored configurable equivalent.

Anything visually authored should belong to palette, recipe, geometry, typography, or icon metrics.

---

# 4. Target architecture

```text
Style/
├─ MixtormatTheme.h/.cpp
├─ MixtormatThemeSchema.h/.cpp
├─ MixtormatResolvedStyle.h/.cpp
├─ MixtormatPalette.h/.cpp
├─ MixtormatTypography.h/.cpp
├─ MixtormatGeometry.h/.cpp
├─ MixtormatRecipes.h/.cpp
├─ MixtormatCompositing.h/.cpp
└─ MixtormatStyle.cpp

UI/Primitives/
├─ MixtormatSurfacePainter.h/.cpp
├─ MixtormatGradientPainter.h/.cpp
├─ MixtormatHierarchyPainter.h/.cpp
└─ MixtormatIconMetrics.h

Widgets/
└─ SMixtormatLiveThemePanel.h/.cpp
```

Specialized widgets remain, but become thin adapters.

---

# 5. Data flow

```text
Authored defaults
      ↓
FMixtormatTheme
      ↓
UI STYLE edits
      ↓
Validate
      ↓
Resolve()
      ↓
FMixtormatResolvedStyle
      ↓
FMixtormatStyle::Refresh()
      ↓
Widgets paint
```

The paint path must not need to understand the editable schema.

---

# 6. Theme model

Create a clean new `FMixtormatTheme`.

```cpp
struct FMixtormatTheme
{
    FMixtormatPaletteTheme Palette;
    FMixtormatControlTheme Controls;
    FMixtormatFoldoutTheme Foldouts;
    FMixtormatCardTheme Cards;
    FMixtormatLayerTheme Layers;
    FMixtormatButtonTheme Buttons;
    FMixtormatMenuTheme Menus;
    FMixtormatPreviewTheme Preview;
    FMixtormatGalleryTheme Gallery;
    FMixtormatShellTheme Shell;
    FMixtormatTypographyTheme Typography;
};
```

This is the editable model.

Do not build it from hundreds of global inline variables.

---

# 7. Semantic palette

The editable palette should be small.

Recommended source colors:

```cpp
struct FMixtormatPaletteTheme
{
    FLinearColor Ground;
    FLinearColor Shell;
    FLinearColor Panel;

    FLinearColor Text;
    FLinearColor TextMuted;

    FLinearColor Accent;
    FLinearColor Modified;
    FLinearColor Warning;
    FLinearColor Error;

    FLinearColor Shade;
    FLinearColor Hairline;

    FLinearColor MenuGround;
    FLinearColor ThumbnailGround;
    FLinearColor OverlayGround;
};
```

Do not immediately add more colors because a component needs a hover state.

Try to derive the state from source role, opacity, blend, saturation, and falloff first.

---

# 8. Color references

Recipes should reference semantic colors.

```cpp
enum class EMixtormatColorRole : uint8
{
    Ground,
    Shell,
    Panel,
    Text,
    TextMuted,
    Accent,
    Modified,
    Warning,
    Error,
    Shade,
    Hairline,
    MenuGround,
    ThumbnailGround,
    OverlayGround,
};
```

```cpp
struct FMixtormatColorRef
{
    EMixtormatColorRole Role = EMixtormatColorRole::Ground;
    float Opacity = 1.0f;
    float Saturation = 1.0f;
    FLinearColor Multiplier = FLinearColor::White;
};
```

One resolver:

```cpp
FLinearColor ResolveColor(
    const FMixtormatResolvedPalette& Palette,
    const FMixtormatColorRef& Ref);
```

---

# 9. Blend model

One enum:

```cpp
enum class EMixtormatBlendMode : uint8
{
    Normal,
    Additive,
    Multiply,
    SoftLight,
};
```

One implementation:

```cpp
FLinearColor ApplyBlend(
    EMixtormatBlendMode Mode,
    const FLinearColor& Backdrop,
    const FLinearColor& Source,
    float Strength = 1.0f);
```

The implementation must define alpha behavior deliberately.

No component-specific variant of Additive or Multiply.

---

# 10. Blend semantics

## Normal

Standard source-over.

## Additive / Plus Lighter

Use the closest consistent equivalent to CSS `plus-lighter`.

For opaque UI backdrops:

```text
Backdrop.rgb + Source.rgb × Source.a
```

clamped to valid range.

## Multiply

For source `S` and alpha `A`:

```text
Result = Backdrop × lerp(1, S, A)
```

Black at alpha `A` becomes:

```text
Backdrop × (1 - A)
```

## Soft Light

Use the corrected W3C piecewise soft-light function.

Do not approximate it with a broken polynomial branch.

---

# 11. Paint layer model

All compound surfaces use the same layer representation.

```cpp
enum class EMixtormatAxis : uint8
{
    None,
    Horizontal,
    Vertical,
};
```

```cpp
struct FMixtormatRampPoint
{
    float Position = 0.0f;
    float Value = 1.0f;
};
```

```cpp
struct FMixtormatRamp
{
    EMixtormatAxis Axis = EMixtormatAxis::None;
    TArray<FMixtormatRampPoint, TInlineAllocator<8>> Points;
};
```

```cpp
struct FMixtormatPaintLayer
{
    FMixtormatColorRef Source;
    EMixtormatBlendMode Blend = EMixtormatBlendMode::Normal;
    FMixtormatRamp OpacityRamp;
    float Strength = 1.0f;
    bool bEnabled = true;
};
```

A paint layer is data only.

---

# 12. Falloff model

One reusable power falloff.

```cpp
struct FMixtormatFalloff
{
    float Start = 1.0f;
    float End = 0.0f;
    float Power = 1.0f;
    int32 Samples = 6;
};
```

```cpp
float EvaluateFalloff(
    const FMixtormatFalloff& Falloff,
    float T);
```

```cpp
void BuildFalloffRamp(
    const FMixtormatFalloff& Falloff,
    EMixtormatAxis Axis,
    FMixtormatRamp& OutRamp);
```

Do not have separate formulas for foldouts, cards, glow, or menus.

---

# 13. Border / hairline model

```cpp
struct FMixtormatBorderLayer
{
    FMixtormatColorRef Source;
    EMixtormatBlendMode Blend = EMixtormatBlendMode::Normal;

    float Width = 1.0f;

    bool bTop = false;
    bool bBottom = false;
    bool bLeft = false;
    bool bRight = false;
};
```

Used for foldout hairlines, button hairlines, selected row top edges, well outlines, menu borders, tile selection edges, and separators.

---

# 14. Glow model

```cpp
struct FMixtormatGlowLayer
{
    FMixtormatColorRef Source;
    EMixtormatBlendMode Blend = EMixtormatBlendMode::Additive;

    float Opacity = 0.0f;
    float Saturation = 1.0f;
    float Reach = 0.0f;

    float HairlineWidth = 0.0f;
    float HairlineOpacity = 0.0f;
};
```

---

# 15. Surface recipe

```cpp
struct FMixtormatSurfaceRecipe
{
    FMixtormatColorRef Base;
    TArray<FMixtormatPaintLayer, TInlineAllocator<4>> Layers;
    TArray<FMixtormatBorderLayer, TInlineAllocator<2>> Borders;
    float Radius = 0.0f;
};
```

A generic painter should be able to paint most Mixtormat surfaces from this alone.

---

# 16. State system

Use a base recipe plus numeric state modifiers.

```cpp
enum class EMixtormatVisualState : uint8
{
    Rest,
    Hover,
    Pressed,
    Selected,
    Disabled,
};
```

```cpp
struct FMixtormatStateModifier
{
    float Opacity = 1.0f;
    float Saturation = 1.0f;
    float Strength = 1.0f;

    TOptional<FMixtormatColorRef> SourceOverride;
    TOptional<EMixtormatBlendMode> BlendOverride;
};
```

Do not duplicate entire surfaces just for hover.

---

# 17. Recipe family: wells

The well should be:

```text
Base
  Ground

Shade
  Shade / black
  Multiply
  vertical ramp

Border
  Hairline
  normal

Hover
  stronger border
  optional small lift
```

Suggested:

```cpp
struct FMixtormatWellTheme
{
    float Radius;

    EMixtormatBlendMode ShadeBlend = EMixtormatBlendMode::Multiply;

    float ShadeTop;
    float ShadeBottom;

    float BorderWidth;
    float BorderOpacity;
    float BorderHoverOpacity;

    float HoverLiftOpacity;
};
```

No `WellTop`, `WellBottom`, `WellTopHover`, `WellBottomHover` colors.

---

# 18. Recipe family: fill

The fill is:

```text
Accent vertical body
    Additive

Black horizontal shade
    Multiply
```

```cpp
struct FMixtormatFillTheme
{
    EMixtormatBlendMode BodyBlend = EMixtormatBlendMode::Additive;
    EMixtormatBlendMode ShadeBlend = EMixtormatBlendMode::Multiply;

    float Top;
    float Bottom;

    float HoverTop;
    float HoverBottom;

    float ActiveTop;
    float ActiveBottom;

    float Saturation;
    float HoverSaturation;
    float ActiveSaturation;

    float ShadeStart;
    float ShadeMid;
    float ShadeEnd;
    float ShadeMidPosition;
};
```

The source is Accent.

No separate body colors.

---

# 19. Recipe family: toggle

Unchecked:

```text
Well
```

Checked:

```text
Well
+ Fill
```

Disabled:

```text
state modifier
+ optional shade
```

Use the same Well and Fill recipes.

---

# 20. Recipe family: group button

The HTML structure is:

```text
Body gradient
  source = Accent
  blend = body blend

Hairline
  source = Accent
  blend = Additive

Separator
  source = Text
  subtle opacity
```

Important:

> The body blend and hairline blend are separate.

```cpp
struct FMixtormatButtonTheme
{
    float Height;
    float HorizontalPadding;

    EMixtormatBlendMode BodyBlend = EMixtormatBlendMode::Normal;
    EMixtormatBlendMode HairlineBlend = EMixtormatBlendMode::Additive;

    float RestTop;
    float RestBottom;

    float HoverTop;
    float HoverBottom;

    float SelectedTop;
    float SelectedBottom;

    float GradientSaturation;

    float HairlineWidth;
    float HairlineOpacity;
    float HairlineHoverOpacity;
    float HairlineSelectedOpacity;
    float HairlineSaturation;

    float SeparatorWidth;
    float SeparatorHeight;
    float SeparatorOpacity;
};
```

Used by tabs, segmented controls, top-bar actions, layer-add actions, and compact shared actions.

Same visual recipe, different widget behavior.

---

# 21. Recipe family: foldout

```text
Base
  Ground

Lift
  source role
  Blend = FoldoutLiftBlend
  vertical falloff

Accent pass
  Accent
  Blend = FoldoutAccentBlend
  same domain

Hairline
  Hairline
  independent saturation
```

```cpp
struct FMixtormatFoldoutTheme
{
    FMixtormatFalloff LiftFalloff;

    EMixtormatBlendMode LiftBlend;
    EMixtormatBlendMode AccentBlend;

    float LiftSaturation;
    float HoverSaturation;

    float AccentOpacity;
    float AccentHoverOpacity;

    float HairlineOpacity;
    float HairlineHoverOpacity;
    float HairlineSaturation;
    float HairlineHoverSaturation;
};
```

No vignette requirement.

---

# 22. Recipe family: Group Card

Header and body are one continuous recipe.

```text
Ground base
    ↓
header contribution
    ↓
seam
    ↓
body reach
    ↓
optional mirrored bottom tail
```

```cpp
struct FMixtormatCardTheme
{
    EMixtormatBlendMode Blend = EMixtormatBlendMode::Additive;

    float HeaderOpacity;
    float BodyOpacity;

    float HeaderSaturation;
    float BodySaturation;

    float FalloffPower;
    float Reach;

    float Radius;
};
```

Keep geometry-specific seam mapping in the card painter.

Keep compositing generic.

---

# 23. Recipe family: layer rows

The layer system should describe:

```text
Base
Hover
Selected
Group
Child
```

```cpp
struct FMixtormatLayerTheme
{
    EMixtormatBlendMode Blend = EMixtormatBlendMode::Normal;
    EMixtormatBlendMode GroupBlend = EMixtormatBlendMode::SoftLight;

    float RestSaturation;
    float HoverSaturation;
    float SelectedSaturation;
    float GroupSaturation;

    float ChildSaturation;
    float ChildHoverSaturation;
    float ChildSelectedSaturation;

    float HoverStrength;
    float SelectedStrength;

    FMixtormatGlowLayer ActiveGlow;
};
```

Avoid hard-coded state colors where they can be derived from Panel/Accent/Ground.

---

# 24. Layer hierarchy is foreground structure

Hierarchy rails are **not part of the row surface recipe**.

Required paint order:

```text
0 Ground
1 row body recipe
2 selected glow
3 row hairline
4 hierarchy rails
5 thumbnail / module plates
6 icons
7 text
8 badges/actions
```

Rails:

- normal blend
- token-driven source
- token-driven opacity
- token-driven thickness
- not saturated by the row
- not multiplied through selected gradients
- visually continuous between rows

---

# 25. Hierarchy style

```cpp
struct FMixtormatHierarchyTheme
{
    FMixtormatColorRef Source;

    float Indent;
    float Width;
    float Opacity;

    float ParentJoinOffset;
    float ChildArmLength;
};
```

The hierarchy painter handles vertical stems, ancestor continuations, tee, elbow, last-child elbow, parent-to-first-child stem, and pixel alignment.

Rows only provide topology metadata.

---

# 26. Recipe family: menu

Menu styling:

```text
Ground
+ optional top lip/lift
+ border
```

Menu item states:

```text
rest
hover
checked
disabled
destructive
```

Do not create independent palette colors for every state unless genuinely necessary.

---

# 27. Recipe family: gallery tile

```text
ThumbnailGround base
Border
Hover lift
Selected accent edge
Caption strip
```

Separate tile surface, image, caption, and overlay actions.

---

# 28. Geometry system

Geometry is not color/compositing data.

Create structured metric groups:

```cpp
struct FMixtormatControlMetrics;
struct FMixtormatFoldoutMetrics;
struct FMixtormatCardMetrics;
struct FMixtormatLayerMetrics;
struct FMixtormatMenuMetrics;
struct FMixtormatPreviewMetrics;
struct FMixtormatGalleryMetrics;
struct FMixtormatShellMetrics;
```

Example:

```cpp
struct FMixtormatLayerMetrics
{
    float RowHeight;
    float GroupRowHeight;
    float ChildRowHeight;

    float Gap;
    float PaddingX;

    float ThumbnailSize;
    float ItemGap;

    float ChildIndent;
};
```

---

# 29. Icon architecture

Separate:

```text
glyph size
visual button size
hit size
opacity
```

```cpp
struct FMixtormatIconStyle
{
    float GlyphSize;
    float ButtonSize;
    float HitSize;

    float RestOpacity;
    float HoverOpacity;
    float DisabledOpacity;
};
```

Roles:

```cpp
enum class EMixtormatIconRole : uint8
{
    TopBar,
    PanelToolbar,
    PreviewToolbar,
    LayerEye,
    LayerDisclosure,
    FoldoutDisclosure,
    Menu,
    CardLeading,
    GalleryToolbar,
};
```

No accidental role borrowing.

---

# 30. Typography: clean rewrite

The typography system must actually use the shipped Inter file.

Resource:

```text
Resources/Fonts/Inter.ttf
Resources/Fonts/Inter-OFL.txt
```

Do not fake Inter by taking an engine-default font and only changing `TypefaceFontName`.

Use the correct UE 5.8 Slate API for an actual file-backed font.

Verify the exact API from engine headers before implementation.

---

# 31. Typography API

```cpp
enum class EMixtormatFontFamily : uint8
{
    Inter,
};
```

Start with one family.

The user asked for Inter.

Do not expose Roboto unless Mixtormat deliberately ships and supports it.

---

# 32. Weight model

Only expose weights that really resolve.

Minimum:

```cpp
enum class EMixtormatFontWeight : uint8
{
    Regular,
    Bold,
};
```

If UE 5.8 reliably supports variable-font instances from the shipped TTF, expand later.

Do not invent face names.

---

# 33. Text roles

```cpp
enum class EMixtormatTextRole : uint8
{
    Body,
    ControlLabel,
    ControlValue,
    Caption,
    FoldoutTitle,
    CardTitle,
    LayerName,
    LayerSource,
    Menu,
    MenuShortcut,
    GalleryCaption,
    TopBar,
    PreviewLabel,
    Badge,
};
```

Widgets request roles.

They do not construct fonts directly.

---

# 34. Text spec

```cpp
struct FMixtormatTextSpec
{
    float Size = 10.0f;
    EMixtormatFontWeight Weight = EMixtormatFontWeight::Regular;

    float TrackingPx = 0.0f;
    float Opacity = 1.0f;

    bool bUppercase = false;
    bool bMonospacedNumbers = false;
};
```

Central tracking conversion:

```cpp
SlateLetterSpacing =
    RoundToInt(TrackingPx / SizePx * 1000.0f);
```

---

# 35. Remove Mixtormat `FCoreStyle` font construction

After rewrite, Mixtormat-owned text should not be built directly with:

```cpp
FCoreStyle::GetDefaultFontStyle(...)
```

except an explicit documented fallback path.

All Mixtormat text should use:

```cpp
MixtormatTypography::Make(...)
```

---

# 36. Resolved style

Editable data is resolved once.

```cpp
struct FMixtormatResolvedStyle
{
    FMixtormatResolvedPalette Palette;

    FMixtormatResolvedControlStyle Controls;
    FMixtormatResolvedFoldoutStyle Foldouts;
    FMixtormatResolvedCardStyle Cards;
    FMixtormatResolvedLayerStyle Layers;
    FMixtormatResolvedButtonStyle Buttons;
    FMixtormatResolvedMenuStyle Menus;
    FMixtormatResolvedPreviewStyle Preview;
    FMixtormatResolvedGalleryStyle Gallery;
    FMixtormatResolvedShellStyle Shell;

    FMixtormatResolvedTypography Typography;
};
```

Flow:

```text
Theme
→ ResolveTheme()
→ ResolvedStyle
→ paint
```

---

# 37. No registry lookup in hot paint paths

Avoid repeated:

```cpp
ResolveColor(TEXT("..."))
```

inside layer-row painting.

The resolved style should already contain the source and resolved colors.

Hot paths should read compact structs and enums.

---

# 38. Generic surface painter

Create:

```text
UI/Primitives/MixtormatSurfacePainter.h/.cpp
```

API:

```cpp
int32 PaintSurface(
    FSlateWindowElementList& Elements,
    int32 LayerId,
    const FGeometry& Geometry,
    const FMixtormatSurfaceRecipe& Recipe,
    const FMixtormatResolvedPalette& Palette,
    const FWidgetStyle& WidgetStyle);
```

Responsibilities:

- base
- paint layers
- blending
- opacity ramps
- falloff
- saturation
- borders
- radius
- clipping

---

# 39. Specialized painters become adapters

```text
SMixtormatFoldoutHeader
    chooses state
    computes geometry
    gets Foldout recipe
    paints

MixtormatGroupCardPainter
    computes seam/reach
    feeds Card recipe

SMixtormatLayerSurface
    chooses row state
    feeds Layer recipe

SMixtormatGroupButtonSurface
    chooses rest/hover/selected
    feeds Button recipe
```

They should not own blend formulas.

---

# 40. UI STYLE: complete rewrite

Delete the current dynamic category-prefix model.

Use explicit tabs.

```cpp
enum class EMixtormatStyleTab : uint8
{
    Palette,
    Controls,
    Foldouts,
    Cards,
    Layers,
    Buttons,
    Menus,
    Preview,
    GalleryShell,
    Typography,
};
```

Every editable property has:

```text
Tab
Section
Label
Help
Type
Default
Editor metadata
Reader/writer
```

No string-prefix category matching.

---

# 41. UI STYLE layout

Top:

```text
[search]
[tabs]
```

Tabs remain fixed.

Only content scrolls.

Example:

```text
LAYERS

Geometry
  Row Height             26
  Group Height           18
  Child Height           18
  Gap                     1
  Indent                 28

Surface
  Blend                Normal
  Saturation             .80
  Strength               .12

Hover
  Saturation            1.00
  Strength               .18

Selected
  Saturation            1.10
  Strength               .24

Glow
  Blend              Additive
  Opacity                .12
  Reach                    8
  Hairline                1.0

Group
  Blend            Soft Light
  Saturation             1.00

Hierarchy
  Width                  1.0
  Opacity                .24
```

---

# 42. UI STYLE tabs

Required:

```text
All
Palette
Controls
Foldouts
Cards
Layers
Buttons
Menus
Preview
Gallery / Shell
Typography
```

Use a real horizontal tab strip.

No wrapped tab cloud.

Horizontal scroll or overflow when needed.

---

# 43. UI STYLE sections

Each tab owns explicit sections.

## Controls

```text
Rows
Well
Fill
Toggle
Slider
Dropdown
Text
```

## Layers

```text
Geometry
Surface
Hover
Selected
Group
Child
Hierarchy
Typography
```

## Cards

```text
Geometry
Surface
Falloff
Header
Body
Typography
Icons
```

---

# 44. UI STYLE control types

```text
float  → SSpinBox
bool   → Mixtormat toggle
choice → Mixtormat dropdown
color  → color swatch/picker
```

Do not use AppStyle toggle visuals if Mixtormat already has a reusable toggle.

The authoring panel should look like Mixtormat.

---

# 45. Numeric metadata

```cpp
struct FMixtormatNumberEditorMeta
{
    float Min;
    float Max;
    float Step;

    int32 MinFractionalDigits;
    int32 MaxFractionalDigits;

    bool bCommitOnRelease = true;
};
```

Examples:

```text
integer geometry   step 1    digits 0
opacity            step .01  digits 2
saturation         step .05  digits 2
hairline width     step .25  digits 2
tracking           step .1   digits 1
```

Actually wire fractional digits to the spinbox.

---

# 46. Commit-on-release

Do not mutate the global theme continuously during expensive drag edits.

Use local pending editor value.

```text
mouse drag
→ pending local value

mouse release / commit
→ theme property changes once
→ ResolveTheme
→ style refresh
→ workspace rebuild once
```

Keyboard:

```text
type
→ Enter / focus commit
→ apply once
```

Reset:

```text
click
→ apply default immediately
```

---

# 47. Choice controls

Use real dropdowns for blend modes and other enums.

Labels:

```text
Normal
Additive
Multiply
Soft Light
```

Internal value remains enum.

---

# 48. Boolean controls

Booleans are actual booleans.

No 0–1 numeric substitute.

---

# 49. Palette UI

Do not expose 58 colors.

Target a small set:

```text
Ground
Shell
Panel
Text
Text Muted
Accent
Modified
Warning
Error
Shade
Hairline
Menu Ground
Thumbnail Ground
Overlay Ground
```

If a color can be derived by recipe, do not expose it.

---

# 50. Theme persistence

There are **no old saved themes to preserve**.

Delete the old schema and start clean.

Suggested new schema:

```json
{
  "version": 1,
  "palette": {},
  "controls": {},
  "foldouts": {},
  "cards": {},
  "layers": {},
  "buttons": {},
  "menus": {},
  "preview": {},
  "galleryShell": {},
  "typography": {}
}
```

This version 1 belongs to the rewritten system.

Old theme files may fail to load.

That is acceptable.

---

# 51. Delete stale theme infrastructure

Remove:

- old `Numbers()`
- old `Booleans()`
- old `Choices()`
- old `Colors()`
- prefix categories
- hidden compatibility entries
- numeric bool conversion
- old `Deserialize()` compatibility logic
- old dead registration macros

Replace with structured schema metadata.

---

# 52. Registry representation

Avoid another macro wall.

Prefer:

```cpp
struct FMixtormatStyleProperty
{
    FName Id;

    EMixtormatStyleTab Tab;
    FName Section;

    FText Label;
    FText Help;

    EMixtormatPropertyType Type;

    FMixtormatPropertyBinding Binding;
    FMixtormatEditorMeta Editor;
};
```

The system should be type-safe enough that a bool cannot accidentally become a float editor.

---

# 53. Property IDs

Use stable semantic IDs.

Examples:

```text
Controls.Well.ShadeTop
Controls.Well.ShadeBottom
Controls.Well.Blend

Controls.Fill.BodyBlend
Controls.Fill.ShadeBlend
Controls.Fill.RestTop
Controls.Fill.RestBottom

Layers.Surface.Blend
Layers.Surface.Saturation
Layers.Hover.Strength
Layers.Group.Blend

Typography.ControlLabel.Size
Typography.ControlLabel.Weight
```

No legacy names required.

---

# 54. HTML/CSS mapping

The prototype remains the visual specification.

Create a mapping table during implementation.

Example:

```text
--well-blend-mode
→ Controls.Well.ShadeBlend

--fill-body-top
→ Controls.Fill.RestTop

--group-button-blend-mode
→ Buttons.Shared.BodyBlend

--layer-group-blend-mode
→ Layers.Group.Blend

--card-gradient-reach
→ Cards.Surface.Reach
```

Do not add a C++ property without knowing the corresponding visual concept.

---

# 55. CSS values that stay recipes

Examples:

```text
mix-blend-mode
filter: saturate()
linear-gradient()
opacity
falloff power
hairline opacity
```

Do not turn these into hand-picked color swatches.

---

# 56. CSS values that stay geometry

Examples:

```text
height
padding
gap
radius
icon size
indent
splitter size
```

---

# 57. CSS values that stay typography

Examples:

```text
font size
weight
tracking
case
opacity
```

---

# 58. Static parity before animation

Do not add animations as part of the rewrite.

State endpoints should be numeric where possible:

```text
RestStrength
HoverStrength
SelectedStrength
RestSaturation
HoverSaturation
SelectedSaturation
RestOpacity
HoverOpacity
SelectedOpacity
```

Animation can come later.

---

# 59. Vignettes

Do not build vignette infrastructure in this rewrite.

The UI should already read correctly without radial vignette support.

---

# 60. Shell splitters

Do not convert shell layout to fixed token widths.

Keep functional `SSplitter`.

Authored widths may seed initial ratios.

Style system controls splitter visual width, hit width, separator color, hover, and active treatment.

---

# 61. Preview/gallery split

Keep the resizable splitter.

Prototype gallery height may seed initial fraction.

Do not expose a fake fixed height if runtime uses a fraction.

---

# 62. Layer-stack validation target

The first major visual stress test after core migration should be the layer stack.

Verify:

- ordinary layer
- group
- child
- hover
- selected
- hierarchy rails
- thumbnail
- disclosure
- visibility
- badges
- labels
- group selection glow

This exercises blend, saturation, hierarchy, geometry, typography, icon sizing, and state modifiers.

---

# 63. Paint-order contract

Every component documents its paint order.

Layer row:

```text
0 Ground
1 Body
2 Cross pass
3 Glow
4 Hairline
5 Hierarchy
6 Thumbnail
7 Icons
8 Text
9 Badges
```

Foldout:

```text
0 Ground
1 Lift
2 Accent
3 Hairline
4 Content
```

Button:

```text
0 Body gradient
1 Hairline
2 Separator
3 Content
```

This prevents future blend-scope bugs.

---

# 64. Modularity boundary

A reusable style module answers:

```text
what does it look like?
```

A widget answers:

```text
what does it do?
```

Do not mix those responsibilities.

---

# 65. Performance

Hot layer-row painting must avoid:

- heap allocations
- string lookup
- JSON lookup
- style registry lookup by arbitrary string
- UObject allocation
- dynamic brush creation
- permanent Tick

Prefer resolved structs, enums, inline arrays, cached ramp templates, and stack data.

---

# 66. Style refresh lifecycle

```text
UI STYLE commit
→ validate
→ update FMixtormatTheme
→ ResolveTheme
→ rebuild FMixtormatResolvedStyle
→ FMixtormatStyle::Refresh
→ existing workspace rebuild only if required
```

Individual widgets should not reconstruct style logic themselves.

---

# 67. Future animation compatibility

Do not animate now.

But the architecture should support:

```text
logical state
→ target recipe/state
→ interpolation
→ current resolved values
→ painter
```

Avoid brush-swapping designs that would require another rewrite.

---

# 68. Recommended new files

```text
Private/Style/
    MixtormatTheme.h
    MixtormatTheme.cpp

    MixtormatThemeSchema.h
    MixtormatThemeSchema.cpp

    MixtormatResolvedStyle.h
    MixtormatResolvedStyle.cpp

    MixtormatPalette.h
    MixtormatPalette.cpp

    MixtormatTypography.h
    MixtormatTypography.cpp

    MixtormatGeometry.h
    MixtormatGeometry.cpp

    MixtormatRecipes.h
    MixtormatRecipes.cpp

    MixtormatCompositing.h
    MixtormatCompositing.cpp

Private/UI/Primitives/
    MixtormatSurfacePainter.h
    MixtormatSurfacePainter.cpp

    MixtormatHierarchyPainter.h
    MixtormatHierarchyPainter.cpp
```

Existing files may temporarily coexist during the transition.

---

# 69. Files expected to disappear or radically shrink

Likely delete or heavily replace:

```text
Style/MixtormatDesignTokens.h
Style/MixtormatLiveTheme.h/.cpp
Style/MixtormatPalette.h
Style/MixtormatFont.h/.cpp
```

Retain only ideas that fit the new architecture.

Likely retain/refactor:

```text
Style/MixtormatCompositing.h
UI/Primitives/MixtormatGradientPainter.h/.cpp
```

if their internals remain valid.

---

# 70. `MixtormatStyle.cpp`

After rewrite this file should mainly:

- register brushes/styles
- register semantic text styles
- bridge resolved data to Slate style set

It should not be a second theme system.

It should not contain dozens of local derivation formulas.

---

# 71. Style keys

Named Slate keys should correspond to semantic roles.

Examples:

```text
Mixtormat.Text.Body
Mixtormat.Text.ControlLabel
Mixtormat.Text.ControlValue
Mixtormat.Text.LayerName
Mixtormat.Text.CardTitle

Mixtormat.Brush.Panel
Mixtormat.Brush.MenuGround
```

Do not encode transient state combinations into hundreds of style keys.

---

# 72. Authoring labels

UI STYLE shows human labels.

Example:

```text
Body Blend
Shade Blend
Glow Reach
Hairline Opacity
```

not:

```text
GroupButtonSelectedGradientBottom
```

Property ID and display label are separate.

---

# 73. Help text

Every property should describe the paint layer it controls.

Example:

```text
Body Blend
How the Accent body gradient composites over the button backdrop.

Hairline Blend
How the top edge composites. Independent from Body Blend.
```

---

# 74. Blend UI

Blend mode dropdown should sit beside its layer.

Example:

```text
BUTTONS

Body
  Blend        Normal
  Top           0.06
  Bottom        0.02

Hairline
  Blend      Additive
  Opacity       0.14
```

Do not scatter blend choices into a global misc section.

---

# 75. Source roles

For the first rewrite, recipe source roles can remain fixed where CSS fixes them.

Later, if useful, expose source role choices such as:

```text
Ground
Panel
Accent
Text
Shade
Hairline
```

Do not make arbitrary color-routing a prerequisite.

---

# 76. Inter visual validation

After Inter is truly wired, validate:

- 9px
- 10px
- 11px
- regular
- bold
- uppercase tracking
- numeric values

If it still appears vertically stretched, investigate:

- font metrics
- line-height/layout box
- baseline
- row padding
- text block desired size

Do **not** vertically scale glyphs.

---

# 77. Text vertical alignment

Separate:

```text
font metrics
row height
slot VAlign
padding
```

Do not blame the font family automatically.

---

# 78. Layer rails

The HTML hierarchy line aesthetic should be preserved.

Rails should remain:

- geometric
- thin
- subdued
- clean
- continuous
- foreground relative to row gradients
- background relative to text/icons

No PNG tree artwork.

---

# 79. Dynamic state

Widgets should provide:

```cpp
struct FMixtormatVisualStateContext
{
    bool bHovered;
    bool bPressed;
    bool bSelected;
    bool bEnabled;
};
```

The style system resolves this into state modifiers.

---

# 80. Prototype synchronization

`Docs/ui-prototype/` remains the visual source of truth.

Do not silently diverge Unreal defaults from HTML.

For every migrated recipe, document:

```text
CSS selector/token → Unreal field
```

---

# 81. Mandatory mapping document

Create:

```text
Docs/ui-prototype/UNREAL_STYLE_MAPPING.md
```

Example:

| CSS | Unreal | Painter |
|---|---|---|
| `--well-blend-mode` | `Controls.Well.ShadeBlend` | `MixtormatSurfacePainter` |
| `--card-blend-mode` | `Cards.Surface.Blend` | `MixtormatGroupCardPainter` |
| `--layer-group-blend-mode` | `Layers.Group.Blend` | `SMixtormatLayerSurface` |

This becomes the parity checklist.

---

# 82. Rewrite stages

## Stage 1 — inventory behavior

Before styling replacement:

- identify shared widgets
- document callbacks
- identify drag/drop handlers
- identify splitter ownership
- identify gallery state
- identify context-menu ownership

Do not change behavior.

## Stage 2 — new core model

Implement:

- palette
- blend enum
- compositing
- color refs
- ramps
- falloff
- paint layers
- borders
- glows
- surface recipes
- resolved style

Do not migrate all widgets yet.

## Stage 3 — typography

Implement true Inter-backed typography.

Replace Mixtormat text construction with semantic text roles.

## Stage 4 — generic surface painter

Implement base, layers, blend, saturation, ramps, borders, and radius.

## Stage 5 — controls first

Migrate:

1. Well
2. Fill
3. Slider
4. Toggle
5. Dropdown
6. segmented/tab/shared-button visuals

These prove Multiply, Additive, multi-pass compositing, hover, active, disabled, and border handling.

## Stage 6 — containers

Migrate:

1. Foldout
2. Group Card

Validate falloff, seam, reach, and hierarchy.

## Stage 7 — layer stack

Migrate:

- ordinary rows
- groups
- children
- selected glow
- hierarchy rails
- badges
- disclosure
- visibility

## Stage 8 — menus / preview / gallery / shell

Use the same recipes.

Do not create a second visual system.

## Stage 9 — rebuild UI STYLE

Build the panel from the new schema once property groups are stable.

## Stage 10 — delete old system

Remove all obsolete tokens, registries, painters, styles, and compatibility code.

The final codebase must have one style system.

---

# 83. Temporary bridge policy

A short-lived old/new bridge during implementation is acceptable.

The finished rewrite must not ship with two competing systems.

---

# 84. Recommended first milestone

Prove the architecture with:

```text
Inter typography
Well
Slider fill
Toggle
Group button
UI STYLE tabs
```

Why:

- Inter proves typography ownership
- Well proves Multiply
- Fill proves Additive + Multiply layering
- Toggle proves recipe reuse
- Group button proves body/hairline blend separation
- UI STYLE proves schema/editor design

---

# 85. Second milestone

```text
Foldout
Group Card
```

Validate continuous falloff, seam, saturation, and header/body hierarchy.

---

# 86. Third milestone

```text
Layer stack
Hierarchy rails
```

This is the main visual stress test.

---

# 87. Final cleanup milestone

Delete:

- old token registry
- old palette state colors
- old LiveTheme implementation
- old compatibility code
- unused brushes/styles
- old font paths
- duplicate painter logic

Then audit for:

```text
FCoreStyle::GetDefaultFontStyle
old theme macros
legacy token names
old ResolveColor string lookups
duplicate blend functions
dead UI STYLE fields
```

---

# 88. Definition of done

The rewrite is complete when:

1. Inter is actually rendered from the shipped font resource.
2. Mixtormat typography is centralized.
3. UI STYLE has real tabs and explicit sections.
4. No category-prefix inference remains.
5. No legacy compatibility code remains.
6. Palette is substantially smaller.
7. Wells are recipe-driven.
8. Fills are recipe-driven.
9. Buttons are recipe-driven.
10. Foldouts are recipe-driven.
11. Cards are recipe-driven.
12. Layers are recipe-driven.
13. Every exposed blend dropdown has a real reader.
14. Additive/Multiply/SoftLight use one implementation.
15. Body/hairline/shade/glow blend scopes are independent.
16. UI STYLE numeric edits commit cleanly.
17. Booleans are real toggles.
18. Choices are real dropdowns.
19. UI STYLE exposes no dead values.
20. Hierarchy rails paint above row gradients.
21. Drag/drop behavior is unchanged.
22. Splitters remain functional.
23. Gallery zoom remains functional.
24. No permanent styling Tick exists.
25. No duplicate old styling system remains.

---

# 89. Agent implementation instruction

Use this document as the architecture contract.

Before editing:

1. Read current styling implementation.
2. Read `Docs/ui-prototype/tokens.css`.
3. Read `Docs/ui-prototype/components.css`.
4. Read `Docs/ui-prototype/falloff.js`.
5. Inventory interaction behavior.
6. Identify reusable primitives.
7. Produce a concise delete / replace / retain list.

Then implement the rewrite in stages.

Do **not** preserve the old theme architecture.

Do **not** write compatibility shims.

Do **not** keep dead fields “just in case”.

Prefer deleting obsolete code over layering another abstraction on top.

Do not build, test, launch Unreal, run Git, or push unless explicitly asked.

---

# 90. Final architectural target

```text
Palette
    Ground
    Panel
    Text
    Accent
    Shade
    Hairline

Recipes
    Well
        Ground
        + Multiply Shade

    Fill
        + Additive Accent
        + Multiply Shade

    Button
        + configurable Accent body
        + Additive hairline

    Foldout
        Ground
        + lift
        + accent pass
        + hairline

    Card
        Ground
        + continuous falloff/reach

    Layer
        Ground
        + body
        + group/child modifier
        + selected glow

Hierarchy
    foreground rails

Typography
    true Inter

UI STYLE
    edits recipes, not accidental implementation details
```

That is the rewrite target.
