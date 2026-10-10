// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "Engine/TextureRenderTarget2D.h"
#include "MixtormatEffect.h"
#include "MixtormatGpuCompositor.h"
#include "MixtormatMask.h"
#include "MixtormatMaskShaping.h"
#include "MixtormatLayerTypes.h"
#include "MixtormatParameterDefinition.h"
#include "RendererInterface.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIResources.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "Templates/Function.h"

#include <atomic>

class UTexture2D;
struct FMixtormatPrefixCache;
struct FMixtormatNodeCache;
struct FMixtormatNodeCacheEntry;

DECLARE_LOG_CATEGORY_EXTERN(LogMixtormatComposition, Log, All);

// Render targets and UObject pins owned by one submitted composition. The pipeline translation
// unit needs the complete type so it can resolve referenced outputs and register final targets;
// destruction remains implemented beside the compositor's game-thread lifetime code.
struct FMixtormatComposeResources
{
	TArray<TStrongObjectPtr<UTextureRenderTarget2D>> Pins;
	FTextureRenderTargetResource* BaseColor[2] = {};
	FTextureRenderTargetResource* Normal[2] = {};
	FTextureRenderTargetResource* RAM[2] = {};
	FTextureRenderTargetResource* Height[2] = {};
	FTextureRenderTargetResource* Debug[2] = {};
	FTextureRenderTargetResource* RegionIdPick[2] = {};
	int32 PublishedIndex = 0;
	// Only read/written on the render thread. A failed child must not publish stale pixels.
	bool bSucceeded = false;

	~FMixtormatComposeResources();
};

// Private shared surface between the compositor and its pass groups.
//
// Only what more than one translation unit genuinely needs: the render-data structs the game
// thread fills and the render thread reads, the two contexts the pass functions are handed, the
// handful of small helpers every group calls, and the declarations of the pass entry points the
// compositor dispatches. No shader classes and no pass bodies -- each FGlobalShader stays in the
// .cpp that dispatches it, so adding a parameter to one shader rebuilds one file.
// Generated networks kept between composites.
//
// Growing a propagated craquelure network is one full-resolution dispatch per pixel of reach --
// up to a thousand of them, each doing on the order of eighty texture loads per pixel -- and the
// panel recomposites the entire stack on every frame of a slider drag. Almost nothing a user
// touches while tuning changes the network: Width, Contrast, Balance, Offset, Weight, Invert,
// the blend mode and all four relief controls are applied to the finished distance field, not
// during growth. Regrowing it for those was the dominant cost of interacting with the tool.
//
// Keyed on exactly the parameters growth reads, so a hit is the same field the miss would have
// produced rather than an approximation of it. Everything downstream still runs every frame, so
// the controls that shape a crack stay live.
//
// Render thread only. Held by the compositor and handed to each render command as a shared
// reference, so a composite still in flight cannot outlive the cache it is reading, and the
// pooled targets are released on the render thread by the flush the destructor enqueues.
struct FMixtormatNetworkCache
{
	struct FEntry
	{
		uint64 Key = 0;
		FIntPoint Resolution = FIntPoint::ZeroValue;
		TRefCountPtr<IPooledRenderTarget> Distance;
		uint64 LastUsed = 0;
	};

	// Bounded by bytes rather than by entry count, because the cost of an entry is not a constant:
	// one is a full-resolution RGBA32F, so 16MB at 1K and 268MB at 4K. A fixed count that is
	// comfortable at preview resolution pins well over a gigabyte after a 4K export, which is the
	// one way this cache can cost more than the work it saves.
	//
	// 512MB holds two networks at 4K and thirty at 1K. The working set is one entry per
	// craquelure node in the stack, which is almost always one or two; anything past that is
	// headroom for a seed being scrubbed back and forth, and headroom is what should give way
	// first when the entries get large.
	static constexpr uint64 MaxBytes = 512ull * 1024ull * 1024ull;

	TArray<FEntry> Entries;
	uint64 Tick = 0;

	static uint64 EntryBytes(const FIntPoint InResolution)
	{
		// RGBA32F, matching CraqDistanceDesc. Four channels of four bytes; the field itself uses
		// two of them, and the format is chosen for the crack id, which is a hash up to 2^24 and
		// has to stay exact.
		return static_cast<uint64>(InResolution.X) * static_cast<uint64>(InResolution.Y) * 16ull;
	}

	uint64 TotalBytes() const
	{
		uint64 Total = 0;
		for (const FEntry& Entry : Entries)
		{
			Total += EntryBytes(Entry.Resolution);
		}
		return Total;
	}

	TRefCountPtr<IPooledRenderTarget> Find(const uint64 Key, const FIntPoint InResolution)
	{
		check(IsInRenderingThread());
		++Tick;
		for (FEntry& Entry : Entries)
		{
			if (Entry.Key == Key && Entry.Resolution == InResolution && Entry.Distance.IsValid())
			{
				Entry.LastUsed = Tick;
				return Entry.Distance;
			}
		}
		return nullptr;
	}

	void Store(
		const uint64 Key,
		const FIntPoint InResolution,
		const TRefCountPtr<IPooledRenderTarget>& Distance)
	{
		check(IsInRenderingThread());
		if (!Distance.IsValid())
		{
			return;
		}

		for (FEntry& Entry : Entries)
		{
			if (Entry.Key == Key && Entry.Resolution == InResolution)
			{
				Entry.Distance = Distance;
				Entry.LastUsed = Tick;
				return;
			}
		}

		// Evict least-recently-used until the newcomer fits. The loop is bounded by the array
		// emptying rather than by the budget, so a single entry larger than the cap is stored
		// alone rather than thrashing: a network that cannot be cached at all would mean paying
		// full growth on every frame at exactly the resolution where that hurts most.
		const uint64 Incoming = EntryBytes(InResolution);
		while (!Entries.IsEmpty() && TotalBytes() + Incoming > MaxBytes)
		{
			int32 OldestIndex = 0;
			for (int32 Index = 1; Index < Entries.Num(); ++Index)
			{
				if (Entries[Index].LastUsed < Entries[OldestIndex].LastUsed)
				{
					OldestIndex = Index;
				}
			}
			Entries.RemoveAtSwap(OldestIndex);
		}

		FEntry& Added = Entries.AddDefaulted_GetRef();
		Added.Key = Key;
		Added.Resolution = InResolution;
		Added.Distance = Distance;
		Added.LastUsed = Tick;
	}

	void Reset()
	{
		check(IsInRenderingThread());
		Entries.Reset();
	}
};
namespace MixtormatGpuCompositor
{
	// Sobel passes divide this by eight; eight therefore follows the authored height exactly.
	constexpr float HeightDerivedNormalStrength = 8.0f;

	// Gain for passes that derive their normal through MixtormatHeightNormal.ush, which already
	// carries the pixel-to-slope conversion. Neutral, because a structural effect's relief depth
	// belongs in the height it writes rather than in a private gain on its own derivative --
	// per-effect gains are how the conventions drifted three orders of magnitude apart.
	constexpr float ReliefNormalStrength = 1.0f;
	constexpr float BorderHeightDerivedNormalStrength = 1.0f;

	// Shared by every gather: resolves an authored texture's RHI reference, or null when the
	// asset has no streaming texture yet. Was a file-static in MixtormatGpuCompositor.cpp;
	// the gather file and the pass groups all need it.
	inline FTextureRHIRef GetTextureRHI(UTexture2D* Texture)
	{
		return Texture && Texture->GetResource()
			? Texture->GetResource()->TextureRHI
			: FTextureRHIRef();
	}

	struct FPublishedMaskKey
	{
		FGuid LayerId;
		int32 ChildIndex = INDEX_NONE;
		FName Output;
		// Registry owner discriminator. Both a material layer and a Sources shelf producer are
		// addressed by a GUID, so this makes the owner explicit rather than inferring it: a shelf
		// field can never be mistaken for, or collide with, a layer field.
		EMixtormatOutputReferenceOwnerKind OwnerKind = EMixtormatOutputReferenceOwnerKind::Layer;

		friend bool operator==(const FPublishedMaskKey& A, const FPublishedMaskKey& B)
		{
			return A.LayerId == B.LayerId && A.ChildIndex == B.ChildIndex
				&& A.Output == B.Output && A.OwnerKind == B.OwnerKind;
		}

		friend uint32 GetTypeHash(const FPublishedMaskKey& Key)
		{
			return HashCombine(
				HashCombine(
					HashCombine(GetTypeHash(Key.LayerId), GetTypeHash(Key.ChildIndex)),
					GetTypeHash(Key.Output)),
				GetTypeHash(static_cast<uint32>(Key.OwnerKind)));
		}
	};

	// Shares the existing stable layer/child/output address shape; kind belongs to the payload.
	using FPublishedFieldKey = FPublishedMaskKey;

	// Canonical storage format for a typed field's primary texture. Producers create their field in
	// this format and FPublishedField::IsComplete validates against it, so the two never drift.
	// Flow's auxiliary FlowSmooth (PF_FloatRGBA, same as its primary) and Validity (PF_R16F) are
	// checked separately in IsComplete. Returns PF_Unknown for a kind with no storage contract.
	inline EPixelFormat GetPublishedFieldFormat(const EMixtormatPublishedFieldKind Kind)
	{
		switch (Kind)
		{
		// Integer region identifiers.
		case EMixtormatPublishedFieldKind::RegionIds:    return PF_R32_UINT;
		// Absolute/transformed coordinates; full float so 4K texels resolve.
		case EMixtormatPublishedFieldKind::UVMap:        return PF_G32R32F;
		// Directional/transport field, with its smoothed and validity payloads alongside.
		case EMixtormatPublishedFieldKind::Flow:         return PF_FloatRGBA;
		// RGB/RGBA field.
		case EMixtormatPublishedFieldKind::Color:        return PF_FloatRGBA;
		// Nominal 0..1 scalar; half precision matches the existing 0..1 mask storage.
		case EMixtormatPublishedFieldKind::Scalar01:     return PF_R16F;
		// Signed scalar: full float to preserve negatives beyond half range.
		case EMixtormatPublishedFieldKind::ScalarSigned: return PF_R32_FLOAT;
		// Signed distance: full float, matching existing Rock/Pebble edge distance fields.
		case EMixtormatPublishedFieldKind::SDF:          return PF_R32_FLOAT;
		// Generic 2-component vector; half precision, matching existing 2-channel fields.
		case EMixtormatPublishedFieldKind::Vector2:      return PF_G16R16F;
		default:                                         return PF_Unknown;
		}
	}

	struct FPublishedField
	{
		EMixtormatPublishedFieldKind Kind = EMixtormatPublishedFieldKind::RegionIds;
		FRDGTextureRef Texture = nullptr;
		FRDGTextureRef FlowSmooth = nullptr;
		FRDGTextureRef Validity = nullptr;
		bool bHashedIds = false;

		// Scalar01 and Vector2 producers may retain full precision as well as their default
		// half-precision storage. Flow also requires same-sized smoothing and validity payloads.
		bool IsComplete() const
		{
			if (!Texture) { return false; }
						const EPixelFormat Format = Texture->Desc.Format;
						const bool bFullPrecision = (Kind == EMixtormatPublishedFieldKind::Scalar01 && Format == PF_R32_FLOAT)
							|| (Kind == EMixtormatPublishedFieldKind::Vector2 && Format == PF_G32R32F);
						if (Format != GetPublishedFieldFormat(Kind) && !bFullPrecision) { return false; }
			if (Kind != EMixtormatPublishedFieldKind::Flow) { return true; }
			return FlowSmooth && Validity
				&& FlowSmooth->Desc.Format == PF_FloatRGBA && Validity->Desc.Format == PF_R16F
				&& FlowSmooth->Desc.Extent == Texture->Desc.Extent
				&& Validity->Desc.Extent == Texture->Desc.Extent;
		}
	};

	struct FOutputReferenceRenderData
	{
		FPublishedFieldKey Source;
		EMixtormatPublishedFieldKind Kind = EMixtormatPublishedFieldKind::RegionIds;
		float FlowAmount = 1.0f;
		float FlowTraceLength = 0.05f;
		int32 FlowSteps = 16;
	};

