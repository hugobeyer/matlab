// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Controls/SMixtormatIconRail.h"
#include "Style/MixtormatCompositing.h"
#include "Style/MixtormatRecipes.h"
#include "UI/Primitives/MixtormatSurfacePainter.h"
#include "Style/MixtormatThemeStore.h"
#include "Style/MixtormatTypography.h"
#include "UI/Menus/SMixtormatHelp.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Styling/CoreStyle.h"

namespace
{
    // Self-painted leaf: no checkbox plates, multiline labels or oversized child layouts.
    // Its actual desired width is exactly the authored width of a narrow folder tab.
    class SMixtormatFolderTab final : public SLeafWidget
    {
    public:
        SLATE_BEGIN_ARGS(SMixtormatFolderTab) {}
            SLATE_ARGUMENT(const FSlateBrush*, Icon)
            SLATE_ARGUMENT(FText, Label)
            SLATE_ATTRIBUTE(bool, Selected)
            SLATE_EVENT(FSimpleDelegate, OnChosen)
        SLATE_END_ARGS()

        void Construct(const FArguments& InArgs)
        {
            Icon = InArgs._Icon;
            Label = InArgs._Label;
            Selected = InArgs._Selected;
            OnChosen = InArgs._OnChosen;
        }

        FVector2D ComputeDesiredSize(float) const override
        {
            const auto& Layout = FMixtormatThemeStore::GetResolved().PreviewLayout;
            return FVector2D(Layout.LeftRailButtonWidth, Layout.LeftRailButtonHeight);
        }

        FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent& Event) override
        {
            if (Event.GetEffectingButton() == EKeys::LeftMouseButton)
            {
                OnChosen.ExecuteIfBound();
                return FReply::Handled();
            }
            return FReply::Unhandled();
        }

        void OnMouseEnter(const FGeometry& Geometry, const FPointerEvent& Event) override
        {
            SLeafWidget::OnMouseEnter(Geometry, Event);
            Invalidate(EInvalidateWidgetReason::Paint);
        }

        void OnMouseLeave(const FPointerEvent& Event) override
        {
            SLeafWidget::OnMouseLeave(Event);
            Invalidate(EInvalidateWidgetReason::Paint);
        }

