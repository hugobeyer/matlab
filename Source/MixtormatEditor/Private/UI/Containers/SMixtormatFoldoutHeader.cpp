// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Containers/SMixtormatFoldoutHeader.h"

#include "Style/MixtormatCompositing.h"
#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatPalette.h"
#include "Styling/AppStyle.h"
#include "UI/Primitives/MixtormatGradientPainter.h"

void SMixtormatFoldoutHeader::Construct(const FArguments& InArgs)
{
	Hovered = InArgs._IsHovered;
	bEnabled = InArgs._bEnabled;

	ChildSlot
	[
		InArgs._Content.Widget
	];
}

FVector2D SMixtormatFoldoutHeader::ComputeDesiredSize(const float LayoutScaleMultiplier) const
{
	// Height is the authored minimum *plus* the header's own vertical padding, because the design
	// uses min-height rather than height: the padding adds to the bar instead of eating it. Getting
	// this backwards is what produced a 20px bar whose text was clipped by its own padding.
	//
	// Width comes from the child, which fills the bar -- a fixed zero here would collapse every
	// foldout to the width of nothing.
	const FVector2D ChildSize = SCompoundWidget::ComputeDesiredSize(LayoutScaleMultiplier);
	return FVector2D(
		ChildSize.X,
		FMath::Max(ChildSize.Y,
			MixtormatTokens::FoldoutHeight
				+ MixtormatTokens::FoldoutHeaderPaddingTop
				+ MixtormatTokens::FoldoutHeaderPaddingBottom));
}

