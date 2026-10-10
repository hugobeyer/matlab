// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Layers/SMixtormatSourceRow.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatStyle.h"
#include "Style/MixtormatThemeStore.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "UI/Layers/SMixtormatLayerSurface.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void SMixtormatSourceRow::Construct(const FArguments& InArgs)
{
	Enabled = InArgs._bEnabled;
	OnSelected = InArgs._OnSelected;
	bHasContextMenu = InArgs._OnGetContextMenu.IsBound();
	const ISlateStyle& Style = FMixtormatStyle::Get();
	const FTextBlockStyle NameStyle = Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.LayerName"));
	const FTextBlockStyle KindStyle = Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.LayerSource"));
	const float GlyphSize = FMixtormatThemeStore::GetResolved().Icons.Roles[
		static_cast<uint8>(Mixtormat::EMixtormatIconRole::LayerVisToggle)].GlyphSize;

	ChildSlot
	[
		SNew(SMenuAnchor)
		.Placement(MenuPlacement_MenuRight)
		.OnGetMenuContent(InArgs._OnGetContextMenu.IsBound()
			? InArgs._OnGetContextMenu
			: FOnGetContent())
		[
			SNew(SMixtormatLayerSurface)
			.Kind(Mixtormat::EMixtormatLayerKind::Layer)
			.bSelected(InArgs._bSelected)
			.bHovered_Lambda([this]() { return bHovered; })
			[
				SNew(SBox)
				.HeightOverride(FMixtormatThemeStore::GetResolved().LayerLayout.RowHeight)
				.Padding(FMargin(
					MixtormatTokens::LayerRowInsetLeading, 0.0f,
					MixtormatTokens::LayerRowInsetTrailing, 0.0f))
				.VAlign(VAlign_Center)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, MixtormatTokens::LayerNameInset, 0.0f)
					[
						SNew(SBox)
						.WidthOverride(GlyphSize)
						.HeightOverride(GlyphSize)
						[
							SNew(SImage)
							.Image(MixtormatIcons::Generator())
							// A disabled source keeps its row so it can be re-enabled from here;
							// only the weight of the row changes.
							.ColorAndOpacity_Lambda([this]()
							{
								const FLinearColor Color = FMixtormatThemeStore::GetResolved()
									.Palette.Get(Mixtormat::EMixtormatColorRole::Text);
								return FSlateColor(Enabled.Get(true)
									? Color
									: FLinearColor(Color.R, Color.G, Color.B, Color.A * 0.4f));
							})
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, MixtormatTokens::LayerNameInset, 0.0f)
					[
						SNew(STextBlock)
						.Text(InArgs._Name)
						.TextStyle(&NameStyle)
						.ColorAndOpacity_Lambda([this]()
						{
							return FSlateColor(FMixtormatThemeStore::GetResolved().Palette.Get(
								Enabled.Get(true)
									? Mixtormat::EMixtormatColorRole::Text
									: Mixtormat::EMixtormatColorRole::TextMuted));
						})
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(InArgs._Kind)
						.TextStyle(&KindStyle)
					]
				]
			]
		]
	];
}

FReply SMixtormatSourceRow::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		OnSelected.ExecuteIfBound();
		return FReply::Handled();
	}
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		return OpenContextMenu();
	}
	return FReply::Unhandled();
}

void SMixtormatSourceRow::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);
	bHovered = true;
}

void SMixtormatSourceRow::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseLeave(MouseEvent);
	bHovered = false;
}

FReply SMixtormatSourceRow::OpenContextMenu()
{
	if (!ContextAnchor.IsValid() || !bHasContextMenu)
	{
		return FReply::Unhandled();
	}
	ContextAnchor->SetIsOpen(true);
	return FReply::Handled();
}
