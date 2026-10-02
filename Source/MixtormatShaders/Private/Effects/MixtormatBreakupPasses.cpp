// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "../MixtormatGpuCompositorInternal.h"

#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"

// Breakup is prepared while the child stack is being collected: FieldCS evaluates the SDF once,
// publishes a real region-id texture immediately, and stores the field for the post-composite
// structural passes. That makes Breakup a first-class ID producer for every later child.
class FMixtormatBreakupFieldCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatBreakupFieldCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatBreakupFieldCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(int32, MacroCells)
		SHADER_PARAMETER(int32, MidCells)
		SHADER_PARAMETER(int32, DetailCells)
		SHADER_PARAMETER(float, Density)
		SHADER_PARAMETER(float, SizeMin)
		SHADER_PARAMETER(float, SizeMax)
		SHADER_PARAMETER(float, Stretch)
		SHADER_PARAMETER(float, Angularity)
		SHADER_PARAMETER(float, Jitter)
		SHADER_PARAMETER(int32, OperationMid)
		SHADER_PARAMETER(int32, OperationDetail)
		SHADER_PARAMETER(float, BlendSmooth)
		SHADER_PARAMETER(float, DistortAmount)
		SHADER_PARAMETER(int32, DistortFrequency)
		SHADER_PARAMETER(float, Inset)
		SHADER_PARAMETER(float, GapWidth)
		SHADER_PARAMETER(float, GapVariation)
		SHADER_PARAMETER(uint32, InvertField)
		SHADER_PARAMETER(uint32, HasRegionIds)
		SHADER_PARAMETER(uint32, UsePlacementMask)
		SHADER_PARAMETER(float, PlacementMaskTiling)
		SHADER_PARAMETER(uint32, InvertMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, RegionIds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, LayerMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PlacementMaskTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutputField)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, OutputRegionIds)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatBreakupFieldCS,
	"/Plugin/Mixtormat/Private/MixtormatBreakup.usf",
	"FieldCS",
	SF_Compute);

// The scalar maps Breakup publishes. Reads back the field and IDs FieldCS wrote rather than
// re-evaluating anything, so this costs a handful of loads.
class FMixtormatBreakupMasksCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatBreakupMasksCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatBreakupMasksCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, GapWidth)
		SHADER_PARAMETER(float, GapVariation)
		SHADER_PARAMETER(float, CreaseWidth)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, BreakupField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, BreakupRegionIds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputGap)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputEdge)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputPieces)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatBreakupMasksCS,
	"/Plugin/Mixtormat/Private/MixtormatBreakup.usf",
	"MasksCS",
	SF_Compute);

class FMixtormatBreakupApplyCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatBreakupApplyCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatBreakupApplyCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, Relief)
		SHADER_PARAMETER(float, ThicknessVariation)
		SHADER_PARAMETER(float, GapWidth)
		SHADER_PARAMETER(float, GapDepth)
		SHADER_PARAMETER(float, GapVariation)
		SHADER_PARAMETER(float, FoldHeight)
		SHADER_PARAMETER(float, FoldWidth)
		SHADER_PARAMETER(float, CreaseWidth)
		SHADER_PARAMETER(float, CreaseDepth)
		SHADER_PARAMETER(float, PushAmount)
		SHADER_PARAMETER(float, PushWidth)
		SHADER_PARAMETER(float, PushRelief)
		SHADER_PARAMETER(float, Variation)
		SHADER_PARAMETER(float, Strength)
		SHADER_PARAMETER(uint32, UsePlacementMask)
		SHADER_PARAMETER(float, PlacementMaskTiling)
		SHADER_PARAMETER(uint32, InvertMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, BreakupField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, BreakupRegionIds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, LayerMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PlacementMaskTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputCoverage)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatBreakupApplyCS,
	"/Plugin/Mixtormat/Private/MixtormatBreakup.usf",
	"ApplyCS",
	SF_Compute);

class FMixtormatBreakupShadeCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatBreakupShadeCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatBreakupShadeCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, NormalStrength)
		SHADER_PARAMETER(float, NormalSharpness)
		SHADER_PARAMETER(float, RoughnessAmount)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, CurrentHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, BreakupField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, BreakupRegionIds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, BreakupCoverage)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousNormal)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousRAM)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputNormal)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputRAM)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatBreakupShadeCS,
	"/Plugin/Mixtormat/Private/MixtormatBreakup.usf",
	"ShadeCS",
	SF_Compute);

namespace MixtormatGpuCompositor
{
	// Chipping runs on prepared owner channels after that owner's earlier relief filters.
	void QueuePendingBreakup(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		const FEffectRenderData& Effect,
		FRDGTextureRef FeatureMask)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		TMap<FRHITexture*, FRDGTextureRef>& RegisteredTextures = Ctx.RegisteredTextures;

		FPendingBreakup& Pending = LayerCtx.PendingBreakups.AddDefaulted_GetRef();
		Pending.Effect = &Effect;
		Pending.SourceChildIndex = Child.SourceChildIndex;
		Pending.FeatureMask = FeatureMask;
		Pending.bHasScopedMask = Layer.Children.ContainsByPredicate(
			[&Child](const FChildRenderData& Candidate)
			{
				return Candidate.Type == EMixtormatLayerChildType::Mask
					&& Candidate.ScopeOwnerSourceChildIndex == Child.SourceChildIndex;
			});

		const FRDGTextureRef SourceRegionIds =
			FindRegionIdsAbove(LayerCtx.RegionIdMaps, Child.SourceChildIndex);
		const bool bUsePlacementMask =
			!Pending.bHasScopedMask && Effect.BreakupPlacementMask.IsValid();
		FRDGTextureRef PlacementMask = bUsePlacementMask
			? RegisterTexture(
				GraphBuilder,
				RegisteredTextures,
				Effect.BreakupPlacementMask,
				TEXT("Mixtormat.BreakupPlacementMask"))
			: FeatureMask;

		Pending.Field = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(
				Request.Resolution,
				PF_G16R16F,
				FClearValueBinding::Black,
				TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Mixtormat.Breakup.Field"));
		Pending.GeneratedRegionIds = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(
				Request.Resolution,
				PF_R32_UINT,
				FClearValueBinding::None,
				TexCreate_ShaderResource | TexCreate_UAV),
			TEXT("Mixtormat.Breakup.RegionIds"));

		TShaderMapRef<FMixtormatBreakupFieldCS> FieldShader(
			GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FMixtormatBreakupFieldCS::FParameters* FP =
			GraphBuilder.AllocParameters<FMixtormatBreakupFieldCS::FParameters>();
		FP->OutputSize = Request.Resolution;
		FP->Seed = Effect.BreakupSeed;
		FP->MacroCells = Effect.BreakupMacroCells;
		FP->MidCells = Effect.BreakupMidCells;
		FP->DetailCells = Effect.BreakupDetailCells;
		FP->Density = Effect.BreakupDensity;
		FP->SizeMin = Effect.BreakupSizeMin;
		FP->SizeMax = Effect.BreakupSizeMax;
		FP->Stretch = Effect.BreakupStretch;
		FP->Angularity = Effect.BreakupAngularity;
		FP->Jitter = Effect.BreakupIrregularity;
		FP->OperationMid = Effect.BreakupMidOperation;
		FP->OperationDetail = Effect.BreakupDetailOperation;
		FP->BlendSmooth = Effect.BreakupSmoothness;
		FP->DistortAmount = Effect.BreakupDistortion;
		FP->DistortFrequency = Effect.BreakupDistortionFrequency;
		FP->Inset = Effect.BreakupInset;
		FP->GapWidth = Effect.BreakupGapWidth;
		FP->GapVariation = Effect.BreakupGapVariation;
		FP->InvertField = Effect.bBreakupInvert ? 1u : 0u;
		FP->HasRegionIds = SourceRegionIds != nullptr ? 1u : 0u;
		FP->UsePlacementMask = bUsePlacementMask ? 1u : 0u;
		FP->PlacementMaskTiling = Effect.BreakupMaskTiling;
		FP->InvertMask = !Pending.bHasScopedMask && Effect.bBreakupInvertMask ? 1u : 0u;
		FP->RegionIds = SourceRegionIds ? SourceRegionIds : Ctx.EmptyRegionIds;
		FP->LayerMask = FeatureMask;
		FP->PlacementMaskTexture = PlacementMask;
		FP->LinearWrapSampler =
			TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
		FP->OutputField = GraphBuilder.CreateUAV(Pending.Field);
		FP->OutputRegionIds = GraphBuilder.CreateUAV(Pending.GeneratedRegionIds);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME(
				"Mixtormat.Breakup.L%d.Child%d.Field",
				LayerCtx.LayerIndex,
				Child.SourceChildIndex),
			FieldShader,
			FP,
			FIntVector(
				FMath::DivideAndRoundUp(Request.Resolution.X, 8),
				FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
				1));

