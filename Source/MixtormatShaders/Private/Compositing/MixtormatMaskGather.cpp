// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Compositing/MixtormatMaskGather.h"
#include "MixtormatChildScope.h"
#include "MixtormatOutputReference.h"
#include "MixtormatScalarRampMath.h"

namespace MixtormatGpuCompositor
{
static void GatherOwnedBlurs(const FMixtormatLayer& Layer, const int32 OwnerIndex,
	const TArray<FMixtormatLayer>& EffectiveLayers, TArray<FBlurRenderData, TInlineAllocator<2>>& OutBlurs)
{
	if (!Layer.Children.IsValidIndex(OwnerIndex)
		|| !MixtormatChildScope::CanOwnScopedBlurs(Layer.Children[OwnerIndex])) { return; }
	for (int32 Index = OwnerIndex + 1; Index < Layer.Children.Num(); ++Index)
	{
		const FMixtormatLayerChild& Child = Layer.Children[Index];
		if (Child.Type != EMixtormatLayerChildType::Blur || !Child.Blur.bEnabled
			|| MixtormatChildScope::ResolveOwnerIndex(Layer.Children, Index) != OwnerIndex)
		{
			continue;
		}
		const FBlurRenderData Blur = GatherBlur(Child.Blur, Index, Layer, EffectiveLayers);
		if (Blur.BlursAnything()) { OutBlurs.Add(Blur); }
	}
}

bool GatherMaskChild(FLayerRenderData& Data, const FMixtormatLayer& Layer,
	const FMixtormatLayerChild& LayerChild, const int32 SourceChildIndex,
	const TArray<FMixtormatLayer>& EffectiveLayers)
{
	if (LayerChild.Type == EMixtormatLayerChildType::Blur)
	{
		// Scoped Mask/Curvature Blurs are gathered by their owner. Root Blurs
		// filter only this layer's completed incoming surface at its rail end.
		if (Layer.bEnabled && !LayerChild.ScopeOwnerChildId.IsValid())
		{
			const FBlurRenderData Blur = GatherBlur(LayerChild.Blur, SourceChildIndex, Layer, EffectiveLayers);
			if (Blur.BlursAnything() && Blur.HasLayerChannelWeight())
			{
				Data.SurfaceBlurs.Add(Blur);
			}
		}
		return true;
	}
	if (LayerChild.Type == EMixtormatLayerChildType::Curvature)
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
			const int32 LayerIndex = EffectiveLayers.IndexOfByPredicate(
				[&Layer](const FMixtormatLayer& Candidate) { return Candidate.LayerId == Layer.LayerId; });
			PublishedSourceChildIndex = MixtormatOutputReferences::ResolvePublishedMaskSource(
				EffectiveLayers, LayerIndex, SourceChildIndex, MaskLayer);
		}

		// A Layer Values mask reads the layer it sits on, so it needs no asset at all --
		// which is also why it cannot be dropped for the want of one the way a texture
		// mask is below.
		const bool bLayerValues = MaskLayer.UsesLayerValues();
		const bool bNoise = MaskLayer.UsesNoise();

