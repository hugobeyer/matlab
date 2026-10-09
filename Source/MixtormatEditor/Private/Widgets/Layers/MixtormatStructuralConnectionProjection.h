// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "Widgets/Layers/MixtormatStructuralConnectionModel.h"

enum class EMixtormatProjectedChildKind : uint8
{
	Ordinary,
	IncomingConnection,
	AuthoredRepair
};

// Display-only: indices into this array never address an authored child array.
struct FMixtormatProjectedChildRow
{
	FMixtormatChildAddress Address;
	int32 AuthoredChildIndex = INDEX_NONE;
	EMixtormatProjectedChildKind Kind = EMixtormatProjectedChildKind::Ordinary;
	int32 VisualParentRowIndex = INDEX_NONE;
	int32 AuthoredScopeDepth = 0;
	bool bInIncomingBlock = false;
	int32 ScopeDepthWithinIncoming = 0;
	int32 ModuleAuthoredIndex = INDEX_NONE;
	MixtormatOutputReferences::FStructuralLinkStatus Status;
	// Missing/ambiguous evaluation retains authored endpoints, without claiming they resolved.
	bool bHasResolvedPayload = false;
	bool bModuleEnabled = true;
	FMixtormatOutputReference ResolvedSource;
	FGuid ResolvedTargetId;
	FText PresentationReason;
	// Exclusive authored boundary; INDEX_NONE means no safe contiguous subtree boundary.
	int32 AuthoredSubtreeEnd = INDEX_NONE;
};

namespace MixtormatStructuralConnections
{
	// Includes every local authored child once. Shared group children keep their group home.
	TArray<FMixtormatProjectedChildRow> BuildChildProjection(
		const TArray<FMixtormatLayer>& Layers,
		const TArray<FMixtormatLayerGroup>& Groups,
		int32 LayerIndex);
}
