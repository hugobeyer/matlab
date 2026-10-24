// Copyright 2026 Hugo Beyer. All Rights Reserved.
#include "UI/Parameters/SMixtormatGroupControlPopover.h"

#include "Framework/Application/SlateApplication.h"
#include "MixtormatGroupControls.h"
#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatStyle.h"
#include "Style/MixtormatThemeStore.h"
#include "UI/Controls/SMixtormatColorRamp.h"
#include "UI/Controls/SMixtormatScalarRamp.h"
#include "UI/Controls/SMixtormatSlider.h"
#include "UI/Controls/SMixtormatTextFieldGradient.h"
#include "UI/Menus/MixtormatMenuBuilder.h"
#include "UI/Parameters/SMixtormatDriverPopover.h"
#include "UI/Parameters/MixtormatParameterPresentation.h"
#include "UI/Rows/SMixtormatRow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

void SMixtormatGroupControlPopover::Construct(const FArguments& InArgs)
{
	Draft = InArgs._Control;
	bEditing = InArgs._bEditing;
	bTypeLocked = InArgs._bTypeLocked;
	bLabelOnly = InArgs._bLabelOnly;
	OnCommit = InArgs._OnCommit;

	const TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	Rows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::RowGap)
	[
		MixtormatParameterPresentation::MakeRow(LOCTEXT("GroupControlLabelField", "Label"),
			SNew(SMixtormatTextFieldGradient)
			[
				SNew(SEditableTextBox)
				.Style(&FMixtormatStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>(TEXT("Mixtormat.TextField")))
				.Text(Draft.Label)
				.HintText(LOCTEXT("GroupControlLabelHint", "Parameter label"))
				.SelectAllTextWhenFocused(true)
				.OnTextChanged_Lambda([this](const FText& Text) { Draft.Label = Text; Error = FText::GetEmpty(); })
			])
	];
	if (!bLabelOnly)
	{
		Rows->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::RowGap)
		[
			MixtormatParameterPresentation::MakeRow(LOCTEXT("GroupControlTypeField", "Type"),
				SNew(SBox).IsEnabled(!bTypeLocked)
				[
					MixtormatRow::MakeChip(TAttribute<FText>::CreateLambda([this]()
					{
						return StaticEnum<EMixtormatParameterValueType>()->GetDisplayNameTextByValue(static_cast<int64>(Draft.Value.Type));
					}), FOnGetContent::CreateSP(this, &SMixtormatGroupControlPopover::BuildTypeMenu))
				])
		];
		Rows->AddSlot().AutoHeight()[SAssignNew(ValueFields, SVerticalBox)];
		RebuildValueFields();
		Rows->AddSlot().AutoHeight().Padding(0.0f, MixtormatTokens::RowGap, 0.0f, 0.0f)
		[
			SNew(STextBlock).AutoWrapText(true)
			.Text(LOCTEXT("GroupControlConnectHint", "Connect from the parameter itself: right-click a parameter inside this group → Assign to Group Control."))
			.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.RowLabel")))
			.OverflowPolicy(ETextOverflowPolicy::Clip)
		];
	}
	Rows->AddSlot().AutoHeight()
	[
		SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]() { return Error; })
		.Visibility_Lambda([this]() { return Error.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
		.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.RowLabel")))
		.OverflowPolicy(ETextOverflowPolicy::Clip)
		.WrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping)
		.ColorAndOpacity(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Error))
	];
	Rows->AddSlot().AutoHeight().Padding(0.0f, MixtormatTokens::DialogActionsTopMargin, 0.0f, 0.0f)
	[
		SNew(SBox).MinDesiredHeight(MixtormatTokens::ButtonHeight)
		[
			MixtormatRow::MakePair(
				SNew(SButton)
				.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.CompactRowButton")))
				.OnClicked(this, &SMixtormatGroupControlPopover::Cancel)
				[
					SNew(STextBlock).Text(LOCTEXT("GroupControlCancel", "Cancel"))
					.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.RowLabel")))
					.AutoWrapText(true).OverflowPolicy(ETextOverflowPolicy::Clip)
				],
				SNew(SButton)
				.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.PrimaryButton")))
				.IsEnabled_Lambda([this]() { return !Draft.Label.ToString().TrimStartAndEnd().IsEmpty(); })
				.OnClicked(this, &SMixtormatGroupControlPopover::Commit)
				[
					SNew(STextBlock).Text(bEditing ? LOCTEXT("GroupControlApply", "Apply") : LOCTEXT("GroupControlCreate", "Create Control"))
					.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.RowLabel")))
					.AutoWrapText(true).OverflowPolicy(ETextOverflowPolicy::Clip)
				])
		]
	];
	ChildSlot
	[
		SNew(SMixtormatDriverPopover)
		.Width(MixtormatTokens::GroupControlPopoverWidth)
		.Title(bLabelOnly ? LOCTEXT("GroupControlRenameTitle", "Edit Label")
			: bEditing ? LOCTEXT("GroupControlEditTitle", "Control Settings") : LOCTEXT("GroupControlCreateTitle", "Create Group Control"))
		[Rows]
	];
}