		UTexture2D* MaskTexture = nullptr;
		if (!bPublishedSource && !bLayerValues && !bNoise)
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
			const int32 OwnerIndex = MixtormatChildScope::ResolveOwnerIndex(Layer.Children, SourceChildIndex);
			if (OwnerIndex != INDEX_NONE
			&& MixtormatChildScope::CanOwnScopedMasks(Layer.Children[OwnerIndex]))
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
		MaskData.SourceChildIndex = SourceChildIndex;
		if (bPublishedSource)
		{
			MaskData.PublishedSourceLayerId = MaskLayer.PublishedSourceLayerId;
			MaskData.PublishedSourceChildIndex = PublishedSourceChildIndex;
			MaskData.PublishedSourceOutput = MaskLayer.PublishedSourceOutput;
		}
		else if (bNoise)
		{
			MaskData.bNoise = true;
			MaskData.Noise = MaskLayer.Noise;
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
		MaskData.HexTile = MaskLayer.Sampling == EMixtormatMaskSampling::HexTile ? 1u : 0u;
		MaskData.HexScale = static_cast<float>(FMath::Max(1, MaskLayer.HexScale));
		MaskData.HexRotationStrength = MaskLayer.HexRotationStrength;
		MaskData.HexContrast = MaskLayer.HexContrast;
		MaskData.HexSeed = static_cast<float>(MaskLayer.HexSeed);
		static_cast<FMixtormatMaskShaping&>(MaskData) = MaskLayer.Shaping;
		GatherOwnedBlurs(Layer, SourceChildIndex, EffectiveLayers, MaskData.Blurs);
		for (int32 Index = SourceChildIndex + 1; Index < Layer.Children.Num(); ++Index)
		{
			const FMixtormatLayerChild& CurvatureChild = Layer.Children[Index];
			if (CurvatureChild.Type != EMixtormatLayerChildType::Curvature
				|| MixtormatChildScope::ResolveOwnerIndex(Layer.Children, Index) != SourceChildIndex
				|| !CurvatureChild.Curvature.bEnabled)
			{
				continue;
			}
			FMaskCurvatureRenderData& Filter = MaskData.CurvatureFilters.AddDefaulted_GetRef();
			static_cast<FMixtormatMaskCurvature&>(Filter) = CurvatureChild.Curvature;
			GatherOwnedBlurs(Layer, Index, EffectiveLayers, Filter.Blurs);
			if (!Filter.KeepsAnything() && Filter.Blurs.IsEmpty())
			{
				MaskData.CurvatureFilters.Pop();
			}
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
		if (LayerChild.ScopeOwnerChildId.IsValid())
		{
			const int32 OwnerIndex = MixtormatChildScope::ResolveOwnerIndex(Layer.Children, SourceChildIndex);
			if (OwnerIndex != INDEX_NONE
				&& MixtormatChildScope::CanOwnScopedMasks(Layer.Children[OwnerIndex]))
			{
				ChildData.ScopeOwnerSourceChildIndex = OwnerIndex;
			}
			else
			{
				Data.Children.RemoveAt(Data.Children.Num() - 1);
				return true;
			}
		}
		FGeneratedMaskRenderData& GeneratedData = ChildData.Generated;
		GeneratedData.LayerMix = FMath::Clamp(GeneratedMask.LayerMix, 0.0f, 1.0f);
		GeneratedData.ConvexWeight = FMath::Max(GeneratedMask.ConvexWeight, 0.0f);
		GeneratedData.CavityWeight = FMath::Max(GeneratedMask.CavityWeight, 0.0f);
		GeneratedData.CurvatureStrength = FMath::Max(GeneratedMask.CurvatureStrength, 0.0f);
		GeneratedData.CurvatureMacroMix = FMath::Clamp(GeneratedMask.CurvatureMacroMix, 0.0f, 1.0f);
		GeneratedData.CurvatureRadius = FMath::Clamp(GeneratedMask.CurvatureRadius, 1, 32);
		GeneratedData.CurvatureSmoothing = FMath::Clamp(GeneratedMask.CurvatureSmoothing, 1, 4);

		const MixtormatScalarRampMath::FGpuPayload CurvPayload =
			MixtormatScalarRampMath::PrepareGpuPayload(GeneratedMask.CurvatureCurve);
		GeneratedData.CurvatureCurveCount = CurvPayload.PointCount;
		GeneratedData.CurvatureCurveInterpolation = CurvPayload.Interpolation;
		GeneratedData.CurvatureCurvePoints = CurvPayload.Points;

		GeneratedData.AOWeight = FMath::Max(GeneratedMask.AOWeight, 0.0f);
		GeneratedData.AORadius = FMath::Clamp(GeneratedMask.AORadius, 1.0f, 64.0f);
		GeneratedData.AOPower = FMath::Max(GeneratedMask.AOPower, 0.1f);
		GeneratedData.bAOInvert = GeneratedMask.bAOInvert;

		GeneratedData.SlopeWeight = FMath::Max(GeneratedMask.SlopeWeight, 0.0f);
		GeneratedData.SlopeMin = FMath::Clamp(GeneratedMask.SlopeMin, 0.0f, 90.0f) / 90.0f;
		GeneratedData.SlopeMax = FMath::Clamp(GeneratedMask.SlopeMax, 0.0f, 90.0f) / 90.0f;
		GeneratedData.SlopeSoftness = FMath::Clamp(GeneratedMask.SlopeSoftness, 0.005f, 1.0f);
		GeneratedData.bSlopeInvert = GeneratedMask.bSlopeInvert;

		GeneratedData.DirectionWeight = FMath::Max(GeneratedMask.DirectionWeight, 0.0f);
		const float Azimuth = FMath::DegreesToRadians(GeneratedMask.DirectionAngle);
		const float Elevation = FMath::DegreesToRadians(FMath::Clamp(GeneratedMask.DirectionElevation, 0.0f, 90.0f));
		GeneratedData.Direction = FVector3f(
			FMath::Cos(Azimuth) * FMath::Cos(Elevation),
			FMath::Sin(Azimuth) * FMath::Cos(Elevation),
			FMath::Sin(Elevation));
		GeneratedData.DirectionBroadness = FMath::Max(GeneratedMask.DirectionBroadness, 0.05f);
		GeneratedData.bDirectionInvert = GeneratedMask.bDirectionInvert;

		GeneratedData.HeightWeight = FMath::Max(GeneratedMask.HeightWeight, 0.0f);
		GeneratedData.HeightBias = GeneratedMask.HeightBias;

		const MixtormatScalarRampMath::FGpuPayload HeightPayload =
			MixtormatScalarRampMath::PrepareGpuPayload(GeneratedMask.HeightCurve);
		GeneratedData.HeightCurveCount = HeightPayload.PointCount;
		GeneratedData.HeightCurveInterpolation = HeightPayload.Interpolation;
		GeneratedData.HeightCurvePoints = HeightPayload.Points;

		GeneratedData.RidgeWeight = FMath::Max(GeneratedMask.RidgeWeight, 0.0f);

		const float TotalInfluence = FMath::Max(GeneratedData.ConvexWeight, GeneratedData.CavityWeight)
			+ GeneratedData.AOWeight + GeneratedData.SlopeWeight + GeneratedData.DirectionWeight
			+ GeneratedData.HeightWeight + GeneratedData.RidgeWeight;
		GeneratedData.MixScale = 1.0f / FMath::Max(TotalInfluence, 1.0f);

		GeneratedData.BlendMode = GeneratedMask.BlendMode;
		GeneratedData.Weight = GeneratedMask.Weight;
		static_cast<FMixtormatMaskShaping&>(GeneratedData) = GeneratedMask.Shaping;

		if (ChildData.ScopeOwnerSourceChildIndex == INDEX_NONE)
		{
			Data.bHasMask = true;
		}
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
		if (LayerChild.ScopeOwnerChildId.IsValid())
		{
			const int32 OwnerIndex = MixtormatChildScope::ResolveOwnerIndex(Layer.Children, SourceChildIndex);
			if (OwnerIndex != INDEX_NONE
				&& MixtormatChildScope::CanOwnScopedMasks(Layer.Children[OwnerIndex]))
			{
				ChildData.ScopeOwnerSourceChildIndex = OwnerIndex;
			}
			else
			{
				Data.Children.RemoveAt(Data.Children.Num() - 1);
				return true;
			}
		}
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
		if (ChildData.ScopeOwnerSourceChildIndex == INDEX_NONE)
		{
			Data.bHasMask = true;
		}
		return true;
	}
	return true;
}
}
