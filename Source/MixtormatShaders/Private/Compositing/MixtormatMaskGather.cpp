// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Compositing/MixtormatMaskGather.h"

namespace MixtormatGpuCompositor
{
bool GatherMaskChild(FLayerRenderData& Data, const FMixtormatLayer& Layer,
	const FMixtormatLayerChild& LayerChild, const int32 SourceChildIndex,
	const TArray<FMixtormatLayer>& EffectiveLayers)
{
	// Consumed by the mask it is scoped to, never a node of its own down here. Skipped
	// explicitly so it cannot fall through to whatever handles an unrecognised type.
	if (LayerChild.Type == EMixtormatLayerChildType::Blur
		|| LayerChild.Type == EMixtormatLayerChildType::Curvature)
	{
		return true;
	}

	if (LayerChild.Type == EMixtormatLayerChildType::Mask)
	{
		const FMixtormatMaskLayer& MaskLayer = LayerChild.Mask;
		if (!MaskLayer.bEnabled)
		{
			return true;
		}

		const bool bPublishedSource = MaskLayer.HasPublishedSource();
		int32 PublishedSourceChildIndex = INDEX_NONE;
		if (bPublishedSource)
		{
			for (const FMixtormatLayer& SourceLayer : EffectiveLayers)
			{
				if (SourceLayer.LayerId != MaskLayer.PublishedSourceLayerId)
				{
					continue;
				}
				PublishedSourceChildIndex = SourceLayer.Children.IndexOfByPredicate(
					[&MaskLayer](const FMixtormatLayerChild& Candidate)
					{
						return Candidate.ChildId == MaskLayer.PublishedSourceChildId;
					});
				break;
			}
		}

		// A Layer Values mask reads the layer it sits on, so it needs no asset at all --
		// which is also why it cannot be dropped for the want of one the way a texture
		// mask is below.
		const bool bLayerValues = MaskLayer.UsesLayerValues();

		UTexture2D* MaskTexture = nullptr;
		if (!bPublishedSource && !bLayerValues)
		{
			MaskTexture = MaskLayer.MaskTexture.LoadSynchronous();
			if (!MaskTexture)
			{
				if (const UMixtormatMask* MaskAsset = MaskLayer.Mask.LoadSynchronous())
				{
					MaskTexture = MaskAsset->MaskTexture.Get();
				}
			}
			if (!MaskTexture)
			{
				return true;
			}
		}

		FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
		ChildData.Type = EMixtormatLayerChildType::Mask;
		ChildData.SourceChildIndex = SourceChildIndex;
		if (LayerChild.ScopeOwnerChildId.IsValid())
		{
			const int32 OwnerIndex = Layer.Children.IndexOfByPredicate(
				[&LayerChild](const FMixtormatLayerChild& Candidate)
				{
					return Candidate.ChildId == LayerChild.ScopeOwnerChildId;
				});
			if (Layer.Children.IsValidIndex(OwnerIndex)
				&& OwnerIndex < SourceChildIndex
			&& (Layer.Children[OwnerIndex].Type == EMixtormatLayerChildType::Effect
				|| Layer.Children[OwnerIndex].Type == EMixtormatLayerChildType::Generator))
			{
				ChildData.ScopeOwnerSourceChildIndex = OwnerIndex;
			}
			else
			{
				// A scoped mask whose owner cannot be resolved as a preceding Effect
				// is inactive. Never let it fall through as a layer-wide mask.
				Data.Children.RemoveAt(Data.Children.Num() - 1);
				return true;
			}
		}
		FMaskRenderData& MaskData = ChildData.Mask;
		if (bPublishedSource)
		{
			MaskData.PublishedSourceLayerId = MaskLayer.PublishedSourceLayerId;
			MaskData.PublishedSourceChildIndex = PublishedSourceChildIndex;
			MaskData.PublishedSourceOutput = MaskLayer.PublishedSourceOutput;
		}
		else if (bLayerValues)
		{
			MaskData.bLayerValues = true;
			MaskData.SourceChannel =
				static_cast<int32>(MaskLayer.LayerValueChannel);
		}
		else
		{
			MaskData.Texture = GetTextureRHI(MaskTexture);
			if (!MaskData.Texture.IsValid())
			{
				return false;
			}
		}
		MaskData.BlendMode = MaskLayer.BlendMode;
		MaskData.Weight = MaskLayer.Weight;
		// Integer per axis: the shader wraps the read in a frac(), and a fractional
		// scale lands mid-cell at that wrap.
		MaskData.Tiling = FVector2f(
			static_cast<float>(FMath::Max(MaskLayer.TilingX, 1)),
			static_cast<float>(FMath::Max(MaskLayer.TilingY, 1)));
		MaskData.UVOffset = FVector2f(MaskLayer.UVOffsetX, MaskLayer.UVOffsetY);
		MaskData.bFlipU = MaskLayer.bFlipU;
		MaskData.bFlipV = MaskLayer.bFlipV;
		MaskData.Rotation = static_cast<int32>(MaskLayer.Rotation);
		static_cast<FMixtormatMaskShaping&>(MaskData) = MaskLayer.Shaping;
		// Blur is a node in the recipe -- so it can be driven, published and instanced --
		// but a pair of numbers by the time the passes see it. Summed rather than maxed:
		// two blurs stacked on one mask should soften more than either alone, which is
		// what stacking them plainly means. Clamped to the shader's 32-tap unroll.
		for (const FMixtormatLayerChild& BlurChild : Layer.Children)
		{
			if (BlurChild.Type != EMixtormatLayerChildType::Blur
				|| BlurChild.ScopeOwnerChildId != LayerChild.ChildId
				|| !BlurChild.Blur.bEnabled)
			{
				continue;
			}
			MaskData.BlurRadiusX += BlurChild.Blur.RadiusX;
			MaskData.BlurRadiusY += BlurChild.Blur.RadiusY;
		}
		MaskData.BlurRadiusX = FMath::Min(MaskData.BlurRadiusX, 32.0f);
		MaskData.BlurRadiusY = FMath::Min(MaskData.BlurRadiusY, 32.0f);
		// Gathered in the order they appear, and kept as a list rather than reduced: each
		// one narrows what the one before it left, so two of them are not one of anything.
		for (const FMixtormatLayerChild& CurvatureChild : Layer.Children)
		{
			if (CurvatureChild.Type != EMixtormatLayerChildType::Curvature
				|| CurvatureChild.ScopeOwnerChildId != LayerChild.ChildId
				|| !CurvatureChild.Curvature.KeepsAnything())
			{
				continue;
			}
			MaskData.CurvatureFilters.Add(CurvatureChild.Curvature);
		}
		if (ChildData.ScopeOwnerSourceChildIndex == INDEX_NONE)
		{
			Data.bHasMask = true;
		}
		return true;
	}

	if (LayerChild.Type == EMixtormatLayerChildType::Generated)
	{
		const FMixtormatGeneratedMask& GeneratedMask = LayerChild.Generated;
		if (!GeneratedMask.bEnabled || !GeneratedMask.HasAnySignal())
		{
			return true;
		}

		FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
		ChildData.Type = EMixtormatLayerChildType::Generated;
		ChildData.SourceChildIndex = SourceChildIndex;
		FGeneratedMaskRenderData& GeneratedData = ChildData.Generated;
		GeneratedData.CurvatureWeight = GeneratedMask.CurvatureWeight;
		GeneratedData.CurvatureBias = GeneratedMask.CurvatureBias;
		GeneratedData.CurvatureStrength = GeneratedMask.CurvatureStrength;
		GeneratedData.CurvaturePower = FMath::Max(GeneratedMask.CurvaturePower, 0.001f);
		GeneratedData.DirectionWeight = GeneratedMask.DirectionWeight;
		GeneratedData.DirectionAngle = GeneratedMask.DirectionAngle;
		GeneratedData.DirectionBroadness = FMath::Max(GeneratedMask.DirectionBroadness, 0.001f);
		GeneratedData.AOWeight = GeneratedMask.AOWeight;
		GeneratedData.HeightWeight = GeneratedMask.HeightWeight;
		GeneratedData.HeightBias = GeneratedMask.HeightBias;
		GeneratedData.bNormalizeWeights = GeneratedMask.bNormalizeWeights;
		GeneratedData.Broadness = FMath::Max(GeneratedMask.Broadness, 1);
		GeneratedData.Smoothing = FMath::Max(GeneratedMask.Smoothing, 1);
		GeneratedData.Bias = FMath::Max(GeneratedMask.Bias, 0.001f);
		GeneratedData.WarpAmount = GeneratedMask.WarpAmount;
		GeneratedData.WarpSource = GeneratedMask.WarpSource;
		GeneratedData.WarpRadius = FMath::Max(GeneratedMask.WarpRadius, 1);
		GeneratedData.BlendMode = GeneratedMask.BlendMode;
		GeneratedData.Weight = GeneratedMask.Weight;
		static_cast<FMixtormatMaskShaping&>(GeneratedData) = GeneratedMask.Shaping;
		GeneratedData.RidgeWeight = GeneratedMask.RidgeWeight;
		Data.bHasMask = true;
		return true;
	}

	if (LayerChild.Type == EMixtormatLayerChildType::Craquelure)
	{
		const FMixtormatCraquelure& Craquelure = LayerChild.Craquelure;

		// No HasAnySignal() equivalent: this node has one signal and it is always on.
		// Weight 0 is how a craquelure node is muted, the same as any other mask.
		if (!Craquelure.bEnabled)
		{
			return true;
		}

		FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
		ChildData.Type = EMixtormatLayerChildType::Craquelure;
		ChildData.SourceChildIndex = SourceChildIndex;
		FCraquelureRenderData& CrackData = ChildData.Craquelure;

		// The authored parameters are the ones a user thinks in; the growth kernel wants
		// the ones it was written against. This is where the one becomes the other, so the
		// shaders keep the maths they were tuned with and the consolidation costs nothing
		// at runtime.
		//
		// The clamps guard the lattice rather than taste: a period is a wrap modulus and a
		// non-positive one divides the hash by zero.
		const int32 CrackScale = FMath::Max(Craquelure.Scale, 1);
		const float CrackJitter = Craquelure.Jitter;

		// One Scale, read as the cell count by whichever mode is running.
		CrackData.Period = CrackScale;
		CrackData.SeedCells = CrackScale;
		CrackData.Jitter = CrackJitter;
		CrackData.SeedJitter = CrackJitter;

		CrackData.Seed = static_cast<uint32>(Craquelure.Seed);
		// The warp rides the network's seed rather than carrying its own. An offset, not the
		// same value, so reseeding moves both without the two fields ever sharing a hash.
		CrackData.WarpSeed = CrackData.Seed + 7919u;
		CrackData.WarpPeriod = FMath::Max(Craquelure.WarpScale, 1);
		CrackData.Warp = Craquelure.Warp;

		CrackData.Width = Craquelure.Width;
		CrackData.Variation = Craquelure.Variation;
		CrackData.BlendMode = Craquelure.BlendMode;
		CrackData.Weight = Craquelure.Weight;
		static_cast<FMixtormatMaskShaping&>(CrackData) = Craquelure.Shaping;

		CrackData.Mode = Craquelure.Mode;
		CrackData.ReliefDepth = Craquelure.ReliefDepth;
		CrackData.ReliefWidth = FMath::Max(Craquelure.ReliefWidth, 0.002f);
		CrackData.ReliefProfile = FMath::Max(Craquelure.ReliefProfile, 0.05f);
		CrackData.ReliefGrooveVariation = Craquelure.ReliefGrooveVariation;
		CrackData.ReliefProfileVariation = Craquelure.ReliefProfileVariation;
		CrackData.ReliefWidthVariation = Craquelure.ReliefWidthVariation;
		CrackData.Iterations = FMath::Max(Craquelure.Iterations, 1);
		CrackData.SeedChance = Craquelure.Density;

		// Detail is a multiple of Scale, so the fields keep their size relative to the
		// pieces when Scale moves. As an absolute cell count it fought Scale on every drag.
		CrackData.NoiseCells = FMath::Max(FMath::RoundToInt(CrackScale * FMath::Max(Craquelure.Detail, 0.1f)), 1);

		// Stress and toughness vary by the same amount, from independent noise. They were two
		// dials for the two ends of one balance.
		const float FieldContrast = Craquelure.FieldContrast;
		CrackData.StressVariation = FieldContrast;
		CrackData.ToughnessVariation = FieldContrast;

		// Likewise the two weights on opposite signs of the same comparison.
		const float FractureBias = Craquelure.FractureBias;
		CrackData.StressGain = FractureBias;
		CrackData.ToughnessCost = FractureBias;

		// Straightness drives both halves of holding a heading: how much alignment counts in
		// the score, and how fast the stored direction follows the step actually taken. They
		// run opposite ways -- a straighter crack weights alignment more and turns slower --
		// which is exactly why two dials for it were easy to set against each other. The
		// constants put the old defaults near 0.35.
		const float Straightness = Craquelure.Straightness;
		CrackData.Persistence = Straightness * 6.0f;
		CrackData.TurnResponse = FMath::Clamp(1.0f - Straightness * 0.8f, 0.0f, 1.0f);

		CrackData.FlowStrength = Craquelure.Flow;
		CrackData.Irregularity = Craquelure.Roughness;
		CrackData.GrowthThreshold = Craquelure.GrowthThreshold;
		CrackData.CollisionLimit = FMath::Max(Craquelure.CollisionLimit, 1);

		// Built from the clamped values rather than the authored ones, so two settings
		// the clamps map onto the same network share a cache entry -- and, more to the
		// point, so a key can never describe a network the shader would not produce.
		//
		// Mode is in the key because the two modes build entirely different fields from
		// overlapping parameters. Width, Variation, the blend tail and every relief
		// control are deliberately absent: they shape the field after it exists, and
		// including them would miss on exactly the sliders most likely to be dragged.
		{
			uint64 Key = MixtormatNetworkKey::Seed();
			const uint8 ModeByte = static_cast<uint8>(CrackData.Mode);
			Key = MixtormatNetworkKey::Add(Key, ModeByte);
			Key = MixtormatNetworkKey::Add(Key, CrackData.Seed);
			// Warp is deliberately absent. It bends where the finished network is read
			// from rather than how it grows, so the cached distance field stays valid
			// across a warp change -- which turns dragging the dial from a full regrow of
			// the most expensive node in the graph into one resolve pass.
			if (CrackData.Mode == EMixtormatCraquelureMode::Propagated)
			{
				Key = MixtormatNetworkKey::Add(Key, CrackData.Iterations);
				Key = MixtormatNetworkKey::Add(Key, CrackData.SeedCells);
				Key = MixtormatNetworkKey::Add(Key, CrackData.SeedChance);
				Key = MixtormatNetworkKey::Add(Key, CrackData.SeedJitter);
				Key = MixtormatNetworkKey::Add(Key, CrackData.NoiseCells);
				Key = MixtormatNetworkKey::Add(Key, CrackData.StressVariation);
				Key = MixtormatNetworkKey::Add(Key, CrackData.ToughnessVariation);
				Key = MixtormatNetworkKey::Add(Key, CrackData.Persistence);
				Key = MixtormatNetworkKey::Add(Key, CrackData.FlowStrength);
				Key = MixtormatNetworkKey::Add(Key, CrackData.StressGain);
				Key = MixtormatNetworkKey::Add(Key, CrackData.ToughnessCost);
				Key = MixtormatNetworkKey::Add(Key, CrackData.Irregularity);
				Key = MixtormatNetworkKey::Add(Key, CrackData.GrowthThreshold);
				Key = MixtormatNetworkKey::Add(Key, CrackData.TurnResponse);
				Key = MixtormatNetworkKey::Add(Key, CrackData.CollisionLimit);
			}
			else
			{
				Key = MixtormatNetworkKey::Add(Key, CrackData.Period);
				Key = MixtormatNetworkKey::Add(Key, CrackData.Jitter);
			}
			CrackData.NetworkKey = Key;
		}
		// Unconditional now that height and normal are their own weights rather than an
		// output mode. The node always contributes a mask; Weight 0 is how that half is
		// muted, exactly as on every other mask child.
		Data.bHasMask = true;
		return true;
	}
	return true;
}
}
