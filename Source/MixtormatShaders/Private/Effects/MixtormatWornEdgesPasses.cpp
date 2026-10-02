// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "../MixtormatGpuCompositorInternal.h"

#include "MixtormatEffectPassesPrivate.h"
#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"

// Worn Edges is a post-composite height filter. One shader layout serves the cheap ID-edge
// localization passes, the Houdini directional-MIN solve, and final-height normal regeneration.
class FMixtormatEdgeWearCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatEdgeWearCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatEdgeWearCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(int32, Mode)
		SHADER_PARAMETER(int32, EdgeStep)
		SHADER_PARAMETER(int32, Radius)
		SHADER_PARAMETER(float, Slope)
		SHADER_PARAMETER(float, Strength)
		SHADER_PARAMETER(float, Feather)
		SHADER_PARAMETER(int32, Directions)
		SHADER_PARAMETER(float, AngularAA)
		SHADER_PARAMETER(float, Gravity)
		SHADER_PARAMETER(float, GravityAngle)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(int32, MacroScale)
		SHADER_PARAMETER(float, MacroAmount)
		SHADER_PARAMETER(int32, CellScale)
		SHADER_PARAMETER(float, CellAmount)
		SHADER_PARAMETER(int32, RidgeScale)
		SHADER_PARAMETER(float, RidgeAmount)
		SHADER_PARAMETER(int32, MicroScale)
		SHADER_PARAMETER(float, MicroAmount)
		SHADER_PARAMETER(int32, WarpScale)
		SHADER_PARAMETER(float, WarpAmount)
		SHADER_PARAMETER(float, NoiseContrast)
		SHADER_PARAMETER(float, IDVariation)
		SHADER_PARAMETER(float, IDRadius)
		SHADER_PARAMETER(float, IDSlope)
		SHADER_PARAMETER(float, IDStrength)
		SHADER_PARAMETER(float, IDNoise)
		SHADER_PARAMETER(uint32, HasPatternEdge)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, WornHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousNormal)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, RegionIds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, EdgeField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousEdgeBand)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, EdgeBand)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, LayerMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, WearMask)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputWearMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputEdgeBand)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputNormal)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatEdgeWearCS,
	"/Plugin/Mixtormat/Private/MixtormatEdgeWear.usf",
	"MainCS",
	SF_Compute);

namespace MixtormatGpuCompositor
{
	// Worn Edges needs an upstream Region ID producer; without one it is a deterministic
	// no-op rather than an invented ID system.
	void QueuePendingWornEdges(
		FMixtormatLayerPassContext& LayerCtx,
		const FChildRenderData& Child,
		const FEffectRenderData& Effect,
		FRDGTextureRef FeatureMask)
	{
		TArray<TPair<int32, FRDGTextureRef>>& RegionIdMaps = LayerCtx.RegionIdMaps;
		TArray<FPatternIdPassOutput, TInlineAllocator<2>>& PatternOutputs =
			LayerCtx.PatternOutputs;
		TArray<FPendingWornEdges, TInlineAllocator<2>>& PendingWornEdges =
			LayerCtx.PendingWornEdges;

		int32 ProducerChildIndex = INDEX_NONE;
		FRDGTextureRef WearRegionIds = nullptr;
		for (const TPair<int32, FRDGTextureRef>& Entry : RegionIdMaps)
		{
			if (Entry.Key < Child.SourceChildIndex)
			{
				ProducerChildIndex = Entry.Key;
				WearRegionIds = Entry.Value;
			}
		}
		if (!WearRegionIds)
		{
			// Task B contract: without an upstream Region ID producer Worn Edges
			// is a deterministic no-op rather than inventing an ID system.
			return;
		}

		FPendingWornEdges& Wear = PendingWornEdges.AddDefaulted_GetRef();
		Wear.Effect = &Effect;
		Wear.SourceChildIndex = Child.SourceChildIndex;
		Wear.FeatureMask = FeatureMask;
		Wear.RegionIds = WearRegionIds;
		for (const FPatternIdPassOutput& PatternOutput : PatternOutputs)
		{
			if (PatternOutput.SourceChildIndex != ProducerChildIndex)
			{
				continue;
			}
			// OutputEdge.x is signed pixels only in absolute mode. Relative mode
			// stores a cell fraction, so use the ID-derived band there rather than
			// comparing unlike units to Radius pixels.
			if (PatternOutput.Settings && !PatternOutput.Settings->bRelativeEdgeWidth)
			{
				Wear.PatternEdge = PatternOutput.Edge;
				Wear.bHasPatternEdge = PatternOutput.Edge != nullptr;
			}
			break;
		}
	}

