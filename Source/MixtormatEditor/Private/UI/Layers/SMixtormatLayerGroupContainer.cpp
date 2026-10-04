// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Layers/SMixtormatLayerGroupContainer.h"
#include "Layout/ArrangedChildren.h"
#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatPalette.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"

void SMixtormatLayerGroupContainer::Construct(const FArguments& InArgs)
{
	SetVisibility(EVisibility::SelfHitTestInvisible);
	ChildSlot
	[
		SAssignNew(Stack, SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SAssignNew(HeaderBox, SBox)
			[InArgs._Header.Widget]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SAssignNew(BodyBox, SBox)
			[InArgs._Body.Widget]
		]
	];
}

int32 SMixtormatLayerGroupContainer::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry,
	const FSlateRect& CullingRect, FSlateWindowElementList& Elements, int32 LayerId,
	const FWidgetStyle& Style, bool bParentEnabled) const
{
	MixtormatGroupCard::FSurface Surface;
	Surface.Size = FVector2f(Geometry.GetLocalSize());
	if (Surface.Size.X <= 0.0f || Surface.Size.Y <= 0.0f)
	{
		return LayerId;
	}
	FArrangedChildren Arranged(EVisibility::Visible);
	Stack->ArrangeChildren(Geometry, Arranged);
	for (int32 Index = 0; Index < Arranged.Num(); ++Index)
	{
		const FArrangedWidget& Child = Arranged[Index];
		const float Top = Geometry.AbsoluteToLocal(
			Child.Geometry.LocalToAbsolute(FVector2D::ZeroVector)).Y;
		if (Child.Widget == HeaderBox)
		{
			Surface.HeaderTop = Top;
			Surface.HeaderHeight = Child.Geometry.GetLocalSize().Y;
		}
		else if (Child.Widget == BodyBox)
		{
			Surface.BodyTop = Top;
			Surface.BodyHeight = Child.Geometry.GetLocalSize().Y;
		}
	}
	Surface.AuthoredHeaderHeight = MixtormatTokens::LayerGroupRowHeight;
	Surface.Radius = MixtormatTokens::GroupCardRadius;
	Surface.Reach = MixtormatTokens::GroupCardGradientReach;
	Surface.Power = MixtormatTokens::GroupCardFalloffPower;
	Surface.HeaderOpacity = MixtormatTokens::GroupCardHeaderOpacity;
	Surface.BodyOpacity = MixtormatTokens::GroupCardBodyOpacity;
	Surface.HeaderSaturation = MixtormatTokens::GroupCardHeaderSaturation;
	Surface.BodySaturation = MixtormatTokens::GroupCardBodySaturation;
	Surface.Ground = MixtormatPalette::Ground();
	const ESlateDrawEffect Effect = ShouldBeEnabled(bParentEnabled)
		? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	const int32 ClipCount = MixtormatGroupCard::PushRoundedClip(Elements, Geometry, Surface.Radius);
	SurfacePainter.Paint(Elements, LayerId, Geometry, Surface, Style.GetColorAndOpacityTint(), Effect);
	const int32 LastLayer = SCompoundWidget::OnPaint(Args, Geometry, CullingRect,
		Elements, LayerId + 1, Style, bParentEnabled);
	for (int32 Index = 0; Index < ClipCount; ++Index)
	{
		Elements.PopClip();
	}
	return LastLayer;
}