        int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry,
            const FSlateRect&, FSlateWindowElementList& Elements, const int32 LayerId,
            const FWidgetStyle& WidgetStyle, const bool) const override
        {
            const auto& Resolved = FMixtormatThemeStore::GetResolved();
            const auto& Layout = Resolved.PreviewLayout;
            const auto& Theme = FMixtormatThemeStore::GetTheme();
            const auto& Palette = Resolved.Palette;
            const FVector2f Size(Geometry.GetLocalSize());
            if (Size.X <= 0.0f || Size.Y <= 0.0f)
            {
                return LayerId;
            }

            const bool bActive = Selected.Get(false);
            const bool bHover = IsHovered();
            const float SpineWidth = FMath::Clamp(Layout.LeftRailInnerPadding, 2.0f, Size.X * 0.25f);
            const float Shoulder = FMath::Clamp(Layout.LeftRailCornerRadius * 2.0f,
                2.0f, FMath::Min(12.0f, Size.Y * 0.16f));
            const float BodyHeight = FMath::Max(Size.Y - 2.0f * Shoulder, 1.0f);
            const FVector2f BodyOffset(SpineWidth - 1.0f, Shoulder);
            const FVector2f BodySize(FMath::Max(1.0f, Size.X - BodyOffset.X), BodyHeight);

            // Use the exact shared group-button recipe. A local rounded Slate brush
            // cannot survive the deferred Slate paint pass, and ad-hoc compositing
            // cannot reproduce the button's vertical gradient and hairline blend.
            Mixtormat::FMixtormatSurfaceRecipe FaceRecipe = Mixtormat::MakeButtonRecipe(
                Theme, bActive ? Mixtormat::EMixtormatButtonState::Selected
                    : bHover ? Mixtormat::EMixtormatButtonState::Hover
                    : Mixtormat::EMixtormatButtonState::Rest, false);
            const float Radius = FMath::Clamp(Layout.LeftRailCornerRadius,
                0.0f, FMath::Min(BodySize.X, BodySize.Y) * 0.5f);
            FaceRecipe.Radius = Radius;
            Mixtormat::FMixtormatSurfaceSamples FaceSamples;
            Mixtormat::CompositeSurface(FaceRecipe, Palette,
                Mixtormat::FMixtormatStateModifier(), FaceSamples);
            const FLinearColor FaceTop = FaceSamples.Colors.IsEmpty()
                ? Palette.Get(Mixtormat::EMixtormatColorRole::Ground)
                : FaceSamples.Colors[0];

            if (Layout.LeftRailShadowOpacity > 0.0f && Layout.LeftRailShadowOffset > 0.0f)
            {
                Mixtormat::FMixtormatSurfaceRecipe ShadowRecipe = FaceRecipe;
                ShadowRecipe.Radius = Radius + Layout.LeftRailShadowRadius;
                Mixtormat::FMixtormatSurfaceDrawStyle ShadowStyle;
                ShadowStyle.Tint = FLinearColor(0.0f, 0.0f, 0.0f,
                    Layout.LeftRailShadowOpacity * WidgetStyle.GetColorAndOpacityTint().A);
                Mixtormat::FMixtormatSurfacePainter::PaintBody(Elements, LayerId,
                    Geometry.ToPaintGeometry(BodySize,
                        FSlateLayoutTransform(BodyOffset + FVector2f(
                            Layout.LeftRailShadowOffset, Layout.LeftRailShadowOffset))),
                    ShadowRecipe, FaceSamples, ShadowStyle);
            }

            Mixtormat::FMixtormatSurfaceDrawStyle FaceStyle;
            FaceStyle.Tint = WidgetStyle.GetColorAndOpacityTint();
            Mixtormat::FMixtormatSurfacePainter::PaintBody(Elements, LayerId + 1,
                Geometry.ToPaintGeometry(BodySize, FSlateLayoutTransform(BodyOffset)),
                FaceRecipe, FaceSamples, FaceStyle);

            // Fill the neck against the common spine. It uses the same resolved
            // recipe colour, never a separate white/native plate.
            const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
            FSlateDrawElement::MakeBox(Elements, LayerId + 2,
                Geometry.ToPaintGeometry(
                    FVector2f(SpineWidth + 1.0f, Size.Y), FSlateLayoutTransform()),
                White, ESlateDrawEffect::None, FaceTop * WidgetStyle.GetColorAndOpacityTint());

            // Carry the authored group-button top hairline over the protruding face.
            if (!FaceRecipe.Borders.IsEmpty())
            {
                const auto& Hairline = FaceRecipe.Borders[0];
                FLinearColor Source = Palette.Get(Mixtormat::EMixtormatColorRole::Accent);
                Source.A = Hairline.Source.Opacity
                    * Mixtormat::EvaluateRamp(Hairline.OpacityRamp, 0.0f)
                    * Layout.LeftRailBorderOpacity;
                const FLinearColor Edge = MixtormatCompositing::ApplyBlend(
                    Hairline.Blend, FaceTop, Source);
                const float BorderWidth = FMath::Clamp(Layout.LeftRailBorderThickness
                    * Hairline.Width, 0.0f, BodyHeight);
                if (BorderWidth > 0.0f && BodySize.X > Radius * 2.0f)
                {
                    FSlateDrawElement::MakeBox(Elements, LayerId + 3,
                        Geometry.ToPaintGeometry(
                            FVector2f(BodySize.X - Radius * 2.0f, BorderWidth),
                            FSlateLayoutTransform(BodyOffset + FVector2f(Radius, 0.0f))),
                        White, ESlateDrawEffect::None, Edge * WidgetStyle.GetColorAndOpacityTint());
                }
            }

            const auto& IconRole = Resolved.Icons.Roles[
                static_cast<uint8>(Mixtormat::EMixtormatIconRole::NavigationRail)];
            const float Glyph = FMath::Clamp(IconRole.GlyphSize, 4.0f, Size.X - 8.0f);
            const float GlyphTop = FMath::Max(Layout.LeftRailInnerPadding, 5.0f);
            FLinearColor Foreground = Palette.Get(bActive
                ? Mixtormat::EMixtormatColorRole::Text
                : Mixtormat::EMixtormatColorRole::TextMuted);
            if (bHover && !bActive)
            {
                Foreground = Palette.Get(Mixtormat::EMixtormatColorRole::Text);
            }
            Foreground *= WidgetStyle.GetColorAndOpacityTint();

            if (Icon)
            {
                FSlateDrawElement::MakeBox(Elements, LayerId + 4,
                    Geometry.ToPaintGeometry(FVector2f(Glyph, Glyph),
                        FSlateLayoutTransform(FVector2f((Size.X - Glyph) * 0.5f, GlyphTop))),
                    Icon, ESlateDrawEffect::None, Foreground);
            }

            // One real word, rotated as a single text element. Newlines per character caused
            // the old clipped lettering and inflated desired sizes.
            if (!Label.IsEmpty())
            {
                const auto Font = Mixtormat::FMixtormatTypography::MakeTextStyle(
                    Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography,
                        Mixtormat::EMixtormatTextRole::Badge),
                    Foreground).Font;
                const FVector2D TextSize = FSlateApplication::Get().GetRenderer()
                    ->GetFontMeasureService()->Measure(Label, Font, 1.0f);
                const float TextStart = GlyphTop + Glyph + Layout.LeftRailLabelGap;
                const FVector2D TextCenter(
                    Size.X * 0.5f, TextStart + FMath::Max(0.0f, Size.Y - TextStart) * 0.5f);
                const FVector2D Origin = TextCenter - TextSize * 0.5f;
                FSlateDrawElement::MakeText(Elements, LayerId + 5,
                    Geometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(Origin),
                        FSlateRenderTransform(FQuat2D(-HALF_PI)), FVector2D(0.5f, 0.5f)),
                    Label, Font, ESlateDrawEffect::None, Foreground);
            }

            return LayerId + 5;
        }

    private:
        const FSlateBrush* Icon = nullptr;
        FText Label;
        TAttribute<bool> Selected;
        FSimpleDelegate OnChosen;
    };
}

