// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Compositing/MixtormatGeneratorGather.h"

#include "Compositing/MixtormatComposeHash.h"
#include "Compositing/MixtormatNoiseRender.h"
#include "MixtormatColorRampMath.h"
#include "MixtormatChildScope.h"
#include "MixtormatMaterial.h"
#include "MixtormatOutputReference.h"
#include "MixtormatScalarRampMath.h"

namespace MixtormatGpuCompositor
{
	void GatherGeneratorChild(FLayerRenderData& Data, const FMixtormatLayer& Layer,
		const FMixtormatLayerChild& LayerChild, const int32 SourceChildIndex,
		const bool bGeneratorLayer, const bool bCacheLayers, const uint64 PlacementKey,
		const int32 LayerIndex, const TArray<FMixtormatLayer>& EffectiveLayers,
		const TArray<FMixtormatSourceEntry>& Sources)
	{
		// Modules exist only on Generator layers. A disabled layer must not gather them -- an
		// enabled module on a hidden layer would build a surface nobody can see and still cost
		// the whole solve.
		const FMixtormatGenerator& Generator = LayerChild.Generator;
		if (!bGeneratorLayer || !Layer.bEnabled || !Generator.bEnabled)
		{
			return;
		}

		FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
		ChildData.Type = EMixtormatLayerChildType::Generator;
		ChildData.SourceChildIndex = SourceChildIndex;
		ChildData.Generator.Type = Generator.Type;
		const auto GatherInput = [&](const FMixtormatOutputReference& Reference,
			FGeneratorInputRenderData& Out, const bool bHeight)
		{
			Out.bRequested = Reference.bEnabled;
			Out.Reference.Kind = Reference.Kind;
			const bool bCompatible = bHeight
				? Reference.Kind == EMixtormatPublishedFieldKind::ScalarSigned
				: Reference.Kind == EMixtormatPublishedFieldKind::Flow || Reference.Kind == EMixtormatPublishedFieldKind::UVMap;
			FGuid OwnerId = Reference.SourceLayerId;
			int32 SourceIndex = INDEX_NONE;
			if (Reference.IsShelfSource())
			{
				// A shelf input names a producer by stable entry identity; its root child is index 0 of
				// that producer. Classification keeps malformed, disabled, wrong-kind and missing
				// endpoints unavailable -- never a layer fallback, never a guessed producer.
				OwnerId = Reference.SourceShelfId;
				const MixtormatOutputReferences::FShelfSourceReferenceStatus Status = bCompatible
					? MixtormatOutputReferences::ClassifyShelfSourceReference(Sources, Reference)
					: MixtormatOutputReferences::FShelfSourceReferenceStatus();
				if (Reference.bEnabled
					&& Status.Issue == MixtormatOutputReferences::EShelfSourceReferenceIssue::Unevaluated)
				{
					SourceIndex = 0;
				}
			}
			else if (bCompatible)
			{
				SourceIndex = MixtormatOutputReferences::ResolveGeneratorInputSource(
					EffectiveLayers, LayerIndex, SourceChildIndex, Reference);
			}
			Out.Reference.Source.LayerId = OwnerId;
			Out.Reference.Source.ChildIndex = SourceIndex;
			Out.Reference.Source.Output = Reference.OutputName;
			// The registry owner is explicit: a resolved shelf input is a Shelf field, everything else a
			// layer field, so the key matches exactly what the producer published.
			Out.Reference.Source.OwnerKind = Reference.IsShelfSource()
				? EMixtormatOutputReferenceOwnerKind::Shelf
				: EMixtormatOutputReferenceOwnerKind::Layer;
			Out.Reference.FlowAmount = FMath::IsFinite(Reference.FlowAmount) ? Reference.FlowAmount : 0.0f;
			Out.Reference.FlowTraceLength = FMath::IsFinite(Reference.FlowTraceLength)
				? FMath::Max(Reference.FlowTraceLength, 0.0f) : 0.0f;
			Out.Reference.FlowSteps = FMath::Max(Reference.FlowSteps, 1);
		};
		GatherInput(Generator.HeightSource, ChildData.Generator.HeightSource, true);
		GatherInput(Generator.WarpSource, ChildData.Generator.WarpSource, false);

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
		Out.ChamferWidth=Finite(Cliff.ChamferWidth,Defaults.ChamferWidth); Out.ChamferIntensity=Finite(Cliff.ChamferIntensity,Defaults.ChamferIntensity);
		Out.ChamferVoronoi=Finite(Cliff.ChamferVoronoi,Defaults.ChamferVoronoi); Out.BlockCavityWidth=Finite(Cliff.BlockCavityWidth,Defaults.BlockCavityWidth);
		Out.RowCavityWidth=Finite(Cliff.RowCavityWidth,Defaults.RowCavityWidth); Out.CavityIntensity=Finite(Cliff.CavityIntensity,Defaults.CavityIntensity);
		Out.CavityVoronoiThreshold=Finite(Cliff.CavityVoronoiThreshold,Defaults.CavityVoronoiThreshold); Out.CavityVoronoiMaskGain=Finite(Cliff.CavityVoronoiMaskGain,Defaults.CavityVoronoiMaskGain);
		if(bCacheLayers){MixtormatComposeHash::FHasher Hasher;Hasher.SkipTopLevel={TEXT("bCliffNormalizeHeight"),TEXT("CliffHeightScale")};Hasher.Struct(FMixtormatCliffStrata::StaticStruct(),&Cliff);Out.FieldKey=MixtormatComposeHash::Combine(Hasher.Get(),0x436C696666537472ull)|1ull;}
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
		default:
			break;
		}
	}
}