	struct FMaskRenderData : FMixtormatMaskShaping
	{
		FTextureRHIRef Texture;
		FGuid PublishedSourceLayerId;
		int32 PublishedSourceChildIndex = INDEX_NONE;
		FName PublishedSourceOutput;
		EMixtormatOutputReferenceOwnerKind PublishedSourceOwnerKind = EMixtormatOutputReferenceOwnerKind::Layer;
		FGuid PublishedSourceShelfId;
		EMixtormatMaskBlendMode BlendMode = EMixtormatMaskBlendMode::Replace;
		float Weight = 1.0f;
		FVector2f Tiling = FVector2f(1.0f, 1.0f);
		FVector2f UVOffset = FVector2f::ZeroVector;
		bool bFlipU = false;
		bool bFlipV = false;
		int32 Rotation = 0;
		// Reads the layer's own resolved values instead of an authored texture. Texture stays
		// unset in that case -- there is nothing to register.
		bool bLayerValues = false;
		bool bNoise = false;
		FMixtormatNoise Noise;
		int32 SourceChildIndex = INDEX_NONE;
		// EMixtormatLayerValueChannel by value for a Layer Values source, and 1 -- plain red --
		// for a texture, which is the channel this shader has always read.
		int32 SourceChannel = 1;
		// Summed from every enabled Blur child scoped to this mask. Zero on an axis skips that
		// dispatch. A node in the recipe, a number by the time it reaches here -- which is what
		// lets the blur be driven and instanced without the pass code knowing it exists.
		float BlurRadiusX = 0.0f;
		float BlurRadiusY = 0.0f;
		// Flattened from the Curvature children scoped to this mask, in chain order. Unlike the
		// blur radii these do not sum: two curvature filters are two separate narrowings, applied
		// one after the other, and collapsing them would change what they mean.
		TArray<FMixtormatMaskCurvature, TInlineAllocator<2>> CurvatureFilters;
	};

	struct FColorIdRenderData : FMixtormatMaskShaping
	{
		FTextureRHIRef IdTexture;
		EMixtormatColorIdMode Mode = EMixtormatColorIdMode::ColorRange;
		// Exact ID only, and already widened to what the shader compares against.
		uint32 ExactRegionId = 0;
		TArray<FVector4f, TInlineAllocator<FMixtormatColorIdMask::MaxColors>> Colors;
		float Tolerance = 0.10f;
		float Softness = 0.02f;
		EMixtormatMaskBlendMode BlendMode = EMixtormatMaskBlendMode::Replace;
		float Weight = 1.0f;
		FVector2f Tiling = FVector2f(1.0f, 1.0f);
		FVector2f UVOffset = FVector2f::ZeroVector;
		bool bFlipU = false;
		bool bFlipV = false;
		int32 Rotation = 0;
	};

	struct FEffectRenderData
	{
		EMixtormatEffectType Type = EMixtormatEffectType::Peeling;
		float Tiling = 1.0f;
		float Strength = 1.0f;
		float Front = 0.08f;
		float Width = 0.015f;
		float MacroWarp = 0.01f;
		float MicroWarp = 0.003f;
		float MicroMorph = 1.0f;
		float Thickness = 0.04f;
		float Lift = 0.04f;
		float DetailStrength = 0.02f;
		int32 StainMode = 0;
		FTextureRHIRef StainSourceMask;
		FTextureRHIRef StainDirtMask;
		float StainSourceMaskTiling = 1.0f;
		float StainDirtMaskTiling = 1.0f;
		bool bStainSourceMaskInvert = false;
		bool bStainDirtMaskInvert = false;
		int32 StainIterations = 20;
		uint32 StainSeed = 1;
		float StainSourceAmount = 0.12f;
		float StainGravity = 1.0f;
		float StainSurfaceFollow = 1.0f;
		float StainSpread = 0.12f;
		float StainAccumulation = 0.5f;
		float StainAbsorption = 0.35f;
		float StainDrying = 0.20f;
		float StainDirtAmount = 0.35f;
		float StainConcavityWeight = 0.35f;
		float StainConvexityWeight = 0.15f;
		float StainOcclusionWeight = 0.0f;
		float StainHeightWeight = 0.0f;
		float StainSourceHeightBias = 0.0f;
		float StainSlopeWeight = 0.0f;
		float StainSurfaceResponse = 1.0f;

		// Runoff. Angle in radians by the time it reaches here, radius already converted out of
		// texels into a fraction of the longer side -- see the mapping in MixtormatGpuCompositor.
		float RunoffGravityAngle = 0.0f;
		float RunoffStreakRadius = 0.3125f;
		float RunoffStreakSoftness = 0.46f;
		float RunoffSurfaceInfluence = 0.95f;
		float RunoffStrataAmount = 0.75f;
		float RunoffWarpScale = 18.0f;
		float RunoffWarpAmount = 1.5f;
		float RunoffLipStrength = 0.55f;
		float RunoffStrength = 0.25f;
		uint32 RunoffSeed = 1;
		int32 RunoffStrataCount = 4;
		// Procedural peeling.
		int32 PeelType = 0;
		int32 PeelMacroPeriod = 8;
		int32 PeelMicroPeriod = 32;
		uint32 PeelRandomSeed = 1;
		float PeelSeedThreshold = 0.62f;
		float PeelSeedNoiseWeight = 1.0f;
		float PeelSeedCurvatureWeight = 0.0f;
		float PeelSeedCurvatureBias = 1.0f;
		float PeelSeedAOWeight = 0.0f;
		float PeelSeedHeightWeight = 0.0f;
		float PeelSeedMaskWeight = 0.0f;
		bool bPeelNormalizeSeedWeights = true;
		int32 PeelCurvatureRadius = 2;
		float PeelGrowthStrength = 1.0f;
		float PeelEdgeSharpness = 1.0f;
		float PeelLiftVariation = 0.6f;
		float PeelCornerLift = 0.6f;
		float PeelCornerRadius = 1.0f;
		float PeelIDInfluence = 0.0f;
		float PeelSizeVariation = 0.5f;
		int32 PeelClusterPeriod = 4;
		int32 PeelSolveDivisor = 4;
		FTextureRHIRef PeelOwnMask;
		float PeelMaskTiling = 1.0f;
		bool bPeelMaskInvert = false;
		float PeelClusterAmount = 0.35f;
		int32 PeelWarpPeriod = 16;
		float PeelWarpAmount = 0.0f;
		float PeelWarpSource = 0.0f;
		float PeelHeightAmount = 1.0f;
		bool bPeelHeightInvert = false;

		float ErosionAmount = 1.5f;
		float ErosionDepth = 1.0f;
		float ErosionUnitDistance = 0.3f;
		FVector3f ErosionDirection = FVector3f(0.0f, -1.0f, 0.2f);
		int32 ErosionRadius = 1;
		int32 ErosionIterations = 8;
		float ErosionGravityForce = 0.6f;
		float ErosionSlopePower = 1.0f;
		float ErosionDeposit = 0.25f;
		float ErosionPreserveFlats = 0.002f;
		float ErosionSmoothing = 0.65f;
		float ErosionSecondaryAmount = 0.5f;
		float ErosionVariation = 0.18f;
		int32 ErosionSeed = 1;
		FTextureRHIRef ErosionPlacementMask;
		float ErosionMaskTiling = 1.0f;
		bool bErosionInvertMask = false;
		float ErosionRoughnessAmount = 0.0f;
		float ErosionCarveDepth = 0.05f;

		float GradeAmount = 1.0f;
		int32 GradeTonemap = 0;
		float GradeTonemapStrength = 1.0f;
		float GradeBrightness = 1.0f;
		float GradeContrast = 1.0f;
		float GradeContrastPivot = 0.18f;
		float GradeGamma = 1.0f;
		float GradeInputMin = 0.0f;
		float GradeInputMax = 1.0f;
		float GradeOutputMin = 0.0f;
		float GradeOutputMax = 1.0f;
		FVector3f GradeChannelBias = FVector3f::ZeroVector;

		// Breakup, already reduced to what the two dispatches need: the three cell counts and
		// the size range are derived from Scale/Detail/Size/Size Variation once here rather than
		// re-derived per pass.
		//
		// Defaults come from the canonical parameter definitions rather than literals, so the
		// table, the serialized struct initializers and this struct cannot drift apart. The
		// trailing literals are fallbacks for a missing key only (dev-ensured in DefaultFloat).
		float BreakupAmount = 1.0f;
		int32 BreakupMacroCells = 6;
		int32 BreakupMidCells = 11;
		int32 BreakupDetailCells = 20;
		float BreakupDensity = 0.72f;
		float BreakupSizeMin = 0.22f;
		float BreakupSizeMax = 0.42f;
		float BreakupStretch = 1.6f;
		float BreakupAngularity = 0.72f;
		float BreakupIrregularity = 0.38f;
		int32 BreakupMidOperation = 0;
		int32 BreakupDetailOperation = 0;
		float BreakupSmoothness = 0.30f;
		float BreakupInset = 0.0f;
		float BreakupDistortion = 5.6f;
		int32 BreakupDistortionFrequency = 3;
		bool bBreakupInvert = false;
		float BreakupRelief = -0.06f;
		float BreakupThicknessVariation = 0.30f;
		float BreakupGapWidth = 2.0f;
		float BreakupGapDepth = 0.02f;
		float BreakupGapVariation = 0.35f;
		float BreakupFold = 0.025f;
		float BreakupFoldWidth = 16.0f;
		float BreakupCrease = 0.018f;
		float BreakupCreaseWidth = 1.25f;
		float BreakupPush = 0.0f;
		float BreakupPushWidth = 24.0f;
		float BreakupPushRelief = 0.035f;
		float BreakupVariation = 0.25f;
		float BreakupRoughnessAmount = 0.0f;
		float BreakupNormalStrength = 2.0f;
		float BreakupNormalSharpness = 0.75f;
		FTextureRHIRef BreakupPlacementMask;
		float BreakupMaskTiling = 1.0f;
		bool bBreakupInvertMask = false;
		uint32 BreakupSeed = 1;

		int32 EdgeWearRadius = 24;
		float EdgeWearSlope = 0.35f;
		float EdgeWearStrength = 0.75f;
		float EdgeWearFeather = 1.0f;
		int32 EdgeWearDirections = 16;
		float EdgeWearAngularAA = 0.35f;
		float EdgeWearGravity = 0.0f;
		float EdgeWearGravityAngle = 90.0f;
		uint32 EdgeWearSeed = 1;
		int32 EdgeWearMacroScale = 12;
		float EdgeWearMacroAmount = 0.75f;
		int32 EdgeWearCellScale = 8;
		float EdgeWearCellAmount = 1.0f;
		int32 EdgeWearRidgeScale = 8;
		float EdgeWearRidgeAmount = 1.0f;
		int32 EdgeWearMicroScale = 40;
		float EdgeWearMicroAmount = 0.5f;
		int32 EdgeWearWarpScale = 32;
		float EdgeWearWarpAmount = 0.25f;
		float EdgeWearNoiseContrast = 0.5f;
		float EdgeWearIdVariation = 1.0f;
		float EdgeWearIdRadius = 0.5f;
		float EdgeWearIdSlope = 0.3f;
		float EdgeWearIdStrength = 0.25f;
		float EdgeWearIdNoise = 1.0f;
		float LayerBlurRadiusX = 0.0f;
		float LayerBlurRadiusY = 0.0f;
		uint32 LayerBlurScope = 0;
		float LayerBlurAmount = 1.0f;
		bool bLayerBlurHeight = true;
		float EdgeWearRoughnessWeight = 0.0f;
		float EdgeWearRoughnessOffset = 0.0f;

		float FlowWarpAmount = 1.0f;
		float FlowWarpWeight = 1.0f;
		int32 FlowWarpScale = 8;
		float FlowWarpDirection = 0.0f;
		uint32 FlowWarpSeed = 1;
		float FlowWarpMaskSlopeInfluence = 0.0f;
		float FlowWarpHeightSlopeInfluence = 0.0f;
		FVector2f FlowWarpDerivativeKernel = FVector2f(2.0f, 2.0f);
		uint32 FlowWarpBlendMode = 0;
		bool bGradeInvertMask = false;

