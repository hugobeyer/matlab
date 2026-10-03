// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "../MixtormatGpuCompositorInternal.h"

namespace MixtormatGpuCompositor
{
	void QueuePendingGrade(
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		const FEffectRenderData& Effect,
		FRDGTextureRef FeatureMask)
	{
		TArray<FPendingEffect, TInlineAllocator<2>>& PendingGrades = LayerCtx.PendingGrades;

		// Capture the scoped mask here; the composite shader grades the owning layer's
		// colour before blending it with the accumulated stack.
		FPendingEffect& Grade = PendingGrades.AddDefaulted_GetRef();
		Grade.Effect = &Effect;
		Grade.FeatureMask = FeatureMask;
		Grade.bHasScopedMask = Layer.Children.ContainsByPredicate(
			[&Child](const FChildRenderData& Candidate)
			{
				return Candidate.Type == EMixtormatLayerChildType::Mask
					&& Candidate.ScopeOwnerSourceChildIndex == Child.SourceChildIndex;
			});
	}
}
