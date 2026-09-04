#include "UI/Menus/MixtormatMenuBuilder.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatPalette.h"
#include "Style/MixtormatStyle.h"
#include "UI/Containers/SMixtormatMenuPanel.h"
#include "UI/Menus/SMixtormatMenuItem.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace MixtormatMenu
{
	FBuilder::FEntry& FBuilder::LastRow()
	{
		// Modifiers only ever follow the row they modify, so a builder used out of order is a
		// programming error rather than something to silently absorb.
		check(!Entries.IsEmpty() && Entries.Last().Kind == EKind::Row);
		return Entries.Last();
	}

	FBuilder& FBuilder::Caption(const FText& Text)
	{
		FEntry Entry;
		Entry.Kind = EKind::Caption;
		Entry.Label = Text;
		Entries.Add(MoveTemp(Entry));
		return *this;
	}

	FBuilder& FBuilder::Separator()
	{
		// A separator before anything, or two in a row, would draw a line against the panel's edge
		// or against itself. Dropped rather than rendered.
		if (!Entries.IsEmpty() && Entries.Last().Kind != EKind::Separator)
		{
			FEntry Entry;
			Entry.Kind = EKind::Separator;
			Entries.Add(MoveTemp(Entry));
		}
		return *this;
	}

	FBuilder& FBuilder::Item(
		const FText& Label,
		const FSlateBrush* Icon,
		const FSimpleDelegate& OnActivate)
	{
		FEntry Entry;
		Entry.Kind = EKind::Row;
		Entry.Label = Label;
		Entry.Icon = Icon;
		Entry.OnActivate = OnActivate;
		Entries.Add(MoveTemp(Entry));
		return *this;
	}

	FBuilder& FBuilder::SubMenu(
		const FText& Label,
		const FSlateBrush* Icon,
		const FOnGetContent& OnGetSubMenu)
	{
		FEntry Entry;
		Entry.Kind = EKind::Row;
		Entry.Label = Label;
		Entry.Icon = Icon;
		Entry.OnGetSubMenu = OnGetSubMenu;
		Entries.Add(MoveTemp(Entry));
		return *this;
	}

	FBuilder& FBuilder::Shortcut(const FText& Text)
	{
		LastRow().Shortcut = Text;
		return *this;
	}

	FBuilder& FBuilder::Checked(const TAttribute<bool>& InChecked)
	{
		LastRow().bChecked = InChecked;
		return *this;
	}

	FBuilder& FBuilder::Enabled(const TAttribute<bool>& InEnabled)
	{
		LastRow().bEnabled = InEnabled;
		return *this;
	}

	FBuilder& FBuilder::Destructive()
	{
		LastRow().bDestructive = true;
		return *this;
	}

	FBuilder& FBuilder::Widget(const TSharedRef<SWidget>& InWidget)
	{
		FEntry Entry;
		Entry.Kind = EKind::Widget;
		Entry.Content = InWidget;
		Entries.Add(MoveTemp(Entry));
		return *this;
	}

	TSharedRef<SWidget> FBuilder::Build() const
	{
		const ISlateStyle& Style = FMixtormatStyle::Get();
		TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);

		for (const FEntry& Entry : Entries)
		{
			switch (Entry.Kind)
			{
			case EKind::Caption:
				Rows->AddSlot()
				.AutoHeight()
				.Padding(FMargin(
					MixtormatTokens::MenuItemInset,
					MixtormatTokens::MenuCaptionInsetAbove,
					MixtormatTokens::MenuItemInset,
					MixtormatTokens::MenuCaptionInsetBelow))
				[
					SNew(STextBlock)
					.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.MenuCaption")))
					.Text(Entry.Label)
				];
				break;

			case EKind::Separator:
				Rows->AddSlot()
				.AutoHeight()
				.Padding(0.0f, MixtormatTokens::MenuSeparatorMargin)
				[
					SNew(SBox)
					.HeightOverride(MixtormatTokens::HairlineThickness)
					[
						SNew(SImage).Image(Style.GetBrush(TEXT("Mixtormat.MenuSeparator")))
					]
				];
				break;

			case EKind::Widget:
				Rows->AddSlot().AutoHeight()[Entry.Content.ToSharedRef()];
				break;

			default:
				Rows->AddSlot()
				.AutoHeight()
				[
					SNew(SMixtormatMenuItem)
					.Label(Entry.Label)
					.Shortcut(Entry.Shortcut)
					.Icon(Entry.Icon)
					.bChecked(Entry.bChecked)
					.bEnabled(Entry.bEnabled)
					.bDestructive(Entry.bDestructive)
					.OnActivate(Entry.OnActivate)
					.OnGetSubMenu(Entry.OnGetSubMenu)
				];
				break;
			}
		}

		return SNew(SMixtormatMenuPanel)
			.MinWidth(MixtormatTokens::MenuWidth)
			[
				Rows
			];
	}
}