		// The generated piece map immediately becomes the nearest region producer for every later
		// child in this layer. Amount may be zero: Breakup can intentionally be used as an ID-only
		// structural generator without touching height.
		PublishRegionIds(LayerCtx.RegionIdMaps, Child.SourceChildIndex, Pending.GeneratedRegionIds);

		// The scalar maps, published here rather than from ApplyCS. ApplyCS is deferred until
		// after the layer composites -- it carves the accumulated height, so it has to be -- and
		// a published mask has to exist before the child chain that reads it runs. Amount zero
		// reaches this line for the same reason the IDs do.
		const auto CreateMask = [&](const TCHAR* Name)
		{
			return GraphBuilder.CreateTexture(
				FRDGTextureDesc::Create2D(
					Request.Resolution,
					PF_R16F,
					FClearValueBinding::Black,
					TexCreate_ShaderResource | TexCreate_UAV),
				Name);
		};
		Pending.Gap = CreateMask(TEXT("Mixtormat.Breakup.Gap"));
		Pending.Edge = CreateMask(TEXT("Mixtormat.Breakup.Edge"));
		Pending.Pieces = CreateMask(TEXT("Mixtormat.Breakup.Pieces"));

		TShaderMapRef<FMixtormatBreakupMasksCS> MasksShader(
			GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FMixtormatBreakupMasksCS::FParameters* MP =
			GraphBuilder.AllocParameters<FMixtormatBreakupMasksCS::FParameters>();
		MP->OutputSize = Request.Resolution;
		MP->GapWidth = Effect.BreakupGapWidth;
		MP->GapVariation = Effect.BreakupGapVariation;
		MP->CreaseWidth = Effect.BreakupCreaseWidth;
		MP->BreakupField = Pending.Field;
		MP->BreakupRegionIds = Pending.GeneratedRegionIds;
		MP->OutputGap = GraphBuilder.CreateUAV(Pending.Gap);
		MP->OutputEdge = GraphBuilder.CreateUAV(Pending.Edge);
		MP->OutputPieces = GraphBuilder.CreateUAV(Pending.Pieces);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME(
				"Mixtormat.Breakup.L%d.Child%d.Masks",
				LayerCtx.LayerIndex,
				Child.SourceChildIndex),
			MasksShader,
			MP,
			FIntVector(
				FMath::DivideAndRoundUp(Request.Resolution.X, 8),
				FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
				1));

		Ctx.PublishedMaskOutputs.Add(
			FPublishedMaskKey{Layer.LayerId, Child.SourceChildIndex, FName(TEXT("Gap"))},
			Pending.Gap);
		Ctx.PublishedMaskOutputs.Add(
			FPublishedMaskKey{Layer.LayerId, Child.SourceChildIndex, FName(TEXT("Edge"))},
			Pending.Edge);
		Ctx.PublishedMaskOutputs.Add(
			FPublishedMaskKey{Layer.LayerId, Child.SourceChildIndex, FName(TEXT("Pieces"))},
			Pending.Pieces);

