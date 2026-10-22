// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Compositing/MixtormatSurfacingGather.h"

#include "MixtormatOutputReference.h"

namespace MixtormatGpuCompositor
{
	void GatherSurfacingChild(FLayerRenderData& Data, const FMixtormatLayer& Layer,
		const FMixtormatLayerChild& LayerChild, const int32 SourceChildIndex,
		const int32 LayerIndex, const TArray<FMixtormatLayer>& EffectiveLayers)
	{
		if (!Layer.bEnabled || LayerChild.Type != EMixtormatLayerChildType::Surfacing
			|| !MixtormatOutputReferences::IsSurfacingSourceAvailable(EffectiveLayers, LayerIndex, SourceChildIndex))
		{
			return;
		}

		FChildRenderData& Child = Data.Children.AddDefaulted_GetRef();
		Child.Type = EMixtormatLayerChildType::Surfacing;
		Child.SourceChildIndex = SourceChildIndex;
		Child.ScopeOwnerSourceChildIndex = MixtormatChildScope::ResolveOwnerIndex(Layer.Children, SourceChildIndex);
		Child.Surfacing = LayerChild.Surfacing;
		FMixtormatSkin& Skin = Child.Surfacing.Skin;
		const FMixtormatSkin Defaults;

#define SANITIZE_SKIN_FLOAT(Member) Skin.Member = MixtormatParameterContracts::SanitizeFloat( \
		EMixtormatParameterOwnerType::Surfacing, GET_MEMBER_NAME_CHECKED(FMixtormatSkin, Member), Skin.Member)
#define SANITIZE_SKIN_INT(Member) Skin.Member = MixtormatParameterContracts::SanitizeInt32( \
		EMixtormatParameterOwnerType::Surfacing, GET_MEMBER_NAME_CHECKED(FMixtormatSkin, Member), Skin.Member)
		SANITIZE_SKIN_INT(Seed);
		SANITIZE_SKIN_INT(TissueCells);
		SANITIZE_SKIN_INT(FoldCells);
		SANITIZE_SKIN_INT(DetailCells);
		SANITIZE_SKIN_INT(PoreCells);
		SANITIZE_SKIN_INT(DetailTiling);
		SANITIZE_SKIN_INT(SolverIterations);
		SANITIZE_SKIN_INT(SolverResolution);
		SANITIZE_SKIN_INT(WarpLayers);
		SANITIZE_SKIN_FLOAT(Bending);
		SANITIZE_SKIN_FLOAT(Support);
		SANITIZE_SKIN_FLOAT(Amount);
		SANITIZE_SKIN_FLOAT(HeightScale);
		SANITIZE_SKIN_FLOAT(Fullness);
		SANITIZE_SKIN_FLOAT(Softness);
		SANITIZE_SKIN_FLOAT(Compression);
		SANITIZE_SKIN_FLOAT(Tension);
		SANITIZE_SKIN_FLOAT(FoldDepth);
		SANITIZE_SKIN_FLOAT(FoldWidth);
		SANITIZE_SKIN_FLOAT(Direction);
		SANITIZE_SKIN_FLOAT(Crossing);
		SANITIZE_SKIN_FLOAT(TissueCoupling);
		SANITIZE_SKIN_FLOAT(MarbleDrift);
		SANITIZE_SKIN_FLOAT(DirectionVariation);
		SANITIZE_SKIN_FLOAT(Elongation);
		SANITIZE_SKIN_FLOAT(Cellular);
		SANITIZE_SKIN_FLOAT(CurvatureRadius);
		SANITIZE_SKIN_FLOAT(CurvatureStrength);
		SANITIZE_SKIN_FLOAT(PoreDepth);
		SANITIZE_SKIN_FLOAT(PoreVariation);
		SANITIZE_SKIN_FLOAT(Grain);
		SANITIZE_SKIN_FLOAT(PigmentVariation);
		SANITIZE_SKIN_FLOAT(ThicknessVariation);
		SANITIZE_SKIN_FLOAT(VascularAmount);
		SANITIZE_SKIN_FLOAT(CavityAO);
		SANITIZE_SKIN_FLOAT(CavityRoughness);
		SANITIZE_SKIN_FLOAT(IOR);
		SANITIZE_SKIN_FLOAT(FuzzAmount);
		SANITIZE_SKIN_FLOAT(FuzzRoughness);
		SANITIZE_SKIN_FLOAT(SSSAmount);
#undef SANITIZE_SKIN_INT
#undef SANITIZE_SKIN_FLOAT

		const auto FiniteNonNegative = [](float Value, float Default)
		{
			return FMath::IsFinite(Value) ? FMath::Max(Value, 0.0f) : Default;
		};
		const auto SanitizePhysicalRGB = [&FiniteNonNegative](FLinearColor& Value, const FLinearColor& Default)
		{
			Value.R = FiniteNonNegative(Value.R, Default.R);
			Value.G = FiniteNonNegative(Value.G, Default.G);
			Value.B = FiniteNonNegative(Value.B, Default.B);
			Value.A = Default.A;
		};
		SanitizePhysicalRGB(Skin.FuzzColor, Defaults.FuzzColor);
		SanitizePhysicalRGB(Skin.MeanFreePath, Defaults.MeanFreePath);
		// Use the shared ramp topology/interpolation sanitizer; first remove non-finite colors.
		for (FMixtormatColorRampStop& Stop : Skin.AlbedoRamp.Stops)
		{
			SanitizePhysicalRGB(Stop.Color, Defaults.AlbedoRamp.Stops[0].Color);
			Stop.X = FMath::IsFinite(Stop.X) ? FMath::Clamp(Stop.X, 0.0f, 1.0f) : 0.0f;
		}
		Skin.AlbedoRamp.Sanitize();
		Skin.RoughnessRamp.DomainMin = Defaults.RoughnessRamp.DomainMin;
		Skin.RoughnessRamp.DomainMax = Defaults.RoughnessRamp.DomainMax;
		Skin.RoughnessRamp.Sanitize();
		// No settings-only node cache: this producer depends on the completed incoming substrate.
		// The reflected layer-prefix hash already includes payload, ramps, flags, scope and sources.
	}
}
