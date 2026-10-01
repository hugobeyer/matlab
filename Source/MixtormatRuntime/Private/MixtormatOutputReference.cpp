// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatOutputReference.h"
#include "MixtormatMaterial.h"

namespace MixtormatOutputReferences
{
	int32 ResolveEarlierSource(const TArray<FMixtormatLayer>& Layers,
		const int32 DestinationLayerIndex, const FMixtormatOutputReference& Reference)
	{
		if (!Reference.bEnabled || !Reference.HasSource()
			|| !Layers.IsValidIndex(DestinationLayerIndex))
		{
			return INDEX_NONE;
		}
		const FName Expected = Reference.Kind == EMixtormatPublishedFieldKind::RegionIds
			? FName(TEXT("RegionIds")) : (Reference.Kind == EMixtormatPublishedFieldKind::Flow
				? FName(TEXT("FlowDirection")) : FName(TEXT("WarpedUV")));
		if (Reference.OutputName != Expected
			|| (Reference.Kind != EMixtormatPublishedFieldKind::RegionIds
				&& Reference.Kind != EMixtormatPublishedFieldKind::Flow
				&& Reference.Kind != EMixtormatPublishedFieldKind::UVMap))
		{
			return INDEX_NONE;
		}
		for (int32 LayerIndex = 0; LayerIndex < DestinationLayerIndex; ++LayerIndex)
		{
			const FMixtormatLayer& Layer = Layers[LayerIndex];
			if (Layer.LayerId == Reference.SourceLayerId && Layer.bEnabled)
			{
				return Layer.Children.IndexOfByPredicate([&Reference](const FMixtormatLayerChild& Child)
				{
					return Child.ChildId == Reference.SourceChildId;
				});
			}
		}
		return INDEX_NONE;
	}
}
