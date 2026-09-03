#include "UI/Layers/SMixtormatLayerRow.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatPalette.h"
#include "Style/MixtormatStyle.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "UI/Atoms/SMixtormatBadge.h"
#include "UI/Atoms/SMixtormatIconButton.h"
#include "UI/Primitives/SMixtormatGradientBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void SMixtormatLayerRow::Construct(const FArguments& InArgs)
{
	bLayerEnabled = InArgs._bEnabled;
	bExpanded = InArgs._bExpanded;
	bSelected = InArgs._bSelected;
	OnToggleExpanded = InArgs._OnToggleExpanded;
	OnSelected = InArgs._OnSelected;

	const ISlateStyle& Style = FMixtormatStyle::Get();
	const FSimpleDelegate ToggleEnabled = InArgs._OnToggleEnabled;

	ChildSlot
	[
		SNew(SMixtormatGradientBox)
		.StartColor(this, &SMixtormatLayerRow::GetBackgroundStart)
		.EndColor(this, &SMixtormatLayerRow::GetBackgroundEnd)
		.Orientation(Orient_Vertical)
		.CornerRadius(MixtormatTokens::CornerRadius)
		[
			SNew(SBox)
			.HeightOverride(MixtormatTokens::LayerRowHeight)
			.Padding(FMargin(8.0f, 0.0f, 5.0f, 0.0f))
			[
				SNew(SHorizontalBox)

				// Eye. Its own toggle, so clicking it never also selects the layer.
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(SMixtormatIconButton)
					.Size(12.0f)
					.Icon(MixtormatIcons::Eye())
					.OnClicked(ToggleEnabled)
				]

				// Thumbnail: no border, so the image reads as the surface itself.
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(SBox)
					.WidthOverride(MixtormatTokens::LayerThumbnailSize)
					.HeightOverride(MixtormatTokens::LayerThumbnailSize)
					[
						SNew(SImage).Image(InArgs._Thumbnail)
					]
				]

				// Name grows; source is right-aligned beside it so the two form columns.
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.LayerName")))
					.ColorAndOpacity(this, &SMixtormatLayerRow::GetNameColor)
					.Text(InArgs._Name)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(6.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(STextBlock)
					.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.LayerSource")))
					.Text(InArgs._Source)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SMixtormatBadge).Text(InArgs._Badge)
				]

				// Disclosure last, so the badge column stays flush against it.
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SBox)
					.WidthOverride(9.0f)
					.HeightOverride(9.0f)
					[
						SNew(SImage)
						.Image_Lambda([this]()
						{
							return bExpanded.Get(false)
								? MixtormatIcons::ChevronDown()
								: MixtormatIcons::ChevronRight();
						})
						.ColorAndOpacity(FSlateColor(MixtormatPalette::HeaderText()))
					]
				]
			]
		]
	];
}

FLinearColor SMixtormatLayerRow::GetBackgroundStart() const
{
	// A hidden layer sweeps dark left-to-right; an open one is tinted at its top. Both are read
	// off the same two stops, which is why the row needs a gradient rather than a brush.
	if (!bLayerEnabled.Get(true))
	{
		return MixtormatPalette::LayerHiddenTop();
	}
	if (bExpanded.Get(false) || bSelected.Get(false))
	{
		return MixtormatPalette::LayerOpenTop();
	}
	return MixtormatPalette::Panel();
}

FLinearColor SMixtormatLayerRow::GetBackgroundEnd() const
{
	if (!bLayerEnabled.Get(true))
	{
		return MixtormatPalette::LayerHiddenEnd();
	}
	if (bExpanded.Get(false) || bSelected.Get(false))
	{
		return MixtormatPalette::LayerOpenEnd();
	}
	return MixtormatPalette::Panel();
}

FSlateColor SMixtormatLayerRow::GetNameColor() const
{
	return FSlateColor(bLayerEnabled.Get(true)
		? MixtormatPalette::LayerName()
		: MixtormatPalette::DisabledText());
}

FReply SMixtormatLayerRow::OnMouseButtonDown(const FGeometry&, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	// The empty space of the row selects, and begins a drag. Clicking the row anywhere but on the
	// eye is how a layer is picked up, which is why the eye is a separate widget that handles its
	// own press.
	OnSelected.ExecuteIfBound();
	return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
}