		// Generator flow tools (Shape Deform / Generator Flow / Flow Carve). Sanitized and
		// clamped by GatherGeneratorFlow; distances are UV units, angles degrees.
		uint32 GeneratorFlowSource = 0;
		float GeneratorFlowAmount = 1.0f;
		float GeneratorFlowTangent = 0.0f;
		float GeneratorFlowAngle = 0.0f;
		float GravityFlowSurfaceFollow = 1.0f;
		float GravityFlowDeflection = 1.0f;
		float GeneratorFlowBend = 0.0f;
		uint32 GeneratorFlowSeed = 1;
		int32 GeneratorFlowRadius = 2;
		float GeneratorFlowSmooth = 8.0f;
		float GeneratorFlowReach = 0.1f;
		float GeneratorFlowFeather = 0.5f;
		float GeneratorFlowOffsetAlong = 0.0f;
		float GeneratorFlowOffsetAcross = 0.0f;
		float GeneratorFlowShapeOffset = 0.0f;
		float GeneratorFlowBulge = 0.0f;
		float GeneratorFlowTraceLength = 0.1f;
		int32 GeneratorFlowSteps = 16;
		float GeneratorFlowWarpStrength = 1.0f;
		uint32 GeneratorFlowCarveMode = 0;
		float GeneratorFlowDepth = 1.0f;
		float GeneratorFlowWidth = 0.01f;
		float GeneratorFlowFalloff = 1.0f;
	};

	struct FGeneratedMaskRenderData : FMixtormatMaskShaping
	{
		float CurvatureWeight = 0.0f;
		float CurvatureBias = 0.0f;
		float CurvatureStrength = 4.0f;
		float CurvaturePower = 1.0f;
		float DirectionWeight = 0.0f;
		float DirectionAngle = 90.0f;
		float DirectionBroadness = 1.0f;
		float AOWeight = 0.0f;
		float HeightWeight = 0.0f;
		float HeightBias = 0.0f;
		float RidgeWeight = 0.0f;
		bool bNormalizeWeights = true;
		int32 Broadness = 2;
		int32 Smoothing = 2;
		float Bias = 0.5f;
		float WarpAmount = 0.0f;
		float WarpSource = 0.0f;
		int32 WarpRadius = 1;
		EMixtormatMaskBlendMode BlendMode = EMixtormatMaskBlendMode::Multiply;
		float Weight = 1.0f;
	};

	// Craquelure. No surface inputs at all -- that is the whole reason it left the generated
	// mask, whose every signal is derived from the surface accumulated below it.
	struct FCraquelureRenderData : FMixtormatMaskShaping
	{
		bool bEnabled = true;
		EMixtormatCraquelureMode Mode = EMixtormatCraquelureMode::Propagated;

		float ReliefDepth = 0.04f;
		float ReliefWidth = 0.08f;
		float ReliefProfile = 1.0f;
		float ReliefGrooveVariation = 0.0f;
		float ReliefProfileVariation = 0.0f;
		float ReliefWidthVariation = 0.0f;

		// Hash of exactly the parameters the seed and growth passes read. Everything else about
		// this node is applied to the finished distance field, so it must not appear here or a
		// user tuning crack width would miss the cache on every frame of the drag.
		uint64 NetworkKey = 0;
		int32 Period = 16;
		float Jitter = 1.0f;
		float Width = 0.04f;
		float Variation = 0.0f;
		uint32 Seed = 1;
		float Warp = 0.0f;
		int32 WarpPeriod = 4;
		uint32 WarpSeed = 7;
		EMixtormatMaskBlendMode BlendMode = EMixtormatMaskBlendMode::Max;
		float Weight = 1.0f;

		// Propagated mode only.
		int32 Iterations = 48;
		int32 SeedCells = 4;
		float SeedChance = 0.35f;
		float SeedJitter = 0.85f;
		int32 NoiseCells = 5;
		float StressVariation = 0.35f;
		float ToughnessVariation = 0.45f;
		float Persistence = 1.65f;
		float FlowStrength = 0.18f;
		float StressGain = 0.75f;
		float ToughnessCost = 0.95f;
		float Irregularity = 0.32f;
		float GrowthThreshold = 0.55f;
		float TurnResponse = 0.72f;
		int32 CollisionLimit = 2;
	};

	struct FClusterFilterRenderData
	{
		bool bSurfaceIds = false;
		bool bSplitIslands = false;
		EMixtormatSurfaceIdFeature PrimaryFeature = EMixtormatSurfaceIdFeature::Height;
		EMixtormatSurfaceIdFeature SecondaryFeature = EMixtormatSurfaceIdFeature::Roughness;
		float FeatureMix = 0.0f;
		int32 FormScale = 4;
		int32 GuideBlur = 1;
		int32 MaxIds = 64;
		int32 EdgeClose = 1;
		EMixtormatClusterSource Source = EMixtormatClusterSource::LayerSurface;
		float Threshold = 0.33f;
		float Offset = 0.0f;
		float HeightInfluence = 1.0f;
	};

	struct FPatternIdRenderData
	{
		EMixtormatPatternMode PatternMode = EMixtormatPatternMode::Grid;
		EMixtormatGridMode GridMode = EMixtormatGridMode::Straight;
		int32 Rows = 8;
		int32 Columns = 8;
		float RowOffset = 0.0f;
		float Jitter = 0.0f;
		float Rounding = 0.0f;
		bool bRelativeEdgeWidth = false;
		bool bSwapAxes = false;
		float GapPixels = 0.0f;
		float GapRandom = 0.0f;
		float GapSlide = 0.0f;
		uint32 Seed = 1;

		bool bUVVariation = false;
		bool bOrthogonalUV = true;
		float UVRotationMin = 0.0f;
		float UVRotationMax = 360.0f;
		float UVScaleMin = 1.0f;
		float UVScaleMax = 1.0f;
		float UVOffset = 0.0f;
		bool bRandomFlipU = false;
		bool bRandomFlipV = false;

		float HeightAmount = 0.0f;
		float Feather = 0.15f;
		float BevelHeight = 0.0f;
		float BevelWidthPixels = 4.0f;
		float BevelWidthCells = 0.25f;
		float BevelVariation = 0.0f;
		float BevelRoundness = 0.0f;
		float BevelRoundnessRandom = 0.0f;
		float BevelInsetPixels = 0.0f;
		float GapHeight = 0.0f;
		float HeightRandom = 1.0f;
		float FeatherRandom = 0.0f;
		float FeatherGain = 0.0f;
		float EdgeRoughness = 0.65f;
		float EdgeRoughnessAmount = 0.0f;

		// Fracture Plates only. Inert for every other PatternMode.
		float FractureSizeVariation = 0.3f;
		float FractureSecondaryAmount = 0.5f;
		int32 FractureSecondaryMin = 2;
		int32 FractureSecondaryMax = 3;
		float FractureSecondaryRadius = 0.34f;
		float FractureSecondaryJitter = 0.55f;
		float FractureEdgeIrregularity = 8.0f;
		float FractureEdgeScale = 96.0f;
		float FractureEdgeDetail = 2.5f;
		float FractureEdgeDetailScale = 24.0f;
	};

	inline bool HasIntrinsicPatternOrientation(const FPatternIdRenderData& Pattern)
	{
		return Pattern.PatternMode == EMixtormatPatternMode::Herringbone
			|| Pattern.PatternMode == EMixtormatPatternMode::Basketweave;
	}

	// Reads a cluster filter's ID map and rewrites the layer's albedo. No blend mode and no
	// weight: this is applied at the composite's albedo sample, not in the mask chain.
	struct FHsvIdFilterRenderData
	{
		TArray<FVector4f, TInlineAllocator<FMixtormatHsvIdFilter::MaxPaletteColors>> Palette;
		float MixMin = 0.0f;
		float MixMax = 0.15f;
		float HueMin = 0.0f;
		float HueMax = 0.0f;
		float SatMin = 0.9f;
		float SatMax = 1.1f;
		float ValMin = 0.9f;
		float ValMax = 1.1f;
		uint32 Seed = 1;
	};

	struct FIdGroupRenderData
	{
		EMixtormatIdGroupMode Mode = EMixtormatIdGroupMode::Difference;
		int32 BoundaryWidth = 1;
	};

	struct FRandomIdRenderData : FMixtormatMaskShaping
	{
		float MinValue = 0.0f;
		float MaxValue = 1.0f;
		uint32 Seed = 1;
		EMixtormatMaskBlendMode BlendMode = EMixtormatMaskBlendMode::Replace;
		float Weight = 1.0f;
	};

	struct FRampIdRenderData
	{
		float HeightAmount = 0.05f;
		float IntensityRandom = 0.0f;
		EMixtormatMaskBlendMode BlendMode = EMixtormatMaskBlendMode::AddSub;
		bool bRotateRandom = true;
		bool bAngleStepping = false;
		float AngleStepDegrees = 5.0f;
		uint32 Seed = 1;
	};

	// UV From IDs. Everything the composite's source read needs to place a region's own copy of
	// the layer texture, and nothing about how the regions were produced -- the centre and the
	// frame come from MixtormatRegionFields.usf's bounds solve, not from the producer.
	struct FUvIdRenderData
	{
		bool bOrthogonal = true;
		float RotationMin = 0.0f;
		float RotationMax = 360.0f;
		float ScaleMin = 1.0f;
		float ScaleMax = 1.0f;
		float OffsetU = 0.0f;
		float OffsetV = 0.0f;
		bool bRandomFlipU = false;
		bool bRandomFlipV = false;
		uint32 Seed = 1;
	};

	// Relief From IDs. The same vocabulary FPatternIdRenderData's relief half carries, because the
	// pass underneath is the same one -- what differs is only where the edge field came from.
	struct FReliefIdRenderData
	{
		float HeightAmount = 0.0f;
		float HeightRandom = 1.0f;
		float Profile = 0.0f;
		float ProfileRandom = 0.0f;
		float Feather = 0.15f;
		float FeatherRandom = 0.0f;
		float FeatherGain = 0.0f;
		float BevelHeight = 0.0f;
		float BevelWidthPixels = 4.0f;
		float BevelWidthCells = 0.25f;
		bool bRelativeWidth = false;
		float BevelVariation = 0.0f;
		float BevelInsetPixels = 0.0f;
		float GapHeight = 0.0f;
		float EdgeRoughness = 0.65f;
		float EdgeRoughnessAmount = 0.0f;
		uint32 Seed = 1;
	};


	// Strata Carver settings: ordered interfaces, hardness, shelves and slab joints.
	struct FStrataCarverRenderData
	{
		uint32 Seed = 3;
		float Depth = 0.25f;
		float StrataFrequency = 6.0f;
		float StrataRotation = 0.0f;
		float ThicknessVariation = 0.5f;
		float HeightVariation = 0.5f;
		float Verticality = 0.7f;
		float LedgeWidth = 0.65f;
		float HardnessContrast = 0.75f;
		float SoftRecession = 0.65f;
		float Bend = 0.03f;
		int32 BendScale = 2;
		float Breakup = 0.35f;
		int32 JointScale = 4;
		float JointWidth = 0.035f;
		float HeightFollow = 0.0f;
		float Lamination = 0.25f;
		float CrossBedding = 1.0f;
		float MaskInfluence = 1.0f;
		float IDInfluence = 0.0f;
	};

	// One GENERATORS child. Mirrors FMixtormatGenerator: the kind is a field, so a second
	// generator adds a payload beside StrataCarver and a case in AddGeneratorLayerPasses.
	struct FCracksRenderData
	{
		int32 Seed = 1;
		int32 Cells = 7;
		float Jitter = 0.85f;
		float Width = 0.1f;
		float Depth = 0.415f;
		float Rough = 0.361f;
		float Scale = 7.8f;
		float Detail = 1.0f;
		float Feather = 0.176f;
		float WidthVariation = 0.5f;
		float WidthScale = 6.44f;
		float LineVariation = 0.541f;
		float RegionVariation = 0.615f;
		float Chip = 0.3f;
		float ChipSize = 0.12f;
		float Gap = 0.15f;
		float GapWidth = 1.5f;
		float Slip = 0.1f;
		float Tilt = 0.1f;
		float ChamferAmount = 0.0f;
		float ChamferEdge = 0.12f;
		// Hash of the field-shaping settings only (not chamfer), for the node cache.
		uint64 FieldKey = 0;
	};

