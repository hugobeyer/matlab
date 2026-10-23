// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatSurfacingTypes.h"

namespace MixtormatSurfacing
{
	TConstArrayView<FMixtormatMossPreset> GetMossPresets()
	{
		static const TArray<FMixtormatMossPreset> Presets = []()
		{
			TArray<FMixtormatMossPreset> Built;
			Built.SetNum(5);
			auto Ramp = [](FMixtormatMoss& M, FLinearColor A, FLinearColor B, FLinearColor C)
			{
				M.ColorRamp.Stops = {{0.0f, A}, {0.5f, B}, {1.0f, C}};
			};
			Built[0].Name = NSLOCTEXT("MixtormatSurfacing", "MossLushWet", "Lush Wet");
			Built[0].Description = NSLOCTEXT("MixtormatSurfacing", "MossLushWetHelp", "Deep green saturated cushions, dark wet base, low wet roughness.");
			{ FMixtormatMoss& M = Built[0].Settings; M.Moisture = 0.85f; M.WetRoughness = 0.35f; M.DryRoughness = 0.9f;
			  Ramp(M, FLinearColor(0.03f,0.09f,0.02f,1), FLinearColor(0.08f,0.24f,0.05f,1), FLinearColor(0.30f,0.52f,0.12f,1)); }
			Built[1].Name = NSLOCTEXT("MixtormatSurfacing", "MossDryPale", "Dry Pale");
			Built[1].Description = NSLOCTEXT("MixtormatSurfacing", "MossDryPaleHelp", "Yellow-green desiccated moss, matte and thin.");
			{ FMixtormatMoss& M = Built[1].Settings; M.Moisture = 0.25f; M.WetRoughness = 0.7f; M.DryRoughness = 1.0f; M.CushionHeight = 0.02f;
			  Ramp(M, FLinearColor(0.14f,0.16f,0.05f,1), FLinearColor(0.32f,0.38f,0.12f,1), FLinearColor(0.58f,0.62f,0.25f,1)); }
			Built[2].Name = NSLOCTEXT("MixtormatSurfacing", "MossForestFloor", "Forest Floor");
			Built[2].Description = NSLOCTEXT("MixtormatSurfacing", "MossForestFloorHelp", "Olive-brown mixed colonies with decaying old growth.");
			{ FMixtormatMoss& M = Built[2].Settings; M.Moisture = 0.6f; M.Coverage = 0.65f;
			  Ramp(M, FLinearColor(0.10f,0.10f,0.03f,1), FLinearColor(0.22f,0.26f,0.08f,1), FLinearColor(0.42f,0.40f,0.16f,1)); }
			Built[3].Name = NSLOCTEXT("MixtormatSurfacing", "MossAlpine", "Alpine Cushion");
			Built[3].Description = NSLOCTEXT("MixtormatSurfacing", "MossAlpineHelp", "Compact blue-green cushions, dense fine shoots.");
			{ FMixtormatMoss& M = Built[3].Settings; M.Moisture = 0.5f; M.ClumpDensity = 48.0f; M.ShootDensity = 0.85f; M.CushionHeight = 0.025f;
			  Ramp(M, FLinearColor(0.04f,0.12f,0.08f,1), FLinearColor(0.10f,0.30f,0.18f,1), FLinearColor(0.28f,0.55f,0.35f,1)); }
			Built[4].Name = NSLOCTEXT("MixtormatSurfacing", "MossDecaying", "Decaying");
			Built[4].Description = NSLOCTEXT("MixtormatSurfacing", "MossDecayingHelp", "Brown-tipped old colonies over a dark base.");
			{ FMixtormatMoss& M = Built[4].Settings; M.Moisture = 0.4f; M.GrowthIterations = 700;
			  Ramp(M, FLinearColor(0.09f,0.07f,0.03f,1), FLinearColor(0.20f,0.22f,0.07f,1), FLinearColor(0.45f,0.32f,0.14f,1)); }
			return Built;
		}();
		return MakeArrayView(Presets);
	}