void GatherGeneratorBehaviorChild(FLayerRenderData& Data, const FMixtormatLayer& Layer,
	const FMixtormatLayerChild& LayerChild, const int32 BehaviorChildIndex, const int32 LayerIndex,
	const TArray<FMixtormatLayer>& EffectiveLayers,
	const TArray<FMixtormatSourceEntry>& Sources)
{
	// A Behavior is owned by its Generator, not run as an Effect at its own row.
	// The execution kernel is independent of the generator family.
	if (!Layer.bEnabled || Layer.Type != EMixtormatLayerType::Generator
		|| LayerChild.Type != EMixtormatLayerChildType::Behavior) { return; }
	const FMixtormatBehavior& Behavior = LayerChild.Behavior;
	const bool bWarp = Behavior.Type == EMixtormatBehaviorType::Warp;
	const bool bPush = Behavior.Type == EMixtormatBehaviorType::Push;
	const bool bCarve = Behavior.Type == EMixtormatBehaviorType::Carve;
	const bool bDeform = Behavior.Type == EMixtormatBehaviorType::Deform;
	const bool bFlowField = Behavior.Type == EMixtormatBehaviorType::FlowField;
	const bool bTraced = Behavior.Flow.bUseTracedFlow;
	if (!Behavior.bEnabled || (!bWarp && !bPush && !bCarve && !bDeform && !bFlowField)
		|| (Behavior.Stage != EMixtormatBehaviorStage::PostGeneration
			&& !(Behavior.Stage == EMixtormatBehaviorStage::PreGeneration && bWarp
				&& Behavior.Direction.Origin == EMixtormatBehaviorFieldOrigin::PublishedOutput))
		|| ((bWarp || bDeform) && !bTraced && (Behavior.Direction.Origin != EMixtormatBehaviorFieldOrigin::PublishedOutput
			&& Behavior.Direction.Origin != EMixtormatBehaviorFieldOrigin::OwnNativeHeight))
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
			EffectiveLayers, LayerIndex, BehaviorChildIndex, Sources, &Layer);
	if (!Valid.bCanEvaluate || Valid.GeneratorChildIndex == INDEX_NONE) { return; }
	const bool bPublishedDirection = Behavior.Direction.Origin == EMixtormatBehaviorFieldOrigin::PublishedOutput;
	const FMixtormatOutputReference& Reference = Behavior.Direction.Published;
	const int32 SourceIndex = !bPublishedDirection ? INDEX_NONE
		: Reference.IsShelfSource() ? 0
		: MixtormatOutputReferences::ResolveGeneratorInputSource(
			EffectiveLayers, LayerIndex, Valid.GeneratorChildIndex, Reference);
	if (bPublishedDirection && SourceIndex == INDEX_NONE) { return; }
	FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
	ChildData.Type = EMixtormatLayerChildType::Behavior;
	ChildData.SourceChildIndex = BehaviorChildIndex;
	ChildData.ScopeOwnerSourceChildIndex = Valid.GeneratorChildIndex;
	FBehaviorRenderData& Out = ChildData.Behavior;
	Out.Type = Behavior.Type;
	Out.bUseTracedFlow = bTraced;
	// Gather authored Behavior Flow parameters into the native render data.
	const FMixtormatBehaviorFlowSettings& Flow = Behavior.Flow;
	FBehaviorFlowRenderData& FlowOut = Out.Flow;
	FlowOut.Mode = Flow.Mode;
	Out.Stage = Behavior.Stage;
	FlowOut.GeneratorFlowSource = static_cast<uint32>(Flow.GeneratorFlowSource);
	FlowOut.GeneratorFlowAmount = FMath::IsFinite(Flow.GeneratorFlowAmount) ? Flow.GeneratorFlowAmount : FMixtormatBehaviorFlowSettings().GeneratorFlowAmount;
	FlowOut.GeneratorFlowTangent = FMath::IsFinite(Flow.GeneratorFlowTangent) ? Flow.GeneratorFlowTangent : FMixtormatBehaviorFlowSettings().GeneratorFlowTangent;
	FlowOut.GeneratorFlowAngle = FMath::IsFinite(Flow.GeneratorFlowAngle) ? Flow.GeneratorFlowAngle : FMixtormatBehaviorFlowSettings().GeneratorFlowAngle;
	FlowOut.GravityFlowSurfaceFollow = FMath::IsFinite(Flow.GravityFlowSurfaceFollow) ? Flow.GravityFlowSurfaceFollow : FMixtormatBehaviorFlowSettings().GravityFlowSurfaceFollow;
	FlowOut.GravityFlowDeflection = FMath::IsFinite(Flow.GravityFlowDeflection) ? Flow.GravityFlowDeflection : FMixtormatBehaviorFlowSettings().GravityFlowDeflection;
	FlowOut.GeneratorFlowBend = FMath::IsFinite(Flow.GeneratorFlowBend) ? Flow.GeneratorFlowBend : FMixtormatBehaviorFlowSettings().GeneratorFlowBend;
	FlowOut.GeneratorFlowSeed = static_cast<uint32>(Flow.GeneratorFlowSeed);
	FlowOut.GeneratorFlowRadius = FMath::Max(Flow.GeneratorFlowRadius, 1);
	FlowOut.GeneratorFlowSmooth = FMath::IsFinite(Flow.GeneratorFlowSmooth) ? Flow.GeneratorFlowSmooth : FMixtormatBehaviorFlowSettings().GeneratorFlowSmooth;
	FlowOut.GeneratorFlowReach = FMath::IsFinite(Flow.GeneratorFlowReach) ? Flow.GeneratorFlowReach : FMixtormatBehaviorFlowSettings().GeneratorFlowReach;
	FlowOut.GeneratorFlowFeather = FMath::IsFinite(Flow.GeneratorFlowFeather) ? Flow.GeneratorFlowFeather : FMixtormatBehaviorFlowSettings().GeneratorFlowFeather;
	FlowOut.GeneratorFlowOffsetAlong = FMath::IsFinite(Flow.GeneratorFlowOffsetAlong) ? Flow.GeneratorFlowOffsetAlong : FMixtormatBehaviorFlowSettings().GeneratorFlowOffsetAlong;
	FlowOut.GeneratorFlowOffsetAcross = FMath::IsFinite(Flow.GeneratorFlowOffsetAcross) ? Flow.GeneratorFlowOffsetAcross : FMixtormatBehaviorFlowSettings().GeneratorFlowOffsetAcross;
	FlowOut.GeneratorFlowShapeOffset = FMath::IsFinite(Flow.GeneratorFlowShapeOffset) ? Flow.GeneratorFlowShapeOffset : FMixtormatBehaviorFlowSettings().GeneratorFlowShapeOffset;
	FlowOut.GeneratorFlowBulge = FMath::IsFinite(Flow.GeneratorFlowBulge) ? Flow.GeneratorFlowBulge : FMixtormatBehaviorFlowSettings().GeneratorFlowBulge;
	FlowOut.GeneratorFlowTraceLength = FMath::IsFinite(Flow.GeneratorFlowTraceLength) ? Flow.GeneratorFlowTraceLength : FMixtormatBehaviorFlowSettings().GeneratorFlowTraceLength;
	FlowOut.GeneratorFlowSteps = FMath::Max(Flow.GeneratorFlowSteps, 1);
	FlowOut.GeneratorFlowWarpStrength = FMath::IsFinite(Flow.GeneratorFlowWarpStrength) ? Flow.GeneratorFlowWarpStrength : FMixtormatBehaviorFlowSettings().GeneratorFlowWarpStrength;
	FlowOut.GeneratorFlowCarveMode = static_cast<uint32>(Flow.GeneratorFlowCarveMode);
	FlowOut.GeneratorFlowDepth = FMath::IsFinite(Flow.GeneratorFlowDepth) ? Flow.GeneratorFlowDepth : FMixtormatBehaviorFlowSettings().GeneratorFlowDepth;
	FlowOut.GeneratorFlowWidth = FMath::IsFinite(Flow.GeneratorFlowWidth) ? Flow.GeneratorFlowWidth : FMixtormatBehaviorFlowSettings().GeneratorFlowWidth;
	FlowOut.GeneratorFlowFalloff = FMath::IsFinite(Flow.GeneratorFlowFalloff) ? Flow.GeneratorFlowFalloff : FMixtormatBehaviorFlowSettings().GeneratorFlowFalloff;
	// Resolve drivers for FMixtormatBehaviorFlowSettings properties:
	const FName FlowSettingsProperties[8] = {
		TEXT("GeneratorFlowAmount"),
		TEXT("GeneratorFlowTraceLength"),
		TEXT("GeneratorFlowWarpStrength"),
		TEXT("GeneratorFlowDepth"),
		TEXT("GeneratorFlowShapeOffset"),
		TEXT("GeneratorFlowBulge"),
		TEXT("GeneratorFlowReach"),
		TEXT("GeneratorFlowFeather")
	};
	for (int32 Slot = 0; Slot < 8; ++Slot)
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
		FScalarDriverRenderData& Driver = FlowOut.SettingsDrivers[Slot];
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
		FScalarDriverRenderData& Driver = Out.ScalarDrivers[Slot];
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
	Out.Direction.Source.LayerId = Reference.IsShelfSource()
		? Reference.SourceShelfId : Reference.SourceLayerId;
	Out.Direction.Source.ChildIndex = SourceIndex;
	Out.Direction.Source.Output = Reference.OutputName;
	Out.Direction.Source.OwnerKind = Reference.OwnerKind;
	Out.Direction.Kind = Reference.Kind;
	// Flow driver evaluation acts on the authored FlowAmount. Behavior Strength
	// applies afterward to destination displacement, independent of driver mode.
	Out.Direction.FlowAmount = FMath::IsFinite(Reference.FlowAmount)
		? Reference.FlowAmount : 0.0f;
	Out.Direction.FlowTraceLength = FMath::IsFinite(Reference.FlowTraceLength)
		? FMath::Max(Reference.FlowTraceLength, 0.0f) : 0.0f;
	Out.Direction.FlowSteps = FMath::Max(Reference.FlowSteps, 1);
	// Push consumes signed height; Carve consumes a typed SDF or native boundary,
	// never a signed height silently reinterpreted as distance.
	Out.HeightOrigin = Behavior.Height.Origin;
	Out.HeightComposition.Amplitude = FMath::IsFinite(Behavior.Height.Amplitude)
		? Behavior.Height.Amplitude : 0.0f;
	Out.HeightComposition.bReversed = Behavior.Height.bReversed ? 1u : 0u;
	Out.HeightComposition.Blend = static_cast<uint32>(Behavior.Height.Blend);
	if ((bPush || bCarve) && Out.HeightOrigin == EMixtormatBehaviorFieldOrigin::PublishedOutput)
	{
		const FMixtormatOutputReference& HeightRef = Behavior.Height.Published;
		int32 HeightIndex = HeightRef.IsShelfSource() ? 0
			: bCarve ? MixtormatOutputReferences::ResolveSource(
				EffectiveLayers, LayerIndex, Valid.GeneratorChildIndex, HeightRef)
			: MixtormatOutputReferences::ResolveGeneratorInputSource(
				EffectiveLayers, LayerIndex, Valid.GeneratorChildIndex, HeightRef);
		if (HeightIndex == INDEX_NONE && bPush && !HeightRef.IsShelfSource())
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
		Out.Height.Source.LayerId = HeightRef.IsShelfSource()
			? HeightRef.SourceShelfId : HeightRef.SourceLayerId;
		Out.Height.Source.ChildIndex = HeightIndex;
		Out.Height.Source.Output = HeightRef.OutputName;
		Out.Height.Source.OwnerKind = HeightRef.OwnerKind;
		Out.Height.Kind = HeightRef.Kind;
	}
	if (bPublishedDirection && Reference.Kind == EMixtormatPublishedFieldKind::Flow)
	{
		// BehaviorFlow owns the reflected output-reference properties FlowAmount and
		// FlowTraceLength. Demand only completed earlier-layer mask signals; unsupported or
		// unavailable drivers retain the authored scalar rather than falling back to zero.
		const FName DriverProperties[2] = { TEXT("FlowAmount"), TEXT("FlowTraceLength") };
		for (int32 Slot = 0; Slot < 2; ++Slot)
		{
			const FMixtormatParameterBinding* Binding = LayerChild.ParameterBindings.FindByPredicate(
				[&DriverProperties, Slot](const FMixtormatParameterBinding& Candidate)
				{
					return Candidate.DestinationOwner == EMixtormatParameterOwnerType::BehaviorFlow
						&& Candidate.DestinationParameter == DriverProperties[Slot]
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
			FScalarDriverRenderData& Driver = Out.FlowDrivers[Slot];
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
		if (MaskChildIndex == INDEX_NONE || MaskRef.IsShelfSource()
			|| MaskRef.Kind != EMixtormatPublishedFieldKind::Scalar01)
		{
			Data.Children.RemoveAt(Data.Children.Num() - 1);
			return;
		}
		Out.bHasInfluence = true;
		Out.Influence.Source.LayerId = MaskRef.SourceLayerId;
		Out.Influence.Source.ChildIndex = MaskChildIndex;
		Out.Influence.Source.Output = MaskRef.OutputName;
		Out.Influence.Source.OwnerKind = EMixtormatOutputReferenceOwnerKind::Layer;
		Out.Influence.Kind = MaskRef.Kind;
	}
}

void GatherGeneratorHeightModuleChild(FLayerRenderData& Data, const FMixtormatLayer& Layer,
	const FMixtormatLayerChild& LayerChild, const int32 SourceChildIndex, const bool bGeneratorLayer,
	const int32 LayerIndex, const TArray<FMixtormatLayer>& EffectiveLayers)
{
	// Sublayers exist only on Generator layers, and a disabled layer must not gather them.
	if (!bGeneratorLayer || !Layer.bEnabled) { return; }

	switch (LayerChild.Type)
	{
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
		Out.Amount = FMath::IsFinite(Curve.Amount) ? Curve.Amount : 1.0f;
		Out.bNormalizeInput = Curve.bNormalizeInput ? 1u : 0u;
		Out.InputMin = FMath::IsFinite(Curve.InputMin) ? Curve.InputMin : -1.0f;
		Out.InputMax = FMath::IsFinite(Curve.InputMax) ? Curve.InputMax : 1.0f;
		Out.Balance = FMath::IsFinite(Curve.Balance) ? Curve.Balance : 1.0f;
		Out.Contrast = FMath::IsFinite(Curve.Contrast) ? Curve.Contrast : 1.0f;
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
		if (!Ramp.bEnabled) { return; }
		FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
		ChildData.Type = EMixtormatLayerChildType::HeightColorRamp;
		ChildData.SourceChildIndex = SourceChildIndex;
		FGeneratorHeightColorRampRenderData& Out = ChildData.HeightColorRamp;
		Out.OutputName = FName(TEXT("Color"));
		Out.Source = static_cast<int32>(Ramp.Source);
		// Resolve the referenced module to this layer's child index, the same rule Height Blend
		// follows: only an earlier module can have produced a height by the time this sublayer
		// runs. Anything unresolvable reads the running height, the module's defined neutral.
		Out.SourceChildIndex = INDEX_NONE;
		if (Ramp.Source == EMixtormatColorRampSource::ModuleRef && Ramp.SourceChildId.IsValid())
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
		const MixtormatColorRampMath::FGpuPayload Payload =
			MixtormatColorRampMath::PrepareGpuPayload(Ramp.Ramp);
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