	struct FRockFormationRenderData
	{
		float Style = 0.05f;
		int32 Cells = 4;
		int32 Rows = 12;
		uint32 Seed = 1;
		float Fracture = 1.0f;
		float Chamfer = 0.1f;
		float FractureHeightBias = 0.0f;
		float Gap = 1.0f;
		float ChamferRandom = 1.0f;
		float Spin = 0.0f;
		float SpinRandom = 0.0f;
		float TiltAngle = 0.1f;
		float TiltDirection = 0.75f;
		float TiltRandom = 0.2f;
		float SizeRandom = 0.0f;
		float Stretch = 0.5f;
		float StretchAngle = 0.5f;
		float StretchRandom = 0.0f;
		float HeightClusters = 0.5f;
		float Skew = 0.5f;
		float EdgeJag = 0.2f;
		float JagScale = 4.0f;
		float JagDetail = 0.2f;
		float ChamferJag = 1.0f;
		float RimChips = 0.1f;
		float RimChipSize = 0.075f;
		float FacetChips = 0.5f;
		int32 FacetIterations = 3;
		float FacetFalloff = 4.0f;
		float FacetRandom = 1.0f;
		float FacetAlign = 0.75f;
		float DepthMin = -1.0f;
		float DepthMax = 1.0f;
		// Hash of the field-shaping settings only, for the node cache.
		uint64 FieldKey = 0;
	};

	struct FPebblesRenderData
	{
		int32 Seed = 1;
		int32 Cells = 4;
		float Density = 1.0f;
		float Jitter = 0.7f;
		float Scale = 1.1f;
		float ScaleVariation = 0.5f;
		float Rotation = 180.0f;
		int32 Cuts = 10;
		int32 Direction = 0;
		float Irregularity = 0.5f;
		float Chamfer = 0.02f;
		float Steepness = 5.6f;
		float SteepnessVariation = 1.7f;
		float BiasVariation = 0.08f;
		float HeightGain = 1.0f;
		float HeightVariation = 0.3f;
		bool bFacetIds = false;
		// Hash of the field-shaping settings only, for the node cache.
		uint64 FieldKey = 0;
	};


	struct FCliffStrataRenderData
	{
		int32 CountX = 8, CountY = 7;
		float Density = 0.5f, SizeMin = 1.0f, SizeMax = 1.35f, SizeAspect = 0.95f;
		float Jitter = 0.1f, FlowVariation = 0.6f;
		float HeightMin = 0.025f, HeightMax = 0.1f;
		int32 Steps = 0;
		float Rotation = 0.35f, LeanX = 0.2f, LeanY = 0.0f;
		int32 FormationCells = 5;
		float FormationAmount = 1.0f;
		bool bQuarterCopies = true;
		int32 QuarterYCount = 8;
		float QuarterFill = 0.5f, QuarterSize = 0.9f, QuarterHeight = 2.75f;
		float QuarterJitterX = 0.5f, QuarterJitterY = 0.5f;
		int32 Sides = 4;
		bool bShapeRandom = false;
		float CameraYaw = 0.5f, CameraPitch = 0.25f, ViewScale = 2.50f;
		float DepthMin = -1.0f, DepthMax = 1.0f;
		float UnitDistance = 0.3f, UnitDistanceIdLerp = 0.5f;
		float CarveDepth = 0.375f, CarveVoronoi = 0.1f;
		float YBias = 0.3f, YBiasVoronoi = 0.9f;
		bool bYBiasVoronoiInvert = false;
		float NegativeYUnitDistanceTaper = 1.0f;
		bool bReverse = false;
		int32 Seed = 1234, VoronoiCells = 13;
		float FlowVoronoi = 0.0f;
		float ChamferWidth = 0.5f, ChamferIntensity = 1.0f, ChamferVoronoi = 0.05f;
		float BlockCavityWidth = 0.02f, RowCavityWidth = 0.25f, CavityIntensity = 0.25f;
		float CavityVoronoiThreshold = 0.5f, CavityVoronoiMaskGain = 0.125f;
		uint64 FieldKey = 0;
	};

	struct FGeneratorInputRenderData
	{
		bool bRequested = false;
		FOutputReferenceRenderData Reference;
	};

	// Resolved once at the target's authored position. Missing fields remain unavailable;
	// they never fall back to another generator or the layer-wide ReferencedUV.
	struct FGeneratorInputFields
	{
		FPublishedField Height;
		FPublishedField Warp;
		// Destination-space coordinates: traced from Flow, or the source UVMap directly.
		// Separate from the producer's field and the layer-wide source-sampling placement.
		FRDGTextureRef WarpUV = nullptr;
	};

	struct FGeneratorRenderData
	{
		FGeneratorInputRenderData HeightSource;
		FGeneratorInputRenderData WarpSource;
		EMixtormatGeneratorType Type = EMixtormatGeneratorType::StrataCarver;
		bool bNormalizeHeight = true;
		float HeightScale = 1.0f;
		float HeightBias = 0.0f;
		FStrataCarverRenderData StrataCarver;
		FCracksRenderData Cracks;
		FRockFormationRenderData RockFormation;
		FPebblesRenderData Pebbles;
		FCliffStrataRenderData CliffStrata;
	};

	// One driven scalar's Driver, flattened for the graph. Signal-source agnostic: it names a
	// layer whose combined mask is the signal and says nothing about what produced that mask, so a
	// published-output or region-ID source later fills the same slot without changing this.
	struct FScalarDriverRenderData
	{
		bool bEnabled = false;
		// A region source names the producer child as well as the layer; a mask source names the
		// layer alone. bRegionSource picks which of the slot's two bindings the shader reads.
		bool bRegionSource = false;
		FGuid SourceLayerId;
		int32 SourceChildIndex = INDEX_NONE;
		uint32 Seed = 0;
		float IdRandomMin = 0.0f;
		float IdRandomMax = 1.0f;
		bool bInvert = false;
		float InputMin = 0.0f;
		float InputMax = 1.0f;
		float OutputMin = 0.0f;
		float OutputMax = 1.0f;
		float Amount = 1.0f;
		uint32 Combine = 0;
	};

	// Ordered structural modules target a later same-layer Strata child explicitly.
	struct FGeneratorStructuralWarpRenderData
	{
		FOutputReferenceRenderData Source;
		int32 TargetChildIndex = INDEX_NONE;
		// Flow Amount and Trace Length, respectively. Unresolved signals leave the
		// authored scalars unchanged and never create a new Driver model.
		FScalarDriverRenderData Drivers[2];
	};

	struct FGeneratorHeightPushRenderData
	{
		FOutputReferenceRenderData Source;
		int32 TargetChildIndex = INDEX_NONE;
		float Amount = 1.0f;
	};

	struct FGeneratorHeightBlendRenderData
	{
		int32 Op = 0;
		int32 SourceChildIndex = INDEX_NONE;
		float Amount = 1.0f;
		float Scale = 1.0f;
		float Softness = 0.0f;
		float Threshold = 0.0f;
		float EdgeSoftness = 0.1f;
		float BaseBias = 0.0f;
		float BlendBias = 0.0f;
	};

	// Generator-layer Height Curve / Height Remap sublayer, carrying the prepared scalar-ramp GPU
	// payload and the signed remap controls.
	struct FGeneratorHeightCurveRenderData
	{
		float Amount = 1.0f;
		uint32 bNormalizeInput = 0;
		float InputMin = -1.0f;
		float InputMax = 1.0f;
		float Balance = 1.0f;
		float Contrast = 1.0f;
		float Offset = 0.0f;
		uint32 bInvert = 0;
		uint32 CurveCount = 0;
		uint32 CurveInterpolation = 0;
		TStaticArray<FVector4f, FMixtormatScalarRamp::MaxPoints> CurvePoints;
	};

	// Generator-layer Height Color Ramp sublayer, carrying the prepared colour-ramp GPU payload
	// and the resolved source: Source is an EMixtormatColorRampSource value; SourceChildIndex is
	// the referenced module resolved to this layer's child index (ModuleRef only). bHasGate is
	// compose-resolved: set when a Mask is scoped under the ramp, the Height Blend copy pattern.
	struct FGeneratorHeightColorRampRenderData
	{
		FName OutputName;
		int32 Source = 0;
		int32 SourceChildIndex = INDEX_NONE;
		uint32 bHasGate = 0;
		uint32 StopCount = 0;
		uint32 Interpolation = 0;
		TStaticArray<float, FMixtormatColorRamp::MaxStops> Positions;
		TStaticArray<FVector4f, FMixtormatColorRamp::MaxStops> Colors;
	};

	struct FBoundaryIdRenderData
	{
		bool bExplicitSource = false;
		FOutputReferenceRenderData RegionIdsSource;
		float WidthPixels = 4.0f;
		float Softness = 0.5f;
		float GapWidthPixels = 8.0f;
		float GapSoftness = 0.5f;
		float GapBiasPixels = 0.0f;
		float DistanceRangePixels = 64.0f;
		bool bInvertDistance = false;
	};

	struct FChildRenderData
	{
		EMixtormatLayerChildType Type = EMixtormatLayerChildType::Mask;
		int32 SourceChildIndex = INDEX_NONE;
		// Hash of this child's own settings for FMixtormatNodeCache; 0 when caching is off.
		uint64 CacheKey = 0;
		// Source index of the feature this child gates. INDEX_NONE keeps layer scope.
		int32 ScopeOwnerSourceChildIndex = INDEX_NONE;
		FMaskRenderData Mask;
		FEffectRenderData Effect;
		FGeneratedMaskRenderData Generated;
		FCraquelureRenderData Craquelure;
		FColorIdRenderData ColorId;
		FClusterFilterRenderData Filter;
		FPatternIdRenderData PatternId;
		FHsvIdFilterRenderData HsvFilter;
		FRandomIdRenderData RandomId;
		FRampIdRenderData RampId;

		FIdGroupRenderData IdGroup;
		FGeneratorRenderData Generator;
		FGeneratorHeightBlendRenderData HeightBlend;
		FGeneratorHeightCurveRenderData HeightCurve;
		FGeneratorHeightColorRampRenderData HeightColorRamp;
		FGeneratorHeightPushRenderData HeightPush;
		FGeneratorStructuralWarpRenderData StructuralWarp;
		FUvIdRenderData UvId;
		FReliefIdRenderData ReliefId;
		FBoundaryIdRenderData BoundaryId;
		FOutputReferenceRenderData OutputReference;
	};

	struct FGeneratorBundle
	{
		// Internal producer contract, not new public field kinds. Unknown vectors remain
		// unsupported; Noise declares heterogeneous source-frame semantics for transport-only sampling.
		enum class EFieldSemantic : uint8
		{
			Unsupported,
			ContinuousAttribute,
			RegionAttribute, // Same wrapped anchor as this bundle's immutable RegionIds.
			BedCoordinate, // Owner + local phase branch from immutable RegionIds.
			UvDistance, // Negative inside; own scalar metric, immutable BoundaryField validity.
			CoverageAlias, // Apply-owned Coverage, never an independent remap.
			LiftedCoordinateMap, // Identity winding; compose periodic displacement.
			SourceFrameVector, // Transport-sample only; no implicit covector/direction transform.
			LegacyLocalCentre, // Existing affine-local inverse estimate only.
			LegacyLocalOrientation // Existing inverse-Jacobian angle convention only.
		};
		enum class EFieldUnits : uint8
		{
			Unitless,
			BedFraction,
			CrackCell,
			MapUV,
			GeneratorDomain,
			Radians
		};
		// MapUV is the completed producer's source-map frame, not generator-domain units.
		// Region/bed attributes associate with this bundle's RegionIds; UV distances use
		// its BoundaryField validity. Remapping snapshots both associations before writes.
		struct FFieldDescriptor
		{
			EFieldSemantic Semantic = EFieldSemantic::Unsupported;
			EFieldUnits Units = EFieldUnits::Unitless;
			float InvalidDistance = 0.0f;
		};

		FRDGTextureRef Height = nullptr;
		// Pebbles' local support, transformed by flow and published as an explicit mask.
		// Never accumulated or used as final layer visibility.
		FRDGTextureRef Coverage = nullptr;
		FRDGTextureRef RegionIds = nullptr;
		// Legacy affine-local inverse estimates in map UV / radians, not exact per-ID
		// centres or lifted coordinate maps. Preserve their existing invalid handling.
		FRDGTextureRef CentreUV = nullptr;
		FRDGTextureRef Orientation = nullptr;
		FFieldDescriptor CentreDescriptor{EFieldSemantic::LegacyLocalCentre, EFieldUnits::MapUV};
		FFieldDescriptor OrientationDescriptor{EFieldSemantic::LegacyLocalOrientation, EFieldUnits::Radians};
		TMap<FName, FRDGTextureRef> NamedMasks;
		// Exhaustively registered at production; contains policies only, no second textures.
		TMap<FName, FFieldDescriptor> NamedMaskDescriptors;
		// Negative-inside local distance in map UV + validity. May be generated directly
		// by structurally warped Strata; only a later completed-field flow remaps it.
		FRDGTextureRef BoundaryField = nullptr;
		// Noise declares its heterogeneous Gradient as producer-domain data. The completed-warp
		// path transport-samples it rather than guessing a covector/direction transform.
		FFieldDescriptor GradientDescriptor;
		bool bHashedIds = false;

