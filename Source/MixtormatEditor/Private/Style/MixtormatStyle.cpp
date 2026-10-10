// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Style/MixtormatStyle.h"
#include "Style/MixtormatThemeStore.h"
#include "Services/MixtormatPaths.h"
#include "Style/MixtormatMutableStyleSet.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatGroupButtonTokens.h"
#include "Style/MixtormatTypography.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateImageBrush.h"

#include "Brushes/SlateNoResource.h"
#include "Brushes/SlateRoundedBoxBrush.h"

#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateStyle.h"
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

	// The weight a legacy bold-flag token resolves to.
			//
			// These tokens predate the three-face model and are binary, so a true flag means the heavier
			// of the two faces that role uses. ControlValue is deliberately NOT routed through this:
			// tokens.css authors it at 600, so it is called out as SemiBold at its call site.
			Mixtormat::EMixtormatFontWeight Weight(const bool bBoldToken)
			{
				return bBoldToken ? Mixtormat::EMixtormatFontWeight::Bold : Mixtormat::EMixtormatFontWeight::Regular;
			}

			constexpr Mixtormat::EMixtormatFontWeight Regular = Mixtormat::EMixtormatFontWeight::Regular;
			constexpr Mixtormat::EMixtormatFontWeight SemiBold = Mixtormat::EMixtormatFontWeight::SemiBold;
			constexpr Mixtormat::EMixtormatFontWeight Bold = Mixtormat::EMixtormatFontWeight::Bold;

			// All text styles in this file use the centralized native Unreal font backend.
			//
			// `TrackingPx` is CSS pixels and is converted against the size here, once. Tokens that are
			// already in Slate's 1/1000 em are NOT passed through this: they are assigned to
			// FSlateFontInfo::LetterSpacing directly, and converting them again would divide by the
			// size a second time. See the caption tokens in MixtormatDesignTokens.h for which is which.
			FSlateFontInfo Font(const Mixtormat::EMixtormatFontWeight Weight, const float Size, const float TrackingPx = 0.0f)
			{
				return Mixtormat::FMixtormatTypography::MakeFont(Weight, Size, TrackingPx);
			}

			// Some tokens still carry their weight as a 0..1 number and some as a bool. Both answer the
			// same question for their roles, so both go through Weight().
			//
			// Removed: the old `const TCHAR* Weight(float)` that handed "Regular"/"Bold" strings to
			// FCoreStyle. With three addressable faces a string is no longer enough to say which one.

		// Opacity applied to a shared role's alpha rather than to a separate grey role, so two weights
		// of the same colour differ only in strength. Lerping the RGB toward transparent instead would
		// darken the hue as well, which is a different token pretending to be this one.
		FLinearColor AtOpacity(const FLinearColor& Color, const float Opacity)
		{
			FLinearColor Result = Color;
			Result.A *= Opacity;
			return Result;
		}
	}

TSharedPtr<FMixtormatMutableStyleSet> FMixtormatStyle::StyleInstance;

void FMixtormatStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		// Resolve the authored theme before any production style or workspace construction.
		FMixtormatThemeStore::GetResolved();
		Refresh();
	}
}

