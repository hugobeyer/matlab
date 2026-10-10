// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"
#include "Style/MixtormatStyle.h"
#include "Style/MixtormatThemeStore.h"
#include "UI/Containers/SMixtormatInspectorGroup.h"
#include "UI/Rows/SMixtormatRow.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

// The selected Sources shelf entry. The generator kind's own parameter panel opens beneath this
// card through the same GetSelectedGenerator() resolver the layer stack uses, so a source's
// parameters are edited exactly where a layer generator's are.
TSharedRef<SWidget> SMixtormat::BuildSourcesPanel()
{
	const auto Source = [this]() -> FMixtormatSourceEntry*
	{
		return GetSelectedSource();
	};

	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);

	// The shelf label. Committed on Enter or focus loss; a blank commit keeps the old name.
	Body->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.RowGap)
	[
		MixtormatRow::MakeDropdown(
			LOCTEXT("SourceNameLabel", "Name"),
			SNew(SEditableTextBox)
			.Text_Lambda([Source]()
			{
				const FMixtormatSourceEntry* Entry = Source();
				return Entry ? Entry->DisplayName : FText::GetEmpty();
			})
			.OnTextCommitted(this, &SMixtormat::HandleSourceNameCommitted))
	];

	// The generator kind, read-only here: the kind's parameters are the panel below.
	Body->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.RowGap)
	[
		MixtormatRow::Make(
			LOCTEXT("SourceKindLabel", "Kind"),
			SNew(STextBlock)
			.Text_Lambda([Source]()
			{
				const FMixtormatSourceEntry* Entry = Source();
				return Entry
					? StaticEnum<EMixtormatGeneratorType>()->GetDisplayNameTextByValue(
						static_cast<int64>(Entry->Child.Generator.Type))
					: FText::GetEmpty();
			})
			.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.LayerSource"))))
	];

	// Enabled gates the source's published outputs for every consumer at once. There is no
	// separate contribution flag: a source never composites into the material.
	Body->AddSlot().AutoHeight()
	[
		MixtormatRow::MakeTrailing(
			LOCTEXT("SourceEnabledLabel", "Enabled"),
			MixtormatRow::MakeCheckbox(
				TAttribute<ECheckBoxState>::CreateLambda([Source]()
				{
					const FMixtormatSourceEntry* Entry = Source();
					return Entry && Entry->Child.Generator.bEnabled
						? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				}),
				FOnCheckStateChanged::CreateLambda([this, Source](const ECheckBoxState State)
				{
					if (FMixtormatSourceEntry* Entry = Source())
					{
						Entry->Child.Generator.bEnabled = State == ECheckBoxState::Checked;
						RecordEditHistory();
						bIsWorkingMaterialDirty = !IsCurrentStateSaved();
						WorkingStatusText = bIsWorkingMaterialDirty
							? TEXT("Unsaved changes") : TEXT("All changes saved");
					}
				}),
				LOCTEXT("SourceEnabledHint", "Gate this source's published outputs for every consumer at once.")))
	];

	Body->AddSlot().AutoHeight().Padding(0.0f, FMixtormatThemeStore::GetResolved().ControlLayout.RowGap, 0.0f, 0.0f)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("SourceScopeNote", "A source publishes fields for other operations to consume; it never composites into the material. Connections are not authored yet."))
		.AutoWrapText(true)
		.ColorAndOpacity(FSlateColor::UseSubduedForeground())
	];

	return SNew(SMixtormatInspectorGroup)
		.Visibility_Lambda([this]() { return GetSelectedSource() ? EVisibility::Visible : EVisibility::Collapsed; })
		.Title(LOCTEXT("SourceHeading", "SOURCE"))
		.InitiallyExpanded(true)
		[
			Body
		];
}

#undef LOCTEXT_NAMESPACE
