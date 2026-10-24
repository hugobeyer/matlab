// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "Widgets/MixtormatChildCapabilities.h"
#include "MixtormatSurfacingTypes.h"

// The central child registry delegates its Surfacing case here. Names/kinds stay Runtime-owned.
inline FMixtormatChildCapabilities GetSurfacingCapabilities(const FMixtormatSurfacing& Surfacing)
{
	FMixtormatChildCapabilities Result;
	for (const FMixtormatSurfacingOutput& Output : MixtormatSurfacing::GetOutputs(Surfacing.Type))
	{
		const bool bUnit = Output.Kind == EMixtormatPublishedFieldKind::Scalar01;
		const bool bHeight = Output.Kind == EMixtormatPublishedFieldKind::ScalarSigned;
		const auto Preview = bHeight ? EMixtormatPreviewOutputKind::SignedDistance
			: Output.Kind == EMixtormatPublishedFieldKind::Color ? EMixtormatPreviewOutputKind::Color
			: Output.Kind == EMixtormatPublishedFieldKind::Flow ? EMixtormatPreviewOutputKind::FlowDirection
			: EMixtormatPreviewOutputKind::Mask;
		// SignedDistance is the existing signed preview colorist, not the published field kind.
		Result.Outputs.Add({Output.Name, Output.Label, Preview, bUnit && Output.bReferenceable, true, !bHeight,
			NAME_None, Output.bReferenceable, Output.Kind});
	}
	return Result;
}