		// The generic child-output preview eye. Breakup always builds all four of these, so
		// unlike Pattern IDs' Gap there is no demand-gating to extend -- only a debug blit to add.
		const FRDGTextureRef DebugTarget = Ctx.OutputDebug[Request.PublishedTargetIndex];
		if (IsChildOutputPreviewTarget(Request, EMixtormatPreviewOutputKind::RegionIds, NAME_None,
			LayerCtx.LayerIndex, Child.SourceChildIndex))
		{
			// Default preview: Region IDs with Gap blacked out, so the broken-piece topology
			// reads at a glance instead of every grout pixel getting its own random colour.
			AddDebugPreviewRegionIdsBlitPass(
				GraphBuilder, Pending.GeneratedRegionIds, Pending.Gap, DebugTarget, Request.Resolution);
		}
		else if (IsChildOutputPreviewTarget(Request, EMixtormatPreviewOutputKind::Mask,
			FName(TEXT("Gap")), LayerCtx.LayerIndex, Child.SourceChildIndex))
		{
			AddDebugPreviewMaskBlitPass(GraphBuilder, Pending.Gap, DebugTarget, Request.Resolution);
		}
		else if (IsChildOutputPreviewTarget(Request, EMixtormatPreviewOutputKind::Mask,
			FName(TEXT("Edge")), LayerCtx.LayerIndex, Child.SourceChildIndex))
		{
			AddDebugPreviewMaskBlitPass(GraphBuilder, Pending.Edge, DebugTarget, Request.Resolution);
		}
		else if (IsChildOutputPreviewTarget(Request, EMixtormatPreviewOutputKind::Mask,
			FName(TEXT("Pieces")), LayerCtx.LayerIndex, Child.SourceChildIndex))
		{
			AddDebugPreviewMaskBlitPass(GraphBuilder, Pending.Pieces, DebugTarget, Request.Resolution);
		}
	}



	// Chipping: edge seeds grow against a fixed owner-local height. The caller routes
	// the output slots to prepared layer channels until all local relief is finished.
	void AddBreakupPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		TMap<FRHITexture*, FRDGTextureRef>& RegisteredTextures = Ctx.RegisteredTextures;
		FRDGTextureRef* const OutputN = Ctx.OutputN;
		FRDGTextureRef* const OutputRAM = Ctx.OutputRAM;
		FRDGTextureRef* const HeightTargets = Ctx.OutputHeight;
		const int32 LayerIndex = LayerCtx.LayerIndex;
		const int32 WriteIndex = LayerIndex & 1;

		TShaderMapRef<FMixtormatBreakupApplyCS> ApplyShader(
			GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FMixtormatBreakupShadeCS> ShadeShader(
			GetGlobalShaderMap(GMaxRHIFeatureLevel));

		const FIntVector Groups(
			FMath::DivideAndRoundUp(Request.Resolution.X, 8),
			FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
			1);

		for (int32 BreakupIndex = 0; BreakupIndex < LayerCtx.PendingBreakups.Num(); ++BreakupIndex)
		{
			const FPendingBreakup& PendingBreakup = LayerCtx.PendingBreakups[BreakupIndex];
			if (!PendingBreakup.Effect
				|| !PendingBreakup.Field
				|| !PendingBreakup.GeneratedRegionIds
				|| PendingBreakup.Effect->BreakupAmount <= 0.0f)
			{
				continue;
			}

			const FEffectRenderData& Breakup = *PendingBreakup.Effect;
			const bool bUsePlacementMask =
				!PendingBreakup.bHasScopedMask && Breakup.BreakupPlacementMask.IsValid();
			FRDGTextureRef PlacementMask = bUsePlacementMask
				? RegisterTexture(
					GraphBuilder,
					RegisteredTextures,
					Breakup.BreakupPlacementMask,
					TEXT("Mixtormat.BreakupPlacementMask"))
				: PendingBreakup.FeatureMask;

			FRDGTextureRef Coverage = GraphBuilder.CreateTexture(
				FRDGTextureDesc::Create2D(
					Request.Resolution,
					PF_R16F,
					FClearValueBinding::Black,
					TexCreate_ShaderResource | TexCreate_UAV),
				TEXT("Mixtormat.Breakup.Coverage"));
			FRDGTextureRef SourceH = GraphBuilder.CreateTexture(
				HeightTargets[WriteIndex]->Desc, TEXT("Mixtormat.Breakup.SourceH"));
			FRDGTextureRef ResultH = GraphBuilder.CreateTexture(
				HeightTargets[WriteIndex]->Desc, TEXT("Mixtormat.Breakup.Height"));
			FRDGTextureRef ResultN = GraphBuilder.CreateTexture(
				OutputN[WriteIndex]->Desc, TEXT("Mixtormat.Breakup.Normal"));
			FRDGTextureRef ResultRAM = GraphBuilder.CreateTexture(
				OutputRAM[WriteIndex]->Desc, TEXT("Mixtormat.Breakup.RAM"));

			AddCopyTexturePass(GraphBuilder, HeightTargets[WriteIndex], SourceH);

			FMixtormatBreakupApplyCS::FParameters* AP =
				GraphBuilder.AllocParameters<FMixtormatBreakupApplyCS::FParameters>();
			AP->OutputSize = Request.Resolution;
			AP->Relief = Breakup.BreakupRelief;
			AP->ThicknessVariation = Breakup.BreakupThicknessVariation;
			AP->GapWidth = Breakup.BreakupGapWidth;
			AP->GapDepth = Breakup.BreakupGapDepth;
			AP->GapVariation = Breakup.BreakupGapVariation;
			AP->FoldHeight = Breakup.BreakupFold;
			AP->FoldWidth = Breakup.BreakupFoldWidth;
			AP->CreaseWidth = Breakup.BreakupCreaseWidth;
			AP->CreaseDepth = Breakup.BreakupCrease;
			AP->PushAmount = Breakup.BreakupPush;
			AP->PushWidth = Breakup.BreakupPushWidth;
			AP->PushRelief = Breakup.BreakupPushRelief;
			AP->Variation = Breakup.BreakupVariation;
			AP->Strength = Breakup.BreakupAmount;
			AP->UsePlacementMask = bUsePlacementMask ? 1u : 0u;
			AP->PlacementMaskTiling = Breakup.BreakupMaskTiling;
			AP->InvertMask =
				!PendingBreakup.bHasScopedMask && Breakup.bBreakupInvertMask ? 1u : 0u;
			AP->SourceHeight = SourceH;
			AP->BreakupField = PendingBreakup.Field;
			AP->BreakupRegionIds = PendingBreakup.GeneratedRegionIds;
			AP->LayerMask = PendingBreakup.FeatureMask;
			AP->PlacementMaskTexture = PlacementMask;
			AP->LinearWrapSampler =
				TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
			AP->OutputHeight = GraphBuilder.CreateUAV(ResultH);
			AP->OutputCoverage = GraphBuilder.CreateUAV(Coverage);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.Breakup.L%d.%d.Apply", LayerIndex, BreakupIndex),
				ApplyShader,
				AP,
				Groups);

			FMixtormatBreakupShadeCS::FParameters* SP =
				GraphBuilder.AllocParameters<FMixtormatBreakupShadeCS::FParameters>();
			SP->OutputSize = Request.Resolution;
			SP->NormalStrength = Request.bFinalNormalFromHeight ? 0.0f : Breakup.BreakupNormalStrength;
			SP->NormalSharpness = Breakup.BreakupNormalSharpness;
			SP->RoughnessAmount = Breakup.BreakupRoughnessAmount;
			SP->SourceHeight = SourceH;
			SP->CurrentHeight = ResultH;
			SP->BreakupField = PendingBreakup.Field;
			SP->BreakupRegionIds = PendingBreakup.GeneratedRegionIds;
			SP->BreakupCoverage = Coverage;
			SP->PreviousNormal = OutputN[WriteIndex];
			SP->PreviousRAM = OutputRAM[WriteIndex];
			SP->OutputNormal = GraphBuilder.CreateUAV(ResultN);
			SP->OutputRAM = GraphBuilder.CreateUAV(ResultRAM);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.Breakup.L%d.%d.Shade", LayerIndex, BreakupIndex),
				ShadeShader,
				SP,
				Groups);

			AddCopyTexturePass(GraphBuilder, ResultH, HeightTargets[WriteIndex]);
			AddCopyTexturePass(GraphBuilder, ResultN, OutputN[WriteIndex]);
			AddCopyTexturePass(GraphBuilder, ResultRAM, OutputRAM[WriteIndex]);
		}
	}

}