void SMixtormatIconRail::Construct(const FArguments& InArgs)
{
    ActiveIndex = InArgs._ActiveIndex;
    const auto& Layout = FMixtormatThemeStore::GetResolved().PreviewLayout;
    TSharedRef<SVerticalBox> Rail = SNew(SVerticalBox);
    for (int32 Index = 0; Index < InArgs._Options.Num(); ++Index)
    {
        const FMixtormatOnSegmentChosen Choose = InArgs._OnChosen;
        Rail->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, Layout.LeftRailButtonGap)
        [
            SNew(SMixtormatHelp)
            .Text(InArgs._ToolTips.IsValidIndex(Index)
                ? InArgs._ToolTips[Index] : FText::GetEmpty())
            [
                SNew(SMixtormatFolderTab)
                .Icon(InArgs._Options[Index])
                .Label(InArgs._Labels.IsValidIndex(Index)
                    ? InArgs._Labels[Index] : FText::GetEmpty())
                .Selected_Lambda([Active = ActiveIndex, Index]()
                {
                    return Active.Get(0) == Index;
                })
                .OnChosen(FSimpleDelegate::CreateLambda([Choose, Index]()
                {
                    Choose.ExecuteIfBound(Index);
                }))
            ]
        ];
    }
    ChildSlot [Rail];
}

int32 SMixtormatIconRail::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry,
    const FSlateRect& CullingRect, FSlateWindowElementList& Elements, int32 LayerId,
    const FWidgetStyle& WidgetStyle, bool bParentEnabled) const
{
    const auto& Layout = FMixtormatThemeStore::GetResolved().PreviewLayout;
    const FVector2f Size(Geometry.GetLocalSize());
    const float SpineWidth = FMath::Clamp(Layout.LeftRailInnerPadding, 2.0f, 5.0f);
    FLinearColor SpineColor = FMixtormatThemeStore::GetResolved().Palette.Get(
        Mixtormat::EMixtormatColorRole::Panel);
    SpineColor.A = 1.0f;
    if (Size.X > 0.0f && Size.Y > 0.0f)
    {
        FSlateDrawElement::MakeBox(Elements, LayerId,
            Geometry.ToPaintGeometry(FVector2f(SpineWidth, Size.Y),
                FSlateLayoutTransform()),
            FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")), ESlateDrawEffect::None,
            SpineColor * WidgetStyle.GetColorAndOpacityTint());
    }
    return SCompoundWidget::OnPaint(Args, Geometry, CullingRect, Elements,
        LayerId + 1, WidgetStyle, bParentEnabled);
}
