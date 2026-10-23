// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"
#include "MixtormatSurfacingTypes.h"
#include "UI/Controls/SMixtormatColorRamp.h"
#include "UI/Controls/SMixtormatScalarRamp.h"
#include "UI/Parameters/MixtormatParameterUiMeta.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

TSharedRef<SWidget> SMixtormat::BuildSkinControls()
{
	const auto Skin = [this]() { return GetSelectedSkin(); };
	const FMixtormatSkin Defaults;
	const auto Property = [](FName Name) -> const FProperty*
	{
		const FProperty* Result = FMixtormatSkin::StaticStruct()->FindPropertyByName(Name);
		check(Result);
		return Result;
	};
	const auto Scalar = [this, Skin, Property](FName Name, float FMixtormatSkin::* Member)
	{
		FMixtormatParameterUiResolution Meta;
		MixtormatParameterUi::TryResolveUi(
			{EMixtormatParameterOwnerType::Surfacing, Name, EMixtormatParameterValueType::Float}, Meta);
		return MakeMemberSlider<FMixtormatSkin>(Property(Name)->GetDisplayNameText(), Skin, Member,
			Meta.UiMin, Meta.UiMax, Meta.Default, Meta.Snap, Property(Name)->GetToolTipText());
	};
	const auto Integer = [this, Skin, Property](FName Name, int32 FMixtormatSkin::* Member)
	{
		FMixtormatParameterUiResolution Meta;
		MixtormatParameterUi::TryResolveUi(
			{EMixtormatParameterOwnerType::Surfacing, Name, EMixtormatParameterValueType::Int}, Meta);
		return MakeMemberSliderInt<FMixtormatSkin>(Property(Name)->GetDisplayNameText(), Skin, Member,
			Meta.UiMin, Meta.UiMax, static_cast<int32>(Meta.Default), Property(Name)->GetToolTipText());
	};
	const auto Toggle = [this, Skin, Property](FName Name, bool FMixtormatSkin::* Member)
	{
		return MakeMemberToggle<FMixtormatSkin>(Property(Name)->GetDisplayNameText(), Skin, Member,
			LOCTEXT("SkinWriteChannelHint", "When disabled, preserve the incoming channel. Child masks gate all enabled channels together."));
	};

	TSharedRef<SVerticalBox> Cards = SNew(SVerticalBox);
#define SKIN_FLOAT(Name) Scalar(GET_MEMBER_NAME_CHECKED(FMixtormatSkin, Name), &FMixtormatSkin::Name)
#define SKIN_INT(Name) Integer(GET_MEMBER_NAME_CHECKED(FMixtormatSkin, Name), &FMixtormatSkin::Name)
#define SKIN_WRITE(Name) Toggle(GET_MEMBER_NAME_CHECKED(FMixtormatSkin, Name), &FMixtormatSkin::Name)
	const auto Surface = AddCard(Cards, LOCTEXT("SkinSurfaceCard", "SURFACE"));
	AddSliderRow(Surface, SKIN_FLOAT(Amount));
		AddSliderRow(Surface, MixtormatRow::MakePair(
			MakeMemberToggle<FMixtormatSkin>(
				Property(GET_MEMBER_NAME_CHECKED(FMixtormatSkin, bAutoScaleHeight))->GetDisplayNameText(),
				Skin, &FMixtormatSkin::bAutoScaleHeight,
				Property(GET_MEMBER_NAME_CHECKED(FMixtormatSkin, bAutoScaleHeight))->GetToolTipText()),
			SKIN_FLOAT(HeightScale)));
	AddSliderRow(Surface, MixtormatRow::MakePair(SKIN_WRITE(bWriteHeight), SKIN_WRITE(bWriteColor)));
	AddSliderRow(Surface, MixtormatRow::MakePair(SKIN_WRITE(bWriteRoughness), SKIN_WRITE(bWriteAO)));
	AddSliderRow(Surface, MixtormatRow::MakePair(SKIN_WRITE(bWriteF0), SKIN_WRITE(bWriteFuzz)));
	AddSliderRow(Surface, SKIN_WRITE(bWriteSSS));

	const auto Tissue = AddCard(Cards, LOCTEXT("SkinTissueCard", "TISSUE"));
	AddSliderRow(Tissue, MixtormatRow::MakePair(SKIN_INT(Seed), SKIN_INT(TissueCells)));
	AddSliderRow(Tissue, MixtormatRow::MakePair(SKIN_FLOAT(Fullness), SKIN_FLOAT(Softness)));

	const auto Solver = AddCard(Cards, LOCTEXT("SkinSolverCard", "FILM RELAXATION"));
	AddSliderRow(Solver, MixtormatRow::MakePair(SKIN_INT(SolverIterations), SKIN_INT(SolverResolution)));
	AddSliderRow(Solver, MixtormatRow::MakePair(SKIN_FLOAT(Bending), SKIN_FLOAT(Support)));

	const auto Folds = AddCard(Cards, LOCTEXT("SkinFoldsCard", "FOLDS & RESPONSE"));
	AddSliderRow(Folds, MixtormatRow::MakePair(SKIN_INT(FoldCells), SKIN_FLOAT(FoldDepth)));
	AddSliderRow(Folds, MixtormatRow::MakePair(SKIN_FLOAT(FoldWidth), SKIN_FLOAT(Direction)));
	AddSliderRow(Folds, MixtormatRow::MakePair(SKIN_FLOAT(Compression), SKIN_FLOAT(Tension)));
	AddSliderRow(Folds, MixtormatRow::MakePair(SKIN_FLOAT(Crossing), SKIN_FLOAT(TissueCoupling)));
	AddSliderRow(Folds, MixtormatRow::MakePair(SKIN_FLOAT(MarbleDrift), SKIN_FLOAT(WarpScale)));
		AddSliderRow(Folds, SKIN_INT(WarpLayers));
	AddSliderRow(Folds, MixtormatRow::MakePair(SKIN_FLOAT(DirectionVariation), SKIN_FLOAT(Elongation)));
	AddSliderRow(Folds, SKIN_FLOAT(Cellular));
	AddSliderRow(Folds, MixtormatRow::MakePair(SKIN_FLOAT(CurvatureRadius), SKIN_FLOAT(CurvatureStrength)));

	const auto Micro = AddCard(Cards, LOCTEXT("SkinMicroCard", "MICRODETAIL"));
	AddSliderRow(Micro, MixtormatRow::MakePair(SKIN_INT(DetailCells), SKIN_INT(PoreCells)));
		AddSliderRow(Micro, SKIN_FLOAT(PoreSize));
	AddSliderRow(Micro, MixtormatRow::MakePair(SKIN_FLOAT(PoreDepth), SKIN_FLOAT(PoreVariation)));
	AddSliderRow(Micro, MixtormatRow::MakePair(SKIN_FLOAT(Grain), SKIN_INT(DetailTiling)));

	const auto Albedo = AddCard(Cards, LOCTEXT("SkinAlbedoCard", "ALBEDO"));
	const TSharedRef<SWidget> AlbedoEditor = SNew(SMixtormatColorRamp)
		.IsEnabled_Lambda([this, Skin]() { return Skin() && !IsSelectedChildInstance()
			&& !GroupControlValueFor(Skin(), FMixtormatSkin::StaticStruct(), TEXT("AlbedoRamp")); })
		.Ramp_Lambda([this, Skin, Defaults]()
		{
			if (const auto* Value = GroupControlValueFor(Skin(), FMixtormatSkin::StaticStruct(), TEXT("AlbedoRamp")))
			{ return Value->ColorRampValue; }
			return Skin() ? Skin()->AlbedoRamp : Defaults.AlbedoRamp;
		})
		.DomainMin(0.0f).DomainMax(1.0f)
		.OnChanged_Lambda([this, Skin](const FMixtormatColorRamp& Ramp)
		{
			if (auto* S = Skin(); S && !IsSelectedChildInstance()
				&& !GroupControlValueFor(S, FMixtormatSkin::StaticStruct(), TEXT("AlbedoRamp")))
			{
				S->AlbedoRamp = Ramp;
				RefreshLayeredPreview();
			}
		})
		.OnBeginInteractiveEdit_Lambda([this]() { bInteractiveEdit = true; })
		.OnEndInteractiveEdit_Lambda([this]() { RefreshLayeredPreview(); });
	AddSliderRow(Albedo, WrapParameterControl(AlbedoEditor,
		MakeAddressResolver<FMixtormatSkin>(Skin, &FMixtormatSkin::AlbedoRamp)));
	AddSliderRow(Albedo, MixtormatRow::MakePair(SKIN_FLOAT(PigmentVariation), SKIN_FLOAT(VascularAmount)));
	AddSliderRow(Albedo, SKIN_FLOAT(ThicknessVariation));

	const auto Roughness = AddCard(Cards, LOCTEXT("SkinRoughnessCard", "ROUGHNESS & CAVITY"));
	const TSharedRef<SWidget> RoughnessEditor = SNew(SMixtormatScalarRamp)
		.IsEnabled_Lambda([this, Skin]() { return Skin() && !IsSelectedChildInstance()
			&& !GroupControlValueFor(Skin(), FMixtormatSkin::StaticStruct(), TEXT("RoughnessRamp")); })
		.Ramp_Lambda([this, Skin, Defaults]()
		{
			if (const auto* Value = GroupControlValueFor(Skin(), FMixtormatSkin::StaticStruct(), TEXT("RoughnessRamp")))
			{ return Value->ScalarRampValue; }
			return Skin() ? Skin()->RoughnessRamp : Defaults.RoughnessRamp;
		})
		.Height(FMixtormatThemeStore::GetResolved().ControlLayout.ScalarRampHeight)
		.CanonicalXMin(0.0f).CanonicalXMax(1.0f)
		.CanonicalYMin(0.0f).CanonicalYMax(1.0f)
		.SoftYMin(0.0f).SoftYMax(1.0f)
		.ExtendedYMin(0.0f).ExtendedYMax(1.0f)
		.OnChanged_Lambda([this, Skin](const FMixtormatScalarRamp& Ramp)
		{
			if (auto* S = Skin(); S && !IsSelectedChildInstance()
				&& !GroupControlValueFor(S, FMixtormatSkin::StaticStruct(), TEXT("RoughnessRamp")))
			{
				S->RoughnessRamp = Ramp;
				RefreshLayeredPreview();
			}
		})
		.OnBeginInteractiveEdit_Lambda([this]() { bInteractiveEdit = true; })
		.OnEndInteractiveEdit_Lambda([this]() { RefreshLayeredPreview(); });
	AddSliderRow(Roughness, WrapParameterControl(RoughnessEditor,
		MakeAddressResolver<FMixtormatSkin>(Skin, &FMixtormatSkin::RoughnessRamp)));
	AddSliderRow(Roughness, MixtormatRow::MakePair(SKIN_FLOAT(CavityRoughness), SKIN_FLOAT(CavityAO)));

	const auto Reflectance = AddCard(Cards, LOCTEXT("SkinReflectanceCard", "REFLECTANCE"));
	AddSliderRow(Reflectance, SKIN_FLOAT(IOR));
	const auto Fuzz = AddCard(Cards, LOCTEXT("SkinFuzzCard", "FUZZ"));
	AddSliderRow(Fuzz, MixtormatRow::MakePair(SKIN_FLOAT(FuzzAmount), SKIN_FLOAT(FuzzRoughness)));
	AddSliderRow(Fuzz, MixtormatRow::MakeTrailing(LOCTEXT("SkinFuzzColor", "Tint"),
		SNew(SButton)
		.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.CompactRowButton")))
		.OnClicked_Lambda([this, Skin]()
		{
			if (!Skin() || IsSelectedChildInstance()) { return FReply::Handled(); }
			const auto Address = GetSelectedChildAddress();
			const auto Original = Skin()->FuzzColor;
			const TWeakPtr<SMixtormat> WeakThis = SharedThis(this);
			const auto Apply = [WeakThis, Address](FLinearColor Color)
			{
				if (const auto Widget = WeakThis.Pin())
				{
					if (auto* Child = Widget->ResolveChildAt(Address); Child && !Child->IsInstance()
						&& Child->Type == EMixtormatLayerChildType::Surfacing)
					{
						Color.A = 1.0f;
						Child->Surfacing.Skin.FuzzColor = Color;
						Widget->RefreshLayeredPreview();
					}
				}
			};
			LastHistoryRecordTime = 0.0;
			FColorPickerArgs Args;
			Args.bUseAlpha = false;
			Args.InitialColor = Original;
			Args.OnColorCommitted = FOnLinearColorValueChanged::CreateLambda(Apply);
			Args.OnColorPickerCancelled = FOnColorPickerCancelled::CreateLambda(
				[Apply, Original](FLinearColor) { Apply(Original); });
			OpenColorPicker(Args);
			return FReply::Handled();
		})
		[
			SNew(SColorBlock)
			.Size(FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize))
			.Color_Lambda([Skin, Defaults]() { return Skin() ? Skin()->FuzzColor : Defaults.FuzzColor; })
		], Property(TEXT("FuzzColor"))->GetToolTipText()));

	const auto Subsurface = AddCard(Cards, LOCTEXT("SkinSubsurfaceCard", "SUBSURFACE"));
	AddSliderRow(Subsurface, SKIN_FLOAT(SSSAmount));
	// Physical RGB distances use numeric controls, never a display-color picker.
	const auto MFP = [this, Skin, Defaults, Property](const FText& Label, float FLinearColor::* Component)
	{
		const FProperty* P = Property(TEXT("MeanFreePath"));
		const double Min = FCString::Atod(*P->GetMetaData(TEXT("UIMin")));
		const double Max = FCString::Atod(*P->GetMetaData(TEXT("UIMax")));
		const double Delta = FCString::Atod(*P->GetMetaData(TEXT("Delta")));
		const double Default = Defaults.MeanFreePath.*Component;
		return MakeSlider(Label,
			TAttribute<double>::CreateLambda([Skin, Component, Default]()
			{ return Skin() ? static_cast<double>(Skin()->MeanFreePath.*Component) : Default; }),
			Min, Max, Default, Delta, false,
			FMixtormatOnSliderValueChanged::CreateLambda([this, Skin, Component](double Value)
			{
				if (auto* S = Skin(); S && !IsSelectedChildInstance() && FMath::IsFinite(Value))
				{
					S->MeanFreePath.*Component = FMath::Max(0.0f, static_cast<float>(Value));
					RefreshLayeredPreview();
				}
			}),
			FSimpleDelegate::CreateLambda([this, Skin, Component, Default]()
			{
				if (auto* S = Skin(); S && !IsSelectedChildInstance())
				{
					S->MeanFreePath.*Component = static_cast<float>(Default);
					RefreshLayeredPreview();
				}
			}), P->GetToolTipText());
	};
	AddSliderRow(Subsurface, MFP(LOCTEXT("SkinMFPRed", "Mean Free Path R (cm)"), &FLinearColor::R));
	AddSliderRow(Subsurface, MFP(LOCTEXT("SkinMFPGreen", "Mean Free Path G (cm)"), &FLinearColor::G));
	AddSliderRow(Subsurface, MFP(LOCTEXT("SkinMFPBlue", "Mean Free Path B (cm)"), &FLinearColor::B));
#undef SKIN_WRITE
#undef SKIN_INT
#undef SKIN_FLOAT

	return SNew(SBox)
		.Visibility_Lambda([Skin]() { return Skin() ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("SkinHeading", "SURFACING · SKIN"))
			.PresetAction(MakeModulePresetButton())
			.InitiallyExpanded(true)
			.HeaderAction(MakeChildOutputPreviewButton(GetChildPreviewOutputSet([]()
			{
				FMixtormatLayerChild Probe;
				Probe.Type = EMixtormatLayerChildType::Surfacing;
				return Probe;
			}())))
			[
				SNew(SBox).IsEnabled_Lambda([this]() { return !IsSelectedChildInstance(); })[Cards]
			]
		];
}

#undef LOCTEXT_NAMESPACE
