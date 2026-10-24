// Copyright 2026 Hugo Beyer. All Rights Reserved.
#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"
#include "MixtormatSurfacingTypes.h"
#include "UI/Controls/SMixtormatColorRamp.h"
#include "UI/Parameters/MixtormatParameterUiMeta.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

FMixtormatStone* SMixtormat::GetSelectedStone()
{
	return const_cast<FMixtormatStone*>(static_cast<const SMixtormat*>(this)->GetSelectedStone());
}

const FMixtormatStone* SMixtormat::GetSelectedStone() const
{
	const FMixtormatLayerChild* Child = ResolveChildAt(GetSelectedChildAddress());
	return Child && Child->Type == EMixtormatLayerChildType::Surfacing
		&& Child->Surfacing.Type == EMixtormatSurfacingType::Stone ? &Child->Surfacing.Stone : nullptr;
}

TSharedRef<SWidget> SMixtormat::BuildStoneControls()
{
	const auto Stone = [this]() { return GetSelectedStone(); };
	const FMixtormatStone Defaults;
	const auto Property = [](FName Name)
	{
		const FProperty* P = FMixtormatStone::StaticStruct()->FindPropertyByName(Name);
		check(P);
		return P;
	};
	const auto Scalar = [this, Stone, Property](FName Name, float FMixtormatStone::* Member)
	{
		FMixtormatParameterUiResolution Meta;
		MixtormatParameterUi::TryResolveUi({EMixtormatParameterOwnerType::Stone, Name, EMixtormatParameterValueType::Float}, Meta);
		return MakeMemberSlider<FMixtormatStone>(Property(Name)->GetDisplayNameText(), Stone, Member,
			Meta.UiMin, Meta.UiMax, Meta.Default, Meta.Snap, Property(Name)->GetToolTipText());
	};
	const auto Toggle = [this, Stone, Property](FName Name, bool FMixtormatStone::* Member)
	{
		return MakeMemberToggle<FMixtormatStone>(Property(Name)->GetDisplayNameText(), Stone, Member,
			LOCTEXT("StoneChannelHint", "When disabled, preserve the incoming channel. Child masks gate all enabled channels together."));
	};
	TSharedRef<SVerticalBox> Cards = SNew(SVerticalBox);
#define STONE_FLOAT(Name) Scalar(GET_MEMBER_NAME_CHECKED(FMixtormatStone, Name), &FMixtormatStone::Name)
#define STONE_WRITE(Name) Toggle(GET_MEMBER_NAME_CHECKED(FMixtormatStone, Name), &FMixtormatStone::Name)
#define STONE_ENUM(Name, Type) MakeMemberEnum<FMixtormatStone, Type>(Property(TEXT(#Name))->GetDisplayNameText(), Stone, &FMixtormatStone::Name, Property(TEXT(#Name))->GetToolTipText())
	const auto Surface = AddCard(Cards, LOCTEXT("StoneSurfaceCard", "STONE · RELATIVE UNITS"));
	AddSliderRow(Surface, STONE_ENUM(Family, EMixtormatStoneFamily));
	AddSliderRow(Surface, STONE_ENUM(Pattern, EMixtormatStonePattern));
	AddSliderRow(Surface, MixtormatRow::MakePair(STONE_FLOAT(Amount), STONE_FLOAT(HeightScale)));
	AddSliderRow(Surface, MakeMemberSliderInt<FMixtormatStone>(Property(TEXT("Seed"))->GetDisplayNameText(), Stone,
		&FMixtormatStone::Seed, 0, 9999, Defaults.Seed));
	AddSliderRow(Surface, MixtormatRow::MakePair(STONE_FLOAT(Scale), STONE_FLOAT(Direction)));
	AddSliderRow(Surface, MixtormatRow::MakePair(STONE_FLOAT(GrainSize), STONE_FLOAT(CellScale)));
	const auto Veins = AddCard(Cards, LOCTEXT("StoneVeinsCard", "VEINS & BANDS"));
	AddSliderRow(Veins, MixtormatRow::MakePair(STONE_FLOAT(VeinFreq), STONE_FLOAT(VeinWidth)));
	AddSliderRow(Veins, MixtormatRow::MakePair(STONE_FLOAT(Sharpness), STONE_FLOAT(Secondary)));
	AddSliderRow(Veins, MixtormatRow::MakePair(STONE_FLOAT(Distort), STONE_FLOAT(Grain)));
	AddSliderRow(Veins, MixtormatRow::MakePair(STONE_FLOAT(WarpFreq), STONE_FLOAT(WarpPower)));
	AddSliderRow(Veins, MixtormatRow::MakePair(STONE_FLOAT(StripeFreq), STONE_FLOAT(StripeSoft)));
	AddSliderRow(Veins, STONE_FLOAT(StripeTone));
	const auto Cells = AddCard(Cards, LOCTEXT("StoneCellsCard", "MINERAL CELLS"));
	AddSliderRow(Cells, MixtormatRow::MakePair(STONE_FLOAT(CellJitter), STONE_FLOAT(CellWarp)));
	AddSliderRow(Cells, MixtormatRow::MakePair(STONE_FLOAT(EdgeWidth), STONE_FLOAT(EdgeDarken)));
	const auto Material = AddCard(Cards, LOCTEXT("StoneMaterialCard", "FINISH & PALETTE"));
	AddSliderRow(Material, STONE_ENUM(Finish, EMixtormatStoneFinish));
	AddSliderRow(Material, MixtormatRow::MakePair(STONE_FLOAT(FinishAmount), STONE_FLOAT(Roughness)));
	AddSliderRow(Material, STONE_FLOAT(Alteration));
	const TSharedRef<SWidget> Palette = SNew(SMixtormatColorRamp)
		.IsEnabled_Lambda([this, Stone]() { return Stone() && !IsSelectedChildInstance()
			&& !GroupControlValueFor(Stone(), FMixtormatStone::StaticStruct(), TEXT("ColorRamp")); })
		.Ramp_Lambda([this, Stone, Defaults]()
		{
			if (const auto* V = GroupControlValueFor(Stone(), FMixtormatStone::StaticStruct(), TEXT("ColorRamp")))
			{ return V->ColorRampValue; }
			return Stone() ? Stone()->ColorRamp : Defaults.ColorRamp;
		})
		.DomainMin(0).DomainMax(1)
		.OnChanged_Lambda([this, Stone](const FMixtormatColorRamp& Ramp)
		{
			if (auto* S = Stone(); S && !IsSelectedChildInstance()
				&& !GroupControlValueFor(S, FMixtormatStone::StaticStruct(), TEXT("ColorRamp")))
			{ S->ColorRamp = Ramp; RefreshLayeredPreview(); }
		})
		.OnBeginInteractiveEdit_Lambda([this]() { bInteractiveEdit = true; })
		.OnEndInteractiveEdit_Lambda([this]() { RefreshLayeredPreview(); });
	AddSliderRow(Material, WrapParameterControl(Palette, MakeAddressResolver<FMixtormatStone>(Stone, &FMixtormatStone::ColorRamp)));
	const auto Channels = AddCard(Cards, LOCTEXT("StoneChannelsCard", "CHANNELS"));
	AddSliderRow(Channels, MixtormatRow::MakePair(STONE_WRITE(bWriteHeight), STONE_WRITE(bWriteColor)));
	AddSliderRow(Channels, MixtormatRow::MakePair(STONE_WRITE(bWriteRoughness), STONE_WRITE(bWriteMaterial)));
#undef STONE_ENUM
#undef STONE_WRITE
#undef STONE_FLOAT
	return SNew(SBox).Visibility_Lambda([Stone]() { return Stone() ? EVisibility::Visible : EVisibility::Collapsed; })[
		SNew(SMixtormatInspectorGroup)
		.Title(LOCTEXT("StoneHeading", "SURFACING · STONE"))
		.PresetAction(MakeModulePresetButton()).InitiallyExpanded(true)
		.HeaderAction(MakeChildOutputPreviewButton(GetChildPreviewOutputSet([]()
		{
			FMixtormatLayerChild Probe; Probe.Type = EMixtormatLayerChildType::Surfacing;
			Probe.Surfacing.Type = EMixtormatSurfacingType::Stone; return Probe;
		}())))
		[SNew(SBox).IsEnabled_Lambda([this]() { return !IsSelectedChildInstance(); })[Cards]]];
}

#undef LOCTEXT_NAMESPACE
