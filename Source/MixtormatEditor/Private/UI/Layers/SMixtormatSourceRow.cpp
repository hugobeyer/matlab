// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Layers/SMixtormatSourceRow.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatRecipes.h"
#include "Style/MixtormatStyle.h"
#include "Style/MixtormatThemeStore.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "UI/Primitives/SMixtormatSurfaceBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "UI/Controls/SMixtormatTextFieldGradient.h"
#include "UI/Controls/MixtormatEntryCommit.h"
#include "Framework/Application/SlateApplication.h"
#include "UI/Layers/SMixtormatLayerIcon.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void SMixtormatSourceRow::Construct(const FArguments& InArgs)
{
	Enabled = InArgs._bEnabled;
	OnSelected = InArgs._OnSelected;
	OnNameCommitted = InArgs._OnNameCommitted;
	OnToggleEnabled = InArgs._OnToggleEnabled;
	EditableName = InArgs._Name;
	bHasContextMenu = InArgs._OnGetContextMenu.IsBound();
	const ISlateStyle& Style = FMixtormatStyle::Get();
	const FTextBlockStyle NameStyle = Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.LayerName"));
	const FTextBlockStyle KindStyle = Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.LayerSource"));
	const float GlyphSize = FMixtormatThemeStore::GetResolved().Icons.Roles[
		static_cast<uint8>(Mixtormat::EMixtormatIconRole::LayerVisToggle)].GlyphSize;

	ChildSlot
	[
		// Assigned, not just constructed: OpenContextMenu() toggles this anchor, so an unassigned
		// pointer would make every right click silently do nothing.
		SAssignNew(ContextAnchor, SMenuAnchor)
		.Placement(MenuPlacement_MenuRight)
		.OnGetMenuContent(InArgs._OnGetContextMenu.IsBound()
			? InArgs._OnGetContextMenu
			: FOnGetContent())
		[
			SNew(SMixtormatSurfaceBox)
			// Selection/hover share the menu-row recipe instead of a compositing
			// layer surface. Sources are reusable entries, not material layers.
			.Recipe_Lambda([this, Selected = InArgs._bSelected]()
			{
				const auto State = Selected.Get(false)
					? Mixtormat::EMixtormatMenuRowState::Checked
					: bHovered ? Mixtormat::EMixtormatMenuRowState::Hover
					: Mixtormat::EMixtormatMenuRowState::Normal;
				return Mixtormat::MakeMenuRowRecipe(FMixtormatThemeStore::GetTheme(), State);
			})
			.InheritWidgetStyle(true)
			[
				SNew(SBox)
				.HeightOverride(FMixtormatThemeStore::GetResolved().LayerLayout.SourcesRowHeight)
				.Padding(FMargin(
					MixtormatTokens::LayerRowInsetLeading, 0.0f,
					MixtormatTokens::LayerRowInsetTrailing, 0.0f))
				.VAlign(VAlign_Center)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, MixtormatTokens::LayerNameInset, 0.0f)
					[
						SNew(SBox)
						.WidthOverride(GlyphSize)
						.HeightOverride(GlyphSize)
						[
							SNew(SImage)
							.Image(MixtormatIcons::Generator())
							// A disabled source keeps its row so it can be re-enabled from here;
							// only the weight of the row changes.
							.ColorAndOpacity_Lambda([this]()
							{
								const FLinearColor Color = FMixtormatThemeStore::GetResolved()
									.Palette.Get(Mixtormat::EMixtormatColorRole::Text);
								return FSlateColor(Enabled.Get(true)
									? Color
									: FLinearColor(Color.R, Color.G, Color.B, Color.A * 0.4f));
							})
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, MixtormatTokens::LayerNameInset, 0.0f)
					[
						SAssignNew(NameSwitcher, SWidgetSwitcher)
						+ SWidgetSwitcher::Slot()
						[
SNew(STextBlock)
									.Text(InArgs._Name)
									.TextStyle(&NameStyle)
									.ColorAndOpacity_Lambda([this]()
									{
										return FSlateColor(FMixtormatThemeStore::GetResolved().Palette.Get(
											Enabled.Get(true)
												? Mixtormat::EMixtormatColorRole::Text
												: Mixtormat::EMixtormatColorRole::TextMuted));
									})
						]
						+ SWidgetSwitcher::Slot()
						[
							SNew(SMixtormatTextFieldGradient)
							[
								SAssignNew(NameEditBox, SEditableTextBox)
								.Style(&FMixtormatStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>(TEXT("Mixtormat.TextField")))
								.OnKeyDownHandler_Lambda([this](const FGeometry& Geometry, const FKeyEvent& Event)
								{ return NameEntry.IsValid() ? NameEntry->HandleKeyDown(Geometry, Event) : FReply::Unhandled(); })
								.SelectAllTextWhenFocused(true)
								.ClearKeyboardFocusOnCommit(true)
								.OnTextCommitted(this, &SMixtormatSourceRow::HandleNameCommitted)
							]
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(InArgs._Kind)
						.TextStyle(&KindStyle)
					]
				]
			]
		]
	];
}

FReply SMixtormatSourceRow::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		OnSelected.ExecuteIfBound();
		return FReply::Handled();
	}
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		return OpenContextMenu();
	}
	return FReply::Unhandled();
}

void SMixtormatSourceRow::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);
	bHovered = true;
}

void SMixtormatSourceRow::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseLeave(MouseEvent);
	bHovered = false;
}

FReply SMixtormatSourceRow::OpenContextMenu()
{
	if (!ContextAnchor.IsValid() || !bHasContextMenu)
	{
		return FReply::Unhandled();
	}
	ContextAnchor->SetIsOpen(true);
	return FReply::Handled();
}

void SMixtormatSourceRow::BeginRename()
{
	if (!NameSwitcher.IsValid() || !NameEditBox.IsValid()) { return; }
	NameEditBox->SetText(EditableName.Get(FText::GetEmpty()));
	NameSwitcher->SetActiveWidgetIndex(1);
	FSlateApplication::Get().SetKeyboardFocus(NameEditBox, EFocusCause::SetDirectly);
	if (!NameEntry.IsValid()) { NameEntry = MakeShared<FMixtormatEntryCommit>(); }
	const TWeakPtr<SMixtormatSourceRow> WeakSelf = StaticCastSharedRef<SMixtormatSourceRow>(AsShared());
	NameEntry->Begin(NameEditBox.ToSharedRef(), [WeakSelf]()
	{
		if (const TSharedPtr<SMixtormatSourceRow> Self = WeakSelf.Pin())
		{
			if (Self->NameSwitcher.IsValid()) { Self->NameSwitcher->SetActiveWidgetIndex(0); }
		}
	});
}

void SMixtormatSourceRow::HandleNameCommitted(const FText& Text, ETextCommit::Type CommitType)
{
	const bool bCancelled = NameEntry.IsValid() && NameEntry->Finish();
	if (NameSwitcher.IsValid()) { NameSwitcher->SetActiveWidgetIndex(0); }
	if (!bCancelled) { OnNameCommitted.ExecuteIfBound(Text, CommitType); }
}

void SMixtormatSourceRow::ToggleEnabled()
{
	OnToggleEnabled.ExecuteIfBound();
}