		void RegisterNamedMask(const FName Name, FRDGTextureRef Texture,
			const EFieldSemantic Semantic, const EFieldUnits Units = EFieldUnits::Unitless,
			const float InvalidDistance = 0.0f)
		{
			NamedMasks.Add(Name, Texture);
			NamedMaskDescriptors.Add(Name, FFieldDescriptor{Semantic, Units, InvalidDistance});
		}
	};

	struct FLayerRenderData
	{
		FGuid LayerId;
		// Explicit shelf ownership. Set only on the SourceProducers array: the entry is a Sources
		// shelf producer rather than a material layer -- it is never composited, snapshotted or
		// indexed as a layer. LayerId mirrors SourceShelfId purely as the published-field registry
		// address, so shelf identity is never conflated with a layer when the stack is walked.
		bool bIsShelfSource = false;
		FGuid SourceShelfId;
		bool bGenerator = false;
		bool bGeneratorAlbedo = false;

		// Hash of the layer without its children: everything a producer reading the layer's own
		// maps can see (surface, reference source, UV transform). 0 when caching is off.
		uint64 SourceCacheKey = 0;
		FScalarDriverRenderData ScalarDrivers[2];
		FTextureRHIRef BaseColor;
		FTextureRHIRef Normal;
		FTextureRHIRef RAM;
		FTextureRHIRef Height;
		TSharedPtr<FMixtormatComposeResources, ESPMode::ThreadSafe> SourceOutputs;
		bool bUseSourceF0 = false;
		FTextureRHIRef Mask;
		TArray<FChildRenderData> Children;
		FVector4f FillColor = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
		float Opacity = 1.0f;
		float Tiling = 1.0f;
		int32 UVScaleX = 1;
		int32 UVScaleY = 1;
		FVector2f UVOffset = FVector2f::ZeroVector;
		bool bFlipU = false;
		bool bFlipV = false;
		int32 Rotation = 0;
		float NormalIntensity = 1.0f;
		float HueShift = 0.0f;
		float Saturation = 1.0f;
		float Value = 1.0f;
		float RoughnessBias = 0.5f;
		float RoughnessContrast = 0.0f;
		float RoughnessOffset = 0.0f;
		float FillRoughness = 0.5f;
		float FillMetallic = 0.0f;
		float LayerF0 = 0.04f;
		EMixtormatColorBlendMode BaseColorBlendMode = EMixtormatColorBlendMode::Normal;
		float BaseColorBlendAmount = 1.0f;
		float BaseColorInfluence = 1.0f;
		float RoughnessInfluence = 1.0f;
		float AOInfluence = 1.0f;
		float MetallicInfluence = 1.0f;
		float F0Influence = 1.0f;
		float NormalInfluence = 1.0f;
		float HeightInfluence = 1.0f;
		float HeightBoost = 1.0f;
		float HeightLevelOffset = 0.0f;
		float HeightShape = 0.0f;
		float HeightSmooth = 0.0f;
		// The layer's Height Blend Strength, kept apart from HeightBlend because a Driver modulates
		// it in the composite.
		float HeightBlendAmount = 1.0f;
		FMixtormatHeightBlend HeightBlend;
		float HeightContrast = 1.0f;
		float ConstantHeight = 0.5f;
		float MaskHeightInfluence = 0.0f;
		float HeightContactAOAmount = 0.0f;
		float HeightContactAOWidth = 0.05f;
		float HeightBorderLift = 0.0f;
		float HeightBorderWidth = 0.05f;
		float HeightSmoothRadius = 0.0f;
		float HeightSmoothAmount = 1.0f;
		float HeightBorderSmoothing = 1.0f;
		float FeatureInfluence = 0.0f;
		float FeatureBias = 0.0f;
		float HeightFeatureInfluence = 0.0f;
		float AOFeatureInfluence = 0.0f;
		float CurvatureStrength = 1.0f;
		float CurvaturePower = 1.0f;
		int32 CurvatureSmoothing = 2;
		int32 CurvatureRadius = 1;
		bool bEnabled = true;
		bool bHasMask = false;
		bool bHasEffects = false;
		bool bOverrideBaseColor = false;
		bool bOverrideRoughness = false;
		bool bOverrideMetallic = false;
		bool bCoat = false;
		bool bFill = false;
		bool bHasSurface = false;
		bool bHasNormal = false;
		bool bNormalOnly = false;
		bool bOverrideNormal = false;
		bool bFlipNormalY = false;
		bool bHasPackedHeight = false;
		bool bInvertHeight = false;
		bool bDirectHeightComparison = false;
		bool bInvertHeightFeature = false;
		bool bInvertAOFeature = false;
		bool bInvertFeature = false;
		uint32 HeightSource = 2u;
		int32 HeightReferenceLayerIndex = INDEX_NONE;
	};

	// Builds the registry address a render layer publishes under. A material layer is tagged Layer;
	// a Sources shelf producer tags Shelf, so a shelf field is explicitly owned and can never be
	// read as, or matched against, a layer field. Every producer publish/lookup uses this.
	inline FPublishedMaskKey PublishedKey(const FLayerRenderData& Layer,
		const int32 ChildIndex, const FName Output)
	{
		FPublishedMaskKey Key;
		Key.LayerId = Layer.LayerId;
		Key.ChildIndex = ChildIndex;
		Key.Output = Output;
		Key.OwnerKind = Layer.bIsShelfSource
			? EMixtormatOutputReferenceOwnerKind::Shelf
			: EMixtormatOutputReferenceOwnerKind::Layer;
		return Key;
	}

	struct FRenderRequest
	{
		FIntPoint Resolution = FIntPoint::ZeroValue;
		TArray<FLayerRenderData> Layers;
		// Explicitly source-owned producers for the demanded Sources shelf entries, run ahead of the
		// stack in dependency order. They publish into the shared field registry keyed by the entry's
		// SourceId and are never composited, snapshotted or cached; the stack reads them by key like
		// any other published field.
		TArray<FLayerRenderData> SourceProducers;
		TSharedPtr<FMixtormatComposeResources, ESPMode::ThreadSafe> Targets;
		FTextureRHIRef OutputBC[2];
		FTextureRHIRef OutputN[2];
		FTextureRHIRef OutputRAM[2];
		FTextureRHIRef OutputHeight[2];
		FTextureRHIRef OutputDebug[2];
		// The raw Region IDs behind whatever the Region IDs preview is showing, one float per
		// pixel, so the editor can read an exact integer ID back off a click. Deliberately not
		// derived from OutputDebug: that one holds a hashed colour chosen to be legible, and
		// inverting it is impossible -- the hash is not injective and the buffer is half float.
		FTextureRHIRef OutputRegionIdPick[2];
		FMixtormatDebugPreviewSettings DebugSettings;
		FSimpleDelegate OnComplete;
		int32 PublishedTargetIndex = 0;
		bool bRotateOutput90 = false;

		// Shared rather than raw, so a composite still in flight holds the cache alive even if
		// the panel that owns it has gone.
		TSharedPtr<FMixtormatNetworkCache, ESPMode::ThreadSafe> NetworkCache;

		// Layer-prefix cache (see FMixtormatPrefixCache). PrefixHashes[i] covers layers 0..i and
		// every global input; empty disables both resume and save for this request.
		TSharedPtr<FMixtormatPrefixCache, ESPMode::ThreadSafe> PrefixCache;
		TSharedPtr<FMixtormatNodeCache, ESPMode::ThreadSafe> NodeCache;
		// Final-surface passes (see FMixtormatFinalSettings).
		float FinalAOAmount = 0.0f;
		float FinalAORadius = 8.0f;
		// Normal rebuilt from the final height. While set, every per-effect relief normal is
		// suppressed (height only) so the final pass is the one place slope comes from.
		bool bFinalNormalFromHeight = false;
		float FinalNormalStrength = 1.0f;
		bool bFinalAutoRemapHeight = false;
		TArray<uint64> PrefixHashes;
		// The layer whose finished state is worth keeping: the one just below the lowest layer
		// that changed since the previous composite. INDEX_NONE saves nothing.
		int32 SnapshotLayer = INDEX_NONE;
		// Resume and save are only allowed strictly below this layer. A debug view writes from
		// inside the loop, so the layers up to the one it inspects have to actually run.
		int32 CacheLayerLimit = MAX_int32;
		uint64 CacheBudgetBytes = 0;
		// Cleared on the render thread once the graph is submitted; see IsComposeInFlight.
		TSharedPtr<std::atomic<bool>, ESPMode::ThreadSafe> InFlight;
	};

	// True when Kind/OutputName/LayerIndex/ChildIndex together name the active generic
	// child-output preview target. LayerIndex/ChildIndex come from Request.DebugSettings, already
	// resolved from ChildTarget's GUIDs once at the top of RequestComposeInternal -- callers here
	// compare against them exactly the way every other debug mode already does.
	inline bool IsChildOutputPreviewTarget(
		const FRenderRequest& Request,
		const EMixtormatPreviewOutputKind Kind,
		const FName OutputName,
		const int32 LayerIndex,
		const int32 ChildIndex)
	{
		return Request.DebugSettings.Mode == EMixtormatDebugPreviewMode::ChildOutput
			&& Request.DebugSettings.LayerIndex == LayerIndex
			&& Request.DebugSettings.ChildIndex == ChildIndex
			&& Request.DebugSettings.ChildTarget.Kind == Kind
			&& Request.DebugSettings.ChildTarget.OutputName == OutputName;
	}

	inline FRDGTextureRef RegisterTexture(
		FRDGBuilder& GraphBuilder,
		TMap<FRHITexture*, FRDGTextureRef>& RegisteredTextures,
		const FTextureRHIRef& Texture,
		const TCHAR* Name)
	{
		FRHITexture* TextureRHI = Texture.GetReference();
		check(TextureRHI);
		if (const FRDGTextureRef* ExistingTexture = RegisteredTextures.Find(TextureRHI))
		{
			return *ExistingTexture;
		}

		FRDGTextureRef RegisteredTexture =
			GraphBuilder.RegisterExternalTexture(CreateRenderTarget(Texture, Name));
		RegisteredTextures.Add(TextureRHI, RegisteredTexture);
		return RegisteredTexture;
	}

	// Region producers run before the whole mask/effect chain regardless of row
	// order, because masks, composite colour and deferred relief can all read them.
	// Keyed by SourceChildIndex so a consumer finds the nearest producer above it.
	struct FPatternIdPassOutput
	{
		int32 SourceChildIndex = INDEX_NONE;
		FRDGTextureRef Ids = nullptr;
		FRDGTextureRef UV = nullptr;
		FRDGTextureRef Ramp = nullptr;
		FRDGTextureRef Edge = nullptr;
		FRDGTextureRef Gap = nullptr;
		FRDGTextureRef Orientation = nullptr;
		const FPatternIdRenderData* Settings = nullptr;
	};

	// One UV From IDs row, resolved before the composite because the composite's own source read
	// is what it changes. Keyed by SourceChildIndex so ordinary stack order decides which one a
	// consumer below sees.
	struct FUvIdPassOutput
	{
		int32 SourceChildIndex = INDEX_NONE;
		FRDGTextureRef Ids = nullptr;
		FRDGTextureRef CentreUV = nullptr;
		// Pattern's intrinsic orientation field (radians, PF_R32_FLOAT; Pattern writes only 0 and
		// pi/2), bound only when the producer this node resolved
		// to is a Herringbone/Basketweave Pattern in the same layer. Not an approximation for
		// other producers -- they simply do not have one, and this stays null rather than
		// inventing a basis. See FMixtormatUvIdFilter.
		FRDGTextureRef Orientation = nullptr;
		bool bIntrinsicOrientation = false;
		const FUvIdRenderData* Settings = nullptr;
	};

