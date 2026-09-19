// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatMaterial.h"

// Groups resolve to ordinary layers before anything renders.
//
// The whole feature is one transform: a group's authored child stack is copied onto each of its
// member layers, and the group's enable gates theirs. Everything downstream -- validation,
// reference resolution, render-data gathering, the render graph, the shaders -- goes on seeing a
// flat array of authored-looking layers and never learns what a group is. That is deliberate: the
// alternative, teaching each GPU pass about groups, would put the same rule in a dozen places.
namespace MixtormatLayerGroups
{
	// Repairs serialized group data in place. Layer order is never touched: a group whose members
	// are no longer adjacent keeps its first contiguous run and loses the strays, because silently
	// reordering a user's stack to satisfy an invariant is worse than dropping the membership.
	MIXTORMATRUNTIME_API void ValidateGroups(
		TArray<FMixtormatLayer>& Layers,
		TArray<FMixtormatLayerGroup>& Groups);

	// False when the layers and groups compose exactly as they are, which is every document that
	// has no groups. Lets the compositor skip the copy on the common path -- this runs on every
	// frame of a slider drag.
	MIXTORMATRUNTIME_API bool RequiresExpansion(
		const TArray<FMixtormatLayer>& Layers,
		const TArray<FMixtormatLayerGroup>& Groups);

	// The array the compositor actually walks. Same length, same order, same LayerIds as the
	// input, so every layer index -- HeightReferenceLayerIndex, debug indices, ping-pong parity --
	// still means what it meant. Only bEnabled and the tail of Children differ, and only on
	// members. Authored data is not modified.
	MIXTORMATRUNTIME_API void BuildEffectiveLayers(
		const TArray<FMixtormatLayer>& Layers,
		const TArray<FMixtormatLayerGroup>& Groups,
		TArray<FMixtormatLayer>& OutLayers);

	// The identity one group child takes on inside one member layer.
	//
	// Derived rather than random because the same child has to come back with the same ID on the
	// next composite: a debug preview pinned to a shared effect, and any reference resolved
	// against one, would otherwise follow a new GUID every frame. Distinct per member, so two
	// members carrying the same shared effect do not collide in the child-ID space.
	MIXTORMATRUNTIME_API FGuid MakeEffectiveChildId(
		const FGuid& GroupId,
		const FGuid& GroupChildId,
		const FGuid& MemberLayerId);

	MIXTORMATRUNTIME_API const FMixtormatLayerGroup* FindGroup(
		const TArray<FMixtormatLayerGroup>& Groups,
		const FGuid& GroupId);
	MIXTORMATRUNTIME_API FMixtormatLayerGroup* FindGroup(
		TArray<FMixtormatLayerGroup>& Groups,
		const FGuid& GroupId);

	// First and last layer index belonging to GroupId, or false when the group has no members.
	MIXTORMATRUNTIME_API bool GetGroupRange(
		const TArray<FMixtormatLayer>& Layers,
		const FGuid& GroupId,
		int32& OutFirstIndex,
		int32& OutLastIndex);
}
