// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Compositing/MixtormatGeneratorGather.h"

#include "Compositing/MixtormatComposeHash.h"
#include "Compositing/MixtormatNoiseRender.h"
#include "MixtormatColorRampMath.h"
#include "MixtormatChildScope.h"
#include "MixtormatMaterial.h"
#include "MixtormatOutputReference.h"
#include "MixtormatScalarRampMath.h"
#include "MixtormatParameterDrivers.h"

namespace MixtormatGpuCompositor
{
	void GatherGeneratorChild(FLayerRenderData& Data, const FMixtormatLayer& Layer,
		const FMixtormatLayerChild& LayerChild, const int32 SourceChildIndex,
		const bool bGeneratorLayer, const bool bCacheLayers, const uint64 PlacementKey,
		const int32 LayerIndex, const TArray<FMixtormatLayer>& EffectiveLayers)
	{
		// Modules exist on any layer that owns them; bGeneratorLayer is the caller's answer to that
		// question, not a layer-type test. A disabled layer must not gather them -- an enabled
		// module on a hidden layer would build a surface nobody can see and still cost the solve.
		const FMixtormatGenerator& Generator = LayerChild.Generator;
		if (!bGeneratorLayer || !Layer.bEnabled || !Generator.bEnabled)
		{
			return;
		}

		FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
		ChildData.Type = EMixtormatLayerChildType::Generator;
		ChildData.SourceChildIndex = SourceChildIndex;
		ChildData.ScopeOwnerSourceChildIndex = MixtormatChildScope::ResolveOwnerIndex(Layer.Children, SourceChildIndex);
		ChildData.Generator.Type = Generator.Type;

	switch (Generator.Type)
	{
	case EMixtormatGeneratorType::StrataCarver:
	{
		const FMixtormatStrataCarver& Carver = Generator.StrataCarver;
		const FMixtormatStrataCarver Defaults;
		FStrataCarverRenderData& Out = ChildData.Generator.StrataCarver;
		// Non-finite falls back to the default; ranges are authored in the tool.
		const auto Finite = [](const float Value, const float Fallback)
		{
			return FMath::IsFinite(Value) ? Value : Fallback;
		};
		ChildData.Generator.bNormalizeHeight = Carver.bStrataNormalizeHeight;
		ChildData.Generator.HeightScale = Finite(Carver.StrataHeightScale, Defaults.StrataHeightScale);
		Out.Seed = static_cast<uint32>(Carver.Seed);
		Out.Depth = Finite(Carver.Depth, Defaults.Depth);
		Out.StrataFrequency = Finite(Carver.StrataFrequency, Defaults.StrataFrequency);
		Out.StrataRotation = Finite(Carver.StrataRotation, Defaults.StrataRotation);
		Out.ThicknessVariation = Finite(Carver.ThicknessVariation, Defaults.ThicknessVariation);
		Out.HeightVariation = Finite(Carver.HeightVariation, Defaults.HeightVariation);
		Out.Verticality = Finite(Carver.Verticality, Defaults.Verticality);
		Out.LedgeWidth = Finite(Carver.LedgeWidth, Defaults.LedgeWidth);
		Out.HardnessContrast = Finite(Carver.HardnessContrast, Defaults.HardnessContrast);
		Out.SoftRecession = Finite(Carver.SoftRecession, Defaults.SoftRecession);
		Out.Bend = Finite(Carver.Bend, Defaults.Bend);
		Out.BendScale = Carver.BendScale;
		Out.Breakup = Finite(Carver.Breakup, Defaults.Breakup);
		Out.JointScale = Carver.JointScale;
		Out.JointWidth = Finite(Carver.JointWidth, Defaults.JointWidth);
		Out.HeightFollow = Finite(Carver.HeightFollow, Defaults.HeightFollow);
		Out.Lamination = Finite(Carver.Lamination, Defaults.Lamination);
		Out.CrossBedding = Finite(Carver.CrossBedding, Defaults.CrossBedding);
		Out.MaskInfluence = Finite(Carver.MaskInfluence, Defaults.MaskInfluence);
		Out.IDInfluence = Finite(Carver.IDInfluence, Defaults.IDInfluence);
		break;
	}
	case EMixtormatGeneratorType::Cracks:
	{
		// Unclamped: ranges are authored in the tool. Non-finite falls back to the default.
		const FMixtormatCracks& Cracks = Generator.Cracks;
		const FMixtormatCracks Defaults;
		FCracksRenderData& Out = ChildData.Generator.Cracks;
		const auto Finite = [](const float Value, const float Fallback)
		{
			return FMath::IsFinite(Value) ? Value : Fallback;
		};
		ChildData.Generator.bNormalizeHeight = Cracks.bCrackNormalizeHeight;
		ChildData.Generator.HeightScale = Finite(Cracks.CrackHeightScale, 1.0f);
		Out.Seed = Cracks.CrackSeed;
		// No lattice below one cell.
		Out.Cells = FMath::Max(Cracks.CrackCells, 1);
		Out.Jitter = Finite(Cracks.CrackJitter, Defaults.CrackJitter);
		Out.Width = Finite(Cracks.CrackWidth, Defaults.CrackWidth);
		Out.Depth = Finite(Cracks.CrackDepth, Defaults.CrackDepth);
		Out.Rough = Finite(Cracks.CrackRough, Defaults.CrackRough);
		Out.Scale = Finite(Cracks.CrackScale, Defaults.CrackScale);
		Out.Detail = Finite(Cracks.CrackDetail, Defaults.CrackDetail);
		Out.Feather = Finite(Cracks.CrackFeather, Defaults.CrackFeather);
		Out.WidthVariation = Finite(Cracks.CrackWidthVariation, Defaults.CrackWidthVariation);
		Out.WidthScale = Finite(Cracks.CrackWidthScale, Defaults.CrackWidthScale);
		Out.LineVariation = Finite(Cracks.CrackLineVariation, Defaults.CrackLineVariation);
		Out.RegionVariation = Finite(Cracks.CrackRegionVariation, Defaults.CrackRegionVariation);
		Out.Chip = Finite(Cracks.CrackChip, Defaults.CrackChip);
		Out.ChipSize = Finite(Cracks.CrackChipSize, Defaults.CrackChipSize);
		Out.Gap = Finite(Cracks.CrackGap, Defaults.CrackGap);
		Out.GapWidth = Finite(Cracks.CrackGapWidth, Defaults.CrackGapWidth);
		Out.Slip = Finite(Cracks.CrackSlip, Defaults.CrackSlip);
		Out.Tilt = Finite(Cracks.CrackTilt, Defaults.CrackTilt);
		Out.ChamferAmount = Finite(Cracks.CrackChamferAmount, Defaults.CrackChamferAmount);
		Out.ChamferEdge = Finite(Cracks.CrackChamferEdge, Defaults.CrackChamferEdge);
		if (bCacheLayers)
		{
			MixtormatComposeHash::FHasher Hasher;
			// The chamfer and the height scale read the cached field; they do not change it.
			Hasher.SkipTopLevel = {TEXT("CrackChamferAmount"), TEXT("CrackChamferEdge"),
				TEXT("CrackHeightScale"), TEXT("bCrackNormalizeHeight")};
			Hasher.Struct(FMixtormatCracks::StaticStruct(), &Cracks);
			// Combined height and negative delta are cached separately; invalidate the old field layout.
			Out.FieldKey = MixtormatComposeHash::Combine(Hasher.Get(), 0x437261636B7333ull) | 1ull;
		}
		break;
	}
	case EMixtormatGeneratorType::RockFormation:
	{
		// Non-finite values fall back to defaults; lattice counts and Jag Scale have safety floors.
		const FMixtormatRockFormation& Rock = Generator.RockFormation;
		const FMixtormatRockFormation Defaults;
		FRockFormationRenderData& Out = ChildData.Generator.RockFormation;
		const auto Finite = [](const float Value, const float Fallback)
		{
			return FMath::IsFinite(Value) ? Value : Fallback;
		};
		ChildData.Generator.bNormalizeHeight = Rock.bRockNormalizeHeight;
		ChildData.Generator.HeightScale = Finite(Rock.RockHeightScale, 1.0f);
		Out.Style = Finite(Rock.RockStyle, Defaults.RockStyle);
		// A cell count of zero or less has no lattice to evaluate.
		Out.Cells = FMath::Max(Rock.RockCells, 1);
		Out.Rows = FMath::Max(Rock.RockRows, 1);
		Out.Seed = static_cast<uint32>(Rock.RockSeed);
		Out.Fracture = Finite(Rock.RockFracture, Defaults.RockFracture);
		Out.Chamfer = Finite(Rock.RockChamfer, Defaults.RockChamfer);
		Out.FractureHeightBias = Finite(Rock.RockFractureHeightBias, Defaults.RockFractureHeightBias);
		Out.Gap = Finite(Rock.RockGap, Defaults.RockGap);
		Out.ChamferRandom = Finite(Rock.RockChamferRandom, Defaults.RockChamferRandom);
		Out.Spin = Finite(Rock.RockSpin, Defaults.RockSpin);
		Out.SpinRandom = Finite(Rock.RockSpinRandom, Defaults.RockSpinRandom);
		Out.TiltAngle = Finite(Rock.RockTiltAngle, Defaults.RockTiltAngle);
		Out.TiltDirection = Finite(Rock.RockTiltDirection, Defaults.RockTiltDirection);
		Out.TiltRandom = Finite(Rock.RockTiltRandom, Defaults.RockTiltRandom);
		Out.SizeRandom = Finite(Rock.RockSizeRandom, Defaults.RockSizeRandom);
		Out.Stretch = Finite(Rock.RockStretch, Defaults.RockStretch);
		Out.StretchAngle = Finite(Rock.RockStretchAngle, Defaults.RockStretchAngle);
		Out.StretchRandom = Finite(Rock.RockStretchRandom, Defaults.RockStretchRandom);
		Out.HeightClusters = Finite(Rock.RockHeightClusters, Defaults.RockHeightClusters);
		Out.Skew = Finite(Rock.RockSkew, Defaults.RockSkew);
		Out.EdgeJag = Finite(Rock.RockEdgeJag, Defaults.RockEdgeJag);
		// Sub-unit frequency inflates inverse jag amplitude and the per-pixel field search reach.
		Out.JagScale = FMath::Max(Finite(Rock.RockJagScale, Defaults.RockJagScale), 1.0f);
		Out.JagDetail = Finite(Rock.RockJagDetail, Defaults.RockJagDetail);
		Out.ChamferJag = Finite(Rock.RockChamferJag, Defaults.RockChamferJag);
		Out.RimChips = Finite(Rock.RockRimChips, Defaults.RockRimChips);
		Out.RimChipSize = Finite(Rock.RockRimChipSize, Defaults.RockRimChipSize);
		Out.FacetChips = Finite(Rock.RockFacetChips, Defaults.RockFacetChips);
		Out.FacetIterations = Rock.RockFacetIterations;
		Out.FacetFalloff = Finite(Rock.RockFacetFalloff, Defaults.RockFacetFalloff);
		Out.FacetRandom = Finite(Rock.RockFacetRandom, Defaults.RockFacetRandom);
		Out.FacetAlign = Finite(Rock.RockFacetAlign, Defaults.RockFacetAlign);
		Out.DepthMin = Finite(Rock.RockDepthMin, Defaults.RockDepthMin);
		Out.DepthMax = Finite(Rock.RockDepthMax, Defaults.RockDepthMax);
		if (bCacheLayers)
		{
			MixtormatComposeHash::FHasher Hasher;
			// Height mode and scale consume the cached field; they do not reshape it.
			Hasher.SkipTopLevel = {TEXT("RockHeightScale"), TEXT("RockHeightMode"), TEXT("bRockNormalizeHeight"), TEXT("RockDepthMin"), TEXT("RockDepthMax")};
			Hasher.Struct(FMixtormatRockFormation::StaticStruct(), &Rock);
			// Signed gap now expands the cached outline as well as the height edge planes.
			Out.FieldKey = MixtormatComposeHash::Combine(Hasher.Get(), 0x526F636B47617032ull) | 1ull;
		}
		break;
	}
	case EMixtormatGeneratorType::Pebbles:
	{
		// Unclamped: ranges are authored in the tool. Non-finite falls back to the default.
		const FMixtormatPebbles& Pebbles = Generator.Pebbles;
		const FMixtormatPebbles Defaults;
		FPebblesRenderData& Out = ChildData.Generator.Pebbles;
		const auto Finite = [](const float Value, const float Fallback)
		{
			return FMath::IsFinite(Value) ? Value : Fallback;
		};
		ChildData.Generator.bNormalizeHeight = Pebbles.bPebbleNormalizeHeight;
		ChildData.Generator.HeightScale = Finite(Pebbles.PebbleHeightScale, 1.0f);
		Out.Seed = Pebbles.PebbleSeed;
		// No lattice below one cell; no shape below two planes.
		Out.Cells = FMath::Max(Pebbles.PebbleCells, 1);
		Out.Cuts = FMath::Max(Pebbles.PebbleCuts, 2);
		Out.Density = Finite(Pebbles.PebbleDensity, Defaults.PebbleDensity);
		Out.Jitter = Finite(Pebbles.PebbleJitter, Defaults.PebbleJitter);
		Out.Scale = Finite(Pebbles.PebbleScale, Defaults.PebbleScale);
		Out.ScaleVariation = Finite(Pebbles.PebbleScaleVariation, Defaults.PebbleScaleVariation);
		Out.Rotation = Finite(Pebbles.PebbleRotation, Defaults.PebbleRotation);
		Out.Direction = static_cast<int32>(Pebbles.PebbleDirection);
		Out.Irregularity = Finite(Pebbles.PebbleIrregularity, Defaults.PebbleIrregularity);
		Out.Chamfer = Finite(Pebbles.PebbleChamfer, Defaults.PebbleChamfer);
		Out.Steepness = Finite(Pebbles.PebbleSteepness, Defaults.PebbleSteepness);
		Out.SteepnessVariation = Finite(Pebbles.PebbleSteepnessVariation, Defaults.PebbleSteepnessVariation);
		Out.BiasVariation = Finite(Pebbles.PebbleBiasVariation, Defaults.PebbleBiasVariation);
		Out.HeightGain = Finite(Pebbles.PebbleHeightGain, Defaults.PebbleHeightGain);
		Out.HeightVariation = Finite(Pebbles.PebbleHeightVariation, Defaults.PebbleHeightVariation);
		Out.bFacetIds = Pebbles.bPebbleFacetIds;
		if (bCacheLayers)
		{
			MixtormatComposeHash::FHasher Hasher;
			Hasher.SkipTopLevel = {TEXT("PebbleHeightScale"), TEXT("bPebbleNormalizeHeight")};
			Hasher.Struct(FMixtormatPebbles::StaticStruct(), &Pebbles);
			Out.FieldKey = Hasher.Get() | 1ull;
		}
		break;
	}

	case EMixtormatGeneratorType::CliffStrata:
	{
		const FMixtormatCliffStrata& Cliff = Generator.CliffStrata;
		const FMixtormatCliffStrata Defaults;
		FCliffStrataRenderData& Out = ChildData.Generator.CliffStrata;
		const auto Finite = [](const float Value, const float Fallback) { return FMath::IsFinite(Value) ? Value : Fallback; };
		ChildData.Generator.bNormalizeHeight = Cliff.bCliffNormalizeHeight;
		ChildData.Generator.HeightScale = Finite(Cliff.CliffHeightScale, 1.0f);
		Out.CountX=FMath::Max(Cliff.CountX,1); Out.CountY=FMath::Max(Cliff.CountY,1);
		Out.Density=Finite(Cliff.Density,Defaults.Density); Out.SizeMin=Finite(Cliff.SizeMin,Defaults.SizeMin); Out.SizeMax=Finite(Cliff.SizeMax,Defaults.SizeMax);
		Out.SizeAspect=Finite(Cliff.SizeAspect,Defaults.SizeAspect); Out.Jitter=Finite(Cliff.Jitter,Defaults.Jitter); Out.FlowVariation=Finite(Cliff.FlowVariation,Defaults.FlowVariation);
		Out.HeightMin=Finite(Cliff.HeightMin,Defaults.HeightMin); Out.HeightMax=Finite(Cliff.HeightMax,Defaults.HeightMax); Out.Steps=FMath::Max(Cliff.Steps,0);
		Out.Rotation=Finite(Cliff.Rotation,Defaults.Rotation); Out.LeanX=Finite(Cliff.LeanX,Defaults.LeanX); Out.LeanY=Finite(Cliff.LeanY,Defaults.LeanY);
		Out.FormationCells=FMath::Max(Cliff.FormationCells,1); Out.FormationAmount=Finite(Cliff.FormationAmount,Defaults.FormationAmount);
		Out.bQuarterCopies=Cliff.bQuarterCopies; Out.QuarterYCount=FMath::Max(Cliff.QuarterYCount,1); Out.QuarterFill=Finite(Cliff.QuarterFill,Defaults.QuarterFill);
		Out.QuarterSize=Finite(Cliff.QuarterSize,Defaults.QuarterSize); Out.QuarterHeight=Finite(Cliff.QuarterHeight,Defaults.QuarterHeight);
		Out.QuarterJitterX=Finite(Cliff.QuarterJitterX,Defaults.QuarterJitterX); Out.QuarterJitterY=Finite(Cliff.QuarterJitterY,Defaults.QuarterJitterY);
		Out.Sides=FMath::Clamp(Cliff.Sides,3,12); Out.bShapeRandom=Cliff.bShapeRandom; Out.CameraYaw=Finite(Cliff.CameraYaw,Defaults.CameraYaw);
		Out.CameraPitch=Finite(Cliff.CameraPitch,Defaults.CameraPitch); Out.ViewScale=FMath::Max(1.0f,Finite(Cliff.ViewScale,Defaults.ViewScale));
		Out.DepthMin=Finite(Cliff.DepthMin,Defaults.DepthMin); Out.DepthMax=Finite(Cliff.DepthMax,Defaults.DepthMax);
		Out.UnitDistance=Finite(Cliff.UnitDistance,Defaults.UnitDistance); Out.UnitDistanceIdLerp=Finite(Cliff.UnitDistanceIdLerp,Defaults.UnitDistanceIdLerp);
		Out.CarveDepth=Finite(Cliff.CarveDepth,Defaults.CarveDepth); Out.CarveVoronoi=Finite(Cliff.CarveVoronoi,Defaults.CarveVoronoi);
		Out.YBias=Finite(Cliff.YBias,Defaults.YBias); Out.YBiasVoronoi=Finite(Cliff.YBiasVoronoi,Defaults.YBiasVoronoi); Out.bYBiasVoronoiInvert=Cliff.bYBiasVoronoiInvert;
		Out.NegativeYUnitDistanceTaper=Finite(Cliff.NegativeYUnitDistanceTaper,Defaults.NegativeYUnitDistanceTaper); Out.bReverse=Cliff.bReverse;
		Out.Seed=Cliff.Seed; Out.VoronoiCells=FMath::Max(Cliff.VoronoiCells,1); Out.FlowVoronoi=Finite(Cliff.FlowVoronoi,Defaults.FlowVoronoi);
		Out.ChamferMode = static_cast<uint32>(Cliff.ChamferMode);
		Out.BoxChamferWidth = FMath::Clamp(Finite(Cliff.BoxChamferWidth, Defaults.BoxChamferWidth), 0.0f, 1.0f);
		Out.ChamferWidth=Finite(Cliff.ChamferWidth,Defaults.ChamferWidth); Out.ChamferIntensity=Finite(Cliff.ChamferIntensity,Defaults.ChamferIntensity);
		Out.ChamferVoronoi=Finite(Cliff.ChamferVoronoi,Defaults.ChamferVoronoi); Out.BlockCavityWidth=Finite(Cliff.BlockCavityWidth,Defaults.BlockCavityWidth);
		Out.RowCavityWidth=Finite(Cliff.RowCavityWidth,Defaults.RowCavityWidth); Out.CavityIntensity=Finite(Cliff.CavityIntensity,Defaults.CavityIntensity);
		Out.CavityVoronoiThreshold=Finite(Cliff.CavityVoronoiThreshold,Defaults.CavityVoronoiThreshold); Out.CavityVoronoiMaskGain=Finite(Cliff.CavityVoronoiMaskGain,Defaults.CavityVoronoiMaskGain);
		if(bCacheLayers){MixtormatComposeHash::FHasher Hasher;Hasher.SkipTopLevel={TEXT("bCliffNormalizeHeight"),TEXT("CliffHeightScale")};Hasher.Struct(FMixtormatCliffStrata::StaticStruct(),&Cliff);Out.FieldKey=MixtormatComposeHash::Combine(Hasher.Get(),0x436C696666537472ull)|1ull;}
		break;
	}
	case EMixtormatGeneratorType::BooleanOps:
	{
		const FMixtormatBooleanOps& B = Generator.BooleanOps;
		const FMixtormatBooleanOps Defaults;
		FBooleanOpsRenderData& Out = ChildData.Generator.BooleanOps;
		const auto Finite = [](float V, float Fallback) { return FMath::IsFinite(V) ? V : Fallback; };
		ChildData.Generator.bNormalizeHeight = false;
		ChildData.Generator.HeightScale = 1.0f;
        Out.Mode = static_cast<int32>(B.Mode);
        Out.FieldOperation = static_cast<int32>(B.FieldOperation);
        Out.FieldWidth = FMath::Max(0.0f, Finite(B.FieldWidth, Defaults.FieldWidth));
        Out.FieldBias = Finite(B.FieldBias, Defaults.FieldBias);
        Out.FieldSourceScale = Finite(B.FieldSourceScale, Defaults.FieldSourceScale);
        Out.FieldAmount = FMath::Clamp(Finite(B.FieldAmount, Defaults.FieldAmount), 0.0f, 1.0f);
        Out.SourceChildIndex = INDEX_NONE;
        if (B.SourceChildId.IsValid())
        {
            for (int32 Index = 0; Index < SourceChildIndex && Index < Layer.Children.Num(); ++Index)
            {
                const FMixtormatLayerChild& Candidate = Layer.Children[Index];
                if (Candidate.ChildId == B.SourceChildId
                    && Candidate.Type == EMixtormatLayerChildType::Generator
                    && Candidate.Generator.bEnabled
                    && MixtormatChildScope::ResolveOwnerIndex(Layer.Children, Index) == INDEX_NONE
                    && (Candidate.Generator.Type != EMixtormatGeneratorType::Noise
                        || Candidate.Generator.Noise.bNoiseWriteHeight))
                {
                    Out.SourceChildIndex = Index;
                    break;
                }
            }
        }
        Out.Operation = static_cast<int32>(B.Operation);
		Out.Shape = static_cast<int32>(B.Shape);
		Out.Seed = B.Seed;
		Out.Count = B.Count;
		Out.Radius = Finite(B.Radius, Defaults.Radius);
		Out.RadiusVariation = Finite(B.RadiusVariation, Defaults.RadiusVariation);
		Out.CenterDepth = Finite(B.CenterDepth, Defaults.CenterDepth);
		Out.DepthVariation = Finite(B.DepthVariation, Defaults.DepthVariation);
		Out.ReferenceHeight = Finite(B.ReferenceHeight, Defaults.ReferenceHeight);
		Out.DepthScale = Finite(B.DepthScale, Defaults.DepthScale);
		Out.Sides = B.Sides;
		Out.Irregularity = Finite(B.Irregularity, Defaults.Irregularity);
		Out.Rotation = Finite(B.Rotation, Defaults.Rotation);
		Out.RotationVariation = Finite(B.RotationVariation, Defaults.RotationVariation);
		Out.ChamferFraction = Finite(B.ChamferFraction, Defaults.ChamferFraction);
		Out.ChamferVariation = Finite(B.ChamferVariation, Defaults.ChamferVariation);
		Out.HeightToUV = Finite(B.HeightToUV, Defaults.HeightToUV);
		Out.Count = FMath::Clamp(Out.Count, 0, 4096);
		Out.Sides = FMath::Clamp(Out.Sides, 3, 32);
		Out.Radius = FMath::Max(Out.Radius, 0.0f);
		Out.DepthScale = FMath::Max(Out.DepthScale, 0.0f);
		Out.HeightToUV = FMath::Max(Out.HeightToUV, 0.00001f);
		break;
	}
	case EMixtormatGeneratorType::Moss:
	{
		FMossRenderData& Out = ChildData.Generator.Moss;
		Out.Settings = Generator.Moss;
		FMixtormatMoss& S = Out.Settings;
		const FMixtormatMoss Defaults;
		const auto Finite = [](float V, float D) { return FMath::IsFinite(V) ? V : D; };
		// Artist maxima belong to the UI. Retain finite values and valid simulation dimensions/rates.
		S.GrowthIterations = FMath::Max(S.GrowthIterations, 0);
		S.SimulationResolution = FMath::Max(S.SimulationResolution, 1);

		S.Preview = static_cast<EMixtormatMossPreview>(FMath::Clamp(static_cast<int32>(S.Preview),
			static_cast<int32>(EMixtormatMossPreview::Moss), static_cast<int32>(EMixtormatMossPreview::Biomass)));
		S.CavityAttraction = Finite(S.CavityAttraction, Defaults.CavityAttraction);
		S.OcclusionInfluence = Finite(S.OcclusionInfluence, Defaults.OcclusionInfluence);
		S.Moisture = Finite(S.Moisture, Defaults.Moisture);
		S.SurfaceAnglePreference = Finite(S.SurfaceAnglePreference, Defaults.SurfaceAnglePreference);
		S.GravityAngle = Finite(S.GravityAngle, Defaults.GravityAngle);
		S.ExposureTolerance = Finite(S.ExposureTolerance, Defaults.ExposureTolerance);
		S.ColonyDensity = FMath::Max(S.ColonyDensity, 1);
		S.GrowthSpread = Finite(S.GrowthSpread, Defaults.GrowthSpread);

		S.Coverage = Finite(S.Coverage, Defaults.Coverage);
		S.CushionSize = Finite(S.CushionSize, Defaults.CushionSize);
		S.CushionHeight = Finite(S.CushionHeight, Defaults.CushionHeight);

		S.MatureHeight = Finite(S.MatureHeight, Defaults.MatureHeight);
		S.ClumpDensity = Finite(S.ClumpDensity, Defaults.ClumpDensity);
		S.ShootDensity = Finite(S.ShootDensity, Defaults.ShootDensity);
		S.ShootLength = Finite(S.ShootLength, Defaults.ShootLength);
		S.FiberThickness = Finite(S.FiberThickness, Defaults.FiberThickness);

		S.Droop = Finite(S.Droop, Defaults.Droop);
		S.HeightScale = Finite(S.HeightScale, Defaults.HeightScale);

		S.SeedMaskTiling = FMath::Max(S.SeedMaskTiling, 1);
		// Same direct-texture/asset precedence as Peeling. Resolve before hashing so the
		// live asset texture, streaming state and reimports participate in both cache keys.
		UTexture2D* SeedTexture = S.SeedMaskTexture.LoadSynchronous();
		if (!SeedTexture)
		{
			if (const UMixtormatMask* Mask = S.SeedMask.LoadSynchronous())
			{
				SeedTexture = Mask->MaskTexture.Get();
			}
		}
		if (SeedTexture)
		{
			Out.SeedMask = GetTextureRHI(SeedTexture);
		}
		ChildData.Generator.bNormalizeHeight = S.bNormalizeHeight;
		ChildData.Generator.HeightScale = S.HeightScale;
		if (bCacheLayers)
		{
			MixtormatComposeHash::FHasher DependencyHasher;
			DependencyHasher.Value(static_cast<uint64>(LayerIndex));
			DependencyHasher.Value(static_cast<uint64>(SourceChildIndex));
			// Full reflection includes referenced assets affecting the composed substrate below.
			for (int32 BelowLayerIndex = 0; BelowLayerIndex < LayerIndex; ++BelowLayerIndex)
			{
				DependencyHasher.Struct(FMixtormatLayer::StaticStruct(), &EffectiveLayers[BelowLayerIndex]);
			}
			// Include current-layer maps and placement, but never this Moss child's own settings.
			DependencyHasher.SkipTopLevel = {GET_MEMBER_NAME_CHECKED(FMixtormatLayer, Children)};
			DependencyHasher.Struct(FMixtormatLayer::StaticStruct(), &Layer);
			// SkipTopLevel persists across Struct calls; children must be reflected in full.
			DependencyHasher.SkipTopLevel.Reset();
			for (int32 ChildIndex = 0; ChildIndex < Layer.Children.Num(); ++ChildIndex)
			{
				if (ChildIndex != SourceChildIndex)
				{
					// Conservative: scope effects can occur anywhere; later children may cause cache misses.
					DependencyHasher.Struct(FMixtormatLayerChild::StaticStruct(), &Layer.Children[ChildIndex]);
				}
			}
			const uint64 DependencyKey = DependencyHasher.Get();

			MixtormatComposeHash::FHasher FieldHasher;
			// Include output controls in the final-field identity; chemistry has a separate key.
			FieldHasher.Struct(FMixtormatMoss::StaticStruct(), &S);
			Out.FieldKey = MixtormatComposeHash::Combine(
				MixtormatComposeHash::Combine(FieldHasher.Get(), DependencyKey), 0x4d6f737347723134ull) | 1ull;

			MixtormatComposeHash::FHasher SimulationHasher;
			SimulationHasher.SkipTopLevel = {
				TEXT("HeightScale"), TEXT("bNormalizeHeight"), TEXT("Coverage"), TEXT("CushionSize"),
				TEXT("CushionHeight"), TEXT("MatureHeight"), TEXT("ClumpDensity"), TEXT("ShootDensity"), TEXT("ShootLength"),
				TEXT("FiberThickness"),
				TEXT("Droop"), TEXT("Preview")};
			SimulationHasher.Struct(FMixtormatMoss::StaticStruct(), &S);
			// Placement is already covered by the current-layer dependency hash.
			Out.SimulationKey = MixtormatComposeHash::Combine(
				MixtormatComposeHash::Combine(SimulationHasher.Get(), DependencyKey), 0x4d6f737353696d37ull) | 1ull;
		}
		break;
	}
	case EMixtormatGeneratorType::Mycelium:
	{
		FMyceliumRenderData& Out = ChildData.Generator.Mycelium;
		Out.Settings = Generator.Mycelium;
		FMixtormatMycelium& S = Out.Settings;
		const FMixtormatMycelium Defaults;
		const auto Finite = [](float V, float D) { return FMath::IsFinite(V) ? V : D; };
		S.ColonyCount = FMath::Clamp(S.ColonyCount, 1, 32);
		S.GrowthExtent = FMath::Clamp(Finite(S.GrowthExtent, Defaults.GrowthExtent), 0.0f, 1.0f);
		S.Coverage = FMath::Clamp(Finite(S.Coverage, Defaults.Coverage), 0.0f, 1.0f);
		S.NutrientAttraction = FMath::Clamp(Finite(S.NutrientAttraction, Defaults.NutrientAttraction), 0.0f, 1.0f);
		S.BranchingFrequency = FMath::Clamp(Finite(S.BranchingFrequency, Defaults.BranchingFrequency), 0.0f, 1.0f);
		S.DirectionalPersistence = FMath::Clamp(Finite(S.DirectionalPersistence, Defaults.DirectionalPersistence), 0.0f, 1.0f);
		S.NetworkConnectivity = FMath::Clamp(Finite(S.NetworkConnectivity, Defaults.NetworkConnectivity), 0.0f, 1.0f);
		S.SecondaryBranchDensity = FMath::Clamp(Finite(S.SecondaryBranchDensity, Defaults.SecondaryBranchDensity), 0.0f, 1.0f);
		S.MicrofilamentDensity = FMath::Clamp(Finite(S.MicrofilamentDensity, Defaults.MicrofilamentDensity), 0.0f, 1.0f);
		S.FilamentWidth = FMath::Clamp(Finite(S.FilamentWidth, Defaults.FilamentWidth), 0.001f, 0.25f);
		S.Taper = FMath::Clamp(Finite(S.Taper, Defaults.Taper), 0.0f, 1.0f);
		ChildData.Generator.bNormalizeHeight = S.bNormalizeHeight;
		ChildData.Generator.HeightScale = Finite(S.HeightScale, Defaults.HeightScale);
		if (bCacheLayers)
		{
			MixtormatComposeHash::FHasher Hasher;
			Hasher.SkipTopLevel = {TEXT("HeightScale"), TEXT("bNormalizeHeight")};
			Hasher.Struct(FMixtormatMycelium::StaticStruct(), &S);
			Out.FieldKey = MixtormatComposeHash::Combine(Hasher.Get(), 0x4d79636547723032ull) | 1ull;
		}
		break;
	}
	case EMixtormatGeneratorType::Scales:
	{
		FScalesRenderData& Out = ChildData.Generator.Scales;
		Out.Settings = Generator.Scales;
		FMixtormatScales& S = Out.Settings;
		const FMixtormatScales Defaults;
		// Sanitize non-finite inputs without imposing the inspector's artistic ranges.
		const auto Finite = [](float V, float D) { return FMath::IsFinite(V) ? V : D; };
		S.CountX = FMath::Max(S.CountX, 1);
		S.CountY = FMath::Max(S.CountY, 1);
		S.Width = FMath::Max(Finite(S.Width, Defaults.Width), 0.001f);
		S.Length = FMath::Max(Finite(S.Length, Defaults.Length), 0.001f);
		S.Stagger = Finite(S.Stagger, Defaults.Stagger);
		S.Direction = Finite(S.Direction, Defaults.Direction);
		S.TipRoundness = Finite(S.TipRoundness, Defaults.TipRoundness);
		S.Taper = Finite(S.Taper, Defaults.Taper);
		S.Overlap = Finite(S.Overlap, Defaults.Overlap);
		S.TipLift = Finite(S.TipLift, Defaults.TipLift);
		S.RootTuck = Finite(S.RootTuck, Defaults.RootTuck);
		S.Thickness = Finite(S.Thickness, Defaults.Thickness);
		S.Dome = Finite(S.Dome, Defaults.Dome);
		S.Ridge = Finite(S.Ridge, Defaults.Ridge);
		S.Bevel = Finite(S.Bevel, Defaults.Bevel);
		S.SizeRandom = Finite(S.SizeRandom, Defaults.SizeRandom);
		S.RotationRandom = Finite(S.RotationRandom, Defaults.RotationRandom);
		S.HeightRandom = Finite(S.HeightRandom, Defaults.HeightRandom);
		S.RowJitter = Finite(S.RowJitter, Defaults.RowJitter);
		S.GrooveAmount = Finite(S.GrooveAmount, Defaults.GrooveAmount);
		S.GrooveCount = Finite(S.GrooveCount, Defaults.GrooveCount);
		S.GrooveDirection = Finite(S.GrooveDirection, Defaults.GrooveDirection);
		ChildData.Generator.bNormalizeHeight = S.bNormalizeHeight;
		ChildData.Generator.HeightScale = Finite(S.HeightScale, Defaults.HeightScale);
		if (bCacheLayers)
		{
			MixtormatComposeHash::FHasher Hasher;
			Hasher.SkipTopLevel = {TEXT("HeightScale"), TEXT("bNormalizeHeight")};
			Hasher.Struct(FMixtormatScales::StaticStruct(), &S);
			Out.FieldKey = MixtormatComposeHash::Combine(Hasher.Get(), 0x5363616C657331ull) | 1ull;
		}
		break;
	}
	case EMixtormatGeneratorType::RootNetwork:
	{
		const FMixtormatRootNetwork& Root = Generator.RootNetwork;
		const FMixtormatRootNetwork Defaults;
		FRootNetworkRenderData& Out = ChildData.Generator.RootNetwork;
		const auto Finite = [](const float Value, const float Fallback)
		{
			return FMath::IsFinite(Value) ? Value : Fallback;
		};
		ChildData.Generator.bNormalizeHeight = Root.bRootNormalizeHeight;
		ChildData.Generator.HeightScale = Finite(Root.RootHeightScale, Defaults.RootHeightScale);
		Out.Seed = static_cast<uint32>(FMath::Max(Root.RootSeed, 0));
		Out.Trunks = FMath::Clamp(Root.RootTrunks, 1, 128);
		Out.Attractors = FMath::Clamp(Root.RootAttractors, 16, 4096);
		Out.Iterations = FMath::Clamp(Root.RootIterations, 1, 256);
		Out.StepLength = FMath::Max(Finite(Root.RootStepLength, Defaults.RootStepLength), 0.0001f);
		Out.InfluenceRadius = FMath::Max(Finite(Root.RootInfluenceRadius, Defaults.RootInfluenceRadius), 0.0001f);
		Out.KillDistance = FMath::Max(Finite(Root.RootKillDistance, Defaults.RootKillDistance), 0.0001f);
		Out.BranchSpread = Finite(Root.RootBranchSpread, Defaults.RootBranchSpread);
		Out.DirectionInertia = Finite(Root.RootDirectionInertia, Defaults.RootDirectionInertia);
		Out.CurlStrength = Finite(Root.RootCurlStrength, Defaults.RootCurlStrength);
		Out.CurlScale = FMath::Max(Root.RootCurlScale, 1);
		Out.BaseRadius = FMath::Max(Finite(Root.RootBaseRadius, Defaults.RootBaseRadius), 0.0001f);
		Out.TipTaper = Finite(Root.RootTipTaper, Defaults.RootTipTaper);
		Out.JunctionSmooth = FMath::Max(Finite(Root.RootJunctionSmooth, Defaults.RootJunctionSmooth), 0.0f);
		Out.ProfileFlatness = Finite(Root.RootProfileFlatness, Defaults.RootProfileFlatness);
		Out.WeaveAmount = Finite(Root.RootWeaveAmount, Defaults.RootWeaveAmount);
		Out.Kink = Finite(Root.RootKink, Defaults.RootKink);
		Out.ProfileBias = Finite(Root.RootProfileBias, Defaults.RootProfileBias);
		Out.Burial = Finite(Root.RootBurial, Defaults.RootBurial);
		Out.AzimuthAngle = Finite(Root.RootAzimuthAngle, Defaults.RootAzimuthAngle);
		Out.AzimuthStrength = Finite(Root.RootAzimuthStrength, Defaults.RootAzimuthStrength);
		Out.SwirlStrength = Finite(Root.RootSwirlStrength, Defaults.RootSwirlStrength);
		Out.AxisRatio = FMath::Max(Finite(Root.RootAxisRatio, Defaults.RootAxisRatio), 0.01f);
		Out.bUseRadiusCurve = Root.bUseRadiusCurve;
		const MixtormatScalarRampMath::FGpuPayload RadiusPayload =
			MixtormatScalarRampMath::PrepareGpuPayload(Root.RadiusCurve);
		Out.RadiusCurveCount = RadiusPayload.PointCount;
		Out.RadiusCurveInterpolation = RadiusPayload.Interpolation;
		for (int32 Index = 0; Index < FMixtormatScalarRamp::MaxPoints; ++Index)
		{
			Out.RadiusCurvePoints[Index] = RadiusPayload.Points[Index];
		}
		Out.bUseBurialCurve = Root.bUseBurialCurve;
		const MixtormatScalarRampMath::FGpuPayload BurialPayload =
			MixtormatScalarRampMath::PrepareGpuPayload(Root.BurialCurve);
		Out.BurialCurveCount = BurialPayload.PointCount;
		Out.BurialCurveInterpolation = BurialPayload.Interpolation;
		for (int32 Index = 0; Index < FMixtormatScalarRamp::MaxPoints; ++Index)
		{
			Out.BurialCurvePoints[Index] = BurialPayload.Points[Index];
		}
		Out.RootUVScaleU = Finite(Root.RootUVScaleU, Defaults.RootUVScaleU);
		Out.RootUVScaleV = Finite(Root.RootUVScaleV, Defaults.RootUVScaleV);
		Out.RootUVOffsetU = Finite(Root.RootUVOffsetU, Defaults.RootUVOffsetU);
		Out.RootUVOffsetV = Finite(Root.RootUVOffsetV, Defaults.RootUVOffsetV);
		if (bCacheLayers)
		{
			MixtormatComposeHash::FHasher Hasher;
			Hasher.SkipTopLevel = {TEXT("RootHeightScale"), TEXT("bRootNormalizeHeight")};
			Hasher.Struct(FMixtormatRootNetwork::StaticStruct(), &Root);
			Out.FieldKey = MixtormatComposeHash::Combine(Hasher.Get(), 0x526F6F744E657431ull) | 1ull;
		}
		break;
	}
	case EMixtormatGeneratorType::Noise:
	{
		// A field producer: the gather only resolves the settings and hands them to the pass.
		// Non-finite falls back to the default; Scale and Lacunarity have safety floors, because
		// a lattice that does not close on the tile cannot be wrapped.
		const FMixtormatNoise& Noise = Generator.Noise;
		const FMixtormatNoise Defaults;
		const auto Finite = [](const float Value, const float Fallback)
		{
			return FMath::IsFinite(Value) ? Value : Fallback;
		};
		ChildData.Generator.bNormalizeHeight = Noise.bNoiseNormalizeHeight;
		ChildData.Generator.HeightScale = Finite(Noise.NoiseHeightScale, Defaults.NoiseHeightScale);
		ChildData.Generator.HeightBias = FMath::Clamp(
			Finite(Noise.NoiseHeightBias, Defaults.NoiseHeightBias), -1.0f, 1.0f);

		const FMixtormatNoiseRenderData Out = ResolveNoiseRenderData(Noise);
		// The settings ride the store to the pass; see MixtormatNoiseRender.h for why.
		MixtormatNoiseRenderStore().Set(FMixtormatNoiseRenderKey{Data.LayerId, SourceChildIndex}, Out);
		break;
	}
	}
	// Native Flow settings do not belong to the geometry cache keys.
	if (Generator.Type != EMixtormatGeneratorType::Noise
		&& Generator.Type != EMixtormatGeneratorType::BooleanOps
		&& Generator.Type != EMixtormatGeneratorType::StrataCarver)
	{
		const FMixtormatNativeFlowSettings Defaults;
		const FMixtormatNativeFlowSettings& F = Generator.NativeFlow;
		FNativeFlowRenderData& Out = ChildData.Generator.NativeFlow;
		const auto Finite = [](float V, float Default) { return FMath::IsFinite(V) ? V : Default; };
		Out.Amount = FMath::Clamp(Finite(F.Amount, Defaults.Amount), 0.0f, 2.0f);
		Out.Angle = FMath::Clamp(Finite(F.Angle, Defaults.Angle), -180.0f, 180.0f);
		Out.SlopeInfluence = FMath::Clamp(Finite(F.SlopeInfluence, Defaults.SlopeInfluence), 0.0f, 1.0f);
		Out.BoundaryInfluence = FMath::Clamp(Finite(F.BoundaryInfluence, Defaults.BoundaryInfluence), 0.0f, 1.0f);
		Out.GravityInfluence = FMath::Clamp(Finite(F.GravityInfluence, Defaults.GravityInfluence), 0.0f, 1.0f);
		Out.GravityAngle = FMath::Clamp(Finite(F.GravityAngle, Defaults.GravityAngle), -180.0f, 180.0f);
	}
	if (PlacementKey != 0)
	{
		switch (Generator.Type)
		{
		case EMixtormatGeneratorType::RockFormation:
			if (ChildData.Generator.RockFormation.FieldKey != 0)
			{
				ChildData.Generator.RockFormation.FieldKey = MixtormatComposeHash::Combine(
					ChildData.Generator.RockFormation.FieldKey, PlacementKey) | 1ull;
			}
			break;
		case EMixtormatGeneratorType::Pebbles:
			if (ChildData.Generator.Pebbles.FieldKey != 0)
			{
				ChildData.Generator.Pebbles.FieldKey = MixtormatComposeHash::Combine(
					ChildData.Generator.Pebbles.FieldKey, PlacementKey) | 1ull;
			}
			break;
		case EMixtormatGeneratorType::Cracks:
			if (ChildData.Generator.Cracks.FieldKey != 0)
			{
				ChildData.Generator.Cracks.FieldKey = MixtormatComposeHash::Combine(
					ChildData.Generator.Cracks.FieldKey, PlacementKey) | 1ull;
			}
			break;
		case EMixtormatGeneratorType::CliffStrata:
			if (ChildData.Generator.CliffStrata.FieldKey != 0)
			{
				ChildData.Generator.CliffStrata.FieldKey = MixtormatComposeHash::Combine(
					ChildData.Generator.CliffStrata.FieldKey, PlacementKey) | 1ull;
			}
			break;
		case EMixtormatGeneratorType::Moss:
			if (ChildData.Generator.Moss.FieldKey != 0)
			{
				ChildData.Generator.Moss.FieldKey = MixtormatComposeHash::Combine(
					ChildData.Generator.Moss.FieldKey, PlacementKey) | 1ull;
			}
			break;
		case EMixtormatGeneratorType::Mycelium:
			if (ChildData.Generator.Mycelium.FieldKey != 0)
			{
				ChildData.Generator.Mycelium.FieldKey = MixtormatComposeHash::Combine(
					ChildData.Generator.Mycelium.FieldKey, PlacementKey) | 1ull;
			}
			break;
		case EMixtormatGeneratorType::Scales:
			if (ChildData.Generator.Scales.FieldKey != 0)
			{
				ChildData.Generator.Scales.FieldKey = MixtormatComposeHash::Combine(
					ChildData.Generator.Scales.FieldKey, PlacementKey) | 1ull;
			}
			break;
		case EMixtormatGeneratorType::RootNetwork:
			if (ChildData.Generator.RootNetwork.FieldKey != 0)
			{
				ChildData.Generator.RootNetwork.FieldKey = MixtormatComposeHash::Combine(
					ChildData.Generator.RootNetwork.FieldKey, PlacementKey) | 1ull;
			}
			break;
		default:
			break;
		}
	}
}

void GatherGeneratorBehaviorChild(FLayerRenderData& Data, const FMixtormatLayer& Layer,
	const FMixtormatLayerChild& LayerChild, const int32 BehaviorChildIndex, const int32 LayerIndex,
	const TArray<FMixtormatLayer>& EffectiveLayers)
{
	// A Behavior is owned by its Generator, not run as an Effect at its own row.
	// The execution kernel is independent of the generator family.
	if (!Layer.bEnabled || !Layer.HasGeneratorModules()
		|| LayerChild.Type != EMixtormatLayerChildType::Behavior) { return; }
	const FMixtormatBehavior& Behavior = LayerChild.Behavior;
	const bool bWarp = Behavior.Type == EMixtormatBehaviorType::Warp;
	const bool bPush = Behavior.Type == EMixtormatBehaviorType::Push;
	const bool bCarve = Behavior.Type == EMixtormatBehaviorType::Carve;
	const bool bDeform = Behavior.Type == EMixtormatBehaviorType::Deform;
	const bool bFlowField = Behavior.Type == EMixtormatBehaviorType::FlowField;
	const bool bFeather = Behavior.Type == EMixtormatBehaviorType::Feather;
	const bool bTraced = bCarve || (!bWarp && Behavior.Flow.bUseTracedFlow);
	if (!Behavior.bEnabled || (!bWarp && !bPush && !bCarve && !bDeform && !bFlowField && !bFeather)
		|| (Behavior.Stage != EMixtormatBehaviorStage::PostGeneration
			&& !(Behavior.Stage == EMixtormatBehaviorStage::PreGeneration && bWarp
				&& Behavior.Direction.Origin == EMixtormatBehaviorFieldOrigin::PublishedOutput))
		|| ((bWarp || bDeform) && !bTraced && (Behavior.Direction.Origin != EMixtormatBehaviorFieldOrigin::PublishedOutput
			&& Behavior.Direction.Origin != EMixtormatBehaviorFieldOrigin::OwnNativeHeight
			&& Behavior.Direction.Origin != EMixtormatBehaviorFieldOrigin::None))
		|| ((bWarp || bDeform) && Behavior.Height.Origin != EMixtormatBehaviorFieldOrigin::None)
		|| (bPush && (Behavior.Direction.Origin != EMixtormatBehaviorFieldOrigin::None
			|| (Behavior.Height.Origin != EMixtormatBehaviorFieldOrigin::PublishedOutput
				&& Behavior.Height.Origin != EMixtormatBehaviorFieldOrigin::OwnNativeHeight
				&& Behavior.Height.Origin != EMixtormatBehaviorFieldOrigin::PreviousRunningHeight)))
		|| (bCarve && !bTraced && (Behavior.Direction.Origin != EMixtormatBehaviorFieldOrigin::None
			|| (Behavior.Height.Origin != EMixtormatBehaviorFieldOrigin::OwnBoundary
				&& Behavior.Height.Origin != EMixtormatBehaviorFieldOrigin::PublishedOutput)))
		|| !FMath::IsFinite(Behavior.Strength)) { return; }

	const MixtormatChildScope::FBehaviorInputStatus Valid =
		MixtormatChildScope::ValidateBehaviorInputs(
			EffectiveLayers, LayerIndex, BehaviorChildIndex, &Layer);
	if (!Valid.bCanEvaluate || Valid.GeneratorChildIndex == INDEX_NONE) { return; }
	const bool bPublishedDirection = Behavior.Direction.Origin == EMixtormatBehaviorFieldOrigin::PublishedOutput;
	const FMixtormatOutputReference& Reference = Behavior.Direction.Published;
	const int32 SourceIndex = !bPublishedDirection ? INDEX_NONE
		: MixtormatOutputReferences::ResolveGeneratorInputSource(
			EffectiveLayers, LayerIndex, Valid.GeneratorChildIndex, Reference);
	if (bPublishedDirection && SourceIndex == INDEX_NONE) { return; }
	FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
	ChildData.Type = EMixtormatLayerChildType::Behavior;
	ChildData.SourceChildIndex = BehaviorChildIndex;
	ChildData.ScopeOwnerSourceChildIndex = MixtormatChildScope::ResolveOwnerIndex(Layer.Children, BehaviorChildIndex);
	FBehaviorRenderData& Out = ChildData.Behavior;
	Out.Type = Behavior.Type;
	Out.bUseTracedFlow = bTraced;
	// Gather authored Behavior Flow parameters into the native render data.
	const FMixtormatBehaviorFlowSettings& Flow = Behavior.Flow;
	FBehaviorFlowRenderData& FlowOut = Out.Flow;
	FlowOut.Mode = Flow.Mode;
	FlowOut.ModeMix = FMath::IsFinite(Flow.ModeMix) ? FMath::Clamp(Flow.ModeMix, 0.0f, 1.0f) : 0.0f;
	Out.Stage = Behavior.Stage;
	// Scoped Sculpt and Traced Deform no longer read SDF steering.
	FlowOut.FlowSource = static_cast<uint32>((bCarve || (bDeform && bTraced))
		? EMixtormatBehaviorFlowSource::Height : Flow.FlowSource);
	FlowOut.FlowAmount = FMath::IsFinite(Flow.FlowAmount) ? Flow.FlowAmount : FMixtormatBehaviorFlowSettings().FlowAmount;
	FlowOut.FlowTangent = FMath::IsFinite(Flow.FlowTangent) ? Flow.FlowTangent : FMixtormatBehaviorFlowSettings().FlowTangent;
	FlowOut.FlowAngle = FMath::IsFinite(Flow.FlowAngle) ? Flow.FlowAngle : FMixtormatBehaviorFlowSettings().FlowAngle;
	FlowOut.GravitySurfaceFollow = FMath::IsFinite(Flow.GravitySurfaceFollow) ? Flow.GravitySurfaceFollow : FMixtormatBehaviorFlowSettings().GravitySurfaceFollow;
	FlowOut.GravityDeflection = FMath::IsFinite(Flow.GravityDeflection) ? Flow.GravityDeflection : FMixtormatBehaviorFlowSettings().GravityDeflection;
	FlowOut.FlowBend = FMath::IsFinite(Flow.FlowBend) ? Flow.FlowBend : FMixtormatBehaviorFlowSettings().FlowBend;
	FlowOut.FlowSeed = static_cast<uint32>(Flow.FlowSeed);
	FlowOut.FlowRadius = FMath::Max(Flow.FlowRadius, 1);
	FlowOut.FlowSmooth = FMath::IsFinite(Flow.FlowSmooth) ? Flow.FlowSmooth : FMixtormatBehaviorFlowSettings().FlowSmooth;
	FlowOut.Reach = FMath::IsFinite(Flow.Reach) ? Flow.Reach : FMixtormatBehaviorFlowSettings().Reach;
	FlowOut.Feather = FMath::IsFinite(Flow.Feather) ? Flow.Feather : FMixtormatBehaviorFlowSettings().Feather;
	FlowOut.FlowOffsetAlong = FMath::IsFinite(Flow.FlowOffsetAlong) ? Flow.FlowOffsetAlong : FMixtormatBehaviorFlowSettings().FlowOffsetAlong;
	FlowOut.FlowOffsetAcross = FMath::IsFinite(Flow.FlowOffsetAcross) ? Flow.FlowOffsetAcross : FMixtormatBehaviorFlowSettings().FlowOffsetAcross;
	FlowOut.ShapeOffset = FMath::IsFinite(Flow.ShapeOffset) ? Flow.ShapeOffset : FMixtormatBehaviorFlowSettings().ShapeOffset;
	FlowOut.Bulge = FMath::IsFinite(Flow.Bulge) ? Flow.Bulge : FMixtormatBehaviorFlowSettings().Bulge;
	FlowOut.StandardTraceLength = FMath::IsFinite(Flow.StandardTraceLength)
		? FMath::Max(Flow.StandardTraceLength, 0.0f) : 0.1f;
	FlowOut.StandardTraceSteps = FMath::Clamp(Flow.StandardTraceSteps, 1, 64);
	FlowOut.TraceLength = FMath::IsFinite(Flow.TraceLength) ? Flow.TraceLength : FMixtormatBehaviorFlowSettings().TraceLength;
	FlowOut.TraceSteps = FMath::Max(Flow.TraceSteps, 1);
	FlowOut.WarpStrength = FMath::IsFinite(Flow.WarpStrength) ? Flow.WarpStrength : FMixtormatBehaviorFlowSettings().WarpStrength;
	FlowOut.CarveMode = static_cast<uint32>(Flow.CarveMode);
	FlowOut.Depth = FMath::IsFinite(Flow.Depth) ? Flow.Depth : FMixtormatBehaviorFlowSettings().Depth;
	FlowOut.Width = FMath::IsFinite(Flow.Width) ? Flow.Width : FMixtormatBehaviorFlowSettings().Width;
	FlowOut.Falloff = FMath::IsFinite(Flow.Falloff) ? Flow.Falloff : FMixtormatBehaviorFlowSettings().Falloff;
	// Resolve drivers for FMixtormatBehaviorFlowSettings properties:
	const FName FlowSettingsProperties[9] = {
		TEXT("FlowAmount"),
		TEXT("TraceLength"),
		TEXT("WarpStrength"),
		TEXT("Depth"),
		TEXT("ShapeOffset"),
		TEXT("Bulge"),
		TEXT("Reach"),
		TEXT("Feather"),
		TEXT("ModeMix")
	};
	for (int32 Slot = 0; Slot < 9; ++Slot)
	{
		const FMixtormatParameterBinding* Binding = LayerChild.ParameterBindings.FindByPredicate(
			[&FlowSettingsProperties, Slot](const FMixtormatParameterBinding& Candidate)
			{
				return Candidate.DestinationOwner == EMixtormatParameterOwnerType::BehaviorFlowSettings
					&& Candidate.DestinationParameter == FlowSettingsProperties[Slot]
					&& Candidate.Driver.bEnabled
					&& Candidate.Driver.SourceKind == EMixtormatDriverSourceKind::CombinedMask
					&& Candidate.Driver.SourceLayerId.IsValid();
			});
		if (!Binding) { continue; }
		bool bEarlier = false;
		for (int32 Previous = 0; Previous < LayerIndex; ++Previous)
		{
			if (EffectiveLayers.IsValidIndex(Previous)
				&& EffectiveLayers[Previous].LayerId == Binding->Driver.SourceLayerId
				&& EffectiveLayers[Previous].bEnabled)
			{
				bEarlier = true;
				break;
			}
		}
		if (!bEarlier) { continue; }
		const FMixtormatParameterDriver& Authored = Binding->Driver;
		FText TransferError;
		if (!MixtormatParameterDrivers::ValidateTransfer(Authored, TransferError)) { continue; }
		FScalarDriverRenderData& Driver = FlowOut.SettingsDrivers[Slot];
		Driver.bUseTransferCurve = Authored.bUseTransferCurve;
		Driver.TransferCurve = Authored.TransferCurve;
		Driver.bEnabled = true;
		Driver.SourceLayerId = Authored.SourceLayerId;
		Driver.bInvert = Authored.bInvert;
		Driver.InputMin = Authored.InputMin;
		Driver.InputMax = Authored.InputMax;
		Driver.OutputMin = Authored.OutputMin;
		Driver.OutputMax = Authored.OutputMax;
		Driver.Amount = Authored.Amount;
		Driver.Combine = static_cast<uint32>(Authored.Combine);
	}
	Out.GeneratorChildIndex = Valid.GeneratorChildIndex;
	Out.Strength = Behavior.Strength;
	Out.GradientReach = FMath::IsFinite(Behavior.GradientReach)
		? FMath::Max(Behavior.GradientReach, 0.0f) : 0.0f;

	Out.FeatherMode = Behavior.Feather.Mode;
	Out.FeatherSide = Behavior.Feather.Side;
	// The editor's JSON authoring controls the preferred ranges and typed-value clamps.
	// Rendering retains its own finite/nonnegative guards; the pass caps iterative work at 512.
	Out.FeatherRadius = FMath::IsFinite(Behavior.Feather.Radius)
		? FMath::Max(Behavior.Feather.Radius, 0.0f) : 0.0f;
	Out.FeatherIterations = FMath::Max(Behavior.Feather.Iterations, 1);
	Out.FeatherDirectionBias = FMath::IsFinite(Behavior.Feather.DirectionBias)
		? FMath::Clamp(Behavior.Feather.DirectionBias, -1.0f, 1.0f) : 0.0f;
	Out.CarveWidth = FMath::IsFinite(Behavior.CarveWidth)
		? FMath::Max(Behavior.CarveWidth, 1e-6f) : 0.02f;
	// Resolve the canonical Behavior scalar addresses; only earlier completed
	// layer masks may drive the current shader. Unsupported drivers stay inert.
	const FName ScalarProperties[2] = { TEXT("Strength"), TEXT("GradientReach") };
	for (int32 Slot = 0; Slot < 2; ++Slot)
	{
		const FMixtormatParameterBinding* Binding = LayerChild.ParameterBindings.FindByPredicate(
			[&ScalarProperties, Slot](const FMixtormatParameterBinding& Candidate)
			{
				return Candidate.DestinationOwner == EMixtormatParameterOwnerType::Behavior
					&& Candidate.DestinationParameter == ScalarProperties[Slot]
					&& Candidate.Driver.bEnabled
					&& Candidate.Driver.SourceKind == EMixtormatDriverSourceKind::CombinedMask
					&& Candidate.Driver.SourceLayerId.IsValid();
			});
		if (!Binding) { continue; }
		bool bEarlier = false;
		for (int32 Previous = 0; Previous < LayerIndex; ++Previous)
		{
			if (EffectiveLayers[Previous].LayerId == Binding->Driver.SourceLayerId
				&& EffectiveLayers[Previous].bEnabled)
			{
				bEarlier = true;
				break;
			}
		}
		if (!bEarlier) { continue; }
		const FMixtormatParameterDriver& Authored = Binding->Driver;
		FText TransferError;
		if (!MixtormatParameterDrivers::ValidateTransfer(Authored, TransferError)) { continue; }
		FScalarDriverRenderData& Driver = Out.ScalarDrivers[Slot];
		Driver.bUseTransferCurve = Authored.bUseTransferCurve;
		Driver.TransferCurve = Authored.TransferCurve;
		Driver.bEnabled = true;
		Driver.SourceLayerId = Authored.SourceLayerId;
		Driver.bInvert = Authored.bInvert;
		Driver.InputMin = Authored.InputMin;
		Driver.InputMax = Authored.InputMax;
		Driver.OutputMin = Authored.OutputMin;
		Driver.OutputMax = Authored.OutputMax;
		Driver.Amount = Authored.Amount;
		Driver.Combine = static_cast<uint32>(Authored.Combine);
	}
	if (bFeather)
	{
		FMixtormatParameterAddress Target;
		Target.LayerId = Layer.LayerId; Target.ChildId = LayerChild.ChildId;
		Target.Owner = EMixtormatParameterOwnerType::BehaviorFeatherSettings;
		Target.Parameter = TEXT("Side"); Target.ValueType = EMixtormatParameterValueType::Enum;
		Target.TypeName = StaticEnum<EMixtormatBehaviorFeatherSide>()->GetFName();
		const FMixtormatParameterBinding* Binding = LayerChild.ParameterBindings.FindByPredicate([&Target](const auto& Candidate)
		{
			return Candidate.DestinationOwner == Target.Owner && Candidate.DestinationParameter == Target.Parameter
				&& Candidate.ValueType == Target.ValueType && Candidate.TypeName == Target.TypeName && Candidate.Driver.bEnabled
				&& !Candidate.Reference.bEnabled && Candidate.Driver.SourceKind == EMixtormatDriverSourceKind::CombinedMask;
		});
		if (Binding && MixtormatParameterDrivers::Capability(EffectiveLayers, Target).IsSupported())
		{
			FText Error;
			const auto& Authored = Binding->Driver;
			bool bEarlier = false;
			for (int32 Previous = 0; Previous < LayerIndex; ++Previous)
			{ bEarlier |= EffectiveLayers[Previous].bEnabled && EffectiveLayers[Previous].LayerId == Authored.SourceLayerId; }
			if (bEarlier && MixtormatParameterDrivers::ValidateEnumBins(StaticEnum<EMixtormatBehaviorFeatherSide>(), Authored.EnumBins, Error)
				&& MixtormatParameterDrivers::ValidateTransfer(Authored, Error))
			{
				auto& Driver = Out.FeatherSideDriver;
				Driver.bEnabled = true; Driver.bEnumBins = true; Driver.EnumBins = Authored.EnumBins;
				Driver.SourceLayerId = Authored.SourceLayerId; Driver.bInvert = Authored.bInvert;
				Driver.InputMin = Authored.InputMin; Driver.InputMax = Authored.InputMax;
				Driver.bUseTransferCurve = Authored.bUseTransferCurve; Driver.TransferCurve = Authored.TransferCurve;
			}
		}
	}

	// A zero-authored Strength remains neutral unless a valid ordered driver
	// can replace or combine it. This check must follow driver resolution.
	const bool bHasActiveFlowSettingsDriver = FlowOut.SettingsDrivers[0].bEnabled
		|| FlowOut.SettingsDrivers[1].bEnabled || FlowOut.SettingsDrivers[2].bEnabled
		|| FlowOut.SettingsDrivers[3].bEnabled || FlowOut.SettingsDrivers[4].bEnabled
		|| FlowOut.SettingsDrivers[5].bEnabled || FlowOut.SettingsDrivers[6].bEnabled
		|| FlowOut.SettingsDrivers[7].bEnabled;
	if (!bFlowField && !bTraced && Out.Strength == 0.0f && !Out.ScalarDrivers[0].bEnabled)
	{
		Data.Children.Pop();
		return;
	}
	Out.DirectionOrigin = Behavior.Direction.Origin;
	// Composition is validated by ValidateBehaviorInputs before this point, so the
	// amplitude is already finite and the reverse/blend pair is already legal for
	// this socket's kind. Clamping here keeps a non-finite value from ever
	// reaching a shader uniform even if a caller bypasses validation.
	Out.DirectionComposition.Amplitude = FMath::IsFinite(Behavior.Direction.Amplitude)
		? Behavior.Direction.Amplitude : 0.0f;
	Out.DirectionComposition.bReversed = Behavior.Direction.bReversed ? 1u : 0u;
	Out.DirectionComposition.Blend = static_cast<uint32>(Behavior.Direction.Blend);
	Out.Direction.Source.LayerId = Reference.SourceLayerId;
	Out.Direction.Source.ChildIndex = SourceIndex;
	Out.Direction.Source.Output = Reference.OutputName;
	Out.Direction.Kind = Reference.Kind;
	// Flow driver evaluation acts on the authored FlowAmount. Behavior Strength
	// applies afterward to destination displacement, independent of driver mode.
	Out.Direction.FlowAmount = FMath::IsFinite(Reference.FlowAmount)
		? Reference.FlowAmount : 0.0f;
	Out.Direction.FlowTraceLength = FMath::IsFinite(Reference.FlowTraceLength)
		? FMath::Max(Reference.FlowTraceLength, 0.0f) : 0.0f;
	Out.Direction.FlowSteps = FMath::Max(Reference.FlowSteps, 1);
	// Push consumes signed height. Sculpt consumes current Height and accumulated Flow
	// directly; its Height socket is unused and cannot reinterpret SDF.
	Out.HeightOrigin = Behavior.Height.Origin;
	Out.HeightComposition.Amplitude = FMath::IsFinite(Behavior.Height.Amplitude)
		? Behavior.Height.Amplitude : 0.0f;
	Out.HeightComposition.bReversed = Behavior.Height.bReversed ? 1u : 0u;
	Out.HeightComposition.Blend = static_cast<uint32>(Behavior.Height.Blend);
	if ((bPush || bCarve) && Out.HeightOrigin == EMixtormatBehaviorFieldOrigin::PublishedOutput)
	{
		const FMixtormatOutputReference& HeightRef = Behavior.Height.Published;
		int32 HeightIndex = bCarve ? MixtormatOutputReferences::ResolveSource(
				EffectiveLayers, LayerIndex, Valid.GeneratorChildIndex, HeightRef)
			: MixtormatOutputReferences::ResolveGeneratorInputSource(
				EffectiveLayers, LayerIndex, Valid.GeneratorChildIndex, HeightRef);
		if (HeightIndex == INDEX_NONE && bPush)
		{
			HeightIndex = MixtormatOutputReferences::ResolveSource(
				EffectiveLayers, LayerIndex, Valid.GeneratorChildIndex, HeightRef);
		}
		if (HeightIndex == INDEX_NONE || HeightRef.Kind != (bCarve
			? EMixtormatPublishedFieldKind::SDF : EMixtormatPublishedFieldKind::ScalarSigned))
		{
			Data.Children.Pop();
			return;
		}
		Out.Height.Source.LayerId = HeightRef.SourceLayerId;
		Out.Height.Source.ChildIndex = HeightIndex;
		Out.Height.Source.Output = HeightRef.OutputName;
		Out.Height.Kind = HeightRef.Kind;
	}
	if (bPublishedDirection && Reference.Kind == EMixtormatPublishedFieldKind::Flow)
	{
		// BehaviorFlow owns the reflected output-reference properties FlowAmount and
		// FlowTraceLength. Demand only completed earlier-layer mask signals; unsupported or
		// unavailable drivers retain the authored scalar rather than falling back to zero.
		const FName DriverProperties[3] = { TEXT("FlowAmount"), TEXT("FlowTraceLength"), TEXT("FlowSteps") };
		for (int32 Slot = 0; Slot < 3; ++Slot)
		{
			const FMixtormatParameterBinding* Binding = LayerChild.ParameterBindings.FindByPredicate(
				[&DriverProperties, Slot](const FMixtormatParameterBinding& Candidate)
				{
					return Candidate.DestinationOwner == EMixtormatParameterOwnerType::BehaviorFlow
						&& Candidate.DestinationParameter == DriverProperties[Slot]
						&& Candidate.ValueType == (Slot == 2 ? EMixtormatParameterValueType::Int : EMixtormatParameterValueType::Float)
						&& Candidate.Driver.bEnabled
						&& Candidate.Driver.SourceKind == EMixtormatDriverSourceKind::CombinedMask
						&& Candidate.Driver.SourceLayerId.IsValid();
				});
			if (!Binding) { continue; }
			bool bEarlier = false;
			for (int32 Previous = 0; Previous < LayerIndex; ++Previous)
			{
				if (EffectiveLayers.IsValidIndex(Previous)
					&& EffectiveLayers[Previous].LayerId == Binding->Driver.SourceLayerId
					&& EffectiveLayers[Previous].bEnabled)
				{
					bEarlier = true;
					break;
				}
			}
			if (!bEarlier) { continue; }
			const FMixtormatParameterDriver& Authored = Binding->Driver;
			FText TransferError;
			if (!MixtormatParameterDrivers::ValidateTransfer(Authored, TransferError)) { continue; }
			FScalarDriverRenderData& Driver = Out.FlowDrivers[Slot];
			Driver.bUseTransferCurve = Authored.bUseTransferCurve;
			Driver.TransferCurve = Authored.TransferCurve;
			Driver.bEnabled = true;
			Driver.SourceLayerId = Authored.SourceLayerId;
			Driver.bInvert = Authored.bInvert;
			Driver.InputMin = Authored.InputMin;
			Driver.InputMax = Authored.InputMax;
			Driver.OutputMin = Authored.OutputMin;
			Driver.OutputMax = Authored.OutputMax;
			Driver.Amount = Authored.Amount;
			Driver.Combine = static_cast<uint32>(Authored.Combine);
		}
	}
	if (Behavior.Influence.Origin == EMixtormatBehaviorFieldOrigin::PublishedOutput)
	{
		const FMixtormatOutputReference& MaskRef = Behavior.Influence.Published;
		// Generic mask-valued outputs use the ordered field resolver, not the
		// generator's Height/Warp socket or the Noise Value-to-Coverage coercion.
		const int32 MaskChildIndex = MixtormatOutputReferences::ResolveSource(
			EffectiveLayers, LayerIndex, Valid.GeneratorChildIndex, MaskRef);
		if (MaskChildIndex == INDEX_NONE
			|| MaskRef.Kind != EMixtormatPublishedFieldKind::Scalar01)
		{
			Data.Children.RemoveAt(Data.Children.Num() - 1);
			return;
		}
		Out.bHasInfluence = true;
		Out.Influence.Source.LayerId = MaskRef.SourceLayerId;
		Out.Influence.Source.ChildIndex = MaskChildIndex;
		Out.Influence.Source.Output = MaskRef.OutputName;
		Out.Influence.Kind = MaskRef.Kind;
	}
}

void GatherGeneratorHeightModuleChild(FLayerRenderData& Data, const FMixtormatLayer& Layer,
	const FMixtormatLayerChild& LayerChild, const int32 SourceChildIndex, const bool bGeneratorLayer,
	const int32 LayerIndex, const TArray<FMixtormatLayer>& EffectiveLayers)
{
	// Sublayers exist on any layer that owns generator modules, and a disabled layer must not
	// gather them.
	if (!bGeneratorLayer || !Layer.bEnabled) { return; }

	switch (LayerChild.Type)
	{
	case EMixtormatLayerChildType::Boolean:
	{
		if (!LayerChild.Boolean.bEnabled) { return; }
		FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
		ChildData.Type = EMixtormatLayerChildType::Boolean;
		ChildData.SourceChildIndex = SourceChildIndex;
		FBooleanStackRenderData& Out = ChildData.Boolean;
		const FMixtormatBooleanStack& B = LayerChild.Boolean;
		const auto Finite = [](float V, float Fallback) { return FMath::IsFinite(V) ? V : Fallback; };
		Out.Operation = static_cast<int32>(B.Operation);
		Out.EdgeStyle = static_cast<int32>(B.EdgeStyle);
		Out.TargetOffset = FVector2f(Finite(B.TargetOffsetX, 0.0f), Finite(B.TargetOffsetY, 0.0f));
		Out.SourceOffset = FVector2f(Finite(B.SourceOffsetX, 0.0f), Finite(B.SourceOffsetY, 0.0f));
		Out.TargetHeightOffset = Finite(B.TargetHeightOffset, 0.0f);
		Out.SourceHeightOffset = Finite(B.SourceHeightOffset, 0.0f);
		Out.SourceScale = Finite(B.SourceScale, 1.0f);
		Out.Amount = FMath::Clamp(Finite(B.Amount, 1.0f), 0.0f, 1.0f);
		Out.JoinWidth = FMath::Max(0.0f, Finite(B.JoinWidth, 0.1f));
		int32 AuthoredCount = 0;
		bool bEveryOperandEligible = true;
		for (int32 Index = SourceChildIndex + 1; Index < Layer.Children.Num(); ++Index)
		{
			const FMixtormatLayerChild& Candidate = Layer.Children[Index];
			if (Candidate.ScopeOwnerChildId != LayerChild.ChildId
				|| Candidate.Type == EMixtormatLayerChildType::Mask) { continue; }
			++AuthoredCount;
			const bool bEligible = Candidate.Type == EMixtormatLayerChildType::Generator
				&& Candidate.Generator.bEnabled
				&& Candidate.Generator.Type != EMixtormatGeneratorType::BooleanOps
				&& (Candidate.Generator.Type != EMixtormatGeneratorType::Noise
					|| Candidate.Generator.Noise.bNoiseWriteHeight);
			bEveryOperandEligible &= bEligible;
			if (AuthoredCount == 1) { Out.TargetChildIndex = Index; }
			else if (AuthoredCount == 2) { Out.SourceChildIndex = Index; }
			Out.LastChildIndex = Index;
		}
		Out.bComplete = bEveryOperandEligible && (AuthoredCount == 1 || AuthoredCount == 2);
		Out.bSelf = Out.bComplete && AuthoredCount == 1;
		if (Out.bSelf) { Out.SourceChildIndex = Out.TargetChildIndex; }
		break;
	}
	case EMixtormatLayerChildType::HeightBlend:
	{
		const FMixtormatGeneratorHeightBlend& Blend = LayerChild.HeightBlend;
		if (!Blend.bEnabled) { return; }
		FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
		ChildData.Type = EMixtormatLayerChildType::HeightBlend;
		ChildData.SourceChildIndex = SourceChildIndex;
		FGeneratorHeightBlendRenderData& Out = ChildData.HeightBlend;
		const auto Finite = [](const float Value, const float Fallback)
		{
			return FMath::IsFinite(Value) ? Value : Fallback;
		};
		Out.Op = static_cast<int32>(Blend.Op);
		Out.Amount = Finite(Blend.Amount, 1.0f);
		Out.Scale = Finite(Blend.Scale, 1.0f);
		Out.Softness = Finite(Blend.Softness, 0.0f);
		Out.Threshold = Finite(Blend.Threshold, 0.0f);
		Out.EdgeSoftness = Finite(Blend.EdgeSoftness, 0.1f);
		Out.BaseBias = Finite(Blend.BaseBias, 0.0f);
		Out.BlendBias = Finite(Blend.BlendBias, 0.0f);
		// Resolve the referenced module to this layer's child index. Only an earlier module can
		// have produced a height by the time this sublayer runs.
		Out.SourceChildIndex = INDEX_NONE;
		if (Blend.SourceChildId.IsValid())
		{
			for (int32 Index = 0; Index < SourceChildIndex && Index < Layer.Children.Num(); ++Index)
			{
				if (Layer.Children[Index].ChildId == Blend.SourceChildId)
				{
					Out.SourceChildIndex = Index;
					break;
				}
			}
		}
		if (Out.SourceChildIndex == INDEX_NONE)
		{
			if (LayerChild.ScopeOwnerChildId.IsValid())
			{
				for (int32 Index = 0; Index < SourceChildIndex && Index < Layer.Children.Num(); ++Index)
				{
					if (Layer.Children[Index].ChildId == LayerChild.ScopeOwnerChildId)
					{
						Out.SourceChildIndex = Index;
						break;
					}
				}
			}
			if (Out.SourceChildIndex == INDEX_NONE)
			{
				for (int32 Index = FMath::Min(SourceChildIndex, Layer.Children.Num()) - 1; Index >= 0; --Index)
				{
					const FMixtormatLayerChild& Candidate = Layer.Children[Index];
					if (Candidate.Type == EMixtormatLayerChildType::Generator)
					{
						if (Candidate.Generator.bEnabled)
						{
							Out.SourceChildIndex = Index;
							break;
						}
					}
					else if (Candidate.Type == EMixtormatLayerChildType::Boolean)
					{
						if (Candidate.Boolean.bEnabled)
						{
							Out.SourceChildIndex = Index;
							break;
						}
					}
				}
			}
		}
		break;
	}
	case EMixtormatLayerChildType::HeightCurve:
	{
		const FMixtormatGeneratorHeightCurve& Curve = LayerChild.HeightCurve;
		if (!Curve.bEnabled) { return; }
		FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
		ChildData.Type = EMixtormatLayerChildType::HeightCurve;
		ChildData.SourceChildIndex = SourceChildIndex;
		FGeneratorHeightCurveRenderData& Out = ChildData.HeightCurve;
		Out.Amount = FMath::IsFinite(Curve.Amount) ? FMath::Clamp(Curve.Amount, 0.0f, 1.0f) : 1.0f;
		Out.bNormalizeInput = Curve.bNormalizeInput ? 1u : 0u;
		Out.InputMin = FMath::IsFinite(Curve.InputMin) ? FMath::Min(Curve.InputMin, 0.0f) : -1.0f;
		Out.InputMax = FMath::IsFinite(Curve.InputMax) ? FMath::Max(Curve.InputMax, 0.0f) : 1.0f;

		Out.Offset = FMath::IsFinite(Curve.Offset) ? Curve.Offset : 0.0f;
		Out.bInvert = Curve.bInvert ? 1u : 0u;
		const MixtormatScalarRampMath::FGpuPayload Payload =
			MixtormatScalarRampMath::PrepareGpuPayload(Curve.Curve);
		Out.CurveCount = Payload.PointCount;
		Out.CurveInterpolation = Payload.Interpolation;
		Out.CurvePoints = Payload.Points;
		break;
	}
	case EMixtormatLayerChildType::HeightColorRamp:
	{
		const FMixtormatGeneratorHeightColorRamp& Ramp = LayerChild.HeightColorRamp;
		if (!Ramp.bEnabled || Ramp.Weight <= 0.0f) { return; }
		FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
		ChildData.Type = EMixtormatLayerChildType::HeightColorRamp;
		ChildData.SourceChildIndex = SourceChildIndex;
		ChildData.ScopeOwnerSourceChildIndex = MixtormatChildScope::ResolveOwnerIndex(
			Layer.Children, SourceChildIndex);
		FGeneratorHeightColorRampRenderData& Out = ChildData.HeightColorRamp;
		Out.bNormalizeInput = Ramp.bNormalizeInput;
		Out.bMirrorRepeat = Ramp.bMirrorRepeat;
		Out.Weight = FMath::Clamp(FMath::IsFinite(Ramp.Weight) ? Ramp.Weight : 1.0f, 0.0f, 1.0f);
		Out.InputScale = FMath::IsFinite(Ramp.InputScale) ? Ramp.InputScale : 1.0f;
		Out.InputOffset = FMath::IsFinite(Ramp.InputOffset) ? Ramp.InputOffset : 0.0f;
		Out.OutputName = FName(TEXT("Color"));
		Out.Source = static_cast<int32>(Ramp.Source);
		Out.SourceChildIndex = INDEX_NONE;
		Out.BlendMode = static_cast<int32>(Ramp.BlendMode);
		Out.FlowModulation = FMath::IsFinite(Ramp.FlowModulation) ? Ramp.FlowModulation : 0.0f;
		Out.bSignedFlowModulation = Ramp.bSignedFlowModulation;
		const float FlowRad = FMath::DegreesToRadians(Ramp.FlowModulationAngle);
		Out.FlowModulationDir = FVector2f(FMath::Cos(FlowRad), FMath::Sin(FlowRad));
		Out.UVModulation = FMath::IsFinite(Ramp.UVModulation) ? Ramp.UVModulation : 0.0f;
		const float UVRad = FMath::DegreesToRadians(Ramp.UVModulationAngle);
		Out.UVModulationDir = FVector2f(FMath::Cos(UVRad), FMath::Sin(UVRad));
		Out.IdVariation = FMath::Clamp(FMath::IsFinite(Ramp.IdVariation) ? Ramp.IdVariation : 0.0f, 0.0f, 1.0f);
		Out.IdVariationSeed = static_cast<uint32>(FMath::Max(0, Ramp.IdVariationSeed));

		const bool bHasModulation = FMath::Abs(Out.FlowModulation) > 1.0e-5f
			|| FMath::Abs(Out.UVModulation) > 1.0e-5f
			|| Out.IdVariation > 1.0e-5f;
		const bool bUsesModuleChild = (Ramp.Source != EMixtormatColorRampSource::GeneratorRunning
			&& Ramp.Source != EMixtormatColorRampSource::LayerHeight
			&& Ramp.Source != EMixtormatColorRampSource::CompositeBelow)
			|| bHasModulation;

		if (bUsesModuleChild && Ramp.SourceChildId.IsValid())
		{
			for (int32 Index = 0; Index < SourceChildIndex && Index < Layer.Children.Num(); ++Index)
			{
				if (Layer.Children[Index].ChildId == Ramp.SourceChildId)
				{
					Out.SourceChildIndex = Index;
					break;
				}
			}
		}
		if (bUsesModuleChild && Out.SourceChildIndex == INDEX_NONE
			&& ChildData.ScopeOwnerSourceChildIndex != INDEX_NONE
			&& Layer.Children.IsValidIndex(ChildData.ScopeOwnerSourceChildIndex))
		{
			Out.SourceChildIndex = ChildData.ScopeOwnerSourceChildIndex;
		}
		if (bUsesModuleChild && Out.SourceChildIndex == INDEX_NONE)
		{
			for (int32 Index = FMath::Min(SourceChildIndex, Layer.Children.Num()) - 1; Index >= 0; --Index)
			{
				const FMixtormatLayerChild& Candidate = Layer.Children[Index];
				if (Ramp.Source == EMixtormatColorRampSource::RootArcLength
					|| Ramp.Source == EMixtormatColorRampSource::RootAcross)
				{
					if (Candidate.Type == EMixtormatLayerChildType::Generator
						&& Candidate.Generator.Type == EMixtormatGeneratorType::RootNetwork)
					{
						Out.SourceChildIndex = Index;
						break;
					}
				}
				else if (Ramp.Source == EMixtormatColorRampSource::NoiseValue)
				{
					if (Candidate.Type == EMixtormatLayerChildType::Generator
						&& Candidate.Generator.Type == EMixtormatGeneratorType::Noise)
					{
						Out.SourceChildIndex = Index;
						break;
					}
				}
				else if (Ramp.Source == EMixtormatColorRampSource::FlowMagnitude
					|| Ramp.Source == EMixtormatColorRampSource::FlowAngle)
				{
					if (Candidate.Type == EMixtormatLayerChildType::Generator
						|| (Candidate.Type == EMixtormatLayerChildType::Behavior
							&& (Candidate.Behavior.Type == EMixtormatBehaviorType::FlowField
								|| Candidate.Behavior.Type == EMixtormatBehaviorType::Feather)))
					{
						Out.SourceChildIndex = Index;
						break;
					}
				}
				else if (Ramp.Source == EMixtormatColorRampSource::RegionRandom)
				{
					if (Candidate.Type == EMixtormatLayerChildType::Generator
						|| Candidate.Type == EMixtormatLayerChildType::ColorId
						|| Candidate.Type == EMixtormatLayerChildType::Filter
						|| Candidate.Type == EMixtormatLayerChildType::CombineId
						|| Candidate.Type == EMixtormatLayerChildType::PatternId
						|| Candidate.Type == EMixtormatLayerChildType::RandomId)
					{
						Out.SourceChildIndex = Index;
						break;
					}
				}
				else if (Candidate.Type == EMixtormatLayerChildType::Generator
					|| Candidate.Type == EMixtormatLayerChildType::Boolean)
				{
					Out.SourceChildIndex = Index;
					break;
				}
			}
		}
		const MixtormatColorRampMath::FGpuPayload Payload =
			MixtormatColorRampMath::PrepareGpuPayload(Ramp.GetUnitRamp());
		Out.StopCount = Payload.StopCount;
		Out.Interpolation = Payload.Interpolation;
		Out.Positions = Payload.Positions;
		Out.Colors = Payload.Colors;
		break;
	}
	default:
		break;
	}
}
}
