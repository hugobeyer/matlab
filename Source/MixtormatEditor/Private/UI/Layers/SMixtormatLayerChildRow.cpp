#include "UI/Layers/SMixtormatLayerChildRow.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatPalette.h"
#include "Style/MixtormatStyle.h"
#include "UI/Atoms/SMixtormatBadge.h"
#include "UI/Atoms/SMixtormatStatusDot.h"
#include "UI/Primitives/SMixtormatGradientBox.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "Mixtormat"

void SMixtormatLayerChildRow::Construct(const FArguments& InArgs)
{
	bSelected = InArgs._bSelected;
	OnSelected = InArgs._OnSelected;
	OnRowDragDetected = InArgs._OnDragDetected;

	const ISlateStyle& Style = FMixtormatStyle::Get();

	ChildSlot
	[
		SAssignNew(ContextAnchor, SMenuAnchor)
		.Placement(MenuPlacement_MenuRight)
		.UseApplicationMenuStack(true)
		.OnGetMenuContent(InArgs._OnGetContextMenu)
		[
			// Horizontal, not vertical: children stay dark at the left and lift toward the right,
			// so they remain subordinate to the owning layer's top-to-bottom gradient.
			SNew(SMixtormatGradientBox)
			.StartColor(this, &SMixtormatLayerChildRow::GetTintEnd)
			.EndColor(this, &SMixtormatLayerChildRow::GetTintStart)
			.Orientation(Orient_Horizontal)
			.CornerRadius(MixtormatTokens::CornerRadius)
			[
				SNew(SBox)
				.HeightOverride(MixtormatTokens::LayerChildRowHeight)
				.Padding(FMargin(
					MixtormatTokens::LayerChildIndent,
					0.0f,
					MixtormatTokens::LayerRowInsetTrailing,
					0.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, MixtormatTokens::LayerItemGap, 0.0f)
					[
						SNew(SBox)
						.WidthOverride(MixtormatTokens::LayerChildIconSize)
						.HeightOverride(MixtormatTokens::LayerChildIconSize)
						[
							InArgs._Icon.Widget
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.LayerName")))
						.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
						.Text(InArgs._Name)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(
						MixtormatTokens::LayerItemGap,
						0.0f,
						MixtormatTokens::LayerItemGap,
						0.0f)
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
					.Padding(MixtormatTokens::LayerItemGap, 0.0f, 0.0f, 0.0f)
					[
						SNew(SMixtormatStatusDot)
						.Size(MixtormatTokens::StatusDotSize)
						.bFilled(InArgs._bActive)
						.ToolTip(LOCTEXT("ChildDotHint", "Enable or disable this child."))
						.OnClicked(InArgs._OnToggleActive)
					]
				]
			]
		]
	];
}

FLinearColor SMixtormatLayerChildRow::GetTintStart() const
{
	if (bSelected.Get(false))
	{
		return MixtormatPalette::LayerChildSelectedRight();
	}
	return IsHovered() ? MixtormatPalette::LayerChildHoverRight() : FLinearColor::Transparent;
}

FLinearColor SMixtormatLayerChildRow::GetTintEnd() const
{
	if (bSelected.Get(false))
	{
		return MixtormatPalette::LayerChildSelectedLeft();
	}
	return IsHovered() ? MixtormatPalette::LayerChildHoverLeft() : FLinearColor::Transparent;
}

FReply SMixtormatLayerChildRow::OnMouseButtonDown(const FGeometry&, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		// Select first, so the menu is built against the child it opened on.
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
	OnSelected.ExecuteIfBound();
	return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
}

FReply SMixtormatLayerChildRow::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return OnRowDragDetected.IsBound()
		? OnRowDragDetected.Execute(MyGeometry, MouseEvent)
		: FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
