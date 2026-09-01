#include "Style/MaterialLabStyle.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"

#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"

namespace MaterialLabStylePrivate
{
	const FName StyleSetName(TEXT("MaterialLabStyle"));

	FLinearColor WithOpacity(FLinearColor Color, const float Opacity)
	{
		Color.A = Opacity;
		return Color;
	}

	FLinearColor Shade(FLinearColor Color, const float Amount)
	{
		Color.R *= Amount;
		Color.G *= Amount;
		Color.B *= Amount;
		return Color;
	}
}

TSharedPtr<FSlateStyleSet> FMaterialLabStyle::StyleInstance;

void FMaterialLabStyle::Initialize()
{
	if (StyleInstance.IsValid())
	{
		return;
	}

	using namespace MaterialLabStylePrivate;

	const ISlateStyle& AppStyle = FAppStyle::Get();
	const auto AppColor = [&AppStyle](const FName ColorName)
	{
		return AppStyle.GetSlateColor(ColorName).GetSpecifiedColor();
	};

	const FLinearColor Window = Shade(AppColor(TEXT("Colors.Background")), 0.72f);
	const FLinearColor TopBar = Shade(AppColor(TEXT("Colors.Header")), 0.82f);
	const FLinearColor Panel = Shade(AppColor(TEXT("Colors.Panel")), 0.86f);
	const FLinearColor RaisedPanel = Shade(AppColor(TEXT("Colors.Foldout")), 0.92f);
	const FLinearColor RaisedPanelHover = AppColor(TEXT("Colors.Hover"));
	const FLinearColor Viewport = Shade(AppColor(TEXT("Colors.Recessed")), 0.72f);
	const FLinearColor ThumbnailBackground(0.018f, 0.022f, 0.028f, 1.0f);
	const FLinearColor Border = WithOpacity(AppColor(TEXT("Colors.InputOutline")), 0.42f);
	const FLinearColor BorderStrong = WithOpacity(AppColor(TEXT("Colors.Secondary")), 0.58f);
	const FLinearColor Shadow = FLinearColor(0.0f, 0.0f, 0.0f, 0.52f);
	const FLinearColor Inset = Shade(Panel, 0.72f);
	const FLinearColor Accent = WithOpacity(AppColor(TEXT("Colors.Select")), 0.72f);
	const FLinearColor AccentHover = WithOpacity(AppColor(TEXT("Colors.SelectHover")), 0.82f);
	const FLinearColor AccentPressed = WithOpacity(AppColor(TEXT("Colors.Select")), 0.92f);
	const FLinearColor SelectionFill = WithOpacity(AppColor(TEXT("Colors.Select")), 0.16f);
	const FLinearColor FocusFill = WithOpacity(AppColor(TEXT("Colors.Select")), 0.10f);
	const FLinearColor Text = AppColor(TEXT("Colors.Foreground"));
	const FLinearColor MutedText = AppColor(TEXT("Colors.Secondary"));
	const FLinearColor DisabledText = WithOpacity(AppColor(TEXT("Colors.Foreground")), 0.35f);
	const FLinearColor Icon = AppColor(TEXT("Colors.Foreground"));
	const FLinearColor Danger = AppColor(TEXT("Colors.AccentRed"));

	StyleInstance = MakeShared<FSlateStyleSet>(GetStyleSetName());
	StyleInstance->SetContentRoot(
		FPaths::ProjectPluginsDir() / TEXT("MaterialLab/Resources"));

	StyleInstance->Set(
		TEXT("MaterialLab.Window"),
		new FSlateColorBrush(Window));
	StyleInstance->Set(
		TEXT("MaterialLab.TopBar"),
		new FSlateColorBrush(TopBar));
	StyleInstance->Set(
		TEXT("MaterialLab.PanelShadow"),
		new FSlateRoundedBoxBrush(Shadow, 7.0f));
	StyleInstance->Set(
		TEXT("MaterialLab.Panel"),
		new FSlateRoundedBoxBrush(Panel, 5.0f, Border, 0.55f));
	StyleInstance->Set(
		TEXT("MaterialLab.RaisedPanel"),
		new FSlateRoundedBoxBrush(RaisedPanel, 4.0f, Border, 0.45f));
	StyleInstance->Set(
		TEXT("MaterialLab.InsetPanel"),
		new FSlateRoundedBoxBrush(Inset, 4.0f, Shadow, 0.45f));
	StyleInstance->Set(
		TEXT("MaterialLab.SectionBar"),
		new FSlateRoundedBoxBrush(RaisedPanel, 1.0f, Border, 0.3f));
	StyleInstance->Set(
		TEXT("MaterialLab.InspectorGroup"),
		new FSlateRoundedBoxBrush(Inset, 5.0f, BorderStrong, 0.55f));
	StyleInstance->Set(
		TEXT("MaterialLab.InspectorGroupHeader"),
		new FSlateRoundedBoxBrush(RaisedPanel, 4.0f, Border, 0.35f));
	StyleInstance->Set(
		TEXT("MaterialLab.DragGhost"),
		new FSlateRoundedBoxBrush(RaisedPanel, 6.0f, BorderStrong, 0.8f));
	StyleInstance->Set(
		TEXT("MaterialLab.DragGhostAccent"),
		new FSlateRoundedBoxBrush(SelectionFill, 6.0f, AccentHover, 1.0f));
	StyleInstance->Set(
		TEXT("MaterialLab.ViewportBorder"),
		new FSlateRoundedBoxBrush(Viewport, 6.0f, BorderStrong, 0.5f));
	StyleInstance->Set(
		TEXT("MaterialLab.ViewportOverlayGroup"),
		new FSlateRoundedBoxBrush(WithOpacity(RaisedPanel, 0.82f), 4.0f, BorderStrong, 0.45f));
	StyleInstance->Set(
		TEXT("MaterialLab.LayerCard"),
		new FSlateRoundedBoxBrush(RaisedPanel, 4.0f, Border, 0.4f));
	StyleInstance->Set(
		TEXT("MaterialLab.LayerCardSelected"),
		new FSlateRoundedBoxBrush(SelectionFill, 4.0f, Accent, 0.8f));
	StyleInstance->Set(
		TEXT("MaterialLab.Accent"),
		new FSlateColorBrush(Accent));
	StyleInstance->Set(
		TEXT("MaterialLab.TabUnderline"),
		new FSlateColorBrush(FLinearColor::Transparent));
	StyleInstance->Set(
		TEXT("MaterialLab.TabUnderlineSelected"),
		new FSlateColorBrush(Accent));
	StyleInstance->Set(
		TEXT("MaterialLab.CompactRow"),
		new FSlateRoundedBoxBrush(RaisedPanel, 1.0f, Border, 0.25f));
	StyleInstance->Set(
		TEXT("MaterialLab.CompactRowHovered"),
		new FSlateRoundedBoxBrush(RaisedPanelHover, 1.0f, BorderStrong, 0.35f));
	StyleInstance->Set(
		TEXT("MaterialLab.CompactRowSelected"),
		new FSlateRoundedBoxBrush(SelectionFill, 1.0f, Accent, 0.55f));
	StyleInstance->Set(
		TEXT("MaterialLab.CompactRowValidDrop"),
		new FSlateRoundedBoxBrush(FocusFill, 1.0f, AccentHover, 0.65f));

	FTextBlockStyle SectionHeader = FTextBlockStyle()
		.SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 8))
		.SetColorAndOpacity(Text)
		.SetShadowOffset(FVector2D::ZeroVector)
		.SetShadowColorAndOpacity(FLinearColor::Transparent);
	StyleInstance->Set(TEXT("MaterialLab.SectionHeader"), SectionHeader);

	FTextBlockStyle Muted = FTextBlockStyle()
		.SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9))
		.SetColorAndOpacity(MutedText)
		.SetShadowOffset(FVector2D::ZeroVector)
		.SetShadowColorAndOpacity(FLinearColor::Transparent);
	StyleInstance->Set(TEXT("MaterialLab.MutedText"), Muted);

	FButtonStyle AssetCard = FButtonStyle()
		.SetNormal(FSlateRoundedBoxBrush(RaisedPanel, 2.0f, Border, 0.35f))
		.SetHovered(FSlateRoundedBoxBrush(RaisedPanelHover, 2.0f, BorderStrong, 0.5f))
		.SetPressed(FSlateRoundedBoxBrush(Panel, 2.0f, Accent, 0.65f))
		.SetDisabled(FSlateRoundedBoxBrush(Panel, 2.0f, Border, 0.35f))
		.SetNormalForeground(FSlateColor(Text))
		.SetHoveredForeground(FSlateColor(Text))
		.SetPressedForeground(FSlateColor(Text))
		.SetDisabledForeground(FSlateColor(DisabledText))
		.SetNormalPadding(FMargin(2.0f))
		.SetPressedPadding(FMargin(2.0f, 3.0f, 2.0f, 1.0f));
	StyleInstance->Set(TEXT("MaterialLab.AssetCard"), AssetCard);

	FButtonStyle InspectorHeaderButton = FButtonStyle()
		.SetNormal(FSlateRoundedBoxBrush(FLinearColor::Transparent, 3.0f))
		.SetHovered(FSlateRoundedBoxBrush(RaisedPanelHover, 3.0f))
		.SetPressed(FSlateRoundedBoxBrush(FocusFill, 3.0f))
		.SetDisabled(FSlateRoundedBoxBrush(FLinearColor::Transparent, 3.0f))
		.SetNormalForeground(FSlateColor(Text))
		.SetHoveredForeground(FSlateColor(Text))
		.SetPressedForeground(FSlateColor(Text))
		.SetDisabledForeground(FSlateColor(DisabledText))
		.SetNormalPadding(FMargin(2.0f, 1.0f))
		.SetPressedPadding(FMargin(2.0f, 2.0f, 2.0f, 0.0f));
	StyleInstance->Set(TEXT("MaterialLab.InspectorHeaderButton"), InspectorHeaderButton);

	StyleInstance->Set(
		TEXT("MaterialLab.ThumbnailBackground"),
		new FSlateRoundedBoxBrush(ThumbnailBackground, 2.0f));

	FButtonStyle ThumbnailCard = FButtonStyle()
		.SetNormal(FSlateRoundedBoxBrush(ThumbnailBackground, 2.0f, Border, 0.35f))
		.SetHovered(FSlateRoundedBoxBrush(ThumbnailBackground, 2.0f, BorderStrong, 0.5f))
		.SetPressed(FSlateRoundedBoxBrush(ThumbnailBackground, 2.0f, BorderStrong, 0.5f))
		.SetDisabled(FSlateRoundedBoxBrush(ThumbnailBackground, 2.0f, Border, 0.35f))
		.SetNormalForeground(FSlateColor(Text))
		.SetHoveredForeground(FSlateColor(Text))
		.SetPressedForeground(FSlateColor(Text))
		.SetDisabledForeground(FSlateColor(DisabledText))
		.SetNormalPadding(FMargin(2.0f))
		.SetPressedPadding(FMargin(2.0f, 3.0f, 2.0f, 1.0f));
	StyleInstance->Set(TEXT("MaterialLab.ThumbnailCard"), ThumbnailCard);

	FButtonStyle TopButton = FButtonStyle()
		.SetNormal(FSlateRoundedBoxBrush(FLinearColor::Transparent, 2.0f))
		.SetHovered(FSlateRoundedBoxBrush(RaisedPanelHover, 2.0f))
		.SetPressed(FSlateRoundedBoxBrush(Panel, 2.0f))
		.SetDisabled(FSlateRoundedBoxBrush(FLinearColor::Transparent, 2.0f))
		.SetNormalForeground(FSlateColor(Icon))
		.SetHoveredForeground(FSlateColor(Text))
		.SetPressedForeground(FSlateColor(Accent))
		.SetDisabledForeground(FSlateColor(DisabledText))
		.SetNormalPadding(FMargin(4.0f, 2.0f))
		.SetPressedPadding(FMargin(4.0f, 3.0f, 4.0f, 1.0f));
	StyleInstance->Set(TEXT("MaterialLab.TopButton"), TopButton);

	FButtonStyle PrimaryButton = FButtonStyle()
		.SetNormal(FSlateRoundedBoxBrush(RaisedPanel, 2.0f, BorderStrong, 0.35f))
		.SetHovered(FSlateRoundedBoxBrush(RaisedPanelHover, 2.0f, BorderStrong, 0.5f))
		.SetPressed(FSlateRoundedBoxBrush(FocusFill, 2.0f, AccentPressed, 0.65f))
		.SetDisabled(FSlateRoundedBoxBrush(Panel, 2.0f, Border, 0.25f))
		.SetNormalForeground(FSlateColor(Text))
		.SetHoveredForeground(FSlateColor(Text))
		.SetPressedForeground(FSlateColor(Text))
		.SetDisabledForeground(FSlateColor(DisabledText))
		.SetNormalPadding(FMargin(6.0f, 2.0f))
		.SetPressedPadding(FMargin(6.0f, 3.0f, 6.0f, 1.0f));
	StyleInstance->Set(TEXT("MaterialLab.PrimaryButton"), PrimaryButton);

	FButtonStyle TabButton = FButtonStyle()
		.SetNormal(FSlateRoundedBoxBrush(FLinearColor::Transparent, 1.0f))
		.SetHovered(FSlateRoundedBoxBrush(RaisedPanelHover, 1.0f))
		.SetPressed(FSlateRoundedBoxBrush(FocusFill, 1.0f))
		.SetNormalForeground(FSlateColor(MutedText))
		.SetHoveredForeground(FSlateColor(Text))
		.SetPressedForeground(FSlateColor(Text))
		.SetNormalPadding(FMargin(6.0f, 1.0f))
		.SetPressedPadding(FMargin(6.0f, 2.0f, 6.0f, 0.0f));
	StyleInstance->Set(TEXT("MaterialLab.TabButton"), TabButton);
	StyleInstance->Set(TEXT("MaterialLab.CompactTab"), TabButton);

	FButtonStyle TabButtonActive = FButtonStyle()
		.SetNormal(FSlateRoundedBoxBrush(SelectionFill, 1.0f, Accent, 0.45f))
		.SetHovered(FSlateRoundedBoxBrush(FocusFill, 1.0f, AccentHover, 0.55f))
		.SetPressed(FSlateRoundedBoxBrush(FocusFill, 1.0f, AccentPressed, 0.65f))
		.SetNormalForeground(FSlateColor(Text))
		.SetHoveredForeground(FSlateColor(Text))
		.SetPressedForeground(FSlateColor(Text))
		.SetNormalPadding(FMargin(6.0f, 1.0f))
		.SetPressedPadding(FMargin(6.0f, 2.0f, 6.0f, 0.0f));
	StyleInstance->Set(TEXT("MaterialLab.TabButtonActive"), TabButtonActive);
	StyleInstance->Set(TEXT("MaterialLab.CompactTabActive"), TabButtonActive);

	FCheckBoxStyle LayerButton = FCheckBoxStyle()
		.SetCheckBoxType(ESlateCheckBoxType::ToggleButton)
		.SetUncheckedImage(FSlateRoundedBoxBrush(RaisedPanel, 1.0f, Border, 0.35f))
		.SetUncheckedHoveredImage(FSlateRoundedBoxBrush(RaisedPanelHover, 1.0f, BorderStrong, 0.5f))
		.SetUncheckedPressedImage(FSlateRoundedBoxBrush(Panel, 1.0f, BorderStrong, 0.5f))
		.SetCheckedImage(FSlateRoundedBoxBrush(RaisedPanelHover, 1.0f, Accent, 0.65f))
		.SetCheckedHoveredImage(FSlateRoundedBoxBrush(RaisedPanelHover, 1.0f, AccentHover, 0.65f))
		.SetCheckedPressedImage(FSlateRoundedBoxBrush(Panel, 1.0f, AccentPressed, 0.65f))
		.SetUndeterminedImage(FSlateRoundedBoxBrush(RaisedPanel, 1.0f, BorderStrong, 0.5f))
		.SetUndeterminedHoveredImage(FSlateRoundedBoxBrush(RaisedPanelHover, 1.0f, Accent, 0.65f))
		.SetUndeterminedPressedImage(FSlateRoundedBoxBrush(Panel, 1.0f, AccentPressed, 0.65f))
		.SetPadding(FMargin(3.0f));
	StyleInstance->Set(TEXT("MaterialLab.LayerButton"), LayerButton);

	FCheckBoxStyle TabToggle = FCheckBoxStyle()
		.SetCheckBoxType(ESlateCheckBoxType::ToggleButton)
		.SetUncheckedImage(FSlateRoundedBoxBrush(FLinearColor::Transparent, 3.0f))
		.SetUncheckedHoveredImage(FSlateRoundedBoxBrush(RaisedPanelHover, 3.0f))
		.SetUncheckedPressedImage(FSlateRoundedBoxBrush(FocusFill, 3.0f))
		.SetCheckedImage(FSlateRoundedBoxBrush(SelectionFill, 3.0f))
		.SetCheckedHoveredImage(FSlateRoundedBoxBrush(FocusFill, 3.0f))
		.SetCheckedPressedImage(FSlateRoundedBoxBrush(FocusFill, 3.0f))
		.SetPadding(FMargin(8.0f, 5.0f));
	StyleInstance->Set(TEXT("MaterialLab.TabToggle"), TabToggle);

	FCheckBoxStyle ViewportOverlayToggle = FCheckBoxStyle()
		.SetCheckBoxType(ESlateCheckBoxType::ToggleButton)
		.SetUncheckedImage(FSlateRoundedBoxBrush(FLinearColor::Transparent, 2.0f))
		.SetUncheckedHoveredImage(FSlateRoundedBoxBrush(RaisedPanelHover, 2.0f))
		.SetUncheckedPressedImage(FSlateRoundedBoxBrush(FocusFill, 2.0f))
		.SetCheckedImage(FSlateRoundedBoxBrush(SelectionFill, 2.0f, Accent, 0.55f))
		.SetCheckedHoveredImage(FSlateRoundedBoxBrush(FocusFill, 2.0f, AccentHover, 0.65f))
		.SetCheckedPressedImage(FSlateRoundedBoxBrush(FocusFill, 2.0f, AccentPressed, 0.65f))
		.SetPadding(FMargin(2.0f));
	StyleInstance->Set(TEXT("MaterialLab.ViewportOverlayToggle"), ViewportOverlayToggle);

	FSliderStyle Slider = FSliderStyle()
		.SetNormalBarImage(FSlateRoundedBoxBrush(Border, 1.5f))
		.SetHoveredBarImage(FSlateRoundedBoxBrush(BorderStrong, 1.5f))
		.SetNormalThumbImage(FSlateRoundedBoxBrush(BorderStrong, 4.0f))
		.SetHoveredThumbImage(FSlateRoundedBoxBrush(AccentHover, 5.0f))
		.SetBarThickness(2.0f);
	StyleInstance->Set(TEXT("MaterialLab.Slider"), Slider);
	StyleInstance->Set(TEXT("MaterialLab.ScrubSlider"), Slider);

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
	StyleInstance->Set(TEXT("MaterialLab.CompactRowButton"), CompactRowButton);

	FButtonStyle DragHandle = FButtonStyle()
		.SetNormal(FSlateRoundedBoxBrush(FLinearColor::Transparent, 1.0f))
		.SetHovered(FSlateRoundedBoxBrush(RaisedPanelHover, 1.0f))
		.SetPressed(FSlateRoundedBoxBrush(FocusFill, 1.0f))
		.SetDisabled(FSlateRoundedBoxBrush(FLinearColor::Transparent, 1.0f))
		.SetNormalForeground(FSlateColor(MutedText))
		.SetHoveredForeground(FSlateColor(Text))
		.SetPressedForeground(FSlateColor(Accent))
		.SetDisabledForeground(FSlateColor(DisabledText))
		.SetNormalPadding(FMargin(2.0f, 0.0f))
		.SetPressedPadding(FMargin(2.0f, 1.0f, 2.0f, 0.0f));
	StyleInstance->Set(TEXT("MaterialLab.DragHandle"), DragHandle);

	FSpinBoxStyle ScrubControl = AppStyle.GetWidgetStyle<FSpinBoxStyle>(TEXT("NumericEntrySpinBox"));
	ScrubControl
		.SetBackgroundBrush(FSlateRoundedBoxBrush(AppColor(TEXT("Colors.Input")), 1.0f, Border, 0.25f))
		.SetHoveredBackgroundBrush(FSlateRoundedBoxBrush(RaisedPanelHover, 1.0f, BorderStrong, 0.35f))
		.SetActiveFillBrush(FSlateRoundedBoxBrush(FocusFill, 1.0f, Accent, 0.4f))
		.SetInactiveFillBrush(FSlateRoundedBoxBrush(FLinearColor::Transparent, 1.0f))
		.SetForegroundColor(FSlateColor(Text))
		.SetTextPadding(FMargin(3.0f, 0.0f));
	StyleInstance->Set(TEXT("MaterialLab.ScrubControl"), ScrubControl);

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

	SetIcon(TEXT("MaterialLab.Icon.Save"), TEXT("Icons/save"), FVector2D(16.0f, 16.0f));
	SetIcon(TEXT("MaterialLab.Icon.SaveAs"), TEXT("Icons/save-all"), FVector2D(16.0f, 16.0f));
	SetIcon(TEXT("MaterialLab.Icon.Overflow"), TEXT("Icons/ellipsis"), FVector2D(16.0f, 16.0f));
	SetIcon(TEXT("MaterialLab.Icon.Add"), TEXT("Icons/plus"), FVector2D(16.0f, 16.0f));
	SetIcon(TEXT("MaterialLab.Icon.Eye"), TEXT("Icons/eye"), FVector2D(16.0f, 16.0f));
	SetIcon(TEXT("MaterialLab.Icon.EyeOff"), TEXT("Icons/eye-off"), FVector2D(16.0f, 16.0f));
	SetIcon(TEXT("MaterialLab.Icon.Duplicate"), TEXT("Icons/copy"), FVector2D(16.0f, 16.0f));
	SetIcon(TEXT("MaterialLab.Icon.Folder"), TEXT("Icons/folder-open"), FVector2D(16.0f, 16.0f));
	SetIcon(TEXT("MaterialLab.Icon.Refresh"), TEXT("Icons/refresh-cw"), FVector2D(16.0f, 16.0f));
	SetIcon(TEXT("MaterialLab.Icon.Trash"), TEXT("Icons/trash-2"), FVector2D(16.0f, 16.0f));
	SetIcon(TEXT("MaterialLab.Icon.Grip"), TEXT("Icons/grip-vertical"), FVector2D(16.0f, 16.0f));
	SetIcon(TEXT("MaterialLab.Icon.ArrowUp"), TEXT("Icons/arrow-up"), FVector2D(16.0f, 16.0f));
	SetIcon(TEXT("MaterialLab.Icon.ArrowDown"), TEXT("Icons/arrow-down"), FVector2D(16.0f, 16.0f));
	SetIcon(TEXT("MaterialLab.Icon.Cube"), TEXT("Icons/box"), FVector2D(16.0f, 16.0f));
	SetIcon(TEXT("MaterialLab.Icon.Sphere"), TEXT("Icons/circle"), FVector2D(16.0f, 16.0f));
	SetIcon(TEXT("MaterialLab.Icon.Plane"), TEXT("Icons/rectangle-horizontal"), FVector2D(16.0f, 16.0f));
	SetIcon(TEXT("MaterialLab.Icon.Globe"), TEXT("Icons/globe"), FVector2D(20.0f, 20.0f));
	SetIcon(TEXT("MaterialLab.Icon.Nodes"), TEXT("Icons/workflow"), FVector2D(20.0f, 20.0f));
	SetIcon(TEXT("MaterialLab.Icon.Camera"), TEXT("Icons/camera"), FVector2D(20.0f, 20.0f));
	SetIcon(TEXT("MaterialLab.Icon.Search"), TEXT("Icons/search"), FVector2D(16.0f, 16.0f));
	SetIcon(TEXT("MaterialLab.Icon.Documentation"), TEXT("Icons/book-open-text"), FVector2D(20.0f, 20.0f));
	SetIcon(TEXT("MaterialLab.Icon.Feedback"), TEXT("Icons/message-square"), FVector2D(20.0f, 20.0f));
	SetIcon(TEXT("MaterialLab.Icon.LightNeutral"), TEXT("Icons/sun"), FVector2D(16.0f, 16.0f));
	SetIcon(TEXT("MaterialLab.Icon.LightSoft"), TEXT("Icons/cloud-sun"), FVector2D(16.0f, 16.0f));
	SetIcon(TEXT("MaterialLab.Icon.LightDramatic"), TEXT("Icons/contrast"), FVector2D(16.0f, 16.0f));
	SetIcon(TEXT("MaterialLab.Icon.LightRim"), TEXT("Icons/sunrise"), FVector2D(16.0f, 16.0f));
	SetIcon(TEXT("MaterialLab.Icon.QualityLow"), TEXT("Icons/signal-low"), FVector2D(16.0f, 16.0f));
	SetIcon(TEXT("MaterialLab.Icon.QualityMedium"), TEXT("Icons/signal-medium"), FVector2D(16.0f, 16.0f));
	SetIcon(TEXT("MaterialLab.Icon.QualityHigh"), TEXT("Icons/signal-high"), FVector2D(16.0f, 16.0f));
	StyleInstance->Set(
		TEXT("MaterialLab.Icon.Trash.Danger"),
		new FSlateVectorImageBrush(
			StyleInstance->RootToContentDir(TEXT("Icons/trash-2"), TEXT(".svg")),
			FVector2D(16.0f, 16.0f),
			FSlateColor(Danger)));

	FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
}

void FMaterialLabStyle::Shutdown()
{
	if (!StyleInstance.IsValid())
	{
		return;
	}

	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

const ISlateStyle& FMaterialLabStyle::Get()
{
	check(StyleInstance.IsValid());
	return *StyleInstance;
}

FName FMaterialLabStyle::GetStyleSetName()
{
	return MaterialLabStylePrivate::StyleSetName;
}