void FMixtormatStyle::Refresh()
{
	check(IsInGameThread());
	const bool bFirstRegistration = !StyleInstance.IsValid();
	using namespace MixtormatStylePrivate;

	const ISlateStyle& AppStyle = FAppStyle::Get();

	// The editor is intentionally independent of the host-editor theme. These local aliases keep
	// the style registration readable while every actual colour remains defined in the palette.
	const FLinearColor Window = FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Ground);
	const FLinearColor TopBar = FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Shell);
	const FLinearColor Panel = FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Panel);
	const FLinearColor RaisedPanel = FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Panel);
	const FLinearColor RaisedPanelHover = FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Panel);
	const FLinearColor Viewport = FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::OverlayGround);
	const FLinearColor ThumbnailBackground = FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::ThumbnailGround);
	const FLinearColor Border = FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Hairline);
	const FLinearColor BorderStrong = FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Hairline);
	const FLinearColor Shadow = FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Shade);
	const FLinearColor Inset = FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Shade);
	const FLinearColor Accent = FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Accent);
	const FLinearColor AccentHover = FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Accent);
	const FLinearColor AccentPressed = FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Accent);
	const FLinearColor SelectionFill = FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Accent);
	const FLinearColor FocusFill = FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Accent);
	const FLinearColor Text = FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Text);
	const FLinearColor MutedText = FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted);
	const FLinearColor DisabledText = FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted);
	const FLinearColor Icon = FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Text);
	const FLinearColor HeaderText = FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted);
	const FLinearColor CaptionText = FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted);
	const FLinearColor CardTitleText = FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Text);
	const FLinearColor RowText = FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Text);
	const FLinearColor TroughSurface = FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Panel);
	const FLinearColor TroughLine = FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Hairline);

	if (bFirstRegistration)
	{
		StyleInstance = MakeShared<FMixtormatMutableStyleSet>(GetStyleSetName());
	}
	// Resolved through the plugin rather than assembled from a folder name: a plugin's
	// name comes from its .uplugin, which need not match the directory containing it, and
	// assuming they match is what silently emptied the icon set during the rename.
	const TSharedPtr<IPlugin> StylePlugin = FMixtormatPaths::FindPlugin();
	check(StylePlugin.IsValid());
	StyleInstance->SetContentRoot(FMixtormatPaths::ResourcesDir());

	StyleInstance->Set(
		TEXT("Mixtormat.Window"),
		new FSlateColorBrush(Window));
	StyleInstance->Set(
		TEXT("Mixtormat.TopBar"),
		new FSlateColorBrush(TopBar));
	StyleInstance->Set(
		TEXT("Mixtormat.PanelShadow"),
		new FSlateRoundedBoxBrush(Shadow, MixtormatTokens::PanelShadowCornerRadius));
	// The ground both columns sit on. Darker than the rows and groups stacked in it, so those
	// read as raised sheets -- and with no outline, because contrast here comes from the shade
	// difference rather than from a drawn edge.
	StyleInstance->Set(
		TEXT("Mixtormat.Panel"),
		new FSlateRoundedBoxBrush(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Shell), FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius));
	StyleInstance->Set(
		TEXT("Mixtormat.InsetPanel"),
		new FSlateRoundedBoxBrush(Inset, MixtormatTokens::InsetPanelCornerRadius, Shadow, MixtormatTokens::InsetPanelOutlineWidth));
	StyleInstance->Set(
		TEXT("Mixtormat.SectionBar"),
		new FSlateRoundedBoxBrush(RaisedPanel, 1.0f, Border, MixtormatTokens::SectionBarOutlineWidth));
	StyleInstance->Set(
		TEXT("Mixtormat.DragGhost"),
		new FSlateRoundedBoxBrush(RaisedPanel, MixtormatTokens::DragGhostCornerRadius, BorderStrong, MixtormatTokens::DragGhostOutlineWidth));
	StyleInstance->Set(
		TEXT("Mixtormat.DragGhostAccent"),
		new FSlateRoundedBoxBrush(SelectionFill, MixtormatTokens::DragGhostCornerRadius, AccentHover, FMixtormatThemeStore::GetResolved().ControlLayout.OutlineWidth));
	// Tabs join the column below, so only their exposed top corners are rounded. The active
	// plate uses the column ground; the resting plate recedes one shade behind it.
	const FVector4 TabTopCorners(
		FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius,
		FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius,
		0.0f,
		0.0f);
	StyleInstance->Set(
		TEXT("Mixtormat.TabActive"),
		new FSlateRoundedBoxBrush(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Shell), TabTopCorners));
	StyleInstance->Set(
		TEXT("Mixtormat.TabInactive"),
		new FSlateRoundedBoxBrush(Inset, TabTopCorners));
	StyleInstance->Set(
		TEXT("Mixtormat.TabUnderline"),
		new FSlateColorBrush(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Hairline)));
	StyleInstance->Set(
		TEXT("Mixtormat.TabUnderlineSelected"),
		new FSlateColorBrush(Accent));
	StyleInstance->Set(
		TEXT("Mixtormat.CompactRow"),
		new FSlateRoundedBoxBrush(RaisedPanel, 1.0f, Border, MixtormatTokens::CompactRowOutlineWidth));
	// The line drawn where a dragged row will land. A bar rather than an outline: an outline says
	// "onto this row", and the whole point of the line is that it says "between these two".
	StyleInstance->Set(
		TEXT("Mixtormat.DropInsertionLine"),
		new FSlateColorBrush(AccentHover));
	StyleInstance->Set(
		TEXT("Mixtormat.CompactRowValidDrop"),
		new FSlateRoundedBoxBrush(FocusFill, 1.0f, AccentHover, MixtormatTokens::CompactRowValidDropOutlineWidth));

	FTextBlockStyle SectionHeader = FTextBlockStyle()
		.SetFont(Font(Weight(MixtormatTokens::GroupHeaderBold), MixtormatTokens::FontGroupHeader))
		.SetColorAndOpacity(HeaderText)
		.SetShadowOffset(FVector2D::ZeroVector)
		.SetShadowColorAndOpacity(FLinearColor::Transparent);
	FSlateFontInfo SectionHeaderFont = SectionHeader.Font;
	// Already in Slate's 1/1000 em: the caption-tier tracking tokens were authored at their
	// converted value, so they are assigned rather than run through Font()'s px conversion.
	SectionHeaderFont.LetterSpacing = MixtormatTokens::GroupHeaderLetterSpacing;
	SectionHeader.SetFont(SectionHeaderFont);
	StyleInstance->Set(TEXT("Mixtormat.SectionHeader"), SectionHeader);

		// The foldout title is a section heading, not a caption. It was reading SectionHeader above --
		// the compact Group Header tier, 7px tracked caps -- which is a caption's role and made every
		// foldout read as a label above its contents rather than as the name of a section.
		//
		// Tracking converts from the authored px into Slate's 1/1000 em against this face's own
		// size: 1px at a 9px face is 111, not 1. Font() owns that conversion, so the number below
		// stays in the units the prototype authored it in.
		FSlateFontInfo FoldoutTitleFont = Font(
			Weight(MixtormatTokens::FoldoutTitleBold),
			MixtormatTokens::FontFoldoutTitle,
			MixtormatTokens::FoldoutTitleTracking);

		FLinearColor FoldoutTitleColor = RowText;
		FoldoutTitleColor.A *= MixtormatTokens::FoldoutTitleOpacity;

		FTextBlockStyle FoldoutTitle = FTextBlockStyle()
			.SetFont(FoldoutTitleFont)
			.SetColorAndOpacity(FoldoutTitleColor)
			.SetShadowOffset(FVector2D::ZeroVector)
			.SetShadowColorAndOpacity(FLinearColor::Transparent);
		StyleInstance->Set(TEXT("Mixtormat.FoldoutTitle"), FoldoutTitle);

		FTextBlockStyle FoldoutTitleDisabled = FoldoutTitle;
		FoldoutTitleDisabled.SetColorAndOpacity(MixtormatStylePrivate::AtOpacity(
			RowText, MixtormatTokens::FoldoutTitleDisabledOpacity));
		StyleInstance->Set(TEXT("Mixtormat.FoldoutTitleDisabled"), FoldoutTitleDisabled);

	FTextBlockStyle Muted = FTextBlockStyle()
		.SetFont(Font(Regular, MixtormatTokens::FontBody))
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
		new FSlateRoundedBoxBrush(ThumbnailBackground, MixtormatTokens::ThumbnailBackgroundCornerRadius));

	FButtonStyle ThumbnailCard = FButtonStyle()
		// Material cards have no outline: the image must read as edge-to-edge, without the
		// stale selection line that Slate's thumbnail plate can leave along its lower edge.
		.SetNormal(FSlateRoundedBoxBrush(ThumbnailBackground, FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius))
		.SetHovered(FSlateRoundedBoxBrush(RaisedPanelHover, FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius))
		.SetPressed(FSlateRoundedBoxBrush(Panel, FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius))
		.SetDisabled(FSlateRoundedBoxBrush(ThumbnailBackground, FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius))
		.SetNormalForeground(FSlateColor(Text))
		.SetHoveredForeground(FSlateColor(Text))
		.SetPressedForeground(FSlateColor(Text))
		.SetDisabledForeground(FSlateColor(DisabledText))
		.SetNormalPadding(FMargin(FMixtormatThemeStore::GetResolved().ControlLayout.ThumbnailCardPadding))
		.SetPressedPadding(FMargin(
			FMixtormatThemeStore::GetResolved().ControlLayout.ThumbnailCardPadding,
			FMixtormatThemeStore::GetResolved().ControlLayout.ThumbnailCardPadding + MixtormatTokens::ButtonPressedOffset,
			FMixtormatThemeStore::GetResolved().ControlLayout.ThumbnailCardPadding,
			FMixtormatThemeStore::GetResolved().ControlLayout.ThumbnailCardPadding - MixtormatTokens::ButtonPressedOffset));
	StyleInstance->Set(TEXT("Mixtormat.ThumbnailCard"), ThumbnailCard);

	FButtonStyle TopButton = FButtonStyle()
		.SetNormal(FSlateRoundedBoxBrush(FLinearColor::Transparent, FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius))
		.SetHovered(FSlateRoundedBoxBrush(RaisedPanelHover, FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius))
		.SetPressed(FSlateRoundedBoxBrush(Panel, FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius))
		.SetDisabled(FSlateRoundedBoxBrush(FLinearColor::Transparent, FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius))
		.SetNormalForeground(FSlateColor(Icon))
		.SetHoveredForeground(FSlateColor(Text))
		.SetPressedForeground(FSlateColor(Accent))
		.SetDisabledForeground(FSlateColor(DisabledText))
		.SetNormalPadding(FMargin(FMixtormatThemeStore::GetResolved().ControlLayout.ButtonPaddingCompact, 0.0f))
		.SetPressedPadding(FMargin(FMixtormatThemeStore::GetResolved().ControlLayout.ButtonPaddingCompact, 0.0f));
	StyleInstance->Set(TEXT("Mixtormat.TopButton"), TopButton);

	FButtonStyle BottomLibraryCollapseButton = FButtonStyle()
		.SetNormal(FSlateRoundedBoxBrush(FLinearColor::Transparent, FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius))
		.SetHovered(FSlateRoundedBoxBrush(RaisedPanelHover, FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius))
		.SetPressed(FSlateRoundedBoxBrush(Panel, FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius))
		.SetDisabled(FSlateRoundedBoxBrush(FLinearColor::Transparent, FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius))
		.SetNormalForeground(FSlateColor(Icon))
		.SetHoveredForeground(FSlateColor(Text))
		.SetPressedForeground(FSlateColor(Accent))
		.SetDisabledForeground(FSlateColor(DisabledText))
		.SetNormalPadding(FMargin(FMixtormatThemeStore::GetResolved().ControlLayout.ButtonPaddingCompact, 0.0f))
		.SetPressedPadding(FMargin(FMixtormatThemeStore::GetResolved().ControlLayout.ButtonPaddingCompact, 0.0f));
	StyleInstance->Set(TEXT("Mixtormat.BottomLibraryCollapseButton"), BottomLibraryCollapseButton);

	FButtonStyle PrimaryButton = FButtonStyle()
		.SetNormal(FSlateRoundedBoxBrush(RaisedPanel, FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius, BorderStrong, MixtormatTokens::PrimaryButtonOutlineWidth))
		.SetHovered(FSlateRoundedBoxBrush(RaisedPanelHover, FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius, BorderStrong, MixtormatTokens::PrimaryButtonHoverOutlineWidth))
		.SetPressed(FSlateRoundedBoxBrush(FocusFill, FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius, AccentPressed, MixtormatTokens::PrimaryButtonPressedOutlineWidth))
		.SetDisabled(FSlateRoundedBoxBrush(Panel, FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius, Border, MixtormatTokens::PrimaryButtonDisabledOutlineWidth))
		.SetNormalForeground(FSlateColor(Text))
		.SetHoveredForeground(FSlateColor(Text))
		.SetPressedForeground(FSlateColor(Text))
		.SetDisabledForeground(FSlateColor(DisabledText))
		.SetNormalPadding(FMargin(MixtormatTokens::ButtonPaddingPrimary, 0.0f))
		.SetPressedPadding(FMargin(MixtormatTokens::ButtonPaddingPrimary, 0.0f));
	StyleInstance->Set(TEXT("Mixtormat.PrimaryButton"), PrimaryButton);

	FButtonStyle TabButton = FButtonStyle()
		.SetNormal(FSlateRoundedBoxBrush(FLinearColor::Transparent, FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius))
		.SetHovered(FSlateRoundedBoxBrush(RaisedPanelHover, FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius))
		.SetPressed(FSlateRoundedBoxBrush(FocusFill, FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius))
		.SetNormalForeground(FSlateColor(MutedText))
		.SetHoveredForeground(FSlateColor(Text))
		.SetPressedForeground(FSlateColor(Text))
		.SetNormalPadding(FMargin(FMixtormatThemeStore::GetResolved().ControlLayout.ButtonPaddingTab, 0.0f))
		.SetPressedPadding(FMargin(FMixtormatThemeStore::GetResolved().ControlLayout.ButtonPaddingTab, 0.0f));
	StyleInstance->Set(TEXT("Mixtormat.TabButton"), TabButton);

	FButtonStyle TabButtonActive = FButtonStyle()
		.SetNormal(FSlateRoundedBoxBrush(SelectionFill, FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius, Accent, MixtormatTokens::ActiveTabOutlineWidth))
		.SetHovered(FSlateRoundedBoxBrush(FocusFill, FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius, AccentHover, MixtormatTokens::ActiveTabHoverOutlineWidth))
		.SetPressed(FSlateRoundedBoxBrush(FocusFill, FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius, AccentPressed, MixtormatTokens::ActiveTabPressedOutlineWidth))
		.SetNormalForeground(FSlateColor(Text))
		.SetHoveredForeground(FSlateColor(Text))
		.SetPressedForeground(FSlateColor(Text))
		.SetNormalPadding(FMargin(FMixtormatThemeStore::GetResolved().ControlLayout.ButtonPaddingTab, 0.0f))
		.SetPressedPadding(FMargin(FMixtormatThemeStore::GetResolved().ControlLayout.ButtonPaddingTab, 0.0f));
	StyleInstance->Set(TEXT("Mixtormat.TabButtonActive"), TabButtonActive);

	FCheckBoxStyle TabToggle = FCheckBoxStyle()
		.SetCheckBoxType(ESlateCheckBoxType::ToggleButton)
		.SetUncheckedImage(FSlateRoundedBoxBrush(FLinearColor::Transparent, FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius))
		.SetUncheckedHoveredImage(FSlateRoundedBoxBrush(RaisedPanelHover, FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius))
		.SetUncheckedPressedImage(FSlateRoundedBoxBrush(FocusFill, FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius))
		.SetCheckedImage(FSlateRoundedBoxBrush(SelectionFill, FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius))
		.SetCheckedHoveredImage(FSlateRoundedBoxBrush(FocusFill, FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius))
		.SetCheckedPressedImage(FSlateRoundedBoxBrush(FocusFill, FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius))
		.SetPadding(FMargin(FMixtormatThemeStore::GetResolved().ControlLayout.ButtonPaddingTab, 0.0f));
	StyleInstance->Set(TEXT("Mixtormat.TabToggle"), TabToggle);

	// --group-button-font-weight is a real CSS number, so it is snapped to the nearest shipped
	// face rather than through the legacy bold flag.
	FSlateFontInfo GroupButtonFont = Font(
		Mixtormat::FMixtormatTypography::FromCssWeight(MixtormatTokens::GroupButtonFontWeight),
		MixtormatTokens::GroupButtonFontSize,
		MixtormatTokens::GroupButtonTracking);
	FTextBlockStyle GroupButtonText = FTextBlockStyle()
		.SetFont(GroupButtonFont)
		.SetColorAndOpacity(RowText)
		.SetShadowOffset(FVector2D::ZeroVector)
		.SetShadowColorAndOpacity(FLinearColor::Transparent);
	StyleInstance->Set(TEXT("Mixtormat.GroupButtonText"), GroupButtonText);

	// Viewport rail buttons: each on its own rounded plate. Hover and press add the accent to the
	// plate; the glyph is dimmed at rest and full on hover (rail icons draw in the foreground).
	const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
	FLinearColor OverlayPlate = Resolved.Preview.OverlayPlate;
	OverlayPlate.A *= Resolved.Preview.OverlayPlateOpacity;
	const auto AccentAdded = [&OverlayPlate](const float Amount)
	{
		FLinearColor Lit = OverlayPlate + FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Accent) * Amount;
		Lit.A = OverlayPlate.A;
		return Lit;
	};
	const float PlateRadius = FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius;
	const FSlateRoundedBoxBrush PlateRest(OverlayPlate, PlateRadius);
	const FSlateRoundedBoxBrush PlateHover(AccentAdded(Resolved.Preview.HoverAccent), PlateRadius);
	const FSlateRoundedBoxBrush PlatePress(AccentAdded(Resolved.Preview.PressAccent), PlateRadius);
	const FSlateRoundedBoxBrush PlateChecked(AccentAdded(Resolved.Preview.HoverAccent * 0.6f), PlateRadius);
	FCheckBoxStyle ViewportOverlayToggle = FCheckBoxStyle()
		.SetCheckBoxType(ESlateCheckBoxType::ToggleButton)
		.SetUncheckedImage(PlateRest)
		.SetUncheckedHoveredImage(PlateHover)
		.SetUncheckedPressedImage(PlatePress)
		.SetCheckedImage(PlateChecked)
		.SetCheckedHoveredImage(PlateHover)
		.SetCheckedPressedImage(PlatePress)
		.SetUndeterminedImage(PlateRest)
		.SetUndeterminedHoveredImage(PlateHover)
		.SetUndeterminedPressedImage(PlatePress)
		.SetForegroundColor(FSlateColor(WithOpacity(Text, Resolved.Preview.IconRestOpacity)))
		.SetHoveredForegroundColor(FSlateColor(Text))
		.SetPressedForegroundColor(FSlateColor(Text))
		.SetCheckedForegroundColor(FSlateColor(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Accent)))
		.SetCheckedHoveredForegroundColor(FSlateColor(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Accent)))
		.SetCheckedPressedForegroundColor(FSlateColor(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Accent)))
		.SetPadding(FMargin(Resolved.PreviewLayout.TogglePadding));
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
		.SetNormal(PlateRest)
		.SetHovered(PlateHover)
		.SetPressed(PlatePress)
		.SetDisabled(PlateRest)
		.SetNormalForeground(FSlateColor(WithOpacity(Text, Resolved.Preview.IconRestOpacity)))
		.SetHoveredForeground(FSlateColor(Text))
		.SetPressedForeground(FSlateColor(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Accent)))
		.SetDisabledForeground(FSlateColor(DisabledText))
		.SetNormalPadding(FMargin(Resolved.PreviewLayout.TogglePadding))
		.SetPressedPadding(FMargin(Resolved.PreviewLayout.TogglePadding));
	StyleInstance->Set(TEXT("Mixtormat.ViewportOverlayButton"), ViewportOverlayButton);

	FButtonStyle CompactRowButton = FButtonStyle()
		.SetNormal(FSlateRoundedBoxBrush(RaisedPanel, 1.0f, Border, MixtormatTokens::CompactRowOutlineWidth))
		.SetHovered(FSlateRoundedBoxBrush(RaisedPanelHover, 1.0f, BorderStrong, MixtormatTokens::CompactRowHoverOutlineWidth))
		.SetPressed(FSlateRoundedBoxBrush(FocusFill, 1.0f, Accent, MixtormatTokens::CompactRowPressedOutlineWidth))
		.SetDisabled(FSlateRoundedBoxBrush(Panel, 1.0f, Border, MixtormatTokens::CompactRowDisabledOutlineWidth))
		.SetNormalForeground(FSlateColor(Text))
		.SetHoveredForeground(FSlateColor(Text))
		.SetPressedForeground(FSlateColor(Text))
		.SetDisabledForeground(FSlateColor(DisabledText))
		.SetNormalPadding(FMargin(MixtormatTokens::CompactRowButtonPaddingHorizontal, MixtormatTokens::CompactRowButtonPaddingVertical))
		.SetPressedPadding(FMargin(
			MixtormatTokens::CompactRowButtonPaddingHorizontal,
			MixtormatTokens::CompactRowButtonPaddingVertical + MixtormatTokens::ButtonPressedOffset,
			MixtormatTokens::CompactRowButtonPaddingHorizontal,
			MixtormatTokens::CompactRowButtonPaddingVertical - MixtormatTokens::ButtonPressedOffset));
	StyleInstance->Set(TEXT("Mixtormat.CompactRowButton"), CompactRowButton);

	FSpinBoxStyle ScrubControl = AppStyle.GetWidgetStyle<FSpinBoxStyle>(TEXT("NumericEntrySpinBox"));
	ScrubControl
		.SetBackgroundBrush(FSlateRoundedBoxBrush(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Panel), FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadiusInner, Border, MixtormatTokens::ScrubControlOutlineWidth))
		.SetHoveredBackgroundBrush(FSlateRoundedBoxBrush(RaisedPanelHover, 1.0f, BorderStrong, MixtormatTokens::ScrubControlHoverOutlineWidth))
		.SetActiveFillBrush(FSlateRoundedBoxBrush(FocusFill, 1.0f, Accent, MixtormatTokens::ScrubControlActiveOutlineWidth))
		.SetInactiveFillBrush(FSlateRoundedBoxBrush(FLinearColor::Transparent, 1.0f))
		.SetForegroundColor(FSlateColor(Text))
		.SetTextPadding(FMargin(MixtormatTokens::ScrubControlTextInset, 0.0f));

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
	const FLinearColor AccentBright = FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Accent);
	const FLinearColor SubduedText = FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted);
	const FLinearColor ModifiedMarker = FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Modified);

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
		new FSlateRoundedBoxBrush(TroughSurface, FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius, TroughLine, FMixtormatThemeStore::GetResolved().ControlLayout.OutlineWidth));
	StyleInstance->Set(
		TEXT("Mixtormat.ValueSlider.BackgroundHovered"),
		new FSlateRoundedBoxBrush(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Panel), FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius, FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Hairline), FMixtormatThemeStore::GetResolved().ControlLayout.OutlineWidth));
	StyleInstance->Set(
		TEXT("Mixtormat.ValueSlider.BackgroundActive"),
		new FSlateRoundedBoxBrush(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Panel), FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius, FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Accent), FMixtormatThemeStore::GetResolved().ControlLayout.OutlineWidth));
	StyleInstance->Set(
		TEXT("Mixtormat.ValueSlider.BackgroundEntry"),
		new FSlateRoundedBoxBrush(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Ground), FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius, FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Accent), FMixtormatThemeStore::GetResolved().ControlLayout.OutlineWidth));
	StyleInstance->Set(
		TEXT("Mixtormat.ValueSlider.BackgroundDisabled"),
		new FSlateRoundedBoxBrush(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Panel), FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius, FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Hairline), FMixtormatThemeStore::GetResolved().ControlLayout.OutlineWidth));

	// Centre tick on a signed range, and the modified-from-default stripe.
	// The badge: fixed-size mark carrying a row's composite mode.
	StyleInstance->Set(
		TEXT("Mixtormat.Badge"),
		new FSlateRoundedBoxBrush(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Panel), 1.0f));
	FTextBlockStyle BadgeText = FTextBlockStyle()
		.SetFont(Font(Regular, MixtormatTokens::FontBadge))
		.SetColorAndOpacity(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Text))
		.SetShadowOffset(FVector2D::ZeroVector)
		.SetShadowColorAndOpacity(FLinearColor::Transparent);
	FSlateFontInfo BadgeFont = BadgeText.Font;
	// Caption tier, already in 1/1000 em -- assigned, not converted.
	BadgeFont.LetterSpacing = MixtormatTokens::CaptionLetterSpacing;
	BadgeText.SetFont(BadgeFont);
	StyleInstance->Set(TEXT("Mixtormat.BadgeText"), BadgeText);


	// Common typography and states for Slate text-entry widgets (rename and authoring fields).
	{
		const Mixtormat::FMixtormatTextFieldTheme& T = FMixtormatThemeStore::GetTheme().TextField;
		FLinearColor BorderColor = T.Border;
		BorderColor.A *= T.BorderOpacity;
		FLinearColor Highlight = T.Highlight;
		Highlight.A *= T.HighlightOpacity;
		const FLinearColor FocusedBorder = MixtormatCompositing::ApplyBlend(T.HighlightBlend, T.Border, Highlight);
		FEditableTextBoxStyle Edit = FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>(TEXT("NormalEditableTextBox"));
		// Gradient is painted by SMixtormatTextFieldGradient behind this transparent entry.
		// Keep border and focus states in the native editable box for normal keyboard behavior.
		Edit.SetBackgroundImageNormal(FSlateRoundedBoxBrush(FLinearColor::Transparent, T.Radius, BorderColor, T.BorderThickness));
		Edit.SetBackgroundImageHovered(FSlateRoundedBoxBrush(FLinearColor::Transparent, T.Radius, BorderColor, T.BorderThickness));
		Edit.SetBackgroundImageFocused(FSlateRoundedBoxBrush(FLinearColor::Transparent, T.Radius, FocusedBorder, T.BorderThickness));
		Edit.SetBackgroundImageReadOnly(FSlateRoundedBoxBrush(FLinearColor::Transparent, T.Radius));
		Edit.SetPadding(FMargin(T.PaddingX, T.PaddingY));
		FSlateFontInfo EntryFont = Edit.TextStyle.Font;
		EntryFont.Size = FMath::RoundToInt(T.FontSize);
		Edit.TextStyle.SetFont(EntryFont);
		FLinearColor TextColor = FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Text);
		TextColor.A *= T.LabelOpacity;
		Edit.SetForegroundColor(FSlateColor(TextColor));
		Edit.SetFocusedForegroundColor(FSlateColor(TextColor));
		Edit.SetReadOnlyForegroundColor(FSlateColor(TextColor));
		FLinearColor SelectionColor = T.SelectionColor;
		SelectionColor.A *= T.SelectionOpacity;
		Edit.TextStyle.SetSelectedBackgroundColor(FSlateColor(SelectionColor));
		StyleInstance->Set(TEXT("Mixtormat.TextField"), Edit);
	}

	// A circle is a rounded box whose radius is half its size.
	StyleInstance->Set(
		TEXT("Mixtormat.StatusDot.Filled"),
		new FSlateRoundedBoxBrush(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Accent), FMixtormatThemeStore::GetResolved().ControlLayout.StatusDotSize * 0.5f));
	// A parameter's state, in the same circle at the same size -- only the hue changes, so the
	// three states read as one readout in three conditions rather than three different marks.
	// Modified is already the palette's "this is not the authored value"; Destructive is already
	// its warning. Neither needs a colour of its own here.
	StyleInstance->Set(
		TEXT("Mixtormat.StatusDot.Reference"),
		new FSlateRoundedBoxBrush(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Modified), FMixtormatThemeStore::GetResolved().ControlLayout.StatusDotSize * 0.5f));
	StyleInstance->Set(
		TEXT("Mixtormat.StatusDot.Previewing"),
		new FSlateRoundedBoxBrush(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Accent), FMixtormatThemeStore::GetResolved().ControlLayout.StatusDotSize * 0.5f));
	StyleInstance->Set(
		TEXT("Mixtormat.StatusDot.Broken"),
		new FSlateRoundedBoxBrush(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Error), FMixtormatThemeStore::GetResolved().ControlLayout.StatusDotSize * 0.5f));
	StyleInstance->Set(
		TEXT("Mixtormat.StatusDot.Hollow"),
		new FSlateRoundedBoxBrush(
			FLinearColor::Transparent,
			FMixtormatThemeStore::GetResolved().ControlLayout.StatusDotSize * 0.5f,
			FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted),
			FMixtormatThemeStore::GetResolved().ControlLayout.OutlineWidth));
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
	StyleInstance->Set(TEXT("Mixtormat.InspectorWell"), new FSlateColorBrush(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Ground)));
	// A foldout body is Ground: same base as its header and as every card, so the header's lift
	// actually dissolves into it. It was GroupSurround, which matched only because the old header
	// faded to that colour too -- a coincidence between two independently-authored values.
	StyleInstance->Set(TEXT("Mixtormat.GroupBody"), new FSlateColorBrush(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Ground)));

	// A card: one titled run of rows, raised off the body.
	StyleInstance->Set(
		TEXT("Mixtormat.Card"),
		new FSlateRoundedBoxBrush(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Panel), FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius));

	// Our own popovers, which are widgets rather than multibox rows: a label, the shortcut printed
	// quietly beside it, and the section caption above a run of them.
	{
		FTextBlockStyle MenuLabel = FTextBlockStyle()
			.SetFont(Font(Regular, MixtormatTokens::FontBody))
			.SetColorAndOpacity(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Text))
			.SetShadowOffset(FVector2D::ZeroVector)
			.SetShadowColorAndOpacity(FLinearColor::Transparent);
		StyleInstance->Set(TEXT("Mixtormat.MenuLabel"), MenuLabel);

				FLinearColor HelpBodyColor = FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Text);
				HelpBodyColor.A *= MixtormatTokens::HelpBodyOpacity;
				FTextBlockStyle HelpBody = FTextBlockStyle(MenuLabel)
					.SetColorAndOpacity(HelpBodyColor);
				StyleInstance->Set(TEXT("Mixtormat.HelpBody"), HelpBody);
				FTextBlockStyle HelpTitle = FTextBlockStyle(MenuLabel)
					.SetFont(Font(Bold, MixtormatTokens::FontBody));
				StyleInstance->Set(TEXT("Mixtormat.HelpTitle"), HelpTitle);

		FTextBlockStyle MenuShortcut = FTextBlockStyle(MenuLabel)
			.SetFont(Font(Regular, MixtormatTokens::FontMenuShortcut))
			.SetColorAndOpacity(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted));
		StyleInstance->Set(TEXT("Mixtormat.MenuShortcut"), MenuShortcut);

		FTextBlockStyle MenuCaption = FTextBlockStyle(MenuLabel)
			.SetFont(Font(Regular, MixtormatTokens::FontCaption))
			.SetColorAndOpacity(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted));
		FSlateFontInfo MenuCaptionFont = MenuCaption.Font;
		// Caption tier, already in 1/1000 em -- assigned, not converted.
		MenuCaptionFont.LetterSpacing = MixtormatTokens::MenuCaptionLetterSpacing;
		MenuCaption.SetFont(MenuCaptionFont);
		StyleInstance->Set(TEXT("Mixtormat.MenuCaption"), MenuCaption);

		StyleInstance->Set(
			TEXT("Mixtormat.MenuSeparator"),
			new FSlateColorBrush(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Hairline)));
	}

	// The lip along the top edge of a header. A flat colour brush, because it is drawn into a
	// one-pixel box laid over the header's top edge rather than used as a border around it --
	// Slate has no top-only border, and a brush used as a BorderImage fills the whole area.
	StyleInstance->Set(
		TEXT("Mixtormat.HeaderHairline"),
		new FSlateColorBrush(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Hairline)));
	StyleInstance->Set(
		TEXT("Mixtormat.HeaderHairlineGlow"),
		new FSlateColorBrush(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Hairline)));

	// Layer stack: the enclosing edge, and the two type styles its rows use.
	StyleInstance->Set(TEXT("Mixtormat.LayerEdge"), new FSlateColorBrush(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Hairline)));

	FTextBlockStyle LayerName = FTextBlockStyle()
		.SetFont(Font(Regular, MixtormatTokens::FontLayerName))
		.SetColorAndOpacity(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Text))
		.SetShadowOffset(FVector2D::ZeroVector)
		.SetShadowColorAndOpacity(FLinearColor::Transparent)
		.SetOverflowPolicy(ETextOverflowPolicy::Ellipsis);
	StyleInstance->Set(TEXT("Mixtormat.LayerName"), LayerName);

	FTextBlockStyle LayerSource = FTextBlockStyle()
		.SetFont(Font(Regular, MixtormatTokens::FontLayerSource))
		.SetColorAndOpacity(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted))
		.SetShadowOffset(FVector2D::ZeroVector)
		.SetShadowColorAndOpacity(FLinearColor::Transparent)
		.SetOverflowPolicy(ETextOverflowPolicy::Ellipsis);
	FSlateFontInfo LayerSourceFont = LayerSource.Font;
	// Caption tier, already in 1/1000 em -- assigned, not converted.
	LayerSourceFont.LetterSpacing = MixtormatTokens::LayerSourceLetterSpacing;
	LayerSource.SetFont(LayerSourceFont);
	StyleInstance->Set(TEXT("Mixtormat.LayerSource"), LayerSource);

	StyleInstance->Set(TEXT("Mixtormat.SegmentSeam"), new FSlateColorBrush(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Hairline)));

	StyleInstance->Set(TEXT("Mixtormat.WellOutline"), new FSlateRoundedBoxBrush(
		FLinearColor::Transparent, FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius,
		FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Hairline), FMixtormatThemeStore::GetResolved().ControlLayout.OutlineWidth));
	StyleInstance->Set(TEXT("Mixtormat.ValueSlider.Tick"), new FSlateColorBrush(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Hairline)));
	StyleInstance->Set(TEXT("Mixtormat.ValueSlider.Modified"), new FSlateColorBrush(ModifiedMarker));

	// ---- Control type ----------------------------------------------------------------------
	// The label and the value are authored separately, which they were not before: both were
	// driven from FontSliderLabel and differed only in weight. The design treats a row's name as
	// quiet context beside its number, so the label needs its own size, weight and opacity and the
	// value needs its own -- tuning one must not drag the other.
	//
	// Tracking is authored in CSS px and converted against the label's own size inside Font().
			// At the authored 0px the conversion is the identity, which is why the bug was invisible:
			// the number was copied straight into Slate's 1/1000 em field and happened to be zero. Any
			// non-zero value would have been 1000x too small.
			FSlateFontInfo LabelFont = Font(
				Weight(MixtormatTokens::ControlLabelBold),
				MixtormatTokens::FontControlLabel,
				MixtormatTokens::ControlLabelLetterSpacing);

	// Opacity is applied to the shared row-text role's alpha rather than to a separate grey role, so
			// label and value stay the same hue and differ only in weight -- which is what the design
			// asks for.
			const FLinearColor LabelColor = AtOpacity(RowText, MixtormatTokens::ControlLabelOpacity);
		const FLinearColor ValueColor = AtOpacity(RowText, MixtormatTokens::ControlValueOpacity);

			FTextBlockStyle SliderLabel = FTextBlockStyle()
		.SetFont(LabelFont)
		.SetColorAndOpacity(LabelColor)
		.SetShadowOffset(FVector2D::ZeroVector)
		.SetShadowColorAndOpacity(FLinearColor::Transparent);
	StyleInstance->Set(TEXT("Mixtormat.ValueSlider.Label"), SliderLabel);

	// Values keep their heavier face and a uniform advance, which makes a column of numbers line
	// up on the decimal point without putting the same visual weight on the label.
	//
	// SemiBold, not Bold, and not "whatever a bold flag implies": tokens.css authors
	// --value-weight: 600. The previous mapping snapped everything from 500 up onto the engine's
	// Bold face, so control values rendered at 700 against a design that asked for 600. That is
	// the single most-visible thing this stage corrects.
	FTextBlockStyle SliderValue = FTextBlockStyle()
		.SetFont(Font(SemiBold, MixtormatTokens::FontControlValue))
		.SetColorAndOpacity(ValueColor)
		.SetShadowOffset(FVector2D::ZeroVector)
		.SetShadowColorAndOpacity(FLinearColor::Transparent);
	FSlateFontInfo ValueFont = SliderValue.Font;
	ValueFont.bForceMonospaced = true;
	ValueFont.MonospacedWidth = 0.52f;
	SliderValue.SetFont(ValueFont);
	StyleInstance->Set(TEXT("Mixtormat.ValueSlider.Value"), SliderValue);

	// Disabled dims the row's text by an authored factor rather than swapping to a separate grey
	// role, so it stays the same hue as the enabled text rather than becoming a different colour.
	const FLinearColor DisabledColor = AtOpacity(RowText, MixtormatTokens::TextDisabledOpacity);
	FTextBlockStyle SliderLabelDisabled = SliderLabel;
	SliderLabelDisabled.SetColorAndOpacity(DisabledColor);
	StyleInstance->Set(TEXT("Mixtormat.ValueSlider.LabelDisabled"), SliderLabelDisabled);

	FEditableTextBoxStyle SliderEntry =
		AppStyle.GetWidgetStyle<FEditableTextBoxStyle>(TEXT("NormalEditableTextBox"));
	SliderEntry
		.SetBackgroundImageNormal(FSlateRoundedBoxBrush(RaisedPanel, FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius, AccentBright, FMixtormatThemeStore::GetResolved().ControlLayout.OutlineWidth))
		.SetBackgroundImageHovered(FSlateRoundedBoxBrush(RaisedPanel, FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius, AccentBright, FMixtormatThemeStore::GetResolved().ControlLayout.OutlineWidth))
		.SetBackgroundImageFocused(FSlateRoundedBoxBrush(RaisedPanel, FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius, AccentBright, FMixtormatThemeStore::GetResolved().ControlLayout.OutlineWidth))
		.SetForegroundColor(FSlateColor(Text))
		.SetPadding(FMargin(FMixtormatThemeStore::GetResolved().ControlLayout.DraggerTextInset - 1.0f, 0.0f));
	// The entry replaces the value in place, so it matches the face it is typing over.
	SliderEntry.TextStyle.SetFont(SliderValue.Font);
	StyleInstance->Set(TEXT("Mixtormat.ValueSlider.Entry"), SliderEntry);

	// A search field is a well that happens to take text. Left on the editor's own editable-text
	// style it kept that style's pill radius and hairline, so it read as a foreign control sitting
	// in a row of Mixtormat wells rather than as one of them. Same ground, same well outline, same
	// radius token, same label face as the rest of the inspector.
	{
		const FSlateNoResource SearchBackground;
		FEditableTextBoxStyle SearchStyle =
			AppStyle.GetWidgetStyle<FEditableTextBoxStyle>(TEXT("NormalEditableTextBox"));
		SearchStyle
			.SetBackgroundImageNormal(SearchBackground)
			.SetBackgroundImageHovered(SearchBackground)
			.SetBackgroundImageFocused(SearchBackground)
			.SetBackgroundImageReadOnly(SearchBackground)
			.SetForegroundColor(FSlateColor(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Ground)))
			.SetPadding(FMargin(FMixtormatThemeStore::GetResolved().ControlLayout.DraggerTextInset * 0.5f, 0.0f))
			.SetTextStyle(SliderLabel);
		StyleInstance->Set(TEXT("Mixtormat.SearchBox"), SearchStyle);
	}

	// One shared thin scrollbar for Mixtormat-owned surfaces.
	{
		const Mixtormat::FMixtormatResolvedStyle& R = FMixtormatThemeStore::GetResolved();
		const FSlateNoResource Empty;
		const FLinearColor C = R.Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted);
		const FSlateColorBrush Normal(C.CopyWithNewOpacity(R.ShellLayout.ScrollbarThumbOpacity));
		const FSlateColorBrush Hover(C.CopyWithNewOpacity(R.ShellLayout.ScrollbarHoverOpacity));
		FScrollBarStyle B;
		B.SetHorizontalBackgroundImage(Empty).SetVerticalBackgroundImage(Empty)
		 .SetHorizontalTopSlotImage(Empty).SetHorizontalBottomSlotImage(Empty)
		 .SetVerticalTopSlotImage(Empty).SetVerticalBottomSlotImage(Empty)
		 .SetNormalThumbImage(Normal).SetHoveredThumbImage(Hover).SetDraggedThumbImage(Hover)
		 .SetThickness(R.ShellLayout.ScrollbarThickness);
		StyleInstance->Set(TEXT("Mixtormat.ScrollBar"), B);
	}

	// ---- Row furniture ----------------------------------------------------------------------
	// Sub-group caption, and the hairline that separates two runs of rows without naming them.
	FTextBlockStyle RowCaption = FTextBlockStyle()
		.SetFont(Font(Regular, MixtormatTokens::FontCaption))
		.SetColorAndOpacity(CaptionText)
		.SetShadowOffset(FVector2D::ZeroVector)
		.SetShadowColorAndOpacity(FLinearColor::Transparent);
	FSlateFontInfo CaptionFont = RowCaption.Font;
	// Caption tier, already in 1/1000 em -- assigned, not converted.
	CaptionFont.LetterSpacing = MixtormatTokens::CaptionLetterSpacing;
	RowCaption.SetFont(CaptionFont);
	StyleInstance->Set(TEXT("Mixtormat.RowCaption"), RowCaption);

	// A card's title line. Same tracking as the caption it used to share a style with, but its own
	// weight, size and colour -- the three things that decide whether a title reads as one.
	FTextBlockStyle CardTitle = FTextBlockStyle()
		.SetFont(Font(Weight(MixtormatTokens::CardTitleBold), MixtormatTokens::FontCardTitle))
		.SetColorAndOpacity(CardTitleText)
		.SetShadowOffset(FVector2D::ZeroVector)
		.SetShadowColorAndOpacity(FLinearColor::Transparent);
	FSlateFontInfo CardTitleFont = CardTitle.Font;
	// Caption tier, already in 1/1000 em -- assigned, not converted.
	CardTitleFont.LetterSpacing = MixtormatTokens::CaptionLetterSpacing;
	CardTitle.SetFont(CardTitleFont);
	StyleInstance->Set(TEXT("Mixtormat.CardTitle"), CardTitle);

	FTextBlockStyle GroupCardTitle = FTextBlockStyle()
		.SetFont(Font(Weight(MixtormatTokens::GroupCardTitleBold), MixtormatTokens::FontGroupCardTitle))
		.SetColorAndOpacity(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Text))
		.SetShadowOffset(FVector2D::ZeroVector)
		.SetShadowColorAndOpacity(FLinearColor::Transparent);
	FSlateFontInfo GroupCardTitleFont = GroupCardTitle.Font;
	// Persisted in Slate's 1/1000 em (see the token's own comment), so assigned, not converted.
	GroupCardTitleFont.LetterSpacing = FMath::RoundToInt(MixtormatTokens::GroupCardTitleLetterSpacing);
	GroupCardTitle.SetFont(GroupCardTitleFont);
	StyleInstance->Set(TEXT("Mixtormat.GroupCardTitle"), GroupCardTitle);

	FTextBlockStyle RowLabel = SliderLabel;
	RowLabel.SetOverflowPolicy(ETextOverflowPolicy::Ellipsis);
	StyleInstance->Set(TEXT("Mixtormat.RowLabel"), RowLabel);

	StyleInstance->Set(TEXT("Mixtormat.Hairline"), new FSlateColorBrush(Border));

	// ---- Thumbnail tiles --------------------------------------------------------------------
	// One tile serves the library, the mask replacement grid and the mask picker. The name strip
	// is an overlay on the image, so showing it costs picture rather than layout height.
	StyleInstance->Set(
		TEXT("Mixtormat.Tile.Normal"),
		new FSlateRoundedBoxBrush(ThumbnailBackground, FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius, Border, FMixtormatThemeStore::GetResolved().ControlLayout.OutlineWidth));
	StyleInstance->Set(
		TEXT("Mixtormat.Tile.Hovered"),
		new FSlateRoundedBoxBrush(ThumbnailBackground, FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius, WithOpacity(Text, 0.42f), FMixtormatThemeStore::GetResolved().ControlLayout.OutlineWidth));
	StyleInstance->Set(
		TEXT("Mixtormat.Tile.Selected"),
		new FSlateRoundedBoxBrush(ThumbnailBackground, FMixtormatThemeStore::GetResolved().ControlLayout.CornerRadius, AccentBright, FMixtormatThemeStore::GetResolved().ControlLayout.OutlineWidth));
	StyleInstance->Set(
		TEXT("Mixtormat.Tile.NameStrip"),
		new FSlateColorBrush(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Shade)));

	FTextBlockStyle TileName = FTextBlockStyle()
		.SetFont(Font(Regular, MixtormatTokens::FontTile))
		.SetColorAndOpacity(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Text))
		.SetShadowOffset(FVector2D::ZeroVector)
		.SetShadowColorAndOpacity(FLinearColor::Transparent)
		.SetOverflowPolicy(ETextOverflowPolicy::Ellipsis);
	StyleInstance->Set(TEXT("Mixtormat.Tile.Name"), TileName);

	// ---- Drag and drop ----------------------------------------------------------------------
	// A line in the gutter means "between"; a tinted row means "into". They have to look
	// different, or dropping an effect beside a layer versus into it is a coin flip.

	const auto SetBrandArtwork = [&Icon](
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

	// SVG glyphs remain vector-backed and use the same theme tint and authored dimensions.
	const auto SetSvgIcon = [&Icon](const FName Key, const TCHAR* FileName, const FVector2D Size)
	{
		StyleInstance->Set(
			Key,
			new FSlateVectorImageBrush(
				StyleInstance->RootToContentDir(FileName, TEXT(".svg")),
				Size,
				FSlateColor(Icon)));
	};
	const auto SetPngIcon = [&Icon](const FName Key, const TCHAR* FileName, const FVector2D Size)
	{
		StyleInstance->Set(
			Key,
			new FSlateImageBrush(
				StyleInstance->RootToContentDir(FileName, TEXT(".png")),
				Size,
				FSlateColor(Icon)));
	};

	SetSvgIcon(TEXT("Mixtormat.Icon.Save"), TEXT("Icons/save"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.SaveAs"), TEXT("Icons/save-as"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.Overflow"), TEXT("Icons/overflow"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.Add"), TEXT("Icons/add"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.Settings"), TEXT("Icons/settings"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.Eye"), TEXT("Icons/eye"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.EyeOff"), TEXT("Icons/eye-off"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.Duplicate"), TEXT("Icons/duplicate"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.Folder"), TEXT("Icons/folder"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.Refresh"), TEXT("Icons/refresh"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.Trash"), TEXT("Icons/trash"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.Grip"), TEXT("Icons/grip"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.ArrowUp"), TEXT("Icons/arrow-up"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.ArrowDown"), TEXT("Icons/arrow-down"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.Cube"), TEXT("Icons/cube"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.Sphere"), TEXT("Icons/sphere"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.Plane"), TEXT("Icons/plane"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.Cylinder"), TEXT("Icons/cylinder"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.Globe"), TEXT("Icons/globe"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSizeLarge, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSizeLarge));
	SetSvgIcon(TEXT("Mixtormat.Icon.Nodes"), TEXT("Icons/nodes"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSizeLarge, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSizeLarge));
	SetSvgIcon(TEXT("Mixtormat.Icon.Camera"), TEXT("Icons/camera"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSizeLarge, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSizeLarge));
	SetSvgIcon(TEXT("Mixtormat.Icon.Search"), TEXT("Icons/search"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.Documentation"), TEXT("Icons/documentation"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSizeLarge, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSizeLarge));
	SetSvgIcon(TEXT("Mixtormat.Icon.Feedback"), TEXT("Icons/feedback"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSizeLarge, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSizeLarge));
	SetSvgIcon(TEXT("Mixtormat.Icon.LightNeutral"), TEXT("Icons/light-neutral"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.LightSoft"), TEXT("Icons/light-soft"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.LightDramatic"), TEXT("Icons/light-dramatic"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.LightRim"), TEXT("Icons/light-rim"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.QualityLow"), TEXT("Icons/quality-low"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.QualityMedium"), TEXT("Icons/quality-medium"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.QualityHigh"), TEXT("Icons/quality-high"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.ScalarRampConstant"), TEXT("Icons/ramp-constant"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampIconSize, FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampIconSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.ScalarRampLinear"), TEXT("Icons/ramp-linear"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampIconSize, FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampIconSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.ScalarRampSpline"), TEXT("Icons/ramp-spline"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampIconSize, FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampIconSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.ScalarRampBSpline"), TEXT("Icons/ramp-bspline"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampIconSize, FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampIconSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.ScalarRampFrame"), TEXT("Icons/ramp-frame"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampIconSize, FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampIconSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.ScalarRampReset"), TEXT("Icons/ramp-reset"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampIconSize, FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampIconSize));

	// What a layer's children are. Each glyph says what kind of thing the child is, since the row
	// beside it is already carrying the name and the blend mode -- a mask outline for something
	// that shapes coverage, a bolt for an effect, a mountain for a procedural generator,
	// a shoot for generated masks, and a cluster for IDs and data producers.
	SetSvgIcon(TEXT("Mixtormat.Icon.Mask"), TEXT("Icons/mask"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.Effect"), TEXT("Icons/effect"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.Generator"), TEXT("Icons/generator"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.Generated"), TEXT("Icons/generated"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.FlowDirection"), TEXT("Icons/flow-direction"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.FlowGravity"), TEXT("Icons/flow-gravity"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.WarpDeform"), TEXT("Icons/warp-deform"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.WarpNoise"), TEXT("Icons/warp-noise"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.WarpPush"), TEXT("Icons/warp-push"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.WarpStructural"), TEXT("Icons/warp-structural"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	// IDs and ID-derived data are their own category, not Generated Mask. They were borrowing that
	// glyph only because there was no icon for them yet.
	SetSvgIcon(TEXT("Mixtormat.Icon.Ids"), TEXT("Icons/ids"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	// Layer kinds. A square for a material, a circle for a fill -- the shapes the add bar uses.
	SetSvgIcon(TEXT("Mixtormat.Icon.LayerMaterial"), TEXT("Icons/layer-material"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.LayerFill"), TEXT("Icons/layer-fill"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));

	// Disclosure. These were being borrowed from FAppStyle, which meant the one glyph in the stack
	// that is not ours changed weight whenever the editor theme did.
	SetSvgIcon(TEXT("Mixtormat.Icon.ChevronDown"), TEXT("Icons/chevron-down"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.ChevronRight"), TEXT("Icons/chevron-right"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));
	SetSvgIcon(TEXT("Mixtormat.Icon.Check"), TEXT("Icons/check"), FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize));

	// Disclosure and hierarchy-root glyphs. Tree rails and indentation are geometry-painted.
	{
		const FVector2D Size(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize);
		SetSvgIcon(TEXT("Mixtormat.Icon.ChevronUp"), TEXT("Icons/chevron-up"), Size);
		SetSvgIcon(TEXT("Mixtormat.Icon.ChevronDownBold"), TEXT("Icons/chevron-down-bold"), Size);
		SetSvgIcon(TEXT("Mixtormat.Icon.HierarchyRoot"), TEXT("Icons/hierarchy-root"), Size);

	}

	// Brand marks. The source art is 53.46 x 58.07 for the icon and 297.14 x 58.07 for the
	// logo, so every size below holds those ratios rather than squashing the glyph.
	const FVector2D IconSize(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize, FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize);
	SetSvgIcon(TEXT("Mixtormat.Icon.Layers"), TEXT("Icons/layers"), IconSize);
	SetSvgIcon(TEXT("Mixtormat.Icon.Library"), TEXT("Icons/library"), IconSize);
	SetSvgIcon(TEXT("Mixtormat.Icon.Global"), TEXT("Icons/global"), IconSize);
	SetSvgIcon(TEXT("Mixtormat.Icon.Close"), TEXT("Icons/close"), IconSize);
	SetSvgIcon(TEXT("Mixtormat.Icon.Minimize"), TEXT("Icons/minimize"), IconSize);
	SetSvgIcon(TEXT("Mixtormat.Icon.ChevronLeft"), TEXT("Icons/chevron-left"), IconSize);
	SetSvgIcon(TEXT("Mixtormat.Icon.Pin"), TEXT("Icons/pin"), IconSize);
	SetSvgIcon(TEXT("Mixtormat.Icon.Dock"), TEXT("Icons/dock"), IconSize);
	SetSvgIcon(TEXT("Mixtormat.Icon.VariableLink"), TEXT("Icons/variable-link"), IconSize);
	SetSvgIcon(TEXT("Mixtormat.Icon.VariableUnlink"), TEXT("Icons/variable-unlink"), IconSize);
	SetSvgIcon(TEXT("Mixtormat.Icon.Squircle"), TEXT("Icons/squircle"), IconSize);
	SetBrandArtwork(TEXT("Mixtormat.Brand.Icon"), TEXT("Icons/mixtormat-icon"), FVector2D(MixtormatTokens::BrandIconWidth, MixtormatTokens::BrandIconHeight));
	SetBrandArtwork(TEXT("Mixtormat.Brand.Logo"), TEXT("Icons/mixtormat-logo"), FVector2D(MixtormatTokens::BrandLogoWidth, MixtormatTokens::BrandLogoHeight));

	// Viewport watermark. Tinted dark and mostly transparent so it sits under the material
	// rather than competing with it, and small enough to stay out of the way.
	StyleInstance->Set(
		TEXT("Mixtormat.Brand.Watermark"),
		new FSlateVectorImageBrush(
			StyleInstance->RootToContentDir(TEXT("Icons/mixtormat-icon"), TEXT(".svg")),
			FVector2D(MixtormatTokens::BrandWatermarkWidth, MixtormatTokens::BrandWatermarkHeight),
			FSlateColor(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted))));

	if (bFirstRegistration)
	{
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
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
