// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatLayerTypes.h"

// One lookup for ScopeOwnerChildId. Editor placement and gather evaluation both use this so a
// GUID the UI still shows cannot evaluate as a different owner, or as a layer-wide mask.
namespace MixtormatChildScope
{
	MIXTORMATRUNTIME_API int32 FindIndexById(
		const TArray<FMixtormatLayerChild>& Children,
		const FGuid& ChildId);

	// INDEX_NONE when the owner id is invalid, missing, self, or not strictly before ChildIndex.
	MIXTORMATRUNTIME_API int32 ResolveOwnerIndex(
		const TArray<FMixtormatLayerChild>& Children,
		int32 ChildIndex);

	// V2 Behaviors must be directly owned by an earlier Generator child. Never silently
	// accept a layer-wide/unscoped Behavior or an indirect Mask/Effect parent.
	MIXTORMATRUNTIME_API int32 ResolveBehaviorGeneratorIndex(
		const TArray<FMixtormatLayerChild>& Children,
		int32 BehaviorChildIndex);

	// Clears ScopeOwnerChildId when ResolveOwnerIndex would return INDEX_NONE. Returns true if
	// any child changed.
	MIXTORMATRUNTIME_API bool SanitizeStaleOwners(TArray<FMixtormatLayerChild>& Children);

	// Who may have a Mask child scoped underneath. Gather and Editor both call this.
	MIXTORMATRUNTIME_API bool CanOwnScopedMasks(const FMixtormatLayerChild& Child);
}