	TConstArrayView<FMixtormatSkinPreset> GetSkinPresets()
	{
		static const TArray<FMixtormatSkinPreset> Presets = []()
		{
			TArray<FMixtormatSkinPreset> Built;
			Built.SetNum(5);

			FMixtormatSkinPreset& Elephant = Built[0];
			Elephant.Name = NSLOCTEXT("MixtormatSurfacing", "SkinElephant", "Elephant");
			Elephant.Description = NSLOCTEXT("MixtormatSurfacing", "SkinElephantHelp", "Heavy wandering folds with broad raised shoulders, coarse tissue and dry grey-brown pigmentation.");
			FMixtormatSkin& E = Elephant.Settings;
			E.Seed = 17;
			E.TissueCells = 4;
			E.FoldCells = 10;
			E.SolverIterations = 72;
			E.Bending = 0.55f;
			E.Support = 0.35f;
			E.Fullness = 0.95f;
			E.Softness = 0.4f;
			E.Compression = 0.9f;
			E.Tension = 0.08f;
			E.FoldDepth = 1.4f;
			E.FoldWidth = 0.2f;
			E.Crossing = 0.55f;
			E.MarbleDrift = 1.6f;
			E.WarpLayers = 4;
			E.DirectionVariation = 0.65f;
			E.Elongation = 4.8f;
			E.Cellular = 0.22f;
			E.DetailCells = 72;
			E.PoreCells = 88;
			E.PoreDepth = 0.3f;
			E.Grain = 0.65f;
			E.PigmentVariation = 0.3f;
			E.ThicknessVariation = 0.65f;
			E.VascularAmount = 0.02f;
			E.CavityAO = 0.7f;
			E.FuzzAmount = 0.02f;
			E.SSSAmount = 0.2f;
			E.AlbedoRamp.Stops = {
				{0.0f, FLinearColor(0.055f, 0.045f, 0.035f, 1.0f)},
				{0.5f, FLinearColor(0.24f, 0.21f, 0.17f, 1.0f)},
				{1.0f, FLinearColor(0.42f, 0.38f, 0.31f, 1.0f)}};
			E.RoughnessRamp.Points = {{0.0f, 0.85f}, {0.5f, 0.75f}, {1.0f, 0.65f}};

			FMixtormatSkinPreset& Rough = Built[1];
			Rough.Name = NSLOCTEXT("MixtormatSurfacing", "SkinRough", "Rough");
			Rough.Description = NSLOCTEXT("MixtormatSurfacing", "SkinRoughHelp", "Dense crossing folds, irregular cellular breakup and pronounced dry microdetail.");
			FMixtormatSkin& R = Rough.Settings;
			R.Seed = 29;
			R.TissueCells = 8;
			R.FoldCells = 24;
			R.SolverIterations = 64;
			R.Bending = 0.2f;
			R.Support = 0.6f;
			R.Softness = 0.3f;
			R.Compression = 0.85f;
			R.FoldDepth = 1.05f;
			R.FoldWidth = 0.11f;
			R.Crossing = 0.8f;
			R.MarbleDrift = 1.8f;
			R.WarpLayers = 4;
			R.DirectionVariation = 0.8f;
			R.Elongation = 2.5f;
			R.Cellular = 0.35f;
			R.DetailCells = 96;
			R.PoreCells = 110;
			R.PoreDepth = 0.75f;
			R.PoreVariation = 0.8f;
			R.Grain = 0.5f;
			R.PigmentVariation = 0.25f;
			R.ThicknessVariation = 0.55f;
			R.VascularAmount = 0.08f;
			R.CavityAO = 0.65f;
			R.CavityRoughness = 0.2f;
			R.RoughnessRamp.Points = {{0.0f, 0.8f}, {0.5f, 0.65f}, {1.0f, 0.55f}};

			FMixtormatSkinPreset& Smooth = Built[2];
			Smooth.Name = NSLOCTEXT("MixtormatSurfacing", "SkinSmooth", "Smooth");
			Smooth.Description = NSLOCTEXT("MixtormatSurfacing", "SkinSmoothHelp", "Soft supported tissue, shallow elongated folds and restrained pores with gentle absorption.");
			FMixtormatSkin& S = Smooth.Settings;
			S.Seed = 7;
			S.TissueCells = 5;
			S.FoldCells = 12;
			S.SolverIterations = 64;
			S.Bending = 0.75f;
			S.Support = 0.75f;
			S.Fullness = 0.6f;
			S.Softness = 0.85f;
			S.Compression = 0.2f;
			S.Tension = 0.65f;
			S.FoldDepth = 0.18f;
			S.FoldWidth = 0.22f;
			S.Crossing = 0.25f;
			S.TissueCoupling = 0.45f;
			S.MarbleDrift = 0.65f;
			S.WarpLayers = 2;
			S.DirectionVariation = 0.25f;
			S.Elongation = 5.5f;
			S.Cellular = 0.03f;
			S.PoreCells = 160;
			S.PoreDepth = 0.1f;
			S.PoreVariation = 0.25f;
			S.Grain = 0.12f;
			S.PigmentVariation = 0.08f;
			S.ThicknessVariation = 0.2f;
			S.VascularAmount = 0.08f;
			S.CavityAO = 0.2f;
			S.CavityRoughness = 0.04f;
			S.RoughnessRamp.Points = {{0.0f, 0.48f}, {0.5f, 0.36f}, {1.0f, 0.3f}};

			FMixtormatSkinPreset& Pored = Built[3];
			Pored.Name = NSLOCTEXT("MixtormatSurfacing", "SkinPored", "Pored");
			Pored.Description = NSLOCTEXT("MixtormatSurfacing", "SkinPoredHelp", "Prominent irregular pores over moderately relaxed folds, with localized cavity roughness.");
			FMixtormatSkin& P = Pored.Settings;
			P.Seed = 41;
			P.TissueCells = 7;
			P.FoldCells = 18;
			P.Bending = 0.45f;
			P.Support = 0.65f;
			P.Softness = 0.6f;
			P.Compression = 0.4f;
			P.Tension = 0.3f;
			P.FoldDepth = 0.4f;
			P.Crossing = 0.4f;
			P.MarbleDrift = 0.9f;
			P.DirectionVariation = 0.4f;
			P.Elongation = 3.2f;
			P.Cellular = 0.18f;
			P.DetailCells = 96;
			P.PoreCells = 92;
			P.PoreDepth = 1.0f;
			P.PoreVariation = 0.85f;
			P.Grain = 0.35f;
			P.PigmentVariation = 0.18f;
			P.ThicknessVariation = 0.35f;
			P.VascularAmount = 0.1f;
			P.CavityAO = 0.65f;
			P.CavityRoughness = 0.22f;
			P.RoughnessRamp.Points = {{0.0f, 0.68f}, {0.5f, 0.48f}, {1.0f, 0.38f}};

			FMixtormatSkinPreset& Human = Built[4];
			Human.Name = NSLOCTEXT("MixtormatSurfacing", "SkinHuman", "Human");
			Human.Description = NSLOCTEXT("MixtormatSurfacing", "SkinHumanHelp", "Fine connected folds and pores with linked pigment, tissue thickness and vascular absorption. An artistic skin look, not a medical tissue simulation.");
			FMixtormatSkin& H = Human.Settings;
			H.FoldCells = 22;
			H.Fullness = 0.7f;
			H.Softness = 0.65f;
			H.Compression = 0.45f;
			H.Tension = 0.35f;
			H.FoldDepth = 0.38f;
			H.FoldWidth = 0.12f;
			H.Crossing = 0.45f;
			H.TissueCoupling = 0.65f;
			H.DetailCells = 96;
			H.PoreCells = 120;
			H.PoreDepth = 0.35f;
			H.Grain = 0.22f;
			H.PigmentVariation = 0.18f;
			H.ThicknessVariation = 0.45f;
			H.VascularAmount = 0.25f;
			H.CavityAO = 0.3f;
			H.CavityRoughness = 0.08f;
			H.AlbedoRamp.Stops = {
				{0.0f, FLinearColor(0.16f, 0.045f, 0.025f, 1.0f)},
				{0.5f, FLinearColor(0.5f, 0.23f, 0.14f, 1.0f)},
				{1.0f, FLinearColor(0.72f, 0.42f, 0.27f, 1.0f)}};
			H.RoughnessRamp.Points = {{0.0f, 0.55f}, {0.5f, 0.42f}, {1.0f, 0.34f}};
			return Built;
		}();
		return MakeArrayView(Presets);
	}

