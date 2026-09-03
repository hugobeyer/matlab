#include "UI/Layers/SMixtormatLayerChildRow.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatPalette.h"
#include "Style/MixtormatStyle.h"
#include "UI/Atoms/SMixtormatBadge.h"
#include "UI/Atoms/SMixtormatStatusDot.h"
#include "UI/Primitives/SMixtormatGradientBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void SMixtormatLayerChildRow::Construct(const FArguments& InArgs)
{
	bSelected = InArgs._bSelected;
	OnSelected = InArgs._OnSelected;

	const ISlateStyle& Style = FMixtormatStyle::Get();

	ChildSlot
	[
		// Horizontal, not vertical: a selected child lights from its right edge, so it cannot be
		// mistaken for a small layer row lighting from the top.
		SNew(SMixtormatGradientBox)
		.StartColor(this, &SMixtormatLayerChildRow::GetTintEnd)
		.EndColor(this, &SMixtormatLayerChildRow::GetTintStart)
		.Orientation(Orient_Horizontal)
		.CornerRadius(MixtormatTokens::CornerRadius)
		[
			SNew(SBox)
			.HeightOverride(MixtormatTokens::LayerChildRowHeight)
			.Padding(FMargin(MixtormatTokens::LayerChildIndent, 0.0f, 5.0f, 0.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(SBox)
					.WidthOverride(10.0f)
					.HeightOverride(10.0f)
					[
						SNew(SImage)
						.Image(InArgs._Icon)
						.ColorAndOpacity(FSlateColor(MixtormatPalette::CaptionText()))
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.LayerName")))
					.Text(InArgs._Name)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(6.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(STextBlock)
					.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.LayerSource")))
					.Text(InArgs._Kind)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SMixtormatBadge).Text(InArgs._Badge)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SMixtormatStatusDot).bFilled(InArgs._bActive)
				]
			]
		]
	];
}

FLinearColor SMixtormatLayerChildRow::GetTintStart() const
{
	return bSelected.Get(false) ? MixtormatPalette::LayerOpenTop() : FLinearColor::Transparent;
}

FLinearColor SMixtormatLayerChildRow::GetTintEnd() const
{
	return bSelected.Get(false) ? MixtormatPalette::LayerOpenEnd() : FLinearColor::Transparent;
}

FReply SMixtormatLayerChildRow::OnMouseButtonDown(const FGeometry&, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	OnSelected.ExecuteIfBound();
	return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
}
