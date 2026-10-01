// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Compositing/MixtormatGeneratorGather.h"

#include "Compositing/MixtormatComposeHash.h"

namespace MixtormatGpuCompositor
{
void GatherGeneratorChild(FLayerRenderData& Data, const FMixtormatLayer& Layer,
	const FMixtormatLayerChild& LayerChild, const int32 SourceChildIndex,
	const bool bGeneratorLayer, const bool bCacheLayers, const uint64 PlacementKey)
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
	ChildData.GeneratorHeightBlend = Generator.HeightBlend;
	ChildData.Generator.Type = Generator.Type;

	switch (Generator.Type)
	{
	case EMixtormatGeneratorType::StrataCarver:
	{
		const FMixtormatStrataCarver& Carver = Generator.StrataCarver;
		FStrataCarverRenderData& Out = ChildData.Generator.StrataCarver;
		// Non-finite falls back to the default; ranges are authored in the tool.
		const auto Finite = [](const float Value, const float Fallback)
		{
			return FMath::IsFinite(Value) ? Value : Fallback;
		};
		Out.Seed = static_cast<uint32>(Carver.Seed);
		Out.Depth = Finite(Carver.Depth, 0.25f);
		Out.StrataFrequency = Finite(Carver.StrataFrequency, 6.0f);
		Out.StrataRotation = Finite(Carver.StrataRotation, 0.0f);
		Out.ThicknessVariation = Finite(Carver.ThicknessVariation, 0.5f);
		Out.HeightVariation = Finite(Carver.HeightVariation, 0.5f);
		Out.Verticality = Finite(Carver.Verticality, 0.7f);
		Out.RampShape = Finite(Carver.RampShape, 0.0f);
		Out.Bend = Finite(Carver.Bend, 0.03f);
		Out.BendScale = Carver.BendScale;
		Out.Breakup = Finite(Carver.Breakup, 0.1f);
		Out.HeightFollow = Finite(Carver.HeightFollow, 0.0f);
		Out.Lamination = Finite(Carver.Lamination, 0.25f);
		Out.CrossBedding = Finite(Carver.CrossBedding, 1.0f);
		Out.MaskInfluence = Finite(Carver.MaskInfluence, 1.0f);
		Out.IDInfluence = Finite(Carver.IDInfluence, 0.0f);
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
		Out.HeightScale = Finite(Cracks.CrackHeightScale, Defaults.CrackHeightScale);
		if (bCacheLayers)
		{
			MixtormatComposeHash::FHasher Hasher;
			// The chamfer and the height scale read the cached field; they do not change it.
			Hasher.SkipTopLevel = {TEXT("CrackChamferAmount"), TEXT("CrackChamferEdge"),
				TEXT("CrackHeightScale")};
			Hasher.Struct(FMixtormatCracks::StaticStruct(), &Cracks);
			// Combined height and negative delta are cached separately; invalidate the old field layout.
			Out.FieldKey = MixtormatComposeHash::Combine(Hasher.Get(), 0x437261636B7333ull) | 1ull;
		}
		break;
	}
	case EMixtormatGeneratorType::RockFormation:
	{
		// Unclamped: ranges are authored in the tool. Non-finite falls back to the default.
		const FMixtormatRockFormation& Rock = Generator.RockFormation;
		const FMixtormatRockFormation Defaults;
		FRockFormationRenderData& Out = ChildData.Generator.RockFormation;
		const auto Finite = [](const float Value, const float Fallback)
		{
			return FMath::IsFinite(Value) ? Value : Fallback;
		};
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
		Out.TiltRandom = Finite(Rock.RockTiltRandom, Defaults.RockTiltRandom);
		Out.SizeRandom = Finite(Rock.RockSizeRandom, Defaults.RockSizeRandom);
		Out.Stretch = Finite(Rock.RockStretch, Defaults.RockStretch);
		Out.StretchAngle = Finite(Rock.RockStretchAngle, Defaults.RockStretchAngle);
		Out.StretchRandom = Finite(Rock.RockStretchRandom, Defaults.RockStretchRandom);
		Out.HeightClusters = Finite(Rock.RockHeightClusters, Defaults.RockHeightClusters);
		Out.Skew = Finite(Rock.RockSkew, Defaults.RockSkew);
		Out.EdgeJag = Finite(Rock.RockEdgeJag, Defaults.RockEdgeJag);
		Out.JagScale = Finite(Rock.RockJagScale, Defaults.RockJagScale);
		Out.JagDetail = Finite(Rock.RockJagDetail, Defaults.RockJagDetail);
		Out.ChamferJag = Finite(Rock.RockChamferJag, Defaults.RockChamferJag);
		Out.RimChips = Finite(Rock.RockRimChips, Defaults.RockRimChips);
		Out.RimChipSize = Finite(Rock.RockRimChipSize, Defaults.RockRimChipSize);
		Out.FacetChips = Finite(Rock.RockFacetChips, Defaults.RockFacetChips);
		Out.FacetIterations = Rock.RockFacetIterations;
		Out.FacetFalloff = Finite(Rock.RockFacetFalloff, Defaults.RockFacetFalloff);
		Out.FacetRandom = Finite(Rock.RockFacetRandom, Defaults.RockFacetRandom);
		Out.FacetAlign = Finite(Rock.RockFacetAlign, Defaults.RockFacetAlign);
		Out.HeightMode = Rock.RockHeightMode;
		Out.HeightScale = Finite(Rock.RockHeightScale, Defaults.RockHeightScale);
		if (bCacheLayers)
		{
			MixtormatComposeHash::FHasher Hasher;
			// Height mode and scale consume the cached field; they do not reshape it.
			Hasher.SkipTopLevel = {TEXT("RockHeightScale"), TEXT("RockHeightMode")};
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
		Out.HeightScale = Finite(Pebbles.PebbleHeightScale, Defaults.PebbleHeightScale);
		if (bCacheLayers)
		{
			MixtormatComposeHash::FHasher Hasher;
			Hasher.SkipTopLevel = {TEXT("PebbleHeightScale")};
			Hasher.Struct(FMixtormatPebbles::StaticStruct(), &Pebbles);
			Out.FieldKey = Hasher.Get() | 1ull;
		}
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
		default:
			break;
		}
	}
}
}
