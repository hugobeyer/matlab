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
	case EMixtormatLayerChildType::Generator:
		if (Child.Generator.Type == EMixtormatGeneratorType::Fracture)
		{
			Result.Outputs.Add({FName(TEXT("Fracture")),
				NSLOCTEXT("SMixtormat", "PreviewOutputFracture", "Fracture"),
				EMixtormatPreviewOutputKind::Mask, true, true, true, NAME_None});
			Result.Outputs.Add({FName(TEXT("FaceProgress")),
				NSLOCTEXT("SMixtormat", "PreviewOutputFractureFaceProgress", "Face Progress"),
				EMixtormatPreviewOutputKind::Mask, true, true, true, NAME_None});
			Result.Outputs.Add({FName(TEXT("FractureHeight")),
				NSLOCTEXT("SMixtormat", "PreviewOutputFractureHeight", "Fracture Height"),
				EMixtormatPreviewOutputKind::Mask, true, true, true, NAME_None});
		}
		break;
	case EMixtormatLayerChildType::Effect:
		if (EffectType == EMixtormatEffectType::Breakup)
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
		// Everything else (Grade, Layer Blur, Flow Warp, Erosion, Blur, Curvature, HSV/Ramp/Random
		// From IDs, Strata Carver, Peeling) publishes nothing a preview or Copy Output could use.
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
