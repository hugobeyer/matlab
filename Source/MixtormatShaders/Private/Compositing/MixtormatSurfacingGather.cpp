// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Compositing/MixtormatSurfacingGather.h"
#include "Compositing/MixtormatComposeHash.h"

#include "MixtormatMaskTypes.h"
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
		if (Child.Surfacing.Type == EMixtormatSurfacingType::Moss)
		{
			// Moss moved from GeneratorGather: same sanitization, seed resolution
			// and simulation/field cache keys, now owned by Surfacing.
			FMossRenderData& Out = Child.Moss;
			Out.Settings = Child.Surfacing.Moss;
			FMixtormatMoss& S = Out.Settings;
			const FMixtormatMoss MossDefaults;
			const auto Finite = [](float V, float D) { return FMath::IsFinite(V) ? V : D; };
			S.GrowthIterations = FMath::Max(S.GrowthIterations, 0);
			S.SimulationResolution = FMath::Max(S.SimulationResolution, 1);
			S.Preview = static_cast<EMixtormatMossPreview>(FMath::Clamp(static_cast<int32>(S.Preview),
				static_cast<int32>(EMixtormatMossPreview::Moss), static_cast<int32>(EMixtormatMossPreview::Biomass)));
			S.CushionMode = static_cast<EMixtormatMossCushionMode>(FMath::Clamp(static_cast<int32>(S.CushionMode),
				static_cast<int32>(EMixtormatMossCushionMode::Deposition), static_cast<int32>(EMixtormatMossCushionMode::SdfInflation)));
			S.CavityAttraction = Finite(S.CavityAttraction, MossDefaults.CavityAttraction);
			S.OcclusionInfluence = Finite(S.OcclusionInfluence, MossDefaults.OcclusionInfluence);
			S.Moisture = Finite(S.Moisture, MossDefaults.Moisture);
			S.SurfaceAnglePreference = Finite(S.SurfaceAnglePreference, MossDefaults.SurfaceAnglePreference);
			S.Competition = Finite(S.Competition, MossDefaults.Competition);
			S.ExposureTolerance = Finite(S.ExposureTolerance, MossDefaults.ExposureTolerance);
			S.ColonyDensity = FMath::Max(S.ColonyDensity, 1);
			S.GrowthSpread = Finite(S.GrowthSpread, MossDefaults.GrowthSpread);
			S.Coverage = Finite(S.Coverage, MossDefaults.Coverage);
			S.CushionSize = Finite(S.CushionSize, MossDefaults.CushionSize);
			S.CushionHeight = Finite(S.CushionHeight, MossDefaults.CushionHeight);
			S.MatureHeight = Finite(S.MatureHeight, MossDefaults.MatureHeight);
			S.ClumpDensity = Finite(S.ClumpDensity, MossDefaults.ClumpDensity);
			S.ShootDensity = Finite(S.ShootDensity, MossDefaults.ShootDensity);
			S.ShootLength = Finite(S.ShootLength, MossDefaults.ShootLength);
			S.FiberThickness = Finite(S.FiberThickness, MossDefaults.FiberThickness);
			S.Softness = Finite(S.Softness, MossDefaults.Softness);
			S.Plumpness = Finite(S.Plumpness, MossDefaults.Plumpness);
			S.HeightScale = Finite(S.HeightScale, MossDefaults.HeightScale);
			S.Amount = Finite(S.Amount, MossDefaults.Amount);
			S.DryRoughness = Finite(S.DryRoughness, MossDefaults.DryRoughness);
			S.WetRoughness = Finite(S.WetRoughness, MossDefaults.WetRoughness);
			S.NormalStrength = Finite(S.NormalStrength, MossDefaults.NormalStrength);
			S.GrowthForm = static_cast<EMixtormatMossForm>(FMath::Clamp(static_cast<int32>(S.GrowthForm), 0, 3));
			S.LeafScale = FMath::Clamp(Finite(S.LeafScale, MossDefaults.LeafScale), 1.0f, 8.0f);
			S.Hydration = FMath::Clamp(Finite(S.Hydration, MossDefaults.Hydration), 0.0f, 1.0f);
			S.SurfaceWetness = FMath::Clamp(Finite(S.SurfaceWetness, MossDefaults.SurfaceWetness), 0.0f, 1.0f);
			S.WetDarkening = FMath::Clamp(Finite(S.WetDarkening, MossDefaults.WetDarkening), 0.0f, 1.0f);
			S.ColonyVariation = FMath::Clamp(Finite(S.ColonyVariation, MossDefaults.ColonyVariation), 0.0f, 1.0f);
			S.LeafVariation = FMath::Clamp(Finite(S.LeafVariation, MossDefaults.LeafVariation), 0.0f, 1.0f);
			S.TipColor = FMath::Clamp(Finite(S.TipColor, MossDefaults.TipColor), 0.0f, 1.0f);
			S.Damage = FMath::Clamp(Finite(S.Damage, MossDefaults.Damage), 0.0f, 1.0f);
			S.SeedMaskTiling = FMath::Max(S.SeedMaskTiling, 1);
			S.ColorRamp.Sanitize();
			Child.Surfacing.Moss = S;
			UTexture2D* SeedTexture = S.SeedMaskTexture.LoadSynchronous();
			if (!SeedTexture)
			{
				if (const UMixtormatMask* Mask = S.SeedMask.LoadSynchronous())
				{
					SeedTexture = Mask->MaskTexture.Get();
				}
			}
			if (SeedTexture) { Out.SeedMask = GetTextureRHI(SeedTexture); }
			{
				MixtormatComposeHash::FHasher DependencyHasher;
				DependencyHasher.Value(static_cast<uint64>(LayerIndex));
				DependencyHasher.Value(static_cast<uint64>(SourceChildIndex));
				for (int32 BelowLayerIndex = 0; BelowLayerIndex < LayerIndex; ++BelowLayerIndex)
				{
					DependencyHasher.Struct(FMixtormatLayer::StaticStruct(), &EffectiveLayers[BelowLayerIndex]);
				}
				DependencyHasher.SkipTopLevel = {GET_MEMBER_NAME_CHECKED(FMixtormatLayer, Children)};
				DependencyHasher.Struct(FMixtormatLayer::StaticStruct(), &Layer);
				DependencyHasher.SkipTopLevel.Reset();
				for (int32 ChildIndex = 0; ChildIndex < Layer.Children.Num(); ++ChildIndex)
				{
					if (ChildIndex != SourceChildIndex)
					{
						DependencyHasher.Struct(FMixtormatLayerChild::StaticStruct(), &Layer.Children[ChildIndex]);
					}
				}
				const uint64 DependencyKey = DependencyHasher.Get();
				// Stable procedural colony seeding and shared visible-leaf evaluation.
				MixtormatComposeHash::FHasher FieldHasher;
				FieldHasher.Struct(FMixtormatMoss::StaticStruct(), &S);
				Out.FieldKey = MixtormatComposeHash::Combine(
					MixtormatComposeHash::Combine(FieldHasher.Get(), DependencyKey), 0x4d6f737344657036ull) | 1ull;
				MixtormatComposeHash::FHasher SimulationHasher;
				SimulationHasher.SkipTopLevel = {
					TEXT("HeightScale"), TEXT("bNormalizeHeight"), TEXT("Coverage"), TEXT("CushionMode"), TEXT("CushionSize"),
					TEXT("CushionHeight"), TEXT("MatureHeight"), TEXT("ClumpDensity"), TEXT("ShootDensity"), TEXT("ShootLength"),
					TEXT("FiberThickness"), TEXT("Softness"), TEXT("Plumpness"), TEXT("Preview"),
					TEXT("Amount"), TEXT("bWriteHeight"), TEXT("bWriteColor"), TEXT("bWriteRoughness"), TEXT("bWriteNormal"),
					TEXT("ColorRamp"), TEXT("DryRoughness"), TEXT("WetRoughness"), TEXT("NormalStrength"),
										TEXT("GrowthForm"), TEXT("LeafScale"), TEXT("Hydration"), TEXT("SurfaceWetness"),
										TEXT("WetDarkening"), TEXT("ColonyVariation"), TEXT("LeafVariation"), TEXT("TipColor"), TEXT("Damage")};
				SimulationHasher.Struct(FMixtormatMoss::StaticStruct(), &S);
				Out.SimulationKey = MixtormatComposeHash::Combine(
					MixtormatComposeHash::Combine(SimulationHasher.Get(), DependencyKey), 0x4d6f737344657035ull) | 1ull;
			}
			return;
		}
		if (Child.Surfacing.Type == EMixtormatSurfacingType::Rust)
					{
						FMixtormatRust& R = Child.Surfacing.Rust;
						const FMixtormatRust Defaults;
						const auto Bounded = [](float V, float D, float Min, float Max)
						{ return FMath::Clamp(FMath::IsFinite(V) ? V : D, Min, Max); };
						R.Amount = Bounded(R.Amount, Defaults.Amount, 0, 1);
						R.Coverage = Bounded(R.Coverage, Defaults.Coverage, 0, 1);
						R.Structure = static_cast<EMixtormatRustStructure>(FMath::Clamp(static_cast<int32>(R.Structure), 0, 2));
						R.PitDepth = Bounded(R.PitDepth, Defaults.PitDepth, 0, 1);
						R.Buildup = Bounded(R.Buildup, Defaults.Buildup, 0, 1);
						R.Flaking = Bounded(R.Flaking, Defaults.Flaking, 0, 1);
						R.FeatureSize = Bounded(R.FeatureSize, Defaults.FeatureSize, .5f, 8);
						R.SurfaceBias = Bounded(R.SurfaceBias, Defaults.SurfaceBias, -1, 1);
						R.Variation = Bounded(R.Variation, Defaults.Variation, 0, 1);
						R.HeightScale = Bounded(R.HeightScale, Defaults.HeightScale, -4, 4);
						R.Roughness = Bounded(R.Roughness, Defaults.Roughness, 0, 1);
						for (auto& Stop : R.ColorRamp.Stops)
						{
							Stop.X = Bounded(Stop.X, 0, 0, 1);
							Stop.Color.R = Bounded(Stop.Color.R, Defaults.ColorRamp.Stops[0].Color.R, 0, 1);
							Stop.Color.G = Bounded(Stop.Color.G, Defaults.ColorRamp.Stops[0].Color.G, 0, 1);
							Stop.Color.B = Bounded(Stop.Color.B, Defaults.ColorRamp.Stops[0].Color.B, 0, 1);
							Stop.Color.A = 1;
						}
						R.ColorRamp.Sanitize();
						return;
					}
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
		SANITIZE_SKIN_FLOAT(SurfaceTiling);
		SANITIZE_SKIN_FLOAT(GravityHold);
		SANITIZE_SKIN_FLOAT(GravityBulge);
		SANITIZE_SKIN_FLOAT(GravityAngle);
		SANITIZE_SKIN_FLOAT(VascularScale);
		SANITIZE_SKIN_FLOAT(VascularDensity);
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
				SANITIZE_SKIN_FLOAT(WarpScale);
		SANITIZE_SKIN_FLOAT(DirectionVariation);
		SANITIZE_SKIN_FLOAT(Elongation);
		SANITIZE_SKIN_FLOAT(Cellular);
		SANITIZE_SKIN_FLOAT(CurvatureRadius);
		SANITIZE_SKIN_FLOAT(CurvatureStrength);
		SANITIZE_SKIN_FLOAT(PoreSize);
				SANITIZE_SKIN_FLOAT(PoreDepth);
		SANITIZE_SKIN_FLOAT(PoreVariation);
		SANITIZE_SKIN_FLOAT(Grain);
		SANITIZE_SKIN_FLOAT(PigmentVariation);
		SANITIZE_SKIN_FLOAT(ThicknessVariation);
		SANITIZE_SKIN_FLOAT(VascularAmount);
		SANITIZE_SKIN_FLOAT(RednessAmount);
		SANITIZE_SKIN_FLOAT(VeinAmount);
		SANITIZE_SKIN_FLOAT(VeinWidth);
		SANITIZE_SKIN_FLOAT(VeinDepth);
		SANITIZE_SKIN_FLOAT(VeinDensity);
		SANITIZE_SKIN_FLOAT(OilAmount);
		SANITIZE_SKIN_FLOAT(Dryness);
		SANITIZE_SKIN_FLOAT(Flaking);
		SANITIZE_SKIN_FLOAT(FinishVariation);
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
