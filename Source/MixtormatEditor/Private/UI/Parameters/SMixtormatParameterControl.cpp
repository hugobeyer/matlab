#include "UI/Parameters/SMixtormatParameterControl.h"

#include "Style/MixtormatDesignTokens.h"
#include "UI/Atoms/SMixtormatStatusDot.h"
#include "UI/Menus/SMixtormatPopupAnchor.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "Mixtormat"

void SMixtormatParameterControl::Construct(const FArguments& InArgs)
{
	bHasTarget = InArgs._bHasTarget;
	bReferenced = InArgs._bReferenced;
	bDriven = InArgs._bDriven;
	bBroken = InArgs._bBroken;

	ChildSlot
	[
		SAssignNew(ContextAnchor, SMixtormatPopupAnchor)
		.OnGetMenuContent(InArgs._OnGetContextMenu)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				InArgs._Content.Widget
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(MixtormatTokens::ParameterStateGap, 0.0f, 0.0f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(MixtormatTokens::ParameterStateSlotWidth)
				.HAlign(HAlign_Center)
				// Collapsed, not hidden: a parameter with nothing on it must give the width
				// back. A hollow dot on all ~118 rows is noise, and reserving its 12px so it
				// could appear on hover would ripple the whole column as the cursor ran down
				// it. Add Driver... lives in the right-click menu, so nothing is lost by the
				// slot being absent until there is state to report.
				.Visibility(TAttribute<EVisibility>::CreateSP(
					this, &SMixtormatParameterControl::GetStateVisibility))
				[
					SAssignNew(DriverAnchor, SMixtormatPopupAnchor)
					.OnGetMenuContent(InArgs._OnGetDriverContent)
					[
						SNew(SMixtormatStatusDot)
						.Size(MixtormatTokens::StatusDotSize)
						.BrushName(TAttribute<FName>::CreateSP(
							this, &SMixtormatParameterControl::GetStateBrushName))
						.ToolTip(TAttribute<FText>::CreateSP(this, &SMixtormatParameterControl::GetStateToolTip))
						.OnClicked(FSimpleDelegate::CreateSP(this, &SMixtormatParameterControl::OpenDriverPopover))
					]
				]
			]
		]
	];
}

SMixtormatParameterControl::EState SMixtormatParameterControl::GetState() const
{
	if (!bHasTarget.Get(true))
	{
		return EState::None;
	}
	if (bBroken.Get(false)) { return EState::Broken; }
	if (bReferenced.Get(false)) { return EState::Referenced; }
	if (bDriven.Get(false)) { return EState::Driven; }
	return EState::None;
}

FName SMixtormatParameterControl::GetStateBrushName() const
{
	switch (GetState())
	{
	case EState::Broken:     return TEXT("Mixtormat.StatusDot.Broken");
	case EState::Referenced: return TEXT("Mixtormat.StatusDot.Reference");
	case EState::Driven:     return TEXT("Mixtormat.StatusDot.Filled");
	default:                 return TEXT("Mixtormat.StatusDot.Hollow");
	}
}

EVisibility SMixtormatParameterControl::GetStateVisibility() const
{
	return GetState() == EState::None ? EVisibility::Collapsed : EVisibility::Visible;
}

FText SMixtormatParameterControl::GetStateToolTip() const
{
	switch (GetState())
	{
	case EState::Broken:
		return LOCTEXT("ParameterReferenceBroken", "Reference source is missing. Local value is used.");
	case EState::Referenced:
		return LOCTEXT("ParameterReferenced", "This parameter follows another parameter. Click for Driver options; right-click for reference actions.");
	case EState::Driven:
		return LOCTEXT("ParameterDriven", "This parameter is modulated by a Driver. Click to edit it.");
	default:
		return LOCTEXT("ParameterNotDriven", "Click to add a Driver. Right-click for Copy/Paste Reference.");
	}
}

void SMixtormatParameterControl::OpenDriverPopover()
{
	if (DriverAnchor.IsValid())
	{
		DriverAnchor->OpenAt(FSlateApplication::Get().GetCursorPos());
	}
}

FReply SMixtormatParameterControl::OnMouseButtonDown(const FGeometry&, const FPointerEvent& MouseEvent)
{
	// Unhandled without a target, so the press keeps bubbling to the group and its own menu --
	// Expand All -- still answers on headers, captions and empty panel space.
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton
		&& bHasTarget.Get(true)
		&& ContextAnchor.IsValid())
	{
		ContextAnchor->OpenAt(MouseEvent.GetScreenSpacePosition());
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
