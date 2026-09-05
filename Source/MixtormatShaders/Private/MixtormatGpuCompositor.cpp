#include "MixtormatGpuCompositor.h"

#include "Async/Async.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GlobalShader.h"
#include "MixtormatEffect.h"
#include "MixtormatMask.h"
#include "MixtormatMaterial.h"
#include "MixtormatSurface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"
#include "TextureResource.h"

// The ground every stack composites onto.
//
// Deliberately not a layer. It has no row, no selection, no children and no inspector -- it
// exists so that the bottom of the stack is an ordinary position rather than a privileged one.
// Before it, layer 0 seeded these buffers by replacing them, which meant the bottom layer
// ignored its own mask, feature influence and height blend; a layer therefore rendered
// differently depending on where it sat, and could not be freely dragged to the bottom.
//
// Values are the neutral read for each buffer, in that buffer's own encoding:
//
//   BaseColor  mid gray, so an uncovered or fully masked stack reads as unlit material
//              rather than as a hole. Alpha 1 -- the substrate is opaque by definition.
//   Normal     EncodeNormal(0, 0, 1) == n * 0.5 + 0.5, NOT (0, 0, 1). The buffer stores
//              encoded normals and the composite shader decodes what it reads, so a literal
//              (0, 0, 1) here decodes to (1, 1, ...) normalized -- a hard 45 degree tilt that
//              lights plausibly enough to survive review.
//   PackedRAM  roughness 0.5, AO 1 (unoccluded), metallic 0 (dielectric), specular 0.04.
//   Height     0.5, the midpoint every height comparison in the composite is written around.
//              Not 0: zero is the bottom of the range, not the absence of displacement, and
//              would bias every height blend against the layer above it.
namespace MixtormatSubstrate
{
	static const FVector4f BaseColor(0.5f, 0.5f, 0.5f, 1.0f);
	static const FVector4f Normal(0.5f, 0.5f, 1.0f, 1.0f);
	static const FVector4f PackedRAM(0.5f, 1.0f, 0.0f, 0.04f);
	static const FVector4f Height(0.5f, 0.0f, 0.0f, 0.0f);
}

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

namespace MixtormatNetworkKey
{
	// FNV-1a over the raw bytes of whatever is fed in. The values are floats straight out of the
	// panel, so this hashes bit patterns rather than magnitudes -- which is what is wanted: two
	// settings that differ anywhere at all must miss, and a value that round-trips through the
	// UI unchanged must hit.
	inline uint64 Combine(const uint64 Hash, const void* Data, const int32 Size)
	{
		const uint8* Bytes = static_cast<const uint8*>(Data);
		uint64 Result = Hash;
		for (int32 Index = 0; Index < Size; ++Index)
		{
			Result ^= static_cast<uint64>(Bytes[Index]);
			Result *= 1099511628211ull;
		}
		return Result;
	}

	template <typename T>
	inline uint64 Add(const uint64 Hash, const T& Value)
	{
		static_assert(TIsPODType<T>::Value, "Network key inputs are hashed as raw bytes.");
		return Combine(Hash, &Value, sizeof(T));
	}

	inline uint64 Seed()
	{
		return 14695981039346656037ull;
	}
}

class FMixtormatCompositeCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatCompositeCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatCompositeCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Enabled)
		SHADER_PARAMETER(uint32, HasMask)
		SHADER_PARAMETER(uint32, HasEffects)
		SHADER_PARAMETER(uint32, HasStain)
		SHADER_PARAMETER(uint32, OverrideBaseColor)
		SHADER_PARAMETER(uint32, OverrideRoughness)
		SHADER_PARAMETER(uint32, OverrideMetallic)
		SHADER_PARAMETER(uint32, CompositionMode)
		SHADER_PARAMETER(uint32, IsFill)
		SHADER_PARAMETER(uint32, HasSurface)
		SHADER_PARAMETER(uint32, HasPackedHeight)
		SHADER_PARAMETER(uint32, HasNormal)
		SHADER_PARAMETER(uint32, NormalOnly)
		SHADER_PARAMETER(uint32, OverrideNormal)
		SHADER_PARAMETER(uint32, FlipNormalY)
		SHADER_PARAMETER(uint32, HeightBlendEnabled)
		SHADER_PARAMETER(uint32, HeightSource)
		SHADER_PARAMETER(uint32, InvertHeight)
		SHADER_PARAMETER(uint32, DirectHeightComparison)
		SHADER_PARAMETER(uint32, InvertHeightFeature)
		SHADER_PARAMETER(uint32, InvertAOFeature)
		SHADER_PARAMETER(uint32, InvertFeature)
		SHADER_PARAMETER(uint32, DebugMode)
		SHADER_PARAMETER(uint32, WriteDebug)
		SHADER_PARAMETER(float, Opacity)
		SHADER_PARAMETER(float, Tiling)
		SHADER_PARAMETER(int32, UVScaleX)
		SHADER_PARAMETER(int32, UVScaleY)
		SHADER_PARAMETER(uint32, FlipU)
		SHADER_PARAMETER(uint32, FlipV)
		SHADER_PARAMETER(FVector2f, UVOffset)
		SHADER_PARAMETER(int32, Rotation)
		SHADER_PARAMETER(float, NormalIntensity)
		SHADER_PARAMETER(float, HueShift)
		SHADER_PARAMETER(float, Saturation)
		SHADER_PARAMETER(float, Value)
		SHADER_PARAMETER(float, RoughnessBias)
		SHADER_PARAMETER(float, RoughnessContrast)
		SHADER_PARAMETER(float, RoughnessOffset)
		SHADER_PARAMETER(float, FillRoughness)
		SHADER_PARAMETER(float, FillMetallic)
		SHADER_PARAMETER(float, LayerF0)
		SHADER_PARAMETER(float, BaseColorInfluence)
		SHADER_PARAMETER(float, RoughnessInfluence)
		SHADER_PARAMETER(float, AOInfluence)
		SHADER_PARAMETER(float, MetallicInfluence)
		SHADER_PARAMETER(float, F0Influence)
		SHADER_PARAMETER(float, NormalInfluence)
		SHADER_PARAMETER(float, HeightInfluence)
		SHADER_PARAMETER(float, HeightBlendAmount)
		SHADER_PARAMETER(float, HeightThreshold)
		SHADER_PARAMETER(float, HeightRange)
		SHADER_PARAMETER(float, HeightContrast)
		SHADER_PARAMETER(float, HeightOffset)
		SHADER_PARAMETER(float, HeightBias)
		SHADER_PARAMETER(float, ConstantHeight)
		SHADER_PARAMETER(float, MaskHeightInfluence)
		SHADER_PARAMETER(float, HeightContactAOAmount)
		SHADER_PARAMETER(float, HeightContactAOWidth)
		SHADER_PARAMETER(float, HeightBorderLift)
		SHADER_PARAMETER(float, HeightBorderWidth)
		SHADER_PARAMETER(float, HeightBorderNormalStrength)
		SHADER_PARAMETER(float, FeatureInfluence)
		SHADER_PARAMETER(float, FeatureBias)
		SHADER_PARAMETER(float, HeightFeatureInfluence)
		SHADER_PARAMETER(float, AOFeatureInfluence)
		SHADER_PARAMETER(int32, CurvatureRadius)
		SHADER_PARAMETER(float, CurvatureStrength)
		SHADER_PARAMETER(float, CurvaturePower)
		SHADER_PARAMETER(int32, CurvatureSmoothing)
		SHADER_PARAMETER(FVector4f, FillColor)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousBC)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousN)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousRAM)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ReferenceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, LayerBC)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, LayerN)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, LayerRAM)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, LayerMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, EffectData)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, EffectHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, StainData)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, DebugMask)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputBC)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputN)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputRAM)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputDebug)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatCompositeCS,
	"/Plugin/MaterialLab/Private/MixtormatComposite.usf",
	"MainCS",
	SF_Compute);

class FMixtormatMaskCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatMaskCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatMaskCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Initialize)
		SHADER_PARAMETER(uint32, BlendMode)
		SHADER_PARAMETER(uint32, Invert)
		SHADER_PARAMETER(float, Weight)
		SHADER_PARAMETER(FVector2f, Tiling)
		SHADER_PARAMETER(FVector2f, UVOffset)
		SHADER_PARAMETER(uint32, FlipU)
		SHADER_PARAMETER(uint32, FlipV)
		SHADER_PARAMETER(int32, Rotation)
		SHADER_PARAMETER(float, Balance)
		SHADER_PARAMETER(float, Contrast)
		SHADER_PARAMETER(float, Offset)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, IncomingMask)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputMask)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatMaskCS,
	"/Plugin/MaterialLab/Private/MixtormatMask.usf",
	"MainCS",
	SF_Compute);

// Colour ID mask. Selects the regions of an ID map carrying one of a set of chosen colours.
//
// The colours are a fixed-size array rather than a buffer: eight is already more of a set than
// anyone selects at once, and a constant array costs one root constant range against a structured
// buffer's descriptor and its own lifetime.
class FMixtormatColorIdCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatColorIdCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatColorIdCS, FGlobalShader);

	// Deferred to the struct rather than restated, so the array here cannot drift from the array
	// the inspector offers to fill.
	static constexpr int32 MaxColors = FMixtormatColorIdMask::MaxColors;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Initialize)
		SHADER_PARAMETER_ARRAY(FVector4f, TargetColors, [MaxColors])
		SHADER_PARAMETER(int32, ColorCount)
		SHADER_PARAMETER(float, Tolerance)
		SHADER_PARAMETER(float, Softness)
		SHADER_PARAMETER(uint32, BlendMode)
		SHADER_PARAMETER(uint32, Invert)
		SHADER_PARAMETER(float, Weight)
		SHADER_PARAMETER(float, Balance)
		SHADER_PARAMETER(float, Contrast)
		SHADER_PARAMETER(float, Offset)
		SHADER_PARAMETER(FVector2f, Tiling)
		SHADER_PARAMETER(FVector2f, UVOffset)
		SHADER_PARAMETER(uint32, FlipU)
		SHADER_PARAMETER(uint32, FlipV)
		SHADER_PARAMETER(int32, Rotation)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, IdTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, PointSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputMask)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatColorIdCS,
	"/Plugin/MaterialLab/Private/MixtormatColorId.usf",
	"MainCS",
	SF_Compute);

class FMixtormatGeneratedMaskCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatGeneratedMaskCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatGeneratedMaskCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Initialize)
		SHADER_PARAMETER(uint32, SurfaceValid)
		SHADER_PARAMETER(uint32, FlipNormalY)
		SHADER_PARAMETER(float, CurvatureWeight)
		SHADER_PARAMETER(float, CurvatureBias)
		SHADER_PARAMETER(float, CurvatureStrength)
		SHADER_PARAMETER(float, CurvaturePower)
		SHADER_PARAMETER(float, DirectionWeight)
		SHADER_PARAMETER(float, DirectionAngle)
		SHADER_PARAMETER(float, DirectionBroadness)
		SHADER_PARAMETER(float, AOWeight)
		SHADER_PARAMETER(float, HeightWeight)
		SHADER_PARAMETER(float, HeightBias)
		SHADER_PARAMETER(float, RidgeWeight)
		SHADER_PARAMETER(uint32, NormalizeWeights)
		SHADER_PARAMETER(int32, Broadness)
		SHADER_PARAMETER(int32, Smoothing)
		SHADER_PARAMETER(float, Bias)
		SHADER_PARAMETER(float, WarpAmount)
		SHADER_PARAMETER(float, WarpSource)
		SHADER_PARAMETER(int32, WarpRadius)
		SHADER_PARAMETER(uint32, BlendMode)
		SHADER_PARAMETER(uint32, Invert)
		SHADER_PARAMETER(float, Weight)
		SHADER_PARAMETER(float, Balance)
		SHADER_PARAMETER(float, Contrast)
		SHADER_PARAMETER(float, Offset)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SurfaceNormal)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SurfaceRAM)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SurfaceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SurfaceRidge)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputMask)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatGeneratedMaskCS,
	"/Plugin/MaterialLab/Private/MixtormatGeneratedMask.usf",
	"MainCS",
	SF_Compute);

// Craquelure. A crack network on a cellular lattice, blended into the layer mask.
//
// Its own node rather than a signal on the generated mask: that node reads the surface below
// and early-returns when there is none, while this is generated from a lattice and means
// something on the bottom layer. The mask tail is shared through MixtormatMaskOps.ush rather
// than through a shared parameter struct, so this one carries no surface textures at all.
class FMixtormatCraquelureCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatCraquelureCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatCraquelureCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Initialize)
		SHADER_PARAMETER(int32, Period)
		SHADER_PARAMETER(float, Jitter)
		SHADER_PARAMETER(float, Width)
		SHADER_PARAMETER(float, Variation)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(float, Warp)
		SHADER_PARAMETER(int32, WarpPeriod)
		SHADER_PARAMETER(uint32, WarpSeed)
		SHADER_PARAMETER(uint32, BlendMode)
		SHADER_PARAMETER(uint32, Invert)
		SHADER_PARAMETER(float, Weight)
		SHADER_PARAMETER(float, Balance)
		SHADER_PARAMETER(float, Contrast)
		SHADER_PARAMETER(float, Offset)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousMask)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputDistance)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatCraquelureCS,
	"/Plugin/MaterialLab/Private/MixtormatCraquelure.usf",
	"MainCS",
	SF_Compute);

// Propagated craquelure. Three entry points in one file, one shader class each.
//
// Each class binds only the parameters its own entry point uses, rather than a shared struct
// covering all three. That is not tidiness: RDG rejects a pass that binds a transient nothing
// has written, so a seed pass carrying a PreviousState slot would fail on the first dispatch.
class FMixtormatCraquelureSeedCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatCraquelureSeedCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatCraquelureSeedCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(int32, SeedCells)
		SHADER_PARAMETER(float, SeedChance)
		SHADER_PARAMETER(float, SeedJitter)
		SHADER_PARAMETER(int32, NoiseCells)
		SHADER_PARAMETER(float, StressVariation)
		SHADER_PARAMETER(float, ToughnessVariation)
		SHADER_PARAMETER(float, Warp)
		SHADER_PARAMETER(int32, WarpPeriod)
		SHADER_PARAMETER(uint32, WarpSeed)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputState)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputDirection)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputField)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatCraquelureSeedCS,
	"/Plugin/MaterialLab/Private/MixtormatCraquelureGrow.usf",
	"SeedCS",
	SF_Compute);

class FMixtormatCraquelureGrowCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatCraquelureGrowCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatCraquelureGrowCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(float, Persistence)
		SHADER_PARAMETER(float, FlowStrength)
		SHADER_PARAMETER(float, StressGain)
		SHADER_PARAMETER(float, ToughnessCost)
		SHADER_PARAMETER(float, Irregularity)
		SHADER_PARAMETER(float, GrowthThreshold)
		SHADER_PARAMETER(float, MinAlignment)
		SHADER_PARAMETER(float, TurnResponse)
		SHADER_PARAMETER(int32, CollisionLimit)
		SHADER_PARAMETER(int32, Iteration)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousState)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousDirection)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, Field)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputState)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputDirection)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatCraquelureGrowCS,
	"/Plugin/MaterialLab/Private/MixtormatCraquelureGrow.usf",
	"GrowCS",
	SF_Compute);

class FMixtormatCraquelureResolveCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatCraquelureResolveCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatCraquelureResolveCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Initialize)
		SHADER_PARAMETER(int32, SeedCells)
		SHADER_PARAMETER(float, Width)
		SHADER_PARAMETER(float, Variation)
		SHADER_PARAMETER(uint32, BlendMode)
		SHADER_PARAMETER(uint32, Invert)
		SHADER_PARAMETER(float, Weight)
		SHADER_PARAMETER(float, Balance)
		SHADER_PARAMETER(float, Contrast)
		SHADER_PARAMETER(float, Offset)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, CrackDistance)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousMask)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputMask)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatCraquelureResolveCS,
	"/Plugin/MaterialLab/Private/MixtormatCraquelureGrow.usf",
	"ResolveCS",
	SF_Compute);

// Distance to the nearest crack, by jump flooding. Three entry points, one class each, for the
// same reason the growth passes are split: RDG rejects a pass that binds a transient nothing has
// written, so a seed pass carrying a PreviousRecord slot would fail on its first dispatch.
class FMixtormatCraquelureDistanceSeedCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatCraquelureDistanceSeedCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatCraquelureDistanceSeedCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, CrackState)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputRecord)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatCraquelureDistanceSeedCS,
	"/Plugin/MaterialLab/Private/MixtormatCraquelureDistance.usf",
	"SeedCS",
	SF_Compute);

class FMixtormatCraquelureDistanceStepCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatCraquelureDistanceStepCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatCraquelureDistanceStepCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(int32, StepSize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousRecord)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputRecord)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatCraquelureDistanceStepCS,
	"/Plugin/MaterialLab/Private/MixtormatCraquelureDistance.usf",
	"StepCS",
	SF_Compute);

class FMixtormatCraquelureDistanceResolveCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatCraquelureDistanceResolveCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatCraquelureDistanceResolveCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousRecord)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputDistance)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatCraquelureDistanceResolveCS,
	"/Plugin/MaterialLab/Private/MixtormatCraquelureDistance.usf",
	"ResolveDistanceCS",
	SF_Compute);

// Craquelure relief. Reads the distance field rather than the mask: by the time the mask exists
// it has been through shaping, a blend mode and a weight lerp, and the distance the groove
// profile needs is gone.
class FMixtormatCraquelureReliefCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatCraquelureReliefCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatCraquelureReliefCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, HeightWeight)
		SHADER_PARAMETER(float, NormalWeight)
		SHADER_PARAMETER(float, ReliefWidthPixels)
		SHADER_PARAMETER(float, Variation)
		SHADER_PARAMETER(float, Profile)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, CrackDistance)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousNormal)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputNormal)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatCraquelureReliefCS,
	"/Plugin/MaterialLab/Private/MixtormatCraquelureRelief.usf",
	"MainCS",
	SF_Compute);

class FMixtormatErosionCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatErosionCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatErosionCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(int32, NormalPass)
		SHADER_PARAMETER(int32, BlurPass)
		SHADER_PARAMETER(int32, BlurAxis)
		SHADER_PARAMETER(int32, ResamplePass)
		SHADER_PARAMETER(int32, ResampleRidge)
		SHADER_PARAMETER(float, BlurRadius)
		SHADER_PARAMETER(float, NormalStrength)
		SHADER_PARAMETER(float, Amount)
		SHADER_PARAMETER(float, Strength)
		SHADER_PARAMETER(int32, Octaves)
		SHADER_PARAMETER(int32, Period)
		SHADER_PARAMETER(float, Gain)
		SHADER_PARAMETER(float, Detail)
		SHADER_PARAMETER(float, GullyWeight)
		SHADER_PARAMETER(float, Normalization)
		SHADER_PARAMETER(float, RidgeRounding)
		SHADER_PARAMETER(float, CreaseRounding)
		SHADER_PARAMETER(float, SlopeOnset)
		SHADER_PARAMETER(float, FeatureOnset)
		SHADER_PARAMETER(float, AssumedSlope)
		SHADER_PARAMETER(float, AssumedSlopeAmount)
		SHADER_PARAMETER(int32, SlopeRadius)
		SHADER_PARAMETER(int32, CurvatureMode)
		SHADER_PARAMETER(float, CavityInfluence)
		SHADER_PARAMETER(float, CavityOffset)
		SHADER_PARAMETER(float, CavityRemapMin)
		SHADER_PARAMETER(float, CavityRemapMax)
		SHADER_PARAMETER(float, HeightInfluence)
		SHADER_PARAMETER(float, HeightScale)
		SHADER_PARAMETER(uint32, UsePlacementMask)
		SHADER_PARAMETER(float, PlacementMaskTiling)
		SHADER_PARAMETER(uint32, InvertMask)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousNormal)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, GuideHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, LayerMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PlacementMaskTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousRidge)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputRidge)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputNormal)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatErosionCS,
	"/Plugin/MaterialLab/Private/MixtormatErosion.usf",
	"MainCS",
	SF_Compute);

// Colour grade. The second Filter: it transforms the base colour composited up to its owning
// layer and is the identity at zero amount.
class FMixtormatGradeCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatGradeCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatGradeCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, HasMask)
		SHADER_PARAMETER(uint32, InvertMask)
		SHADER_PARAMETER(int32, TonemapMode)
		SHADER_PARAMETER(float, TonemapStrength)
		SHADER_PARAMETER(float, Brightness)
		SHADER_PARAMETER(float, Contrast)
		SHADER_PARAMETER(float, ContrastPivot)
		SHADER_PARAMETER(float, Gamma)
		SHADER_PARAMETER(float, Amount)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SourceColor)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, LayerMask)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputColor)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatGradeCS,
	"/Plugin/MaterialLab/Private/MixtormatGrade.usf",
	"MainCS",
	SF_Compute);

// Colour and roughness for what erosion removed, blended into what the layer composite has
// already written. Kept separate from FMixtormatErosionCS so its four texture slots are not
// bound, and dummied, on every carving and resample dispatch that has no use for them.
class FMixtormatCarveShadeCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatCarveShadeCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatCarveShadeCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(FVector4f, ErodedColor)
		SHADER_PARAMETER(float, ColorAmount)
		SHADER_PARAMETER(float, RoughnessAmount)
		SHADER_PARAMETER(float, CarveDepth)
		SHADER_PARAMETER(uint32, UseCoverageTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, CarvedHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, CoverageTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SourceColor)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SourceRAM)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputColor)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputRAM)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatCarveShadeCS,
	"/Plugin/MaterialLab/Private/MixtormatCarveShade.usf",
	"MainCS",
	SF_Compute);

// Min/max of a scalar texture, folded to 1x1 over a few passes. Chipping thresholds the
// composited height against this rather than against the nominal range of the format, which is
// what makes its Grout Level a position inside the content instead of an absolute value that
// lands on the clear colour.
class FMixtormatReduceMinMaxCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatReduceMinMaxCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatReduceMinMaxCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, InputSize)
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(int32, FirstPass)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SourceRange)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputRange)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatReduceMinMaxCS,
	"/Plugin/MaterialLab/Private/MixtormatReduceMinMax.usf",
	"MainCS",
	SF_Compute);

// The fold factor the reduction shader uses. Declared here too because the pass count and the
// intermediate sizes are worked out on this side.
static constexpr int32 GMixtormatReduceFactor = 16;

// Chipping. A smooth height selection mixed with local cavity seeds chips, which grow inward
// over N iterations. One dispatch per iteration ping-pongs (core, tip, dirX, dirY); height stays
// read-only so the selection remains defined by the surface chipping received.
class FMixtormatChippingCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatChippingCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatChippingCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(int32, Iteration)
		SHADER_PARAMETER(int32, NormalPass)
		SHADER_PARAMETER(float, NormalStrength)
		SHADER_PARAMETER(float, GroutLevel)
		SHADER_PARAMETER(float, GroutSoftness)
		SHADER_PARAMETER(float, ChipAmount)
		SHADER_PARAMETER(float, ChipSize)
		SHADER_PARAMETER(float, ChipDepth)
		SHADER_PARAMETER(float, Irregularity)
		SHADER_PARAMETER(float, MaskEdge)
		SHADER_PARAMETER(float, CavityInfluence)
		SHADER_PARAMETER(float, CavityOffset)
		SHADER_PARAMETER(float, CavityRemapMin)
		SHADER_PARAMETER(float, CavityRemapMax)
		SHADER_PARAMETER(float, HeightInfluence)
		SHADER_PARAMETER(float, HeightScale)
		SHADER_PARAMETER(uint32, UsePlacementMask)
		SHADER_PARAMETER(float, PlacementMaskTiling)
		SHADER_PARAMETER(uint32, InvertMask)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, HeightRange)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousState)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, LayerMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PlacementMaskTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ChipsTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousNormal)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputState)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputChips)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputNormal)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatChippingCS,
	"/Plugin/MaterialLab/Private/MixtormatChipping.usf",
	"MainCS",
	SF_Compute);

