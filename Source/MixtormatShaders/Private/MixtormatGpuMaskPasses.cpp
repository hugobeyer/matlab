#include "MixtormatGpuCompositorInternal.h"

#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"

// Mask passes: the authored/published mask child, the generated mask, the colour-ID mask,
// the scoped feature masks an effect owns, and the two separable blurs the composite uses
// to round a height field.

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
	"/Plugin/Mixtormat/Private/MixtormatMask.usf",
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
	"/Plugin/Mixtormat/Private/MixtormatColorId.usf",
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
	"/Plugin/Mixtormat/Private/MixtormatGeneratedMask.usf",
	"MainCS",
	SF_Compute);

// Separable Gaussian over a layer mask. Two dispatches, one per axis.
class FMixtormatMaskBlurCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatMaskBlurCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatMaskBlurCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(int32, Axis)
		SHADER_PARAMETER(float, Radius)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceMask)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputMask)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatMaskBlurCS,
	"/Plugin/Mixtormat/Private/MixtormatMaskBlur.usf",
	"MainCS",
	SF_Compute);

namespace MixtormatGpuCompositor
{
	// Scoped masks use the same shader and controls as layer masks, but write to
	// owner-local textures. The starting point is the layer mask visible at the
	// owner's row; the result never feeds back into CombinedMask.
	//
	// Called for every effect child before the effect-type dispatch, including the ones that
	// only append to a pending list: those still need these passes emitted at that point in
	// the order, and it reads MaskPassIndex live rather than after the child that follows.
	FRDGTextureRef AddScopedFeatureMask(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const int32 OwnerSourceChildIndex)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		TMap<FRHITexture*, FRDGTextureRef>& RegisteredTextures = Ctx.RegisteredTextures;
		const int32 LayerIndex = LayerCtx.LayerIndex;
		const FRDGTextureDesc& MaskDesc = LayerCtx.MaskDesc;
		const FRDGTextureRef CombinedMask = LayerCtx.CombinedMask;
		FRDGTextureRef& DebugMask = LayerCtx.DebugMask;
		const int32 MaskPassIndex = LayerCtx.MaskPassIndex;
		TShaderMapRef<FMixtormatMaskCS> MaskShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FRDGTextureRef FeatureMask = CombinedMask;
		bool bHasScopedMask = false;
		int32 ScopedPassIndex = 0;
		for (const FChildRenderData& ScopedChild : Layer.Children)
		{
			if (ScopedChild.Type != EMixtormatLayerChildType::Mask
				|| ScopedChild.ScopeOwnerSourceChildIndex != OwnerSourceChildIndex)
			{
				continue;
			}
			const FMaskRenderData& Mask = ScopedChild.Mask;
			if (Mask.Weight == 0.0f)
			{
				continue;
			}

			FRDGTextureRef ScopedOutput = GraphBuilder.CreateTexture(
				MaskDesc, TEXT("Mixtormat.ScopedFeatureMask"));
			FMixtormatMaskCS::FParameters* MP =
				GraphBuilder.AllocParameters<FMixtormatMaskCS::FParameters>();
			MP->OutputSize = Request.Resolution;
			MP->Initialize = 0u;
			MP->BlendMode = static_cast<uint32>(Mask.BlendMode);
			MP->Invert = Mask.bInvert ? 1u : 0u;
			MP->Weight = Mask.Weight;
			MP->Tiling = Mask.Tiling;
			MP->UVOffset = Mask.UVOffset;
			MP->FlipU = Mask.bFlipU ? 1u : 0u;
			MP->FlipV = Mask.bFlipV ? 1u : 0u;
			MP->Rotation = Mask.Rotation;
			MP->Balance = Mask.Balance;
			MP->Contrast = Mask.Contrast;
			MP->Offset = Mask.Offset;
			MP->PreviousMask = FeatureMask;
			MP->IncomingMask = RegisterTexture(
				GraphBuilder, RegisteredTextures, Mask.Texture,
				TEXT("Mixtormat.ScopedIncomingMask"));
			MP->LinearWrapSampler = TStaticSamplerState<
				SF_AnisotropicLinear, AM_Wrap, AM_Wrap, AM_Wrap, 0, 4>::GetRHI();
			MP->OutputMask = GraphBuilder.CreateUAV(ScopedOutput);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME(
					"Mixtormat.ScopedMask.Layer%d.Owner%d.Pass%d",
					LayerIndex, OwnerSourceChildIndex, ScopedPassIndex),
				MaskShader,
				MP,
				FIntVector(
					FMath::DivideAndRoundUp(Request.Resolution.X, 8),
					FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
					1));
			FeatureMask = ScopedOutput;
			if (Request.DebugSettings.Mode == EMixtormatDebugPreviewMode::LayerMask
				&& Request.DebugSettings.LayerIndex == LayerIndex
				&& Request.DebugSettings.ChildIndex == ScopedChild.SourceChildIndex)
			{
				FRDGTextureRef DebugSnapshot = GraphBuilder.CreateTexture(
					MaskDesc, TEXT("Mixtormat.DebugScopedMaskSnapshot"));
				AddCopyTexturePass(GraphBuilder, FeatureMask, DebugSnapshot);
				DebugMask = DebugSnapshot;
			}
			bHasScopedMask = true;
			++ScopedPassIndex;
		}

		// Deferred owners need a stable snapshot even when they have no scoped mask,
		// because the global ping-pong target may be overwritten later in the loop.
		if (!bHasScopedMask && MaskPassIndex > 0)
		{
			FRDGTextureRef Snapshot = GraphBuilder.CreateTexture(
				MaskDesc, TEXT("Mixtormat.FeatureMaskSnapshot"));
			AddCopyTexturePass(GraphBuilder, FeatureMask, Snapshot);
			FeatureMask = Snapshot;
		}
		return FeatureMask;
	}

	// Generated masks read the surface accumulated below this layer, which is the same
	// ping-pong slot the layer composite reads.
	void AddGeneratedMaskPass(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		const int32 ChildIndex)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		FRDGTextureRef* const OutputN = Ctx.OutputN;
		FRDGTextureRef* const OutputRAM = Ctx.OutputRAM;
		FRDGTextureRef* const HeightTargets = Ctx.OutputHeight;
		const int32 LayerIndex = LayerCtx.LayerIndex;
		const FRDGTextureDesc& MaskDesc = LayerCtx.MaskDesc;
		FRDGTextureRef* const MaskTargets = LayerCtx.MaskTargets;
		FRDGTextureRef* const RidgeTargets = LayerCtx.RidgeTargets;
		FRDGTextureRef& CombinedMask = LayerCtx.CombinedMask;
		FRDGTextureRef& DebugMask = LayerCtx.DebugMask;
		int32& MaskPassIndex = LayerCtx.MaskPassIndex;
		TShaderMapRef<FMixtormatGeneratedMaskCS> GeneratedMaskShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

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
			return;
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
	}

	// Colour ID mask: the regions of an ID map carrying one of a set of chosen colours.
	void AddColorIdMaskPass(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		const int32 ChildIndex)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		TMap<FRHITexture*, FRDGTextureRef>& RegisteredTextures = Ctx.RegisteredTextures;
		const int32 LayerIndex = LayerCtx.LayerIndex;
		const FRDGTextureDesc& MaskDesc = LayerCtx.MaskDesc;
		FRDGTextureRef* const MaskTargets = LayerCtx.MaskTargets;
		FRDGTextureRef& CombinedMask = LayerCtx.CombinedMask;
		FRDGTextureRef& DebugMask = LayerCtx.DebugMask;
		int32& MaskPassIndex = LayerCtx.MaskPassIndex;
		TShaderMapRef<FMixtormatColorIdCS> ColorIdShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

		const FColorIdRenderData& ColorId = Child.ColorId;

		// The same identity as the other mask children: at Weight 0 the tail
		// returns Previous unchanged, and skipping from the second child on
		// leaves exactly that behind.
		if (MaskPassIndex > 0 && ColorId.Weight == 0.0f)
		{
			return;
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
	}

	// An authored or published mask blended into the layer's mask chain.
	void AddTextureMaskPass(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		const int32 ChildIndex)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		TMap<FRHITexture*, FRDGTextureRef>& RegisteredTextures = Ctx.RegisteredTextures;
		const FRDGTextureRef EmptyDriverSignal = Ctx.EmptyDriverSignal;
		TMap<FPublishedMaskKey, FRDGTextureRef>& PublishedMaskOutputs = Ctx.PublishedMaskOutputs;
		const int32 LayerIndex = LayerCtx.LayerIndex;
		const FRDGTextureDesc& MaskDesc = LayerCtx.MaskDesc;
		FRDGTextureRef* const MaskTargets = LayerCtx.MaskTargets;
		FRDGTextureRef& CombinedMask = LayerCtx.CombinedMask;
		FRDGTextureRef& DebugMask = LayerCtx.DebugMask;
		int32& MaskPassIndex = LayerCtx.MaskPassIndex;
		TShaderMapRef<FMixtormatMaskCS> MaskShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

		// Evaluated by AddScopedFeatureMask when its owner row is reached.
		if (Child.ScopeOwnerSourceChildIndex != INDEX_NONE)
		{
			return;
		}
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
			return;
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
		FRDGTextureRef IncomingMask = nullptr;
		if (!Mask.PublishedSourceOutput.IsNone())
		{
			const FPublishedMaskKey Key{
				Mask.PublishedSourceLayerId,
				Mask.PublishedSourceChildIndex,
				Mask.PublishedSourceOutput};
			if (FRDGTextureRef* Published = PublishedMaskOutputs.Find(Key))
			{
				IncomingMask = *Published;
			}
			else
			{
				IncomingMask = EmptyDriverSignal;
			}
		}
		else
		{
			IncomingMask = RegisterTexture(
				GraphBuilder,
				RegisteredTextures,
				Mask.Texture,
				TEXT("Mixtormat.IncomingMask"));
		}
		MaskParameters->IncomingMask = IncomingMask;
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
	}

	// The separable Gaussian behind contact AO and the border lift, run over the accumulated
	// height the two fields are derived from.
	//
	// It happens here rather than inside the composite because a blur wants the field already
	// in a texture: evaluating it per tap would cost four reads each, and a kernel wide enough
	// to matter would be dozens of taps per pixel. Blurring the source rather than widening the
	// derivative is the point -- a central difference taken further apart reaches further into
	// the noise instead of averaging it.
	FRDGTextureRef AddBorderHeightBlurPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		FRDGTextureRef* const HeightTargets = Ctx.OutputHeight;
		const int32 LayerIndex = LayerCtx.LayerIndex;
		const int32 ReadIndex = 1 - (LayerCtx.LayerIndex & 1);
		TShaderMapRef<FMixtormatMaskBlurCS> MaskBlurShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FRDGTextureRef BorderBaseHeight = HeightTargets[ReadIndex];
		{
			FRDGTextureRef BorderBlur[2] = {
				GraphBuilder.CreateTexture(
					HeightTargets[ReadIndex]->Desc, TEXT("Mixtormat.BorderHeightBlurX")),
				GraphBuilder.CreateTexture(
					HeightTargets[ReadIndex]->Desc, TEXT("Mixtormat.BorderHeightBlurY"))};
			const FIntVector BorderGroups(
				FMath::DivideAndRoundUp(Request.Resolution.X, 8),
				FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
				1);
			for (int32 BlurAxis = 0; BlurAxis < 2; ++BlurAxis)
			{
				FMixtormatMaskBlurCS::FParameters* BorderBlurParameters =
					GraphBuilder.AllocParameters<FMixtormatMaskBlurCS::FParameters>();
				BorderBlurParameters->OutputSize = Request.Resolution;
				BorderBlurParameters->Axis = BlurAxis;
				BorderBlurParameters->Radius = Layer.HeightBorderSmoothing;
				BorderBlurParameters->SourceMask = BlurAxis == 0
					? HeightTargets[ReadIndex]
					: BorderBlur[0];
				BorderBlurParameters->LinearWrapSampler =
					TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
				BorderBlurParameters->OutputMask =
					GraphBuilder.CreateUAV(BorderBlur[BlurAxis]);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME(
						"Mixtormat.BorderHeightBlur.Layer%d.Axis%d",
						LayerIndex,
						BlurAxis),
					MaskBlurShader,
					BorderBlurParameters,
					BorderGroups);
			}
			BorderBaseHeight = BorderBlur[1];
		}
		return BorderBaseHeight;
	}

	// Rounding for the height field. A placement mask is a step, so the layer's height falls
	// from full to nothing across one texel and the layer reads as a decal sitting on the
	// surface. Taking the height from a blurred copy replaces that step with a ramp.
	//
	// Its own pair of scratch targets, not the mask ping-pong halves: those are the chain the
	// next layer's mask children read and write.
	FRDGTextureRef AddHeightMaskBlurPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		const int32 LayerIndex = LayerCtx.LayerIndex;
		const FRDGTextureDesc& MaskDesc = LayerCtx.MaskDesc;
		const FRDGTextureRef CombinedMask = LayerCtx.CombinedMask;
		TShaderMapRef<FMixtormatMaskBlurCS> MaskBlurShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FRDGTextureRef LayerHeightMask = CombinedMask;
		{
			FRDGTextureRef BlurTargets[2] = {
				GraphBuilder.CreateTexture(MaskDesc, TEXT("Mixtormat.HeightMaskBlurX")),
				GraphBuilder.CreateTexture(MaskDesc, TEXT("Mixtormat.HeightMaskBlurY"))};
			const FIntVector BlurGroups(
				FMath::DivideAndRoundUp(Request.Resolution.X, 8),
				FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
				1);
			for (int32 BlurAxis = 0; BlurAxis < 2; ++BlurAxis)
			{
				FMixtormatMaskBlurCS::FParameters* BlurParameters =
					GraphBuilder.AllocParameters<FMixtormatMaskBlurCS::FParameters>();
				BlurParameters->OutputSize = Request.Resolution;
				BlurParameters->Axis = BlurAxis;
				BlurParameters->Radius = Layer.HeightSmoothRadius;
				BlurParameters->SourceMask =
					BlurAxis == 0 ? CombinedMask : BlurTargets[0];
				BlurParameters->LinearWrapSampler =
					TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
				BlurParameters->OutputMask =
					GraphBuilder.CreateUAV(BlurTargets[BlurAxis]);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME(
						"Mixtormat.HeightMaskBlur.Layer%d.Axis%d", LayerIndex, BlurAxis),
					MaskBlurShader,
					BlurParameters,
					BlurGroups);
			}
			LayerHeightMask = BlurTargets[1];
		}
		return LayerHeightMask;
	}

}
