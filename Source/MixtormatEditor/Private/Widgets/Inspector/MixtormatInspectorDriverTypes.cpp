// Copyright 2026 Hugo Beyer. All Rights Reserved.
#include "Widgets/SMixtormat.h"
#include "MixtormatGroupControls.h"
#include "MixtormatParameterDrivers.h"
#include "Style/MixtormatStyle.h"
#include "UI/Controls/SMixtormatScalarRamp.h"
#include "UI/Menus/MixtormatMenuBuilder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "SMixtormat"
TSharedRef<SWidget> SMixtormat::BuildDriverTypedControls(FMixtormatParameterAddress Target)
{
	const auto CanEdit = [this, Target]() { return MixtormatParameterDrivers::Capability({WorkingLayers, WorkingLayerGroups}, Target).IsSupported(); };
	const TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	Rows->AddSlot().AutoHeight()[MixtormatRow::MakeTrailing(LOCTEXT("DriverTransferEnabled", "Scalar Transfer Curve"), MixtormatRow::MakeCheckbox(
		TAttribute<ECheckBoxState>::CreateLambda([this, Target]() { const auto* B = FindParameterBinding(Target); return B && B->Driver.bUseTransferCurve ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }),
		FOnCheckStateChanged::CreateLambda([this, Target, CanEdit](ECheckBoxState State)
		{
			if (!CanEdit()) { return; }
			BeginGroupControlEdit();
			if (auto* B = FindParameterBinding(Target, true)) { B->Driver.bUseTransferCurve = State == ECheckBoxState::Checked; }
			EndGroupControlEdit();
		})))];
	const auto* Binding = FindParameterBinding(Target);
	const FMixtormatScalarRamp Initial = Binding ? Binding->Driver.TransferCurve : FMixtormatScalarRamp();
	Rows->AddSlot().AutoHeight()[SNew(SBox).IsEnabled_Lambda([this, Target, CanEdit]() { const auto* B = FindParameterBinding(Target); return CanEdit() && B && B->Driver.bUseTransferCurve; })
	[
		SNew(SMixtormatScalarRamp).CanonicalXMin(Initial.DomainMin).CanonicalXMax(Initial.DomainMax)
		.ExtendedYMin(FMixtormatScalarRamp::MinOutput).ExtendedYMax(FMixtormatScalarRamp::MaxOutput)
		.Ramp_Lambda([this, Target]() { const auto* B = FindParameterBinding(Target); return B ? B->Driver.TransferCurve : FMixtormatScalarRamp(); })
		.OnBeginInteractiveEdit_Lambda([this, CanEdit]() { if (CanEdit()) { BeginGroupControlEdit(); } })
		.OnEndInteractiveEdit_Lambda([this]() { if (bGroupControlInteractiveEdit) { EndGroupControlEdit(); } })
		.OnChanged_Lambda([this, Target, CanEdit](const FMixtormatScalarRamp& Ramp)
		{
			if (!CanEdit()) { return; }
			if (auto* B = FindParameterBinding(Target, true)) { B->Driver.TransferCurve = Ramp; RefreshLayeredPreview(); }
		})
	]];
	Rows->AddSlot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("DriverTransferChainHelp", "Signal → Invert → Input window → Curve → Output mapping → Combine. Extended curve Y values are retained. Curve off preserves the original float chain."))
		.AutoWrapText(true).TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.RowLabel")))];
	if (Target.ValueType == EMixtormatParameterValueType::Int)
	{
		Rows->AddSlot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("IntegerDriverRule", "Nearest rounding after numeric mapping/combine (ties toward +infinity), then clamped to 1–64 Flow Steps. It changes the per-pixel loop bound, not CPU pass counts."))
			.AutoWrapText(true).TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.RowLabel")))];
	}
	if (Target.ValueType == EMixtormatParameterValueType::Enum)
	{
		Rows->AddSlot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("EnumDriverRule", "Explicit upper-inclusive bins on the normalized/curved signal. The last bin ends at 1. No arithmetic/interpolation on enum values; Amount, Combine and numeric Out fields do not apply."))
			.AutoWrapText(true).TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.RowLabel")))];
		const auto Update = [this, Target, CanEdit](TFunction<void(TArray<FMixtormatDriverEnumBin>&)> Edit)
		{
			if (!CanEdit()) { return; }
			auto* B = FindParameterBinding(Target, true); if (!B) { return; }
			auto Candidate = B->Driver.EnumBins;
			Edit(Candidate);
			FText Error;
			if (!MixtormatParameterDrivers::ValidateEnumBins(MixtormatGroupControls::FindEnumType(Target.TypeName), Candidate, Error)) { WorkingStatusText = Error.ToString(); return; }
			const bool bDiscrete = !bGroupControlInteractiveEdit;
			if (bDiscrete) { BeginGroupControlEdit(); }
			B->Driver.EnumBins = MoveTemp(Candidate);
			RefreshLayeredPreview();
			if (bDiscrete) { EndGroupControlEdit(); }
		};
		for (int32 Index = 0; Index < FMixtormatParameterDriver::MaxEnumBins; ++Index)
		{
			Rows->AddSlot().AutoHeight()[SNew(SBox).Visibility_Lambda([this, Target, Index]() { const auto* B = FindParameterBinding(Target); return B && B->Driver.EnumBins.IsValidIndex(Index) ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[SNew(SBox).IsEnabled_Lambda([this, Target, Index]() { const auto* B = FindParameterBinding(Target); return B && Index < B->Driver.EnumBins.Num() - 1; })
				[
					SNew(SMixtormatSlider).Label(FText::Format(LOCTEXT("EnumBinUpper", "Bin {0} ≤"), FText::AsNumber(Index + 1)))
					.Value_Lambda([this, Target, Index]() { const auto* B = FindParameterBinding(Target); return B && B->Driver.EnumBins.IsValidIndex(Index) ? B->Driver.EnumBins[Index].UpperBound : 1.0; })
					.MinValue(0.0).MaxValue(1.0).HardMinValue(0.0).HardMaxValue(1.0)
					.OnBeginDrag(this, &SMixtormat::BeginGroupControlEdit).OnEndDrag(this, &SMixtormat::EndGroupControlEdit)
					.OnValueChanged_Lambda([Update, Index](double Number) { Update([Index, Number](auto& Bins) { if (Bins.IsValidIndex(Index)) { Bins[Index].UpperBound = static_cast<float>(Number); } }); })
				]]
				+ SVerticalBox::Slot().AutoHeight()[MixtormatRow::MakeChip(
					TAttribute<FText>::CreateLambda([this, Target, Index]()
					{
						const auto* B = FindParameterBinding(Target); const UEnum* Enum = MixtormatGroupControls::FindEnumType(Target.TypeName);
						return Enum && B && B->Driver.EnumBins.IsValidIndex(Index) ? Enum->GetDisplayNameTextByValue(B->Driver.EnumBins[Index].Value) : LOCTEXT("EnumBinMissing", "Missing enum/bin");
					}), FOnGetContent::CreateLambda([this, Target, Index, Update]()
					{
						MixtormatMenu::FBuilder Menu;
						const UEnum* Enum = MixtormatGroupControls::FindEnumType(Target.TypeName);
						if (Enum)
						{
							for (int32 Entry = 0; Entry < Enum->NumEnums(); ++Entry)
							{
								if (Enum->HasMetaData(TEXT("Hidden"), Entry) || Enum->HasMetaData(TEXT("Deprecated"), Entry) || Enum->GetNameStringByIndex(Entry).EndsWith(TEXT("_MAX"))) { continue; }
								const int64 Value = Enum->GetValueByIndex(Entry);
								Menu.Item(Enum->GetDisplayNameTextByIndex(Entry), nullptr, FSimpleDelegate::CreateLambda([Update, Index, Value]() { Update([Index, Value](auto& Bins) { if (Bins.IsValidIndex(Index)) { Bins[Index].Value = Value; } }); }));
							}
						}
						Menu.Item(LOCTEXT("RemoveEnumBin", "Remove Bin"), nullptr, FSimpleDelegate::CreateLambda([Update, Index]()
						{ Update([Index](auto& Bins) { if (Bins.Num() > 1 && Bins.IsValidIndex(Index)) { Bins.RemoveAt(Index); Bins.Last().UpperBound = 1.0f; } }); }))
						.Enabled(TAttribute<bool>::CreateLambda([this, Target]() { const auto* B = FindParameterBinding(Target); return B && B->Driver.EnumBins.Num() > 1; }));
						return Menu.Build();
					}))]
			]];
		}
		Rows->AddSlot().AutoHeight()[MixtormatRow::MakeChip(TAttribute<FText>(LOCTEXT("AddEnumBin", "Add Enum Bin…")), FOnGetContent::CreateLambda([this, Target, Update]()
		{
			MixtormatMenu::FBuilder Menu;
			Menu.Item(LOCTEXT("AddEnumBinAction", "Split Last Interval / Add First Bin"), nullptr, FSimpleDelegate::CreateLambda([this, Target, Update]()
			{
				FMixtormatParameterValue Base;
				if (!MixtormatParameterBinding::TryReadLocalValue({WorkingLayers, WorkingLayerGroups}, Target, Base)) { return; }
				Update([Base](auto& Bins)
				{
					if (Bins.Num() >= FMixtormatParameterDriver::MaxEnumBins) { return; }
					FMixtormatDriverEnumBin Added; Added.Value = Bins.IsEmpty() ? Base.EnumValue : Bins.Last().Value;
					if (!Bins.IsEmpty()) { const float Lower = Bins.Num() > 1 ? Bins[Bins.Num() - 2].UpperBound : 0.0f; Bins.Last().UpperBound = (Lower + 1.0f) * 0.5f; }
					Bins.Add(Added);
				});
			})).Enabled(TAttribute<bool>::CreateLambda([this, Target]() { const auto* B = FindParameterBinding(Target); return !B || B->Driver.EnumBins.Num() < FMixtormatParameterDriver::MaxEnumBins; }));
			return Menu.Build();
		}))];
	}
	Rows->AddSlot().AutoHeight()[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this, Target]()
	{
		const auto* B = FindParameterBinding(Target); FText Error;
		if (!B) { return FText::GetEmpty(); }
		if (!MixtormatParameterDrivers::ValidateTransfer(B->Driver, Error)) { return Error; }
		if (Target.ValueType == EMixtormatParameterValueType::Enum) { MixtormatParameterDrivers::ValidateEnumBins(MixtormatGroupControls::FindEnumType(Target.TypeName), B->Driver.EnumBins, Error); }
		return Error;
	}).TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.RowLabel")))];
	return Rows;
}
#undef LOCTEXT_NAMESPACE