// Coherent tangent flow. Built from a height texture, consumed by any effect wanting a
// direction that follows the surface rather than the per-pixel gradient. Deliberately its
// own shader rather than another mode on the effects that use it: the two call sites differ
// in resolution, in which height they read and in where they sit in the graph, so the thing
// worth sharing is the construction, not a texture.
class FMixtormatFlowCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatFlowCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatFlowCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(int32, Mode)
		SHADER_PARAMETER(int32, Radius)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, PreviousFlow)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutputFlow)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatFlowCS,
	"/Plugin/MaterialLab/Private/MixtormatFlow.usf",
	"MainCS",
	SF_Compute);

class FMixtormatPeelingCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatPeelingCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatPeelingCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Initialize)
		SHADER_PARAMETER(float, Tiling)
		SHADER_PARAMETER(float, Strength)
		SHADER_PARAMETER(float, Front)
		SHADER_PARAMETER(float, Width)
		SHADER_PARAMETER(float, MacroWarp)
		SHADER_PARAMETER(float, MicroWarp)
		SHADER_PARAMETER(float, MicroMorph)
		SHADER_PARAMETER(float, Thickness)
		SHADER_PARAMETER(float, Lift)
		SHADER_PARAMETER(float, DetailStrength)
		SHADER_PARAMETER(float, DistanceRange)
		SHADER_PARAMETER(float, SDFRange)
		SHADER_PARAMETER(float, HeightRange)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousEffectData)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ChildMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PeelData)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PeelMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PeelHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PeelSDF)
		SHADER_PARAMETER(uint32, ProceduralSource)
		SHADER_PARAMETER(float, ProceduralAOStrength)
		SHADER_PARAMETER(float, HeightAmount)
		SHADER_PARAMETER(float, HeightInvert)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PeelFieldA)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PeelFieldB)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, PointWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousEffectHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputEffectData)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputEffectHeight)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatPeelingCS,
	"/Plugin/MaterialLab/Private/MixtormatPeeling.usf",
	"MainCS",
	SF_Compute);

// Grows a peel front across the surface, replacing the authored PDM/MSK/H/SDF set.
// Seed and Solve run at reduced resolution; Resolve runs full and filters arrival up.
class FMixtormatPeelFieldCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatPeelFieldCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatPeelFieldCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(FIntPoint, SolveSize)
		SHADER_PARAMETER(int32, Mode)
		SHADER_PARAMETER(uint32, SurfaceValid)
		SHADER_PARAMETER(uint32, FlipNormalY)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(float, MaskWeight)
		SHADER_PARAMETER(float, PeelMaskTiling)
		SHADER_PARAMETER(uint32, PeelMaskInvert)
		SHADER_PARAMETER(uint32, UseOwnMask)
		SHADER_PARAMETER(float, SeedThreshold)
		SHADER_PARAMETER(int32, CurvatureRadius)
		SHADER_PARAMETER(int32, CurvatureSmoothing)
		SHADER_PARAMETER(float, CurvatureWeight)
		SHADER_PARAMETER(float, CurvatureBias)
		SHADER_PARAMETER(float, AOWeight)
		SHADER_PARAMETER(float, HeightWeight)
		SHADER_PARAMETER(uint32, NormalizeWeights)
		SHADER_PARAMETER(float, GrowthStrength)
		SHADER_PARAMETER(float, SizeVariation)
		SHADER_PARAMETER(int32, FlakeCells)
		SHADER_PARAMETER(int32, PeelType)
		SHADER_PARAMETER(float, Front)
		SHADER_PARAMETER(float, Width)
		SHADER_PARAMETER(float, MacroWarp)
		SHADER_PARAMETER(float, MicroWarp)
		SHADER_PARAMETER(float, MicroMorph)
		SHADER_PARAMETER(float, Thickness)
		SHADER_PARAMETER(float, Lift)
		SHADER_PARAMETER(float, DetailStrength)
		SHADER_PARAMETER(float, LiftVariation)
		SHADER_PARAMETER(float, EdgeSharpness)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SurfaceNormal)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SurfaceRAM)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SurfaceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ChildMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PeelOwnMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, PreviousArrival)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, GrowthField)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutputArrival)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputGrowth)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputFieldA)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputFieldB)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatPeelFieldCS,
	"/Plugin/MaterialLab/Private/MixtormatPeelField.usf",
	"MainCS",
	SF_Compute);

class FMixtormatStainCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatStainCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatStainCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Initialize)
		SHADER_PARAMETER(float, Strength)
		SHADER_PARAMETER(FVector4f, StainColor)
		SHADER_PARAMETER(float, RoughnessInfluence)
		SHADER_PARAMETER(float, HeightInfluence)
		SHADER_PARAMETER(float, HeightWarp)
		SHADER_PARAMETER(float, HeightBias)
		SHADER_PARAMETER(float, HeightContrast)
		SHADER_PARAMETER(float, FlowAmount)
		SHADER_PARAMETER(float, Gravity)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousStainData)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ChildMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, AccumulatedHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, FlowField)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputStainData)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatStainCS,
	"/Plugin/MaterialLab/Private/MixtormatStain.usf",
	"MainCS",
	SF_Compute);

namespace MixtormatGpuCompositor
{
	struct FMaskRenderData
	{
		FTextureRHIRef Texture;
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
		TArray<FVector4f, TInlineAllocator<FMixtormatColorIdCS::MaxColors>> Colors;
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
		FTextureRHIRef PeelData;
		FTextureRHIRef Mask;
		FTextureRHIRef Height;
		FTextureRHIRef SDF;
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
		float DistanceRange = 1.0f;
		float SDFRange = 0.1f;
		float HeightRange = 0.1f;
		FLinearColor StainColor = FLinearColor(0.22f, 0.09f, 0.035f, 1.0f);
		float StainRoughness = 0.2f;
		float StainHeightInfluence = 0.5f;
		float StainHeightWarp = 0.0f;
		float StainHeightBias = -1.0f;
		float StainHeightContrast = 1.0f;
		float StainFlowAmount = 0.0f;
		float StainGravity = 1.0f;
		int32 StainFlowRadius = 4;
		int32 StainFlowSmoothing = 3;
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
		FLinearColor ErosionColor = FLinearColor(0.16f, 0.14f, 0.12f, 1.0f);
		float ErosionColorAmount = 0.0f;
		float ErosionRoughnessAmount = 0.0f;
		float ErosionCarveDepth = 0.05f;

		float GradeAmount = 1.0f;
		int32 GradeTonemap = 0;
		float GradeTonemapStrength = 1.0f;
		float GradeBrightness = 1.0f;
		float GradeContrast = 1.0f;
		float GradeContrastPivot = 0.18f;
		float GradeGamma = 1.0f;

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
		FLinearColor ChipColor = FLinearColor(0.34f, 0.30f, 0.27f, 1.0f);
		float ChipColorAmount = 0.0f;
		float ChipRoughnessAmount = 0.0f;
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

	struct FChildRenderData
	{
		EMixtormatLayerChildType Type = EMixtormatLayerChildType::Mask;
		int32 SourceChildIndex = INDEX_NONE;
		FMaskRenderData Mask;
		FEffectRenderData Effect;
		FGeneratedMaskRenderData Generated;
		FCraquelureRenderData Craquelure;
		FColorIdRenderData ColorId;
	};

	struct FLayerRenderData
	{
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
		float BaseColorInfluence = 1.0f;
		float RoughnessInfluence = 1.0f;
		float AOInfluence = 1.0f;
		float MetallicInfluence = 1.0f;
		float F0Influence = 1.0f;
		float NormalInfluence = 1.0f;
		float HeightInfluence = 1.0f;
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
		bool bHasStain = false;
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

	FTextureRHIRef GetTextureRHI(UTexture2D* Texture)
	{
		return Texture && Texture->GetResource()
			? Texture->GetResource()->TextureRHI
			: FTextureRHIRef();
	}

	UTextureRenderTarget2D* CreateTarget(
		const FIntPoint Resolution,
		const FLinearColor ClearColor,
		const EPixelFormat Format = PF_FloatRGBA)
	{
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
		Target->ClearColor = ClearColor;
		Target->bCanCreateUAV = true;
		Target->bAutoGenerateMips = false;
		Target->Filter = TF_Bilinear;
		Target->AddressX = TA_Wrap;
		Target->AddressY = TA_Wrap;
		Target->InitCustomFormat(Resolution.X, Resolution.Y, Format, true);
		Target->UpdateResourceImmediate(true);
		return Target;
	}

	FTextureRHIRef GetTargetRHI(UTextureRenderTarget2D* Target)
	{
		return Target && Target->GameThread_GetRenderTargetResource()
			? Target->GameThread_GetRenderTargetResource()->GetRenderTargetTexture()
			: FTextureRHIRef();
	}

