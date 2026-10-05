// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatChildScope.h"

namespace MixtormatChildScope
{
	int32 FindIndexById(const TArray<FMixtormatLayerChild>& Children, const FGuid& ChildId)
	{
		if (!ChildId.IsValid())
		{
			return INDEX_NONE;
		}
		return Children.IndexOfByPredicate([&ChildId](const FMixtormatLayerChild& Child)
		{
			return Child.ChildId == ChildId;
		});
	}

	int32 ResolveOwnerIndex(const TArray<FMixtormatLayerChild>& Children, const int32 ChildIndex)
	{
		if (!Children.IsValidIndex(ChildIndex))
		{
			return INDEX_NONE;
		}
		const FGuid& OwnerId = Children[ChildIndex].ScopeOwnerChildId;
		if (!OwnerId.IsValid() || OwnerId == Children[ChildIndex].ChildId)
		{
			return INDEX_NONE;
		}
		const int32 OwnerIndex = FindIndexById(Children, OwnerId);
		if (OwnerIndex == INDEX_NONE || OwnerIndex >= ChildIndex)
		{
			return INDEX_NONE;
		}
		return OwnerIndex;
	}

	bool SanitizeStaleOwners(TArray<FMixtormatLayerChild>& Children)
	{
		bool bChanged = false;
		for (int32 Index = 0; Index < Children.Num(); ++Index)
		{
			if (!Children[Index].ScopeOwnerChildId.IsValid())
			{
				continue;
			}
			if (ResolveOwnerIndex(Children, Index) == INDEX_NONE)
			{
				Children[Index].ScopeOwnerChildId.Invalidate();
				bChanged = true;
			}
		}
		return bChanged;
	}
}
