// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Compositing/MixtormatIdGather.h"

#include "Compositing/MixtormatComposeHash.h"
#include "MixtormatSurface.h"

namespace MixtormatGpuCompositor
{
	static int32 ResolveBoundarySource(const TArray<FMixtormatLayer>& Layers,
		const int32 LayerIndex, const int32 ChildIndex,
		const FMixtormatLayerChild& Destination, const FMixtormatOutputReference& Reference)
	{
		if (!Reference.bEnabled || !Reference.HasSource()
			|| Reference.Kind != EMixtormatPublishedFieldKind::RegionIds
			|| Reference.OutputName != FName(TEXT("RegionIds"))
			|| !Layers.IsValidIndex(LayerIndex))
		{
			return INDEX_NONE;
		}
		const FMixtormatLayer& Owner = Layers[LayerIndex];
		if (Reference.SourceLayerId != Owner.LayerId)
		{
			return MixtormatOutputReferences::ResolveEarlierSource(Layers, LayerIndex, Reference);
		}
		if (!Owner.bEnabled) { return INDEX_NONE; }
		for (int32 Index = 0; Index < ChildIndex && Index < Owner.Children.Num(); ++Index)
		{
			const FMixtormatLayerChild& Candidate = Owner.Children[Index];
			if (Candidate.ChildId == Reference.SourceChildId
				&& Candidate.ScopeOwnerChildId == Destination.ScopeOwnerChildId)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

bool GatherIdChild(FLayerRenderData& Data, const FMixtormatLayer& Layer,
	const FMixtormatLayerChild& LayerChild, const int32 SourceChildIndex, const int32 LayerIndex,
	const TArray<FMixtormatLayer>& EffectiveLayers, const UMixtormatSurface* Surface,
	UTexture2D* WhiteTexture, const bool bCacheLayers)
{
	if (LayerChild.Type == EMixtormatLayerChildType::HsvFilter)
	{
		// Rewrites albedo at the composite's own sample, so it never reaches the mask
		// chain. An empty palette is still valid -- the jitter rows work alone.
		const FMixtormatHsvIdFilter& Hsv = LayerChild.HsvFilter;
		if (!Layer.bEnabled || !Hsv.bEnabled)
		{
			return true;
		}
		FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
		ChildData.Type = EMixtormatLayerChildType::HsvFilter;
		ChildData.SourceChildIndex = SourceChildIndex;
		FHsvIdFilterRenderData& HsvData = ChildData.HsvFilter;
		const int32 PaletteCount =
			FMath::Min(Hsv.Palette.Num(), FMixtormatHsvIdFilter::MaxPaletteColors);
		HsvData.Palette.Reserve(PaletteCount);
		for (int32 ColorIndex = 0; ColorIndex < PaletteCount; ++ColorIndex)
		{
			HsvData.Palette.Add(FVector4f(Hsv.Palette[ColorIndex]));
		}
		HsvData.MixMin = Hsv.RampMixMin;
		HsvData.MixMax = Hsv.RampMixMax;
		HsvData.HueMin = Hsv.HueMin;
		HsvData.HueMax = Hsv.HueMax;
		HsvData.SatMin = Hsv.SaturationMin;
		HsvData.SatMax = Hsv.SaturationMax;
		HsvData.ValMin = Hsv.ValueMin;
		HsvData.ValMax = Hsv.ValueMax;
		HsvData.Seed = static_cast<uint32>(Hsv.Seed);
		return true;
	}

	if (LayerChild.Type == EMixtormatLayerChildType::RampId)
	{
		// Its pass runs after the composite, so a disabled layer must not gather it --
		// the same reason effects are skipped above.
		const FMixtormatRampIdFilter& Ramp = LayerChild.RampId;
		if (!Layer.bEnabled || !Ramp.bEnabled)
		{
			return true;
		}
		FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
		ChildData.Type = EMixtormatLayerChildType::RampId;
		ChildData.SourceChildIndex = SourceChildIndex;
		FRampIdRenderData& RampData = ChildData.RampId;
		RampData.HeightAmount = FMath::IsFinite(Ramp.HeightAmount)
			? Ramp.HeightAmount : 0.05f;
		RampData.IntensityRandom = Ramp.IntensityRandom;
		RampData.BlendMode = Ramp.BlendMode;
		RampData.bRotateRandom = Ramp.bRotateRandom;
		RampData.bAngleStepping = Ramp.bAngleStepping;
		RampData.AngleStepDegrees = FMath::IsFinite(Ramp.AngleStepDegrees)
			? FMath::Max(Ramp.AngleStepDegrees, 0.01f) : 5.0f;
		RampData.Seed = static_cast<uint32>(Ramp.Seed);
		return true;
	}

	if (LayerChild.Type == EMixtormatLayerChildType::OutputReference)
	{
		const FMixtormatOutputReference& Reference = LayerChild.OutputReference;
		const int32 SourceIndex = MixtormatOutputReferences::ResolveEarlierSource(
			EffectiveLayers, LayerIndex, Reference);
		if (!Layer.bEnabled || SourceIndex == INDEX_NONE
			|| (LayerChild.ScopeOwnerChildId.IsValid() && Reference.Kind != EMixtormatPublishedFieldKind::RegionIds))
		{
			return true;
		}
		FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
		ChildData.Type = EMixtormatLayerChildType::OutputReference;
		ChildData.SourceChildIndex = SourceChildIndex;
		FOutputReferenceRenderData& Out = ChildData.OutputReference;
		Out.Source = {Reference.SourceLayerId, SourceIndex, Reference.OutputName};
		Out.Kind = Reference.Kind;
		Out.FlowAmount = FMath::IsFinite(Reference.FlowAmount) ? Reference.FlowAmount : 0.0f;
		Out.FlowTraceLength = FMath::IsFinite(Reference.FlowTraceLength)
			? FMath::Max(Reference.FlowTraceLength, 0.0f) : 0.0f;
		Out.FlowSteps = FMath::Max(Reference.FlowSteps, 1);
		return true;
	}

	if (LayerChild.Type == EMixtormatLayerChildType::UvFromIds)
	{
		// Changes the layer's own source read, so a disabled layer must not gather it --
		// the same reason effects and the ramp tilt are skipped above.
		const FMixtormatUvIdFilter& Uv = LayerChild.UvId;
		if (!Layer.bEnabled || !Uv.bEnabled)
		{
			return true;
		}
		FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
		ChildData.Type = EMixtormatLayerChildType::UvFromIds;
		ChildData.SourceChildIndex = SourceChildIndex;
		FUvIdRenderData& UvData = ChildData.UvId;
		UvData.bOrthogonal = Uv.bOrthogonal;
		UvData.RotationMin = FMath::IsFinite(Uv.RotationMin)
			? Uv.RotationMin : 0.0f;
		UvData.RotationMax = FMath::IsFinite(Uv.RotationMax)
			? Uv.RotationMax : 360.0f;
		UvData.ScaleMin = FMath::IsFinite(Uv.ScaleMin)
			? FMath::Max(Uv.ScaleMin, 0.05f) : 1.0f;
		UvData.ScaleMax = FMath::IsFinite(Uv.ScaleMax)
			? FMath::Max(Uv.ScaleMax, 0.05f) : 1.0f;
		UvData.OffsetU = FMath::IsFinite(Uv.OffsetU)
			? Uv.OffsetU : 0.0f;
		UvData.OffsetV = FMath::IsFinite(Uv.OffsetV)
			? Uv.OffsetV : 0.0f;
		UvData.bRandomFlipU = Uv.bRandomFlipU;
		UvData.bRandomFlipV = Uv.bRandomFlipV;
		UvData.Seed = static_cast<uint32>(Uv.Seed);
		return true;
	}

	if (LayerChild.Type == EMixtormatLayerChildType::BoundaryFromIds)
	{
		const FMixtormatBoundaryIdFilter& Boundary = LayerChild.BoundaryId;
		if (!Layer.bEnabled || !Boundary.bEnabled) { return true; }
		const FMixtormatOutputReference& Reference = Boundary.RegionIdsSource;
		// A partial or invalid authored address is not an implicit-source request.
		const bool bExplicit = Reference.SourceLayerId.IsValid()
			|| Reference.SourceChildId.IsValid() || !Reference.OutputName.IsNone();
		int32 SourceIndex = INDEX_NONE;
		if (bExplicit)
		{
			if (Reference.Kind != EMixtormatPublishedFieldKind::RegionIds) { return true; }
			SourceIndex = ResolveBoundarySource(EffectiveLayers, LayerIndex,
				SourceChildIndex, LayerChild, Reference);
			if (SourceIndex == INDEX_NONE) { return true; }
		}
		FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
		ChildData.Type = EMixtormatLayerChildType::BoundaryFromIds;
		ChildData.SourceChildIndex = SourceChildIndex;
		FBoundaryIdRenderData& Out = ChildData.BoundaryId;
		Out.bExplicitSource = bExplicit;
		Out.RegionIdsSource.Source = {Reference.SourceLayerId, SourceIndex, Reference.OutputName};
		Out.RegionIdsSource.Kind = Reference.Kind;
		const auto Finite = [](float Value, float Default)
		{
			return FMath::IsFinite(Value) ? Value : Default;
		};
		Out.WidthPixels = FMath::Max(Finite(Boundary.WidthPixels, 4.0f), 0.0f);
		Out.Softness = FMath::Clamp(Finite(Boundary.Softness, 0.5f), 0.0f, 1.0f);
		Out.GapWidthPixels = FMath::Max(Finite(Boundary.GapWidthPixels, 8.0f), 0.0f);
		Out.GapSoftness = FMath::Clamp(Finite(Boundary.GapSoftness, 0.5f), 0.0f, 1.0f);
		Out.GapBiasPixels = Finite(Boundary.GapBiasPixels, 0.0f);
		Out.DistanceRangePixels = FMath::Max(Finite(Boundary.DistanceRangePixels, 64.0f), 0.25f);
		Out.bInvertDistance = Boundary.bInvertDistance;
		return true;
	}

	if (LayerChild.Type == EMixtormatLayerChildType::ReliefFromIds)
	{
		// Its pass runs after the composite, so a disabled layer must not gather it.
		const FMixtormatReliefIdFilter& Relief = LayerChild.ReliefId;
		if (!Layer.bEnabled || !Relief.bEnabled)
		{
			return true;
		}
		FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
		ChildData.Type = EMixtormatLayerChildType::ReliefFromIds;
		ChildData.SourceChildIndex = SourceChildIndex;
		FReliefIdRenderData& ReliefData = ChildData.ReliefId;
		// Finite-guarded rather than range-clamped wherever Pattern's equivalent is, and
		// bounded wherever Pattern's is bounded: these feed the same shader, so a value
		// that is safe there is safe here and one that is not is not.
		ReliefData.HeightAmount = FMath::IsFinite(Relief.HeightAmount)
			? Relief.HeightAmount : 0.0f;
		ReliefData.HeightRandom = FMath::IsFinite(Relief.HeightRandom)
			? Relief.HeightRandom : 1.0f;
		ReliefData.Profile = FMath::IsFinite(Relief.Profile)
			? Relief.Profile : 0.0f;
		ReliefData.ProfileRandom = FMath::IsFinite(Relief.ProfileRandom)
			? Relief.ProfileRandom : 0.0f;
		ReliefData.Feather = FMath::IsFinite(Relief.Feather)
			? Relief.Feather : 0.1f;
		ReliefData.FeatherRandom = FMath::IsFinite(Relief.FeatherRandom)
			? Relief.FeatherRandom : 0.0f;
		ReliefData.FeatherGain = FMath::IsFinite(Relief.FeatherGain)
			? Relief.FeatherGain : 0.0f;
		ReliefData.BevelHeight = FMath::IsFinite(Relief.BevelHeight)
			? Relief.BevelHeight : 0.0f;
		ReliefData.BevelWidthPixels = FMath::IsFinite(Relief.BevelWidthPixels)
			? FMath::Max(Relief.BevelWidthPixels, 0.0001f) : 4.0f;
		ReliefData.BevelWidthCells = FMath::IsFinite(Relief.BevelWidthCells)
			? FMath::Max(Relief.BevelWidthCells, 0.0001f) : 0.25f;
		ReliefData.bRelativeWidth = Relief.bRelativeWidth;
		ReliefData.BevelVariation = FMath::IsFinite(Relief.BevelVariation)
			? Relief.BevelVariation : 0.0f;
		ReliefData.BevelInsetPixels = FMath::IsFinite(Relief.BevelInsetPixels)
			? Relief.BevelInsetPixels : 0.0f;
		ReliefData.GapHeight = FMath::IsFinite(Relief.GapHeight)
			? Relief.GapHeight : 0.0f;
		ReliefData.EdgeRoughness = FMath::IsFinite(Relief.EdgeRoughness)
			? Relief.EdgeRoughness : 0.65f;
		ReliefData.EdgeRoughnessAmount = FMath::IsFinite(Relief.EdgeRoughnessAmount)
			? Relief.EdgeRoughnessAmount : 0.0f;
		ReliefData.Seed = static_cast<uint32>(Relief.Seed);
		return true;
	}

	if (LayerChild.Type == EMixtormatLayerChildType::RandomId)
	{
		const FMixtormatRandomIdMask& RandomId = LayerChild.RandomId;
		if (!RandomId.bEnabled)
		{
			return true;
		}
		FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
		ChildData.Type = EMixtormatLayerChildType::RandomId;
		ChildData.SourceChildIndex = SourceChildIndex;
		FRandomIdRenderData& RandomData = ChildData.RandomId;
		RandomData.MinValue = RandomId.MinValue;
		RandomData.MaxValue = RandomId.MaxValue;
		RandomData.Seed = static_cast<uint32>(RandomId.Seed);
		RandomData.BlendMode = RandomId.BlendMode;
		RandomData.Weight = RandomId.Weight;
		RandomData.bInvert = RandomId.Shaping.bInvert;
		RandomData.Balance = RandomId.Shaping.Balance;
		RandomData.Contrast = RandomId.Shaping.Contrast;
		RandomData.Offset = RandomId.Shaping.Offset;
		return true;
	}

	if (LayerChild.Type == EMixtormatLayerChildType::IdGroup)
	{
		const FMixtormatIdGroup& Group = LayerChild.IdGroup;
		if (!Layer.bEnabled || !Group.bEnabled)
		{
			return true;
		}
		FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
		ChildData.Type = EMixtormatLayerChildType::IdGroup;
		ChildData.SourceChildIndex = SourceChildIndex;
		ChildData.IdGroup.Mode = Group.Mode;
		return true;
	}

	if (LayerChild.Type == EMixtormatLayerChildType::CombineId)
	{
		const FMixtormatCombineIdFilter& Combine = LayerChild.CombineId;
		if (!Layer.bEnabled || !Combine.bEnabled)
		{
			return true;
		}

		FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
		ChildData.Type = EMixtormatLayerChildType::CombineId;
		ChildData.SourceChildIndex = SourceChildIndex;
		ChildData.CombineId.Amount = FMath::IsFinite(Combine.Amount)
			? Combine.Amount : 0.0f;
		ChildData.CombineId.Seed = static_cast<uint32>(Combine.Seed);
		ChildData.CombineId.Passes = FMath::Max(Combine.Passes, 1);
		ChildData.CombineId.bSubtract =
			Combine.Mode == EMixtormatIdCombineMode::Subtract;
		return true;
	}

	if (LayerChild.Type == EMixtormatLayerChildType::PatternId)
	{
		const FMixtormatPatternFilter& Pattern = LayerChild.PatternId;
		if (!Layer.bEnabled || !Pattern.bEnabled)
		{
			return true;
		}

		FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
		ChildData.Type = EMixtormatLayerChildType::PatternId;
		ChildData.SourceChildIndex = SourceChildIndex;
		if (bCacheLayers)
		{
			MixtormatComposeHash::FHasher Hasher;
			Hasher.Struct(FMixtormatPatternFilter::StaticStruct(), &Pattern);
			ChildData.CacheKey = Hasher.Get() | 1ull;
		}
		FPatternIdRenderData& PatternData = ChildData.PatternId;

		PatternData.PatternMode = Pattern.PatternMode;
		PatternData.GridMode = Pattern.GridMode;
		PatternData.Rows = FMath::Max(Pattern.Rows, 1);
		PatternData.Columns = FMath::Max(Pattern.Columns, 1);
		PatternData.RowOffset = FMath::IsFinite(Pattern.RowOffset)
			? Pattern.RowOffset : 0.0f;
		PatternData.Jitter = FMath::IsFinite(Pattern.Jitter)
			? Pattern.Jitter : 0.0f;
		PatternData.Rounding = FMath::IsFinite(Pattern.Rounding)
			? Pattern.Rounding : 0.0f;
		PatternData.bRelativeEdgeWidth = Pattern.bRelativeEdgeWidth;
		PatternData.bSwapAxes = Pattern.bSwapAxes;
		PatternData.GapPixels = FMath::IsFinite(Pattern.GapPixels)
			? Pattern.GapPixels : 0.0f;
		// Both are fractions of the piece's own half-gap, so they are genuinely bounded
		// rather than merely dragged -- past 1 a piece would invade its neighbour.
		PatternData.GapRandom = FMath::IsFinite(Pattern.GapRandom)
			? Pattern.GapRandom : 0.0f;
		PatternData.GapSlide = FMath::IsFinite(Pattern.GapSlide)
			? Pattern.GapSlide : 0.0f;
		PatternData.Seed = static_cast<uint32>(Pattern.Seed);

		PatternData.bUVVariation = Pattern.bUVVariation;
		PatternData.bOrthogonalUV = Pattern.bOrthogonalUV;
		PatternData.UVRotationMin = FMath::IsFinite(Pattern.UVRotationMin)
			? Pattern.UVRotationMin : 0.0f;
		PatternData.UVRotationMax = FMath::IsFinite(Pattern.UVRotationMax)
			? Pattern.UVRotationMax : 360.0f;
		PatternData.UVScaleMin = FMath::IsFinite(Pattern.UVScaleMin)
			? FMath::Max(Pattern.UVScaleMin, 0.05f) : 1.0f;
		PatternData.UVScaleMax = FMath::IsFinite(Pattern.UVScaleMax)
			? FMath::Max(Pattern.UVScaleMax, 0.05f) : 1.0f;
		PatternData.UVOffset = FMath::IsFinite(Pattern.UVOffset)
			? Pattern.UVOffset : 0.0f;
		PatternData.bRandomFlipU = Pattern.bRandomFlipU;
		PatternData.bRandomFlipV = Pattern.bRandomFlipV;

		PatternData.HeightAmount = FMath::IsFinite(Pattern.HeightAmount)
			? Pattern.HeightAmount : 0.0f;
		PatternData.Feather = FMath::IsFinite(Pattern.Feather)
			? Pattern.Feather : 0.15f;
		// Finite-guarded, not range-clamped. The editor sliders bound the drag; a typed
		// value goes through, because these are artistic amounts and the shader is what
		// actually has to be safe -- BoundedDelta stops any height clipping the surface,
		// and every divisor below is floored in the shader. A non-finite value is the one
		// thing that cannot pass: a NaN here poisons the whole composited height.
		PatternData.BevelHeight = FMath::IsFinite(Pattern.BevelHeight)
			? Pattern.BevelHeight : 0.0f;
		PatternData.BevelWidthPixels = FMath::IsFinite(Pattern.BevelWidthPixels)
			? FMath::Max(Pattern.BevelWidthPixels, 0.0001f) : 4.0f;
		PatternData.BevelWidthCells = FMath::IsFinite(Pattern.BevelWidthCells)
			? FMath::Max(Pattern.BevelWidthCells, 0.0001f) : 0.25f;
		PatternData.BevelVariation = FMath::IsFinite(Pattern.BevelVariation)
			? Pattern.BevelVariation : 0.0f;
		PatternData.BevelRoundness = FMath::IsFinite(Pattern.Profile)
			? Pattern.Profile : 0.0f;
		PatternData.BevelRoundnessRandom = FMath::IsFinite(Pattern.ProfileRandom)
			? Pattern.ProfileRandom : 0.0f;
		PatternData.HeightRandom = FMath::IsFinite(Pattern.HeightRandom)
			? Pattern.HeightRandom : 1.0f;
		PatternData.FeatherRandom = FMath::IsFinite(Pattern.FeatherRandom)
			? Pattern.FeatherRandom : 0.0f;
		// Floored, not range-clamped: an artistic amount, like the Bevel block above. The
		// shader normalises the curve by its own peak, so a large value cannot clip.
		PatternData.FeatherGain = FMath::IsFinite(Pattern.FeatherGain)
			? Pattern.FeatherGain : 0.0f;
		PatternData.BevelInsetPixels = FMath::IsFinite(Pattern.BevelInsetPixels)
			? Pattern.BevelInsetPixels : 0.0f;
		PatternData.GapHeight = FMath::IsFinite(Pattern.GapHeight)
			? Pattern.GapHeight : 0.0f;
		PatternData.EdgeRoughness = FMath::IsFinite(Pattern.EdgeRoughness)
			? Pattern.EdgeRoughness : 0.65f;
		PatternData.EdgeRoughnessAmount = FMath::IsFinite(Pattern.EdgeRoughnessAmount)
			? Pattern.EdgeRoughnessAmount : 0.0f;

		// Fracture Plates. Size Variation and the four Edge controls are artistic
		// amounts, so they are finite-guarded and not range-clamped -- a typed value
		// above the slider's range goes through, exactly as the Bevel block above does.
		// Secondary Amount is a probability and the child counts are indices, so those
		// are genuinely bounded rather than merely dragged.
		PatternData.FractureSizeVariation = FMath::IsFinite(Pattern.FractureSizeVariation)
			? Pattern.FractureSizeVariation : 0.3f;
		PatternData.FractureSecondaryAmount =
			FMath::IsFinite(Pattern.FractureSecondaryAmount)
				? Pattern.FractureSecondaryAmount : 0.5f;
		PatternData.FractureSecondaryMin =
			FMath::Max(Pattern.FractureSecondaryMin, 2);
		PatternData.FractureSecondaryMax = Pattern.FractureSecondaryMax;
		PatternData.FractureSecondaryRadius =
			FMath::IsFinite(Pattern.FractureSecondaryRadius)
				? Pattern.FractureSecondaryRadius : 0.34f;
		PatternData.FractureSecondaryJitter =
			FMath::IsFinite(Pattern.FractureSecondaryJitter)
				? Pattern.FractureSecondaryJitter : 0.55f;
		PatternData.FractureEdgeIrregularity =
			FMath::IsFinite(Pattern.FractureEdgeIrregularity)
				? Pattern.FractureEdgeIrregularity : 8.0f;
		// Floored, not clamped: the scale is a divisor that sizes an integer lattice
		// period, and a period of zero has no meaning to floor it into.
		PatternData.FractureEdgeScale = FMath::IsFinite(Pattern.FractureEdgeScale)
			? FMath::Max(Pattern.FractureEdgeScale, 1.0f) : 96.0f;
		PatternData.FractureEdgeDetail = FMath::IsFinite(Pattern.FractureEdgeDetail)
			? Pattern.FractureEdgeDetail : 2.5f;
		PatternData.FractureEdgeDetailScale =
			FMath::IsFinite(Pattern.FractureEdgeDetailScale)
				? FMath::Max(Pattern.FractureEdgeDetailScale, 1.0f) : 24.0f;
		return true;
	}

	if (LayerChild.Type == EMixtormatLayerChildType::Filter)
	{
		const FMixtormatClusterFilter& Filter = LayerChild.Filter;
		const bool bSurfaceInputAvailable = Filter.bSurfaceIds
			&& ((Filter.Source == EMixtormatClusterSource::CompositeBelow && LayerIndex > 0)
				|| Data.SourceOutputs.IsValid());
		if (!Layer.bEnabled || !Filter.bEnabled
			|| (!bSurfaceInputAvailable && !Filter.CanSampleSurface(Surface)))
		{
			return true;
		}
		FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
		ChildData.Type = EMixtormatLayerChildType::Filter;
		ChildData.SourceChildIndex = SourceChildIndex;
		if (bCacheLayers)
		{
			MixtormatComposeHash::FHasher Hasher;
			Hasher.Struct(FMixtormatClusterFilter::StaticStruct(), &Filter);
			ChildData.CacheKey = Hasher.Get() | 1ull;
		}
		ChildData.Filter.Source = Filter.Source;
		ChildData.Filter.bSurfaceIds = Filter.bSurfaceIds;
		ChildData.Filter.bSplitIslands = Filter.bSplitIslands;
		ChildData.Filter.PrimaryFeature = static_cast<EMixtormatSurfaceIdFeature>(
			FMath::Clamp(static_cast<int32>(Filter.PrimaryFeature), 0, 8));
		ChildData.Filter.SecondaryFeature = static_cast<EMixtormatSurfaceIdFeature>(
			FMath::Clamp(static_cast<int32>(Filter.SecondaryFeature), 0, 8));
		ChildData.Filter.FeatureMix = FMath::IsFinite(Filter.FeatureMix)
			? Filter.FeatureMix : 0.0f;
		ChildData.Filter.FormScale = FMath::Max(Filter.FormScale, 1);
		ChildData.Filter.GuideBlur = FMath::Max(Filter.GuideBlur, 0); // Kernel weight sum is a divisor.
		ChildData.Filter.MaxIds = FMath::Clamp(Filter.MaxIds, 2, 256);
		ChildData.Filter.EdgeClose = FMath::Max(Filter.EdgeClose, 0);
		ChildData.Filter.Threshold = FMath::IsFinite(Filter.Threshold)
			? Filter.Threshold : 0.33f;
		ChildData.Filter.Offset = FMath::IsFinite(Filter.Offset) ? Filter.Offset : 0.0f;
		ChildData.Filter.HeightInfluence = FMath::IsFinite(Filter.HeightInfluence)
			? Filter.HeightInfluence : 1.0f;
		return true;
	}

	if (LayerChild.Type == EMixtormatLayerChildType::ColorId)
	{
		const FMixtormatColorIdMask& ColorIdMask = LayerChild.ColorId;
		if (!ColorIdMask.bEnabled)
		{
			return true;
		}

		const bool bExactId = ColorIdMask.Mode == EMixtormatColorIdMode::ExactId;
		UTexture2D* IdTexture = ColorIdMask.IdTexture.LoadSynchronous();

		// A node with no map or no colours selects nothing, and selecting nothing is not
		// the same as being the identity: it would blend a mask of zero. Dropping it
		// entirely is what an unconfigured node should do, and matches how a painted mask
		// with no texture behaves.
		//
		// Exact ID reads neither: it compares the Region IDs published above it, so an
		// unset map and an empty colour list are its normal state. Whether it has a
		// producer above it to read is decided in the pass, where the child list has
		// already been walked -- the same place Random From IDs decides it.
		if (!bExactId && (!IdTexture || ColorIdMask.Colors.IsEmpty()))
		{
			return true;
		}

		FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
		ChildData.Type = EMixtormatLayerChildType::ColorId;
		ChildData.SourceChildIndex = SourceChildIndex;
		FColorIdRenderData& IdData = ChildData.ColorId;
		IdData.Mode = ColorIdMask.Mode;
		IdData.ExactRegionId = static_cast<uint32>(ColorIdMask.ExactRegionId);
		// White in Exact ID, where the slot is never sampled. The shader parameter still
		// has to be bound, and the cached white texture is already resident.
		IdData.IdTexture = GetTextureRHI(IdTexture ? IdTexture : WhiteTexture);
		if (!IdData.IdTexture.IsValid())
		{
			return false;
		}

		// Truncated rather than reported: the array is what the shader can hold, and the
		// inspector does not offer to add past it, so this only trips on data authored
		// through Blueprint or a hand-edited asset.
		const int32 ColorCount =
			FMath::Min(ColorIdMask.Colors.Num(), FMixtormatColorIdMask::MaxColors);
		for (int32 ColorIndex = 0; ColorIndex < ColorCount; ++ColorIndex)
		{
			const FLinearColor& Color = ColorIdMask.Colors[ColorIndex];
			IdData.Colors.Add(FVector4f(Color.R, Color.G, Color.B, 1.0f));
		}

		IdData.Tolerance = ColorIdMask.Tolerance;
		IdData.Softness = ColorIdMask.Softness;
		IdData.BlendMode = ColorIdMask.BlendMode;
		IdData.Weight = ColorIdMask.Weight;
		IdData.bInvert = ColorIdMask.Shaping.bInvert;
		IdData.Tiling = FVector2f(
			static_cast<float>(FMath::Max(ColorIdMask.TilingX, 1)),
			static_cast<float>(FMath::Max(ColorIdMask.TilingY, 1)));
		IdData.UVOffset = FVector2f(ColorIdMask.UVOffsetX, ColorIdMask.UVOffsetY);
		IdData.bFlipU = ColorIdMask.bFlipU;
		IdData.bFlipV = ColorIdMask.bFlipV;
		IdData.Rotation = static_cast<int32>(ColorIdMask.Rotation);
		IdData.Balance = ColorIdMask.Shaping.Balance;
		IdData.Contrast = ColorIdMask.Shaping.Contrast;
		IdData.Offset = ColorIdMask.Shaping.Offset;
		Data.bHasMask = true;
		return true;
	}
	return true;
}
}
