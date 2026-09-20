// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Layers/SMixtormatLayerGroupRow.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatPalette.h"
#include "Style/MixtormatStyle.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "UI/Atoms/SMixtormatIconButton.h"
#include "UI/Primitives/SMixtormatGradientBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "Mixtormat"

namespace
{
	// Tints Base toward Accent without changing how bright it is.
	//
	// A straight lerp toward a saturated colour would wash the row out and flatten the
	// enabled/hover/selected ramp those palette colours exist to carry. So the accent is first
	// rescaled to Base's own luminance -- which leaves only its hue and saturation to contribute --
	// and the lerp happens against that. The row stays as dark as it was and simply leans.
	FLinearColor TintTowards(const FLinearColor& Base, const FLinearColor& Accent, const float Strength)
	{
		if (Accent.A <= 0.0f || Strength <= 0.0f)
		{
			return Base;
		}
		const float AccentLuminance = Accent.GetLuminance();
		if (AccentLuminance <= KINDA_SMALL_NUMBER)
		{
			return Base;
		}
		const float Scale = Base.GetLuminance() / AccentLuminance;
		const FLinearColor Matched(
			Accent.R * Scale,
			Accent.G * Scale,
			Accent.B * Scale,
			Base.A);
		return FMath::Lerp(Base, Matched, FMath::Clamp(Strength, 0.0f, 1.0f));
	}
}

void SMixtormatLayerGroupRow::Construct(const FArguments& InArgs)
{
	bGroupEnabled = InArgs._bEnabled;
	bExpanded = InArgs._bExpanded;
	bSelected = InArgs._bSelected;
	AccentColor = InArgs._AccentColor;
	OnToggleExpanded = InArgs._OnToggleExpanded;
	OnSelected = InArgs._OnSelected;
	OnToggleEnabled = InArgs._OnToggleEnabled;
	Name = InArgs._Name;
	OnNameCommitted = InArgs._OnNameCommitted;
	OnRowDragDetected = InArgs._OnDragDetected;

	const ISlateStyle& Style = FMixtormatStyle::Get();
	const TAttribute<int32> MemberCount = InArgs._MemberCount;

	ChildSlot
	[
		SAssignNew(ContextAnchor, SMenuAnchor)
		.Placement(MenuPlacement_MenuRight)
		.UseApplicationMenuStack(true)
		.OnGetMenuContent(InArgs._OnGetContextMenu)
		[
			SNew(SMixtormatGradientBox)
			.StartColor(this, &SMixtormatLayerGroupRow::GetBackgroundStart)
			.EndColor(this, &SMixtormatLayerGroupRow::GetBackgroundEnd)
			// The cross pass, left to right over the top-down one. Two axes is the group row's
			// signature: a layer ramps top-down only and a child row left-right only, so a header
			// doing both is distinguishable from either without a glyph to say so.
			.MultiplyStart(this, &SMixtormatLayerGroupRow::GetCrossStart)
			.MultiplyEnd(this, &SMixtormatLayerGroupRow::GetCrossEnd)
			.Orientation(Orient_Vertical)
			.CornerRadius(MixtormatTokens::CornerRadius)
			[
				SNew(SBox)
				.HeightOverride(MixtormatTokens::LayerGroupRowHeight)
				.Padding(FMargin(
					MixtormatTokens::LayerRowInsetLeading,
					0.0f,
					MixtormatTokens::LayerRowInsetTrailing,
					0.0f))
				[
					SNew(SHorizontalBox)

					// The group's eye hides every member for the render without touching any
					// member's own eye, so switching the group back on restores what the user set.
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, MixtormatTokens::LayerItemGap, 0.0f)
					[
						SNew(SMixtormatIconButton)
						.Size(MixtormatTokens::LayerEyeSize)
						.Icon_Lambda([this]()
						{
							return bGroupEnabled.Get(true)
								? MixtormatIcons::Eye() : MixtormatIcons::EyeOff();
						})
						.ToolTipText(LOCTEXT(
							"GroupEyeHint",
							"Show or hide every layer in this group. Each layer keeps its own visibility."))
						.OnClicked(OnToggleEnabled)
					]

					// No folder glyph where a layer carries its thumbnail: the row is already
					// unmistakably a group from its height, its inset and the layers nested under
					// it, so the icon was repeating what the shape already said.

					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					.Padding(MixtormatTokens::LayerNameInset, 0.0f, 0.0f, 0.0f)
					[
						SAssignNew(NameSwitcher, SWidgetSwitcher)
						+ SWidgetSwitcher::Slot()
						[
							SNew(STextBlock)
							.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.LayerName")))
							.ColorAndOpacity(this, &SMixtormatLayerGroupRow::GetNameColor)
							.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
							.Text(InArgs._Name)
						]
						+ SWidgetSwitcher::Slot()
						[
							SAssignNew(NameEditBox, SEditableTextBox)
							.SelectAllTextWhenFocused(true)
							.ClearKeyboardFocusOnCommit(true)
							.OnTextCommitted(this, &SMixtormatLayerGroupRow::HandleNameCommitted)
						]
					]

					// Subdued and unlabelled: it sits where a layer's source text sits, and a
					// bare number there reads as a count without needing the word.
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(MixtormatTokens::LayerItemGap, 0.0f, MixtormatTokens::LayerItemGap, 0.0f)
					[
						SNew(STextBlock)
						.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.LayerSource")))
						.Text_Lambda([MemberCount]()
						{
							return FText::AsNumber(MemberCount.Get(0));
						})
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SMixtormatIconButton)
						.Size(MixtormatTokens::ChevronSize)
						.Icon_Lambda([this]()
						{
							return bExpanded.Get(true)
								? MixtormatIcons::ChevronDown()
								: MixtormatIcons::ChevronRight();
						})
						.OnClicked(OnToggleExpanded)
					]
				]
			]
		]
	];
}

