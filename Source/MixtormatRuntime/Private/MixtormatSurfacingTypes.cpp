// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatSurfacingTypes.h"

namespace MixtormatSurfacing
{
	TConstArrayView<FMixtormatRustPreset> GetRustPresets()
	{
		static const TArray<FMixtormatRustPreset> Presets = []()
		{
			TArray<FMixtormatRustPreset> Built;
			Built.SetNum(7);
			Built[0].Name = NSLOCTEXT("MixtormatSurfacing", "RustAtmospheric", "Atmospheric Granular");
			Built[0].Description = NSLOCTEXT("MixtormatSurfacing", "RustAtmosphericHelp", "Clustered orange-brown granular deposits with shallow connected metal loss.");
			Built[1].Name = NSLOCTEXT("MixtormatSurfacing", "RustThin", "Thin Discoloration");
			Built[1].Description = NSLOCTEXT("MixtormatSurfacing", "RustThinHelp", "Smooth sparse oxide with almost no relief; underlying stamped detail remains intact.");
			{ auto& R = Built[1].Settings; R.Structure = EMixtormatRustStructure::Smooth; R.Coverage = .3f;
			  R.PitDepth = .001f; R.Buildup = .001f; R.Flaking = 0; R.Roughness = .58f; R.Variation = .4f; }
			Built[2].Name = NSLOCTEXT("MixtormatSurfacing", "RustPits", "Localized Attack");
			Built[2].Description = NSLOCTEXT("MixtormatSurfacing", "RustPitsHelp", "Deep coalescing pits with little retained product; loss is independent of oxide coverage.");
			{ auto& R = Built[2].Settings; R.Coverage = .38f; R.PitDepth = .1f; R.Buildup = .006f;
			  R.FeatureSize = 1.2f; R.SurfaceBias = .65f; R.Flaking = .65f; R.Seed = 37; }
			Built[3].Name = NSLOCTEXT("MixtormatSurfacing", "RustCrust", "Compact Crust");
			Built[3].Description = NSLOCTEXT("MixtormatSurfacing", "RustCrustHelp", "Broad dark plates with mineral seams and restrained shedding.");
			{ auto& R = Built[3].Settings; R.Structure = EMixtormatRustStructure::Layered; R.Coverage = .82f;
			  R.FeatureSize = 3.5f; R.PitDepth = .035f; R.Buildup = .07f; R.Flaking = .08f; R.Roughness = .9f;
			  R.ColorRamp.Stops = {{0, FLinearColor(.025f,.012f,.008f,1)}, {.5f, FLinearColor(.11f,.035f,.016f,1)}, {1, FLinearColor(.34f,.10f,.025f,1)}}; }
			Built[4].Name = NSLOCTEXT("MixtormatSurfacing", "RustBuildup", "Granular Buildup");
			Built[4].Description = NSLOCTEXT("MixtormatSurfacing", "RustBuildupHelp", "Raised ochre aggregate with shallow attack; demonstrates independent buildup.");
			{ auto& R = Built[4].Settings; R.Coverage = .72f; R.PitDepth = .006f; R.Buildup = .12f;
			  R.Flaking = 0; R.FeatureSize = 2.5f; R.Variation = .9f; R.Roughness = .94f;
			  R.ColorRamp.Stops = {{0, FLinearColor(.08f,.025f,.01f,1)}, {.5f, FLinearColor(.32f,.10f,.02f,1)}, {1, FLinearColor(.6f,.28f,.065f,1)}}; }
			Built[5].Name = NSLOCTEXT("MixtormatSurfacing", "RustLayers", "Layered Accumulation");
			Built[5].Description = NSLOCTEXT("MixtormatSurfacing", "RustLayersHelp", "Terraced attached plates and irregular surviving edges above recessed metal.");
			{ auto& R = Built[5].Settings; R.Structure = EMixtormatRustStructure::Layered; R.Coverage = .85f;
			  R.PitDepth = .055f; R.Buildup = .14f; R.Flaking = .35f; R.FeatureSize = 4; R.Variation = .85f; R.Seed = 71; }
			Built[6].Name = NSLOCTEXT("MixtormatSurfacing", "RustShed", "After Shedding");
			Built[6].Description = NSLOCTEXT("MixtormatSurfacing", "RustShedHelp", "Sparse retained plate islands over the same deep attack footprint; shedding does not heal pits.");
			Built[6].Settings = Built[5].Settings;
			Built[6].Settings.Flaking = .85f;
			return Built;
		}();
		return MakeArrayView(Presets);
	}
	TConstArrayView<FMixtormatMossPreset> GetMossPresets()
	{
		static const TArray<FMixtormatMossPreset> Presets = []()
		{
			TArray<FMixtormatMossPreset> Built;
			Built.SetNum(10);
			auto Ramp = [](FMixtormatMoss& M, FLinearColor A, FLinearColor B, FLinearColor C)
			{
				M.ColorRamp.Stops = {{0.0f, A}, {0.5f, B}, {1.0f, C}};
			};
			Built[0].Name = NSLOCTEXT("MixtormatSurfacing", "MossLushWet", "Lush Wet");
			Built[0].Description = NSLOCTEXT("MixtormatSurfacing", "MossLushWetHelp", "Deep green saturated cushions, dark wet base, low wet roughness.");
			{ FMixtormatMoss& M = Built[0].Settings; M.Moisture = 0.85f; M.WetRoughness = 0.35f; M.DryRoughness = 0.9f;
						  M.Hydration = 0.95f; M.SurfaceWetness = 0.85f; M.Plumpness = 0.8f; M.LeafScale = 3.5f;
						  M.ColonyDensity = 6; M.Coverage = 0.75f; M.TipColor = 0.55f; M.ColonyVariation = 0.65f;
			  Ramp(M, FLinearColor(0.03f,0.09f,0.02f,1), FLinearColor(0.08f,0.24f,0.05f,1), FLinearColor(0.30f,0.52f,0.12f,1)); }
			Built[1].Name = NSLOCTEXT("MixtormatSurfacing", "MossDryPale", "Dry Pale");
			Built[1].Description = NSLOCTEXT("MixtormatSurfacing", "MossDryPaleHelp", "Yellow-green desiccated moss, matte and thin.");
			{ FMixtormatMoss& M = Built[1].Settings; M.Moisture = 0.25f; M.WetRoughness = 0.7f; M.DryRoughness = 1.0f; M.CushionHeight = 0.02f;
						  M.Hydration = 0.12f; M.SurfaceWetness = 0; M.Damage = 0.12f; M.Plumpness = 0.2f;
						  M.LeafScale = 4; M.ShootLength = 0.65f; M.LeafVariation = 0.8f; M.TipColor = 0.12f;
			  Ramp(M, FLinearColor(0.14f,0.16f,0.05f,1), FLinearColor(0.32f,0.38f,0.12f,1), FLinearColor(0.58f,0.62f,0.25f,1)); }
			Built[2].Name = NSLOCTEXT("MixtormatSurfacing", "MossForestFloor", "Forest Floor");
			Built[2].Description = NSLOCTEXT("MixtormatSurfacing", "MossForestFloorHelp", "Olive-brown mixed colonies with decaying old growth.");
			{ FMixtormatMoss& M = Built[2].Settings; M.Moisture = 0.6f; M.Coverage = 0.65f;
						  M.GrowthForm = EMixtormatMossForm::CreepingMat; M.LeafScale = 5; M.Damage = 0.25f;
						  M.ColonyVariation = 0.85f; M.LeafVariation = 0.7f; M.CushionHeight = 0.014f; M.MatureHeight = 0.02f;
			  Ramp(M, FLinearColor(0.10f,0.10f,0.03f,1), FLinearColor(0.22f,0.26f,0.08f,1), FLinearColor(0.42f,0.40f,0.16f,1)); }
			Built[3].Name = NSLOCTEXT("MixtormatSurfacing", "MossAlpine", "Alpine Cushion");
			Built[3].Description = NSLOCTEXT("MixtormatSurfacing", "MossAlpineHelp", "Compact blue-green cushions, dense fine shoots.");
			{ FMixtormatMoss& M = Built[3].Settings; M.Moisture = 0.5f; M.ClumpDensity = 48.0f; M.ShootDensity = 0.85f; M.CushionHeight = 0.025f;
						  M.CushionMode = EMixtormatMossCushionMode::SdfInflation; M.ColonyDensity = 5; M.Competition = 0.9f;
						  M.LeafScale = 2.5f; M.Softness = 0.25f; M.SurfaceWetness = 0.08f; M.TipColor = 0.55f;
			  Ramp(M, FLinearColor(0.04f,0.12f,0.08f,1), FLinearColor(0.10f,0.30f,0.18f,1), FLinearColor(0.28f,0.55f,0.35f,1)); }
			Built[4].Name = NSLOCTEXT("MixtormatSurfacing", "MossDecaying", "Decaying");
			Built[4].Description = NSLOCTEXT("MixtormatSurfacing", "MossDecayingHelp", "Brown-tipped old colonies over a dark base.");
			{ FMixtormatMoss& M = Built[4].Settings; M.Moisture = 0.4f; M.GrowthIterations = 700;
						  M.GrowthForm = EMixtormatMossForm::SweptTuft; M.Damage = 0.75f; M.Hydration = 0.25f;
						  M.SurfaceWetness = 0.08f; M.TipColor = 0.08f; M.ColonyVariation = 0.9f; M.LeafScale = 5;
			  Ramp(M, FLinearColor(0.09f,0.07f,0.03f,1), FLinearColor(0.20f,0.22f,0.07f,1), FLinearColor(0.45f,0.32f,0.14f,1)); }
			Built[5].Name = NSLOCTEXT("MixtormatSurfacing", "MossRockCushion", "Rock Cushion");
			Built[5].Description = NSLOCTEXT("MixtormatSurfacing", "MossRockCushionHelp", "Separated firm cushions with radial rosettes and dry gold-green tips.");
			{ FMixtormatMoss& M = Built[5].Settings; M.CushionMode = EMixtormatMossCushionMode::SdfInflation;
			  M.ColonyDensity = 4; M.Coverage = 0.4f; M.Competition = 0.95f; M.GrowthSpread = 0.25f;
			  M.Softness = 0.2f; M.LeafScale = 4; M.Hydration = 0.4f; M.SurfaceWetness = 0.05f;
			  M.TipColor = 0.75f; M.ShootLength = 0.6f;
			  Ramp(M, FLinearColor(.045f,.075f,.018f,1), FLinearColor(.16f,.25f,.045f,1), FLinearColor(.48f,.50f,.12f,1)); }
			Built[6].Name = NSLOCTEXT("MixtormatSurfacing", "MossForestPillow", "Forest Pillow");
			Built[6].Description = NSLOCTEXT("MixtormatSurfacing", "MossForestPillowHelp", "Broad soft colonies, plump radial shoots and warm new-growth tips.");
			{ FMixtormatMoss& M = Built[6].Settings; M.ColonyDensity = 4; M.Coverage = 0.8f;
			  M.CushionSize = 0.2f; M.CushionHeight = 0.055f; M.MatureHeight = 0.07f; M.Softness = 0.85f;
			  M.Hydration = 0.85f; M.Plumpness = 0.85f; M.LeafScale = 4; M.ColonyVariation = 0.7f;
			  M.TipColor = 0.65f; M.SurfaceWetness = 0.35f; }
			Built[7].Name = NSLOCTEXT("MixtormatSurfacing", "MossSweptTuft", "Swept Tuft");
			Built[7].Description = NSLOCTEXT("MixtormatSurfacing", "MossSweptTuftHelp", "Open fans of long tapered leaves following local growth and cushion flow.");
			{ FMixtormatMoss& M = Built[7].Settings; M.GrowthForm = EMixtormatMossForm::SweptTuft;
			  M.LeafScale = 6; M.ShootDensity = 0.4f; M.ShootLength = 0.85f; M.FiberThickness = 0.28f;
			  M.Hydration = 0.7f; M.CushionHeight = 0.025f; M.LeafVariation = 0.8f; M.TipColor = 0.6f;
			  Ramp(M, FLinearColor(.035f,.07f,.025f,1), FLinearColor(.09f,.24f,.08f,1), FLinearColor(.34f,.5f,.16f,1)); }
			Built[8].Name = NSLOCTEXT("MixtormatSurfacing", "MossCreepingCarpet", "Creeping Carpet");
			Built[8].Description = NSLOCTEXT("MixtormatSurfacing", "MossCreepingCarpetHelp", "Low spreading mats with alternate leaves, restrained bulk and varied olive shoots.");
			{ FMixtormatMoss& M = Built[8].Settings; M.GrowthForm = EMixtormatMossForm::CreepingMat;
			  M.LeafScale = 6; M.Coverage = 0.8f; M.GrowthSpread = 0.8f; M.Competition = 0.25f;
			  M.CushionHeight = 0.008f; M.MatureHeight = 0.012f; M.ShootLength = 0.5f;
			  M.FiberThickness = 0.65f; M.ColonyVariation = 0.8f; M.Hydration = 0.8f;
			  Ramp(M, FLinearColor(.06f,.08f,.02f,1), FLinearColor(.18f,.27f,.06f,1), FLinearColor(.4f,.49f,.15f,1)); }
			Built[9].Name = NSLOCTEXT("MixtormatSurfacing", "MossFeatherWeft", "Feather Weft");
			Built[9].Description = NSLOCTEXT("MixtormatSurfacing", "MossFeatherWeftHelp", "Paired tapering fronds over a shallow connected bed; no detached geometry.");
			{ FMixtormatMoss& M = Built[9].Settings; M.GrowthForm = EMixtormatMossForm::Feather;
			  M.LeafScale = 8; M.ShootDensity = 0.35f; M.ShootLength = 0.7f; M.FiberThickness = 0.55f;
			  M.CushionHeight = 0.012f; M.MatureHeight = 0.018f; M.Coverage = 0.7f;
			  M.Hydration = 0.9f; M.LeafVariation = 0.7f; M.TipColor = 0.7f; M.SurfaceWetness = 0.4f;
			  Ramp(M, FLinearColor(.025f,.08f,.045f,1), FLinearColor(.07f,.25f,.12f,1), FLinearColor(.3f,.52f,.21f,1)); }
			return Built;
		}();
		return MakeArrayView(Presets);
	}

