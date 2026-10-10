// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Controls/SMixtormatIconRail.h"
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
            SLATE_ARGUMENT(int32, TabIndex)
            SLATE_ARGUMENT(int32, TabCount)
            SLATE_EVENT(FSimpleDelegate, OnChosen)
        SLATE_END_ARGS()

        void Construct(const FArguments& InArgs)
        {
            Icon = InArgs._Icon;
            Label = InArgs._Label;
            Selected = InArgs._Selected;
            TabIndex = InArgs._TabIndex;
            TabCount = FMath::Max(1, InArgs._TabCount);
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
            // Full-rect, contiguous tabs. One shared recipe provides the
            // group-button body, hairline and globally biased vertical shade.
            // No spine, neck, outside shadow or independent edge plate.
            const Mixtormat::EMixtormatButtonState State = bActive
                ? Mixtormat::EMixtormatButtonState::Selected
                : bHover ? Mixtormat::EMixtormatButtonState::Hover
                : Mixtormat::EMixtormatButtonState::Rest;
            const Mixtormat::FMixtormatSurfaceRecipe Recipe =
                Mixtormat::MakeNavigationRailTabRecipe(Theme, State, TabIndex, TabCount);
            Mixtormat::FMixtormatSurfaceDrawStyle DrawStyle;
            DrawStyle.Tint = WidgetStyle.GetColorAndOpacityTint();
            const int32 SurfaceLayer = Mixtormat::FMixtormatSurfacePainter::PaintSurface(
                Elements, LayerId, Geometry, Recipe, Palette, WidgetStyle, DrawStyle);

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
                FSlateDrawElement::MakeBox(Elements, SurfaceLayer + 1,
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
                FSlateDrawElement::MakeText(Elements, SurfaceLayer + 2,
                    Geometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(Origin),
                        FSlateRenderTransform(FQuat2D(-HALF_PI)), FVector2D(0.5f, 0.5f)),
                    Label, Font, ESlateDrawEffect::None, Foreground);
            }

            return SurfaceLayer + 2;
        }

    private:
        const FSlateBrush* Icon = nullptr;
        FText Label;
        TAttribute<bool> Selected;
        int32 TabIndex = 0;
        int32 TabCount = 1;
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
                .TabIndex(Index)
                .TabCount(InArgs._Options.Num())
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
    // No left-side spine: the tabs are painted within the page's own inset.
    return SCompoundWidget::OnPaint(Args, Geometry, CullingRect, Elements,
        LayerId, WidgetStyle, bParentEnabled);
}