// A hidden group is not tinted: "switched off" has to stay legible at a glance, and a colour is
// the one thing that would argue with it. Selection tints harder than rest, so the accent
// reinforces the highlight rather than competing with it.
float SMixtormatLayerGroupRow::GetAccentStrength() const
{
	if (!bGroupEnabled.Get(true))
	{
		return 0.0f;
	}
	return bSelected.Get(false)
		? MixtormatTokens::GroupAccentSelectedStrength
		: MixtormatTokens::GroupAccentStrength;
}

// Grey by default, and tinted with the accent when there is one -- a neutral band sitting across
// a coloured row would read as a fault rather than as a second axis.
FLinearColor SMixtormatLayerGroupRow::GetCrossStart() const
{
	if (!bGroupEnabled.Get(true))
	{
		return FLinearColor::Transparent;
	}
	const FLinearColor Base = TintTowards(
		MixtormatPalette::GroupRowCross(),
		AccentColor.Get(FLinearColor::Transparent),
		GetAccentStrength());
	return FLinearColor(Base.R, Base.G, Base.B, MixtormatTokens::GroupRowCrossStrength);
}

// Transparent, so the pass fades out entirely and the vertical gradient is what the right-hand
// side of the row shows.
FLinearColor SMixtormatLayerGroupRow::GetCrossEnd() const
{
	return FLinearColor::Transparent;
}

FLinearColor SMixtormatLayerGroupRow::GetBackgroundStart() const
{
	if (!bGroupEnabled.Get(true))
	{
		return MixtormatPalette::LayerHiddenTop();
	}
	const FLinearColor Base = bSelected.Get(false)
		? MixtormatPalette::LayerSelectedTop()
		: (IsHovered() ? MixtormatPalette::LayerHoverTop() : MixtormatPalette::Panel());
	return TintTowards(Base, AccentColor.Get(FLinearColor::Transparent), GetAccentStrength());
}

FLinearColor SMixtormatLayerGroupRow::GetBackgroundEnd() const
{
	if (!bGroupEnabled.Get(true))
	{
		return MixtormatPalette::LayerHiddenEnd();
	}
	const FLinearColor Base = bSelected.Get(false)
		? MixtormatPalette::LayerSelectedBottom()
		: (IsHovered() ? MixtormatPalette::LayerHoverBottom() : MixtormatPalette::PanelBottom());
	return TintTowards(Base, AccentColor.Get(FLinearColor::Transparent), GetAccentStrength());
}

FSlateColor SMixtormatLayerGroupRow::GetNameColor() const
{
	if (!bGroupEnabled.Get(true))
	{
		return FSlateColor(MixtormatPalette::DisabledText());
	}
	return FSlateColor(bSelected.Get(false) || IsHovered()
		? MixtormatPalette::RowText()
		: MixtormatPalette::LayerName());
}

void SMixtormatLayerGroupRow::BeginRename()
{
	if (!NameSwitcher.IsValid() || !NameEditBox.IsValid())
	{
		return;
	}
	NameEditBox->SetText(Name.Get(FText::GetEmpty()));
	NameSwitcher->SetActiveWidgetIndex(1);
	FSlateApplication::Get().SetKeyboardFocus(NameEditBox, EFocusCause::SetDirectly);
}

void SMixtormatLayerGroupRow::HandleNameCommitted(
	const FText& Text,
	const ETextCommit::Type CommitType)
{
	if (NameSwitcher.IsValid())
	{
		NameSwitcher->SetActiveWidgetIndex(0);
	}
	if (CommitType == ETextCommit::OnCleared)
	{
		return;
	}
	OnNameCommitted.ExecuteIfBound(Text, CommitType);
}

FReply SMixtormatLayerGroupRow::OnMouseButtonDown(const FGeometry&, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		OnSelected.ExecuteIfBound();
		if (ContextAnchor.IsValid())
		{
			ContextAnchor->SetIsOpen(true);
		}
		return FReply::Handled();
	}
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	// The body of the row is the drag handle, exactly as it is on a layer.
	OnSelected.ExecuteIfBound();
	return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
}

FReply SMixtormatLayerGroupRow::OnDragDetected(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent)
{
	return OnRowDragDetected.IsBound()
		? OnRowDragDetected.Execute(MyGeometry, MouseEvent)
		: FReply::Unhandled();
}

FReply SMixtormatLayerGroupRow::OnMouseButtonDoubleClick(
	const FGeometry&,
	const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	// The same action the chevron performs, so the two ways in cannot disagree.
	OnSelected.ExecuteIfBound();
	OnToggleExpanded.ExecuteIfBound();
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
