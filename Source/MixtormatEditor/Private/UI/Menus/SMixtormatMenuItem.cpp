// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Menus/SMixtormatMenuItem.h"

#include "Framework/Application/SlateApplication.h"
#include "Style/MixtormatRecipes.h"
#include "Style/MixtormatThemeStore.h"
#include "Style/MixtormatStyle.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "UI/Primitives/SMixtormatSurfaceBox.h"
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
		.Padding(0.0f, 0.0f, FMixtormatThemeStore::GetResolved().MenuLayout.ItemGap, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(FMixtormatThemeStore::GetResolved().Icons.Roles[static_cast<uint8>(Mixtormat::EMixtormatIconRole::Menu)].GlyphSize)
			.HeightOverride(FMixtormatThemeStore::GetResolved().Icons.Roles[static_cast<uint8>(Mixtormat::EMixtormatIconRole::Menu)].GlyphSize)
			[
				SNew(SImage)
				.Image_Lambda([this, Icon]()
				{
					return bChecked.Get(false) ? MixtormatIcons::Check() : Icon;
				})
				.ColorAndOpacity(this, &SMixtormatMenuItem::GetIconColor)
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
		.Padding(FMixtormatThemeStore::GetResolved().MenuLayout.ItemGap, 0.0f, 0.0f, 0.0f)
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
		.Padding(FMixtormatThemeStore::GetResolved().MenuLayout.ItemGap, 0.0f, 0.0f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(FMixtormatThemeStore::GetResolved().MenuLayout.ChevronSize)
			.HeightOverride(FMixtormatThemeStore::GetResolved().MenuLayout.ChevronSize)
			[
				SNew(SImage)
				.Image(MixtormatIcons::ChevronRight())
				.ColorAndOpacity(this, &SMixtormatMenuItem::GetIconColor)
			]
		];
	}

	TSharedRef<SWidget> Surface =
		SNew(SMixtormatSurfaceBox)
		.Recipe_Lambda([this]()
		{
			const Mixtormat::EMixtormatMenuRowState State = !IsRowEnabled()
				? Mixtormat::EMixtormatMenuRowState::Disabled
				: bDestructive
					? (IsHovered() ? Mixtormat::EMixtormatMenuRowState::DestructiveHover : Mixtormat::EMixtormatMenuRowState::Destructive)
					: bChecked.Get(false)
						? Mixtormat::EMixtormatMenuRowState::Checked
						: IsHovered()
							? Mixtormat::EMixtormatMenuRowState::Hover
							: Mixtormat::EMixtormatMenuRowState::Normal;
			return Mixtormat::MakeMenuRowRecipe(FMixtormatThemeStore::GetTheme(), State);
		})
		[
			SNew(SBox)
			.HeightOverride(FMixtormatThemeStore::GetResolved().MenuLayout.RowHeight)
			.Padding(FMargin(FMixtormatThemeStore::GetResolved().MenuLayout.ItemInset, 0.0f))
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
			.Method(EPopupMethod::CreateNewWindow)
			.UseApplicationMenuStack(true)
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


FSlateColor SMixtormatMenuItem::GetLabelColor() const
{
	if (!IsRowEnabled())
	{
		return FSlateColor(FMixtormatThemeStore::GetResolved().Menus.ItemDisabled);
	}
	if (bChecked.Get(false) && !bDestructive)
	{
		return FSlateColor(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Accent));
	}
	return FSlateColor(bDestructive
		? FMixtormatThemeStore::GetResolved().Menus.DestructiveText
		: FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Text));
}

FSlateColor SMixtormatMenuItem::GetIconColor() const
{
	FLinearColor Color = bDestructive
		? FMixtormatThemeStore::GetResolved().Menus.DestructiveText
		: bChecked.Get(false)
			? FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Accent)
			: FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Text);
	Color.A *= FMixtormatThemeStore::GetResolved().Icons.Roles[static_cast<uint8>(Mixtormat::EMixtormatIconRole::Menu)].RestOpacity;
	if (!IsRowEnabled())
	{
		Color.A *= FMixtormatThemeStore::GetResolved().Menus.ItemDisabled.A;
	}
	return FSlateColor(Color);
}

FCursorReply SMixtormatMenuItem::OnCursorQuery(const FGeometry&, const FPointerEvent&) const
{
	return IsRowEnabled() ? FCursorReply::Cursor(EMouseCursor::Hand) : FCursorReply::Unhandled();
}


FReply SMixtormatMenuItem::OnMouseButtonUp(const FGeometry&, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !IsRowEnabled())
	{
		return FReply::Unhandled();
	}

	// A submenu row is a destination, not an action. Give it a separate popup window so large
	// galleries keep their requested size instead of being clipped by the 190px parent menu.
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
