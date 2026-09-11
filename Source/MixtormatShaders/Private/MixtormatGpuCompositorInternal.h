#pragma once

#include "CoreMinimal.h"
#include "Engine/TextureRenderTarget2D.h"
#include "MixtormatEffect.h"
#include "MixtormatGpuCompositor.h"
#include "MixtormatMask.h"
#include "MixtormatMaterial.h"
#include "RendererInterface.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIResources.h"

class UTexture2D;

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
	struct FPublishedMaskKey
	{
		FGuid LayerId;
		int32 ChildIndex = INDEX_NONE;
		FName Output;

		friend bool operator==(const FPublishedMaskKey& A, const FPublishedMaskKey& B)
		{
			return A.LayerId == B.LayerId && A.ChildIndex == B.ChildIndex && A.Output == B.Output;
		}

		friend uint32 GetTypeHash(const FPublishedMaskKey& Key)
		{
			return HashCombine(
				HashCombine(GetTypeHash(Key.LayerId), GetTypeHash(Key.ChildIndex)),
				GetTypeHash(Key.Output));
		}
	};

	struct FMaskRenderData
	{
		FTextureRHIRef Texture;
		FGuid PublishedSourceLayerId;
		int32 PublishedSourceChildIndex = INDEX_NONE;
		FName PublishedSourceOutput;
		EMixtormatMaskBlendMode BlendMode = EMixtormatMaskBlendMode::Replace;
		float Weight = 1.0f;
		FVector2f Tiling = FVector2f(1.0f, 1.0f);
		FVector2f UVOffset = FVector2f::ZeroVector;
		bool bFlipU = false;
		bool bFlipV = false;
		int32 Rotation = 0;
		float Balance = 0.5f;
		float Contrast = 1.0f;
		float Offset = 0.0f;
		bool bInvert = false;
	};

	struct FColorIdRenderData
	{
		FTextureRHIRef IdTexture;
		TArray<FVector4f, TInlineAllocator<FMixtormatColorIdMask::MaxColors>> Colors;
		float Tolerance = 0.10f;
		float Softness = 0.02f;
		EMixtormatMaskBlendMode BlendMode = EMixtormatMaskBlendMode::Replace;
		float Weight = 1.0f;
		bool bInvert = false;
		FVector2f Tiling = FVector2f(1.0f, 1.0f);
		FVector2f UVOffset = FVector2f::ZeroVector;
		bool bFlipU = false;
		bool bFlipV = false;
		int32 Rotation = 0;
		float Balance = 0.5f;
		float Contrast = 1.0f;
		float Offset = 0.0f;
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
		// Procedural peeling. bProceduralPeel selects the generated field over the
		// authored maps; the shaping values above are shared by both paths.
		bool bProceduralPeel = false;
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
		float PeelAOStrength = 0.8f;
		float PeelEdgeSharpness = 1.0f;
		float PeelLiftVariation = 0.6f;
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

		float ErosionAmount = 1.0f;
		float ErosionStrength = 0.08f;
		int32 ErosionOctaves = 5;
		int32 ErosionPeriod = 12;
		float ErosionGain = 0.5f;
		float ErosionDetail = 1.5f;
		float ErosionGullyWeight = 0.65f;
		float ErosionNormalization = 0.5f;
		float ErosionRidgeRounding = 0.10f;
		float ErosionCreaseRounding = 0.0f;
		float ErosionSlopeOnset = 1.0f;
		float ErosionFeatureOnset = 1.25f;
		float ErosionAssumedSlope = 0.7f;
		float ErosionAssumedSlopeAmount = 1.0f;
		float ErosionNormalStrength = 8.0f;
		int32 ErosionSlopeRadius = 2;
		float ErosionSlopeBlur = 0.0f;
		int32 ErosionCurvatureMode = 1;
		float ErosionCavityInfluence = 0.0f;
		float ErosionCavityOffset = 0.0f;
		float ErosionCavityRemapMin = 0.0f;
		float ErosionCavityRemapMax = 1.0f;
		float ErosionHeightInfluence = 0.0f;
		float ErosionHeightScale = 1.0f;
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

		float ChipAmount = 0.45f;
		float ChipGroutLevel = 0.5f;
		float ChipGroutSoftness = 0.08f;
		float ChipSize = 0.6f;
		float ChipDepth = 0.035f;
		float ChipIrregularity = 0.6f;
		int32 ChipIterations = 16;
		float ChipNormalStrength = 8.0f;
		float ChipMaskEdge = 0.0f;
		float ChipCavityInfluence = 0.5f;
		float ChipCavityOffset = 0.0f;
		float ChipCavityRemapMin = 0.0f;
		float ChipCavityRemapMax = 0.04f;
		float ChipHeightInfluence = 1.0f;
		float ChipHeightScale = 1.0f;
		FTextureRHIRef ChipPlacementMask;
		float ChipMaskTiling = 1.0f;
		bool bChipInvertMask = false;
		uint32 ChipSeed = 1;
		float ChipRoughnessAmount = 0.0f;

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
	};

	struct FGeneratedMaskRenderData
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
		float Balance = 0.5f;
		float Contrast = 1.0f;
		float Offset = 0.0f;
		bool bInvert = false;
	};

	// Craquelure. No surface inputs at all -- that is the whole reason it left the generated
	// mask, whose every signal is derived from the surface accumulated below it.
	struct FCraquelureRenderData
	{
		bool bEnabled = true;
		EMixtormatCraquelureMode Mode = EMixtormatCraquelureMode::Propagated;

		float ReliefDepth = 0.04f;
		float ReliefNormalStrength = 8.0f;
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
		bool bInvert = false;
		float Weight = 1.0f;
		float Balance = 0.5f;
		float Contrast = 1.0f;
		float Offset = 0.0f;

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
		float NormalStrength = 8.0f;
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
		float EdgeRoughness = 0.65f;
		float EdgeRoughnessAmount = 0.0f;
		float AOAmount = 0.0f;
		float AOSpread = 2.0f;
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

	struct FRandomIdRenderData
	{
		float MinValue = 0.0f;
		float MaxValue = 1.0f;
		uint32 Seed = 1;
		EMixtormatMaskBlendMode BlendMode = EMixtormatMaskBlendMode::Replace;
		float Weight = 1.0f;
		bool bInvert = false;
		float Balance = 0.5f;
		float Contrast = 1.0f;
		float Offset = 0.0f;
	};

	struct FRampIdRenderData
	{
		float HeightAmount = 0.05f;
		float NormalStrength = 8.0f;
		float AOAmount = 0.0f;
		float IntensityRandom = 0.0f;
		EMixtormatMaskBlendMode BlendMode = EMixtormatMaskBlendMode::AddSub;
		bool bRotateRandom = true;
		bool bAngleStepping = false;
		float AngleStepDegrees = 5.0f;
		uint32 Seed = 1;
	};

	struct FChildRenderData
	{
		EMixtormatLayerChildType Type = EMixtormatLayerChildType::Mask;
		int32 SourceChildIndex = INDEX_NONE;
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

	struct FLayerRenderData
	{
		FGuid LayerId;
		FScalarDriverRenderData ScalarDrivers[2];
		FTextureRHIRef BaseColor;
		FTextureRHIRef Normal;
		FTextureRHIRef RAM;
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
		float RoughnessContrast = 1.0f;
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
		float HeightBlendAmount = 1.0f;
		float HeightThreshold = 0.5f;
		float HeightRange = 0.1f;
		float HeightContrast = 1.0f;
		float HeightOffset = 0.0f;
		float HeightBias = 0.0f;
		float ConstantHeight = 0.5f;
		float MaskHeightInfluence = 0.0f;
		float HeightContactAOAmount = 0.0f;
		float HeightContactAOWidth = 0.05f;
		float HeightBorderLift = 0.0f;
		float HeightBorderWidth = 0.05f;
		float HeightBorderNormalStrength = 1.0f;
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
		bool bHeightBlendEnabled = false;
		bool bHasPackedHeight = false;
		bool bInvertHeight = false;
		bool bDirectHeightComparison = false;
		bool bInvertHeightFeature = false;
		bool bInvertAOFeature = false;
		bool bInvertFeature = false;
		uint32 HeightSource = 2u;
		int32 HeightReferenceLayerIndex = INDEX_NONE;
	};

	struct FRenderRequest
	{
		FIntPoint Resolution = FIntPoint::ZeroValue;
		TArray<FLayerRenderData> Layers;
		FTextureRHIRef OutputBC[2];
		FTextureRHIRef OutputN[2];
		FTextureRHIRef OutputRAM[2];
		FTextureRHIRef OutputHeight[2];
		FTextureRHIRef OutputDebug[2];
		FMixtormatDebugPreviewSettings DebugSettings;
		FSimpleDelegate OnComplete;
		int32 PublishedTargetIndex = 0;

		// Shared rather than raw, so a composite still in flight holds the cache alive even if
		// the panel that owns it has gone.
		TSharedPtr<FMixtormatNetworkCache, ESPMode::ThreadSafe> NetworkCache;
	};

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
		FRDGTextureRef Orientation = nullptr;
		const FPatternIdRenderData* Settings = nullptr;
	};

	// Deferred filters retain the mask visible at their own row. Keeping the
	// texture beside the effect prevents later layer masks from changing scope.
	struct FPendingEffect
	{
		const FEffectRenderData* Effect = nullptr;
		FRDGTextureRef FeatureMask = nullptr;
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
		float NormalWeight = 0.0f;
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
		float NormalStrength = 0.0f;
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
		float EdgeRoughness = 0.65f;
		float EdgeRoughnessAmount = 0.0f;
		float AOAmount = 0.0f;
		float AOSpread = 2.0f;
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

		FRDGTextureRef EmptyRegionIds = nullptr;
		FRDGTextureRef EmptyPatternUV = nullptr;
		FRDGTextureRef EmptyPatternOrientation = nullptr;
		FRDGTextureRef EmptyDriverSignal = nullptr;

		TSet<FGuid> DriverSnapshotDemand;
		TMap<FGuid, FRDGTextureRef> DriverSnapshots;
		TMap<FPublishedMaskKey, FRDGTextureRef> PublishedMaskOutputs;
		TSet<int32> RequiredHeightSnapshots;
		TMap<int32, FRDGTextureRef> HeightSnapshots;

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
		FRDGTextureRef EffectTargets[2] = {};
		FRDGTextureRef EffectHeightTargets[2] = {};

		FRDGTextureRef PeelNoiseDummy = nullptr;
		FRDGTextureRef PeelFieldDummy = nullptr;

		// Per layer, and reset by BeginLayer before anything reads it. The pending entries hold
		// pointers into one layer's children and RDG refs produced by that layer's passes;
		// neither means anything in the iteration after the one that filled them.
		int32 LayerIndex = INDEX_NONE;
		TArray<TPair<int32, FRDGTextureRef>> RegionIdMaps;
		TArray<FPatternIdPassOutput, TInlineAllocator<2>> PatternOutputs;
		FRDGTextureRef CombinedMask = nullptr;
		FRDGTextureRef CombinedEffectData = nullptr;
		FRDGTextureRef CombinedEffectHeight = nullptr;
		FRDGTextureRef DebugMask = nullptr;
		int32 MaskPassIndex = 0;
		int32 EffectPassIndex = 0;
		FPendingEffect PendingErosion;
		FPendingEffect PendingChipping;
		TArray<FPendingWornEdges, TInlineAllocator<2>> PendingWornEdges;
		TArray<FPendingCraquelureRelief, TInlineAllocator<2>> PendingCraquelureReliefs;
		TArray<FPendingRampTilt, TInlineAllocator<2>> PendingRampTilts;
		TArray<FPendingEffect, TInlineAllocator<2>> PendingFlowWarps;
		TArray<FPendingEffect, TInlineAllocator<2>> PendingGrades;

		explicit FMixtormatLayerPassContext(FMixtormatComposeContext& InCtx)
			: Ctx(InCtx)
		{
		}

		void BeginLayer(const int32 InLayerIndex)
		{
			LayerIndex = InLayerIndex;
			RegionIdMaps.Reset();
			PatternOutputs.Reset();
			CombinedMask = nullptr;
			CombinedEffectData = nullptr;
			CombinedEffectHeight = nullptr;
			DebugMask = nullptr;
			MaskPassIndex = 0;
			EffectPassIndex = 0;
			PendingErosion = FPendingEffect();
			PendingChipping = FPendingEffect();
			PendingWornEdges.Reset();
			PendingCraquelureReliefs.Reset();
			PendingRampTilts.Reset();
			PendingFlowWarps.Reset();
			PendingGrades.Reset();
		}
	};

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


	// MixtormatGpuMaskPasses.cpp -- mask processing, generated masks, mask shaping.
	FRDGTextureRef AddScopedFeatureMask(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const int32 OwnerSourceChildIndex);

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

	// MixtormatGpuPatternPasses.cpp -- Pattern IDs, Cluster/Colour IDs, HSV/Random/Ramp-from-ID.
	void AddRandomIdMaskPass(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		const int32 ChildIndex);

	void AddRegionProducerPasses(
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

	// MixtormatGpuEffectPasses.cpp -- Grade, Craquelure, Worn Edges, Chipping, Erosion,
	// Flow Warp and the shared height-derived normal.
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
		const float AOAmount,
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

	void QueuePendingChipping(
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		const FEffectRenderData& Effect,
		FRDGTextureRef FeatureMask);

	void QueuePendingFlowWarp(
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		const FEffectRenderData& Effect,
		FRDGTextureRef FeatureMask);

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

	void AddFlowWarpPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer);

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

	void AddChippingPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer);

	void AddGradePasses(
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
}
