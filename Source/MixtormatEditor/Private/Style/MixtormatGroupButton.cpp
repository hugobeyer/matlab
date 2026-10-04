// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Style/MixtormatGroupButton.h"

#include "Brushes/SlateNoResource.h"
#include "Style/MixtormatCompositing.h"
#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatPalette.h"
#include "UI/Primitives/MixtormatGradientPainter.h"

namespace MixtormatGroupButton
{
	EState ResolveState(const bool bEnabled, const bool bHovered, const bool bPressed, const bool bSelected)
	{
		if (!bEnabled) { return bSelected ? EState::DisabledSelected : EState::Disabled; }
		if (bSelected) { return EState::Selected; }
		if (bPressed) { return EState::Active; }
		return bHovered ? EState::Hover : EState::Rest;
	}

	FLinearColor TextColor(const EState State)
	{
		const bool bSelected = State == EState::Selected || State == EState::Active || State == EState::DisabledSelected;
		FLinearColor Color = bSelected ? MixtormatPalette::Accent() : MixtormatPalette::RowText();
		Color.A = bSelected || State == EState::Hover ? 1.0f : MixtormatTokens::GroupButtonTextOpacity;
		if (State == EState::Disabled || State == EState::DisabledSelected)
		{
			Color.A *= MixtormatTokens::TextDisabledOpacity;
		}
		return Color;
	}

	FButtonStyle MakeButtonStyle(const FButtonStyle& Existing)
	{
		FButtonStyle Result = Existing;
		Result.SetNormal(FSlateNoResource()).SetHovered(FSlateNoResource())
			.SetPressed(FSlateNoResource()).SetDisabled(FSlateNoResource())
			.SetNormalForeground(TextColor(EState::Rest))
			.SetHoveredForeground(TextColor(EState::Hover))
			.SetPressedForeground(TextColor(EState::Active))
			.SetDisabledForeground(TextColor(EState::Disabled))
			.SetNormalPadding(FMargin(0.0f)).SetPressedPadding(FMargin(0.0f));
		return Result;
	}

	FCheckBoxStyle MakeCheckBoxStyle(const FCheckBoxStyle& Existing)
	{
		FCheckBoxStyle Result = Existing;
		Result.SetUncheckedImage(FSlateNoResource()).SetUncheckedHoveredImage(FSlateNoResource())
			.SetUncheckedPressedImage(FSlateNoResource()).SetCheckedImage(FSlateNoResource())
			.SetCheckedHoveredImage(FSlateNoResource()).SetCheckedPressedImage(FSlateNoResource())
			.SetUndeterminedImage(FSlateNoResource()).SetUndeterminedHoveredImage(FSlateNoResource())
			.SetUndeterminedPressedImage(FSlateNoResource()).SetBackgroundImage(FSlateNoResource())
			.SetBackgroundHoveredImage(FSlateNoResource()).SetBackgroundPressedImage(FSlateNoResource());
		return Result;
	}
}

void SMixtormatGroupButtonSurface::Construct(const FArguments& InArgs)
{
	Hovered = InArgs._Hovered;
	Pressed = InArgs._Pressed;
	Selected = InArgs._Selected;
	Ground = InArgs._Ground;
	bShowSeparator = InArgs._ShowSeparator;
	GradientStops.Reserve(13);
	for (int32 Index = 0; Index < 13; ++Index)
	{
		GradientStops.Emplace(FVector2f::ZeroVector, FLinearColor::Transparent);
	}
	ChildSlot.Padding(InArgs._Padding)[InArgs._Content.Widget];
}

