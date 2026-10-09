// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "MixtormatLayerGroups.h"
#include "MixtormatOutputReference.h"
#include "Widgets/MixtormatChildAddress.h"

namespace MixtormatStructuralConnections
{
	using EIssue = MixtormatOutputReferences::EStructuralLinkIssue;

	// Full canonical descriptions shared by endpoint editing and connected creation.
	FText IssueText(EIssue Issue);
}

// Indices address the supplied authored layers for adapter-side label formatting.
// Status indices instead address the context's effective projection.
struct FMixtormatStructuralSourceCandidate
{
	FMixtormatChildAddress SourceAddress;
	FMixtormatOutputReference Source;
	int32 LayerIndex = INDEX_NONE;
	int32 ChildIndex = INDEX_NONE;
	MixtormatOutputReferences::FStructuralLinkStatus Status;
	MixtormatStructuralConnections::EIssue Issue = MixtormatStructuralConnections::EIssue::None;
};

// Owned transient snapshot; no widgets, working-array references, or mutation/history state.
struct FMixtormatStructuralConnectionContext
{
	FMixtormatStructuralConnectionContext(const TArray<FMixtormatLayer>& Layers,
		const TArray<FMixtormatLayerGroup>& Groups, const FMixtormatChildAddress& Address);

	MixtormatOutputReferences::FStructuralLinkStatus Evaluate(
		const FMixtormatOutputReference* Source = nullptr, const FGuid* Target = nullptr) const;

	TArray<FMixtormatStructuralSourceCandidate> CollectSources(
		const TArray<FMixtormatLayer>& AuthoredLayers, const FMixtormatOutputReference& CurrentSource) const;

	// The same binding-resolved destination used by Evaluate() without candidate overrides.
	const FMixtormatLayer* GetResolvedDestination() const;

	TArray<FMixtormatLayer> Effective;
	int32 LayerIndex = INDEX_NONE;
	int32 ChildIndex = INDEX_NONE;

private:
	TArray<FMixtormatLayerGroup> Groups;
	FMixtormatLayer ResolvedDestination;
	MixtormatStructuralConnections::EIssue AddressIssue = MixtormatStructuralConnections::EIssue::MissingLayer;
};
