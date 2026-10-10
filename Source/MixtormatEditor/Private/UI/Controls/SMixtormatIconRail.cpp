// Copyright 2026 Hugo Beyer. All Rights Reserved.
#include "UI/Controls/SMixtormatIconRail.h"
#include "Style/MixtormatThemeStore.h"
#include "Style/MixtormatCompositing.h"
#include "UI/Menus/SMixtormatHelp.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Rendering/DrawElements.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"

namespace
{
class SMixtormatFolderTab final : public SCompoundWidget
{
public:
 SLATE_BEGIN_ARGS(SMixtormatFolderTab) {}
 SLATE_ARGUMENT(const FSlateBrush*, Icon)
 SLATE_ARGUMENT(FText, Label)
 SLATE_ATTRIBUTE(bool, Selected)
 SLATE_EVENT(FSimpleDelegate, OnChosen)
 SLATE_END_ARGS()

 void Construct(const FArguments& Args)
 {
  Selected = Args._Selected;
  OnChosen = Args._OnChosen;
  const auto& M = FMixtormatThemeStore::GetResolved().PreviewLayout;
  const auto& Role = FMixtormatThemeStore::GetResolved().Icons.Roles[static_cast<uint8>(Mixtormat::EMixtormatIconRole::NavigationRail)];
  FString VerticalLabel;
  const FString Text = Args._Label.ToString();
  for (int32 I = 0; I < Text.Len(); ++I)
  {
   if (I) VerticalLabel += TEXT("\n");
   VerticalLabel.AppendChar(Text[I]);
  }
  ChildSlot
  [
   SNew(SBox)
   .WidthOverride(M.LeftRailButtonWidth)
   .HeightOverride(M.LeftRailButtonHeight)
   .Padding(FMargin(M.LeftRailInnerPadding))
   [
    SNew(SVerticalBox)
    + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
    [
     SNew(SBox).WidthOverride(Role.GlyphSize).HeightOverride(Role.GlyphSize)
     [ SNew(SImage).Image(Args._Icon) ]
    ]
    + SVerticalBox::Slot().FillHeight(1.0f).HAlign(HAlign_Center).VAlign(VAlign_Center)
    .Padding(0.0f, M.LeftRailLabelGap, 0.0f, 0.0f)
    [
     SNew(STextBlock).Text(FText::FromString(VerticalLabel))
     .Justification(ETextJustify::Center)
     .ColorAndOpacity_Lambda([this]()
     {
      const auto& R = FMixtormatThemeStore::GetResolved();
      return FSlateColor(R.Palette.Get(Selected.Get(false) ? Mixtormat::EMixtormatColorRole::Text : Mixtormat::EMixtormatColorRole::TextMuted));
     })
    ]
   ]
  ];
 }
 virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent& Event) override
 {
  if (Event.GetEffectingButton() == EKeys::LeftMouseButton)
  {
   OnChosen.ExecuteIfBound();
   return FReply::Handled();
  }
  return FReply::Unhandled();
 }
 virtual void OnMouseEnter(const FGeometry& Geometry, const FPointerEvent& Event) override
 {
  SCompoundWidget::OnMouseEnter(Geometry, Event);
  Invalidate(EInvalidateWidgetReason::Paint);
 }
 virtual void OnMouseLeave(const FPointerEvent& Event) override
 {
  SCompoundWidget::OnMouseLeave(Event);
  Invalidate(EInvalidateWidgetReason::Paint);
 }
 virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Clip,
  FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool Enabled) const override
 {
  const auto& R = FMixtormatThemeStore::GetResolved();
  const auto& M = R.PreviewLayout;
  const bool Active = Selected.Get(false);
  const bool Hover = IsHovered();
  const FLinearColor Base = R.Palette.Get(Mixtormat::EMixtormatColorRole::Panel);
  FLinearColor Shade = R.Palette.Get(Mixtormat::EMixtormatColorRole::Hairline);
  Shade.A = Active ? 0.42f : Hover ? 0.28f : 0.14f;
  const FLinearColor Fill = MixtormatCompositing::ApplyBlend(MixtormatCompositing::EMixtormatBlendMode::Normal, Base, Shade);
  FLinearColor Edge = R.Palette.Get(Mixtormat::EMixtormatColorRole::Hairline);
  Edge.A *= M.LeftRailBorderOpacity;
  const float Radius = M.LeftRailCornerRadius;
  FSlateRoundedBoxBrush Face(Fill, Radius, Edge, M.LeftRailBorderThickness);
  FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(), &Face, ESlateDrawEffect::None, Style.GetColorAndOpacityTint());
  return SCompoundWidget::OnPaint(Args, Geometry, Clip, Out, Layer + 1, Style, Enabled);
 }
private:
 TAttribute<bool> Selected;
 FSimpleDelegate OnChosen;
};
}

void SMixtormatIconRail::Construct(const FArguments& InArgs)
{
 ActiveIndex = InArgs._ActiveIndex;
 const auto& M = FMixtormatThemeStore::GetResolved().PreviewLayout;
 TSharedRef<SVerticalBox> Rail = SNew(SVerticalBox);
 for (int32 Index = 0; Index < InArgs._Options.Num(); ++Index)
 {
  const FMixtormatOnSegmentChosen Choose = InArgs._OnChosen;
  Rail->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, M.LeftRailButtonGap)
  [
   SNew(SMixtormatHelp)
   .Text(InArgs._ToolTips.IsValidIndex(Index) ? InArgs._ToolTips[Index] : FText::GetEmpty())
   [
    SNew(SMixtormatFolderTab)
    .Icon(InArgs._Options[Index])
    .Label(InArgs._Labels.IsValidIndex(Index) ? InArgs._Labels[Index] : FText::GetEmpty())
    .Selected_Lambda([Active = ActiveIndex, Index]() { return Active.Get(0) == Index; })
    .OnChosen(FSimpleDelegate::CreateLambda([Choose, Index]() { Choose.ExecuteIfBound(Index); }))
   ]
  ];
 }
 ChildSlot [ Rail ];
}

int32 SMixtormatIconRail::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry,
 const FSlateRect& Clip, FSlateWindowElementList& Out, int32 Layer,
 const FWidgetStyle& Style, bool Enabled) const
{
 const auto& R = FMixtormatThemeStore::GetResolved();
 const auto& M = R.PreviewLayout;
 const FVector2D Size = Geometry.GetLocalSize();
 const float SpineWidth = FMath::Max(2.0f, M.LeftRailInnerPadding);
 FSlateRoundedBoxBrush Spine(R.Palette.Get(Mixtormat::EMixtormatColorRole::Panel), 0.0f);
 FSlateDrawElement::MakeBox(Out, Layer,
  Geometry.ToPaintGeometry(FVector2D(SpineWidth, Size.Y), FSlateLayoutTransform()),
  &Spine, ESlateDrawEffect::None, Style.GetColorAndOpacityTint());
 return SCompoundWidget::OnPaint(Args, Geometry, Clip, Out, Layer + 1, Style, Enabled);
}