	// Generic region centres are a function of the Region ID producer, not of a UV node's
	// rotation/scale/offset controls. Cache them per producer so two UV From IDs rows reading
	// the same map do not allocate and reduce another full-resolution bounds field.
	struct FRegionCentreCacheEntry
	{
		int32 SourceChildIndex = INDEX_NONE;
		FRDGTextureRef CentreUV = nullptr;
	};

	// The ID-map-only half of the region analysis, kept so several consumers of one producer pay
	// for it once. Nothing here depends on the consumer's own controls: the jump flood and the
	// per-region reach are functions of the Region IDs alone, and only the final field resolve --
	// one dispatch -- reads a node's feather and width.
	enum class ERegionSeedPolicy : uint32
	{
		LegacyFour = 0,
		ValidOnlyEight = 1
	};

	struct FRegionDistanceRecord
	{
		FRDGTextureRef Source = nullptr;
		FIntPoint Resolution = FIntPoint::ZeroValue;
		ERegionSeedPolicy Policy = ERegionSeedPolicy::LegacyFour;
		FRDGTextureRef Record = nullptr;
	};

	struct FRegionDistanceCacheEntry
	{
		int32 SourceChildIndex = INDEX_NONE;
		FRDGTextureRef Source = nullptr;
		FRDGTextureRef RootIds = nullptr;
		FIntPoint Resolution = FIntPoint::ZeroValue;
		FRDGTextureRef Record = nullptr;
		FRDGTextureRef Extent = nullptr;
	};

	// Deferred filters retain the mask visible at their own row. Keeping the
	// texture beside the effect prevents later layer masks from changing scope.
	struct FPendingEffect
	{
		const FEffectRenderData* Effect = nullptr;
		FRDGTextureRef FeatureMask = nullptr;
		bool bHasScopedMask = false;
	};

	// Breakup carries an optional Region ID map: when a Pattern IDs producer sits above it in the
	// same layer its ids fold into the generated piece identity, giving per-brick variation on top
	// of Breakup's own sub-fragments. Null is the normal case and is not an error -- unlike Worn
	// Edges, Breakup generates its own ids and never needs an external producer.
	struct FPendingBreakup
	{
		const FEffectRenderData* Effect = nullptr;
		int32 SourceChildIndex = INDEX_NONE;
		FRDGTextureRef FeatureMask = nullptr;
		FRDGTextureRef Field = nullptr;
		FRDGTextureRef GeneratedRegionIds = nullptr;
		// Published scalar outputs, written beside the IDs so a consumer in the same layer can
		// read them: the grout between pieces, the boundary around and between them, and the
		// piece interiors.
		FRDGTextureRef Gap = nullptr;
		FRDGTextureRef Edge = nullptr;
		FRDGTextureRef Pieces = nullptr;
		bool bHasScopedMask = false;
	};

	struct FPendingWornEdges
	{
		const FEffectRenderData* Effect = nullptr;
		int32 SourceChildIndex = INDEX_NONE;
		FRDGTextureRef FeatureMask = nullptr;
		FRDGTextureRef RegionIds = nullptr;
		FRDGTextureRef PatternEdge = nullptr;
		bool bHasPatternEdge = false;
	};

	// Craquelure relief, deferred out of the child loop for the same reason
	// erosion and chipping are: the loop runs before the layer composites, so a
	// carve made here would be painted straight back over.
	//
	// The distance field is carried rather than looked up again later, because
	// the mask targets it was produced alongside are ping-ponged -- any later
	// mask child overwrites the slot, and relief would then read whichever child
	// happened to run last instead of its own network.
	struct FPendingCraquelureRelief
	{
		FRDGTextureRef Distance = nullptr;
		float HeightWeight = 0.0f;
		float WidthPixels = 0.0f;
		float Variation = 0.0f;
		float Profile = 1.0f;
		float GrooveVariation = 0.0f;
		float ProfileVariation = 0.0f;
		float WidthVariation = 0.0f;
		// Carried so the groove is read through the same displacement as the
		// mask. The two passes share one distance field; warp only one and the
		// height ends up beside the crack instead of under it.
		float Warp = 0.0f;
		int32 WarpPeriod = 4;
		uint32 WarpSeed = 7;
	};

	// Region relief and edge shading are deferred for exactly the reason
	// craquelure relief is: their fields exist before the composite, but the
	// surface they modify does not exist until after it.
	struct FPendingRampTilt
	{
		FRDGTextureRef Field = nullptr;
		FRDGTextureRef EdgeField = nullptr;
		FRDGTextureRef RegionIds = nullptr;
		float HeightAmount = 0.0f;
		float CellHeightAmount = 0.0f;
		float CellHeightRandom = 0.0f;
		uint32 BlendMode = 0;
		bool bUseEdge = false;
		float BevelHeight = 0.0f;
		float BevelWidthPixels = 4.0f;
		float BevelWidthCells = 0.25f;
		bool bBevelRelative = false;
		float BevelVariation = 0.0f;
		float BevelRoundness = 0.0f;
		float BevelRoundnessRandom = 0.0f;
		float BevelInsetPixels = 0.0f;
		float GapHeight = 0.0f;
		float FeatherGain = 0.0f;
		float EdgeRoughness = 0.65f;
		float EdgeRoughnessAmount = 0.0f;
	};

	// Graph-wide state for one composite.
	//
	// Constructed inside the render command rather than beside the request, so it can only ever
	// reference render-thread lifetimes -- a member bound to the game-thread stack the request
	// was gathered on would already be dangling by the time the first pass is added.
	//
	// Height is its own target here and stays that way. The source material convention is RAMH,
	// but this compositor has always carried height separately, and folding it into the packed
	// alpha would change what every pass reads and writes rather than only where the code lives.
	struct FMixtormatComposeContext
	{
		FRDGBuilder& GraphBuilder;
		const FRenderRequest& Request;

		TMap<FRHITexture*, FRDGTextureRef> RegisteredTextures;

		FRDGTextureRef OutputBC[2] = {};
		FRDGTextureRef OutputN[2] = {};
		FRDGTextureRef OutputRAM[2] = {};
		FRDGTextureRef OutputHeight[2] = {};
		FRDGTextureRef OutputDebug[2] = {};
		FRDGTextureRef OutputRegionIdPick[2] = {};

		FRDGTextureRef EmptyRegionIds = nullptr;
		FRDGTextureRef EmptyPatternUV = nullptr;
		FRDGTextureRef EmptyPatternOrientation = nullptr;
		FRDGTextureRef EmptyDriverSignal = nullptr;
		// RGBA, unlike the three above: the composite declares its debug slot as RWTexture2D<float4>.
		FRDGTextureRef EmptyDebugOutput = nullptr;

		TSet<FGuid> DriverSnapshotDemand;
		TMap<FGuid, FRDGTextureRef> DriverSnapshots;
		TMap<FPublishedMaskKey, FRDGTextureRef> PublishedMaskOutputs;
		TMap<FPublishedFieldKey, FPublishedField> PublishedFieldOutputs;
		// Coverage conversions and inline noise are graph-local; raw typed fields stay untouched.
		TMap<FPublishedMaskKey, FRDGTextureRef> NoiseMaskSources;
		TSet<FPublishedFieldKey> PublishedFieldDemand;
		// Graph-local identity cache; never retain RDG pointers across compose requests.
		TArray<FRegionDistanceRecord, TInlineAllocator<2>> RegionDistanceRecords;
		TSet<int32> RequiredHeightSnapshots;
		TMap<int32, FRDGTextureRef> HeightSnapshots;

		// Node-cache entries whose targets are extracted by this graph; stored after Execute.
		TArray<TSharedPtr<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe>> PendingNodeEntries;

		FMixtormatComposeContext(FRDGBuilder& InGraphBuilder, const FRenderRequest& InRequest)
			: GraphBuilder(InGraphBuilder)
			, Request(InRequest)
		{
		}
	};

	// The layer pipeline: the targets every layer ping-pongs on its own index, and the state of
	// the layer currently being built.
	//
	// Constructed only inside the branch where that pipeline exists -- a stack with at least one
	// layer in it -- rather than sitting on the graph-wide context as a row of fields that are
	// null whenever the stack is empty.
	struct FMixtormatLayerPassContext
	{
		FMixtormatComposeContext& Ctx;

		FRDGTextureDesc MaskDesc;
		FRDGTextureDesc EffectDesc;
		FRDGTextureDesc EffectHeightDesc;

		FRDGTextureRef MaskTargets[2] = {};
		FRDGTextureRef RidgeTargets[2] = {};
		// Per-pixel coverage of the ground, ping-ponged on the layer index like the height targets
		// it travels with. 0 is bare substrate; a layer writes max(below, its coverage).
		FRDGTextureRef OccupancyTargets[2] = {};
		FRDGTextureRef EffectTargets[2] = {};
		FRDGTextureRef EffectHeightTargets[2] = {};

		// Material channels resolved into layer/output UV space only when a top-level Flow Warp
		// needs an isolated input. The composite reads these instead of the authored textures.
		FRDGTextureRef LayerInputBC = nullptr;
		FRDGTextureRef LayerInputN = nullptr;
		FRDGTextureRef LayerInputRAM = nullptr;
		// True when the generator stack produced a real colour this layer may paint as albedo
		// (the last valid ordered Color Ramp result). Without it a GeneratorAlbedo layer must keep
		// the composite below rather than writing the resolved neutral input.
		bool bGeneratorColor = false;
		// Albedo in RGB, roughness in alpha, resolved once per layer by AddLayerValuesPass and
		// shared by every Layer Values mask on it.
		FRDGTextureRef LayerValues = nullptr;
		FRDGTextureRef LayerInputHeight = nullptr;
		FGeneratorBundle GeneratorBundle;
		// Last valid Flow/UV reference placement. Applied before the destination's own source UVs.
		FRDGTextureRef ReferencedUV = nullptr;
		// Last valid Color reference placement. Used as the destination layer's Base Color input
		// when no material surface is available, so Fill / Generator layers can still resolve a
		// referenced colour into their Base Color.
		FRDGTextureRef ReferencedColor = nullptr;

		// Set when a generator rewrote LayerInputHeight: the composite then reads it as this
		// layer's height even when the layer has no packed height of its own.
		bool bGeneratedHeight = false;
		// Per-layer generator field outputs, keyed by source child index, so the ID phase and the
		// height phase share one evaluation.
		TMap<int32, TArray<FRDGTextureRef, TInlineAllocator<7>>> GeneratorFields;

		// Each Generator module's signed height, keyed by source child index, so a later Height
		// Blend sublayer can read another module's result.
		TMap<int32, FRDGTextureRef> GeneratorModuleHeights;
		TMap<int32, FGeneratorInputFields> GeneratorInputs;
		TMap<int32, FRDGTextureRef> GeneratorHeightPushFields;
		// Destination-tile displacement, separate from the composed signed bedding shift.
		TMap<int32, FRDGTextureRef> GeneratorStructuralDisplacements;

		FRDGTextureRef PeelNoiseDummy = nullptr;
		FRDGTextureRef PeelFieldDummy = nullptr;

		// Per layer, and reset by BeginLayer before anything reads it. The pending entries hold
		// pointers into one layer's children and RDG refs produced by that layer's passes;
		// neither means anything in the iteration after the one that filled them.
		int32 LayerIndex = INDEX_NONE;
		TArray<TPair<int32, FRDGTextureRef>> RegionIdMaps;
		TArray<FPatternIdPassOutput, TInlineAllocator<2>> PatternOutputs;
		TArray<FUvIdPassOutput, TInlineAllocator<2>> UvIdOutputs;
		TArray<FRegionCentreCacheEntry, TInlineAllocator<2>> RegionCentreCache;
		TArray<FRegionDistanceCacheEntry, TInlineAllocator<2>> RegionDistanceCache;
		FRDGTextureRef CombinedMask = nullptr;
		FRDGTextureRef CombinedEffectData = nullptr;
		FRDGTextureRef CombinedEffectHeight = nullptr;
		FRDGTextureRef DebugMask = nullptr;
		int32 MaskPassIndex = 0;
		int32 EffectPassIndex = 0;
		FPendingEffect PendingErosion;
		TArray<FPendingBreakup, TInlineAllocator<2>> PendingBreakups;
		TArray<FPendingWornEdges, TInlineAllocator<2>> PendingWornEdges;
		TArray<FPendingCraquelureRelief, TInlineAllocator<2>> PendingCraquelureReliefs;
		TArray<FPendingRampTilt, TInlineAllocator<2>> PendingRampTilts;
		TArray<FPendingEffect, TInlineAllocator<2>> PendingGrades;
		// Last of the filters, so it softens the finished surface rather than one a later
		// filter was about to change.
		TArray<FPendingEffect, TInlineAllocator<2>> PendingLayerBlurs;

