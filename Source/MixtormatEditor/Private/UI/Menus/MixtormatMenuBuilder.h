#pragma once

#include "CoreMinimal.h"
#include "Framework/SlateDelegates.h"

struct FSlateBrush;

// Declaring a popover, instead of assembling one.
//
// Replaces FMenuBuilder for this tool's menus. FMenuBuilder gives submenus and keyboard navigation
// for free, but every surface it draws comes from a brush, and the design's menu is a three-stop
// gradient with a lit top edge whose rows hover with the same two-axis fill a slider uses. None of
// that is expressible as a brush, so the menus were the one place still wearing Unreal's chrome.
//
// The vocabulary is small on purpose -- caption, item, submenu, separator -- because the canvas
// only has those four, and a builder that can express more than the design does is a builder that
// will be used to express more than the design does.
//
//   MixtormatMenu::FBuilder Menu;
//   Menu.Caption(LOCTEXT("Add", "Add"))
//       .SubMenu(LOCTEXT("Mask", "Mask"), MaskIcon, FOnGetContent::CreateSP(...))
//       .Separator()
//       .Item(LOCTEXT("Delete", "Delete"), nullptr, Action).Destructive().Shortcut(Del);
//   return Menu.Build();
namespace MixtormatMenu
{
	class FBuilder
	{
	public:
		// A section label. Not a row: it cannot be hovered or activated.
		FBuilder& Caption(const FText& Text);

		FBuilder& Separator();

		FBuilder& Item(
			const FText& Label,
			const FSlateBrush* Icon,
			const FSimpleDelegate& OnActivate);

		FBuilder& SubMenu(
			const FText& Label,
			const FSlateBrush* Icon,
			const FOnGetContent& OnGetSubMenu);

		// Modifiers, applied to the row added last. Chained after Item so a row reads in one line
		// rather than through a nine-argument call where every other argument is a default.
		FBuilder& Shortcut(const FText& Text);
		FBuilder& Checked(const TAttribute<bool>& InChecked);
		FBuilder& Enabled(const TAttribute<bool>& InEnabled);
		FBuilder& Destructive();

		// Anything that is not a row: a mask grid, a colour picker. It gets the panel's ground and
		// nothing else -- no hover, no inset -- because it brings its own.
		FBuilder& Widget(const TSharedRef<SWidget>& InWidget);

		bool IsEmpty() const { return Entries.IsEmpty(); }

		TSharedRef<SWidget> Build() const;

	private:
		enum class EKind : uint8 { Caption, Separator, Row, Widget };

		struct FEntry
		{
			EKind Kind = EKind::Row;
			FText Label;
			FText Shortcut;
			const FSlateBrush* Icon = nullptr;
			FSimpleDelegate OnActivate;
			FOnGetContent OnGetSubMenu;
			TAttribute<bool> bChecked = false;
			TAttribute<bool> bEnabled = true;
			bool bDestructive = false;
			TSharedPtr<SWidget> Content;
		};

		FEntry& LastRow();

		TArray<FEntry> Entries;
	};
}
