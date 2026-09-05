#include "UI/Containers/SMixtormatMenuPanel.h"

#include "Style/MixtormatPalette.h"
#include "Style/MixtormatStyle.h"
#include "UI/Primitives/MixtormatGradientPainter.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"

void SMixtormatMenuPanel::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SOverlay)

		// The additive lip, over the top edge -- the same seam a group header carries, so a menu
		// reads as another sheet in the same stack rather than as a different kind of surface.
		+ SOverlay::Slot()
		.VAlign(VAlign_Top)
		[
			SNew(SBox)
			.HeightOverride(MixtormatTokens::HairlineThickness)
			[
				SNew(SImage).Image(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.HeaderHairline")))
			]
		]

		+ SOverlay::Slot()
		[
			SNew(SBox)
			.MinDesiredWidth(InArgs._MinWidth)
			.Padding(InArgs._Padding)
			[
				InArgs._Content.Widget
			]
		]
	];
}

int32 SMixtormatMenuPanel::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	const bool bParentEnabled) const
{
	const FVector2f Size = FVector2f(AllottedGeometry.GetLocalSize());

	// The middle stop is a height, not a fraction: the tint has to be spent by the bottom of the
	// first row whatever the menu's length. Clamped below one so a menu shorter than its own lip
	// still gets a ramp rather than a flat tinted block.
	const float LipStop = Size.Y > UE_SMALL_NUMBER
		? FMath::Min(MixtormatTokens::MenuLipHeight / Size.Y, 1.0f)
		: 1.0f;

	const MixtormatGradient::FStop Ground[] = {
		{ 0.0f, MixtormatPalette::MenuTint() },
		{ LipStop, MixtormatPalette::MenuGroundTop() },
		{ 1.0f, MixtormatPalette::MenuGround() },
	};
	MixtormatGradient::Paint(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToPaintGeometry(),
		Size,
		Orient_Vertical,
		Ground,
		FVector4f(MixtormatTokens::MenuCornerRadius));

	return SCompoundWidget::OnPaint(
		Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 1, InWidgetStyle, bParentEnabled);
}