	FRDGTextureRef RegisterTexture(
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
}

FMixtormatGpuCompositor::FMixtormatGpuCompositor()
	: NetworkCache(MakeShared<FMixtormatNetworkCache, ESPMode::ThreadSafe>())
{
}

FMixtormatGpuCompositor::~FMixtormatGpuCompositor()
{
	// The entries hold pooled render targets, which have to be released on the render thread.
	// Dropping the last reference here would release them on whichever thread destroyed the
	// panel, so the contents go first and the shared pointer is left to expire on its own.
	if (NetworkCache.IsValid())
	{
		ENQUEUE_RENDER_COMMAND(MixtormatFlushNetworkCache)(
			[Cache = NetworkCache](FRHICommandListImmediate&)
			{
				Cache->Reset();
			});
	}
	NetworkCache.Reset();
}

bool FMixtormatGpuCompositor::Initialize(const FIntPoint InResolution)
{
	using namespace MixtormatGpuCompositor;
	if (InResolution.X <= 0 || InResolution.Y <= 0)
	{
		return false;
	}
	if (bInitialized && Resolution == InResolution)
	{
		return true;
	}

	Resolution = InResolution;
	for (FTargetSet& Set : Targets)
	{
		Set.BaseColor.Reset(CreateTarget(Resolution, FLinearColor::Black));
		Set.Normal.Reset(CreateTarget(Resolution, FLinearColor(0.5f, 0.5f, 1.0f, 1.0f)));
		Set.RAM.Reset(CreateTarget(Resolution, FLinearColor(0.5f, 1.0f, 0.0f, 0.04f)));
		Set.Height.Reset(CreateTarget(Resolution, FLinearColor(0.5f, 0.0f, 0.0f, 0.0f), PF_R16F));
		Set.Debug.Reset(CreateTarget(Resolution, FLinearColor(0.08f, 0.02f, 0.12f, 1.0f)));
	}
	if (NetworkCache.IsValid())
	{
		// Entries are keyed on resolution, so stale ones could never be hit again. They would
		// still hold their pooled targets at the old size until six newer networks pushed them
		// out, which at 4K is a lot of memory to keep for nothing.
		ENQUEUE_RENDER_COMMAND(MixtormatResizeNetworkCache)(
			[Cache = NetworkCache](FRHICommandListImmediate&)
			{
				Cache->Reset();
			});
	}

	FlushRenderingCommands();
	PublishedTargetIndex = 0;
	bInitialized = true;
	return true;
}

bool FMixtormatGpuCompositor::RequestCompose(
	const TArray<FMixtormatLayer>& Layers,
	FSimpleDelegate OnComplete,
	FMixtormatDebugPreviewSettings DebugSettings)
{
	using namespace MixtormatGpuCompositor;
	check(IsInGameThread());
	if (!bInitialized && !Initialize())
	{
		return false;
	}

	// Resolved once and kept. These are engine defaults that stand in for a missing map, so they
	// never change, and this runs on the game thread on every frame of a slider drag -- a package
	// lookup each time for two objects that were already resolved on the first composite.
	//
	// Strong pointers rather than weak: they are engine content that outlives the panel, and a
	// weak one would send us back through LoadObject the moment garbage collection ran with
	// nothing else referencing them.
	static TStrongObjectPtr<UTexture2D> CachedWhiteTexture;
	static TStrongObjectPtr<UTexture2D> CachedNormalTexture;
	if (!CachedWhiteTexture.IsValid())
	{
		CachedWhiteTexture.Reset(LoadObject<UTexture2D>(
			nullptr,
			TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture")));
	}
	if (!CachedNormalTexture.IsValid())
	{
		CachedNormalTexture.Reset(LoadObject<UTexture2D>(
			nullptr,
			TEXT("/Engine/EngineMaterials/DefaultNormal.DefaultNormal")));
	}

	UTexture2D* WhiteTexture = CachedWhiteTexture.Get();
	UTexture2D* NormalTexture = CachedNormalTexture.Get();
	if (!WhiteTexture || !NormalTexture)
	{
		return false;
	}

	FRenderRequest Request;
	Request.Resolution = Resolution;
	Request.DebugSettings = DebugSettings;
	Request.OnComplete = MoveTemp(OnComplete);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		Request.OutputBC[Index] = GetTargetRHI(Targets[Index].BaseColor.Get());
		Request.OutputN[Index] = GetTargetRHI(Targets[Index].Normal.Get());
		Request.OutputRAM[Index] = GetTargetRHI(Targets[Index].RAM.Get());
		Request.OutputHeight[Index] = GetTargetRHI(Targets[Index].Height.Get());
		Request.OutputDebug[Index] = GetTargetRHI(Targets[Index].Debug.Get());
		if (!Request.OutputBC[Index].IsValid()
			|| !Request.OutputN[Index].IsValid()
			|| !Request.OutputRAM[Index].IsValid()
			|| !Request.OutputHeight[Index].IsValid()
			|| !Request.OutputDebug[Index].IsValid())
		{
			return false;
		}
	}

	for (int32 LayerIndex = 0; LayerIndex < Layers.Num(); ++LayerIndex)
	{
		const FMixtormatLayer& Layer = Layers[LayerIndex];
		FLayerRenderData& Data = Request.Layers.AddDefaulted_GetRef();
		const UMixtormatSurface* Surface = Layer.SourceSurface.LoadSynchronous();
		const bool bNormalOnly = Layer.ChannelMode == EMixtormatLayerChannelMode::NormalDetail;
		UTexture2D* LayerBaseColor = Surface && Surface->BaseColor ? Surface->BaseColor.Get() : WhiteTexture;
		UTexture2D* LayerNormal = Surface && Surface->Normal ? Surface->Normal.Get() : NormalTexture;
		if (bNormalOnly && Layer.NormalSourceType == EMixtormatNormalSourceType::Texture)
		{
			LayerNormal = Layer.NormalTexture.LoadSynchronous();
		}
		UTexture2D* LayerRAM = Surface && Surface->RoughnessAOMetallic
			? Surface->RoughnessAOMetallic.Get()
			: WhiteTexture;

		Data.BaseColor = GetTextureRHI(LayerBaseColor);
		Data.Normal = GetTextureRHI(LayerNormal ? LayerNormal : NormalTexture);
		Data.RAM = GetTextureRHI(LayerRAM);
		Data.Mask = GetTextureRHI(WhiteTexture);
		if (!Data.BaseColor.IsValid()
			|| !Data.Normal.IsValid()
			|| !Data.RAM.IsValid()
			|| !Data.Mask.IsValid())
		{
			return false;
		}
		Data.FillColor = FVector4f(
			Layer.BaseColor.R,
			Layer.BaseColor.G,
			Layer.BaseColor.B,
			Layer.BaseColor.A);
		for (int32 SourceChildIndex = 0; SourceChildIndex < Layer.Children.Num(); ++SourceChildIndex)
		{
			const FMixtormatLayerChild& LayerChild = Layer.Children[SourceChildIndex];

			// Mask children still resolve on disabled layers so other layers can reference them.
			// Effects never contribute to that mask, and filters run after the disabled composite,
			// so capturing them would let a hidden layer modify the accumulated result.
			if (!Layer.bEnabled && LayerChild.Type == EMixtormatLayerChildType::Effect)
			{
				continue;
			}

			if (LayerChild.Type == EMixtormatLayerChildType::ColorId)
			{
				const FMixtormatColorIdMask& ColorIdMask = LayerChild.ColorId;
				if (!ColorIdMask.bEnabled)
				{
					continue;
				}

				UTexture2D* IdTexture = ColorIdMask.IdTexture.LoadSynchronous();

				// A node with no map or no colours selects nothing, and selecting nothing is not
				// the same as being the identity: it would blend a mask of zero. Dropping it
				// entirely is what an unconfigured node should do, and matches how a painted mask
				// with no texture behaves.
				if (!IdTexture || ColorIdMask.Colors.IsEmpty())
				{
					continue;
				}

				FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
				ChildData.Type = EMixtormatLayerChildType::ColorId;
				ChildData.SourceChildIndex = SourceChildIndex;
				FColorIdRenderData& IdData = ChildData.ColorId;
				IdData.IdTexture = GetTextureRHI(IdTexture);
				if (!IdData.IdTexture.IsValid())
				{
					return false;
				}

				// Truncated rather than reported: the array is what the shader can hold, and the
				// inspector does not offer to add past it, so this only trips on data authored
				// through Blueprint or a hand-edited asset.
				const int32 ColorCount =
					FMath::Min(ColorIdMask.Colors.Num(), FMixtormatColorIdCS::MaxColors);
				for (int32 ColorIndex = 0; ColorIndex < ColorCount; ++ColorIndex)
				{
					const FLinearColor& Color = ColorIdMask.Colors[ColorIndex];
					IdData.Colors.Add(FVector4f(Color.R, Color.G, Color.B, 1.0f));
				}

				IdData.Tolerance = FMath::Clamp(ColorIdMask.Tolerance, 0.0f, 1.732f);
				IdData.Softness = FMath::Clamp(ColorIdMask.Softness, 0.0f, 0.5f);
				IdData.BlendMode = ColorIdMask.BlendMode;
				IdData.Weight = FMath::Clamp(ColorIdMask.Weight, 0.0f, 1.0f);
				IdData.bInvert = ColorIdMask.bInvert;
				IdData.Tiling = FVector2f(
					static_cast<float>(FMath::Max(ColorIdMask.TilingX, 1)),
					static_cast<float>(FMath::Max(ColorIdMask.TilingY, 1)));
				IdData.UVOffset = FVector2f(ColorIdMask.UVOffsetX, ColorIdMask.UVOffsetY);
				IdData.bFlipU = ColorIdMask.bFlipU;
				IdData.bFlipV = ColorIdMask.bFlipV;
				IdData.Rotation = static_cast<int32>(ColorIdMask.Rotation);
				IdData.Balance = FMath::Clamp(ColorIdMask.Balance, 0.0f, 2.0f);
				IdData.Contrast = FMath::Clamp(ColorIdMask.Contrast, 0.0f, 10.0f);
				IdData.Offset = FMath::Clamp(ColorIdMask.Offset, -1.0f, 1.0f);
				Data.bHasMask = true;
				continue;
			}

			if (LayerChild.Type == EMixtormatLayerChildType::Mask)
			{
				const FMixtormatMaskLayer& MaskLayer = LayerChild.Mask;
				if (!MaskLayer.bEnabled)
				{
					continue;
				}

				UTexture2D* MaskTexture = MaskLayer.MaskTexture.LoadSynchronous();
				if (!MaskTexture)
				{
					if (const UMixtormatMask* MaskAsset = MaskLayer.Mask.LoadSynchronous())
					{
						MaskTexture = MaskAsset->MaskTexture.Get();
					}
				}
				if (!MaskTexture)
				{
					continue;
				}

				FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
				ChildData.Type = EMixtormatLayerChildType::Mask;
				ChildData.SourceChildIndex = SourceChildIndex;
				FMaskRenderData& MaskData = ChildData.Mask;
				MaskData.Texture = GetTextureRHI(MaskTexture);
				if (!MaskData.Texture.IsValid())
				{
					return false;
				}
				MaskData.BlendMode = MaskLayer.BlendMode;
				MaskData.Weight = FMath::Clamp(MaskLayer.Weight, 0.0f, 1.0f);
				// Integer per axis: the shader wraps the read in a frac(), and a fractional
				// scale lands mid-cell at that wrap.
				MaskData.Tiling = FVector2f(
					static_cast<float>(FMath::Max(MaskLayer.TilingX, 1)),
					static_cast<float>(FMath::Max(MaskLayer.TilingY, 1)));
				MaskData.UVOffset = FVector2f(MaskLayer.UVOffsetX, MaskLayer.UVOffsetY);
				MaskData.bFlipU = MaskLayer.bFlipU;
				MaskData.bFlipV = MaskLayer.bFlipV;
				MaskData.Rotation = static_cast<int32>(MaskLayer.Rotation);
				MaskData.Balance = FMath::Clamp(MaskLayer.Balance, 0.0f, 2.0f);
				MaskData.Contrast = FMath::Clamp(MaskLayer.Contrast, 0.0f, 10.0f);
				MaskData.Offset = FMath::Clamp(MaskLayer.Offset, -1.0f, 1.0f);
				MaskData.bInvert = MaskLayer.bInvert;
				Data.bHasMask = true;
				continue;
			}

			if (LayerChild.Type == EMixtormatLayerChildType::Generated)
			{
				const FMixtormatGeneratedMask& GeneratedMask = LayerChild.Generated;
				if (!GeneratedMask.bEnabled || !GeneratedMask.HasAnySignal())
				{
					continue;
				}

				FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
				ChildData.Type = EMixtormatLayerChildType::Generated;
				ChildData.SourceChildIndex = SourceChildIndex;
				FGeneratedMaskRenderData& GeneratedData = ChildData.Generated;
				GeneratedData.CurvatureWeight = GeneratedMask.CurvatureWeight;
				GeneratedData.CurvatureBias = FMath::Clamp(GeneratedMask.CurvatureBias, 0.0f, 1.0f);
				GeneratedData.CurvatureStrength = FMath::Max(GeneratedMask.CurvatureStrength, 0.0f);
				GeneratedData.CurvaturePower = FMath::Max(GeneratedMask.CurvaturePower, 0.001f);
				GeneratedData.DirectionWeight = GeneratedMask.DirectionWeight;
				GeneratedData.DirectionAngle = GeneratedMask.DirectionAngle;
				GeneratedData.DirectionBroadness = FMath::Max(GeneratedMask.DirectionBroadness, 0.001f);
				GeneratedData.AOWeight = GeneratedMask.AOWeight;
				GeneratedData.HeightWeight = GeneratedMask.HeightWeight;
				GeneratedData.HeightBias = GeneratedMask.HeightBias;
				GeneratedData.bNormalizeWeights = GeneratedMask.bNormalizeWeights;
				GeneratedData.Broadness = FMath::Clamp(GeneratedMask.Broadness, 1, 32);
				GeneratedData.Smoothing = FMath::Clamp(GeneratedMask.Smoothing, 1, 4);
				GeneratedData.Bias = FMath::Clamp(GeneratedMask.Bias, 0.001f, 0.999f);
				GeneratedData.WarpAmount = FMath::Max(GeneratedMask.WarpAmount, 0.0f);
				GeneratedData.WarpSource = FMath::Clamp(GeneratedMask.WarpSource, 0.0f, 1.0f);
				GeneratedData.WarpRadius = FMath::Clamp(GeneratedMask.WarpRadius, 1, 16);
				GeneratedData.BlendMode = GeneratedMask.BlendMode;
				GeneratedData.Weight = FMath::Max(GeneratedMask.Weight, 0.0f);
				GeneratedData.Balance = FMath::Max(GeneratedMask.Balance, 0.0f);
				GeneratedData.Contrast = FMath::Max(GeneratedMask.Contrast, 0.0f);
				GeneratedData.Offset = GeneratedMask.Offset;
				GeneratedData.bInvert = GeneratedMask.bInvert;
				GeneratedData.RidgeWeight = GeneratedMask.RidgeWeight;
				Data.bHasMask = true;
				continue;
			}

			if (LayerChild.Type == EMixtormatLayerChildType::Craquelure)
			{
				const FMixtormatCraquelure& Craquelure = LayerChild.Craquelure;

				// No HasAnySignal() equivalent: this node has one signal and it is always on.
				// Weight 0 is how a craquelure node is muted, the same as any other mask.
				if (!Craquelure.bEnabled)
				{
					continue;
				}

				FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
				ChildData.Type = EMixtormatLayerChildType::Craquelure;
				ChildData.SourceChildIndex = SourceChildIndex;
				FCraquelureRenderData& CrackData = ChildData.Craquelure;

				// The clamps here guard the lattice, not taste: the period is a wrap modulus
				// and a non-positive one would divide the hash by zero.
				CrackData.Period = FMath::Max(Craquelure.Period, 1);
				CrackData.WarpPeriod = FMath::Max(Craquelure.WarpPeriod, 1);
				CrackData.Seed = static_cast<uint32>(FMath::Max(Craquelure.Seed, 0));
				CrackData.WarpSeed = static_cast<uint32>(FMath::Max(Craquelure.WarpSeed, 0));
				CrackData.Jitter = Craquelure.Jitter;
				CrackData.Width = Craquelure.Width;
				CrackData.Variation = Craquelure.Variation;
				CrackData.Warp = Craquelure.Warp;
				CrackData.BlendMode = Craquelure.BlendMode;
				CrackData.bInvert = Craquelure.bInvert;
				CrackData.Weight = Craquelure.Weight;
				CrackData.Balance = Craquelure.Balance;
				CrackData.Contrast = Craquelure.Contrast;
				CrackData.Offset = Craquelure.Offset;

				CrackData.Mode = Craquelure.Mode;
				CrackData.ReliefDepth = FMath::Max(Craquelure.ReliefDepth, 0.0f);
				CrackData.ReliefNormalStrength = FMath::Max(Craquelure.ReliefNormalStrength, 0.0f);
				CrackData.ReliefWidth = FMath::Max(Craquelure.ReliefWidth, 0.002f);
				CrackData.ReliefProfile = FMath::Clamp(Craquelure.ReliefProfile, 0.05f, 8.0f);
				CrackData.Iterations = FMath::Clamp(Craquelure.Iterations, 1, 1024);
				CrackData.SeedCells = FMath::Max(Craquelure.SeedCells, 1);
				CrackData.SeedChance = FMath::Clamp(Craquelure.SeedChance, 0.0f, 1.0f);
				CrackData.SeedJitter = FMath::Clamp(Craquelure.SeedJitter, 0.0f, 1.0f);
				CrackData.NoiseCells = FMath::Max(Craquelure.NoiseCells, 1);
				CrackData.StressVariation = Craquelure.StressVariation;
				CrackData.ToughnessVariation = Craquelure.ToughnessVariation;
				CrackData.Persistence = Craquelure.Persistence;
				CrackData.FlowStrength = FMath::Clamp(Craquelure.FlowStrength, 0.0f, 1.0f);
				CrackData.StressGain = Craquelure.StressGain;
				CrackData.ToughnessCost = Craquelure.ToughnessCost;
				CrackData.Irregularity = Craquelure.Irregularity;
				CrackData.GrowthThreshold = Craquelure.GrowthThreshold;
				CrackData.TurnResponse = FMath::Clamp(Craquelure.TurnResponse, 0.0f, 1.0f);
				CrackData.CollisionLimit = FMath::Clamp(Craquelure.CollisionLimit, 1, 8);

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
					Key = MixtormatNetworkKey::Add(Key, CrackData.Warp);
					Key = MixtormatNetworkKey::Add(Key, CrackData.WarpPeriod);
					Key = MixtormatNetworkKey::Add(Key, CrackData.WarpSeed);
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
				continue;
			}

			const FMixtormatLayerEffect& LayerEffect = LayerChild.Effect;
			if (!LayerEffect.bEnabled)
			{
				continue;
			}

			const UMixtormatEffect* EffectAsset = LayerEffect.Effect.LoadSynchronous();

			// Procedural effects carry no source maps and so have no asset to read a type
			// from. Asset-backed effects are unchanged.
			// Every Filter is procedural -- none of them read source maps -- and Peeling is
			// the one Surface effect with a procedural path.
			const bool bProcedural =
				!EffectAsset
				&& (MixtormatEffectClassOf(LayerEffect.ProceduralType) == EMixtormatEffectClass::Filter
					|| LayerEffect.ProceduralType == EMixtormatEffectType::Peeling);
			if (!EffectAsset && !bProcedural)
			{
				continue;
			}
			const EMixtormatEffectType ResolvedType =
				EffectAsset ? EffectAsset->EffectType : LayerEffect.ProceduralType;
			if (ResolvedType == EMixtormatEffectType::Peeling && EffectAsset
				&& (!EffectAsset->PeelData
					|| !EffectAsset->Mask
					|| !EffectAsset->Height
					|| !EffectAsset->SDF))
			{
				continue;
			}

			FChildRenderData& ChildData = Data.Children.AddDefaulted_GetRef();
			ChildData.Type = EMixtormatLayerChildType::Effect;
			ChildData.SourceChildIndex = SourceChildIndex;
			FEffectRenderData& EffectData = ChildData.Effect;
			EffectData.Type = ResolvedType;
			EffectData.Tiling = FMath::Max(1.0f, FMath::RoundToFloat(Layer.Tiling));
			EffectData.Strength = FMath::Clamp(LayerEffect.Strength, 0.0f, 1.0f);
			if (ResolvedType == EMixtormatEffectType::Erosion)
			{
				// Values pass through unclamped. The inspector constrains the scrub range
				// visually, but a typed value outside it stays intact all the way to the
				// shader, which keeps its own epsilon guards at the division sites.
				EffectData.ErosionAmount = LayerEffect.ErosionAmount;
				EffectData.ErosionStrength = LayerEffect.ErosionStrength;
				EffectData.ErosionOctaves = FMath::Clamp(LayerEffect.ErosionOctaves, 1, 12);
				EffectData.ErosionPeriod = FMath::Clamp(LayerEffect.ErosionPeriod, 1, 1024);
				EffectData.ErosionGain = LayerEffect.ErosionGain;
				EffectData.ErosionDetail = LayerEffect.ErosionDetail;
				EffectData.ErosionGullyWeight = LayerEffect.ErosionGullyWeight;
				EffectData.ErosionNormalization = LayerEffect.ErosionNormalization;
				EffectData.ErosionRidgeRounding = LayerEffect.ErosionRidgeRounding;
				EffectData.ErosionCreaseRounding = LayerEffect.ErosionCreaseRounding;
				EffectData.ErosionSlopeOnset = LayerEffect.ErosionSlopeOnset;
				EffectData.ErosionFeatureOnset = LayerEffect.ErosionFeatureOnset;
				EffectData.ErosionAssumedSlope = LayerEffect.ErosionAssumedSlope;
				EffectData.ErosionAssumedSlopeAmount = LayerEffect.ErosionAssumedSlopeAmount;
				EffectData.ErosionNormalStrength = LayerEffect.ErosionNormalStrength;
				EffectData.ErosionSlopeRadius = FMath::Clamp(LayerEffect.ErosionSlopeRadius, 1, 32);
				EffectData.ErosionSlopeBlur = LayerEffect.ErosionSlopeBlur;
				EffectData.ErosionCurvatureMode = static_cast<int32>(LayerEffect.ErosionCurvatureMode);
				EffectData.ErosionCavityInfluence = LayerEffect.ErosionCavityInfluence;
				EffectData.ErosionCavityOffset = LayerEffect.ErosionCavityOffset;
				EffectData.ErosionCavityRemapMin = LayerEffect.ErosionCavityRemapMin;
				EffectData.ErosionCavityRemapMax = LayerEffect.ErosionCavityRemapMax;
				EffectData.ErosionHeightInfluence = LayerEffect.ErosionHeightInfluence;
				EffectData.ErosionHeightScale = LayerEffect.ErosionHeightScale;
				EffectData.ErosionMaskTiling = FMath::Max(1.0f, static_cast<float>(LayerEffect.ErosionMaskTiling));
				EffectData.bErosionInvertMask = LayerEffect.bErosionInvertMask;
				{
					UTexture2D* PlacementMask = LayerEffect.ErosionMaskTexture.LoadSynchronous();
					if (!PlacementMask)
					{
						if (const UMixtormatMask* MaskAsset = LayerEffect.ErosionMask.LoadSynchronous())
						{
							PlacementMask = MaskAsset->MaskTexture.Get();
						}
					}
					if (PlacementMask)
					{
						EffectData.ErosionPlacementMask = GetTextureRHI(PlacementMask);
					}
				}

				EffectData.ErosionColor = LayerEffect.ErosionColor;
				EffectData.ErosionColorAmount = LayerEffect.ErosionColorAmount;
				EffectData.ErosionRoughnessAmount = LayerEffect.ErosionRoughnessAmount;
				EffectData.ErosionCarveDepth = LayerEffect.ErosionCarveDepth;
			}

			if (ResolvedType == EMixtormatEffectType::Grade)
			{
				EffectData.GradeAmount = LayerEffect.GradeAmount;
				EffectData.GradeTonemap = static_cast<int32>(LayerEffect.GradeTonemap);
				EffectData.GradeTonemapStrength =
					FMath::Clamp(LayerEffect.GradeTonemapStrength, 0.0f, 1.0f);
				EffectData.GradeBrightness = LayerEffect.GradeBrightness;
				EffectData.GradeContrast = LayerEffect.GradeContrast;
				EffectData.GradeContrastPivot = LayerEffect.GradeContrastPivot;
				EffectData.GradeGamma = LayerEffect.GradeGamma;
				EffectData.bGradeInvertMask = LayerEffect.bGradeInvertMask;
			}

			if (ResolvedType == EMixtormatEffectType::Chipping)
			{
				EffectData.ChipAmount = LayerEffect.ChipAmount;
				EffectData.ChipGroutLevel = LayerEffect.ChipGroutLevel;
				EffectData.ChipGroutSoftness = LayerEffect.ChipGroutSoftness;
				EffectData.ChipSize = LayerEffect.ChipSize;
				EffectData.ChipDepth = LayerEffect.ChipDepth;
				EffectData.ChipIrregularity = LayerEffect.ChipIrregularity;
				EffectData.ChipIterations = FMath::Clamp(LayerEffect.ChipIterations, 1, 32);
				EffectData.ChipMaskEdge = LayerEffect.ChipMaskEdge;
				EffectData.ChipNormalStrength = LayerEffect.ChipNormalStrength;
				EffectData.ChipCavityInfluence = LayerEffect.ChipCavityInfluence;
				EffectData.ChipCavityOffset = LayerEffect.ChipCavityOffset;
				EffectData.ChipCavityRemapMin = LayerEffect.ChipCavityRemapMin;
				EffectData.ChipCavityRemapMax = LayerEffect.ChipCavityRemapMax;
				EffectData.ChipHeightInfluence = LayerEffect.ChipHeightInfluence;
				EffectData.ChipHeightScale = LayerEffect.ChipHeightScale;
				EffectData.ChipMaskTiling = FMath::Max(1.0f, static_cast<float>(LayerEffect.ChipMaskTiling));
				EffectData.bChipInvertMask = LayerEffect.bChipInvertMask;
				{
					UTexture2D* PlacementMask = LayerEffect.ChipMaskTexture.LoadSynchronous();
					if (!PlacementMask)
					{
						if (const UMixtormatMask* MaskAsset = LayerEffect.ChipMask.LoadSynchronous())
						{
							PlacementMask = MaskAsset->MaskTexture.Get();
						}
					}
					if (PlacementMask)
					{
						EffectData.ChipPlacementMask = GetTextureRHI(PlacementMask);
					}
				}
				EffectData.ChipSeed = static_cast<uint32>(FMath::Max(LayerEffect.ChipSeed, 0));
				EffectData.ChipColor = LayerEffect.ChipColor;
				EffectData.ChipColorAmount = LayerEffect.ChipColorAmount;
				EffectData.ChipRoughnessAmount = LayerEffect.ChipRoughnessAmount;
			}

			// Filters have nothing further to gather. bHasEffects is deliberately not set for
			// them: a Filter never writes the effect data target, so flagging it would make
			// the composite sample a buffer nothing wrote.
			//
			// Gated on the class, not on the absence of an asset. Erosion got away with the
			// narrower test because nothing creates Erosion assets, but Grade is a valid
			// EffectType on UMixtormatEffect, so an authored Grade asset would fall through
			// into the peel branches below and trip exactly the failure above.
			if (MixtormatEffectClassOf(ResolvedType) == EMixtormatEffectClass::Filter)
			{
				continue;
			}

			// A procedural peel does write effect data, so it is gathered here — before the
			// asset-backed branches below, all of which dereference EffectAsset.
			if (!EffectAsset)
			{
				EffectData.bProceduralPeel = true;
				EffectData.PeelType = static_cast<int32>(LayerEffect.PeelType);
				EffectData.PeelMacroPeriod = LayerEffect.PeelMacroPeriod;
				EffectData.PeelMicroPeriod = LayerEffect.PeelMicroPeriod;
				EffectData.PeelRandomSeed = static_cast<uint32>(FMath::Max(LayerEffect.PeelRandomSeed, 1));
				EffectData.PeelSeedThreshold = LayerEffect.PeelSeedThreshold;
				EffectData.PeelSeedNoiseWeight = LayerEffect.PeelSeedNoiseWeight;
				EffectData.PeelSeedCurvatureWeight = LayerEffect.PeelSeedCurvatureWeight;
				EffectData.PeelSeedCurvatureBias = LayerEffect.PeelSeedCurvatureBias;
				EffectData.PeelSeedAOWeight = LayerEffect.PeelSeedAOWeight;
				EffectData.PeelSeedHeightWeight = LayerEffect.PeelSeedHeightWeight;
				EffectData.PeelSeedMaskWeight = LayerEffect.PeelSeedMaskWeight;
				EffectData.bPeelNormalizeSeedWeights = LayerEffect.bPeelNormalizeSeedWeights;
				EffectData.PeelCurvatureRadius = LayerEffect.PeelCurvatureRadius;
				EffectData.PeelGrowthStrength = LayerEffect.PeelGrowthStrength;
				EffectData.PeelAOStrength = LayerEffect.PeelAOStrength;
				EffectData.PeelEdgeSharpness = LayerEffect.PeelEdgeSharpness;
				EffectData.PeelLiftVariation = LayerEffect.PeelLiftVariation;
				EffectData.PeelSizeVariation = LayerEffect.PeelSizeVariation;
				EffectData.PeelClusterPeriod = LayerEffect.PeelClusterPeriod;
				EffectData.PeelSolveDivisor = LayerEffect.PeelSolveDivisor;
				EffectData.PeelMaskTiling = FMath::Max(1.0f, static_cast<float>(LayerEffect.PeelMaskTiling));
				EffectData.bPeelMaskInvert = LayerEffect.bPeelMaskInvert;
				{
					// Direct texture wins over the asset, matching how mask children resolve.
					UTexture2D* OwnMask = LayerEffect.PeelMaskTexture.LoadSynchronous();
					if (!OwnMask)
					{
						if (const UMixtormatMask* MaskAsset = LayerEffect.PeelMask.LoadSynchronous())
						{
							OwnMask = MaskAsset->MaskTexture.Get();
						}
					}
					if (OwnMask)
					{
						EffectData.PeelOwnMask = GetTextureRHI(OwnMask);
					}
				}
				EffectData.PeelClusterAmount = LayerEffect.PeelClusterAmount;
				EffectData.PeelWarpPeriod = LayerEffect.PeelWarpPeriod;
				EffectData.PeelWarpAmount = LayerEffect.PeelWarpAmount;
				EffectData.PeelWarpSource = LayerEffect.PeelWarpSource;
				EffectData.Front = LayerEffect.Front;
				EffectData.Width = FMath::Max(LayerEffect.Width, 1.0e-6f);
				EffectData.MacroWarp = LayerEffect.MacroWarp;
				EffectData.MicroWarp = LayerEffect.MicroWarp;
				EffectData.MicroMorph = FMath::Clamp(LayerEffect.MicroMorph, 0.0f, 1.0f);
				EffectData.Thickness = FMath::Max(LayerEffect.Thickness, 0.0f);
				EffectData.Lift = FMath::Max(LayerEffect.Lift, 0.0f);
				EffectData.DetailStrength = FMath::Max(LayerEffect.DetailStrength, 0.0f);
				EffectData.PeelHeightAmount = LayerEffect.PeelHeightAmount;
				EffectData.bPeelHeightInvert = LayerEffect.bPeelHeightInvert;
				// No 8-bit round trip on a transient float target, so nothing to decode.
				EffectData.DistanceRange = 1.0f;
				EffectData.SDFRange = 1.0f;
				EffectData.HeightRange = 1.0f;
				Data.bHasEffects = true;
				continue;
			}
			if (EffectAsset->EffectType == EMixtormatEffectType::Stain)
			{
				EffectData.StainColor = LayerEffect.StainColor;
				EffectData.StainRoughness = LayerEffect.StainRoughness;
				EffectData.StainHeightInfluence = LayerEffect.StainHeightInfluence;
				EffectData.StainHeightWarp = LayerEffect.StainHeightWarp;
				EffectData.StainHeightBias = LayerEffect.StainHeightBias;
				EffectData.StainHeightContrast = FMath::Max(LayerEffect.StainHeightContrast, 0.01f);
				EffectData.StainFlowAmount = LayerEffect.StainFlowAmount;
				EffectData.StainGravity = LayerEffect.StainGravity;
				EffectData.StainFlowRadius = FMath::Clamp(LayerEffect.StainFlowRadius, 1, 64);
				// Floors at 1 for the same reason the erosion pair does.
				EffectData.StainFlowSmoothing = FMath::Clamp(LayerEffect.StainFlowSmoothing, 1, 16);
				Data.bHasStain = true;
				continue;
			}


			EffectData.PeelData = GetTextureRHI(EffectAsset->PeelData.Get());
			EffectData.Mask = GetTextureRHI(EffectAsset->Mask.Get());
			EffectData.Height = GetTextureRHI(EffectAsset->Height.Get());
			EffectData.SDF = GetTextureRHI(EffectAsset->SDF.Get());
			if (!EffectData.PeelData.IsValid()
				|| !EffectData.Mask.IsValid()
				|| !EffectData.Height.IsValid()
				|| !EffectData.SDF.IsValid())
			{
				return false;
			}
			EffectData.Front = LayerEffect.Front;
			EffectData.Width = FMath::Max(LayerEffect.Width, 1.0e-6f);
			EffectData.MacroWarp = LayerEffect.MacroWarp;
			EffectData.MicroWarp = LayerEffect.MicroWarp;
			EffectData.MicroMorph = FMath::Clamp(LayerEffect.MicroMorph, 0.0f, 1.0f);
			EffectData.Thickness = FMath::Max(LayerEffect.Thickness, 0.0f);
			EffectData.Lift = FMath::Max(LayerEffect.Lift, 0.0f);
			EffectData.DetailStrength = FMath::Max(LayerEffect.DetailStrength, 0.0f);
			EffectData.PeelHeightAmount = LayerEffect.PeelHeightAmount;
			EffectData.bPeelHeightInvert = LayerEffect.bPeelHeightInvert;
			EffectData.DistanceRange = FMath::Max(EffectAsset->DistanceRange, 1.0e-6f);
			EffectData.SDFRange = FMath::Max(EffectAsset->SDFRange, 1.0e-6f);
			EffectData.HeightRange = FMath::Max(EffectAsset->HeightRange, 1.0e-6f);
			Data.bHasEffects = true;
		}

		Data.Opacity = Layer.Opacity;
		Data.Tiling = FMath::Max(1.0f, FMath::RoundToFloat(Layer.Tiling));

		// Integer, because the compositor wraps every source read in a frac() and a
		// fractional scale lands mid-cell at the wrap. Offset and flip are unclamped:
		// translating and mirroring a periodic function leave it periodic.
		Data.UVScaleX = FMath::Clamp(Layer.UVScaleX, 1, 16);
		Data.UVScaleY = FMath::Clamp(Layer.UVScaleY, 1, 16);
		Data.UVOffset = FVector2f(Layer.UVOffsetX, Layer.UVOffsetY);
		Data.Rotation = static_cast<int32>(Layer.Rotation);
		Data.bFlipU = Layer.bFlipU;
		Data.bFlipV = Layer.bFlipV;
		Data.NormalIntensity = Layer.NormalIntensity;
		Data.HueShift = FMath::Clamp(Layer.HueShift, -180.0f, 180.0f);
		Data.Saturation = FMath::Clamp(Layer.Saturation, 0.0f, 2.0f);
		Data.Value = FMath::Clamp(Layer.Value, 0.0f, 2.0f);
		Data.RoughnessBias = Layer.RoughnessBias;
		Data.RoughnessContrast = Layer.RoughnessContrast;
		Data.RoughnessOffset = Layer.RoughnessOffset;
		Data.FillRoughness = Layer.Roughness;
		Data.FillMetallic = Layer.Metallic;
		const float SourceIOR = Surface ? Surface->DefaultIOR : 1.5f;
		const float LayerIOR = FMath::Max(
			1.0f,
			Layer.bOverrideIOR ? Layer.IOR : SourceIOR);
		Data.LayerF0 = FMath::Square((LayerIOR - 1.0f) / (LayerIOR + 1.0f));
		Data.BaseColorInfluence = FMath::Clamp(Layer.BaseColorInfluence, 0.0f, 1.0f);
		Data.RoughnessInfluence = FMath::Clamp(Layer.RoughnessInfluence, 0.0f, 1.0f);
		Data.AOInfluence = FMath::Clamp(Layer.AOInfluence, 0.0f, 1.0f);
		Data.MetallicInfluence = FMath::Clamp(Layer.MetallicInfluence, 0.0f, 1.0f);
		Data.F0Influence = FMath::Clamp(Layer.F0Influence, 0.0f, 1.0f);
		Data.NormalInfluence = FMath::Clamp(Layer.NormalInfluence, 0.0f, 1.0f);
		Data.HeightInfluence = FMath::Clamp(Layer.HeightInfluence, 0.0f, 1.0f);
		Data.HeightBlendAmount = FMath::Clamp(Layer.HeightBlendAmount, 0.0f, 4.0f);
		Data.HeightThreshold = FMath::Clamp(Layer.HeightThreshold, 0.0f, 1.0f);
		Data.HeightRange = FMath::Max(Layer.HeightRange, 1.0e-6f);
		Data.HeightContrast = FMath::Max(Layer.HeightContrast, 0.01f);
		Data.HeightOffset = FMath::Clamp(Layer.HeightOffset, -1.0f, 1.0f);
		Data.HeightBias = FMath::Clamp(Layer.HeightBias, -1.0f, 1.0f);
		Data.ConstantHeight = FMath::Clamp(Layer.ConstantHeight, 0.0f, 1.0f);
		Data.MaskHeightInfluence = FMath::Clamp(Layer.MaskHeightInfluence, 0.0f, 1.0f);
		Data.HeightContactAOAmount = FMath::Clamp(Layer.HeightContactAOAmount, 0.0f, 1.0f);
		Data.HeightContactAOWidth = FMath::Max(Layer.HeightContactAOWidth, 1.0e-4f);
		Data.HeightBorderLift = FMath::Clamp(Layer.HeightBorderLift, -1.0f, 1.0f);
		Data.HeightBorderWidth = FMath::Max(Layer.HeightBorderWidth, 1.0e-4f);
		Data.HeightBorderNormalStrength = FMath::Max(Layer.HeightBorderNormalStrength, 0.0f);
		Data.FeatureInfluence = FMath::Clamp(Layer.FeatureInfluence, 0.0f, 1.0f);
		Data.FeatureBias = FMath::Clamp(Layer.FeatureBias, 0.0f, 1.0f);
		Data.HeightFeatureInfluence = FMath::Clamp(Layer.HeightFeatureInfluence, 0.0f, 1.0f);
		Data.AOFeatureInfluence = FMath::Clamp(Layer.AOFeatureInfluence, 0.0f, 1.0f);
		Data.CurvatureRadius = Layer.CurvatureRadius;
		Data.CurvatureSmoothing = FMath::Clamp(Layer.CurvatureSmoothing, 1, 4);
		Data.CurvatureStrength = Layer.CurvatureStrength;
		Data.CurvaturePower = Layer.CurvaturePower;
		Data.bEnabled = Layer.bEnabled;
		Data.bHeightBlendEnabled = Layer.bHeightBlendEnabled;
		Data.bHasPackedHeight = Surface && Surface->bHasBlendHeight;
		Data.bInvertHeight = Layer.bInvertHeight;
		Data.bDirectHeightComparison = !bNormalOnly;
		Data.bInvertHeightFeature = Layer.bInvertHeightFeature;
		Data.bInvertAOFeature = Layer.bInvertAOFeature;
		Data.bInvertFeature = Layer.bInvertFeature;
		Data.HeightReferenceLayerIndex = Layer.HeightReferenceLayerIndex >= 0
			&& Layer.HeightReferenceLayerIndex < LayerIndex
			? Layer.HeightReferenceLayerIndex
			: INDEX_NONE;
		switch (Layer.HeightSource)
		{
		case EMixtormatHeightSource::Automatic:
			Data.HeightSource = Data.bHasPackedHeight ? 0u : (Data.bHasMask ? 1u : 2u);
			break;
		case EMixtormatHeightSource::CombinedMask:
			Data.HeightSource = Data.bHasMask ? 1u : 2u;
			break;
		case EMixtormatHeightSource::Constant:
			Data.HeightSource = 2u;
			break;
		case EMixtormatHeightSource::RAMHAlpha:
		case EMixtormatHeightSource::LayerHeight:
		default:
			Data.HeightSource = Data.bHasPackedHeight ? 0u : 2u;
			break;
		}
		Data.bOverrideBaseColor = Layer.bOverrideBaseColor;
		Data.bOverrideRoughness = Layer.bOverrideRoughness;
		Data.bOverrideMetallic = Layer.bOverrideMetallic;
		Data.bCoat = Layer.CompositionMode == EMixtormatCompositionMode::Coat;
		Data.bFill = Layer.Type == EMixtormatLayerType::Fill;
		Data.bHasSurface = Surface
			&& Surface->BaseColor
			&& Surface->Normal
			&& Surface->RoughnessAOMetallic;
		Data.bHasNormal = LayerNormal != nullptr;
		Data.bNormalOnly = bNormalOnly;
		Data.bOverrideNormal = Layer.NormalBlendMode == EMixtormatNormalBlendMode::Override;
		Data.bFlipNormalY = Layer.bFlipNormalY;
	}

	PublishedTargetIndex = Request.Layers.IsEmpty()
		? 0
		: (Request.Layers.Num() - 1) & 1;
	Request.PublishedTargetIndex = PublishedTargetIndex;
	Request.NetworkCache = NetworkCache;

	ENQUEUE_RENDER_COMMAND(MixtormatComposite)(
		[Request = MoveTemp(Request)](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);
			TMap<FRHITexture*, FRDGTextureRef> RegisteredTextures;
			FRDGTextureRef OutputBC[2];
			FRDGTextureRef OutputN[2];
			FRDGTextureRef OutputRAM[2];
			FRDGTextureRef OutputHeight[2];
			FRDGTextureRef OutputDebug[2];
			for (int32 Index = 0; Index < 2; ++Index)
			{
				OutputBC[Index] = RegisterTexture(
					GraphBuilder,
					RegisteredTextures,
					Request.OutputBC[Index],
					TEXT("Mixtormat.OutputBC"));
				OutputN[Index] = RegisterTexture(
					GraphBuilder,
					RegisteredTextures,
					Request.OutputN[Index],
					TEXT("Mixtormat.OutputN"));
				OutputRAM[Index] = RegisterTexture(
					GraphBuilder,
					RegisteredTextures,
					Request.OutputRAM[Index],
					TEXT("Mixtormat.OutputRAM"));
				OutputHeight[Index] = RegisterTexture(
					GraphBuilder,
					RegisteredTextures,
					Request.OutputHeight[Index],
					TEXT("Mixtormat.OutputHeight"));
				OutputDebug[Index] = RegisterTexture(
					GraphBuilder,
					RegisteredTextures,
					Request.OutputDebug[Index],
					TEXT("Mixtormat.OutputDebug"));
			}

			AddClearUAVPass(
				GraphBuilder,
				GraphBuilder.CreateUAV(OutputDebug[Request.PublishedTargetIndex]),
				FVector4f(0.08f, 0.02f, 0.12f, 1.0f));

			if (Request.Layers.IsEmpty())
			{
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutputBC[0]), MixtormatSubstrate::BaseColor);
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutputN[0]), MixtormatSubstrate::Normal);
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutputRAM[0]), MixtormatSubstrate::PackedRAM);
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutputHeight[0]), MixtormatSubstrate::Height);
			}
			else
			{
				const FRDGTextureDesc MaskDesc = FRDGTextureDesc::Create2D(
					Request.Resolution,
					PF_R16F,
					FClearValueBinding::White,
					TexCreate_ShaderResource | TexCreate_UAV);
				FRDGTextureRef MaskTargets[2] =
				{
					GraphBuilder.CreateTexture(MaskDesc, TEXT("Mixtormat.MaskA")),
					GraphBuilder.CreateTexture(MaskDesc, TEXT("Mixtormat.MaskB"))
				};
				// Both halves cleared before anything reads either. The first mask child on a
				// layer binds the half it is not writing as PreviousMask and ignores the value --
				// Initialize makes it treat Previous as zero -- but RDG validates the binding
				// rather than the use, and a transient nothing has written trips
				// "has a read dependency on Mixtormat.MaskB, but it was never written to" on
				// every composite. The ensure fires once per session and is easy to never see;
				// the read itself was always harmless, and this makes the graph honest about it.
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(MaskTargets[0]), FVector4f(0.0f));
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(MaskTargets[1]), FVector4f(0.0f));

				// The substrate, seeded into the half the bottom layer reads.
				//
				// Layer 0 composites onto this the way every other layer composites onto the
				// layer below it, which is the whole point: it used to seed these buffers
				// itself via Initialize, and a seeding layer ignores its own mask, feature
				// influence and height blend. That made position 0 a different thing to be,
				// so a layer could not be dragged to the bottom without changing what it did.
				//
				// Parity matters here and is easy to get backwards. WriteIndex is
				// LayerIndex & 1, so layer 0 writes half 0 and reads half 1 -- the substrate
				// belongs in half 1. Half 0 needs no seed; layer 0 overwrites it.
				FRDGTextureRef* HeightTargets = OutputHeight;
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutputBC[1]), MixtormatSubstrate::BaseColor);
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutputN[1]), MixtormatSubstrate::Normal);
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(OutputRAM[1]), MixtormatSubstrate::PackedRAM);
				AddClearUAVPass(
					GraphBuilder,
					GraphBuilder.CreateUAV(HeightTargets[0]),
					MixtormatSubstrate::Height);
				AddClearUAVPass(
					GraphBuilder,
					GraphBuilder.CreateUAV(HeightTargets[1]),
					MixtormatSubstrate::Height);

				// Drainage and crest lines, produced by the erosion filter and consumed by
				// generated masks on later layers.
				//
				// Ping-ponged on the layer index like every other surface input a generated
				// mask reads, so a mask on layer N sees what was accumulated below layer N.
				// A single shared target would be read-after-write across layers with no
				// versioning, and a mask would see its own layer's ridge on some layers and
				// not others depending on child order.
				//
				// That means every layer has to write it, not only eroding ones: a layer that
				// left its slot alone would hand the next layer the ridge from two layers
				// back. Layers without erosion copy read to write below.
				FRDGTextureRef RidgeTargets[2] =
				{
					GraphBuilder.CreateTexture(MaskDesc, TEXT("Mixtormat.RidgeA")),
					GraphBuilder.CreateTexture(MaskDesc, TEXT("Mixtormat.RidgeB"))
				};
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(RidgeTargets[0]), FVector4f(0.0f));
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(RidgeTargets[1]), FVector4f(0.0f));

				const FRDGTextureDesc EffectDesc = FRDGTextureDesc::Create2D(
					Request.Resolution,
					PF_FloatRGBA,
					FClearValueBinding::White,
					TexCreate_ShaderResource | TexCreate_UAV);
				FRDGTextureRef EffectTargets[2] =
				{
					GraphBuilder.CreateTexture(EffectDesc, TEXT("Mixtormat.EffectA")),
					GraphBuilder.CreateTexture(EffectDesc, TEXT("Mixtormat.EffectB"))
				};

				// Cleared for the same reason the mask pair is: the first surface effect on a
				// layer binds the half it is not writing and ignores it, and RDG validates the
				// binding rather than the use. White, matching the desc's own clear value and the
				// neutral coverage the effect shader expects to read.
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(EffectTargets[0]), FVector4f(1.0f));
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(EffectTargets[1]), FVector4f(1.0f));
				// Peel relief, signed around zero so stacked peels accumulate.
				const FRDGTextureDesc EffectHeightDesc = FRDGTextureDesc::Create2D(
					Request.Resolution,
					PF_R16F,
					FClearValueBinding::Black,
					TexCreate_ShaderResource | TexCreate_UAV);
				FRDGTextureRef EffectHeightTargets[2] =
				{
					GraphBuilder.CreateTexture(EffectHeightDesc, TEXT("Mixtormat.EffectHeightA")),
					GraphBuilder.CreateTexture(EffectHeightDesc, TEXT("Mixtormat.EffectHeightB"))
				};
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(EffectHeightTargets[0]), FVector4f(0.0f));
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(EffectHeightTargets[1]), FVector4f(0.0f));
				FRDGTextureRef StainTargets[2] =
				{
					GraphBuilder.CreateTexture(EffectDesc, TEXT("Mixtormat.StainA")),
					GraphBuilder.CreateTexture(EffectDesc, TEXT("Mixtormat.StainB"))
				};
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(StainTargets[0]), FVector4f(1.0f, 1.0f, 1.0f, 0.0f));
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(StainTargets[1]), FVector4f(1.0f, 1.0f, 1.0f, 0.0f));
				TShaderMapRef<FMixtormatMaskCS> MaskShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				TShaderMapRef<FMixtormatGeneratedMaskCS> GeneratedMaskShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				TShaderMapRef<FMixtormatCraquelureCS> CraquelureShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				TShaderMapRef<FMixtormatColorIdCS> ColorIdShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				TShaderMapRef<FMixtormatCraquelureSeedCS> CraquelureSeedShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				TShaderMapRef<FMixtormatCraquelureGrowCS> CraquelureGrowShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				TShaderMapRef<FMixtormatCraquelureResolveCS> CraquelureResolveShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				TShaderMapRef<FMixtormatCraquelureReliefCS> CraquelureReliefShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				TShaderMapRef<FMixtormatCraquelureDistanceSeedCS> CraqDistanceSeedShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				TShaderMapRef<FMixtormatCraquelureDistanceStepCS> CraqDistanceStepShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				TShaderMapRef<FMixtormatCraquelureDistanceResolveCS> CraqDistanceResolveShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				TShaderMapRef<FMixtormatErosionCS> ErosionShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				TShaderMapRef<FMixtormatCarveShadeCS> CarveShadeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				TShaderMapRef<FMixtormatChippingCS> ChippingShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				TShaderMapRef<FMixtormatReduceMinMaxCS> ReduceMinMaxShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				TShaderMapRef<FMixtormatGradeCS> GradeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				TShaderMapRef<FMixtormatPeelingCS> PeelingShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				TShaderMapRef<FMixtormatPeelFieldCS> PeelFieldShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

				// Placeholders so the peel and peel-field parameter structs always have a
				// bound resource in slots the active mode does not use. Never read, never
				// written; a 1x1 keeps them free.
				const FRDGTextureDesc TinyRGDesc = FRDGTextureDesc::Create2D(
					FIntPoint(1, 1), PF_FloatRGBA, FClearValueBinding::Black,
					TexCreate_ShaderResource | TexCreate_UAV);
				const FRDGTextureDesc TinyRGBADesc = FRDGTextureDesc::Create2D(
					FIntPoint(1, 1), PF_FloatRGBA, FClearValueBinding::Black,
					TexCreate_ShaderResource | TexCreate_UAV);
				FRDGTextureRef PeelNoiseDummy =
					GraphBuilder.CreateTexture(TinyRGDesc, TEXT("Mixtormat.PeelNoiseDummy"));
				FRDGTextureRef PeelFieldDummy =
					GraphBuilder.CreateTexture(TinyRGBADesc, TEXT("Mixtormat.PeelFieldDummy"));

				// Bound wherever a peel input is absent -- the peel's own mask when the layer
				// uses its child mask, and the authored map slots on the procedural path. RDG
				// rejects a pass that reads a transient texture nothing has written, which the
				// log reported as an error on every procedural peel composite, so it is cleared
				// once here rather than left undefined.
				AddClearUAVPass(
					GraphBuilder,
					GraphBuilder.CreateUAV(PeelFieldDummy),
					FVector4f(0.0f, 0.0f, 0.0f, 0.0f));
				AddClearUAVPass(
					GraphBuilder,
					GraphBuilder.CreateUAV(PeelNoiseDummy),
					FVector4f(0.0f, 0.0f, 0.0f, 0.0f));
				TShaderMapRef<FMixtormatStainCS> StainShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				TShaderMapRef<FMixtormatFlowCS> FlowShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

				// Bound on every erosion and stain dispatch that is not using flow. The slot
				// is an SRV sampled by UV, so a 1x1 costs nothing and does not have to match
				// the dispatch size the way the resample UAV does. Cleared for the same reason
				// PeelFieldDummy is: RDG rejects a read of a transient texture nothing wrote.
				FRDGTextureRef FlowDummy = GraphBuilder.CreateTexture(
					FRDGTextureDesc::Create2D(
						FIntPoint(1, 1), PF_G16R16F, FClearValueBinding::Black,
						TexCreate_ShaderResource | TexCreate_UAV),
					TEXT("Mixtormat.FlowDummy"));
				AddClearUAVPass(
					GraphBuilder,
					GraphBuilder.CreateUAV(FlowDummy),
					FVector4f(0.0f, 0.0f, 0.0f, 0.0f));

				// Builds an orientation field from a height texture and smooths it into a
				// coherent one. Shared by erosion and stain: they differ in resolution, in
				// which height they read and in where they sit in the graph, so what is worth
				// sharing is this construction rather than a texture.
				auto AddFlowField = [&GraphBuilder, FlowShader, FlowDummy](
					FRDGTextureRef Height,
					const FIntPoint Res,
					const int32 FlowRadius,
					const int32 Smoothing,
					const TCHAR* DebugName) -> FRDGTextureRef
				{
					const FRDGTextureDesc FlowDesc = FRDGTextureDesc::Create2D(
						Res, PF_G16R16F, FClearValueBinding::Black,
						TexCreate_ShaderResource | TexCreate_UAV);
					FRDGTextureRef Flow[2] = {
						GraphBuilder.CreateTexture(FlowDesc, TEXT("Mixtormat.FlowA")),
						GraphBuilder.CreateTexture(FlowDesc, TEXT("Mixtormat.FlowB"))};

					const FIntVector Groups(
						FMath::DivideAndRoundUp(Res.X, 8),
						FMath::DivideAndRoundUp(Res.Y, 8),
						1);

					int32 Slot = 0;
					for (int32 Step = 0; Step <= Smoothing; ++Step)
					{
						const bool bBuild = Step == 0;
						FMixtormatFlowCS::FParameters* FP =
							GraphBuilder.AllocParameters<FMixtormatFlowCS::FParameters>();
						FP->OutputSize = Res;
						FP->Mode = bBuild ? 0 : 1;
						FP->Radius = FlowRadius;
						FP->SourceHeight = Height;
						// The build pass never reads PreviousFlow, but the slot still has to
						// carry a texture something wrote. The cleared 1x1 serves: it is an
						// SRV, so it does not have to match the dispatch size, and binding the
						// unwritten ping-pong half here is exactly what RDG rejects.
						FP->PreviousFlow = bBuild ? FlowDummy : Flow[Slot];
						FP->LinearWrapSampler =
							TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
						FP->OutputFlow = GraphBuilder.CreateUAV(Flow[bBuild ? 0 : 1 - Slot]);
						FComputeShaderUtils::AddPass(
							GraphBuilder,
							RDG_EVENT_NAME("Mixtormat.Flow.%s.%s%d", DebugName, bBuild ? TEXT("Build") : TEXT("Smooth"), Step),
							FlowShader,
							FP,
							Groups);
						Slot = bBuild ? 0 : 1 - Slot;
					}
					return Flow[Slot];
				};

				TShaderMapRef<FMixtormatCompositeCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
				TSet<int32> RequiredHeightSnapshots;
				for (int32 LayerIndex = 0; LayerIndex < Request.Layers.Num(); ++LayerIndex)
				{
					const int32 ReferenceIndex = Request.Layers[LayerIndex].HeightReferenceLayerIndex;
					if (ReferenceIndex >= 0 && ReferenceIndex < LayerIndex)
					{
						RequiredHeightSnapshots.Add(ReferenceIndex);
					}
				}
				TMap<int32, FRDGTextureRef> HeightSnapshots;
				for (int32 LayerIndex = 0; LayerIndex < Request.Layers.Num(); ++LayerIndex)
				{
					const FLayerRenderData& Layer = Request.Layers[LayerIndex];
					FRDGTextureRef CombinedMask = RegisterTexture(
						GraphBuilder,
						RegisteredTextures,
						Layer.Mask,
						TEXT("Mixtormat.WhiteMask"));
					FRDGTextureRef CombinedEffectData = RegisterTexture(
						GraphBuilder,
						RegisteredTextures,
						Layer.BaseColor,
						TEXT("Mixtormat.DefaultEffectData"));
					FRDGTextureRef CombinedEffectHeight = EffectHeightTargets[0];
					FRDGTextureRef CombinedStainData = RegisterTexture(
						GraphBuilder,
						RegisteredTextures,
						Layer.BaseColor,
						TEXT("Mixtormat.DefaultStainData"));
					FRDGTextureRef DebugMask = CombinedMask;
					// Height the owning layer composites against. An erosion filter replaces it
					// with its carved result so the reshaped height also drives the blend mask,
					// contact AO and border normals rather than only the displacement output.
					const FEffectRenderData* PendingErosion = nullptr;
					const FEffectRenderData* PendingChipping = nullptr;

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
					};
					TArray<FPendingCraquelureRelief, TInlineAllocator<2>> PendingCraquelureReliefs;

					// An array where erosion keeps a single pointer. Two erosions on one layer
					// is nonsense, but a brightness grade and a separate tonemap grade is an
					// ordinary way to use an adjustment layer, and dropping all but the last
					// would read as a bug rather than as a contract.
					TArray<const FEffectRenderData*, TInlineAllocator<2>> PendingGrades;
					int32 MaskPassIndex = 0;
					int32 EffectPassIndex = 0;
					int32 StainPassIndex = 0;
					for (int32 ChildIndex = 0; ChildIndex < Layer.Children.Num(); ++ChildIndex)
					{
						const FChildRenderData& Child = Layer.Children[ChildIndex];
						if (Child.Type == EMixtormatLayerChildType::Generated)
						{
							// Generated masks read the surface accumulated below this layer,
							// which is the same ping-pong slot the layer composite reads.
							const int32 LayerReadIndex = 1 - (LayerIndex & 1);
							const FGeneratedMaskRenderData& Generated = Child.Generated;
							// Weight 0 makes the whole node the identity: every mask shader
							// ends on saturate(lerp(Previous, Result, Weight)), and the masks it
							// reads are already saturated, so the output is the input bit for
							// bit. Skipping is only exact from the second mask child onward --
							// the first establishes the chain with Initialize, where Previous is
							// zero rather than what the layer already had, and a skip there
							// would leave a different mask behind rather than the same one.
							if (MaskPassIndex > 0 && Generated.Weight == 0.0f)
							{
								continue;
							}

							const int32 MaskWriteIndex = MaskPassIndex & 1;
							const int32 MaskReadIndex = 1 - MaskWriteIndex;
							FMixtormatGeneratedMaskCS::FParameters* GeneratedParameters =
								GraphBuilder.AllocParameters<FMixtormatGeneratedMaskCS::FParameters>();
							GeneratedParameters->OutputSize = Request.Resolution;
							GeneratedParameters->Initialize = MaskPassIndex == 0 ? 1u : 0u;
							GeneratedParameters->SurfaceValid = LayerIndex > 0 ? 1u : 0u;
							GeneratedParameters->FlipNormalY = Layer.bFlipNormalY ? 1u : 0u;
							GeneratedParameters->CurvatureWeight = Generated.CurvatureWeight;
							GeneratedParameters->CurvatureBias = Generated.CurvatureBias;
							GeneratedParameters->CurvatureStrength = Generated.CurvatureStrength;
							GeneratedParameters->CurvaturePower = Generated.CurvaturePower;
							GeneratedParameters->DirectionWeight = Generated.DirectionWeight;
							GeneratedParameters->DirectionAngle = Generated.DirectionAngle;
							GeneratedParameters->DirectionBroadness = Generated.DirectionBroadness;
							GeneratedParameters->AOWeight = Generated.AOWeight;
							GeneratedParameters->HeightWeight = Generated.HeightWeight;
							GeneratedParameters->HeightBias = Generated.HeightBias;
							GeneratedParameters->RidgeWeight = Generated.RidgeWeight;
							GeneratedParameters->NormalizeWeights = Generated.bNormalizeWeights ? 1u : 0u;
							GeneratedParameters->Broadness = Generated.Broadness;
							GeneratedParameters->Smoothing = Generated.Smoothing;
							GeneratedParameters->Bias = Generated.Bias;
							GeneratedParameters->WarpAmount = Generated.WarpAmount;
							GeneratedParameters->WarpSource = Generated.WarpSource;
							GeneratedParameters->WarpRadius = Generated.WarpRadius;
							GeneratedParameters->BlendMode = static_cast<uint32>(Generated.BlendMode);
							GeneratedParameters->Invert = Generated.bInvert ? 1u : 0u;
							GeneratedParameters->Weight = Generated.Weight;
							GeneratedParameters->Balance = Generated.Balance;
							GeneratedParameters->Contrast = Generated.Contrast;
							GeneratedParameters->Offset = Generated.Offset;
							GeneratedParameters->PreviousMask = MaskTargets[MaskReadIndex];
							GeneratedParameters->SurfaceNormal = OutputN[LayerReadIndex];
							GeneratedParameters->SurfaceRAM = OutputRAM[LayerReadIndex];
							GeneratedParameters->SurfaceHeight = HeightTargets[LayerReadIndex];
							GeneratedParameters->SurfaceRidge = RidgeTargets[LayerReadIndex];
							GeneratedParameters->LinearWrapSampler =
								TStaticSamplerState<SF_AnisotropicLinear, AM_Wrap, AM_Wrap, AM_Wrap, 0, 4>::GetRHI();
							GeneratedParameters->OutputMask = GraphBuilder.CreateUAV(MaskTargets[MaskWriteIndex]);

							FComputeShaderUtils::AddPass(
								GraphBuilder,
								RDG_EVENT_NAME("Mixtormat.GeneratedMask.Layer%d.Child%d", LayerIndex, ChildIndex),
								GeneratedMaskShader,
								GeneratedParameters,
								FIntVector(
									FMath::DivideAndRoundUp(Request.Resolution.X, 8),
									FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
									1));
							CombinedMask = MaskTargets[MaskWriteIndex];
							if (Request.DebugSettings.Mode == EMixtormatDebugPreviewMode::LayerMask
								&& Request.DebugSettings.LayerIndex == LayerIndex
								&& Request.DebugSettings.ChildIndex == Child.SourceChildIndex)
							{
								FRDGTextureRef DebugGeneratedSnapshot = GraphBuilder.CreateTexture(
									MaskDesc,
									TEXT("Mixtormat.DebugGeneratedSnapshot"));
								AddCopyTexturePass(GraphBuilder, CombinedMask, DebugGeneratedSnapshot);
								DebugMask = DebugGeneratedSnapshot;
							}
							++MaskPassIndex;
							continue;
						}

						if (Child.Type == EMixtormatLayerChildType::Craquelure)
						{
							const FCraquelureRenderData& Crack = Child.Craquelure;

							// The same identity as the other mask children, and the one that
							// saves the most by far: a muted craquelure node was still growing
							// its whole network, up to a thousand full-resolution passes, to
							// produce a mask it then discarded. Relief reads the same network
							// through its own weights, so both halves have to be idle before
							// there is nothing left to compute.
							// Weight 0 makes the mask half the identity: every mask shader
							// ends on saturate(lerp(Previous, Result, Weight)), and the masks it
							// reads are already saturated, so the output is the input bit for
							// bit. Skipping is only exact from the second mask child onward --
							// the first establishes the chain with Initialize, where Previous is
							// zero rather than what the layer already had, and a skip there
							// would leave a different mask behind rather than the same one.
							if (MaskPassIndex > 0
								&& Crack.Weight == 0.0f
								&& Crack.ReliefDepth == 0.0f
								&& Crack.ReliefNormalStrength == 0.0f)
							{
								continue;
							}

							const int32 MaskWriteIndex = MaskPassIndex & 1;
							const int32 MaskReadIndex = 1 - MaskWriteIndex;
							const FIntVector CrackGroups(
								FMath::DivideAndRoundUp(Request.Resolution.X, 8),
								FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
								1);

							// (distance to the nearest crack in pixels, crack id). Both modes fill
							// it -- the lattice analytically, the propagated mode by flooding its
							// grown skeleton -- so the mask tail and relief read one field and
							// never learn which built it.
							//
							// Full float rather than half: the id is a lineage hash up to 2^24 and
							// has to stay exact, and a half would truncate it and silently merge
							// unrelated cracks into one variation value.
							const FRDGTextureDesc CraqDistanceDesc = FRDGTextureDesc::Create2D(
								Request.Resolution,
								PF_A32B32G32R32F,
								FClearValueBinding::Black,
								TexCreate_ShaderResource | TexCreate_UAV);

							// Propagated networks are looked up before anything is dispatched. On
							// a hit the seed, the growth loop and the whole jump flood are
							// skipped and the kept field is registered straight into this graph:
							// it is the same texture the miss would have produced, so everything
							// downstream is unchanged.
							//
							// Lattice mode is deliberately not cached. Its distance falls out of
							// the same single pass that writes its mask, so there is nothing to
							// skip -- the pass would have to run anyway.
							FRDGTextureRef CraqDistance = nullptr;
							bool bCraqNetworkCached = false;
							if (Crack.Mode == EMixtormatCraquelureMode::Propagated
								&& Request.NetworkCache.IsValid())
							{
								const TRefCountPtr<IPooledRenderTarget> Cached =
									Request.NetworkCache->Find(Crack.NetworkKey, Request.Resolution);
								if (Cached.IsValid())
								{
									CraqDistance = GraphBuilder.RegisterExternalTexture(
										Cached, TEXT("Mixtormat.CraqDistanceCached"));
									bCraqNetworkCached = true;
								}
							}
							if (CraqDistance == nullptr)
							{
								CraqDistance = GraphBuilder.CreateTexture(
									CraqDistanceDesc, TEXT("Mixtormat.CraqDistance"));
							}

							// Half-width of the groove in pixels. Cell units on both sides of the
							// conversion, so it means the same fraction of a cell at any
							// resolution -- and the cell count is the seed lattice in propagated
							// mode and the crack lattice in lattice mode, matching what Width
							// already divides by in each.
							const int32 CraqReliefCells = Crack.Mode == EMixtormatCraquelureMode::Propagated
								? FMath::Max(Crack.SeedCells, 1)
								: FMath::Max(Crack.Period, 1);
							const float CraqReliefWidthPixels =
								Crack.ReliefWidth * Request.Resolution.X / static_cast<float>(CraqReliefCells);

							// Queued whether or not either weight is live, so the branch that
							// decides is in one place; the dispatch below skips a pair of zeroes.
							auto QueueCraquelureRelief = [&]()
							{
								if (Crack.ReliefDepth <= 0.0f && Crack.ReliefNormalStrength <= 0.0f)
								{
									return;
								}
								FPendingCraquelureRelief& Relief = PendingCraquelureReliefs.AddDefaulted_GetRef();
								Relief.Distance = CraqDistance;
								Relief.HeightWeight = Crack.ReliefDepth;
								Relief.NormalWeight = Crack.ReliefNormalStrength;
								Relief.WidthPixels = CraqReliefWidthPixels;
								Relief.Variation = Crack.Variation;
								Relief.Profile = Crack.ReliefProfile;
							};

							// Propagated mode grows a network over N iterations against its own
							// ping-ponged state, then resolves it into the layer mask. The
							// state and direction pair are meaningless to any other mask child,
							// so they are allocated here rather than routed through MaskTargets:
							// the node still consumes one MaskPassIndex and writes one R16F
							// target, exactly like every other mask child.
							if (Crack.Mode == EMixtormatCraquelureMode::Propagated)
							{
								// Everything from here to the store is the build, and it runs only
								// on a miss. A hit already holds the field it would produce, and
								// the mask tail below reads the two identically.
								if (!bCraqNetworkCached)
								{
									// State is (cracked, front, id, level) at full float. The id is
									// a lineage hash up to 2^24 and has to stay exact -- a half
									// would truncate it and silently merge unrelated cracks into
									// one, which the per-crack Variation would then show as a
									// single flat value across the whole network.
									const FRDGTextureDesc CraqStateDesc = FRDGTextureDesc::Create2D(
										Request.Resolution,
										PF_A32B32G32R32F,
										FClearValueBinding::Black,
										TexCreate_ShaderResource | TexCreate_UAV);

									// Direction only needs two channels and the field four, but both
									// are four here. A two-channel typed UAV is a binding shape
									// nothing else in this compositor uses, and it is the one thing
									// a standalone HLSL compile cannot check -- it validates the
									// shader in isolation, never the format against the
									// declaration. Four bytes a pixel to delete that failure mode.
									const FRDGTextureDesc CraqDirectionDesc = FRDGTextureDesc::Create2D(
										Request.Resolution,
										PF_FloatRGBA,
										FClearValueBinding::Black,
										TexCreate_ShaderResource | TexCreate_UAV);
									const FRDGTextureDesc CraqFieldDesc = FRDGTextureDesc::Create2D(
										Request.Resolution,
										PF_FloatRGBA,
										FClearValueBinding::Black,
										TexCreate_ShaderResource | TexCreate_UAV);

									FRDGTextureRef CraqState[2] = {
										GraphBuilder.CreateTexture(CraqStateDesc, TEXT("Mixtormat.CraqStateA")),
										GraphBuilder.CreateTexture(CraqStateDesc, TEXT("Mixtormat.CraqStateB"))};
									FRDGTextureRef CraqDirection[2] = {
										GraphBuilder.CreateTexture(CraqDirectionDesc, TEXT("Mixtormat.CraqDirA")),
										GraphBuilder.CreateTexture(CraqDirectionDesc, TEXT("Mixtormat.CraqDirB"))};
									FRDGTextureRef CraqField =
										GraphBuilder.CreateTexture(CraqFieldDesc, TEXT("Mixtormat.CraqField"));

									FMixtormatCraquelureSeedCS::FParameters* SeedParameters =
										GraphBuilder.AllocParameters<FMixtormatCraquelureSeedCS::FParameters>();
									SeedParameters->OutputSize = Request.Resolution;
									SeedParameters->Seed = Crack.Seed;
									SeedParameters->SeedCells = Crack.SeedCells;
									SeedParameters->SeedChance = Crack.SeedChance;
									SeedParameters->SeedJitter = Crack.SeedJitter;
									SeedParameters->NoiseCells = Crack.NoiseCells;
									SeedParameters->StressVariation = Crack.StressVariation;
									SeedParameters->ToughnessVariation = Crack.ToughnessVariation;
									SeedParameters->Warp = Crack.Warp;
									SeedParameters->WarpPeriod = Crack.WarpPeriod;
									SeedParameters->WarpSeed = Crack.WarpSeed;
									SeedParameters->OutputState = GraphBuilder.CreateUAV(CraqState[0]);
									SeedParameters->OutputDirection = GraphBuilder.CreateUAV(CraqDirection[0]);
									SeedParameters->OutputField = GraphBuilder.CreateUAV(CraqField);

									FComputeShaderUtils::AddPass(
										GraphBuilder,
										RDG_EVENT_NAME("Mixtormat.Craquelure.Seed.Layer%d.Child%d", LayerIndex, ChildIndex),
										CraquelureSeedShader,
										SeedParameters,
										CrackGroups);

									// A crack advances one pixel per iteration, so the authored
									// count is a reach in pixels. Scaled against the same 1024
									// reference chipping uses, so a preview and an export grow the
									// same network rather than the same pixel count.
									//
									// The cap is a cost bound. It used to sit at 192, which quietly
									// made it the reach control rather than a guard on it: at a
									// Reach of 1024 the authored value was clamped to under a fifth
									// of itself at every resolution from 1K up, so most of the
									// slider did nothing at all. Raised to the top of the authored
									// range so Reach means what it says.
									//
									// The scaling still stops being honest above that: a 4K export
									// at maximum Reach clamps where the preview did not. Kept as a
									// bound rather than removed because each step is a
									// full-resolution pass doing roughly eighty texture loads per
									// pixel -- this is by some way the most expensive node in the
									// graph, and an unbounded count at 4K is minutes.
									const int32 GrowIterations = FMath::Clamp(
										FMath::RoundToInt(
											Crack.Iterations *
											FMath::Max(Request.Resolution.X, Request.Resolution.Y) / 1024.0f),
										1,
										1024);

									int32 StateIndex = 0;
									for (int32 GrowPass = 0; GrowPass < GrowIterations; ++GrowPass)
									{
										const int32 ReadState = StateIndex;
										const int32 WriteState = 1 - ReadState;

										FMixtormatCraquelureGrowCS::FParameters* GrowParameters =
											GraphBuilder.AllocParameters<FMixtormatCraquelureGrowCS::FParameters>();
										GrowParameters->OutputSize = Request.Resolution;
										GrowParameters->Seed = Crack.Seed;
										GrowParameters->Persistence = Crack.Persistence;
										GrowParameters->FlowStrength = Crack.FlowStrength;
										GrowParameters->StressGain = Crack.StressGain;
										GrowParameters->ToughnessCost = Crack.ToughnessCost;
										GrowParameters->Irregularity = Crack.Irregularity;
										GrowParameters->GrowthThreshold = Crack.GrowthThreshold;
										// Fixed rather than exposed: it only rejects steps a tip
										// would never take anyway, and the interesting control over
										// how straight a crack runs is Persistence.
										GrowParameters->MinAlignment = 0.05f;
										GrowParameters->TurnResponse = Crack.TurnResponse;
										GrowParameters->CollisionLimit = Crack.CollisionLimit;
										GrowParameters->Iteration = GrowPass;
										GrowParameters->PreviousState = CraqState[ReadState];
										GrowParameters->PreviousDirection = CraqDirection[ReadState];
										GrowParameters->Field = CraqField;
										GrowParameters->OutputState = GraphBuilder.CreateUAV(CraqState[WriteState]);
										GrowParameters->OutputDirection = GraphBuilder.CreateUAV(CraqDirection[WriteState]);

										FComputeShaderUtils::AddPass(
											GraphBuilder,
											RDG_EVENT_NAME(
												"Mixtormat.Craquelure.Grow%d.Layer%d.Child%d",
												GrowPass, LayerIndex, ChildIndex),
											CraquelureGrowShader,
											GrowParameters,
											CrackGroups);

										StateIndex = WriteState;
									}

									// Distance to the grown skeleton, by jump flooding. Log2(N) passes
									// for any radius, where the resolve pass used to brute force a box
									// clamped to radius 8 -- quadratic in the width and a hard cap on
									// it. Relief needs the same field at radii far past what a box
									// could reach, so both read this now.
									{
										const FRDGTextureDesc RecordDesc = FRDGTextureDesc::Create2D(
											Request.Resolution,
											PF_A32B32G32R32F,
											FClearValueBinding::Black,
											TexCreate_ShaderResource | TexCreate_UAV);
										FRDGTextureRef Record[2] = {
											GraphBuilder.CreateTexture(RecordDesc, TEXT("Mixtormat.CraqJfaA")),
											GraphBuilder.CreateTexture(RecordDesc, TEXT("Mixtormat.CraqJfaB"))};

										FMixtormatCraquelureDistanceSeedCS::FParameters* JfaSeed =
											GraphBuilder.AllocParameters<FMixtormatCraquelureDistanceSeedCS::FParameters>();
										JfaSeed->OutputSize = Request.Resolution;
										JfaSeed->CrackState = CraqState[StateIndex];
										JfaSeed->OutputRecord = GraphBuilder.CreateUAV(Record[0]);
										FComputeShaderUtils::AddPass(
											GraphBuilder,
											RDG_EVENT_NAME(
												"Mixtormat.Craquelure.Distance.Seed.Layer%d.Child%d",
												LayerIndex, ChildIndex),
											CraqDistanceSeedShader,
											JfaSeed,
											CrackGroups);

										int32 RecordIndex = 0;

										// Strides halve from half the padded extent down to 1, then one
										// more pass at 1 -- the JFA+1 variant. Plain jump flooding is
										// not exact: a seed can be lost when the record that would have
										// carried it was itself overwritten at a coarser stride. The
										// extra unit pass costs one dispatch and removes the islands
										// that error shows up as.
										const int32 FirstStep = FMath::Max(
											1,
											static_cast<int32>(FMath::RoundUpToPowerOfTwo(
												static_cast<uint32>(FMath::Max(
													Request.Resolution.X, Request.Resolution.Y)))) / 2);

										for (int32 StepSize = FirstStep; StepSize >= 1; StepSize /= 2)
										{
											const int32 ReadRecord = RecordIndex;
											const int32 WriteRecord = 1 - ReadRecord;

											FMixtormatCraquelureDistanceStepCS::FParameters* JfaStep =
												GraphBuilder.AllocParameters<FMixtormatCraquelureDistanceStepCS::FParameters>();
											JfaStep->OutputSize = Request.Resolution;
											JfaStep->StepSize = StepSize;
											JfaStep->PreviousRecord = Record[ReadRecord];
											JfaStep->OutputRecord = GraphBuilder.CreateUAV(Record[WriteRecord]);
											FComputeShaderUtils::AddPass(
												GraphBuilder,
												RDG_EVENT_NAME(
													"Mixtormat.Craquelure.Distance.Step%d.Layer%d.Child%d",
													StepSize, LayerIndex, ChildIndex),
												CraqDistanceStepShader,
												JfaStep,
												CrackGroups);

											RecordIndex = WriteRecord;
										}

										{
											const int32 ReadRecord = RecordIndex;
											const int32 WriteRecord = 1 - ReadRecord;

											FMixtormatCraquelureDistanceStepCS::FParameters* JfaStep =
												GraphBuilder.AllocParameters<FMixtormatCraquelureDistanceStepCS::FParameters>();
											JfaStep->OutputSize = Request.Resolution;
											JfaStep->StepSize = 1;
											JfaStep->PreviousRecord = Record[ReadRecord];
											JfaStep->OutputRecord = GraphBuilder.CreateUAV(Record[WriteRecord]);
											FComputeShaderUtils::AddPass(
												GraphBuilder,
												RDG_EVENT_NAME(
													"Mixtormat.Craquelure.Distance.StepFinal.Layer%d.Child%d",
													LayerIndex, ChildIndex),
												CraqDistanceStepShader,
												JfaStep,
												CrackGroups);

											RecordIndex = WriteRecord;
										}

										FMixtormatCraquelureDistanceResolveCS::FParameters* JfaResolve =
											GraphBuilder.AllocParameters<FMixtormatCraquelureDistanceResolveCS::FParameters>();
										JfaResolve->OutputSize = Request.Resolution;
										JfaResolve->PreviousRecord = Record[RecordIndex];
										JfaResolve->OutputDistance = GraphBuilder.CreateUAV(CraqDistance);
										FComputeShaderUtils::AddPass(
											GraphBuilder,
											RDG_EVENT_NAME(
												"Mixtormat.Craquelure.Distance.Resolve.Layer%d.Child%d",
												LayerIndex, ChildIndex),
											CraqDistanceResolveShader,
											JfaResolve,
											CrackGroups);
									}

									// Kept for the next composite. Converting promotes the transient to
									// a pooled target, which costs the memory of one full-resolution
									// RGBA32F per distinct network -- the trade this whole path makes.
									if (Request.NetworkCache.IsValid())
									{
										Request.NetworkCache->Store(
											Crack.NetworkKey,
											Request.Resolution,
											GraphBuilder.ConvertToExternalTexture(CraqDistance));
									}

								}

								QueueCraquelureRelief();

								FMixtormatCraquelureResolveCS::FParameters* ResolveParameters =
									GraphBuilder.AllocParameters<FMixtormatCraquelureResolveCS::FParameters>();
								ResolveParameters->OutputSize = Request.Resolution;
								ResolveParameters->Initialize = MaskPassIndex == 0 ? 1u : 0u;
								ResolveParameters->SeedCells = Crack.SeedCells;
								ResolveParameters->Width = Crack.Width;
								ResolveParameters->Variation = Crack.Variation;
								ResolveParameters->BlendMode = static_cast<uint32>(Crack.BlendMode);
								ResolveParameters->Invert = Crack.bInvert ? 1u : 0u;
								ResolveParameters->Weight = Crack.Weight;
								ResolveParameters->Balance = Crack.Balance;
								ResolveParameters->Contrast = Crack.Contrast;
								ResolveParameters->Offset = Crack.Offset;
								ResolveParameters->CrackDistance = CraqDistance;
								ResolveParameters->PreviousMask = MaskTargets[MaskReadIndex];
								ResolveParameters->LinearWrapSampler =
									TStaticSamplerState<SF_AnisotropicLinear, AM_Wrap, AM_Wrap, AM_Wrap, 0, 4>::GetRHI();
								ResolveParameters->OutputMask =
									GraphBuilder.CreateUAV(MaskTargets[MaskWriteIndex]);

								FComputeShaderUtils::AddPass(
									GraphBuilder,
									RDG_EVENT_NAME("Mixtormat.Craquelure.Resolve.Layer%d.Child%d", LayerIndex, ChildIndex),
									CraquelureResolveShader,
									ResolveParameters,
									CrackGroups);

								CombinedMask = MaskTargets[MaskWriteIndex];
								if (Request.DebugSettings.Mode == EMixtormatDebugPreviewMode::LayerMask
									&& Request.DebugSettings.LayerIndex == LayerIndex
									&& Request.DebugSettings.ChildIndex == Child.SourceChildIndex)
								{
									FRDGTextureRef DebugCrackSnapshot = GraphBuilder.CreateTexture(
										MaskDesc,
										TEXT("Mixtormat.DebugCraquelureSnapshot"));
									AddCopyTexturePass(GraphBuilder, CombinedMask, DebugCrackSnapshot);
									DebugMask = DebugCrackSnapshot;
								}
								++MaskPassIndex;
								continue;
							}

							FMixtormatCraquelureCS::FParameters* CrackParameters =
								GraphBuilder.AllocParameters<FMixtormatCraquelureCS::FParameters>();
							CrackParameters->OutputSize = Request.Resolution;
							CrackParameters->Initialize = MaskPassIndex == 0 ? 1u : 0u;
							CrackParameters->Period = Crack.Period;
							CrackParameters->Jitter = Crack.Jitter;
							CrackParameters->Width = Crack.Width;
							CrackParameters->Variation = Crack.Variation;
							CrackParameters->Seed = Crack.Seed;
							CrackParameters->Warp = Crack.Warp;
							CrackParameters->WarpPeriod = Crack.WarpPeriod;
							CrackParameters->WarpSeed = Crack.WarpSeed;
							CrackParameters->BlendMode = static_cast<uint32>(Crack.BlendMode);
							CrackParameters->Invert = Crack.bInvert ? 1u : 0u;
							CrackParameters->Weight = Crack.Weight;
							CrackParameters->Balance = Crack.Balance;
							CrackParameters->Contrast = Crack.Contrast;
							CrackParameters->Offset = Crack.Offset;
							CrackParameters->PreviousMask = MaskTargets[MaskReadIndex];
							CrackParameters->LinearWrapSampler =
								TStaticSamplerState<SF_AnisotropicLinear, AM_Wrap, AM_Wrap, AM_Wrap, 0, 4>::GetRHI();
							CrackParameters->OutputMask =
								GraphBuilder.CreateUAV(MaskTargets[MaskWriteIndex]);
							CrackParameters->OutputDistance = GraphBuilder.CreateUAV(CraqDistance);

							FComputeShaderUtils::AddPass(
								GraphBuilder,
								RDG_EVENT_NAME("Mixtormat.Craquelure.Layer%d.Child%d", LayerIndex, ChildIndex),
								CraquelureShader,
								CrackParameters,
								FIntVector(
									FMath::DivideAndRoundUp(Request.Resolution.X, 8),
									FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
									1));
							QueueCraquelureRelief();
							CombinedMask = MaskTargets[MaskWriteIndex];
							if (Request.DebugSettings.Mode == EMixtormatDebugPreviewMode::LayerMask
								&& Request.DebugSettings.LayerIndex == LayerIndex
								&& Request.DebugSettings.ChildIndex == Child.SourceChildIndex)
							{
								FRDGTextureRef DebugCrackSnapshot = GraphBuilder.CreateTexture(
									MaskDesc,
									TEXT("Mixtormat.DebugCraquelureSnapshot"));
								AddCopyTexturePass(GraphBuilder, CombinedMask, DebugCrackSnapshot);
								DebugMask = DebugCrackSnapshot;
							}
							++MaskPassIndex;
							continue;
						}

						if (Child.Type == EMixtormatLayerChildType::ColorId)
						{
							const FColorIdRenderData& ColorId = Child.ColorId;

							// The same identity as the other mask children: at Weight 0 the tail
							// returns Previous unchanged, and skipping from the second child on
							// leaves exactly that behind.
							if (MaskPassIndex > 0 && ColorId.Weight == 0.0f)
							{
								continue;
							}

							const int32 MaskWriteIndex = MaskPassIndex & 1;
							const int32 MaskReadIndex = 1 - MaskWriteIndex;

							FMixtormatColorIdCS::FParameters* IdParameters =
								GraphBuilder.AllocParameters<FMixtormatColorIdCS::FParameters>();
							IdParameters->OutputSize = Request.Resolution;
							IdParameters->Initialize = MaskPassIndex == 0 ? 1u : 0u;
							IdParameters->ColorCount = ColorId.Colors.Num();
							for (int32 ColorIndex = 0; ColorIndex < FMixtormatColorIdCS::MaxColors; ++ColorIndex)
							{
								// The unused tail is filled rather than left alone. A shader
								// parameter array is not zero initialised, and the loop in the
								// shader is bounded by ColorCount, but an uninitialised constant
								// is the kind of thing that only misbehaves on one driver.
								IdParameters->TargetColors[ColorIndex] =
									ColorId.Colors.IsValidIndex(ColorIndex)
										? ColorId.Colors[ColorIndex]
										: FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
							}
							IdParameters->Tolerance = ColorId.Tolerance;
							IdParameters->Softness = ColorId.Softness;
							IdParameters->BlendMode = static_cast<uint32>(ColorId.BlendMode);
							IdParameters->Invert = ColorId.bInvert ? 1u : 0u;
							IdParameters->Weight = ColorId.Weight;
							IdParameters->Balance = ColorId.Balance;
							IdParameters->Contrast = ColorId.Contrast;
							IdParameters->Offset = ColorId.Offset;
							IdParameters->Tiling = ColorId.Tiling;
							IdParameters->UVOffset = ColorId.UVOffset;
							IdParameters->FlipU = ColorId.bFlipU ? 1u : 0u;
							IdParameters->FlipV = ColorId.bFlipV ? 1u : 0u;
							IdParameters->Rotation = ColorId.Rotation;
							IdParameters->PreviousMask = MaskTargets[MaskReadIndex];
							IdParameters->IdTexture = RegisterTexture(
								GraphBuilder,
								RegisteredTextures,
								ColorId.IdTexture,
								TEXT("Mixtormat.ColorIdMap"));

							// Point, and the only point sampler in the compositor. Every other
							// map here is a continuous signal that wants filtering; an id map is
							// a set of labels, and the average of two labels is a third label
							// that names nothing.
							IdParameters->PointSampler =
								TStaticSamplerState<SF_Point, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
							IdParameters->LinearWrapSampler =
								TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
							IdParameters->OutputMask =
								GraphBuilder.CreateUAV(MaskTargets[MaskWriteIndex]);

							FComputeShaderUtils::AddPass(
								GraphBuilder,
								RDG_EVENT_NAME("Mixtormat.ColorId.Layer%d.Child%d", LayerIndex, ChildIndex),
								ColorIdShader,
								IdParameters,
								FIntVector(
									FMath::DivideAndRoundUp(Request.Resolution.X, 8),
									FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
									1));

							CombinedMask = MaskTargets[MaskWriteIndex];
							if (Request.DebugSettings.Mode == EMixtormatDebugPreviewMode::LayerMask
								&& Request.DebugSettings.LayerIndex == LayerIndex
								&& Request.DebugSettings.ChildIndex == Child.SourceChildIndex)
							{
								FRDGTextureRef DebugIdSnapshot = GraphBuilder.CreateTexture(
									MaskDesc,
									TEXT("Mixtormat.DebugColorIdSnapshot"));
								AddCopyTexturePass(GraphBuilder, CombinedMask, DebugIdSnapshot);
								DebugMask = DebugIdSnapshot;
							}
							++MaskPassIndex;
							continue;
						}

						if (Child.Type == EMixtormatLayerChildType::Mask)
						{
							const FMaskRenderData& Mask = Child.Mask;
							// Weight 0 makes the whole node the identity: every mask shader
							// ends on saturate(lerp(Previous, Result, Weight)), and the masks it
							// reads are already saturated, so the output is the input bit for
							// bit. Skipping is only exact from the second mask child onward --
							// the first establishes the chain with Initialize, where Previous is
							// zero rather than what the layer already had, and a skip there
							// would leave a different mask behind rather than the same one.
							if (MaskPassIndex > 0 && Mask.Weight == 0.0f)
							{
								continue;
							}

							const int32 MaskWriteIndex = MaskPassIndex & 1;
							const int32 MaskReadIndex = 1 - MaskWriteIndex;
							FMixtormatMaskCS::FParameters* MaskParameters =
								GraphBuilder.AllocParameters<FMixtormatMaskCS::FParameters>();
							MaskParameters->OutputSize = Request.Resolution;
							MaskParameters->Initialize = MaskPassIndex == 0 ? 1u : 0u;
							MaskParameters->BlendMode = static_cast<uint32>(Mask.BlendMode);
							MaskParameters->Invert = Mask.bInvert ? 1u : 0u;
							MaskParameters->Weight = Mask.Weight;
							MaskParameters->Tiling = Mask.Tiling;
							MaskParameters->UVOffset = Mask.UVOffset;
							MaskParameters->FlipU = Mask.bFlipU ? 1u : 0u;
							MaskParameters->FlipV = Mask.bFlipV ? 1u : 0u;
							MaskParameters->Rotation = Mask.Rotation;
							MaskParameters->Balance = Mask.Balance;
							MaskParameters->Contrast = Mask.Contrast;
							MaskParameters->Offset = Mask.Offset;
							MaskParameters->PreviousMask = MaskTargets[MaskReadIndex];
							MaskParameters->IncomingMask = RegisterTexture(
								GraphBuilder,
								RegisteredTextures,
								Mask.Texture,
								TEXT("Mixtormat.IncomingMask"));
							MaskParameters->LinearWrapSampler =
								TStaticSamplerState<SF_AnisotropicLinear, AM_Wrap, AM_Wrap, AM_Wrap, 0, 4>::GetRHI();
							MaskParameters->OutputMask = GraphBuilder.CreateUAV(MaskTargets[MaskWriteIndex]);

							FComputeShaderUtils::AddPass(
								GraphBuilder,
								RDG_EVENT_NAME("Mixtormat.Mask.Layer%d.Child%d", LayerIndex, ChildIndex),
								MaskShader,
								MaskParameters,
								FIntVector(
									FMath::DivideAndRoundUp(Request.Resolution.X, 8),
									FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
									1));
							CombinedMask = MaskTargets[MaskWriteIndex];
							if (Request.DebugSettings.Mode == EMixtormatDebugPreviewMode::LayerMask
								&& Request.DebugSettings.LayerIndex == LayerIndex
								&& Request.DebugSettings.ChildIndex == Child.SourceChildIndex)
							{
								FRDGTextureRef DebugMaskSnapshot = GraphBuilder.CreateTexture(
									MaskDesc,
									TEXT("Mixtormat.DebugMaskSnapshot"));
								AddCopyTexturePass(GraphBuilder, CombinedMask, DebugMaskSnapshot);
								DebugMask = DebugMaskSnapshot;
							}
							++MaskPassIndex;
							continue;
						}

						const FEffectRenderData& Effect = Child.Effect;
						if (Effect.Type == EMixtormatEffectType::Erosion)
						{
							// Erosion is a post-layer filter: it carves what this layer actually
							// composited, not the height underneath it. Running it here would let
							// the layer paint straight back over the carve.
							PendingErosion = &Effect;
							continue;
						}

						if (Effect.Type == EMixtormatEffectType::Chipping)
						{
							// Also a post-layer filter. It runs after erosion rather than before:
							// chipping a surface that has already weathered is the order that
							// makes sense, and the reverse would have erosion smoothing chips it
							// never saw.
							PendingChipping = &Effect;
							continue;
						}

						if (Effect.Type == EMixtormatEffectType::Grade)
						{
							// Also a post-layer filter, for the same reason: it grades what the
							// stack has accumulated at this point, and running it inside the
							// child loop would grade a base colour the layer then overwrites.
							PendingGrades.Add(&Effect);
							continue;
						}

						if (Effect.Type == EMixtormatEffectType::Stain)
						{
							const int32 StainWriteIndex = StainPassIndex & 1;
							const int32 StainReadIndex = 1 - StainWriteIndex;
							FMixtormatStainCS::FParameters* StainParameters =
								GraphBuilder.AllocParameters<FMixtormatStainCS::FParameters>();
							StainParameters->OutputSize = Request.Resolution;
							StainParameters->Initialize = StainPassIndex == 0 ? 1u : 0u;
							StainParameters->Strength = Effect.Strength;
							StainParameters->StainColor = FVector4f(
															Effect.StainColor.R,
															Effect.StainColor.G,
															Effect.StainColor.B,
															Effect.StainColor.A);
							StainParameters->RoughnessInfluence = Effect.StainRoughness;
							StainParameters->HeightInfluence = Effect.StainHeightInfluence;
							StainParameters->HeightWarp = Effect.StainHeightWarp;
							StainParameters->HeightBias = Effect.StainHeightBias;
							StainParameters->HeightContrast = Effect.StainHeightContrast;

							// The same orientation field erosion uses, over the surface this
							// stain runs down. Built per stain rather than hoisted per layer:
							// each carries its own radius and smoothing, most layers have one
							// stain at most, and a stain that does not ask for flow builds
							// nothing. The warp is skipped wholesale at zero, so a field built
							// here would always be read.
							StainParameters->FlowAmount = Effect.StainFlowAmount;
							StainParameters->Gravity = Effect.StainGravity;
							StainParameters->FlowField =
								(Effect.StainFlowAmount > 0.0f && Effect.StainHeightWarp > 0.0f)
									? AddFlowField(
										HeightTargets[1 - (LayerIndex & 1)],
										Request.Resolution,
										Effect.StainFlowRadius,
										Effect.StainFlowSmoothing,
										TEXT("Stain"))
									: FlowDummy;

							StainParameters->PreviousStainData = StainTargets[StainReadIndex];
							StainParameters->ChildMask = CombinedMask;
							StainParameters->AccumulatedHeight = HeightTargets[1 - (LayerIndex & 1)];
							StainParameters->LinearWrapSampler =
								TStaticSamplerState<SF_AnisotropicLinear, AM_Wrap, AM_Wrap, AM_Wrap, 0, 4>::GetRHI();
							StainParameters->OutputStainData = GraphBuilder.CreateUAV(StainTargets[StainWriteIndex]);
							FComputeShaderUtils::AddPass(
								GraphBuilder,
								RDG_EVENT_NAME("Mixtormat.Stain.Layer%d.Child%d", LayerIndex, ChildIndex),
								StainShader,
								StainParameters,
								FIntVector(
									FMath::DivideAndRoundUp(Request.Resolution.X, 8),
									FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
									1));
							CombinedStainData = StainTargets[StainWriteIndex];
							++StainPassIndex;
							continue;
						}

						// A procedural peel builds its field first: seed the mask's threshold
						// contour, then a chain of eikonal solve steps, then one resolve into
						// the same channel layout the authored maps carry. The peel pass below
						// is identical either way apart from which source it reads.
						FRDGTextureRef PeelFieldA = PeelFieldDummy;
						FRDGTextureRef PeelFieldB = PeelFieldDummy;
						if (Effect.bProceduralPeel)
						{
							// Same accumulated state the generated mask reads: the surface
							// composited below this layer.
							const int32 PeelSurfaceIndex = 1 - (LayerIndex & 1);

							// The solve dominates cost, and halving the side both quarters
							// the texels and halves the passes the front needs to cross
							// them. Arrival is smooth enough to filter back up afterwards.
							const int32 SolveDivisor = FMath::Clamp(Effect.PeelSolveDivisor, 1, 32);
							const FIntPoint SolveRes(
								FMath::Max(Request.Resolution.X / SolveDivisor, 64),
								FMath::Max(Request.Resolution.Y / SolveDivisor, 64));

							const FRDGTextureDesc ArrivalDesc = FRDGTextureDesc::Create2D(
								SolveRes, PF_G32R32F, FClearValueBinding::Black,
								TexCreate_ShaderResource | TexCreate_UAV);
							const FRDGTextureDesc GrowthDesc = FRDGTextureDesc::Create2D(
								SolveRes, PF_R16F, FClearValueBinding::Black,
								TexCreate_ShaderResource | TexCreate_UAV);
							const FRDGTextureDesc FieldDesc = FRDGTextureDesc::Create2D(
								Request.Resolution, PF_FloatRGBA, FClearValueBinding::Black,
								TexCreate_ShaderResource | TexCreate_UAV);

							FRDGTextureRef Arrival[2] = {
								GraphBuilder.CreateTexture(ArrivalDesc, TEXT("Mixtormat.PeelArrivalA")),
								GraphBuilder.CreateTexture(ArrivalDesc, TEXT("Mixtormat.PeelArrivalB"))};
							FRDGTextureRef PeelGrowth =
								GraphBuilder.CreateTexture(GrowthDesc, TEXT("Mixtormat.PeelGrowth"));
							FRDGTextureRef FieldA =
								GraphBuilder.CreateTexture(FieldDesc, TEXT("Mixtormat.PeelFieldA"));
							FRDGTextureRef FieldB =
								GraphBuilder.CreateTexture(FieldDesc, TEXT("Mixtormat.PeelFieldB"));

							auto AddPeelFieldPass = [&](
								const int32 ModeIndex,
								FRDGTextureRef InArrival,
								FRDGTextureRef OutArrival,
								const TCHAR* DebugName)
							{
								FMixtormatPeelFieldCS::FParameters* FP =
									GraphBuilder.AllocParameters<FMixtormatPeelFieldCS::FParameters>();
								FP->OutputSize = Request.Resolution;
								FP->SolveSize = SolveRes;
								FP->Mode = ModeIndex;
								FP->SurfaceValid = LayerIndex > 0 ? 1u : 0u;
								FP->FlipNormalY = Layer.bFlipNormalY ? 1u : 0u;
								FP->Seed = Effect.PeelRandomSeed;
								FP->MaskWeight = Effect.PeelSeedMaskWeight;
								FP->PeelMaskTiling = Effect.PeelMaskTiling;
								FP->PeelMaskInvert = Effect.bPeelMaskInvert ? 1u : 0u;
								FP->UseOwnMask = Effect.PeelOwnMask.IsValid() ? 1u : 0u;
								FP->PeelOwnMask = Effect.PeelOwnMask.IsValid()
									? RegisterTexture(GraphBuilder, RegisteredTextures, Effect.PeelOwnMask, TEXT("Mixtormat.PeelOwnMask"))
									: PeelFieldDummy;
								FP->SeedThreshold = Effect.PeelSeedThreshold;
								FP->CurvatureRadius = Effect.PeelCurvatureRadius;
								FP->CurvatureSmoothing = 1;
								FP->CurvatureWeight = Effect.PeelSeedCurvatureWeight;
								FP->CurvatureBias = Effect.PeelSeedCurvatureBias;
								FP->AOWeight = Effect.PeelSeedAOWeight;
								FP->HeightWeight = Effect.PeelSeedHeightWeight;
								FP->NormalizeWeights = Effect.bPeelNormalizeSeedWeights ? 1u : 0u;
								FP->GrowthStrength = Effect.PeelGrowthStrength;
								FP->SizeVariation = Effect.PeelSizeVariation;
								FP->FlakeCells = Effect.PeelClusterPeriod;
								FP->PeelType = Effect.PeelType;
								FP->Front = Effect.Front;
								FP->Width = Effect.Width;
								FP->MacroWarp = Effect.MacroWarp;
								FP->MicroWarp = Effect.MicroWarp;
								FP->MicroMorph = Effect.MicroMorph;
								FP->Thickness = Effect.Thickness;
								FP->Lift = Effect.Lift;
								FP->DetailStrength = Effect.DetailStrength;
								FP->LiftVariation = Effect.PeelLiftVariation;
								FP->EdgeSharpness = Effect.PeelEdgeSharpness;
								FP->SurfaceNormal = OutputN[PeelSurfaceIndex];
								FP->SurfaceRAM = OutputRAM[PeelSurfaceIndex];
								FP->SurfaceHeight = HeightTargets[PeelSurfaceIndex];
								FP->ChildMask = CombinedMask;
								FP->PreviousArrival = InArrival;
								FP->GrowthField = PeelGrowth;
								FP->LinearWrapSampler =
									TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
								FP->OutputArrival = GraphBuilder.CreateUAV(OutArrival);
								FP->OutputGrowth = GraphBuilder.CreateUAV(PeelGrowth);
								FP->OutputFieldA = GraphBuilder.CreateUAV(FieldA);
								FP->OutputFieldB = GraphBuilder.CreateUAV(FieldB);

								const FIntPoint PassRes = ModeIndex == 2 ? Request.Resolution : SolveRes;
								FComputeShaderUtils::AddPass(
									GraphBuilder,
									RDG_EVENT_NAME(
										"Mixtormat.PeelField.L%d.C%d.%s",
										LayerIndex, ChildIndex, DebugName),
									PeelFieldShader,
									FP,
									FIntVector(
										FMath::DivideAndRoundUp(PassRes.X, 8),
										FMath::DivideAndRoundUp(PassRes.Y, 8),
										1));
							};

							AddPeelFieldPass(0, Arrival[1], Arrival[0], TEXT("Seed"));

							// The front only has to travel Front plus a few transition
							// widths, and advances about one texel per pass, so the count
							// is bounded by reach at the solve resolution. Seeding the
							// contour rather than the interior does not raise it: inward
							// and outward propagation leave the same band on the same pass,
							// and the reach each side needs is still the same.
							int32 Ping = 0;
							const float Reach =
								FMath::Abs(Effect.Front) + 4.0f * FMath::Abs(Effect.Width);
							const int32 Iterations = FMath::Clamp(
								FMath::CeilToInt(Reach * SolveRes.X), 1, 256);
							for (int32 Step = 0; Step < Iterations; ++Step)
							{
								AddPeelFieldPass(1, Arrival[Ping], Arrival[1 - Ping], TEXT("Solve"));
								Ping = 1 - Ping;
							}

							AddPeelFieldPass(2, Arrival[Ping], Arrival[1 - Ping], TEXT("Resolve"));

							PeelFieldA = FieldA;
							PeelFieldB = FieldB;
						}

						const int32 EffectWriteIndex = EffectPassIndex & 1;
						const int32 EffectReadIndex = 1 - EffectWriteIndex;
						FMixtormatPeelingCS::FParameters* EffectParameters =
							GraphBuilder.AllocParameters<FMixtormatPeelingCS::FParameters>();
						EffectParameters->OutputSize = Request.Resolution;
						EffectParameters->Initialize = EffectPassIndex == 0 ? 1u : 0u;
						EffectParameters->Tiling = Effect.Tiling;
						EffectParameters->Strength = Effect.Strength;
						EffectParameters->Front = Effect.Front;
						EffectParameters->Width = Effect.Width;
						EffectParameters->MacroWarp = Effect.MacroWarp;
						EffectParameters->MicroWarp = Effect.MicroWarp;
						EffectParameters->MicroMorph = Effect.MicroMorph;
						EffectParameters->Thickness = Effect.Thickness;
						EffectParameters->Lift = Effect.Lift;
						EffectParameters->DetailStrength = Effect.DetailStrength;
						EffectParameters->DistanceRange = Effect.DistanceRange;
						EffectParameters->SDFRange = Effect.SDFRange;
						EffectParameters->HeightRange = Effect.HeightRange;
						EffectParameters->PreviousEffectData = EffectTargets[EffectReadIndex];
						EffectParameters->ChildMask = CombinedMask;
						// A procedural peel has no source maps at all, and RegisterTexture
						// asserts on a null RHI texture, so those four slots take the
						// placeholder. The shader ignores them under ProceduralSource.
						if (Effect.bProceduralPeel)
						{
							EffectParameters->PeelData = PeelFieldDummy;
							EffectParameters->PeelMask = PeelFieldDummy;
							EffectParameters->PeelHeight = PeelFieldDummy;
							EffectParameters->PeelSDF = PeelFieldDummy;
						}
						else
						{
							EffectParameters->PeelData = RegisterTexture(GraphBuilder, RegisteredTextures, Effect.PeelData, TEXT("Mixtormat.PeelData"));
							EffectParameters->PeelMask = RegisterTexture(GraphBuilder, RegisteredTextures, Effect.Mask, TEXT("Mixtormat.PeelMask"));
							EffectParameters->PeelHeight = RegisterTexture(GraphBuilder, RegisteredTextures, Effect.Height, TEXT("Mixtormat.PeelHeight"));
							EffectParameters->PeelSDF = RegisterTexture(GraphBuilder, RegisteredTextures, Effect.SDF, TEXT("Mixtormat.PeelSDF"));
						}
						EffectParameters->ProceduralSource = Effect.bProceduralPeel ? 1u : 0u;
						EffectParameters->ProceduralAOStrength = Effect.PeelAOStrength;
						EffectParameters->HeightAmount = Effect.PeelHeightAmount;
						EffectParameters->HeightInvert = Effect.bPeelHeightInvert ? 1.0f : 0.0f;
						EffectParameters->PreviousEffectHeight = EffectHeightTargets[EffectReadIndex];
						EffectParameters->OutputEffectHeight = GraphBuilder.CreateUAV(EffectHeightTargets[EffectWriteIndex]);
						EffectParameters->PeelFieldA = PeelFieldA;
						EffectParameters->PeelFieldB = PeelFieldB;
						EffectParameters->LinearWrapSampler = TStaticSamplerState<SF_AnisotropicLinear, AM_Wrap, AM_Wrap, AM_Wrap, 0, 4>::GetRHI();
						EffectParameters->PointWrapSampler = TStaticSamplerState<SF_Point, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
						EffectParameters->OutputEffectData = GraphBuilder.CreateUAV(EffectTargets[EffectWriteIndex]);
						FComputeShaderUtils::AddPass(
							GraphBuilder,
							RDG_EVENT_NAME("Mixtormat.Peeling.Layer%d.Child%d", LayerIndex, ChildIndex),
							PeelingShader,
							EffectParameters,
							FIntVector(
								FMath::DivideAndRoundUp(Request.Resolution.X, 8),
								FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
								1));
						CombinedEffectData = EffectTargets[EffectWriteIndex];
						CombinedEffectHeight = EffectHeightTargets[EffectWriteIndex];
						++EffectPassIndex;
					}

					const int32 WriteIndex = LayerIndex & 1;
					const int32 ReadIndex = 1 - WriteIndex;
					FMixtormatCompositeCS::FParameters* Parameters =
						GraphBuilder.AllocParameters<FMixtormatCompositeCS::FParameters>();
					Parameters->OutputSize = Request.Resolution;
					Parameters->Enabled = Layer.bEnabled ? 1u : 0u;
					Parameters->HasMask = Layer.bHasMask ? 1u : 0u;
					Parameters->HasEffects = Layer.bHasEffects ? 1u : 0u;
					Parameters->HasStain = Layer.bHasStain ? 1u : 0u;
					Parameters->OverrideBaseColor = Layer.bOverrideBaseColor ? 1u : 0u;
					Parameters->OverrideRoughness = Layer.bOverrideRoughness ? 1u : 0u;
					Parameters->OverrideMetallic = Layer.bOverrideMetallic ? 1u : 0u;
					Parameters->CompositionMode = Layer.bCoat ? 1u : 0u;
					Parameters->IsFill = Layer.bFill ? 1u : 0u;
					Parameters->HasSurface = Layer.bHasSurface ? 1u : 0u;
					Parameters->HasPackedHeight = Layer.bHasPackedHeight ? 1u : 0u;
					Parameters->HasNormal = Layer.bHasNormal ? 1u : 0u;
					Parameters->NormalOnly = Layer.bNormalOnly ? 1u : 0u;
					Parameters->OverrideNormal = Layer.bOverrideNormal ? 1u : 0u;
					Parameters->FlipNormalY = Layer.bFlipNormalY ? 1u : 0u;
					Parameters->HeightBlendEnabled = Layer.bHeightBlendEnabled ? 1u : 0u;
					Parameters->HeightSource = Layer.HeightSource;
					Parameters->InvertHeight = Layer.bInvertHeight ? 1u : 0u;
					Parameters->DirectHeightComparison = Layer.bDirectHeightComparison ? 1u : 0u;
					Parameters->InvertHeightFeature = Layer.bInvertHeightFeature ? 1u : 0u;
					Parameters->InvertAOFeature = Layer.bInvertAOFeature ? 1u : 0u;
					Parameters->InvertFeature = Layer.bInvertFeature ? 1u : 0u;
					Parameters->DebugMode = static_cast<uint32>(Request.DebugSettings.Mode);
					Parameters->WriteDebug = Request.DebugSettings.Mode != EMixtormatDebugPreviewMode::None
						&& Request.DebugSettings.LayerIndex == LayerIndex ? 1u : 0u;
					Parameters->Opacity = Layer.Opacity;
					Parameters->Tiling = Layer.Tiling;
					Parameters->UVScaleX = Layer.UVScaleX;
					Parameters->UVScaleY = Layer.UVScaleY;
					Parameters->FlipU = Layer.bFlipU ? 1u : 0u;
					Parameters->FlipV = Layer.bFlipV ? 1u : 0u;
					Parameters->UVOffset = Layer.UVOffset;
					Parameters->Rotation = Layer.Rotation;
					Parameters->NormalIntensity = Layer.NormalIntensity;
					Parameters->HueShift = Layer.HueShift;
					Parameters->Saturation = Layer.Saturation;
					Parameters->Value = Layer.Value;
					Parameters->RoughnessBias = Layer.RoughnessBias;
					Parameters->RoughnessContrast = Layer.RoughnessContrast;
					Parameters->RoughnessOffset = Layer.RoughnessOffset;
					Parameters->FillRoughness = Layer.FillRoughness;
					Parameters->FillMetallic = Layer.FillMetallic;
					Parameters->LayerF0 = Layer.LayerF0;
					Parameters->BaseColorInfluence = Layer.BaseColorInfluence;
					Parameters->RoughnessInfluence = Layer.RoughnessInfluence;
					Parameters->AOInfluence = Layer.AOInfluence;
					Parameters->MetallicInfluence = Layer.MetallicInfluence;
					Parameters->F0Influence = Layer.F0Influence;
					Parameters->NormalInfluence = Layer.NormalInfluence;
					Parameters->HeightInfluence = Layer.HeightInfluence;
					Parameters->HeightBlendAmount = Layer.HeightBlendAmount;
					Parameters->HeightThreshold = Layer.HeightThreshold;
					Parameters->HeightRange = Layer.HeightRange;
					Parameters->HeightContrast = Layer.HeightContrast;
					Parameters->HeightOffset = Layer.HeightOffset;
					Parameters->HeightBias = Layer.HeightBias;
					Parameters->ConstantHeight = Layer.ConstantHeight;
					Parameters->MaskHeightInfluence = Layer.MaskHeightInfluence;
					Parameters->HeightContactAOAmount = Layer.HeightContactAOAmount;
					Parameters->HeightContactAOWidth = Layer.HeightContactAOWidth;
					Parameters->HeightBorderLift = Layer.HeightBorderLift;
					Parameters->HeightBorderWidth = Layer.HeightBorderWidth;
					Parameters->HeightBorderNormalStrength = Layer.HeightBorderNormalStrength;
					Parameters->FeatureInfluence = Layer.FeatureInfluence;
					Parameters->FeatureBias = Layer.FeatureBias;
					Parameters->HeightFeatureInfluence = Layer.HeightFeatureInfluence;
					Parameters->AOFeatureInfluence = Layer.AOFeatureInfluence;
					Parameters->CurvatureRadius = Layer.CurvatureRadius;
					Parameters->CurvatureStrength = Layer.CurvatureStrength;
					Parameters->CurvaturePower = Layer.CurvaturePower;
					Parameters->CurvatureSmoothing = Layer.CurvatureSmoothing;
					Parameters->FillColor = Layer.FillColor;
					Parameters->PreviousBC = OutputBC[ReadIndex];
					Parameters->PreviousN = OutputN[ReadIndex];
					Parameters->PreviousRAM = OutputRAM[ReadIndex];
					Parameters->PreviousHeight = HeightTargets[ReadIndex];
					Parameters->ReferenceHeight = HeightTargets[ReadIndex];
					if (FRDGTextureRef* Snapshot = HeightSnapshots.Find(Layer.HeightReferenceLayerIndex))
					{
						Parameters->ReferenceHeight = *Snapshot;
					}
					Parameters->LayerBC = RegisterTexture(
						GraphBuilder,
						RegisteredTextures,
						Layer.BaseColor,
						TEXT("Mixtormat.LayerBC"));
					Parameters->LayerN = RegisterTexture(
						GraphBuilder,
						RegisteredTextures,
						Layer.Normal,
						TEXT("Mixtormat.LayerN"));
					Parameters->LayerRAM = RegisterTexture(
						GraphBuilder,
						RegisteredTextures,
						Layer.RAM,
						TEXT("Mixtormat.LayerRAM"));
					Parameters->LayerMask = CombinedMask;
					Parameters->EffectData = CombinedEffectData;
					Parameters->EffectHeight = CombinedEffectHeight;
					Parameters->StainData = CombinedStainData;
					Parameters->DebugMask = DebugMask;
					Parameters->LinearWrapSampler = TStaticSamplerState<SF_AnisotropicLinear, AM_Wrap, AM_Wrap, AM_Wrap, 0, 4>::GetRHI();
					Parameters->OutputBC = GraphBuilder.CreateUAV(OutputBC[WriteIndex]);
					Parameters->OutputN = GraphBuilder.CreateUAV(OutputN[WriteIndex]);
					Parameters->OutputRAM = GraphBuilder.CreateUAV(OutputRAM[WriteIndex]);
					Parameters->OutputHeight = GraphBuilder.CreateUAV(HeightTargets[WriteIndex]);
					Parameters->OutputDebug = GraphBuilder.CreateUAV(OutputDebug[Request.PublishedTargetIndex]);

					FComputeShaderUtils::AddPass(
						GraphBuilder,
						RDG_EVENT_NAME("Mixtormat.Composite.Layer%d", LayerIndex),
						Shader,
						Parameters,
						FIntVector(
							FMath::DivideAndRoundUp(Request.Resolution.X, 8),
							FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
							1));

					// Amount 0 is an exact identity in the erosion shader: Placement falls to
					// zero, the height comes back as it went in and the normal is copied
					// through. It was still costing the full filter -- six passes at twice the
					// composition resolution, so four times the pixels, plus the resample pair
					// -- which is the single most expensive thing a material could carry while
					// doing nothing at all. An erosion node parked at 0, or a mask that has
					// faded it out, now costs one clear.
					//
					// The clear is not an optimisation detail, it is what makes the skip exact:
					// at Amount 0 the filter writes a ridge of zero, so a layer that skipped it
					// and copied the previous ridge forward instead would hand the next layer a
					// different signal from the one it gets today.
					const bool bErosionActive =
						PendingErosion != nullptr && PendingErosion->ErosionAmount > 0.0f;

					// Every layer hands the next one a ridge, whether or not it erodes. A layer
					// that left the slot alone would pass on the ridge from two layers back,
					// which reads as the mask signal being correct on some layers and stale on
					// others. Eroding layers overwrite this below.
					if (!PendingErosion)
					{
						AddCopyTexturePass(
							GraphBuilder,
							RidgeTargets[1 - WriteIndex],
							RidgeTargets[WriteIndex]);
					}
					else if (!bErosionActive)
					{
						AddClearUAVPass(
							GraphBuilder,
							GraphBuilder.CreateUAV(RidgeTargets[WriteIndex]),
							FVector4f(0.0f));
					}

					// Erosion filters the layer output: it reads the height and normal this
					// layer just composited, carves the height, derives the normal change from
					// what it removed, and writes both back.
					if (bErosionActive)
					{
						const FEffectRenderData& Ero = *PendingErosion;
						FRDGTextureRef ErosionPlacementMask = Ero.ErosionPlacementMask.IsValid()
							? RegisterTexture(
								GraphBuilder,
								RegisteredTextures,
								Ero.ErosionPlacementMask,
								TEXT("Mixtormat.ErosionPlacementMask"))
							: PeelFieldDummy;

						// Erosion runs at twice the composition resolution, capped at 4096,
						// then resamples back. Carving is high-frequency work: at composition
						// resolution the octave loop hits the two-pixels-per-cell floor with
						// passes still to run, so the finest gullies have nowhere to cut.
						// Above 4096 the cost stops buying visible detail.
						const FIntPoint EroRes(
							FMath::Min(Request.Resolution.X * 2, 4096),
							FMath::Min(Request.Resolution.Y * 2, 4096));
						const bool bResample = EroRes != Request.Resolution;

						// The height chain is R32F, not R16F like the rest of the compositor.
						// Every quantity this filter derives is a difference of two nearly
						// equal heights, and half floats do not survive that.
						//
						// A half around mid height has a ULP of 2^-11, about 4.9e-4. The slope
						// Sobel sums six taps and scales by Res/(8R) -- 128 at 2K with radius 2
						// -- so quantisation alone puts roughly 0.25 of noise on a slope the
						// repose gate thresholds at 0.30 with a 0.25 transition. The gate is
						// then close to a coin flip per pixel and it multiplies the carve, so
						// the height comes out dithered before the normal pass amplifies
						// anything. Slope Blur cannot help: the blur averages correctly and the
						// R16F write throws the result straight back to one ULP.
						//
						// The normal pass is the second victim: it differences the carve depth
						// between neighbours, and those differences are far smaller than the
						// carve itself, so they land on nought, one or two ULP -- a handful of
						// distinct slopes over the whole carve. The Hessian is the third, since
						// a second difference divided by StepUV squared multiplies its error by
						// about a million.
						//
						// EroGuide has to be R32F for the same reason as the rest: it is what
						// the slope and curvature stencils actually read.
						const FRDGTextureDesc EroDesc = FRDGTextureDesc::Create2D(
							EroRes,
							PF_R32_FLOAT,
							FClearValueBinding::White,
							TexCreate_ShaderResource | TexCreate_UAV);

						// The ridge map is a 0..1 signal that is never differenced, so it keeps
						// the cheaper format.
						const FRDGTextureDesc EroRidgeDesc = FRDGTextureDesc::Create2D(
							EroRes,
							PF_R16F,
							FClearValueBinding::White,
							TexCreate_ShaderResource | TexCreate_UAV);
						FRDGTextureDesc EroNormalDesc = OutputN[WriteIndex]->Desc;
						EroNormalDesc.Extent = EroRes;

						FRDGTextureRef SourceH = GraphBuilder.CreateTexture(EroDesc, TEXT("Mixtormat.ErosionSrc"));
						FRDGTextureRef EroH[2] = {
							GraphBuilder.CreateTexture(EroDesc, TEXT("Mixtormat.ErosionA")),
							GraphBuilder.CreateTexture(EroDesc, TEXT("Mixtormat.ErosionB"))};
						FRDGTextureRef EroRidge = GraphBuilder.CreateTexture(EroRidgeDesc, TEXT("Mixtormat.ErosionRidge"));
						FRDGTextureRef EroGuide = GraphBuilder.CreateTexture(EroDesc, TEXT("Mixtormat.ErosionGuide"));

						// The horizontal half of the separable slope blur. Allocated here with
						// the rest rather than per pass: it is another full erosion-resolution
						// R32F transient, 64MB at the 4096 cap.
						FRDGTextureRef EroGuideX = GraphBuilder.CreateTexture(
							EroDesc, TEXT("Mixtormat.ErosionGuideX"));
						FRDGTextureRef EroN = GraphBuilder.CreateTexture(EroNormalDesc, TEXT("Mixtormat.ErosionN"));
						// The layer normal every carving pass reads, lifted to erosion resolution.
						FRDGTextureRef EroSrcN = GraphBuilder.CreateTexture(EroNormalDesc, TEXT("Mixtormat.ErosionSrcN"));

						AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(EroRidge), FVector4f(0.0f, 0.0f, 0.0f, 0.0f));

						// Stands in at both ends of the ridge plumbing: the UAV slot on the
						// upsample, which must not be aimed at a composition-res target from an
						// erosion-res dispatch, and the SRV slot on every carving and blur pass,
						// which cannot read EroRidge because those passes write it.
						//
						// Cleared rather than left alone: RDG rejects a read of a transient
						// texture nothing has written, and it is now read as well as bound.
						FRDGTextureRef ResampleRidgeDummy = GraphBuilder.CreateTexture(
							FRDGTextureDesc::Create2D(
								FIntPoint(1, 1),
								PF_R16F,
								FClearValueBinding::White,
								TexCreate_ShaderResource | TexCreate_UAV),
							TEXT("Mixtormat.ErosionRidgeDummy"));
						AddClearUAVPass(
							GraphBuilder, GraphBuilder.CreateUAV(ResampleRidgeDummy), FVector4f(0.0f));

						// One dispatch moves height and normal together, in either direction.
						auto AddErosionResample = [&](
							FRDGTextureRef InH,
							FRDGTextureRef InN,
							FRDGTextureRef OutH,
							FRDGTextureRef OutN,
							FRDGTextureRef InRidge,
							FRDGTextureRef OutRidgeTarget,
							const FIntPoint DestRes,
							const TCHAR* DebugName)
						{
							// A ridge target only on the way down. On the way up the dispatch
							// runs at erosion resolution and the ridge slot holds the 1x1
							// dummy, so the shader's write is gated off rather than aimed at
							// a target it would overrun.
							const bool bCarryRidge = OutRidgeTarget != nullptr;

							FMixtormatErosionCS::FParameters* RP =
								GraphBuilder.AllocParameters<FMixtormatErosionCS::FParameters>();
							RP->OutputSize = DestRes;
							RP->NormalPass = 0;
							RP->BlurPass = 0;
							RP->BlurAxis = 0;
							RP->ResamplePass = 1;
							RP->ResampleRidge = bCarryRidge ? 1 : 0;
							RP->PreviousRidge = InRidge;
							RP->PreviousHeight = InH;
							RP->SourceHeight = InH;
							RP->GuideHeight = InH;
							RP->LayerMask = CombinedMask;
							RP->UsePlacementMask = Ero.ErosionPlacementMask.IsValid() ? 1u : 0u;
							RP->PlacementMaskTiling = Ero.ErosionMaskTiling;
							RP->PlacementMaskTexture = ErosionPlacementMask;
							RP->PreviousNormal = InN;
							RP->LinearWrapSampler =
								TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
							RP->OutputHeight = GraphBuilder.CreateUAV(OutH);
							RP->OutputRidge = GraphBuilder.CreateUAV(
								bCarryRidge ? OutRidgeTarget : ResampleRidgeDummy);
							RP->OutputNormal = GraphBuilder.CreateUAV(OutN);
							FComputeShaderUtils::AddPass(
								GraphBuilder,
								RDG_EVENT_NAME("Mixtormat.Erosion.L%d.%s", LayerIndex, DebugName),
								ErosionShader,
								RP,
								FIntVector(
									FMath::DivideAndRoundUp(DestRes.X, 8),
									FMath::DivideAndRoundUp(DestRes.Y, 8),
									1));
						};

						if (bResample)
						{
							AddErosionResample(
								HeightTargets[WriteIndex], OutputN[WriteIndex],
								SourceH, EroSrcN,
								EroRidge, nullptr,
								EroRes, TEXT("Up"));
						}
						else
						{
							AddCopyTexturePass(GraphBuilder, HeightTargets[WriteIndex], SourceH);
							AddCopyTexturePass(GraphBuilder, OutputN[WriteIndex], EroSrcN);
						}

						// All octaves are evaluated together, so steering is analytical and no pass
						// can feed a masked boundary or quantized intermediate back into the next band.
						auto SetErosionParameters = [&](FMixtormatErosionCS::FParameters* Parameters)
						{
							Parameters->OutputSize = EroRes;
							Parameters->NormalPass = 0;
							Parameters->BlurPass = 0;
							Parameters->BlurAxis = 0;
							Parameters->ResamplePass = 0;
							Parameters->ResampleRidge = 0;
							Parameters->BlurRadius = Ero.ErosionSlopeBlur;
							Parameters->NormalStrength = Ero.ErosionNormalStrength;
							Parameters->Amount = Ero.ErosionAmount;
							Parameters->Strength = Ero.ErosionStrength;
							Parameters->Octaves = Ero.ErosionOctaves;
							Parameters->Period = Ero.ErosionPeriod;
							Parameters->Gain = Ero.ErosionGain;
							Parameters->Detail = Ero.ErosionDetail;
							Parameters->GullyWeight = Ero.ErosionGullyWeight;
							Parameters->Normalization = Ero.ErosionNormalization;
							Parameters->RidgeRounding = Ero.ErosionRidgeRounding;
							Parameters->CreaseRounding = Ero.ErosionCreaseRounding;
							Parameters->SlopeOnset = Ero.ErosionSlopeOnset;
							Parameters->FeatureOnset = Ero.ErosionFeatureOnset;
							Parameters->AssumedSlope = Ero.ErosionAssumedSlope;
							Parameters->AssumedSlopeAmount = Ero.ErosionAssumedSlopeAmount;
							Parameters->SlopeRadius = Ero.ErosionSlopeRadius;
							Parameters->CurvatureMode = Ero.ErosionCurvatureMode;
							Parameters->CavityInfluence = Ero.ErosionCavityInfluence;
							Parameters->CavityOffset = Ero.ErosionCavityOffset;
							Parameters->CavityRemapMin = Ero.ErosionCavityRemapMin;
							Parameters->CavityRemapMax = Ero.ErosionCavityRemapMax;
							Parameters->HeightInfluence = Ero.ErosionHeightInfluence;
							Parameters->HeightScale = Ero.ErosionHeightScale;
							Parameters->UsePlacementMask = Ero.ErosionPlacementMask.IsValid() ? 1u : 0u;
							Parameters->PlacementMaskTiling = Ero.ErosionMaskTiling;
							Parameters->InvertMask = Ero.bErosionInvertMask ? 1u : 0u;
							Parameters->Seed = 1u;
							Parameters->SourceHeight = SourceH;
							Parameters->PreviousNormal = EroSrcN;
							Parameters->LayerMask = CombinedMask;
							Parameters->PlacementMaskTexture = ErosionPlacementMask;
							Parameters->PreviousRidge = ResampleRidgeDummy;
							Parameters->LinearWrapSampler =
								TStaticSamplerState<SF_AnisotropicLinear, AM_Wrap, AM_Wrap, AM_Wrap, 0, 4>::GetRHI();
							Parameters->OutputRidge = GraphBuilder.CreateUAV(EroRidge);
							Parameters->OutputNormal = GraphBuilder.CreateUAV(EroN);
						};

						const FIntVector ErosionGroups(
							FMath::DivideAndRoundUp(EroRes.X, 8),
							FMath::DivideAndRoundUp(EroRes.Y, 8),
							1);

						FRDGTextureRef Guidance = SourceH;
						if (Ero.ErosionSlopeBlur > 0.0f)
						{
							FMixtormatErosionCS::FParameters* BlurX =
								GraphBuilder.AllocParameters<FMixtormatErosionCS::FParameters>();
							SetErosionParameters(BlurX);
							BlurX->BlurPass = 1;
							BlurX->BlurAxis = 0;
							BlurX->PreviousHeight = SourceH;
							BlurX->GuideHeight = SourceH;
							BlurX->OutputHeight = GraphBuilder.CreateUAV(EroGuideX);
							FComputeShaderUtils::AddPass(
								GraphBuilder,
								RDG_EVENT_NAME("Mixtormat.Erosion.L%d.BlurX", LayerIndex),
								ErosionShader,
								BlurX,
								ErosionGroups);

							FMixtormatErosionCS::FParameters* BlurY =
								GraphBuilder.AllocParameters<FMixtormatErosionCS::FParameters>();
							*BlurY = *BlurX;
							BlurY->BlurAxis = 1;
							BlurY->PreviousHeight = EroGuideX;
							BlurY->OutputHeight = GraphBuilder.CreateUAV(EroGuide);
							FComputeShaderUtils::AddPass(
								GraphBuilder,
								RDG_EVENT_NAME("Mixtormat.Erosion.L%d.BlurY", LayerIndex),
								ErosionShader,
								BlurY,
								ErosionGroups);
							Guidance = EroGuide;
						}

						FMixtormatErosionCS::FParameters* ErosionParameters =
							GraphBuilder.AllocParameters<FMixtormatErosionCS::FParameters>();
						SetErosionParameters(ErosionParameters);
						ErosionParameters->PreviousHeight = SourceH;
						ErosionParameters->GuideHeight = Guidance;
						ErosionParameters->OutputHeight = GraphBuilder.CreateUAV(EroH[0]);
						FComputeShaderUtils::AddPass(
							GraphBuilder,
							RDG_EVENT_NAME("Mixtormat.Erosion.L%d.Filter", LayerIndex),
							ErosionShader,
							ErosionParameters,
							ErosionGroups);

						FMixtormatErosionCS::FParameters* NormalParameters =
							GraphBuilder.AllocParameters<FMixtormatErosionCS::FParameters>();
						SetErosionParameters(NormalParameters);
						NormalParameters->NormalPass = 1;
						NormalParameters->PreviousHeight = EroH[0];
						NormalParameters->GuideHeight = Guidance;
						NormalParameters->OutputHeight = GraphBuilder.CreateUAV(EroH[1]);
						FComputeShaderUtils::AddPass(
							GraphBuilder,
							RDG_EVENT_NAME("Mixtormat.Erosion.L%d.Normal", LayerIndex),
							ErosionShader,
							NormalParameters,
							ErosionGroups);

						FRDGTextureRef Result = EroH[1];

						if (bResample)
						{
							AddErosionResample(
								Result, EroN,
								HeightTargets[WriteIndex], OutputN[WriteIndex],
								EroRidge, RidgeTargets[WriteIndex],
								Request.Resolution, TEXT("Down"));
						}
						else
						{
							AddCopyTexturePass(GraphBuilder, Result, HeightTargets[WriteIndex]);
							AddCopyTexturePass(GraphBuilder, EroN, OutputN[WriteIndex]);
							AddCopyTexturePass(GraphBuilder, EroRidge, RidgeTargets[WriteIndex]);
						}

						// Colour and roughness for what was carved. Skipped when neither amount
						// asks for anything, so the common case pays nothing.
						if (Ero.ErosionColorAmount != 0.0f || Ero.ErosionRoughnessAmount != 0.0f)
						{
							// Through scratch and back rather than in place: the pass reads the
							// base colour and RAM the composite just wrote and writes the same
							// two targets, which cannot be bound as SRV and UAV at once. The
							// other ping-pong slot is dead at this point and could be borrowed,
							// but that is a bet on the slot arithmetic staying as it is, and a
							// wrong bet would only show on some layers.
							FRDGTextureRef ShadeBC = GraphBuilder.CreateTexture(
								OutputBC[WriteIndex]->Desc, TEXT("Mixtormat.ErosionShadeBC"));
							FRDGTextureRef ShadeRAM = GraphBuilder.CreateTexture(
								OutputRAM[WriteIndex]->Desc, TEXT("Mixtormat.ErosionShadeRAM"));

							FMixtormatCarveShadeCS::FParameters* SP =
								GraphBuilder.AllocParameters<FMixtormatCarveShadeCS::FParameters>();
							SP->OutputSize = Request.Resolution;
							SP->ErodedColor = FVector4f(
								Ero.ErosionColor.R,
								Ero.ErosionColor.G,
								Ero.ErosionColor.B,
								Ero.ErosionColor.A);
							SP->ColorAmount = Ero.ErosionColorAmount;
							SP->RoughnessAmount = Ero.ErosionRoughnessAmount;
							SP->CarveDepth = Ero.ErosionCarveDepth;

							// Erosion recovers coverage from the height pair, so the coverage
							// slot is unread here. Bound to SourceH because it is already a
							// valid single-channel texture in this scope: a dedicated dummy
							// would be another resource to create, clear and keep correct for
							// a slot the shader never touches on this path.
							SP->UseCoverageTexture = 0;
							SP->CoverageTexture = SourceH;

							// The carve is still the difference of these two, at erosion
							// resolution, and the pass samples them by UV. Nothing had to be
							// copied aside before the resample overwrote the composited height.
							SP->SourceHeight = SourceH;
							SP->CarvedHeight = Result;
							SP->SourceColor = OutputBC[WriteIndex];
							SP->SourceRAM = OutputRAM[WriteIndex];
							SP->LinearWrapSampler =
								TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
							SP->OutputColor = GraphBuilder.CreateUAV(ShadeBC);
							SP->OutputRAM = GraphBuilder.CreateUAV(ShadeRAM);

							FComputeShaderUtils::AddPass(
								GraphBuilder,
								RDG_EVENT_NAME("Mixtormat.Erosion.L%d.Shade", LayerIndex),
								CarveShadeShader,
								SP,
								FIntVector(
									FMath::DivideAndRoundUp(Request.Resolution.X, 8),
									FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
									1));

							AddCopyTexturePass(GraphBuilder, ShadeBC, OutputBC[WriteIndex]);
							AddCopyTexturePass(GraphBuilder, ShadeRAM, OutputRAM[WriteIndex]);
						}
					}

					// Craquelure relief runs after erosion but before chipping. Chipping selects
					// from the current height and its cavity, so it must see cracks already carved
					// into the layer rather than the flat height that preceded them.
					for (int32 ReliefIndex = 0; ReliefIndex < PendingCraquelureReliefs.Num(); ++ReliefIndex)
					{
						const FPendingCraquelureRelief& Relief = PendingCraquelureReliefs[ReliefIndex];
						FRDGTextureRef ReliefH = GraphBuilder.CreateTexture(
							HeightTargets[WriteIndex]->Desc, TEXT("Mixtormat.CraqReliefH"));
						FRDGTextureRef ReliefN = GraphBuilder.CreateTexture(
							OutputN[WriteIndex]->Desc, TEXT("Mixtormat.CraqReliefN"));

						FMixtormatCraquelureReliefCS::FParameters* RelP =
							GraphBuilder.AllocParameters<FMixtormatCraquelureReliefCS::FParameters>();
						RelP->OutputSize = Request.Resolution;
						RelP->HeightWeight = Relief.HeightWeight;
						RelP->NormalWeight = Relief.NormalWeight;
						RelP->ReliefWidthPixels = Relief.WidthPixels;
						RelP->Variation = Relief.Variation;
						RelP->Profile = Relief.Profile;
						RelP->CrackDistance = Relief.Distance;
						RelP->SourceHeight = HeightTargets[WriteIndex];
						RelP->PreviousNormal = OutputN[WriteIndex];
						RelP->OutputHeight = GraphBuilder.CreateUAV(ReliefH);
						RelP->OutputNormal = GraphBuilder.CreateUAV(ReliefN);

						FComputeShaderUtils::AddPass(
							GraphBuilder,
							RDG_EVENT_NAME(
								"Mixtormat.Craquelure.Relief.L%d.%d", LayerIndex, ReliefIndex),
							CraquelureReliefShader,
							RelP,
							FIntVector(
								FMath::DivideAndRoundUp(Request.Resolution.X, 8),
								FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
								1));

						AddCopyTexturePass(GraphBuilder, ReliefH, HeightTargets[WriteIndex]);
						AddCopyTexturePass(GraphBuilder, ReliefN, OutputN[WriteIndex]);
					}

					// Chipping filters the layer output the same way erosion does, after both
					// erosion and craquelure have finished shaping the height it selects from.
					// Amount 0 seeds nothing, so it should also cost nothing rather than run
					// the iteration loop to produce an unchanged height.
					if (PendingChipping && PendingChipping->ChipAmount > 0.0f)
					{
						const FEffectRenderData& Chip = *PendingChipping;
						FRDGTextureRef ChippingPlacementMask = Chip.ChipPlacementMask.IsValid()
							? RegisterTexture(
								GraphBuilder,
								RegisteredTextures,
								Chip.ChipPlacementMask,
								TEXT("Mixtormat.ChippingPlacementMask"))
							: PeelFieldDummy;

						// A chip advances one pixel per iteration, so the authored count is a
						// reach in pixels. Scaled against a 1024 reference so a 512 preview and
						// a 2048 export show the same chip size rather than the same pixel
						// count -- otherwise the preview lies about the result.
						//
						// The obvious alternative, a dilated 3x3 gather at stride N, is cheaper
						// and wrong: at stride 2 the four pixel-parity classes never read each
						// other, so it produces four interleaved chip networks instead of one.
						//
						// This is the one filter whose dispatch count scales with output size.
						// At 4K with Iterations 24 the clamp binds at 96 full-resolution passes,
						// which is where a slow export will be coming from.
						const int32 ChipIterations = FMath::Clamp(
							FMath::RoundToInt(
								Chip.ChipIterations *
								FMath::Max(Request.Resolution.X, Request.Resolution.Y) / 1024.0f),
							1,
							96);

						// The state is (core, tip, dirX, dirY) at full float, not half, for the
						// reason the erosion height chain is R32F. Tip is a geometric decay --
						// multiplied by 0.72..0.99 every iteration, read back, re-multiplied --
						// and tested against a hard 0.001 cutoff, so half-float quantisation
						// near that cutoff turns a chip stopping into a per-pixel coin flip.
						// The stored direction is worse: it is renormalised every pass and fed
						// to a hard alignment test at dot > -0.10.
						const FRDGTextureDesc ChipStateDesc = FRDGTextureDesc::Create2D(
							Request.Resolution,
							PF_A32B32G32R32F,
							FClearValueBinding::Black,
							TexCreate_ShaderResource | TexCreate_UAV);
						const FRDGTextureDesc ChipMaskDesc = FRDGTextureDesc::Create2D(
							Request.Resolution,
							PF_R16F,
							FClearValueBinding::Black,
							TexCreate_ShaderResource | TexCreate_UAV);

						FRDGTextureRef ChipState[2] = {
							GraphBuilder.CreateTexture(ChipStateDesc, TEXT("Mixtormat.ChipStateA")),
							GraphBuilder.CreateTexture(ChipStateDesc, TEXT("Mixtormat.ChipStateB"))};

						// The chip mask ping-pongs for the same reason the state does: the
						// normal pass and the shade pass both read it, and a pass cannot write
						// the texture it is reading.
						FRDGTextureRef ChipMask[2] = {
							GraphBuilder.CreateTexture(ChipMaskDesc, TEXT("Mixtormat.ChipMaskA")),
							GraphBuilder.CreateTexture(ChipMaskDesc, TEXT("Mixtormat.ChipMaskB"))};

						// The height the layer composited, held aside. Every iteration reads
						// this rather than its own output, matching the read-only height bind in
						// the prototype: a chip must not be able to carve its own brick down
						// into grout and so stop itself.
						FRDGTextureRef ChipSourceH = GraphBuilder.CreateTexture(
							HeightTargets[WriteIndex]->Desc, TEXT("Mixtormat.ChipSourceH"));
						AddCopyTexturePass(GraphBuilder, HeightTargets[WriteIndex], ChipSourceH);

						// The extent of that height, folded to a single texel. Every brick/grout
						// decision in the filter is taken on the height remapped through this
						// pair rather than on the composited value itself.
						//
						// Without it Grout Level is an absolute threshold on a target that is
						// cleared to 0.5, so at its own default it sits exactly on the clear
						// value, BrickMask comes out identically zero across the image, and the
						// filter -- every term of which is multiplied by that mask -- returns its
						// input unchanged. That is the whole reason chipping showed nothing.
						//
						// Folded on the GPU and consumed as a texture rather than read back:
						// this runs inside the same graph as the passes that use it, and a
						// readback here would stall the frame to move eight bytes.
						FRDGTextureRef ChipHeightRange = nullptr;
						{
							const FRDGTextureDesc RangeDesc1x1 = FRDGTextureDesc::Create2D(
								FIntPoint(1, 1),
								PF_A32B32G32R32F,
								FClearValueBinding::Black,
								TexCreate_ShaderResource | TexCreate_UAV);

							FIntPoint ReduceSize = Request.Resolution;
							FRDGTextureRef ReduceSource = nullptr;
							int32 ReducePass = 0;
							while (ReduceSource == nullptr || ReduceSize != FIntPoint(1, 1))
							{
								const FIntPoint NextSize(
									FMath::DivideAndRoundUp(ReduceSize.X, GMixtormatReduceFactor),
									FMath::DivideAndRoundUp(ReduceSize.Y, GMixtormatReduceFactor));

								const FRDGTextureDesc StepDesc = FRDGTextureDesc::Create2D(
									NextSize,
									PF_A32B32G32R32F,
									FClearValueBinding::Black,
									TexCreate_ShaderResource | TexCreate_UAV);
								FRDGTextureRef StepTarget = GraphBuilder.CreateTexture(
									StepDesc, TEXT("Mixtormat.ChipHeightRange"));

								FMixtormatReduceMinMaxCS::FParameters* RP =
									GraphBuilder.AllocParameters<FMixtormatReduceMinMaxCS::FParameters>();
								RP->InputSize = ReduceSize;
								RP->OutputSize = NextSize;
								RP->FirstPass = ReducePass == 0 ? 1 : 0;
								RP->SourceHeight = ChipSourceH;

								// Bound on every pass because the struct requires it and unread on
								// the first, where the chain has produced nothing yet. Aiming it at
								// the height would bind an R16F single-channel texture to a float4
								// slot; the 1x1 is the cheapest thing of the right shape, and RDG
								// rejects a transient nothing has written, so it is cleared.
								if (ReducePass == 0)
								{
									FRDGTextureRef RangeDummy = GraphBuilder.CreateTexture(
										RangeDesc1x1, TEXT("Mixtormat.ChipRangeDummy"));
									AddClearUAVPass(
										GraphBuilder,
										GraphBuilder.CreateUAV(RangeDummy),
										FVector4f(0.0f));
									RP->SourceRange = RangeDummy;
								}
								else
								{
									RP->SourceRange = ReduceSource;
								}
								RP->OutputRange = GraphBuilder.CreateUAV(StepTarget);

								FComputeShaderUtils::AddPass(
									GraphBuilder,
									RDG_EVENT_NAME(
										"Mixtormat.Chipping.L%d.HeightRange%d", LayerIndex, ReducePass),
									ReduceMinMaxShader,
									RP,
									FIntVector(
										FMath::DivideAndRoundUp(NextSize.X, 8),
										FMath::DivideAndRoundUp(NextSize.Y, 8),
										1));

								ReduceSource = StepTarget;
								ReduceSize = NextSize;
								++ReducePass;
							}
							ChipHeightRange = ReduceSource;
						}

						// Scratch for the normal pass, which reads the composited normal and
						// writes the same target.
						FRDGTextureRef ChipNormalScratch = GraphBuilder.CreateTexture(
							OutputN[WriteIndex]->Desc, TEXT("Mixtormat.ChipNormalScratch"));

						AddClearUAVPass(
							GraphBuilder, GraphBuilder.CreateUAV(ChipState[0]), FVector4f(0.0f));
						AddClearUAVPass(
							GraphBuilder, GraphBuilder.CreateUAV(ChipState[1]), FVector4f(0.0f));
						AddClearUAVPass(
							GraphBuilder, GraphBuilder.CreateUAV(ChipMask[0]), FVector4f(0.0f));
						AddClearUAVPass(
							GraphBuilder, GraphBuilder.CreateUAV(ChipMask[1]), FVector4f(0.0f));

						const FIntVector ChipGroups(
							FMath::DivideAndRoundUp(Request.Resolution.X, 8),
							FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
							1);

						auto FillChipParameters = [&](FMixtormatChippingCS::FParameters* P)
						{
							P->OutputSize = Request.Resolution;
							P->GroutLevel = Chip.ChipGroutLevel;
							P->GroutSoftness = Chip.ChipGroutSoftness;
							P->ChipAmount = Chip.ChipAmount;
							P->ChipSize = Chip.ChipSize;
							P->ChipDepth = Chip.ChipDepth;
							P->Irregularity = Chip.ChipIrregularity;
							P->MaskEdge = Chip.ChipMaskEdge;
							P->NormalStrength = Chip.ChipNormalStrength;
							P->CavityInfluence = Chip.ChipCavityInfluence;
							P->CavityOffset = Chip.ChipCavityOffset;
							P->CavityRemapMin = Chip.ChipCavityRemapMin;
							P->CavityRemapMax = Chip.ChipCavityRemapMax;
							P->HeightInfluence = Chip.ChipHeightInfluence;
							P->HeightScale = Chip.ChipHeightScale;
							P->UsePlacementMask = Chip.ChipPlacementMask.IsValid() ? 1u : 0u;
							P->PlacementMaskTiling = Chip.ChipMaskTiling;
							P->InvertMask = Chip.bChipInvertMask ? 1u : 0u;
							P->Seed = Chip.ChipSeed;
							P->SourceHeight = ChipSourceH;
							P->HeightRange = ChipHeightRange;
							P->LayerMask = CombinedMask;
							P->PlacementMaskTexture = ChippingPlacementMask;
							P->LinearWrapSampler =
								TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
						};

						int32 ChipWrite = 0;
						for (int32 PassIndex = 0; PassIndex < ChipIterations; ++PassIndex)
						{
							ChipWrite = PassIndex & 1;
							const int32 ChipRead = 1 - ChipWrite;

							FMixtormatChippingCS::FParameters* CP =
								GraphBuilder.AllocParameters<FMixtormatChippingCS::FParameters>();
							FillChipParameters(CP);
							CP->Iteration = PassIndex;
							CP->NormalPass = 0;
							CP->PreviousState = ChipState[ChipRead];
							CP->ChipsTexture = ChipMask[ChipRead];
							CP->PreviousNormal = OutputN[WriteIndex];
							CP->OutputState = GraphBuilder.CreateUAV(ChipState[ChipWrite]);
							CP->OutputChips = GraphBuilder.CreateUAV(ChipMask[ChipWrite]);
							CP->OutputHeight = GraphBuilder.CreateUAV(HeightTargets[WriteIndex]);
							CP->OutputNormal = GraphBuilder.CreateUAV(ChipNormalScratch);

							FComputeShaderUtils::AddPass(
								GraphBuilder,
								RDG_EVENT_NAME("Mixtormat.Chipping.L%d.P%d", LayerIndex, PassIndex),
								ChippingShader,
								CP,
								ChipGroups);
						}

						FRDGTextureRef FinalChips = ChipMask[ChipWrite];
						FRDGTextureRef SpareChips = ChipMask[1 - ChipWrite];

						// Normal from the finished chip mask. Through scratch and back because
						// the pass reads the composited normal and writes the same target, which
						// cannot be SRV and UAV in one dispatch.
						{
							FMixtormatChippingCS::FParameters* NP =
								GraphBuilder.AllocParameters<FMixtormatChippingCS::FParameters>();
							FillChipParameters(NP);
							NP->Iteration = ChipIterations;
							NP->NormalPass = 1;
							NP->PreviousState = ChipState[ChipWrite];
							NP->ChipsTexture = FinalChips;
							NP->PreviousNormal = OutputN[WriteIndex];

							// Bound and unwritten on this path: the normal pass returns before it
							// touches state, chips or height. State and chips are aimed at the
							// spare ping-pong slots so a future edit that stops returning early
							// cannot corrupt what the loop just produced.
							//
							// Height goes to the live target rather than to a spare, because
							// the alternative -- ChipSourceH -- is bound as SourceHeight on
							// this same dispatch, and RDG will not take one texture as SRV and
							// UAV at once. It is safe under the same hypothetical: an edit that
							// removed the early return would write the value the last loop pass
							// already wrote there.
							NP->OutputState = GraphBuilder.CreateUAV(ChipState[1 - ChipWrite]);
							NP->OutputChips = GraphBuilder.CreateUAV(SpareChips);
							NP->OutputHeight = GraphBuilder.CreateUAV(HeightTargets[WriteIndex]);
							NP->OutputNormal = GraphBuilder.CreateUAV(ChipNormalScratch);

							FComputeShaderUtils::AddPass(
								GraphBuilder,
								RDG_EVENT_NAME("Mixtormat.Chipping.L%d.Normal", LayerIndex),
								ChippingShader,
								NP,
								ChipGroups);

							AddCopyTexturePass(GraphBuilder, ChipNormalScratch, OutputN[WriteIndex]);
						}

						// Colour and roughness for what was chipped. Coverage is the chip mask
						// itself rather than a height difference, so it still reads correctly at
						// Depth 0, where a difference would be nothing.
						if (Chip.ChipColorAmount != 0.0f || Chip.ChipRoughnessAmount != 0.0f)
						{
							FRDGTextureRef ShadeBC = GraphBuilder.CreateTexture(
								OutputBC[WriteIndex]->Desc, TEXT("Mixtormat.ChipShadeBC"));
							FRDGTextureRef ShadeRAM = GraphBuilder.CreateTexture(
								OutputRAM[WriteIndex]->Desc, TEXT("Mixtormat.ChipShadeRAM"));

							FMixtormatCarveShadeCS::FParameters* SP =
								GraphBuilder.AllocParameters<FMixtormatCarveShadeCS::FParameters>();
							SP->OutputSize = Request.Resolution;
							SP->ErodedColor = FVector4f(
								Chip.ChipColor.R,
								Chip.ChipColor.G,
								Chip.ChipColor.B,
								Chip.ChipColor.A);
							SP->ColorAmount = Chip.ChipColorAmount;
							SP->RoughnessAmount = Chip.ChipRoughnessAmount;

							// Unused on the coverage-texture path, which needs no normalising
							// divisor because the chip mask is already 0..1.
							SP->CarveDepth = 1.0f;
							SP->UseCoverageTexture = 1;
							SP->CoverageTexture = FinalChips;

							// Bound because the struct requires them, unread on this path.
							SP->SourceHeight = ChipSourceH;
							SP->CarvedHeight = ChipSourceH;

							SP->SourceColor = OutputBC[WriteIndex];
							SP->SourceRAM = OutputRAM[WriteIndex];
							SP->LinearWrapSampler =
								TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
							SP->OutputColor = GraphBuilder.CreateUAV(ShadeBC);
							SP->OutputRAM = GraphBuilder.CreateUAV(ShadeRAM);

							FComputeShaderUtils::AddPass(
								GraphBuilder,
								RDG_EVENT_NAME("Mixtormat.Chipping.L%d.Shade", LayerIndex),
								CarveShadeShader,
								SP,
								ChipGroups);

							AddCopyTexturePass(GraphBuilder, ShadeBC, OutputBC[WriteIndex]);
							AddCopyTexturePass(GraphBuilder, ShadeRAM, OutputRAM[WriteIndex]);
						}
					}


					// Grade runs after erosion, so on a layer carrying both it grades the
					// surface erosion has already shaded rather than the one it was about to.
					// That is the order the panel lists them in and the order a grade wants:
					// last, over the finished result.
					for (int32 GradeIndex = 0; GradeIndex < PendingGrades.Num(); ++GradeIndex)
					{
						const FEffectRenderData& Grade = *PendingGrades[GradeIndex];

						// The shader states its own Filter contract: at Amount 0 it returns
						// exactly what it read. Honour it here rather than paying a
						// full-resolution pass and a full-resolution copy to reproduce the input.
						if (Grade.GradeAmount == 0.0f)
						{
							continue;
						}

						// Through scratch and back, for the same reason the erosion shade pass
						// is: one texture cannot be SRV and UAV in the same dispatch. Stacked
						// grades chain through it, each reading what the last wrote.
						FRDGTextureRef GradedBC = GraphBuilder.CreateTexture(
							OutputBC[WriteIndex]->Desc, TEXT("Mixtormat.GradeBC"));

						FMixtormatGradeCS::FParameters* GP =
							GraphBuilder.AllocParameters<FMixtormatGradeCS::FParameters>();
						GP->OutputSize = Request.Resolution;
						GP->HasMask = Layer.bHasMask ? 1u : 0u;
						GP->InvertMask = Grade.bGradeInvertMask ? 1u : 0u;
						GP->TonemapMode = Grade.GradeTonemap;
						GP->TonemapStrength = Grade.GradeTonemapStrength;
						GP->Brightness = Grade.GradeBrightness;
						GP->Contrast = Grade.GradeContrast;
						GP->ContrastPivot = Grade.GradeContrastPivot;
						GP->Gamma = Grade.GradeGamma;
						GP->Amount = Grade.GradeAmount;
						GP->SourceColor = OutputBC[WriteIndex];

						// The layer's own accumulated child mask, which is what makes this an
						// adjustment layer rather than a whole-surface grade.
						GP->LayerMask = CombinedMask;
						GP->LinearWrapSampler =
							TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
						GP->OutputColor = GraphBuilder.CreateUAV(GradedBC);

						FComputeShaderUtils::AddPass(
							GraphBuilder,
							RDG_EVENT_NAME("Mixtormat.Grade.Layer%d.%d", LayerIndex, GradeIndex),
							GradeShader,
							GP,
							FIntVector(
								FMath::DivideAndRoundUp(Request.Resolution.X, 8),
								FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
								1));

						AddCopyTexturePass(GraphBuilder, GradedBC, OutputBC[WriteIndex]);
					}

					if (RequiredHeightSnapshots.Contains(LayerIndex))
					{
						FRDGTextureRef Snapshot = GraphBuilder.CreateTexture(
							HeightTargets[WriteIndex]->Desc,
							TEXT("Mixtormat.HeightSnapshot"));
						AddCopyTexturePass(GraphBuilder, HeightTargets[WriteIndex], Snapshot);
						HeightSnapshots.Add(LayerIndex, Snapshot);
					}
				}
			}

			GraphBuilder.SetTextureAccessFinal(
				OutputBC[Request.PublishedTargetIndex],
				ERHIAccess::SRVMask);
			GraphBuilder.SetTextureAccessFinal(
				OutputN[Request.PublishedTargetIndex],
				ERHIAccess::SRVMask);
			GraphBuilder.SetTextureAccessFinal(
				OutputRAM[Request.PublishedTargetIndex],
				ERHIAccess::SRVMask);
			GraphBuilder.SetTextureAccessFinal(
				OutputHeight[Request.PublishedTargetIndex],
				ERHIAccess::SRVMask);
			GraphBuilder.SetTextureAccessFinal(
				OutputDebug[Request.PublishedTargetIndex],
				ERHIAccess::SRVMask);
			GraphBuilder.Execute();
			if (Request.OnComplete.IsBound())
			{
				AsyncTask(
					ENamedThreads::GameThread,
					[OnComplete = Request.OnComplete]() mutable
					{
						OnComplete.ExecuteIfBound();
					});
			}
		});

	return true;
}

