#include "Style/MixtormatStyle.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatPalette.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"

#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateStyle.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"

namespace MixtormatStylePrivate
{
	const FName StyleSetName(TEXT("MixtormatStyle"));

	FLinearColor WithOpacity(FLinearColor Color, const float Opacity)
	{
		Color.A = Opacity;
		return Color;
	}

	// The component exploration specified colours as sRGB hex. Converting here keeps the source
	// values legible and identical to what was designed, instead of hand-derived linear floats.
	FLinearColor Hex(const uint32 RGB)
	{
		return FLinearColor::FromSRGBColor(FColor(
			static_cast<uint8>((RGB >> 16) & 0xFF),
			static_cast<uint8>((RGB >> 8) & 0xFF),
			static_cast<uint8>(RGB & 0xFF)));
	}

	FLinearColor Shade(FLinearColor Color, const float Amount)
	{
		Color.R *= Amount;
		Color.G *= Amount;
		Color.B *= Amount;
		return Color;
	}
}

TSharedPtr<FSlateStyleSet> FMixtormatStyle::StyleInstance;

void FMixtormatStyle::Initialize()
{
	if (StyleInstance.IsValid())
	{
		return;
	}

	using namespace MixtormatStylePrivate;

	const ISlateStyle& AppStyle = FAppStyle::Get();

	// The editor is intentionally independent of the host-editor theme. These local aliases keep
	// the style registration readable while every actual colour remains defined in the palette.
	const FLinearColor Window = MixtormatPalette::Window();
	const FLinearColor TopBar = MixtormatPalette::TopBar();
	const FLinearColor Panel = MixtormatPalette::Panel();
	const FLinearColor RaisedPanel = MixtormatPalette::RaisedPanel();
	const FLinearColor RaisedPanelHover = MixtormatPalette::RaisedPanelHover();
	const FLinearColor Viewport = MixtormatPalette::Viewport();
	const FLinearColor ThumbnailBackground = MixtormatPalette::ThumbnailBackground();
	const FLinearColor Border = MixtormatPalette::Border();
	const FLinearColor BorderStrong = MixtormatPalette::BorderStrong();
	const FLinearColor Shadow = MixtormatPalette::Shadow();
	const FLinearColor Inset = MixtormatPalette::Inset();
	const FLinearColor Accent = MixtormatPalette::Accent();
	const FLinearColor AccentHover = MixtormatPalette::AccentBright();
	const FLinearColor AccentPressed = MixtormatPalette::Accent();
	const FLinearColor SelectionFill = MixtormatPalette::SelectionFill();
	const FLinearColor FocusFill = MixtormatPalette::FocusFill();
	const FLinearColor Text = MixtormatPalette::RowText();
	const FLinearColor MutedText = MixtormatPalette::HeaderText();
	const FLinearColor DisabledText = MixtormatPalette::DisabledText();
	const FLinearColor Icon = MixtormatPalette::RowText();
	const FLinearColor HeaderText = MixtormatPalette::HeaderText();
	const FLinearColor CaptionText = MixtormatPalette::CaptionText();
	const FLinearColor RowText = MixtormatPalette::RowText();
	const FLinearColor TroughSurface = MixtormatPalette::WellBottom();
	const FLinearColor TroughLine = MixtormatPalette::WellOutline();

	StyleInstance = MakeShared<FSlateStyleSet>(GetStyleSetName());
	// Resolved through the plugin rather than assembled from a folder name: a plugin's
	// name comes from its .uplugin, which need not match the directory containing it, and
	// assuming they match is what silently emptied the icon set during the rename.
	const TSharedPtr<IPlugin> StylePlugin = IPluginManager::Get().FindPlugin(TEXT("MaterialLab"));
	check(StylePlugin.IsValid());
	StyleInstance->SetContentRoot(StylePlugin->GetBaseDir() / TEXT("Resources"));

	StyleInstance->Set(
		TEXT("Mixtormat.Window"),
		new FSlateColorBrush(Window));
	StyleInstance->Set(
		TEXT("Mixtormat.TopBar"),
		new FSlateColorBrush(TopBar));
	StyleInstance->Set(
		TEXT("Mixtormat.PanelShadow"),
		new FSlateRoundedBoxBrush(Shadow, 7.0f));
	// The ground both columns sit on. Darker than the rows and groups stacked in it, so those
	// read as raised sheets -- and with no outline, because contrast here comes from the shade
	// difference rather than from a drawn edge.
	StyleInstance->Set(
		TEXT("Mixtormat.Panel"),
		new FSlateRoundedBoxBrush(MixtormatPalette::Shell(), MixtormatTokens::CornerRadius));
	StyleInstance->Set(
		TEXT("Mixtormat.InsetPanel"),
		new FSlateRoundedBoxBrush(Inset, 4.0f, Shadow, 0.45f));
	StyleInstance->Set(
		TEXT("Mixtormat.SectionBar"),
		new FSlateRoundedBoxBrush(RaisedPanel, 1.0f, Border, 0.3f));
	StyleInstance->Set(
		TEXT("Mixtormat.DragGhost"),
		new FSlateRoundedBoxBrush(RaisedPanel, 6.0f, BorderStrong, 0.8f));
	StyleInstance->Set(
		TEXT("Mixtormat.DragGhostAccent"),
		new FSlateRoundedBoxBrush(SelectionFill, 6.0f, AccentHover, 1.0f));
	StyleInstance->Set(
		TEXT("Mixtormat.TabUnderline"),
		new FSlateColorBrush(MixtormatPalette::Divider()));
	StyleInstance->Set(
		TEXT("Mixtormat.TabUnderlineSelected"),
		new FSlateColorBrush(Accent));
	StyleInstance->Set(
		TEXT("Mixtormat.CompactRow"),
		new FSlateRoundedBoxBrush(RaisedPanel, 1.0f, Border, 0.25f));
	StyleInstance->Set(
		TEXT("Mixtormat.CompactRowValidDrop"),
		new FSlateRoundedBoxBrush(FocusFill, 1.0f, AccentHover, 0.65f));

	FTextBlockStyle SectionHeader = FTextBlockStyle()
		.SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), MixtormatTokens::FontGroupHeader))
		.SetColorAndOpacity(HeaderText)
		.SetShadowOffset(FVector2D::ZeroVector)
		.SetShadowColorAndOpacity(FLinearColor::Transparent);
	FSlateFontInfo SectionHeaderFont = SectionHeader.Font;
	SectionHeaderFont.LetterSpacing = MixtormatTokens::GroupHeaderLetterSpacing;
	SectionHeader.SetFont(SectionHeaderFont);
	StyleInstance->Set(TEXT("Mixtormat.SectionHeader"), SectionHeader);

	FTextBlockStyle Muted = FTextBlockStyle()
		.SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), MixtormatTokens::FontBody))
		.SetColorAndOpacity(MutedText)
		.SetShadowOffset(FVector2D::ZeroVector)
		.SetShadowColorAndOpacity(FLinearColor::Transparent);
	StyleInstance->Set(TEXT("Mixtormat.MutedText"), Muted);

	// Invisible in every state. It exists to catch the click and nothing else; the header bar
	// behind it draws the normal and hovered surfaces.
	FButtonStyle InspectorHeaderButton = FButtonStyle()
		.SetNormal(FSlateNoResource())
		.SetHovered(FSlateNoResource())
		.SetPressed(FSlateNoResource())
		.SetDisabled(FSlateNoResource())
		.SetNormalForeground(FSlateColor(HeaderText))
		.SetHoveredForeground(FSlateColor(Text))
		.SetPressedForeground(FSlateColor(Text))
		.SetDisabledForeground(FSlateColor(DisabledText))
		.SetNormalPadding(FMargin(0.0f))
		.SetPressedPadding(FMargin(0.0f));
	StyleInstance->Set(TEXT("Mixtormat.InspectorHeaderButton"), InspectorHeaderButton);

	StyleInstance->Set(
		TEXT("Mixtormat.ThumbnailBackground"),
		new FSlateRoundedBoxBrush(ThumbnailBackground, 2.0f));

	FButtonStyle ThumbnailCard = FButtonStyle()
		// Material cards have no outline: the image must read as edge-to-edge, without the
		// stale selection line that Slate's thumbnail plate can leave along its lower edge.
		.SetNormal(FSlateRoundedBoxBrush(ThumbnailBackground, MixtormatTokens::CornerRadius))
		.SetHovered(FSlateRoundedBoxBrush(RaisedPanelHover, MixtormatTokens::CornerRadius))
		.SetPressed(FSlateRoundedBoxBrush(Panel, MixtormatTokens::CornerRadius))
		.SetDisabled(FSlateRoundedBoxBrush(ThumbnailBackground, MixtormatTokens::CornerRadius))
		.SetNormalForeground(FSlateColor(Text))
		.SetHoveredForeground(FSlateColor(Text))
		.SetPressedForeground(FSlateColor(Text))
		.SetDisabledForeground(FSlateColor(DisabledText))
		.SetNormalPadding(FMargin(2.0f))
		.SetPressedPadding(FMargin(2.0f, 3.0f, 2.0f, 1.0f));
	StyleInstance->Set(TEXT("Mixtormat.ThumbnailCard"), ThumbnailCard);

	FButtonStyle TopButton = FButtonStyle()
		.SetNormal(FSlateRoundedBoxBrush(FLinearColor::Transparent, MixtormatTokens::CornerRadius))
		.SetHovered(FSlateRoundedBoxBrush(RaisedPanelHover, MixtormatTokens::CornerRadius))
		.SetPressed(FSlateRoundedBoxBrush(Panel, MixtormatTokens::CornerRadius))
		.SetDisabled(FSlateRoundedBoxBrush(FLinearColor::Transparent, MixtormatTokens::CornerRadius))
		.SetNormalForeground(FSlateColor(Icon))
		.SetHoveredForeground(FSlateColor(Text))
		.SetPressedForeground(FSlateColor(Accent))
		.SetDisabledForeground(FSlateColor(DisabledText))
		.SetNormalPadding(FMargin(MixtormatTokens::ButtonPaddingCompact, 0.0f))
		.SetPressedPadding(FMargin(MixtormatTokens::ButtonPaddingCompact, 0.0f));
	StyleInstance->Set(TEXT("Mixtormat.TopButton"), TopButton);

	FButtonStyle PrimaryButton = FButtonStyle()
		.SetNormal(FSlateRoundedBoxBrush(RaisedPanel, MixtormatTokens::CornerRadius, BorderStrong, 0.35f))
		.SetHovered(FSlateRoundedBoxBrush(RaisedPanelHover, MixtormatTokens::CornerRadius, BorderStrong, 0.5f))
		.SetPressed(FSlateRoundedBoxBrush(FocusFill, MixtormatTokens::CornerRadius, AccentPressed, 0.65f))
		.SetDisabled(FSlateRoundedBoxBrush(Panel, MixtormatTokens::CornerRadius, Border, 0.25f))
		.SetNormalForeground(FSlateColor(Text))
		.SetHoveredForeground(FSlateColor(Text))
		.SetPressedForeground(FSlateColor(Text))
		.SetDisabledForeground(FSlateColor(DisabledText))
		.SetNormalPadding(FMargin(MixtormatTokens::ButtonPaddingPrimary, 0.0f))
		.SetPressedPadding(FMargin(MixtormatTokens::ButtonPaddingPrimary, 0.0f));
	StyleInstance->Set(TEXT("Mixtormat.PrimaryButton"), PrimaryButton);

	FButtonStyle TabButton = FButtonStyle()
		.SetNormal(FSlateRoundedBoxBrush(FLinearColor::Transparent, MixtormatTokens::CornerRadius))
		.SetHovered(FSlateRoundedBoxBrush(RaisedPanelHover, MixtormatTokens::CornerRadius))
		.SetPressed(FSlateRoundedBoxBrush(FocusFill, MixtormatTokens::CornerRadius))
		.SetNormalForeground(FSlateColor(MutedText))
		.SetHoveredForeground(FSlateColor(Text))
		.SetPressedForeground(FSlateColor(Text))
		.SetNormalPadding(FMargin(MixtormatTokens::ButtonPaddingTab, 0.0f))
		.SetPressedPadding(FMargin(MixtormatTokens::ButtonPaddingTab, 0.0f));
	StyleInstance->Set(TEXT("Mixtormat.TabButton"), TabButton);

	FButtonStyle TabButtonActive = FButtonStyle()
		.SetNormal(FSlateRoundedBoxBrush(SelectionFill, MixtormatTokens::CornerRadius, Accent, 0.45f))
		.SetHovered(FSlateRoundedBoxBrush(FocusFill, MixtormatTokens::CornerRadius, AccentHover, 0.55f))
		.SetPressed(FSlateRoundedBoxBrush(FocusFill, MixtormatTokens::CornerRadius, AccentPressed, 0.65f))
		.SetNormalForeground(FSlateColor(Text))
		.SetHoveredForeground(FSlateColor(Text))
		.SetPressedForeground(FSlateColor(Text))
		.SetNormalPadding(FMargin(MixtormatTokens::ButtonPaddingTab, 0.0f))
		.SetPressedPadding(FMargin(MixtormatTokens::ButtonPaddingTab, 0.0f));
	StyleInstance->Set(TEXT("Mixtormat.TabButtonActive"), TabButtonActive);

	FCheckBoxStyle TabToggle = FCheckBoxStyle()
		.SetCheckBoxType(ESlateCheckBoxType::ToggleButton)
		.SetUncheckedImage(FSlateRoundedBoxBrush(FLinearColor::Transparent, MixtormatTokens::CornerRadius))
		.SetUncheckedHoveredImage(FSlateRoundedBoxBrush(RaisedPanelHover, MixtormatTokens::CornerRadius))
		.SetUncheckedPressedImage(FSlateRoundedBoxBrush(FocusFill, MixtormatTokens::CornerRadius))
		.SetCheckedImage(FSlateRoundedBoxBrush(SelectionFill, MixtormatTokens::CornerRadius))
		.SetCheckedHoveredImage(FSlateRoundedBoxBrush(FocusFill, MixtormatTokens::CornerRadius))
		.SetCheckedPressedImage(FSlateRoundedBoxBrush(FocusFill, MixtormatTokens::CornerRadius))
		.SetPadding(FMargin(MixtormatTokens::ButtonPaddingTab, 0.0f));
	StyleInstance->Set(TEXT("Mixtormat.TabToggle"), TabToggle);

	// Icon-only toggle: the glyph carries the state through its colour, so there is no plate
	// behind it in any state. A filled background on a 16px eye reads as a button and competes
	// with the header title next to it.
	FCheckBoxStyle ViewportOverlayToggle = FCheckBoxStyle()
		.SetCheckBoxType(ESlateCheckBoxType::ToggleButton)
		.SetUncheckedImage(FSlateNoResource())
		.SetUncheckedHoveredImage(FSlateNoResource())
		.SetUncheckedPressedImage(FSlateNoResource())
		.SetCheckedImage(FSlateNoResource())
		.SetCheckedHoveredImage(FSlateNoResource())
		.SetCheckedPressedImage(FSlateNoResource())
		.SetUndeterminedImage(FSlateNoResource())
		.SetUndeterminedHoveredImage(FSlateNoResource())
		.SetUndeterminedPressedImage(FSlateNoResource())
		.SetForegroundColor(FSlateColor(WithOpacity(Text, 0.55f)))
		.SetHoveredForegroundColor(FSlateColor(Text))
		.SetPressedForegroundColor(FSlateColor(Text))
		.SetCheckedForegroundColor(FSlateColor(MixtormatPalette::AccentBright()))
		.SetCheckedHoveredForegroundColor(FSlateColor(MixtormatPalette::AccentBright()))
		.SetCheckedPressedForegroundColor(FSlateColor(MixtormatPalette::AccentBright()))
		.SetPadding(FMargin(2.0f));
	StyleInstance->Set(TEXT("Mixtormat.ViewportOverlayToggle"), ViewportOverlayToggle);

	// The inspector's boolean. Every image is empty on purpose: SMixtormatToggle paints the well
	// and its fill as gradients, which no brush can express, so the checkbox underneath is left
	// as pure behaviour -- hit testing, keyboard, the toggled callback -- with nothing of its own
	// on screen. Zero padding for the same reason: the toggle sizes itself.
	//
	// Note this is a CheckBox, not a ToggleButton like the two above. Those are pressed-button
	// chrome for the tab bar and the viewport overlay; this is a checkbox that happens not to
	// draw a check.
	FCheckBoxStyle InspectorToggle = FCheckBoxStyle()
		.SetCheckBoxType(ESlateCheckBoxType::CheckBox)
		.SetUncheckedImage(FSlateNoResource())
		.SetUncheckedHoveredImage(FSlateNoResource())
		.SetUncheckedPressedImage(FSlateNoResource())
		.SetCheckedImage(FSlateNoResource())
		.SetCheckedHoveredImage(FSlateNoResource())
		.SetCheckedPressedImage(FSlateNoResource())
		.SetUndeterminedImage(FSlateNoResource())
		.SetUndeterminedHoveredImage(FSlateNoResource())
		.SetUndeterminedPressedImage(FSlateNoResource())
		.SetBackgroundImage(FSlateNoResource())
		.SetBackgroundHoveredImage(FSlateNoResource())
		.SetBackgroundPressedImage(FSlateNoResource())
		.SetPadding(FMargin(0.0f));
	StyleInstance->Set(TEXT("Mixtormat.Toggle"), InspectorToggle);

	// The plain-button twin of the toggle above, for the combo and push buttons that share a rail
	// with it.
	//
	// Those were on Mixtormat.TopButton, which is a panel button: it draws a rounded plate when
	// hovered and pressed, and pads 7 horizontal against 0 vertical. In a square overlay button
	// that read as a border the toggles beside it did not have, and the asymmetric padding left a
	// 10x24 hole that stretched the glyph. Every state here is resourceless like the toggle, so
	// the rail responds in foreground colour only, and the padding is square.
	FButtonStyle ViewportOverlayButton = FButtonStyle()
		.SetNormal(FSlateNoResource())
		.SetHovered(FSlateNoResource())
		.SetPressed(FSlateNoResource())
		.SetDisabled(FSlateNoResource())
		.SetNormalForeground(FSlateColor(WithOpacity(Text, 0.55f)))
		.SetHoveredForeground(FSlateColor(Text))
		.SetPressedForeground(FSlateColor(MixtormatPalette::AccentBright()))
		.SetDisabledForeground(FSlateColor(DisabledText))
		.SetNormalPadding(FMargin(MixtormatTokens::ViewportOverlayTogglePadding))
		.SetPressedPadding(FMargin(MixtormatTokens::ViewportOverlayTogglePadding));
	StyleInstance->Set(TEXT("Mixtormat.ViewportOverlayButton"), ViewportOverlayButton);

	FButtonStyle CompactRowButton = FButtonStyle()
		.SetNormal(FSlateRoundedBoxBrush(RaisedPanel, 1.0f, Border, 0.25f))
		.SetHovered(FSlateRoundedBoxBrush(RaisedPanelHover, 1.0f, BorderStrong, 0.35f))
		.SetPressed(FSlateRoundedBoxBrush(FocusFill, 1.0f, Accent, 0.5f))
		.SetDisabled(FSlateRoundedBoxBrush(Panel, 1.0f, Border, 0.2f))
		.SetNormalForeground(FSlateColor(Text))
		.SetHoveredForeground(FSlateColor(Text))
		.SetPressedForeground(FSlateColor(Text))
		.SetDisabledForeground(FSlateColor(DisabledText))
		.SetNormalPadding(FMargin(4.0f, 1.0f))
		.SetPressedPadding(FMargin(4.0f, 2.0f, 4.0f, 0.0f));
	StyleInstance->Set(TEXT("Mixtormat.CompactRowButton"), CompactRowButton);

	FSpinBoxStyle ScrubControl = AppStyle.GetWidgetStyle<FSpinBoxStyle>(TEXT("NumericEntrySpinBox"));
	ScrubControl
		.SetBackgroundBrush(FSlateRoundedBoxBrush(MixtormatPalette::WellBottom(), MixtormatTokens::CornerRadiusInner, Border, 0.25f))
		.SetHoveredBackgroundBrush(FSlateRoundedBoxBrush(RaisedPanelHover, 1.0f, BorderStrong, 0.35f))
		.SetActiveFillBrush(FSlateRoundedBoxBrush(FocusFill, 1.0f, Accent, 0.4f))
		.SetInactiveFillBrush(FSlateRoundedBoxBrush(FLinearColor::Transparent, 1.0f))
		.SetForegroundColor(FSlateColor(Text))
		.SetTextPadding(FMargin(3.0f, 0.0f));

	// ---------------------------------------------------------------------------------------
	// Design system. Geometry lives in MixtormatDesignTokens.h; the colours have to resolve
	// against FAppStyle at runtime, so they live here. Widgets read both and hard-code neither.
	// ---------------------------------------------------------------------------------------

	// Two corrections to the palette above, both found by resolving it rather than eyeballing it:
	//
	// `Colors.SelectHover` is aliased to `Panel` by Unreal (#242424) -- it is not a brighter
	// selection blue. Anything built on it as an "accent, but hovered" goes grey on hover instead
	// of brightening, which is the opposite of the intent.
	//
	// `Colors.Secondary` is #383838, an outline grey. As a text colour on the #212121 panel it is
	// nearly invisible, so subdued text derives from Foreground instead.
	const FLinearColor AccentBright = MixtormatPalette::AccentBright();
	const FLinearColor SubduedText = MixtormatPalette::HeaderText();
	const FLinearColor ModifiedMarker = MixtormatPalette::Modified();

	// Blender-style value slider: one bar carrying label, fill and value. Kept as loose keys
	// rather than a widget style so SMixtormatSlider can paint the fill clipped to the value
	// fraction, which no stock Slate style expresses.
	//
	// The trough gets its own colour rather than reusing one of the panel shades. It first used
	// `Colors.Input` raw, which a small spin box gets away with but a full-width bar does not --
	// a column of them read as pale slabs. Correcting that to `Inset` overshot in the other
	// direction: `Mixtormat.InspectorGroup` is also `Inset`, so every bar was exactly the colour
	// of the panel it sat on and the control vanished entirely. It needs to contrast with the
	// group background, which means darker than any panel shade, with an outline to define it.
	StyleInstance->Set(
		TEXT("Mixtormat.ValueSlider.Background"),
		new FSlateRoundedBoxBrush(TroughSurface, MixtormatTokens::CornerRadius, TroughLine, MixtormatTokens::OutlineWidth));
	StyleInstance->Set(
		TEXT("Mixtormat.ValueSlider.BackgroundHovered"),
		new FSlateRoundedBoxBrush(MixtormatPalette::WellBottomHover(), MixtormatTokens::CornerRadius, MixtormatPalette::WellOutlineHover(), MixtormatTokens::OutlineWidth));
	StyleInstance->Set(
		TEXT("Mixtormat.ValueSlider.BackgroundActive"),
		new FSlateRoundedBoxBrush(MixtormatPalette::WellBottomHover(), MixtormatTokens::CornerRadius, MixtormatPalette::Accent(), MixtormatTokens::OutlineWidth));
	StyleInstance->Set(
		TEXT("Mixtormat.ValueSlider.BackgroundEntry"),
		new FSlateRoundedBoxBrush(MixtormatPalette::WellEntry(), MixtormatTokens::CornerRadius, MixtormatPalette::Accent(), MixtormatTokens::OutlineWidth));
	StyleInstance->Set(
		TEXT("Mixtormat.ValueSlider.BackgroundDisabled"),
		new FSlateRoundedBoxBrush(MixtormatPalette::Panel(), MixtormatTokens::CornerRadius, MixtormatPalette::Divider(), MixtormatTokens::OutlineWidth));

	// Centre tick on a signed range, and the modified-from-default stripe.
	// The badge: fixed-size mark carrying a row's composite mode.
	StyleInstance->Set(
		TEXT("Mixtormat.Badge"),
		new FSlateRoundedBoxBrush(MixtormatPalette::BadgeSurface(), 1.0f));
	FTextBlockStyle BadgeText = FTextBlockStyle()
		.SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), MixtormatTokens::FontBadge))
		.SetColorAndOpacity(MixtormatPalette::BadgeText())
		.SetShadowOffset(FVector2D::ZeroVector)
		.SetShadowColorAndOpacity(FLinearColor::Transparent);
	FSlateFontInfo BadgeFont = BadgeText.Font;
	BadgeFont.LetterSpacing = MixtormatTokens::CaptionLetterSpacing;
	BadgeText.SetFont(BadgeFont);
	StyleInstance->Set(TEXT("Mixtormat.BadgeText"), BadgeText);

	// A circle is a rounded box whose radius is half its size.
	StyleInstance->Set(
		TEXT("Mixtormat.StatusDot.Filled"),
		new FSlateRoundedBoxBrush(MixtormatPalette::Accent(), MixtormatTokens::StatusDotSize * 0.5f));
	StyleInstance->Set(
		TEXT("Mixtormat.StatusDot.Hollow"),
		new FSlateRoundedBoxBrush(
			FLinearColor::Transparent,
			MixtormatTokens::StatusDotSize * 0.5f,
			MixtormatPalette::HeaderText(),
			1.0f));
	// Container shell and group body. The header's lip is painted by the gradient box, not
	// brushed, so only these two are flat fills.
	//
	// The well is a step darker than the panel shade so each group reads as a raised block with a
	// margin around it. The body rounds its bottom two corners only -- the header rounds the top
	// two -- so the pair stacks into one rounded rectangle with a flat seam where they meet,
	// rather than two separately rounded slabs. No outline on either: the surround does the
	// separating, which is what a border would otherwise be there to do twice.
	// The body is the same colour as the well it sits in, on purpose: a group is no longer a
	// sheet laid on the column, it is a region of the column with a header over it. What
	// separates one run of rows from the next is the cards inside, which are lighter than both.
	//
	// The body is not rounded for the same reason -- it is the same colour as what is behind it,
	// so a rounded corner there is a shape nobody can see. Only the header rounds, along its top
	// edge, where it does have a colour of its own to be shaped.
	StyleInstance->Set(TEXT("Mixtormat.InspectorWell"), new FSlateColorBrush(MixtormatPalette::GroupSurround()));
	StyleInstance->Set(TEXT("Mixtormat.GroupBody"), new FSlateColorBrush(MixtormatPalette::GroupSurround()));

	// A card: one titled run of rows, raised off the body.
	StyleInstance->Set(
		TEXT("Mixtormat.Card"),
		new FSlateRoundedBoxBrush(MixtormatPalette::Panel(), MixtormatTokens::CornerRadius));

	// Our own popovers, which are widgets rather than multibox rows: a label, the shortcut printed
	// quietly beside it, and the section caption above a run of them.
	{
		FTextBlockStyle MenuLabel = FTextBlockStyle()
			.SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), MixtormatTokens::FontBody))
			.SetColorAndOpacity(MixtormatPalette::RowText())
			.SetShadowOffset(FVector2D::ZeroVector)
			.SetShadowColorAndOpacity(FLinearColor::Transparent);
		StyleInstance->Set(TEXT("Mixtormat.MenuLabel"), MenuLabel);

		FTextBlockStyle MenuShortcut = FTextBlockStyle(MenuLabel)
			.SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), MixtormatTokens::FontCaption))
			.SetColorAndOpacity(MixtormatPalette::ShortcutText());
		StyleInstance->Set(TEXT("Mixtormat.MenuShortcut"), MenuShortcut);

		FTextBlockStyle MenuCaption = FTextBlockStyle(MenuLabel)
			.SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), MixtormatTokens::FontGroupHeader))
			.SetColorAndOpacity(MixtormatPalette::CaptionText());
		FSlateFontInfo MenuCaptionFont = MenuCaption.Font;
		MenuCaptionFont.LetterSpacing = MixtormatTokens::GroupHeaderLetterSpacing;
		MenuCaption.SetFont(MenuCaptionFont);
		StyleInstance->Set(TEXT("Mixtormat.MenuCaption"), MenuCaption);

		StyleInstance->Set(
			TEXT("Mixtormat.MenuSeparator"),
			new FSlateColorBrush(MixtormatPalette::Divider()));
	}

	// The lip along the top edge of a header. A flat colour brush, because it is drawn into a
	// one-pixel box laid over the header's top edge rather than used as a border around it --
	// Slate has no top-only border, and a brush used as a BorderImage fills the whole area.
	StyleInstance->Set(
		TEXT("Mixtormat.HeaderHairline"),
		new FSlateColorBrush(MixtormatPalette::Hairline()));
	StyleInstance->Set(
		TEXT("Mixtormat.HeaderHairlineGlow"),
		new FSlateColorBrush(MixtormatPalette::HairlineGlow()));

	// Layer stack: the enclosing edge, and the two type styles its rows use.
	StyleInstance->Set(TEXT("Mixtormat.LayerEdge"), new FSlateColorBrush(MixtormatPalette::LayerEdge()));

	FTextBlockStyle LayerName = FTextBlockStyle()
		.SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), MixtormatTokens::FontLayerName))
		.SetColorAndOpacity(MixtormatPalette::LayerName())
		.SetShadowOffset(FVector2D::ZeroVector)
		.SetShadowColorAndOpacity(FLinearColor::Transparent)
		.SetOverflowPolicy(ETextOverflowPolicy::Ellipsis);
	StyleInstance->Set(TEXT("Mixtormat.LayerName"), LayerName);

	FTextBlockStyle LayerSource = FTextBlockStyle()
		.SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), MixtormatTokens::FontLayerSource))
		.SetColorAndOpacity(MixtormatPalette::LayerSource())
		.SetShadowOffset(FVector2D::ZeroVector)
		.SetShadowColorAndOpacity(FLinearColor::Transparent)
		.SetOverflowPolicy(ETextOverflowPolicy::Ellipsis);
	FSlateFontInfo LayerSourceFont = LayerSource.Font;
	LayerSourceFont.LetterSpacing = MixtormatTokens::LayerSourceLetterSpacing;
	LayerSource.SetFont(LayerSourceFont);
	StyleInstance->Set(TEXT("Mixtormat.LayerSource"), LayerSource);

	StyleInstance->Set(TEXT("Mixtormat.SegmentSeam"), new FSlateColorBrush(MixtormatPalette::SegmentSeam()));

	StyleInstance->Set(TEXT("Mixtormat.ValueSlider.Tick"), new FSlateColorBrush(MixtormatPalette::Tick()));
	StyleInstance->Set(TEXT("Mixtormat.ValueSlider.Modified"), new FSlateColorBrush(ModifiedMarker));

	FTextBlockStyle SliderLabel = FTextBlockStyle()
		.SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), MixtormatTokens::FontSliderLabel))
		.SetColorAndOpacity(RowText)
		.SetShadowOffset(FVector2D::ZeroVector)
		.SetShadowColorAndOpacity(FLinearColor::Transparent);
	StyleInstance->Set(TEXT("Mixtormat.ValueSlider.Label"), SliderLabel);

	// The value is drawn in the same face forced to a uniform advance, which is what makes a
	// column of numbers line up on the decimal point. Slate exposes no OpenType feature switch,
	// but bForceMonospaced does the same job -- and it is safe here precisely because this style
	// is only ever used for digits, never for the label.
	FTextBlockStyle SliderValue = SliderLabel;
	FSlateFontInfo ValueFont = SliderValue.Font;
	ValueFont.bForceMonospaced = true;
	ValueFont.MonospacedWidth = 0.52f;
	SliderValue.SetFont(ValueFont);
	StyleInstance->Set(TEXT("Mixtormat.ValueSlider.Value"), SliderValue);

	FTextBlockStyle SliderLabelDisabled = SliderLabel;
	SliderLabelDisabled.SetColorAndOpacity(DisabledText);
	StyleInstance->Set(TEXT("Mixtormat.ValueSlider.LabelDisabled"), SliderLabelDisabled);

	FEditableTextBoxStyle SliderEntry =
		AppStyle.GetWidgetStyle<FEditableTextBoxStyle>(TEXT("NormalEditableTextBox"));
	SliderEntry
		.SetBackgroundImageNormal(FSlateRoundedBoxBrush(RaisedPanel, MixtormatTokens::CornerRadius, AccentBright, MixtormatTokens::OutlineWidth))
		.SetBackgroundImageHovered(FSlateRoundedBoxBrush(RaisedPanel, MixtormatTokens::CornerRadius, AccentBright, MixtormatTokens::OutlineWidth))
		.SetBackgroundImageFocused(FSlateRoundedBoxBrush(RaisedPanel, MixtormatTokens::CornerRadius, AccentBright, MixtormatTokens::OutlineWidth))
		.SetForegroundColor(FSlateColor(Text))
		.SetPadding(FMargin(MixtormatTokens::RowTextInset - 1.0f, 0.0f));
	// The entry replaces the value in place, so it matches the face it is typing over.
	SliderEntry.TextStyle.SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), MixtormatTokens::FontSliderLabel));
	StyleInstance->Set(TEXT("Mixtormat.ValueSlider.Entry"), SliderEntry);

	// ---- Row furniture ----------------------------------------------------------------------
	// Sub-group caption, and the hairline that separates two runs of rows without naming them.
	FTextBlockStyle RowCaption = FTextBlockStyle()
		.SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), MixtormatTokens::FontCaption))
		.SetColorAndOpacity(CaptionText)
		.SetShadowOffset(FVector2D::ZeroVector)
		.SetShadowColorAndOpacity(FLinearColor::Transparent);
	FSlateFontInfo CaptionFont = RowCaption.Font;
	CaptionFont.LetterSpacing = MixtormatTokens::CaptionLetterSpacing;
	RowCaption.SetFont(CaptionFont);
	StyleInstance->Set(TEXT("Mixtormat.RowCaption"), RowCaption);

	FTextBlockStyle RowLabel = SliderLabel;
	RowLabel.SetOverflowPolicy(ETextOverflowPolicy::Ellipsis);
	StyleInstance->Set(TEXT("Mixtormat.RowLabel"), RowLabel);

	StyleInstance->Set(TEXT("Mixtormat.Hairline"), new FSlateColorBrush(Border));

	// ---- Thumbnail tiles --------------------------------------------------------------------
	// One tile serves the library, the mask replacement grid and the mask picker. The name strip
	// is an overlay on the image, so showing it costs picture rather than layout height.
	StyleInstance->Set(
		TEXT("Mixtormat.Tile.Normal"),
		new FSlateRoundedBoxBrush(ThumbnailBackground, MixtormatTokens::CornerRadius, Border, MixtormatTokens::OutlineWidth));
	StyleInstance->Set(
		TEXT("Mixtormat.Tile.Hovered"),
		new FSlateRoundedBoxBrush(ThumbnailBackground, MixtormatTokens::CornerRadius, WithOpacity(Text, 0.42f), MixtormatTokens::OutlineWidth));
	StyleInstance->Set(
		TEXT("Mixtormat.Tile.Selected"),
		new FSlateRoundedBoxBrush(ThumbnailBackground, MixtormatTokens::CornerRadius, AccentBright, MixtormatTokens::OutlineWidth));
	StyleInstance->Set(
		TEXT("Mixtormat.Tile.NameStrip"),
		new FSlateColorBrush(MixtormatPalette::TileNameStrip()));

	FTextBlockStyle TileName = FTextBlockStyle()
		.SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), MixtormatTokens::FontTile))
		.SetColorAndOpacity(MixtormatPalette::TileNameText())
		.SetShadowOffset(FVector2D::ZeroVector)
		.SetShadowColorAndOpacity(FLinearColor::Transparent)
		.SetOverflowPolicy(ETextOverflowPolicy::Ellipsis);
	StyleInstance->Set(TEXT("Mixtormat.Tile.Name"), TileName);

	// ---- Drag and drop ----------------------------------------------------------------------
	// A line in the gutter means "between"; a tinted row means "into". They have to look
	// different, or dropping an effect beside a layer versus into it is a coin flip.

	const auto SetIcon = [&Icon](
		const FName Key,
		const TCHAR* FileName,
		const FVector2D Size)
	{
		StyleInstance->Set(
			Key,
			new FSlateVectorImageBrush(
				StyleInstance->RootToContentDir(FileName, TEXT(".svg")),
				Size,
				FSlateColor(Icon)));
	};

	SetIcon(TEXT("Mixtormat.Icon.Save"), TEXT("Icons/save"), FVector2D(MixtormatTokens::IconBrushSize, MixtormatTokens::IconBrushSize));
	SetIcon(TEXT("Mixtormat.Icon.SaveAs"), TEXT("Icons/save-all"), FVector2D(MixtormatTokens::IconBrushSize, MixtormatTokens::IconBrushSize));
	SetIcon(TEXT("Mixtormat.Icon.Overflow"), TEXT("Icons/ellipsis"), FVector2D(MixtormatTokens::IconBrushSize, MixtormatTokens::IconBrushSize));
	SetIcon(TEXT("Mixtormat.Icon.Add"), TEXT("Icons/plus"), FVector2D(MixtormatTokens::IconBrushSize, MixtormatTokens::IconBrushSize));
	SetIcon(TEXT("Mixtormat.Icon.Eye"), TEXT("Icons/eye"), FVector2D(MixtormatTokens::IconBrushSize, MixtormatTokens::IconBrushSize));
	SetIcon(TEXT("Mixtormat.Icon.EyeOff"), TEXT("Icons/eye-off"), FVector2D(MixtormatTokens::IconBrushSize, MixtormatTokens::IconBrushSize));
	SetIcon(TEXT("Mixtormat.Icon.Duplicate"), TEXT("Icons/copy"), FVector2D(MixtormatTokens::IconBrushSize, MixtormatTokens::IconBrushSize));
	SetIcon(TEXT("Mixtormat.Icon.Folder"), TEXT("Icons/folder-open"), FVector2D(MixtormatTokens::IconBrushSize, MixtormatTokens::IconBrushSize));
	SetIcon(TEXT("Mixtormat.Icon.Refresh"), TEXT("Icons/refresh-cw"), FVector2D(MixtormatTokens::IconBrushSize, MixtormatTokens::IconBrushSize));
	SetIcon(TEXT("Mixtormat.Icon.Trash"), TEXT("Icons/trash-2"), FVector2D(MixtormatTokens::IconBrushSize, MixtormatTokens::IconBrushSize));
	SetIcon(TEXT("Mixtormat.Icon.Grip"), TEXT("Icons/grip-vertical"), FVector2D(MixtormatTokens::IconBrushSize, MixtormatTokens::IconBrushSize));
	SetIcon(TEXT("Mixtormat.Icon.ArrowUp"), TEXT("Icons/arrow-up"), FVector2D(MixtormatTokens::IconBrushSize, MixtormatTokens::IconBrushSize));
	SetIcon(TEXT("Mixtormat.Icon.ArrowDown"), TEXT("Icons/arrow-down"), FVector2D(MixtormatTokens::IconBrushSize, MixtormatTokens::IconBrushSize));
	SetIcon(TEXT("Mixtormat.Icon.Cube"), TEXT("Icons/box"), FVector2D(MixtormatTokens::IconBrushSize, MixtormatTokens::IconBrushSize));
	SetIcon(TEXT("Mixtormat.Icon.Sphere"), TEXT("Icons/circle"), FVector2D(MixtormatTokens::IconBrushSize, MixtormatTokens::IconBrushSize));
	SetIcon(TEXT("Mixtormat.Icon.Plane"), TEXT("Icons/rectangle-horizontal"), FVector2D(MixtormatTokens::IconBrushSize, MixtormatTokens::IconBrushSize));
	SetIcon(TEXT("Mixtormat.Icon.Globe"), TEXT("Icons/globe"), FVector2D(MixtormatTokens::IconBrushSizeLarge, MixtormatTokens::IconBrushSizeLarge));
	SetIcon(TEXT("Mixtormat.Icon.Nodes"), TEXT("Icons/workflow"), FVector2D(MixtormatTokens::IconBrushSizeLarge, MixtormatTokens::IconBrushSizeLarge));
	SetIcon(TEXT("Mixtormat.Icon.Camera"), TEXT("Icons/camera"), FVector2D(MixtormatTokens::IconBrushSizeLarge, MixtormatTokens::IconBrushSizeLarge));
	SetIcon(TEXT("Mixtormat.Icon.Search"), TEXT("Icons/search"), FVector2D(MixtormatTokens::IconBrushSize, MixtormatTokens::IconBrushSize));
	SetIcon(TEXT("Mixtormat.Icon.Documentation"), TEXT("Icons/book-open-text"), FVector2D(MixtormatTokens::IconBrushSizeLarge, MixtormatTokens::IconBrushSizeLarge));
	SetIcon(TEXT("Mixtormat.Icon.Feedback"), TEXT("Icons/message-square"), FVector2D(MixtormatTokens::IconBrushSizeLarge, MixtormatTokens::IconBrushSizeLarge));
	SetIcon(TEXT("Mixtormat.Icon.LightNeutral"), TEXT("Icons/sun"), FVector2D(MixtormatTokens::IconBrushSize, MixtormatTokens::IconBrushSize));
	SetIcon(TEXT("Mixtormat.Icon.LightSoft"), TEXT("Icons/cloud-sun"), FVector2D(MixtormatTokens::IconBrushSize, MixtormatTokens::IconBrushSize));
	SetIcon(TEXT("Mixtormat.Icon.LightDramatic"), TEXT("Icons/contrast"), FVector2D(MixtormatTokens::IconBrushSize, MixtormatTokens::IconBrushSize));
	SetIcon(TEXT("Mixtormat.Icon.LightRim"), TEXT("Icons/sunrise"), FVector2D(MixtormatTokens::IconBrushSize, MixtormatTokens::IconBrushSize));
	SetIcon(TEXT("Mixtormat.Icon.QualityLow"), TEXT("Icons/signal-low"), FVector2D(MixtormatTokens::IconBrushSize, MixtormatTokens::IconBrushSize));
	SetIcon(TEXT("Mixtormat.Icon.QualityMedium"), TEXT("Icons/signal-medium"), FVector2D(MixtormatTokens::IconBrushSize, MixtormatTokens::IconBrushSize));
	SetIcon(TEXT("Mixtormat.Icon.QualityHigh"), TEXT("Icons/signal-high"), FVector2D(MixtormatTokens::IconBrushSize, MixtormatTokens::IconBrushSize));

	// What a layer's children are. Each glyph says what kind of thing the child is, since the row
	// beside it is already carrying the name and the blend mode -- a mask outline for something
	// that shapes coverage, a bolt for something that acts on the surface, a shoot for something
	// grown from what is underneath.
	SetIcon(TEXT("Mixtormat.Icon.Mask"), TEXT("Icons/square-dashed"), FVector2D(MixtormatTokens::IconBrushSize, MixtormatTokens::IconBrushSize));
	SetIcon(TEXT("Mixtormat.Icon.Effect"), TEXT("Icons/zap"), FVector2D(MixtormatTokens::IconBrushSize, MixtormatTokens::IconBrushSize));
	SetIcon(TEXT("Mixtormat.Icon.Generated"), TEXT("Icons/sprout"), FVector2D(MixtormatTokens::IconBrushSize, MixtormatTokens::IconBrushSize));
	// Layer kinds. A square for a material, a circle for a fill -- the shapes the add bar uses.
	SetIcon(TEXT("Mixtormat.Icon.LayerMaterial"), TEXT("Icons/box"), FVector2D(MixtormatTokens::IconBrushSize, MixtormatTokens::IconBrushSize));
	SetIcon(TEXT("Mixtormat.Icon.LayerFill"), TEXT("Icons/circle"), FVector2D(MixtormatTokens::IconBrushSize, MixtormatTokens::IconBrushSize));

	// Disclosure. These were being borrowed from FAppStyle, which meant the one glyph in the stack
	// that is not ours changed weight whenever the editor theme did.
	SetIcon(TEXT("Mixtormat.Icon.ChevronDown"), TEXT("Icons/chevron-down"), FVector2D(MixtormatTokens::IconBrushSize, MixtormatTokens::IconBrushSize));
	SetIcon(TEXT("Mixtormat.Icon.ChevronRight"), TEXT("Icons/chevron-right"), FVector2D(MixtormatTokens::IconBrushSize, MixtormatTokens::IconBrushSize));
	SetIcon(TEXT("Mixtormat.Icon.Check"), TEXT("Icons/check"), FVector2D(MixtormatTokens::IconBrushSize, MixtormatTokens::IconBrushSize));

	// Brand marks. The source art is 53.46 x 58.07 for the icon and 297.14 x 58.07 for the
	// logo, so every size below holds those ratios rather than squashing the glyph.
	SetIcon(TEXT("Mixtormat.Brand.Icon"), TEXT("Icons/mixtormat-icon"), FVector2D(MixtormatTokens::BrandIconWidth, MixtormatTokens::BrandIconHeight));
	SetIcon(TEXT("Mixtormat.Brand.Logo"), TEXT("Icons/mixtormat-logo"), FVector2D(MixtormatTokens::BrandLogoWidth, MixtormatTokens::BrandLogoHeight));

	// Viewport watermark. Tinted dark and mostly transparent so it sits under the material
	// rather than competing with it, and small enough to stay out of the way.
	StyleInstance->Set(
		TEXT("Mixtormat.Brand.Watermark"),
		new FSlateVectorImageBrush(
			StyleInstance->RootToContentDir(TEXT("Icons/mixtormat-icon"), TEXT(".svg")),
			FVector2D(MixtormatTokens::BrandWatermarkWidth, MixtormatTokens::BrandWatermarkHeight),
			FSlateColor(MixtormatPalette::Watermark())));

	FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
}

void FMixtormatStyle::Shutdown()
{
	if (!StyleInstance.IsValid())
	{
		return;
	}

	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

const ISlateStyle& FMixtormatStyle::Get()
{
	check(StyleInstance.IsValid());
	return *StyleInstance;
}

FName FMixtormatStyle::GetStyleSetName()
{
	return MixtormatStylePrivate::StyleSetName;
}