		explicit FMixtormatLayerPassContext(FMixtormatComposeContext& InCtx)
			: Ctx(InCtx)
		{
		}

		void BeginLayer(const int32 InLayerIndex)
		{
			LayerIndex = InLayerIndex;
			RegionIdMaps.Reset();
			PatternOutputs.Reset();
			UvIdOutputs.Reset();
			RegionCentreCache.Reset();
			RegionDistanceCache.Reset();
			CombinedMask = nullptr;
			CombinedEffectData = nullptr;
			CombinedEffectHeight = nullptr;
			DebugMask = nullptr;
			MaskPassIndex = 0;
			EffectPassIndex = 0;
			PendingErosion = FPendingEffect();
			PendingBreakups.Reset();
			PendingWornEdges.Reset();
			PendingCraquelureReliefs.Reset();
			PendingRampTilts.Reset();
			PendingGrades.Reset();
			LayerInputBC = nullptr;
			LayerInputN = nullptr;
			LayerInputRAM = nullptr;
			LayerValues = nullptr;
			LayerInputHeight = nullptr;
			GeneratorBundle = FGeneratorBundle();
			ReferencedUV = nullptr;
			ReferencedColor = nullptr;

			bGeneratedHeight = false;
			bGeneratorColor = false;
			GeneratorFields.Reset();
			GeneratorModuleHeights.Reset();
			GeneratorInputs.Reset();
			GeneratorHeightPushFields.Reset();
			GeneratorStructuralDisplacements.Reset();
			PendingLayerBlurs.Reset();
		}
	};

	// Shared compositor seams. Shader-bound implementations stay with their shader classes in
	// MixtormatGpuCompositor.cpp; the pipeline owns graph construction and layer orchestration.
	FLinearColor DebugClearColor();

	void AddLayerCompositePass(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		uint32 PreparedLayerMode);

	void AddRotateOutputPass(
		FMixtormatComposeContext& Ctx,
		int32 InputTargetIndex,
		int32 OutputTargetIndex);

	void EnqueueCompose(FRenderRequest&& Request);

	// Flow/UV imports run before producers. RegionIds also resolve locally as sources finish.
	void AddOutputReferencePasses(FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx, const FLayerRenderData& Layer);
	bool AddRegionIdReferencePass(FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx, const FLayerRenderData& Layer,
		const FChildRenderData& Child, bool bFinalizeUnavailable);
	void AddReadyRegionIdPasses(FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx, const FLayerRenderData& Layer,
		int32 BeforeChildIndex, bool bFinalizeUnavailable);
	void AddBoundaryIdPass(FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx, const FLayerRenderData& Layer,
		const FChildRenderData& Child);
	void PublishLayerRegionIdOutputs(FMixtormatComposeContext& Ctx,
		const FMixtormatLayerPassContext& LayerCtx, const FLayerRenderData& Layer);

	// Producers can publish in different phases; consumers still require source-child order.
	inline void PublishRegionIds(
		TArray<TPair<int32, FRDGTextureRef>>& RegionIdMaps,
		const int32 SourceChildIndex,
		FRDGTextureRef RegionIds)
	{
		int32 InsertIndex = 0;
		while (InsertIndex < RegionIdMaps.Num() && RegionIdMaps[InsertIndex].Key < SourceChildIndex)
		{
			++InsertIndex;
		}
		if (RegionIdMaps.IsValidIndex(InsertIndex) && RegionIdMaps[InsertIndex].Key == SourceChildIndex)
		{
			RegionIdMaps[InsertIndex].Value = RegionIds;
			return;
		}
		RegionIdMaps.Insert(TPair<int32, FRDGTextureRef>(SourceChildIndex, RegionIds), InsertIndex);
	}

	inline FRDGTextureRef FindRegionIdsAbove(
		const TArray<TPair<int32, FRDGTextureRef>>& RegionIdMaps,
		int32 ChildIndex)
	{
		FRDGTextureRef Found = nullptr;
		for (const TPair<int32, FRDGTextureRef>& Entry : RegionIdMaps)
		{
			if (Entry.Key < ChildIndex)
			{
				Found = Entry.Value;
			}
		}
		return Found;
	}


	// True when a generator has at least one enabled mask scoped beneath it. Asked before the
	// mask is resolved, because "no mask" and "a mask that is white" must stay distinguishable.
	// Declared here rather than duplicated: it lives in an anonymous namespace inside
	// MixtormatGpuGeneratorPasses.cpp, so the Noise passes could not otherwise reach it.
	bool HasScopedGeneratorMasks(const FLayerRenderData& Layer, const int32 OwnerSourceChildIndex);

	// MixtormatGpuMaskPasses.cpp -- mask processing, generated masks, mask shaping.
	FRDGTextureRef AddScopedFeatureMask(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const int32 OwnerSourceChildIndex,
		const bool bIndependentScope = false);

	void ApplyScopedMaskGate(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child);

	void AddGeneratedMaskPass(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		const int32 ChildIndex);

	void AddColorIdMaskPass(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		const int32 ChildIndex);

	void AddTextureMaskPass(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		const int32 ChildIndex);

	FRDGTextureRef AddBorderHeightBlurPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer);

	FRDGTextureRef AddHeightMaskBlurPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer);

	void AddLayerHeightSmoothPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer);

	// MixtormatGpuPatternPasses.cpp -- Pattern IDs, Cluster/Colour IDs, HSV/Random/Ramp-from-ID.
	void AddRandomIdMaskPass(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		const int32 ChildIndex);

	FRDGTextureRef AddSurfaceIdPasses(
		FMixtormatComposeContext& Ctx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		int32 LayerIndex,
		bool bWriteDebug);

	void AddRegionProducerPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer);

	// UV From IDs. Scheduled immediately after the producers and before anything reads the layer's
	// source, because the source read is the only thing it changes.
	void AddUvIdPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer);


	void CollectPendingRampTilts(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer);

	void AddRampReliefPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer);

	// MixtormatGpuEffectPasses.cpp -- Grade, Craquelure, Worn Edges, Breakup, Erosion,
	// owner-local Flow Warp and the shared height-derived normal.
	void AddHeightDerivedNormalPass(
		FMixtormatComposeContext& Ctx,
		FRDGTextureRef PreviousHeight,
		FRDGTextureRef CurrentHeight,
		FRDGTextureRef PreviousNormal,
		FRDGTextureRef PreviousRAM,
		FRDGTextureRef OutputNormal,
		FRDGTextureRef OutputRAM,
		const FIntPoint Resolution,
		const float NormalStrength,
		const bool bWriteRAM,
		const TCHAR* DebugName);

	void AddCraquelureMaskPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		const int32 ChildIndex);

	void QueuePendingErosion(
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		const FEffectRenderData& Effect,
		FRDGTextureRef FeatureMask);

	void QueuePendingBreakup(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		const FEffectRenderData& Effect,
		FRDGTextureRef FeatureMask);

	void AddLayerInputPass(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer);

	// The layer's own resolved albedo and roughness, for a Mask child whose source is Layer
	// Values. Idempotent: the first mask to ask for it pays, the rest share it.
	void AddLayerValuesPass(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer);

	void AddLayerFlowWarpPass(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		const FEffectRenderData& Effect,
		FRDGTextureRef FeatureMask);

	bool HasOwnedFlowWarps(
		const FLayerRenderData& Layer,
		const int32 OwnerSourceChildIndex);

	FRDGTextureRef AddOwnedMaskFlowWarpPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const int32 OwnerSourceChildIndex,
		FRDGTextureRef SourceMask);

	void AddOwnedEffectFlowWarpPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const int32 OwnerSourceChildIndex,
		FRDGTextureRef& EffectData,
		FRDGTextureRef& EffectHeight);

	void AddEffectContributionPass(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const int32 OwnerSourceChildIndex,
		FRDGTextureRef EffectData,
		FRDGTextureRef EffectHeight);

	void QueuePendingLayerBlur(
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		const FEffectRenderData& Effect,
		FRDGTextureRef FeatureMask);

	void AddLayerBlurPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer);

	void QueuePendingGrade(
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		const FEffectRenderData& Effect,
		FRDGTextureRef FeatureMask);

	void QueuePendingWornEdges(
		FMixtormatLayerPassContext& LayerCtx,
		const FChildRenderData& Child,
		const FEffectRenderData& Effect,
		FRDGTextureRef FeatureMask);

	void AddErosionPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer);

	void AddCraquelureReliefPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer);

	void AddWornEdgesPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer);

	void AddBreakupPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer);


	// MixtormatGpuSimulationPasses.cpp -- Wet Stain and Procedural Peeling: the iterative,
	// ping-ponged solves.
	void AddStainMaskPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		const int32 ChildIndex,
		const FEffectRenderData& Effect,
		FRDGTextureRef FeatureMask);

	void AddPeelingEffectPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		const int32 ChildIndex,
		const FEffectRenderData& Effect,
		FRDGTextureRef FeatureMask);

	// MixtormatGpuGeneratorPasses.cpp -- ID-phase fields of settings-only generators (Rock
	// Formation, Pebbles): publishes their Region IDs before UV From IDs resolves.
	// Remaps a height field from its measured min/max onto [OutLow, OutHigh]. New texture.
	FRDGTextureRef AddNormalizeFieldPasses(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef Field,
		FIntPoint Size,
		float OutLow,
		float OutHigh,
		const TCHAR* Name);

	// MixtormatGpuGeneratorPasses.cpp -- a Generator layer's module chain (Strata, Rock, Pebbles,
	// Cracks): per-module blend into the running height, per-module publication.
	void AddGeneratorLayerPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer);

	// MixtormatGpuRunoffPasses.cpp -- the procedural streak. Its own translation unit rather
	// than joining the two above: Runoff is deliberately not a simulation, and filing it beside
	// the ping-ponged solves would bury the one thing worth knowing about it.
	void AddRunoffMaskPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		const int32 ChildIndex,
		const FEffectRenderData& Effect,
		FRDGTextureRef FeatureMask);

	// MixtormatGpuDebugPreviewPasses.cpp -- generic child-output preview.
	//
	// For a producer that has already built its texture and just needs to show it when the
	// ChildOutput target names it: no WriteDebug branch of the producer's own kernel to gate, no
	// permutation, just colour the finished texture into OutputDebug after the fact. Callers are
	// expected to guard the call with IsChildOutputPreviewTarget first.
	void AddDebugPreviewMaskBlitPass(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef SourceMask,
		FRDGTextureRef OutputDebug,
		FIntPoint Resolution);

	// A float scalar as unclamped grayscale; signed values map through 0.5 * v + 0.5.
	void AddDebugPreviewScalarBlitPass(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef SourceScalar,
		bool bSigned,
		FRDGTextureRef OutputDebug,
		FIntPoint Resolution);

	// A float2 vector as direction hue, with zero shown as neutral dark gray.
	void AddDebugPreviewVectorBlitPass(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef SourceVector,
		FRDGTextureRef OutputDebug,
		FIntPoint Resolution);

	// A published colour field (a Generator-layer Height Color Ramp), shown as authored.
	void AddDebugPreviewColorBlitPass(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef SourceColor,
		FRDGTextureRef OutputDebug,
		FIntPoint Resolution);

	// A signed distance (negative inside) as two colours with iso-lines. DistanceToPixels takes
	// the producer's unit to output pixels -- Resolution.X for a field in UV widths.
	void AddDebugPreviewSignedDistanceBlitPass(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef SourceDistance,
		float DistanceToPixels,
		FRDGTextureRef OutputDebug,
		FIntPoint Resolution);

	// GapMask may be null: pass a Mask-kind published output on the same child to force its
	// active pixels to black instead of a hashed colour (Breakup), or null when the id map has
	// no separate gap concept to combine (Cluster IDs) or already blackens its own invalid
	// pixels inline (Pattern IDs).
	// Writes the raw Region ID of every pixel into OutputPick as a float, and the no-region
	// sentinel as -1. Float rather than uint because a UTextureRenderTarget2D reads back cleanly
	// as FLinearColor, and the composition tops out at 4096 squared -- 16,777,216 ids, which is
	// exactly the last integer float32 represents without loss.
	void AddRegionIdPickPass(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef SourceIds,
		FRDGTextureRef OutputPick,
		FIntPoint Resolution);

	void AddDebugPreviewRegionIdsBlitPass(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef SourceIds,
		FRDGTextureRef GapMask,
		FRDGTextureRef OutputDebug,
		FIntPoint Resolution);
}