TSharedRef<SWidget> SMixtormatGroupControlPopover::BuildTypeMenu()
{
	MixtormatMenu::FBuilder Menu;
	for (EMixtormatParameterValueType Type : {EMixtormatParameterValueType::Float, EMixtormatParameterValueType::Int,
		EMixtormatParameterValueType::Enum, EMixtormatParameterValueType::ScalarRamp, EMixtormatParameterValueType::ColorRamp})
	{
		Menu.Item(StaticEnum<EMixtormatParameterValueType>()->GetDisplayNameTextByValue(static_cast<int64>(Type)), nullptr,
			FSimpleDelegate::CreateSP(this, &SMixtormatGroupControlPopover::ChangeType, Type))
			.Checked(Draft.Value.Type == Type)
			.Enabled(Type != EMixtormatParameterValueType::Enum)
			.ToolTip(Type == EMixtormatParameterValueType::Enum
				? LOCTEXT("GroupEnumFromParameterHint", "Right-click an enum parameter → Assign to Group Control → New Control. Its declared choices are copied automatically.")
				: FText::GetEmpty());
	}
	return Menu.Build();
}

void SMixtormatGroupControlPopover::ChangeType(EMixtormatParameterValueType Type)
{
	if (bTypeLocked || Draft.Value.Type == Type || Type == EMixtormatParameterValueType::Enum) { return; }
	Draft.Value.Type = Type;
	Draft.DefaultValue.Type = Type;
	Error = FText::GetEmpty();
	RebuildValueFields();
}

