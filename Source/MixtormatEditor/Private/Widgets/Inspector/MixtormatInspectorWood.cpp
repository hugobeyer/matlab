// Copyright 2026 Hugo Beyer. All Rights Reserved.
#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"
#include "MixtormatSurfacingTypes.h"
#include "UI/Controls/SMixtormatColorRamp.h"
#include "UI/Parameters/MixtormatParameterUiMeta.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

FMixtormatWood* SMixtormat::GetSelectedWood()
{
	return const_cast<FMixtormatWood*>(static_cast<const SMixtormat*>(this)->GetSelectedWood());
}

const FMixtormatWood* SMixtormat::GetSelectedWood() const
{
	const FMixtormatLayerChild* Child = ResolveChildAt(GetSelectedChildAddress());
	return Child && Child->Type == EMixtormatLayerChildType::Surfacing
		&& Child->Surfacing.Type == EMixtormatSurfacingType::Wood ? &Child->Surfacing.Wood : nullptr;
}

TSharedRef<SWidget> SMixtormat::BuildWoodControls()
{
	const auto Wood = [this]() { return GetSelectedWood(); };
	const FMixtormatWood Defaults;
	const auto Property = [](FName Name)
	{
		const FProperty* P = FMixtormatWood::StaticStruct()->FindPropertyByName(Name);
		check(P);
		return P;
	};
	const auto Scalar = [this, Wood, Property](FName Name, float FMixtormatWood::* Member)
	{
		FMixtormatParameterUiResolution Meta;
		MixtormatParameterUi::TryResolveUi({EMixtormatParameterOwnerType::Wood, Name, EMixtormatParameterValueType::Float}, Meta);
		return MakeMemberSlider<FMixtormatWood>(Property(Name)->GetDisplayNameText(), Wood, Member,
			Meta.UiMin, Meta.UiMax, Meta.Default, Meta.Snap, Property(Name)->GetToolTipText());
	};
	const auto Toggle = [this, Wood, Property](FName Name, bool FMixtormatWood::* Member)
	{
		return MakeMemberToggle<FMixtormatWood>(Property(Name)->GetDisplayNameText(), Wood, Member,
			LOCTEXT("WoodChannelHint", "When disabled, preserve the incoming channel. Child masks gate all enabled channels together."));
	};
	TSharedRef<SVerticalBox> Cards = SNew(SVerticalBox);
#define WOOD_FLOAT(Name) Scalar(GET_MEMBER_NAME_CHECKED(FMixtormatWood, Name), &FMixtormatWood::Name)
#define WOOD_WRITE(Name) Toggle(GET_MEMBER_NAME_CHECKED(FMixtormatWood, Name), &FMixtormatWood::Name)
#define WOOD_ENUM(Name, Type) MakeMemberEnum<FMixtormatWood, Type>(Property(TEXT(#Name))->GetDisplayNameText(), Wood, &FMixtormatWood::Name, Property(TEXT(#Name))->GetToolTipText())
	const auto Surface = AddCard(Cards, LOCTEXT("WoodSurfaceCard", "WOOD · RELATIVE UNITS"));
	AddSliderRow(Surface, WOOD_ENUM(Mode, EMixtormatWoodMode));
	AddSliderRow(Surface, MixtormatRow::MakePair(WOOD_FLOAT(Amount), WOOD_FLOAT(HeightScale)));
	FMixtormatParameterUiResolution SeedMeta;
	MixtormatParameterUi::TryResolveUi({EMixtormatParameterOwnerType::Wood, TEXT("Seed"), EMixtormatParameterValueType::Int}, SeedMeta);
	AddSliderRow(Surface, MakeMemberSliderInt<FMixtormatWood>(Property(TEXT("Seed"))->GetDisplayNameText(), Wood,
		&FMixtormatWood::Seed, SeedMeta.UiMin, SeedMeta.UiMax, static_cast<int32>(SeedMeta.Default)));

	// Keep anatomy controls visible, but unavailable for a finish-only pass.
	TSharedRef<SVerticalBox> AnatomyCards = SNew(SVerticalBox);
	Cards->AddSlot().AutoHeight()[SNew(SBox)
		.IsEnabled_Lambda([this, Wood]()
		{
			if (const auto* V = GroupControlValueFor(Wood(), FMixtormatWood::StaticStruct(), TEXT("Mode")))
			{ return V->EnumValue == static_cast<int64>(EMixtormatWoodMode::FullWood); }
			return Wood() && Wood()->Mode == EMixtormatWoodMode::FullWood;
		})[AnatomyCards]];
	const auto Anatomy = AddCard(AnatomyCards, LOCTEXT("WoodAnatomyCard", "SPECIES & CUT"));
	AddSliderRow(Anatomy, WOOD_ENUM(Species, EMixtormatWoodSpecies));
	AddSliderRow(Anatomy, WOOD_ENUM(Cut, EMixtormatWoodCut));
	AddSliderRow(Anatomy, MixtormatRow::MakePair(WOOD_FLOAT(PithOffset), WOOD_FLOAT(CutTilt)));
	AddSliderRow(Anatomy, MixtormatRow::MakePair(WOOD_FLOAT(RingScale), WOOD_FLOAT(RingWobble)));
	AddSliderRow(Anatomy, MixtormatRow::MakePair(WOOD_FLOAT(LatewoodRatio), WOOD_FLOAT(LatewoodSharpness)));
	AddSliderRow(Anatomy, MixtormatRow::MakePair(WOOD_FLOAT(PoreDepth), WOOD_FLOAT(PoreDensity)));
	AddSliderRow(Anatomy, MixtormatRow::MakePair(WOOD_FLOAT(RayAmount), WOOD_FLOAT(RayScale)));
	const auto Figure = AddCard(AnatomyCards, LOCTEXT("WoodFigureCard", "FIGURE · INDEPENDENT OF SPECIES"));
	AddSliderRow(Figure, WOOD_ENUM(Figure, EMixtormatWoodFigure));
	AddSliderRow(Figure, MixtormatRow::MakePair(WOOD_FLOAT(FigureAmount), WOOD_FLOAT(FigureScale)));
	AddSliderRow(Figure, WOOD_FLOAT(Chatoyance));
	const auto Prep = AddCard(AnatomyCards, LOCTEXT("WoodPrepCard", "PREPARATION & AGING"));
	AddSliderRow(Prep, WOOD_ENUM(Prep, EMixtormatWoodPrep));
	AddSliderRow(Prep, MixtormatRow::MakePair(WOOD_FLOAT(PrepIntensity), WOOD_FLOAT(Weathering)));
	AddSliderRow(Prep, WOOD_FLOAT(Roughness));
	const TSharedRef<SWidget> Palette = SNew(SMixtormatColorRamp)
		.IsEnabled_Lambda([this, Wood]() { return Wood() && !IsSelectedChildInstance()
			&& !GroupControlValueFor(Wood(), FMixtormatWood::StaticStruct(), TEXT("ColorRamp")); })
		.Ramp_Lambda([this, Wood, Defaults]()
		{
			if (const auto* V = GroupControlValueFor(Wood(), FMixtormatWood::StaticStruct(), TEXT("ColorRamp")))
			{ return V->ColorRampValue; }
			return Wood() ? Wood()->ColorRamp : Defaults.ColorRamp;
		})
		.DomainMin(0).DomainMax(1)
		.OnChanged_Lambda([this, Wood](const FMixtormatColorRamp& Ramp)
		{
			if (auto* W = Wood(); W && !IsSelectedChildInstance()
				&& !GroupControlValueFor(W, FMixtormatWood::StaticStruct(), TEXT("ColorRamp")))
			{ W->ColorRamp = Ramp; RefreshLayeredPreview(); }
		})
		.OnBeginInteractiveEdit_Lambda([this]() { bInteractiveEdit = true; })
		.OnEndInteractiveEdit_Lambda([this]() { RefreshLayeredPreview(); });
	AddSliderRow(Prep, WrapParameterControl(Palette, MakeAddressResolver<FMixtormatWood>(Wood, &FMixtormatWood::ColorRamp)));

	const auto Finish = AddCard(Cards, LOCTEXT("WoodFinishCard", "FINISH · ONE STEP PER MODULE"));
	AddSliderRow(Finish, WOOD_ENUM(Finish, EMixtormatWoodFinish));
	AddSliderRow(Finish, MixtormatRow::MakePair(WOOD_FLOAT(FinishAmount), WOOD_FLOAT(WetOut)));
	AddSliderRow(Finish, WOOD_FLOAT(TintAmount));
	AddSliderRow(Finish, MixtormatRow::MakeTrailing(LOCTEXT("WoodFinishTint", "Tint"),
		WrapParameterControl(SNew(SButton)
		.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.CompactRowButton")))
		.IsEnabled_Lambda([this, Wood]() { return Wood() && !IsSelectedChildInstance()
			&& !GroupControlValueFor(Wood(), FMixtormatWood::StaticStruct(), TEXT("FinishTint")); })
		.OnClicked_Lambda([this, Wood]()
		{
			if (!Wood() || IsSelectedChildInstance()
				|| GroupControlValueFor(Wood(), FMixtormatWood::StaticStruct(), TEXT("FinishTint"))) { return FReply::Handled(); }
			const auto Address = GetSelectedChildAddress();
			const FLinearColor Original = Wood()->FinishTint;
			const TWeakPtr<SMixtormat> WeakThis = SharedThis(this);
			const auto Apply = [WeakThis, Address](FLinearColor Color)
			{
				if (const auto Widget = WeakThis.Pin())
				{
					if (auto* Child = Widget->ResolveChildAt(Address); Child && !Child->IsInstance()
						&& Child->Type == EMixtormatLayerChildType::Surfacing
						&& Child->Surfacing.Type == EMixtormatSurfacingType::Wood
						&& !Widget->GroupControlValueFor(&Child->Surfacing.Wood, FMixtormatWood::StaticStruct(), TEXT("FinishTint")))
					{ Color.A = 1.0f; Child->Surfacing.Wood.FinishTint = Color; Widget->RefreshLayeredPreview(); }
				}
			};
			LastHistoryRecordTime = 0.0;
			FColorPickerArgs Args;
			Args.bUseAlpha = false;
			Args.InitialColor = Original;
			Args.OnColorCommitted = FOnLinearColorValueChanged::CreateLambda(Apply);
			Args.OnColorPickerCancelled = FOnColorPickerCancelled::CreateLambda([Apply, Original](FLinearColor) { Apply(Original); });
			OpenColorPicker(Args);
			return FReply::Handled();
		})[
			SNew(SColorBlock).Size(FVector2D(FMixtormatThemeStore::GetResolved().ControlLayout.IconBrushSize))
			.Color_Lambda([Wood, Defaults]() { return Wood() ? Wood()->FinishTint : Defaults.FinishTint; })
		], MakeAddressResolver<FMixtormatWood>(Wood, &FMixtormatWood::FinishTint)), Property(TEXT("FinishTint"))->GetToolTipText()));
	AddSliderRow(Finish, MixtormatRow::MakePair(WOOD_FLOAT(FilmBuild), WOOD_FLOAT(DepressionFill)));
	AddSliderRow(Finish, MixtormatRow::MakePair(WOOD_FLOAT(Sheen), WOOD_FLOAT(Wear)));
	AddSliderRow(Finish, MixtormatRow::MakePair(WOOD_FLOAT(ApplicationTexture), WOOD_FLOAT(ApplicationAngle)));
	const auto Channels = AddCard(Cards, LOCTEXT("WoodChannelsCard", "CHANNELS"));
	AddSliderRow(Channels, MixtormatRow::MakePair(WOOD_WRITE(bWriteHeight), WOOD_WRITE(bWriteColor)));
	AddSliderRow(Channels, MixtormatRow::MakePair(WOOD_WRITE(bWriteRoughness), WOOD_WRITE(bWriteMaterial)));
#undef WOOD_ENUM
#undef WOOD_WRITE
#undef WOOD_FLOAT
	return SNew(SBox).Visibility_Lambda([Wood]() { return Wood() ? EVisibility::Visible : EVisibility::Collapsed; })[
		SNew(SMixtormatInspectorGroup).Title(LOCTEXT("WoodHeading", "SURFACING · WOOD"))
		.PresetAction(MakeModulePresetButton()).InitiallyExpanded(true)
		.HeaderAction(MakeChildOutputPreviewButton(GetChildPreviewOutputSet([]()
		{
			FMixtormatLayerChild Probe;
			Probe.Type = EMixtormatLayerChildType::Surfacing;
			Probe.Surfacing.Type = EMixtormatSurfacingType::Wood;
			return Probe;
		}())))
		[SNew(SBox).IsEnabled_Lambda([this]() { return !IsSelectedChildInstance(); })[Cards]]];
}

#undef LOCTEXT_NAMESPACE