	TConstArrayView<FMixtormatSkinPreset> GetSkinPresets()
	{
		static const TArray<FMixtormatSkinPreset> Presets = []()
		{
			TArray<FMixtormatSkinPreset> Built;
			Built.SetNum(15);

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
			E.GravityBulge = 0.65f;
			E.GravityHold = 0.6f;
			E.PoreSize = 1.0f;
			E.RednessAmount = 0.0f;
			E.OilAmount = 0.0f;
			E.Dryness = 0.75f;
			E.Flaking = 0.15f;
			E.FinishVariation = 0.8f;
			E.CavityAO = 0.45f;
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
			R.Fullness = 0.4f;
			R.Softness = 0.3f;
			R.Compression = 0.65f;
			R.Tension = 0.15f;
			R.FoldDepth = 0.65f;
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
			R.GravityBulge = 0.3f;
			R.GravityHold = 0.8f;
			R.RednessAmount = 0.08f;
			R.OilAmount = 0.03f;
			R.Dryness = 0.6f;
			R.Flaking = 0.12f;
			R.FinishVariation = 0.7f;
			R.CavityAO = 0.4f;
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
			S.Fullness = 0.12f;
			S.Softness = 0.85f;
			S.Compression = 0.2f;
			S.Tension = 0.65f;
			S.FoldDepth = 0.1f;
			S.FoldWidth = 0.22f;
			S.Crossing = 0.25f;
			S.TissueCoupling = 0.45f;
			S.MarbleDrift = 0.65f;
			S.WarpLayers = 2;
			S.DirectionVariation = 0.25f;
			S.Elongation = 1.4f;
			S.Cellular = 0.03f;
			S.PoreCells = 160;
			S.PoreSize = 0.65f;
			S.PoreDepth = 0.16f;
			S.PoreVariation = 0.25f;
			S.Grain = 0.12f;
			S.PigmentVariation = 0.08f;
			S.ThicknessVariation = 0.2f;
			S.VascularAmount = 0.08f;
			S.GravityBulge = 0.08f;
			S.GravityHold = 1.2f;
			S.RednessAmount = 0.08f;
			S.OilAmount = 0.08f;
			S.Dryness = 0.0f;
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
			P.Fullness = 0.22f;
			P.Softness = 0.6f;
			P.Compression = 0.4f;
			P.Tension = 0.3f;
			P.FoldDepth = 0.2f;
			P.Crossing = 0.4f;
			P.MarbleDrift = 0.9f;
			P.DirectionVariation = 0.4f;
			P.Elongation = 3.2f;
			P.Cellular = 0.18f;
			P.DetailCells = 96;
			P.PoreCells = 92;
			P.PoreSize = 1.35f;
			P.PoreDepth = 0.85f;
			P.PoreVariation = 0.85f;
			P.Grain = 0.35f;
			P.PigmentVariation = 0.18f;
			P.ThicknessVariation = 0.35f;
			P.VascularAmount = 0.1f;
			P.OilAmount = 0.28f;
			P.Dryness = 0.04f;
			P.CavityAO = 0.35f;
			P.CavityRoughness = 0.22f;
			P.RoughnessRamp.Points = {{0.0f, 0.68f}, {0.5f, 0.48f}, {1.0f, 0.38f}};

			FMixtormatSkinPreset& Human = Built[4];
			Human.Name = NSLOCTEXT("MixtormatSurfacing", "SkinHuman", "Human");
			Human.Description = NSLOCTEXT("MixtormatSurfacing", "SkinHumanHelp", "Fine connected folds and pores with linked pigment, tissue thickness and vascular absorption. An artistic skin look, not a medical tissue simulation.");
			FMixtormatSkin& H = Human.Settings;
			H.FoldCells = 22;
			H.Fullness = 0.32f;
			H.Softness = 0.65f;
			H.Compression = 0.45f;
			H.Tension = 0.35f;
			H.FoldDepth = 0.32f;
			H.FoldWidth = 0.12f;
			H.Crossing = 0.45f;
			H.TissueCoupling = 0.65f;
			H.DetailCells = 96;
			H.PoreCells = 120;
			H.PoreDepth = 0.35f;
			H.Grain = 0.22f;
			H.PigmentVariation = 0.18f;
			H.ThicknessVariation = 0.45f;
			H.VascularAmount = 0.18f;
			H.RednessAmount = 0.22f;
			H.VeinAmount = 0.15f;
			H.VeinDensity = 0.25f;
			H.VeinDepth = 0.6f;
			H.OilAmount = 0.16f;
			H.Dryness = 0.04f;
			H.CavityAO = 0.3f;
			H.CavityRoughness = 0.08f;
			H.AlbedoRamp.Stops = {
				{0.0f, FLinearColor(0.16f, 0.045f, 0.025f, 1.0f)},
				{0.5f, FLinearColor(0.5f, 0.23f, 0.14f, 1.0f)},
				{1.0f, FLinearColor(0.72f, 0.42f, 0.27f, 1.0f)}};
			H.RoughnessRamp.Points = {{0.0f, 0.55f}, {0.5f, 0.42f}, {1.0f, 0.34f}};

			// General is the canonical struct default, not a second copy of its values.
			FMixtormatSkinPreset& General = Built[5];
			General.Name = NSLOCTEXT("MixtormatSurfacing", "SkinGeneral", "General");
			General.Description = NSLOCTEXT("MixtormatSurfacing", "SkinGeneralHelp", "Balanced starting point: shallow tissue, fine crossing wrinkles, visible pores, restrained pigment/redness and a light uneven sheen. No prominent veins or flakes.");

			FMixtormatSkinPreset& Oily = Built[6];
			Oily.Name = NSLOCTEXT("MixtormatSurfacing", "SkinOily", "Oily");
			Oily.Description = NSLOCTEXT("MixtormatSurfacing", "SkinOilyHelp", "Patchy polished highlights over dense pores and shallow folds. Oil changes the finish, not tissue color or scattering. No sweat beads.");
			FMixtormatSkin& O = Oily.Settings;
			O.Seed = 11;
			O.FoldCells = 22;
			O.Fullness = 0.2f;
			O.Softness = 0.6f;
			O.FoldDepth = 0.16f;
			O.GravityHold = 1.0f;
			O.PoreCells = 160;
			O.PoreSize = 1.1f;
			O.PoreDepth = 0.5f;
			O.Grain = 0.15f;
			O.RednessAmount = 0.18f;
			O.OilAmount = 0.85f;
			O.Dryness = 0.0f;
			O.FinishVariation = 0.7f;
			O.RoughnessRamp.Points = {{0.0f, 0.5f}, {0.5f, 0.4f}, {1.0f, 0.32f}};

			FMixtormatSkinPreset& DryFlaky = Built[7];
			DryFlaky.Name = NSLOCTEXT("MixtormatSurfacing", "SkinDryFlaky", "Dry Flaky");
			DryFlaky.Description = NSLOCTEXT("MixtormatSurfacing", "SkinDryFlakyHelp", "Coarser attached peeling scales over a dry broken microcrease network. Shallow pores, strong matte finish and restrained tissue bulk; not wounds or scabs.");
			FMixtormatSkin& D = DryFlaky.Settings;
			D.Seed = 53;
			D.Fullness = 0.16f;
			D.Softness = 0.35f;
			D.Compression = 0.65f;
			D.Tension = 0.12f;
			D.FoldDepth = 0.3f;
			D.FoldWidth = 0.08f;
			D.DetailCells = 72;
			D.Cellular = 0.65f;
			// Flake spacing currently follows pore density; coarsen it without deepening pores.
			D.PoreCells = 64;
			D.PoreSize = 0.45f;
			D.PoreDepth = 0.12f;
			D.Grain = 0.65f;
			D.PigmentVariation = 0.25f;
			D.RednessAmount = 0.1f;
			D.OilAmount = 0.0f;
			D.Dryness = 1.0f;
			D.Flaking = 0.95f;
			D.FinishVariation = 0.45f;
			D.RoughnessRamp.Points = {{0.0f, 0.88f}, {0.5f, 0.76f}, {1.0f, 0.65f}};

			FMixtormatSkinPreset& Mature = Built[8];
			Mature.Name = NSLOCTEXT("MixtormatSurfacing", "SkinMature", "Mature");
			Mature.Description = NSLOCTEXT("MixtormatSurfacing", "SkinMatureHelp", "Loose supported tissue, stronger downhill sag, layered creases and uneven pigment. An authored weathered look, not a biological age model.");
			FMixtormatSkin& M = Mature.Settings;
			M.Seed = 67;
			M.TissueCells = 4;
			M.FoldCells = 18;
			M.Fullness = 0.5f;
			M.Softness = 0.8f;
			M.Compression = 0.65f;
			M.Tension = 0.15f;
			M.FoldDepth = 0.55f;
			M.FoldWidth = 0.16f;
			M.Elongation = 2.5f;
			M.GravityBulge = 1.1f;
			M.GravityHold = 0.25f;
			M.Bending = 0.25f;
			M.Support = 0.45f;
			M.TissueCoupling = 0.75f;
			M.DetailCells = 112;
			M.PoreCells = 144;
			M.PoreDepth = 0.3f;
			M.Grain = 0.28f;
			M.PigmentVariation = 0.4f;
			M.ThicknessVariation = 0.4f;
			M.RednessAmount = 0.1f;
			M.VeinAmount = 0.25f;
			M.OilAmount = 0.06f;
			M.Dryness = 0.45f;
			M.Flaking = 0.05f;

			FMixtormatSkinPreset& Veined = Built[9];
			Veined.Name = NSLOCTEXT("MixtormatSurfacing", "SkinVeined", "Veined");
			Veined.Description = NSLOCTEXT("MixtormatSurfacing", "SkinVeinedHelp", "Light-toned shallow skin with broad visible branching veins and subdued background redness. Vein depth is an artistic appearance control, not measured anatomy.");
			FMixtormatSkin& V = Veined.Settings;
			V.Seed = 79;
			V.TissueCells = 4;
			V.Fullness = 0.16f;
			V.Compression = 0.28f;
			V.Tension = 0.45f;
			V.FoldDepth = 0.16f;
			V.FoldWidth = 0.1f;
			V.PoreCells = 144;
			V.PoreSize = 0.7f;
			V.PoreDepth = 0.25f;
			V.Grain = 0.15f;
			V.PigmentVariation = 0.1f;
			V.ThicknessVariation = 0.2f;
			V.VascularAmount = 0.04f;
			V.RednessAmount = 0.04f;
			V.VeinAmount = 0.9f;
			V.VeinDensity = 0.65f;
			V.VeinWidth = 1.4f;
			V.VeinDepth = 0.2f;
			V.OilAmount = 0.08f;
			V.Dryness = 0.04f;
			V.AlbedoRamp.Stops = {
				{0.0f, FLinearColor(0.42f, 0.25f, 0.18f, 1.0f)},
				{0.5f, FLinearColor(0.62f, 0.40f, 0.30f, 1.0f)},
				{1.0f, FLinearColor(0.76f, 0.55f, 0.43f, 1.0f)}};
			V.RoughnessRamp.Points = {{0.0f, 0.55f}, {0.5f, 0.45f}, {1.0f, 0.38f}};

			FMixtormatSkinPreset& DeepTone = Built[10];
			DeepTone.Name = NSLOCTEXT("MixtormatSurfacing", "SkinDeepTone", "Deep Tone");
			DeepTone.Description = NSLOCTEXT("MixtormatSurfacing", "SkinDeepToneHelp", "Deep warm-brown pigment with the General relief and a restrained sheen. Tone does not automatically add wrinkles, dryness, or change physical scattering distances.");
			FMixtormatSkin& T = DeepTone.Settings;
			T.Seed = 97;
			T.PigmentVariation = 0.22f;
			T.RednessAmount = 0.1f;
			T.OilAmount = 0.18f;
			T.Dryness = 0.04f;
			T.FuzzColor = FLinearColor(0.30f, 0.16f, 0.09f, 1.0f);
			T.AlbedoRamp.Stops = {
				{0.0f, FLinearColor(0.04f, 0.014f, 0.007f, 1.0f)},
				{0.5f, FLinearColor(0.11f, 0.045f, 0.023f, 1.0f)},
				{1.0f, FLinearColor(0.23f, 0.105f, 0.055f, 1.0f)}};

			FMixtormatSkinPreset& Marbled = Built[11];
			Marbled.Name = NSLOCTEXT("MixtormatSurfacing", "SkinMarbledFolds", "Marbled Folds");
			Marbled.Description = NSLOCTEXT("MixtormatSurfacing", "SkinMarbledFoldsHelp", "Long wandering relief folds with raised shoulders, layered drift and sparse crossing creases. A creature-skin study, not a painted marble color pattern.");
			FMixtormatSkin& MF = Marbled.Settings;
			MF.Seed = 109;
			MF.TissueCells = 4;
			MF.FoldCells = 12;
			MF.DetailCells = 64;
			MF.Fullness = 0.55f;
			MF.Softness = 0.65f;
			MF.Compression = 0.85f;
			MF.Tension = 0.1f;
			MF.FoldDepth = 1.0f;
			MF.FoldWidth = 0.18f;
			MF.Direction = 25.0f;
			MF.Elongation = 7.0f;
			MF.Crossing = 0.2f;
			MF.Cellular = 0.05f;
			MF.MarbleDrift = 3.2f;
			MF.WarpScale = 0.75f;
			MF.WarpLayers = 5;
			MF.DirectionVariation = 0.9f;
			MF.Bending = 0.5f;
			MF.Support = 0.45f;
			MF.TissueCoupling = 0.85f;
			MF.PoreDepth = 0.12f;
			MF.Grain = 0.18f;
			MF.PigmentVariation = 0.35f;
			MF.ThicknessVariation = 0.4f;
			MF.VascularAmount = 0.0f;
			MF.RednessAmount = 0.0f;
			MF.OilAmount = 0.2f;
			MF.Dryness = 0.1f;
			MF.FuzzAmount = 0.0f;
			MF.AlbedoRamp.Stops = {
				{0.0f, FLinearColor(0.055f, 0.075f, 0.08f, 1.0f)},
				{0.5f, FLinearColor(0.21f, 0.28f, 0.26f, 1.0f)},
				{1.0f, FLinearColor(0.43f, 0.49f, 0.36f, 1.0f)}};
			MF.RoughnessRamp.Points = {{0.0f, 0.64f}, {0.5f, 0.5f}, {1.0f, 0.4f}};

			FMixtormatSkinPreset& Palm = Built[12];
			Palm.Name = NSLOCTEXT("MixtormatSurfacing", "SkinPalmCreases", "Palm Creases");
			Palm.Description = NSLOCTEXT("MixtormatSurfacing", "SkinPalmCreasesHelp", "Broad crossing flexion-like creases over fine dry lines, with almost no follicular pores. Procedural hand-like relief, not anatomical palm lines, fingerprints or a handprint.");
			FMixtormatSkin& PC = Palm.Settings;
			PC.Seed = 127;
			PC.TissueCells = 3;
			PC.FoldCells = 7;
			PC.DetailCells = 80;
			PC.Fullness = 0.2f;
			PC.Softness = 0.55f;
			PC.Compression = 0.75f;
			PC.Tension = 0.2f;
			PC.FoldDepth = 0.8f;
			PC.FoldWidth = 0.07f;
			PC.Direction = -25.0f;
			PC.Elongation = 2.8f;
			PC.Crossing = 0.85f;
			PC.Cellular = 0.12f;
			PC.MarbleDrift = 1.4f;
			PC.DirectionVariation = 0.4f;
			PC.TissueCoupling = 0.8f;
			PC.Bending = 0.2f;
			PC.Support = 0.7f;
			PC.GravityBulge = 0.0f;
			PC.PoreSize = 0.35f;
			PC.PoreDepth = 0.03f;
			PC.Grain = 0.3f;
			PC.PigmentVariation = 0.12f;
			PC.VascularAmount = 0.06f;
			PC.RednessAmount = 0.18f;
			PC.OilAmount = 0.0f;
			PC.Dryness = 0.6f;
			PC.FinishVariation = 0.35f;
			PC.FuzzAmount = 0.0f;
			PC.AlbedoRamp.Stops = {
				{0.0f, FLinearColor(0.28f, 0.12f, 0.075f, 1.0f)},
				{0.5f, FLinearColor(0.55f, 0.32f, 0.22f, 1.0f)},
				{1.0f, FLinearColor(0.72f, 0.49f, 0.35f, 1.0f)}};
			PC.RoughnessRamp.Points = {{0.0f, 0.72f}, {0.5f, 0.6f}, {1.0f, 0.5f}};

			FMixtormatSkinPreset& Hide = Built[13];
			Hide.Name = NSLOCTEXT("MixtormatSurfacing", "SkinPebbledHide", "Pebbled Hide");
			Hide.Description = NSLOCTEXT("MixtormatSurfacing", "SkinPebbledHideHelp", "Compact cellular crease network, rounded tissue islands and dry grain in a neutral animal-hide palette. Height-field hide approximation, not overlapping reptile scales.");
			FMixtormatSkin& PH = Hide.Settings;
			PH.Seed = 149;
			PH.TissueCells = 18;
			PH.FoldCells = 28;
			PH.DetailCells = 80;
			PH.Fullness = 0.7f;
			PH.Softness = 0.3f;
			PH.Compression = 0.8f;
			PH.Tension = 0.08f;
			PH.FoldDepth = 0.65f;
			PH.FoldWidth = 0.09f;
			PH.Elongation = 1.2f;
			PH.Crossing = 0.25f;
			PH.Cellular = 0.9f;
			PH.MarbleDrift = 0.45f;
			PH.DirectionVariation = 0.7f;
			PH.TissueCoupling = 0.9f;
			PH.Bending = 0.25f;
			PH.Support = 0.85f;
			PH.GravityBulge = 0.05f;
			PH.PoreCells = 96;
			PH.PoreSize = 0.5f;
			PH.PoreDepth = 0.16f;
			PH.Grain = 0.6f;
			PH.PigmentVariation = 0.45f;
			PH.ThicknessVariation = 0.45f;
			PH.VascularAmount = 0.0f;
			PH.RednessAmount = 0.0f;
			PH.OilAmount = 0.0f;
			PH.Dryness = 0.85f;
			PH.Flaking = 0.12f;
			PH.FinishVariation = 0.65f;
			PH.FuzzAmount = 0.0f;
			PH.SSSAmount = 0.2f;
			PH.AlbedoRamp.Stops = {
				{0.0f, FLinearColor(0.045f, 0.035f, 0.02f, 1.0f)},
				{0.5f, FLinearColor(0.19f, 0.15f, 0.085f, 1.0f)},
				{1.0f, FLinearColor(0.38f, 0.31f, 0.19f, 1.0f)}};
			PH.RoughnessRamp.Points = {{0.0f, 0.86f}, {0.5f, 0.72f}, {1.0f, 0.6f}};

			FMixtormatSkinPreset& Amphibian = Built[14];
			Amphibian.Name = NSLOCTEXT("MixtormatSurfacing", "SkinWetAmphibian", "Wet Amphibian");
			Amphibian.Description = NSLOCTEXT("MixtormatSurfacing", "SkinWetAmphibianHelp", "Soft small tissue lobes, shallow creases, mottled green pigment and a patchy low-roughness finish. Stylized amphibian skin; Oil approximates sheen, not a separate water film.");
			FMixtormatSkin& WA = Amphibian.Settings;
			WA.Seed = 163;
			WA.TissueCells = 14;
			WA.FoldCells = 18;
			WA.DetailCells = 64;
			WA.Fullness = 0.6f;
			WA.Softness = 0.85f;
			WA.Compression = 0.25f;
			WA.Tension = 0.6f;
			WA.FoldDepth = 0.12f;
			WA.FoldWidth = 0.18f;
			WA.Elongation = 1.1f;
			WA.Crossing = 0.2f;
			WA.Cellular = 0.25f;
			WA.MarbleDrift = 1.1f;
			WA.DirectionVariation = 0.8f;
			WA.Bending = 0.6f;
			WA.Support = 0.8f;
			WA.GravityBulge = 0.08f;
			WA.PoreCells = 80;
			WA.PoreSize = 0.5f;
			WA.PoreDepth = 0.1f;
			WA.Grain = 0.08f;
			WA.PigmentVariation = 0.9f;
			WA.ThicknessVariation = 0.45f;
			WA.VascularAmount = 0.0f;
			WA.RednessAmount = 0.0f;
			WA.OilAmount = 0.85f;
			WA.Dryness = 0.0f;
			WA.FinishVariation = 0.6f;
			WA.FuzzAmount = 0.0f;
			WA.AlbedoRamp.Stops = {
				{0.0f, FLinearColor(0.025f, 0.045f, 0.018f, 1.0f)},
				{0.5f, FLinearColor(0.12f, 0.25f, 0.065f, 1.0f)},
				{1.0f, FLinearColor(0.42f, 0.5f, 0.16f, 1.0f)}};
			WA.RoughnessRamp.Points = {{0.0f, 0.44f}, {0.5f, 0.3f}, {1.0f, 0.22f}};
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
		static const FMixtormatSurfacingOutput RustOutputs[] = {
					{FName(TEXT("Height")), NSLOCTEXT("MixtormatSurfacing", "RustHeight", "Height Delta"), K::ScalarSigned},
					{FName(TEXT("Color")), NSLOCTEXT("MixtormatSurfacing", "RustColor", "Color"), K::Color},
					{FName(TEXT("OxideCoverage")), NSLOCTEXT("MixtormatSurfacing", "RustCoverage", "Visible Oxide"), K::Scalar01},
				};
				if (Type == EMixtormatSurfacingType::Rust) { return MakeArrayView(RustOutputs); }
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