int32 SMixtormatGroupButtonSurface::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry,
	const FSlateRect& CullingRect, FSlateWindowElementList& Elements, const int32 LayerId,
	const FWidgetStyle& WidgetStyle, const bool bParentEnabled) const
{
	using namespace MixtormatTokens;
	using namespace MixtormatGroupButton;
	const FVector2f Size(Geometry.GetLocalSize());
	const EState State = ResolveState(ShouldBeEnabled(bParentEnabled), Hovered.Get(false), Pressed.Get(false), Selected.Get(false));
	const bool bSelected = State == EState::Selected || State == EState::Active || State == EState::DisabledSelected;
	const bool bHover = State == EState::Hover;
	const float Dim = State == EState::Disabled || State == EState::DisabledSelected ? TextDisabledOpacity : 1.0f;
	const float Top = bSelected ? GroupButtonSelectedGradientTop : bHover ? GroupButtonHoverGradientTop : GroupButtonGradientTop;
	const float Bottom = bSelected ? GroupButtonSelectedGradientBottom : bHover ? GroupButtonHoverGradientBottom : GroupButtonGradientBottom;
	const float Hairline = bSelected ? GroupButtonSelectedHairlineOpacity : bHover ? GroupButtonHoverHairlineOpacity : GroupButtonHairlineOpacity;
	const FLinearColor Backdrop = Ground.Get(MixtormatPalette::Ground());
	const FLinearColor Accent = MixtormatPalette::Accent();
	const FLinearColor GradientSource = MixtormatCompositing::Saturate(Accent, GroupButtonGradientSaturation).GetClamped();
	const auto BlendBody = [&](const float Opacity)
	{
		const float Alpha = FMath::Clamp(Opacity * Dim, 0.0f, 1.0f);
		return FLinearColor(
			FMath::Lerp(Backdrop.R, GradientSource.R, Alpha),
			FMath::Lerp(Backdrop.G, GradientSource.G, Alpha),
			FMath::Lerp(Backdrop.B, GradientSource.B, Alpha), Backdrop.A);
	};
	const FLinearColor BodyTop = BlendBody(Top);
	const FLinearColor BodyBottom = BlendBody(Bottom);
	const auto BodyAt = [&](const float T)
	{
		return MixtormatGradient::LerpSRGB(BodyTop, BodyBottom, T);
	};
	const FLinearColor Tint = WidgetStyle.GetColorAndOpacityTint();
	const auto Ramp = [&](const FVector2f& Offset, const FVector2f& Extent, const int32 Layer,
		const FLinearColor& Start, const FLinearColor& End)
	{
		if (Extent.X <= 0.0f || Extent.Y <= 0.0f) { return; }
		for (int32 Index = 0; Index < GradientStops.Num(); ++Index)
		{
			const float T = static_cast<float>(Index) / (GradientStops.Num() - 1);
			GradientStops[Index] = FSlateGradientStop(FVector2f(0.0f, Extent.Y * T), MixtormatGradient::LerpSRGB(Start, End, T) * Tint);
		}
		FSlateDrawElement::MakeGradient(Elements, Layer,
			Geometry.ToPaintGeometry(Extent, FSlateLayoutTransform(Offset)), GradientStops,
			Orient_Horizontal, ESlateDrawEffect::None, FVector4f(0.0f));
	};

	// Normal source-over plate, then additive edges resolved over that exact plate.
	Ramp(FVector2f::ZeroVector, Size, LayerId, BodyAt(0.0f), BodyAt(1.0f));
	FLinearColor HairlineSource = MixtormatCompositing::Saturate(Accent, GroupButtonHairlineSaturation).GetClamped();
	HairlineSource.A = Hairline * Dim;
	const float LineHeight = FMath::Clamp(GroupButtonHairlineWidth, 0.0f, Size.Y);
	Ramp(FVector2f::ZeroVector, FVector2f(Size.X, LineHeight), LayerId + 1,
		MixtormatCompositing::Additive(BodyAt(0.0f), HairlineSource),
		MixtormatCompositing::Additive(BodyAt(Size.Y > 0.0f ? LineHeight / Size.Y : 0.0f), HairlineSource));
	if (bShowSeparator && Size.Y > 0.0f)
	{
		const float Width = FMath::Clamp(GroupButtonSeparatorWidth, 0.0f, Size.X);
		const float Height = FMath::Clamp(GroupButtonSeparatorHeight, 0.0f, Size.Y);
		const float Y = (Size.Y - Height) * 0.5f;
		// CSS puts separator and hairline in the same filtered additive layer.
		FLinearColor Separator = MixtormatCompositing::Saturate(MixtormatPalette::RowText(), GroupButtonHairlineSaturation).GetClamped();
		Separator.A = GroupButtonSeparatorOpacity * Dim;
		const auto EdgeAt = [&](const float Position, const bool bOnHairline)
		{
			FLinearColor Color = BodyAt(Position / Size.Y);
			if (bOnHairline) { Color = MixtormatCompositing::Additive(Color, HairlineSource); }
			return MixtormatCompositing::Additive(Color, Separator);
		};
		// Split at the hairline seam rather than interpolating its light down the separator.
		const float Split = FMath::Clamp(LineHeight, Y, Y + Height);
		Ramp(FVector2f(Size.X - Width, Y), FVector2f(Width, Split - Y), LayerId + 2,
			EdgeAt(Y, true), EdgeAt(Split, true));
		Ramp(FVector2f(Size.X - Width, Split), FVector2f(Width, Y + Height - Split), LayerId + 2,
			EdgeAt(Split, false), EdgeAt(Y + Height, false));
	}
	FWidgetStyle ContentStyle = WidgetStyle;
	ContentStyle.SetForegroundColor(TextColor(State));
	return SCompoundWidget::OnPaint(Args, Geometry, CullingRect, Elements, LayerId + 3, ContentStyle, bParentEnabled);
}