void SMixtormatGroupControlPopover::RebuildValueFields()
{
	if (!ValueFields) { return; }
	ValueFields->ClearChildren();
	const auto AddRow = [this](const TSharedRef<SWidget>& Widget)
	{
		ValueFields->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::RowGap)[Widget];
	};
	const bool bInteger = Draft.Value.Type == EMixtormatParameterValueType::Int;
	if (bInteger || Draft.Value.Type == EMixtormatParameterValueType::Float)
	{
		const auto RangeField = [this, bInteger](const FText& Label, float FMixtormatGroupControl::* Member)
		{
			return SNew(SMixtormatSlider).Label(Label).bInteger(bInteger).KeepPopupOpenOnCommit(true)
				.Value_Lambda([this, Member]() { return static_cast<double>(Draft.*Member); })
				.MinValue_Lambda([this]() { return static_cast<double>(Draft.UiMin); })
				.MaxValue_Lambda([this]() { return static_cast<double>(Draft.UiMax); })
				.HardMinValue(bInteger ? static_cast<double>(MIN_int32) : -UE_BIG_NUMBER)
				.HardMaxValue(bInteger ? static_cast<double>(MAX_int32) : UE_BIG_NUMBER)
				.OnValueChanged_Lambda([this, Member](double Number) { Draft.*Member = static_cast<float>(Number); Error = FText::GetEmpty(); });
		};
		AddRow(MixtormatRow::MakePair(RangeField(LOCTEXT("GroupControlMinimum", "Min"), &FMixtormatGroupControl::UiMin),
			RangeField(LOCTEXT("GroupControlMaximum", "Max"), &FMixtormatGroupControl::UiMax)));
		AddRow(SNew(SMixtormatSlider).Label(LOCTEXT("GroupControlDefault", "Default")).bInteger(bInteger).KeepPopupOpenOnCommit(true)
			.Value_Lambda([this, bInteger]() { return bInteger ? static_cast<double>(Draft.DefaultValue.IntValue) : static_cast<double>(Draft.DefaultValue.FloatValue); })
			.MinValue_Lambda([this]() { return static_cast<double>(Draft.UiMin); })
			.MaxValue_Lambda([this]() { return static_cast<double>(Draft.UiMax); })
			.HardMinValue(bInteger ? static_cast<double>(MIN_int32) : -UE_BIG_NUMBER)
			.HardMaxValue(bInteger ? static_cast<double>(MAX_int32) : UE_BIG_NUMBER)
			.OnValueChanged_Lambda([this, bInteger](double Number)
			{
				if (!FMath::IsFinite(Number)) { return; }
				if (bInteger) { Draft.DefaultValue.IntValue = static_cast<int32>(FMath::RoundToDouble(FMath::Clamp(Number, static_cast<double>(MIN_int32), static_cast<double>(MAX_int32)))); }
				else { Draft.DefaultValue.FloatValue = static_cast<float>(Number); }
				Error = FText::GetEmpty();
			}));
		AddRow(SNew(STextBlock).AutoWrapText(true)
			.Text(LOCTEXT("GroupControlRangeHint", "Min / Max set the scrub range. Typed values still use the target's hard limits. Default is used by Reset."))
			.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.RowLabel")))
			.OverflowPolicy(ETextOverflowPolicy::Clip));
	}
	else if (Draft.Value.Type == EMixtormatParameterValueType::Enum)
	{
		AddRow(MixtormatParameterPresentation::MakeRow(LOCTEXT("GroupControlDefault", "Default"),
			MixtormatRow::MakeChip(TAttribute<FText>::CreateLambda([this]()
			{
				const UEnum* Enum = MixtormatGroupControls::FindEnumType(Draft.Value.TypeName);
				return Enum ? Enum->GetDisplayNameTextByValue(Draft.DefaultValue.EnumValue) : FText::GetEmpty();
			}), FOnGetContent::CreateLambda([this]()
			{
				MixtormatMenu::FBuilder Menu;
				if (const UEnum* Enum = MixtormatGroupControls::FindEnumType(Draft.Value.TypeName))
				{
					for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
					{
						if (Enum->HasMetaData(TEXT("Hidden"), Index) || Enum->HasMetaData(TEXT("Deprecated"), Index) || Enum->GetNameStringByIndex(Index).EndsWith(TEXT("_MAX"))) { continue; }
						const int64 Number = Enum->GetValueByIndex(Index);
						Menu.Item(Enum->GetDisplayNameTextByIndex(Index), nullptr, FSimpleDelegate::CreateLambda([this, Number]() { Draft.DefaultValue.EnumValue = Number; }));
					}
				}
				return Menu.Build();
			}))));
	}
	else if (Draft.Value.Type == EMixtormatParameterValueType::ScalarRamp)
	{
		AddRow(SNew(SMixtormatScalarRamp)
			.Ramp_Lambda([this]() { return Draft.DefaultValue.ScalarRampValue; })
			.CanonicalXMin(Draft.DefaultValue.ScalarRampValue.DomainMin).CanonicalXMax(Draft.DefaultValue.ScalarRampValue.DomainMax)
			.ExtendedYMin(FMixtormatScalarRamp::MinOutput).ExtendedYMax(FMixtormatScalarRamp::MaxOutput)
			.OnChanged_Lambda([this](const FMixtormatScalarRamp& Ramp) { Draft.DefaultValue.ScalarRampValue = Ramp; }));
	}
	else if (Draft.Value.Type == EMixtormatParameterValueType::ColorRamp)
	{
		AddRow(SNew(SMixtormatColorRamp).DomainMin(Draft.ColorDomainMin).DomainMax(Draft.ColorDomainMax)
			.Ramp_Lambda([this]() { return Draft.DefaultValue.ColorRampValue; })
			.OnChanged_Lambda([this](const FMixtormatColorRamp& Ramp) { Draft.DefaultValue.ColorRampValue = Ramp; }));
	}
}

FReply SMixtormatGroupControlPopover::Commit()
{
	Draft.Label = FText::FromString(Draft.Label.ToString().TrimStartAndEnd());
	Error = FText::GetEmpty();
	if (!bEditing) { Draft.Value = Draft.DefaultValue; }
	if (Draft.Label.IsEmpty()) { Error = LOCTEXT("GroupControlLabelRequired", "Enter a label."); }
	else if (!bLabelOnly && (Draft.Value.Type == EMixtormatParameterValueType::Float || Draft.Value.Type == EMixtormatParameterValueType::Int) && Draft.UiMin >= Draft.UiMax)
	{ Error = LOCTEXT("GroupControlRangeInvalid", "Min must be less than Max."); }
	else if ((bLabelOnly || MixtormatGroupControls::ValidateControl(Draft, Error)) && OnCommit.IsBound() && OnCommit.Execute(Draft, Error))
	{
		if (const TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(AsShared())) { Window->RequestDestroyWindow(); }
	}
	return FReply::Handled();
}

FReply SMixtormatGroupControlPopover::Cancel()
{
	if (const TSharedPtr<SWindow> Window = FSlateApplication::Get().FindWidgetWindow(AsShared())) { Window->RequestDestroyWindow(); }
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
