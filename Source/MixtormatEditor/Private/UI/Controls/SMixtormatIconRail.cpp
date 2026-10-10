// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Controls/SMixtormatIconRail.h"
#include "Style/MixtormatCompositing.h"
#include "Style/MixtormatThemeStore.h"
#include "Style/MixtormatTypography.h"
#include "UI/Menus/SMixtormatHelp.h"

#include "Brushes/SlateRoundedBoxBrush.h"
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

            // Reuse the existing button accent blend and well shade blend.
            FLinearColor Surface = Palette.Get(Mixtormat::EMixtormatColorRole::Panel);
            Surface.A = 1.0f;
            FLinearColor Accent = Palette.Get(Mixtormat::EMixtormatColorRole::Accent);
            Accent.A = FMath::Clamp(bActive ? Theme.Button.SelectedTop
                : bHover ? Theme.Button.HoverTop : Theme.Button.RestTop, 0.0f, 1.0f);
            FLinearColor Fill = MixtormatCompositing::ApplyBlend(Theme.Button.BodyBlend, Surface, Accent);
            FLinearColor Shade = Palette.Get(Mixtormat::EMixtormatColorRole::Shade);
            Shade.A = FMath::Clamp(Theme.Well.ShadeBottom, 0.0f, 1.0f);
            Fill = MixtormatCompositing::ApplyBlend(Theme.Well.ShadeBlend, Fill, Shade);
            Fill.A = 1.0f;

            FLinearColor Border = Palette.Get(Mixtormat::EMixtormatColorRole::Hairline);
            Border.A *= Layout.LeftRailBorderOpacity
                * (bActive ? Theme.Button.HairlineSelectedOpacity
                    : bHover ? Theme.Button.HairlineHoverOpacity : Theme.Button.HairlineOpacity);
            const FLinearColor BlendedBorder = MixtormatCompositing::ApplyBlend(
                Theme.Button.HairlineBlend, Fill, Border);
            Border.R = BlendedBorder.R;
            Border.G = BlendedBorder.G;
            Border.B = BlendedBorder.B;

            // The narrow neck joins the tab to the spine. Only the protruding shoulder
            // receives a rounded contour, so the tab is not another floating rounded card.
            const float Radius = FMath::Clamp(Layout.LeftRailCornerRadius,
                0.0f, FMath::Min(BodySize.X, BodySize.Y) * 0.5f);
            const FSlateRoundedBoxBrush Face(Fill, FVector4(Radius, Radius, Radius, Radius),
                Border, Layout.LeftRailBorderThickness);

            if (Layout.LeftRailShadowOpacity > 0.0f && Layout.LeftRailShadowOffset > 0.0f)
            {
                const FSlateRoundedBoxBrush Shadow(FLinearColor::Black,
                    Radius + Layout.LeftRailShadowRadius);
                FSlateDrawElement::MakeBox(Elements, LayerId,
                    Geometry.ToPaintGeometry(BodySize,
                        FSlateLayoutTransform(BodyOffset + FVector2f(
                            Layout.LeftRailShadowOffset, Layout.LeftRailShadowOffset))),
                    &Shadow, ESlateDrawEffect::None,
                    FLinearColor(0.0f, 0.0f, 0.0f,
                        Layout.LeftRailShadowOpacity * WidgetStyle.GetColorAndOpacityTint().A));
            }

            FSlateDrawElement::MakeBox(Elements, LayerId + 1,
                Geometry.ToPaintGeometry(BodySize, FSlateLayoutTransform(BodyOffset)),
                &Face, ESlateDrawEffect::None, WidgetStyle.GetColorAndOpacityTint());

            // Fill the attachment neck over the body's left edge: no separated tiles
            // and no duplicate hairline where each tab meets the common rail.
            const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
            FSlateDrawElement::MakeBox(Elements, LayerId + 2,
                Geometry.ToPaintGeometry(
                    FVector2f(SpineWidth + 1.0f, Size.Y), FSlateLayoutTransform()),
                White, ESlateDrawEffect::None, Fill * WidgetStyle.GetColorAndOpacityTint());

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
                FSlateDrawElement::MakeBox(Elements, LayerId + 3,
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
                FSlateDrawElement::MakeText(Elements, LayerId + 4,
                    Geometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(Origin),
                        FSlateRenderTransform(FQuat2D(-HALF_PI)), FVector2D(0.5f, 0.5f)),
                    Label, Font, ESlateDrawEffect::None, Foreground);
            }

            return LayerId + 4;
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