void FMixtormatGpuCompositor::BindOutputs(UMaterialInstanceDynamic& MaterialInstance) const
{
	MaterialInstance.SetTextureParameterValue(TEXT("ML_BaseColor"), GetBaseColorOutput());
	MaterialInstance.SetTextureParameterValue(TEXT("ML_Normal"), GetNormalOutput());
	MaterialInstance.SetTextureParameterValue(TEXT("ML_RAM"), GetRAMOutput());
	MaterialInstance.SetTextureParameterValue(TEXT("ML_Height"), GetHeightOutput());
	MaterialInstance.SetScalarParameterValue(TEXT("ML_Tiling"), 1.0f);
	MaterialInstance.SetScalarParameterValue(TEXT("ML_RoughnessBias"), 0.5f);
	MaterialInstance.SetScalarParameterValue(TEXT("ML_RoughnessContrast"), 1.0f);
	MaterialInstance.SetScalarParameterValue(TEXT("ML_RoughnessOffset"), 0.0f);
	MaterialInstance.SetScalarParameterValue(TEXT("ML_NormalIntensity"), 1.0f);
	MaterialInstance.SetScalarParameterValue(TEXT("ML_DielectricF0"), 0.04f);
	MaterialInstance.SetScalarParameterValue(TEXT("ML_UsePackedF0"), 1.0f);
}

UTextureRenderTarget2D* FMixtormatGpuCompositor::GetBaseColorOutput() const
{
	return Targets[PublishedTargetIndex].BaseColor.Get();
}

UTextureRenderTarget2D* FMixtormatGpuCompositor::GetNormalOutput() const
{
	return Targets[PublishedTargetIndex].Normal.Get();
}

UTextureRenderTarget2D* FMixtormatGpuCompositor::GetRAMOutput() const
{
	return Targets[PublishedTargetIndex].RAM.Get();
}

UTextureRenderTarget2D* FMixtormatGpuCompositor::GetHeightOutput() const
{
	return Targets[PublishedTargetIndex].Height.Get();
}

UTextureRenderTarget2D* FMixtormatGpuCompositor::GetDebugOutput() const
{
	return Targets[PublishedTargetIndex].Debug.Get();
}