// The finished state of the stack after one layer, kept between composites.
//
// Every edit used to recomposite the whole stack. Layers below the one being edited produce the
// same pixels every frame of a drag, so their result is kept here and the next composite starts
// just above it. An entry is keyed on a hash of every input to layers 0..K
// (FRenderRequest::PrefixHashes), and holds what a later layer is meant to read from below K --
// the four accumulation channels and the ridge the pipeline carries layer to layer, plus the
// dedicated snapshots later layers name explicitly (height references, Driver signals, published
// Copy Output masks). A published output that aliases a ping-pong slot refuses the save.
//
// Not carried: the effect pair (EffectTargets / EffectHeightTargets). Every reader of it on a
// layer is gated on that layer's own first write (the merge's and peel's Initialize), so nothing
// should read another layer's leftovers -- but a layer flagged bHasEffects whose effects all skip
// their contribution would read slot 0 from whatever ran before it, and after a resume that is
// the cleared slot instead. Mixtormat.DisableComposeCache 1 is the reference to compare against.
//
// Render thread only, like FMixtormatNetworkCache. Byte-bounded because one entry is five
// full-resolution targets: about 120MB at 2K and 470MB at 4K.
struct FMixtormatPrefixCache
{
	struct FPublishedFieldSnapshot
	{
		MixtormatGpuCompositor::FPublishedFieldKey Key;
		EMixtormatPublishedFieldKind Kind = EMixtormatPublishedFieldKind::RegionIds;
		TRefCountPtr<IPooledRenderTarget> Texture;
		TRefCountPtr<IPooledRenderTarget> FlowSmooth;
		TRefCountPtr<IPooledRenderTarget> Validity;
		bool bHashedIds = false;
	};

	struct FEntry
	{
		uint64 Key = 0;
		int32 LayerIndex = INDEX_NONE;
		FIntPoint Resolution = FIntPoint::ZeroValue;
		TRefCountPtr<IPooledRenderTarget> BaseColor;
		TRefCountPtr<IPooledRenderTarget> Normal;
		TRefCountPtr<IPooledRenderTarget> RAM;
		TRefCountPtr<IPooledRenderTarget> Height;
		TRefCountPtr<IPooledRenderTarget> Ridge;
		TRefCountPtr<IPooledRenderTarget> Occupancy;
		TArray<TPair<int32, TRefCountPtr<IPooledRenderTarget>>> HeightSnapshots;
		TArray<TPair<FGuid, TRefCountPtr<IPooledRenderTarget>>> DriverSnapshots;
		// Driver sources at or below LayerIndex that were demanded when this was saved. Not every
		// demanded source produces a snapshot (only signal-shaped masks do), so presence of the
		// snapshot cannot be the test; being considered is.
		TSet<FGuid> DriverDemandCovered;
		TArray<TPair<MixtormatGpuCompositor::FPublishedMaskKey, TRefCountPtr<IPooledRenderTarget>>> PublishedMasks;
				TArray<FPublishedFieldSnapshot> PublishedFields;
				TSet<MixtormatGpuCompositor::FPublishedFieldKey> FieldDemandCovered;
		uint64 Bytes = 0;
		uint64 LastUsed = 0;
	};

	static constexpr int32 MaxEntries = 2;

	TArray<TSharedPtr<FEntry, ESPMode::ThreadSafe>> Entries;
	uint64 Tick = 0;

	static uint64 TargetBytes(const TRefCountPtr<IPooledRenderTarget>& Target)
	{
		if (!Target.IsValid())
		{
			return 0;
		}
		const FPooledRenderTargetDesc& Desc = Target->GetDesc();
		return static_cast<uint64>(Desc.Extent.X) * static_cast<uint64>(Desc.Extent.Y)
			* static_cast<uint64>(GPixelFormats[Desc.Format].BlockBytes);
	}

	// Deepest usable entry: its key matches the current prefix at its layer, the layer is below
	// the limit, and it holds every snapshot the current stack will ask for.
	TSharedPtr<FEntry, ESPMode::ThreadSafe> FindDeepest(
		const TArray<uint64>& PrefixHashes,
		const FIntPoint InResolution,
		const int32 LayerLimit,
		const TFunctionRef<bool(const FEntry&)> HasRequiredSnapshots)
	{
		check(IsInRenderingThread());
		++Tick;
		TSharedPtr<FEntry, ESPMode::ThreadSafe> Best;
		for (const TSharedPtr<FEntry, ESPMode::ThreadSafe>& Entry : Entries)
		{
			if (Entry->Resolution != InResolution
				|| !PrefixHashes.IsValidIndex(Entry->LayerIndex)
				|| Entry->LayerIndex >= LayerLimit
				|| PrefixHashes[Entry->LayerIndex] != Entry->Key
				|| !HasRequiredSnapshots(*Entry))
			{
				continue;
			}
			if (!Best.IsValid() || Entry->LayerIndex > Best->LayerIndex)
			{
				Best = Entry;
			}
		}
		if (Best.IsValid())
		{
			Best->LastUsed = Tick;
		}
		return Best;
	}

	bool Contains(const uint64 Key, const int32 LayerIndex, const FIntPoint InResolution,
		const TSet<MixtormatGpuCompositor::FPublishedFieldKey>& FieldDemand,
		const TMap<FGuid, int32>& LayerIndexById) const
	{
		for (const TSharedPtr<FEntry, ESPMode::ThreadSafe>& Entry : Entries)
		{
			if (Entry->Key == Key && Entry->LayerIndex == LayerIndex && Entry->Resolution == InResolution)
			{
				bool bCovered = true;
				for (const auto& Demand : FieldDemand)
				{
					const int32* SourceIndex = LayerIndexById.Find(Demand.LayerId);
					if (SourceIndex && *SourceIndex <= LayerIndex && !Entry->FieldDemandCovered.Contains(Demand))
					{
						bCovered = false;
						break;
					}
				}
				if (bCovered) { return true; }
			}
		}
		return false;
	}

	// Called after the graph that filled Incoming's targets has executed.
	void Store(const TSharedPtr<FEntry, ESPMode::ThreadSafe>& Incoming, const uint64 BudgetBytes)
	{
		check(IsInRenderingThread());
		if (!Incoming.IsValid() || !Incoming->BaseColor.IsValid() || !Incoming->Normal.IsValid()
			|| !Incoming->RAM.IsValid() || !Incoming->Height.IsValid() || !Incoming->Ridge.IsValid()
			|| !Incoming->Occupancy.IsValid())
		{
			return;
		}
		uint64 Bytes = TargetBytes(Incoming->BaseColor) + TargetBytes(Incoming->Normal)
			+ TargetBytes(Incoming->RAM) + TargetBytes(Incoming->Height) + TargetBytes(Incoming->Ridge)
			+ TargetBytes(Incoming->Occupancy);
		for (const auto& Pair : Incoming->HeightSnapshots) { Bytes += TargetBytes(Pair.Value); }
		for (const auto& Pair : Incoming->DriverSnapshots) { Bytes += TargetBytes(Pair.Value); }
		for (const auto& Pair : Incoming->PublishedMasks) { Bytes += TargetBytes(Pair.Value); }
		TSet<IPooledRenderTarget*> KeptFieldTargets;
		for (const FPublishedFieldSnapshot& Field : Incoming->PublishedFields)
		{
			const TRefCountPtr<IPooledRenderTarget> Targets[] = {Field.Texture, Field.FlowSmooth, Field.Validity};
			for (const auto& Target : Targets)
			{
				if (Target.IsValid() && !KeptFieldTargets.Contains(Target.GetReference()))
				{
					KeptFieldTargets.Add(Target.GetReference());
					Bytes += TargetBytes(Target);
				}
			}
		}
		Incoming->Bytes = Bytes;
		if (Bytes > BudgetBytes)
		{
			return;
		}

		Entries.RemoveAll([&Incoming](const TSharedPtr<FEntry, ESPMode::ThreadSafe>& Entry)
		{
			return Entry->LayerIndex == Incoming->LayerIndex || Entry->Resolution != Incoming->Resolution;
		});
		const auto TotalBytes = [this]()
		{
			uint64 Total = 0;
			for (const TSharedPtr<FEntry, ESPMode::ThreadSafe>& Entry : Entries)
			{
				Total += Entry->Bytes;
			}
			return Total;
		};
		while (!Entries.IsEmpty() && (Entries.Num() >= MaxEntries || TotalBytes() + Bytes > BudgetBytes))
		{
			int32 OldestIndex = 0;
			for (int32 Index = 1; Index < Entries.Num(); ++Index)
			{
				if (Entries[Index]->LastUsed < Entries[OldestIndex]->LastUsed)
				{
					OldestIndex = Index;
				}
			}
			Entries.RemoveAtSwap(OldestIndex);
		}
		Incoming->LastUsed = ++Tick;
		Entries.Add(Incoming);
	}

	void Reset()
	{
		check(IsInRenderingThread());
		Entries.Reset();
	}
};

// Outputs of one self-contained producer node, kept between composites.
//
// Region ID producers are a function of their own settings and of what they read -- nothing
// else. Pattern IDs reads nothing at all; Surface/Cluster IDs read the layer's maps or the
// composite below. Keyed on exactly that (see the call sites), a hit is the map a miss would have
// produced, so editing a mask, an effect or the composite on the same layer no longer re-runs them.
struct FMixtormatNodeCacheEntry
{
	uint64 Key = 0;
	FIntPoint Resolution = FIntPoint::ZeroValue;
	// Fixed slots per producer kind; unused slots stay null. Rock Formation uses all ten.
	TRefCountPtr<IPooledRenderTarget> Outputs[10];
	uint64 Bytes = 0;
	uint64 LastUsed = 0;
};

struct FMixtormatNodeCache
{
	static constexpr uint64 MaxBytes = 256ull * 1024ull * 1024ull;

	TArray<TSharedPtr<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe>> Entries;
	uint64 Tick = 0;

	TSharedPtr<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe> Find(const uint64 Key, const FIntPoint InResolution)
	{
		check(IsInRenderingThread());
		for (const TSharedPtr<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe>& Entry : Entries)
		{
			if (Entry->Key == Key && Entry->Resolution == InResolution && Entry->Outputs[0].IsValid())
			{
				Entry->LastUsed = ++Tick;
				return Entry;
			}
		}
		return nullptr;
	}

	void Store(const TSharedPtr<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe>& Incoming)
	{
		check(IsInRenderingThread());
		if (!Incoming.IsValid() || !Incoming->Outputs[0].IsValid())
		{
			return;
		}
		uint64 Bytes = 0;
		for (const TRefCountPtr<IPooledRenderTarget>& Output : Incoming->Outputs)
		{
			Bytes += FMixtormatPrefixCache::TargetBytes(Output);
		}
		Incoming->Bytes = Bytes;
		if (Bytes > MaxBytes)
		{
			return;
		}
		Entries.RemoveAll([&Incoming](const TSharedPtr<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe>& Entry)
		{
			return Entry->Key == Incoming->Key || Entry->Resolution != Incoming->Resolution;
		});
		const auto TotalBytes = [this]()
		{
			uint64 Total = 0;
			for (const TSharedPtr<FMixtormatNodeCacheEntry, ESPMode::ThreadSafe>& Entry : Entries)
			{
				Total += Entry->Bytes;
			}
			return Total;
		};
		while (!Entries.IsEmpty() && TotalBytes() + Bytes > MaxBytes)
		{
			int32 OldestIndex = 0;
			for (int32 Index = 1; Index < Entries.Num(); ++Index)
			{
				if (Entries[Index]->LastUsed < Entries[OldestIndex]->LastUsed)
				{
					OldestIndex = Index;
				}
			}
			Entries.RemoveAtSwap(OldestIndex);
		}
		Incoming->LastUsed = ++Tick;
		Entries.Add(Incoming);
	}

	void Reset()
	{
		check(IsInRenderingThread());
		Entries.Reset();
	}
};