	// Worn Edges: after Pattern/Ramp and craquelure relief so its input is the real structural
	// height, before Breakup so later damage sees the rounded surface. Publishes its wear
	// coverage as a named mask output.
	void AddWornEdgesPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		FRDGTextureRef* const OutputN = Ctx.OutputN;
		FRDGTextureRef* const OutputRAM = Ctx.OutputRAM;
		FRDGTextureRef* const HeightTargets = Ctx.OutputHeight;
		const FRDGTextureRef EmptyPatternUV = Ctx.EmptyPatternUV;
		TMap<FPublishedMaskKey, FRDGTextureRef>& PublishedMaskOutputs = Ctx.PublishedMaskOutputs;
		const int32 LayerIndex = LayerCtx.LayerIndex;
		const int32 WriteIndex = LayerCtx.LayerIndex & 1;
		TArray<FPendingWornEdges, TInlineAllocator<2>>& PendingWornEdges =
			LayerCtx.PendingWornEdges;
		TShaderMapRef<FMixtormatCarveShadeCS> CarveShadeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FMixtormatEdgeWearCS> EdgeWearShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		// Worn Edges runs after Pattern/Ramp and craquelure relief so its input is the
		// actual structural + material height, and before Breakup so later damage sees
		// the rounded surface. Multiple Worn Edges nodes chain in child order.
		for (int32 WearIndex = 0; WearIndex < PendingWornEdges.Num(); ++WearIndex)
		{
			const FPendingWornEdges& PendingWear = PendingWornEdges[WearIndex];
			const FEffectRenderData& Wear = *PendingWear.Effect;
			if (Wear.EdgeWearStrength <= 0.0f)
			{
				continue;
			}

			const FIntVector WearGroups(
				FMath::DivideAndRoundUp(Request.Resolution.X, 8),
				FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
				1);
			const FRDGTextureDesc WearScalarDesc = FRDGTextureDesc::Create2D(
				Request.Resolution,
				PF_R16F,
				FClearValueBinding::Black,
				TexCreate_ShaderResource | TexCreate_UAV);

			FRDGTextureRef WearSourceH = GraphBuilder.CreateTexture(
				HeightTargets[WriteIndex]->Desc, TEXT("Mixtormat.WornEdges.SourceH"));
			FRDGTextureRef WornH = GraphBuilder.CreateTexture(
				HeightTargets[WriteIndex]->Desc, TEXT("Mixtormat.WornEdges.Height"));
			FRDGTextureRef WornN = GraphBuilder.CreateTexture(
				OutputN[WriteIndex]->Desc, TEXT("Mixtormat.WornEdges.Normal"));
			FRDGTextureRef EdgeWearMask = GraphBuilder.CreateTexture(
				WearScalarDesc, TEXT("Mixtormat.WornEdges.EdgeWearMask"));
			AddCopyTexturePass(GraphBuilder, HeightTargets[WriteIndex], WearSourceH);

			// One layout is shared by all four shader modes. Tiny distinct dummies keep
			// every reflected slot valid without ever binding one resource as both SRV
			// and UAV in the same pass.
			const FRDGTextureDesc TinyScalarDesc = FRDGTextureDesc::Create2D(
				FIntPoint(1, 1), PF_R16F, FClearValueBinding::Black,
				TexCreate_ShaderResource | TexCreate_UAV);
			const FRDGTextureDesc TinyNormalDesc = FRDGTextureDesc::Create2D(
				FIntPoint(1, 1), PF_FloatRGBA, FClearValueBinding::Black,
				TexCreate_ShaderResource | TexCreate_UAV);
			FRDGTextureRef ReadDummy = GraphBuilder.CreateTexture(TinyScalarDesc, TEXT("Mixtormat.WornEdges.ReadDummy"));
			FRDGTextureRef WriteDummyA = GraphBuilder.CreateTexture(TinyScalarDesc, TEXT("Mixtormat.WornEdges.WriteDummyA"));
			FRDGTextureRef WriteDummyB = GraphBuilder.CreateTexture(TinyScalarDesc, TEXT("Mixtormat.WornEdges.WriteDummyB"));
			FRDGTextureRef WriteDummyC = GraphBuilder.CreateTexture(TinyScalarDesc, TEXT("Mixtormat.WornEdges.WriteDummyC"));
			FRDGTextureRef NormalDummy = GraphBuilder.CreateTexture(TinyNormalDesc, TEXT("Mixtormat.WornEdges.NormalDummy"));
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(ReadDummy), FVector4f(0.0f));
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(WriteDummyA), FVector4f(0.0f));
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(WriteDummyB), FVector4f(0.0f));
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(WriteDummyC), FVector4f(0.0f));
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(NormalDummy), FVector4f(0.0f));

			FRDGTextureRef FinalEdgeBand = ReadDummy;
			if (!PendingWear.bHasPatternEdge)
			{
				FRDGTextureRef Band[2] = {
					GraphBuilder.CreateTexture(WearScalarDesc, TEXT("Mixtormat.WornEdges.BandA")),
					GraphBuilder.CreateTexture(WearScalarDesc, TEXT("Mixtormat.WornEdges.BandB"))};
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Band[0]), FVector4f(0.0f));
				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Band[1]), FVector4f(0.0f));

				FMixtormatEdgeWearCS::FParameters* SeedP =
					GraphBuilder.AllocParameters<FMixtormatEdgeWearCS::FParameters>();
				SeedP->OutputSize = Request.Resolution;
				SeedP->Mode = 0;
				SeedP->EdgeStep = 1;
				SeedP->Radius = Wear.EdgeWearRadius;
				SeedP->Slope = Wear.EdgeWearSlope;
				SeedP->Strength = Wear.EdgeWearStrength;
				SeedP->Feather = Wear.EdgeWearFeather;
				SeedP->Directions = Wear.EdgeWearDirections;
				SeedP->AngularAA = Wear.EdgeWearAngularAA;
				SeedP->Gravity = Wear.EdgeWearGravity;
				SeedP->GravityAngle = Wear.EdgeWearGravityAngle;
				SeedP->Seed = Wear.EdgeWearSeed;
				SeedP->MacroScale = Wear.EdgeWearMacroScale;
				SeedP->MacroAmount = Wear.EdgeWearMacroAmount;
				SeedP->CellScale = Wear.EdgeWearCellScale;
				SeedP->CellAmount = Wear.EdgeWearCellAmount;
				SeedP->RidgeScale = Wear.EdgeWearRidgeScale;
				SeedP->RidgeAmount = Wear.EdgeWearRidgeAmount;
				SeedP->MicroScale = Wear.EdgeWearMicroScale;
				SeedP->MicroAmount = Wear.EdgeWearMicroAmount;
				SeedP->WarpScale = Wear.EdgeWearWarpScale;
				SeedP->WarpAmount = Wear.EdgeWearWarpAmount;
				SeedP->NoiseContrast = Wear.EdgeWearNoiseContrast;
				SeedP->IDVariation = Wear.EdgeWearIdVariation;
				SeedP->IDRadius = Wear.EdgeWearIdRadius;
				SeedP->IDSlope = Wear.EdgeWearIdSlope;
				SeedP->IDStrength = Wear.EdgeWearIdStrength;
				SeedP->IDNoise = Wear.EdgeWearIdNoise;
				SeedP->HasPatternEdge = 0u;
				SeedP->SourceHeight = WearSourceH;
				SeedP->WornHeight = ReadDummy;
				SeedP->PreviousNormal = OutputN[WriteIndex];
				SeedP->RegionIds = PendingWear.RegionIds;
				SeedP->EdgeField = EmptyPatternUV;
				SeedP->PreviousEdgeBand = ReadDummy;
				SeedP->EdgeBand = ReadDummy;
				SeedP->LayerMask = PendingWear.FeatureMask;
				SeedP->WearMask = ReadDummy;
				SeedP->LinearWrapSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
				SeedP->OutputHeight = GraphBuilder.CreateUAV(WriteDummyA);
				SeedP->OutputWearMask = GraphBuilder.CreateUAV(WriteDummyB);
				SeedP->OutputEdgeBand = GraphBuilder.CreateUAV(Band[0]);
				SeedP->OutputNormal = GraphBuilder.CreateUAV(NormalDummy);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("Mixtormat.WornEdges.L%d.%d.EdgeSeed", LayerIndex, WearIndex),
					EdgeWearShader,
					SeedP,
					WearGroups);

				const float MaxIdRadiusMul = FMath::Max(
					1.0f + FMath::Abs(Wear.EdgeWearIdRadius) * Wear.EdgeWearIdVariation,
					0.15f);
				const int32 ConservativeRadius = FMath::Max(FMath::CeilToInt(static_cast<float>(Wear.EdgeWearRadius) * MaxIdRadiusMul * 1.80f), 1);
				int32 BandIndex = 0;
				int32 Covered = 0;
				int32 DilationStep = 1;
				while (Covered < ConservativeRadius)
				{
					const int32 ThisStep = FMath::Min(DilationStep, ConservativeRadius - Covered);
					const int32 ReadBand = BandIndex;
					const int32 WriteBand = 1 - ReadBand;
					FMixtormatEdgeWearCS::FParameters* DilateP =
						GraphBuilder.AllocParameters<FMixtormatEdgeWearCS::FParameters>();
					*DilateP = *SeedP;
					DilateP->Mode = 1;
					DilateP->EdgeStep = ThisStep;
					DilateP->PreviousEdgeBand = Band[ReadBand];
					DilateP->OutputEdgeBand = GraphBuilder.CreateUAV(Band[WriteBand]);
					FComputeShaderUtils::AddPass(
						GraphBuilder,
						RDG_EVENT_NAME("Mixtormat.WornEdges.L%d.%d.EdgeDilate%d", LayerIndex, WearIndex, Covered),
						EdgeWearShader,
						DilateP,
						WearGroups);
					BandIndex = WriteBand;
					Covered += ThisStep;
					DilationStep *= 2;
				}
				FinalEdgeBand = Band[BandIndex];
			}

			auto FillWearParameters = [&](FMixtormatEdgeWearCS::FParameters* P)
			{
				P->OutputSize = Request.Resolution;
				P->EdgeStep = 1;
				P->Radius = Wear.EdgeWearRadius;
				P->Slope = Wear.EdgeWearSlope;
				P->Strength = Wear.EdgeWearStrength;
				P->Feather = Wear.EdgeWearFeather;
				P->Directions = Wear.EdgeWearDirections;
				P->AngularAA = Wear.EdgeWearAngularAA;
				P->Gravity = Wear.EdgeWearGravity;
				P->GravityAngle = Wear.EdgeWearGravityAngle;
				P->Seed = Wear.EdgeWearSeed;
				P->MacroScale = Wear.EdgeWearMacroScale;
				P->MacroAmount = Wear.EdgeWearMacroAmount;
				P->CellScale = Wear.EdgeWearCellScale;
				P->CellAmount = Wear.EdgeWearCellAmount;
				P->RidgeScale = Wear.EdgeWearRidgeScale;
				P->RidgeAmount = Wear.EdgeWearRidgeAmount;
				P->MicroScale = Wear.EdgeWearMicroScale;
				P->MicroAmount = Wear.EdgeWearMicroAmount;
				P->WarpScale = Wear.EdgeWearWarpScale;
				P->WarpAmount = Wear.EdgeWearWarpAmount;
				P->NoiseContrast = Wear.EdgeWearNoiseContrast;
				P->IDVariation = Wear.EdgeWearIdVariation;
				P->IDRadius = Wear.EdgeWearIdRadius;
				P->IDSlope = Wear.EdgeWearIdSlope;
				P->IDStrength = Wear.EdgeWearIdStrength;
				P->IDNoise = Wear.EdgeWearIdNoise;
				P->HasPatternEdge = PendingWear.bHasPatternEdge ? 1u : 0u;
				P->SourceHeight = WearSourceH;
				P->WornHeight = ReadDummy;
				P->PreviousNormal = OutputN[WriteIndex];
				P->RegionIds = PendingWear.RegionIds;
				P->EdgeField = PendingWear.bHasPatternEdge ? PendingWear.PatternEdge : EmptyPatternUV;
				P->PreviousEdgeBand = ReadDummy;
				P->EdgeBand = FinalEdgeBand;
				P->LayerMask = PendingWear.FeatureMask;
				P->WearMask = ReadDummy;
				P->LinearWrapSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
			};

			FMixtormatEdgeWearCS::FParameters* WearP =
				GraphBuilder.AllocParameters<FMixtormatEdgeWearCS::FParameters>();
			FillWearParameters(WearP);
			WearP->Mode = 2;
			WearP->OutputHeight = GraphBuilder.CreateUAV(WornH);
			WearP->OutputWearMask = GraphBuilder.CreateUAV(EdgeWearMask);
			WearP->OutputEdgeBand = GraphBuilder.CreateUAV(WriteDummyC);
			WearP->OutputNormal = GraphBuilder.CreateUAV(NormalDummy);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.WornEdges.L%d.%d.Wear", LayerIndex, WearIndex),
				EdgeWearShader,
				WearP,
				WearGroups);

			PublishedMaskOutputs.Add(
				FPublishedMaskKey{
					Layer.LayerId,
					PendingWear.SourceChildIndex,
					FName(TEXT("Wear"))},
				EdgeWearMask);

			if (IsChildOutputPreviewTarget(Request, EMixtormatPreviewOutputKind::Mask,
				FName(TEXT("Wear")), LayerIndex, PendingWear.SourceChildIndex))
			{
				AddDebugPreviewMaskBlitPass(
					GraphBuilder, EdgeWearMask,
					Ctx.OutputDebug[Request.PublishedTargetIndex], Request.Resolution);
			}

			FRDGTextureRef WornRAM = GraphBuilder.CreateTexture(
				OutputRAM[WriteIndex]->Desc, TEXT("Mixtormat.WornEdges.HeightDerivedRAM"));
			AddHeightDerivedNormalPass(
				Ctx,
				WearSourceH,
				WornH,
				OutputN[WriteIndex],
				OutputRAM[WriteIndex],
				WornN,
				WornRAM,
				Request.Resolution,
				8.0f,
				true,
				TEXT("WornEdges"));
			FRDGTextureRef FinalWornRAM = WornRAM;

			// EdgeWearMask is the generated wear coverage, already gated by the
			// feature scope. It is the sole roughness mask; placement is not sampled
			// directly a second time here.
			const float RoughnessAmount =
				Wear.EdgeWearRoughnessWeight * Wear.EdgeWearRoughnessOffset;
			if (RoughnessAmount != 0.0f)
			{
				FRDGTextureRef ShadeRAM = GraphBuilder.CreateTexture(
					OutputRAM[WriteIndex]->Desc,
					TEXT("Mixtormat.WornEdges.RoughnessRAM"));
				FMixtormatCarveShadeCS::FParameters* ShadeP =
					GraphBuilder.AllocParameters<FMixtormatCarveShadeCS::FParameters>();
				ShadeP->OutputSize = Request.Resolution;
				ShadeP->RoughnessAmount = RoughnessAmount;
				ShadeP->CarveDepth = 1.0f;
				ShadeP->UseCoverageTexture = 1u;
				ShadeP->CoverageTexture = EdgeWearMask;
				ShadeP->SourceHeight = WornH;
				ShadeP->CarvedHeight = WornH;
				ShadeP->SourceRAM = FinalWornRAM;
				ShadeP->LinearWrapSampler =
					TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
				ShadeP->OutputRAM = GraphBuilder.CreateUAV(ShadeRAM);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME(
						"Mixtormat.WornEdges.L%d.%d.Roughness",
						LayerIndex, WearIndex),
					CarveShadeShader,
					ShadeP,
					WearGroups);
				FinalWornRAM = ShadeRAM;
			}

			AddCopyTexturePass(GraphBuilder, FinalWornRAM, OutputRAM[WriteIndex]);
			AddCopyTexturePass(GraphBuilder, WornH, HeightTargets[WriteIndex]);
			AddCopyTexturePass(GraphBuilder, WornN, OutputN[WriteIndex]);
		}
	}
}
