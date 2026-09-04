#include "UI/Menus/SMixtormatMenuItem.h"

#include "Framework/Application/SlateApplication.h"
#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatPalette.h"
#include "Style/MixtormatStyle.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "UI/Primitives/SMixtormatGradientBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void SMixtormatMenuItem::Construct(const FArguments& InArgs)
{
	bChecked = InArgs._bChecked;
	bRowEnabled = InArgs._bEnabled;
	bDestructive = InArgs._bDestructive;
	OnActivate = InArgs._OnActivate;

	const ISlateStyle& Style = FMixtormatStyle::Get();
	const FSlateBrush* const Icon = InArgs._Icon;
	const bool bHasSubMenu = InArgs._OnGetSubMenu.IsBound();

	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)

		// The gutter. Present whether or not this row has anything to put in it.
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, MixtormatTokens::MenuItemGap, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(MixtormatTokens::MenuIconSize)
			.HeightOverride(MixtormatTokens::MenuIconSize)
			[
				SNew(SImage)
				.Image_Lambda([this, Icon]()
				{
					return bChecked.Get(false) ? MixtormatIcons::Check() : Icon;
				})
				.ColorAndOpacity(this, &SMixtormatMenuItem::GetLabelColor)
			]
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.MenuLabel")))
			.ColorAndOpacity(this, &SMixtormatMenuItem::GetLabelColor)
			.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			.Text(InArgs._Label)
		];

	if (InArgs._Shortcut.IsSet())
	{
		Row->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(MixtormatTokens::MenuItemGap, 0.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.MenuShortcut")))
			.Text(InArgs._Shortcut)
		];
	}

	if (bHasSubMenu)
	{
		Row->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(MixtormatTokens::MenuItemGap, 0.0f, 0.0f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(MixtormatTokens::ChevronSize)
			.HeightOverride(MixtormatTokens::ChevronSize)
			[
				SNew(SImage)
				.Image(MixtormatIcons::ChevronRight())
				.ColorAndOpacity(this, &SMixtormatMenuItem::GetLabelColor)
			]
		];
	}

	TSharedRef<SWidget> Surface =
		SNew(SMixtormatGradientBox)
		.StartColor(this, &SMixtormatMenuItem::GetFillTop)
		.EndColor(this, &SMixtormatMenuItem::GetFillBottom)
		.MultiplyStart(this, &SMixtormatMenuItem::GetShadeStart)
		.MultiplyMid(this, &SMixtormatMenuItem::GetShadeMid)
		.MultiplyMidPosition(MixtormatTokens::MultiplyMidPosition)
		.MultiplyEnd(this, &SMixtormatMenuItem::GetShadeEnd)
		.Orientation(Orient_Vertical)
		.CornerRadius(MixtormatTokens::CornerRadius)
		[
			SNew(SBox)
			.HeightOverride(MixtormatTokens::MenuItemHeight)
			.Padding(FMargin(MixtormatTokens::MenuItemInset, 0.0f))
			[
				Row
			]
		];

	if (bHasSubMenu)
	{
		ChildSlot
		[
			SAssignNew(SubMenuAnchor, SMenuAnchor)
			.Placement(MenuPlacement_MenuRight)
			.OnGetMenuContent(InArgs._OnGetSubMenu)
			[
				Surface
			]
		];
		return;
	}

	ChildSlot[Surface];
}

bool SMixtormatMenuItem::IsRowEnabled() const
{
	return bRowEnabled.Get(true);
}

FLinearColor SMixtormatMenuItem::GetFillTop() const
{
	if (!IsHovered() || !IsRowEnabled())
	{
		return FLinearColor::Transparent;
	}
	return bDestructive ? MixtormatPalette::DestructiveTop() : MixtormatPalette::FillTop();
}

FLinearColor SMixtormatMenuItem::GetFillBottom() const
{
	if (!IsHovered() || !IsRowEnabled())
	{
		return FLinearColor::Transparent;
	}
	return bDestructive ? MixtormatPalette::DestructiveBottom() : MixtormatPalette::FillBottom();
}

// The shade only exists while there is a fill under it to shade.
FLinearColor SMixtormatMenuItem::GetShadeStart() const
{
	return IsHovered() && IsRowEnabled() ? MixtormatPalette::MultiplyStart() : FLinearColor::Transparent;
}

FLinearColor SMixtormatMenuItem::GetShadeMid() const
{
	return IsHovered() && IsRowEnabled() ? MixtormatPalette::MultiplyMid() : FLinearColor::Transparent;
}

FLinearColor SMixtormatMenuItem::GetShadeEnd() const
{
	return IsHovered() && IsRowEnabled() ? MixtormatPalette::MultiplyEnd() : FLinearColor::Transparent;
}

FSlateColor SMixtormatMenuItem::GetLabelColor() const
{
	if (!IsRowEnabled())
	{
		return FSlateColor(MixtormatPalette::DisabledText());
	}
	if (IsHovered())
	{
		// White on both fills: the destructive row is already saying it in red at rest.
		return FSlateColor(FLinearColor::White);
	}
	return FSlateColor(bDestructive
		? MixtormatPalette::Destructive()
		: MixtormatPalette::RowText());
}

FCursorReply SMixtormatMenuItem::OnCursorQuery(const FGeometry&, const FPointerEvent&) const
{
	return IsRowEnabled() ? FCursorReply::Cursor(EMouseCursor::Hand) : FCursorReply::Unhandled();
}

void SMixtormatMenuItem::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);

	// Hover opens a submenu, which is what Unreal does everywhere else -- a menu that needs a
	// click to descend feels stuck when you are already moving toward the thing you want.
	if (SubMenuAnchor.IsValid() && IsRowEnabled() && !SubMenuAnchor->IsOpen())
	{
		SubMenuAnchor->SetIsOpen(true);
	}
}

FReply SMixtormatMenuItem::OnMouseButtonUp(const FGeometry&, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !IsRowEnabled())
	{
		return FReply::Unhandled();
	}

	// A submenu row is a destination, not an action: clicking it opens rather than commits, and it
	// must not take the menu down with it.
	if (SubMenuAnchor.IsValid())
	{
		SubMenuAnchor->SetIsOpen(true);
		return FReply::Handled();
	}

	// Dismiss before acting. Several of these actions rebuild the stack the menu was opened from,
	// and tearing that down underneath a live menu is how a popup outlives the row it belongs to.
	FSlateApplication::Get().DismissAllMenus();
	OnActivate.ExecuteIfBound();
	return FReply::Handled();
}