int32 SMixtormatFoldoutHeader::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	const int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	const bool bParentEnabled) const
{
	// The locals are renamed rather than reusing the member names: `bool bEnabled = bEnabled.Get(...)`
	// shadows the TAttribute and fails to initialise itself.
	const bool bIsHovered = Hovered.Get(false);
	const bool bIsEnabled = bEnabled.Get(true);
	const FVector2f Size(AllottedGeometry.GetLocalSize());
	const FVector4f Radii(MixtormatTokens::FoldoutRadius);

	const FSlateBrush* White = FAppStyle::GetBrush("WhiteBrush");
	const FPaintGeometry PaintGeometry = AllottedGeometry.ToPaintGeometry();
	// The header's own content sits above all four layers, which is why they are numbered below it.
	const int32 ContentLayer = LayerId + 4;

	if (Size.X <= 0.0f || Size.Y <= 0.0f)
	{
		// Nothing to paint the stack onto, but the child still has to be laid out and painted --
		// returning early here would silently drop the title and the actions.
		return SCompoundWidget::OnPaint(
			Args, AllottedGeometry, MyCullingRect, OutDrawElements, ContentLayer,
			InWidgetStyle, bParentEnabled);
	}

	// ---- 1. Ground -------------------------------------------------------------------------
	// The prototype's foldout background is Ground, the same base every card and panel column is
	// painted off. Not Panel and not Shell: the foldout sits inside the inspector column, and a
	// lift measured against a different base would be the wrong lift.
	FSlateDrawElement::MakeBox(
		OutDrawElements, LayerId, PaintGeometry, White,
		ESlateDrawEffect::None, MixtormatPalette::Ground());

	// ---- 2. Additive lift ------------------------------------------------------------------
	// A tint added over the ground, falling to zero at the body seam. Resolved to a final colour
	// and painted flat, because Slate cannot add; the *opacity* of that tint still follows the
	// authored falloff, which is what makes the header dissolve into its body rather than stop.
	//
	// Saturation is applied to this layer alone. The same tint role is read unsaturated by other
	// surfaces, so putting it in the palette would over-saturate them.
	const float LiftOpacity = bIsHovered ? MixtormatTokens::HeaderHoverOpacity : MixtormatTokens::HeaderTintOpacity;
	const float LiftSaturation = bIsHovered
		? MixtormatTokens::FoldoutHoverSaturation
		: MixtormatTokens::FoldoutSaturation;
	FLinearColor Lift = bIsHovered ? MixtormatPalette::FoldoutLiftHover() : MixtormatPalette::FoldoutLift();
	Lift.A = 1.0f;
	Lift = MixtormatCompositing::Saturate(Lift, LiftSaturation);

	TArray<MixtormatGradient::FStop, TInlineAllocator<16>> LiftStops;
	MixtormatGradient::AppendFalloffStops(
		LiftStops, Lift,
		LiftOpacity, 0.0f, MixtormatTokens::FoldoutFalloffPower,
		0.0f, 1.0f, MixtormatTokens::GradientSamplesPerSpan);

	// Each stop is the lift composited additively onto the ground, so the ramp is a ramp of
	// *results* rather than a translucent overlay -- the overlay would darken the ground toward
	// black on the way, which is the opposite of what an additive lift does.
	for (MixtormatGradient::FStop& Stop : LiftStops)
	{
		Stop.Color = MixtormatCompositing::Additive(
			MixtormatPalette::Ground(), Stop.Color);
	}
	MixtormatGradient::Paint(
		OutDrawElements, LayerId + 1, PaintGeometry, Size,
		Orient_Vertical, LiftStops, Radii);

	// ---- 3. Accent cross pass --------------------------------------------------------------
	// A saturated accent over the lift, fading to zero at the same seam. The prototype composites
	// this one with soft-light, which darkens and saturates the top edge instead of adding light
	// to it -- so it is SoftLight against the already-lifted ground, not another Additive.
	const float AccentOpacity = bIsHovered
		? MixtormatTokens::FoldoutAccentHoverMultiplyOpacity
		: MixtormatTokens::FoldoutAccentMultiplyOpacity;

	TArray<MixtormatGradient::FStop, TInlineAllocator<16>> AccentStops;
	MixtormatGradient::AppendFalloffStops(
		AccentStops,
		MixtormatCompositing::Saturate(
			bIsHovered ? MixtormatPalette::FoldoutAccentHover() : MixtormatPalette::FoldoutAccent(),
			LiftSaturation),
		AccentOpacity, 0.0f, MixtormatTokens::FoldoutFalloffPower,
		0.0f, 1.0f, MixtormatTokens::GradientSamplesPerSpan);

	// Soft-light needs the backdrop, so each stop is resolved against the lift *at that same stop*
	// rather than painted as a translucent overlay. Compositing the whole accent ramp against one
	// constant colour would make the top edge blend against the fully-lifted header and the seam
	// against almost-nothing, which is exactly the seam that has to disappear.
	for (int32 Index = 0; Index < AccentStops.Num() && Index < LiftStops.Num(); ++Index)
	{
		AccentStops[Index].Color = MixtormatCompositing::SoftLight(
			LiftStops[Index].Color, AccentStops[Index].Color);
	}
	MixtormatGradient::Paint(
		OutDrawElements, LayerId + 2, PaintGeometry, Size,
		Orient_Vertical, AccentStops, Radii);

	// ---- 4. Hairline -----------------------------------------------------------------------
	// Its own one-pixel layer at the top edge, above both ramps and below the header's content.
	// Not the foldout's outline and not a border on the gradient box: the design puts a lit line
	// *inside* the foldout's top edge, and folding it into the background would mean the body got
	// a rim too.
	//
	// Saturated on its own terms, which is the point of it being separate: a near-neutral grey
	// pushed to 2.0 reads as a lit edge, and the same grey as a border would just read as grey.
	//
	// Disabled replaces the line rather than dimming it, so a disabled group loses its lit edge
	// instead of showing a dimmed copy of it.
	if (bIsEnabled)
	{
		const FLinearColor HairlineColor = MixtormatCompositing::Saturate(
			bIsHovered ? MixtormatPalette::FoldoutHairlineHover() : MixtormatPalette::FoldoutHairline(),
			bIsHovered
				? MixtormatTokens::FoldoutHairlineHoverSaturation
				: MixtormatTokens::FoldoutHairlineSaturation);

		FSlateDrawElement::MakeBox(
			OutDrawElements, LayerId + 3,
			AllottedGeometry.ToPaintGeometry(
				FVector2f(Size.X, MixtormatTokens::HairlineThickness),
				FSlateLayoutTransform(FVector2f::ZeroVector)),
			White, ESlateDrawEffect::None, HairlineColor);
	}

	return SCompoundWidget::OnPaint(
		Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 4,
		InWidgetStyle, bParentEnabled);
}