// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/MixtormatChildCapabilities.h"

#include "MixtormatEffect.h"

namespace
{
	EMixtormatEffectType ResolveChildEffectType(const FMixtormatLayerChild& Child)
	{
		const UMixtormatEffect* Asset = Child.Effect.Effect.LoadSynchronous();
		return Asset ? Asset->EffectType : Child.Effect.ProceduralType;
	}
}

FMixtormatChildCapabilities GetChildCapabilities(const FMixtormatLayerChild& Child)
{
	const FText RegionIdsLabel = NSLOCTEXT("SMixtormat", "PreviewOutputRegionIds", "Region IDs");
	const FText GapLabel = NSLOCTEXT("SMixtormat", "PreviewOutputGap", "Gap");
	const FName GapName(TEXT("Gap"));

	FMixtormatChildCapabilities Result;
	const EMixtormatEffectType EffectType = ResolveChildEffectType(Child);
	switch (Child.Type)
	{
	case EMixtormatLayerChildType::Filter:
		// No invalid pixels to black out: every pixel in a cluster segmentation belongs to some
		// region, so there is no separate gap concept here to combine in. Not copyable: an ID map
		// is not a mask a Replace blend could read.
		Result.Outputs.Add({NAME_None, RegionIdsLabel, EMixtormatPreviewOutputKind::RegionIds,
			false, true, false, NAME_None});
		break;
	case EMixtormatLayerChildType::PatternId:
		// Region IDs blackens its own grout inline (see MixtormatPatternIds.usf's WriteDebug
		// branch) -- PreviewGapMaskName stays empty because the compositor needs no second texture
		// to combine, the one ID map already encodes it.
		Result.Outputs.Add({NAME_None, RegionIdsLabel, EMixtormatPreviewOutputKind::RegionIds,
			false, true, false, NAME_None});
		// Gap is still a real, separately-published scalar output -- Copy Instance Mask from Gap
		// has always been able to lift it into a mask -- it is simply not a second preview option,
		// because the Region IDs eye above already shows it (blackened grout).
		Result.Outputs.Add({GapName, GapLabel, EMixtormatPreviewOutputKind::Mask,
			true, false, false, NAME_None});
		break;
	case EMixtormatLayerChildType::CombineId:
		Result.Outputs.Add({NAME_None, RegionIdsLabel, EMixtormatPreviewOutputKind::RegionIds,
			false, true, false, NAME_None});
		break;
	case EMixtormatLayerChildType::IdGroup:
		Result.Outputs.Add({NAME_None, RegionIdsLabel, EMixtormatPreviewOutputKind::RegionIds,
			false, true, false, NAME_None});
		Result.Outputs.Add({FName(TEXT("Boundary")),
			NSLOCTEXT("SMixtormat", "PreviewOutputIdGroupBoundary", "Boundary"),
			EMixtormatPreviewOutputKind::Mask, true, true, true, NAME_None});
		break;
	case EMixtormatLayerChildType::RampId:
		// The per-region gradient. Preview only: the field is two-channel (gradient, strength), not
		// a scalar mask a Replace-blend mask child could read.
		Result.Outputs.Add({FName(TEXT("Ramp")),
			NSLOCTEXT("SMixtormat", "PreviewOutputRampIdRamp", "Ramp"),
			EMixtormatPreviewOutputKind::Mask, false, true, false, NAME_None});
		break;
	case EMixtormatLayerChildType::Generator:
		if (Child.Generator.Type == EMixtormatGeneratorType::Cracks)
		{
			// Piece IDs feed the ID consumers below this row; the masks are copyable outputs.
			Result.Outputs.Add({NAME_None, RegionIdsLabel, EMixtormatPreviewOutputKind::RegionIds,
				false, true, false, NAME_None});
			Result.Outputs.Add({FName(TEXT("CrackMask")),
				NSLOCTEXT("SMixtormat", "PreviewOutputCrackMask", "Crack"),
				EMixtormatPreviewOutputKind::Mask, true, true, true, NAME_None});
			Result.Outputs.Add({FName(TEXT("CrackDistance")),
				NSLOCTEXT("SMixtormat", "PreviewOutputCrackDistance", "Crack Distance"),
				EMixtormatPreviewOutputKind::Mask, true, true, true, NAME_None});
			Result.Outputs.Add({FName(TEXT("PieceRandom")),
				NSLOCTEXT("SMixtormat", "PreviewOutputCrackPieceRandom", "Piece Random"),
				EMixtormatPreviewOutputKind::Mask, true, true, true, NAME_None});
			Result.Outputs.Add({FName(TEXT("ChamferCut")),
				NSLOCTEXT("SMixtormat", "PreviewOutputCrackChamfer", "Chamfer Cut"),
				EMixtormatPreviewOutputKind::Mask, true, true, true, NAME_None});
		}
		else if (Child.Generator.Type == EMixtormatGeneratorType::RockFormation)
		{
			// Chunk IDs feed the ID consumers below this row; the masks are copyable outputs.
			Result.Outputs.Add({NAME_None, RegionIdsLabel, EMixtormatPreviewOutputKind::RegionIds,
				false, true, false, NAME_None});
			Result.Outputs.Add({FName(TEXT("RockTop")),
				NSLOCTEXT("SMixtormat", "PreviewOutputRockTop", "Top"),
				EMixtormatPreviewOutputKind::Mask, true, true, true, NAME_None});
			Result.Outputs.Add({FName(TEXT("RockChamfer")),
				NSLOCTEXT("SMixtormat", "PreviewOutputRockChamfer", "Chamfer"),
				EMixtormatPreviewOutputKind::Mask, true, true, true, NAME_None});
			Result.Outputs.Add({FName(TEXT("RockWall")),
				NSLOCTEXT("SMixtormat", "PreviewOutputRockWall", "Wall"),
				EMixtormatPreviewOutputKind::Mask, true, true, true, NAME_None});
			Result.Outputs.Add({FName(TEXT("RockEdgeDistance")),
				NSLOCTEXT("SMixtormat", "PreviewOutputRockEdgeDistance", "Signed Boundary Distance"),
				EMixtormatPreviewOutputKind::SignedDistance, true, true, true, NAME_None});
			// 0 -> 1 across each class, zero outside it: the soft counterparts of the three masks.
			Result.Outputs.Add({FName(TEXT("RockTopRamp")),
				NSLOCTEXT("SMixtormat", "PreviewOutputRockTopRamp", "Top Ramp"),
				EMixtormatPreviewOutputKind::Mask, true, true, true, NAME_None});
			Result.Outputs.Add({FName(TEXT("RockChamferRamp")),
				NSLOCTEXT("SMixtormat", "PreviewOutputRockChamferRamp", "Chamfer Ramp"),
				EMixtormatPreviewOutputKind::Mask, true, true, true, NAME_None});
			Result.Outputs.Add({FName(TEXT("RockWallRamp")),
				NSLOCTEXT("SMixtormat", "PreviewOutputRockWallRamp", "Wall Ramp"),
				EMixtormatPreviewOutputKind::Mask, true, true, true, NAME_None});
		}
		else if (Child.Generator.Type == EMixtormatGeneratorType::StrataCarver)
		{
			// Bed IDs feed the ID consumers below this row. Position is 0..1 up each bed, the
			// natural coordinate for a colour ramp; random is one value per bed.
			Result.Outputs.Add({NAME_None, RegionIdsLabel, EMixtormatPreviewOutputKind::RegionIds,
				false, true, false, NAME_None});
			Result.Outputs.Add({FName(TEXT("StrataPosition")),
				NSLOCTEXT("SMixtormat", "PreviewOutputStrataPosition", "Bed Position"),
				EMixtormatPreviewOutputKind::Mask, true, true, true, NAME_None});
			Result.Outputs.Add({FName(TEXT("StrataRandom")),
				NSLOCTEXT("SMixtormat", "PreviewOutputStrataRandom", "Bed Random"),
				EMixtormatPreviewOutputKind::Mask, true, true, true, NAME_None});
		}
		else if (Child.Generator.Type == EMixtormatGeneratorType::Pebbles)
		{
			Result.Outputs.Add({NAME_None, RegionIdsLabel, EMixtormatPreviewOutputKind::RegionIds,
				false, true, false, NAME_None});
			Result.Outputs.Add({FName(TEXT("PebbleCoverage")),
				NSLOCTEXT("SMixtormat", "PreviewOutputPebbleCoverage", "Coverage"),
				EMixtormatPreviewOutputKind::Mask, true, true, true, NAME_None});
			Result.Outputs.Add({FName(TEXT("PebbleEdgeDistance")),
				NSLOCTEXT("SMixtormat", "PreviewOutputPebbleEdgeDistance", "Edge Distance"),
				EMixtormatPreviewOutputKind::SignedDistance, true, true, true, NAME_None});
			Result.Outputs.Add({FName(TEXT("PebbleRandom")),
				NSLOCTEXT("SMixtormat", "PreviewOutputPebbleRandom", "Random"),
				EMixtormatPreviewOutputKind::Mask, true, true, true, NAME_None});
		}
		break;
	case EMixtormatLayerChildType::Effect:
		if (MixtormatIsGeneratorFlowEffect(EffectType))
		{
			Result.Outputs.Add({FName(TEXT("FlowDirection")),
				NSLOCTEXT("SMixtormat", "PreviewOutputFlowDirection", "Flow Direction"),
				EMixtormatPreviewOutputKind::FlowDirection, false, true,
				EffectType != EMixtormatEffectType::ShapeDeform, NAME_None});
			if (EffectType != EMixtormatEffectType::FlowCarve)
			{
				Result.Outputs.Add({FName(TEXT("WarpedUVGrid")),
					NSLOCTEXT("SMixtormat", "PreviewOutputWarpedUVGrid", "Warped UV Grid"),
					EMixtormatPreviewOutputKind::WarpedUVGrid, false, true,
										EffectType != EMixtormatEffectType::GeneratorFlow, NAME_None});
			}
			Result.Outputs.Add({FName(TEXT("Influence")),
				NSLOCTEXT("SMixtormat", "PreviewOutputInfluence", "Influence"),
				EMixtormatPreviewOutputKind::Mask, false, true, true, NAME_None});
			Result.Outputs.Add({FName(TEXT("Validity")),
				NSLOCTEXT("SMixtormat", "PreviewOutputValidity", "Validity"),
				EMixtormatPreviewOutputKind::Mask, false, true, true, NAME_None});
			if (EffectType == EMixtormatEffectType::FlowCarve)
			{
				Result.Outputs.Add({FName(TEXT("CarveMask")),
					NSLOCTEXT("SMixtormat", "PreviewOutputCarveMask", "Carve Mask"),
					EMixtormatPreviewOutputKind::Mask, false, true, false, NAME_None});
			}
		}
		else if (EffectType == EMixtormatEffectType::Breakup)
		{
			// Region IDs has no invalid-pixel concept of its own -- it is a separate pass from
			// Gap -- so PreviewGapMaskName tells the compositor which published output to borrow
			// to blacken grout. This is the one case the generic gap-combine machinery exists for.
			Result.Outputs.Add({NAME_None, RegionIdsLabel, EMixtormatPreviewOutputKind::RegionIds,
				false, true, false, GapName});
			Result.Outputs.Add({GapName, GapLabel, EMixtormatPreviewOutputKind::Mask,
				true, true, true, NAME_None});
			Result.Outputs.Add({FName(TEXT("Edge")),
				NSLOCTEXT("SMixtormat", "PreviewOutputEdge", "Edge"), EMixtormatPreviewOutputKind::Mask,
				true, true, true, NAME_None});
			Result.Outputs.Add({FName(TEXT("Pieces")),
				NSLOCTEXT("SMixtormat", "PreviewOutputPieces", "Pieces"), EMixtormatPreviewOutputKind::Mask,
				true, true, true, NAME_None});
		}
		else if (EffectType == EMixtormatEffectType::WornEdges)
		{
			Result.Outputs.Add({FName(TEXT("Wear")),
				NSLOCTEXT("SMixtormat", "PreviewOutputWear", "Wear"), EMixtormatPreviewOutputKind::Mask,
				true, true, false, NAME_None});
		}
		break;
	default:
		// Everything else (Grade, Layer Blur, Flow Warp, Erosion, Blur, Curvature, HSV/Random
		// From IDs, Peeling) publishes nothing a preview or Copy Output could use.
		//
		// UV From IDs and Relief From IDs belong here too, and deliberately. Neither publishes a
		// scalar another node could read: UV From IDs changes the coordinate the layer's source is
		// sampled at, which the material preview already shows, and Relief From IDs writes into
		// the composited height, normal and RAM through the shared relief path rather than
		// emitting a field of its own. An eye on either would have to invent a texture to show,
		// and a preview that shows something the node does not actually publish is worse than no
		// preview -- see priorities.md on hardcoded preview tables.
		break;
	}
	return Result;
}

FMixtormatChildCapabilities GetChildCapabilitiesForChildType(const EMixtormatLayerChildType Type)
{
	FMixtormatLayerChild Probe;
	Probe.Type = Type;
	return GetChildCapabilities(Probe);
}

FMixtormatChildCapabilities GetChildCapabilitiesForEffectType(const EMixtormatEffectType EffectType)
{
	FMixtormatLayerChild Probe;
	Probe.Type = EMixtormatLayerChildType::Effect;
	Probe.Effect.ProceduralType = EffectType;
	return GetChildCapabilities(Probe);
}

TArray<FMixtormatPublishedOutputDesc> GetCopyableOutputs(const FMixtormatChildCapabilities& Capabilities)
{
	TArray<FMixtormatPublishedOutputDesc> Copyable;
	for (const FMixtormatPublishedOutputDesc& Output : Capabilities.Outputs)
	{
		if (Output.bCopyableAsMask)
		{
			Copyable.Add(Output);
		}
	}
	return Copyable;
}