	TConstArrayView<FMixtormatSurfacingOutput> GetOutputs(const EMixtormatSurfacingType Type)
	{
		using K = EMixtormatPublishedFieldKind;
		static const FMixtormatSurfacingOutput SkinOutputs[] = {
			{FName(TEXT("Height")), NSLOCTEXT("MixtormatSurfacing", "Height", "Height"), K::ScalarSigned},
			{FName(TEXT("Color")), NSLOCTEXT("MixtormatSurfacing", "Color", "Albedo"), K::Color},
			{FName(TEXT("Roughness")), NSLOCTEXT("MixtormatSurfacing", "Roughness", "Roughness"), K::Scalar01},
			{FName(TEXT("AO")), NSLOCTEXT("MixtormatSurfacing", "AO", "Cavity AO"), K::Scalar01},
			{FName(TEXT("F0")), NSLOCTEXT("MixtormatSurfacing", "F0", "Dielectric F0"), K::Scalar01},
			{FName(TEXT("Fuzz")), NSLOCTEXT("MixtormatSurfacing", "Fuzz", "Fuzz Amount"), K::Scalar01},
			{FName(TEXT("Pores")), NSLOCTEXT("MixtormatSurfacing", "Pores", "Pores"), K::Scalar01},
			{FName(TEXT("Compression")), NSLOCTEXT("MixtormatSurfacing", "Compression", "Compression Response"), K::Scalar01},
			{FName(TEXT("SSS")), NSLOCTEXT("MixtormatSurfacing", "SSS", "Mean Free Path (cm)"), K::Color, false}
		};
		static const FMixtormatSurfacingOutput MossOutputs[] = {
			{FName(TEXT("Height")), NSLOCTEXT("MixtormatSurfacing", "MossHeight", "Height"), K::ScalarSigned},
			{FName(TEXT("Color")), NSLOCTEXT("MixtormatSurfacing", "MossColor", "Albedo"), K::Color},
			{FName(TEXT("Roughness")), NSLOCTEXT("MixtormatSurfacing", "MossRoughness", "Roughness"), K::Scalar01},
			{FName(TEXT("Normal")), NSLOCTEXT("MixtormatSurfacing", "MossNormal", "Detail Normal"), K::Color},
			{FName(TEXT("GrowthCoverage")), NSLOCTEXT("MixtormatSurfacing", "MossCoverage", "Growth Coverage"), K::Scalar01},
			{FName(TEXT("GrowthAge")), NSLOCTEXT("MixtormatSurfacing", "MossAge", "Growth Age"), K::Scalar01},
			{FName(TEXT("RegionIds")), NSLOCTEXT("MixtormatSurfacing", "MossIds", "Colony IDs"), K::RegionIds},
		};
		if (Type == EMixtormatSurfacingType::Moss) { return MakeArrayView(MossOutputs); }
		return Type == EMixtormatSurfacingType::Skin
			? MakeArrayView(SkinOutputs) : TConstArrayView<FMixtormatSurfacingOutput>();
	}

	bool CanReferenceField(const FMixtormatSurfacing& Surfacing,
		const EMixtormatPublishedFieldKind Kind, const FName OutputName)
	{
		for (const FMixtormatSurfacingOutput& Output : GetOutputs(Surfacing.Type))
		{
			if (Output.Name == OutputName && Output.Kind == Kind) { return Output.bReferenceable; }
		}
		return false;
	}

	bool PublishesField(const FMixtormatSurfacing& Surfacing,
		const EMixtormatPublishedFieldKind Kind, const FName OutputName)
	{
		for (const FMixtormatSurfacingOutput& Output : GetOutputs(Surfacing.Type))
		{
			if (Output.Name == OutputName && Output.Kind == Kind) { return true; }
		}
		return false;
	}
}
