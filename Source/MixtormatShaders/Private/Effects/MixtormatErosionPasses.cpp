// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "../MixtormatGpuCompositorInternal.h"

#include "MixtormatEffectPassesPrivate.h"
#include "GlobalShader.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "ShaderParameterStruct.h"

class FMixtormatErosionCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatErosionCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatErosionCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(int32, ResamplePass)
		SHADER_PARAMETER(int32, ResampleRidge)
		SHADER_PARAMETER(int32, SmearPass)
		SHADER_PARAMETER(int32, SeedPass)
		SHADER_PARAMETER(uint32, WriteRidge)
		SHADER_PARAMETER(float, Amount)
		SHADER_PARAMETER(float, Depth)
		SHADER_PARAMETER(float, UnitDistance)
		SHADER_PARAMETER(FVector3f, Direction)
		SHADER_PARAMETER(int32, Radius)
		SHADER_PARAMETER(int32, Stride)
		SHADER_PARAMETER(float, GravityForce)
		SHADER_PARAMETER(float, SlopePower)
		SHADER_PARAMETER(float, Deposit)
		SHADER_PARAMETER(float, PreserveFlats)
		SHADER_PARAMETER(float, Smoothing)
		SHADER_PARAMETER(float, SecondaryAmount)
		SHADER_PARAMETER(float, Variation)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(uint32, UsePlacementMask)
		SHADER_PARAMETER(float, PlacementMaskTiling)
		SHADER_PARAMETER(uint32, InvertMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SeedHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousVelocity)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousNormal)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, LayerMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PlacementMaskTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PreviousRidge)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputRidge)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputVelocity)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputNormal)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatErosionCS,
	"/Plugin/Mixtormat/Private/MixtormatErosion.usf",
	"MainCS",
	SF_Compute);

namespace MixtormatGpuCompositor
{
	// Erosion is a post-layer filter: it carves what this layer actually composited, so the
	// child row only records it.
	void QueuePendingErosion(
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		const FEffectRenderData& Effect,
		FRDGTextureRef FeatureMask)
	{
		FPendingEffect& PendingErosion = LayerCtx.PendingErosion;

		// Erosion is a post-layer filter: it carves what this layer actually
		// composited, not the height underneath it. Running it here would let
		// the layer paint straight back over the carve.
		PendingErosion.Effect = &Effect;
		PendingErosion.FeatureMask = FeatureMask;
		PendingErosion.bHasScopedMask = Layer.Children.ContainsByPredicate(
			[&Child](const FChildRenderData& Candidate)
			{
				return Candidate.Type == EMixtormatLayerChildType::Mask
					&& Candidate.ScopeOwnerSourceChildIndex == Child.SourceChildIndex;
			});
	}

	// The erosion jump schedule, the same construction the Strata Carver uses. Strides halve
	// from the jump start down to 1 and then the cycle restarts, so a long iteration budget
	// alternates reach and relaxation instead of spending everything past the first few
	// iterations wearing at the same distance. The wide passes carry wear across the tile; the
	// stride-1 passes are where the carve accumulates and detail forms.
	//
	// The schedule is a function of the jump start alone -- nothing is keyed to the iteration
	// count, which is what lets Iterations be 1 or 32 without a special case: 1 runs the
	// widest jump only, 8 runs most of a cycle, neither is a different algorithm. The stride
	// multiplies the resolution-normalized wear distance, so the reach is identical at
	// 1K/2K/4K.
	constexpr int32 ErosionJumpStart = 8;

	int32 ErosionJumpStride(const int32 Iteration, const int32 JumpStart)
	{
		const int32 Start = FMath::Max(JumpStart, 1);
		int32 Halvings = 0;
		while ((Start >> Halvings) > 1)
		{
			++Halvings;
		}
		const int32 CycleLength = Halvings + 1;
		return FMath::Max(Start >> (Iteration % CycleLength), 1);
	}

	// Erosion filters the layer output: it reads the height and normal this layer just
	// composited, carves the height, derives the normal change from what it removed, and writes
	// both back. Also the one place every layer -- eroding or not -- hands the next layer a
	// ridge, which is why the copy/clear above the filter is part of this and not optional.
	void AddErosionPasses(
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
		const int32 WriteIndex = LayerCtx.LayerIndex & 1;
		FRDGTextureRef* const RidgeTargets = LayerCtx.RidgeTargets;
		const FRDGTextureRef PeelFieldDummy = LayerCtx.PeelFieldDummy;
		FPendingEffect& PendingErosion = LayerCtx.PendingErosion;
		TShaderMapRef<FMixtormatCarveShadeCS> CarveShadeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FMixtormatErosionCS> ErosionShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		// Amount 0 is an exact identity in the erosion shader: the wear delta falls to
		// zero, the height comes back as it went in and the normal is copied through.
		// It was still costing the full filter -- up to 32 wear iterations at twice the
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
			PendingErosion.Effect != nullptr && PendingErosion.Effect->ErosionAmount > 0.0f;

		// Every layer hands the next one a ridge, whether or not it erodes. A layer
		// that left the slot alone would pass on the ridge from two layers back,
		// which reads as the mask signal being correct on some layers and stale on
		// others. Eroding layers overwrite this below.
		if (!PendingErosion.Effect)
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
			const FEffectRenderData& Ero = *PendingErosion.Effect;
			const bool bUseLegacyPlacementMask =
				!PendingErosion.bHasScopedMask && Ero.ErosionPlacementMask.IsValid();
			FRDGTextureRef ErosionPlacementMask = bUseLegacyPlacementMask
				? RegisterTexture(
					GraphBuilder,
					RegisteredTextures,
					Ero.ErosionPlacementMask,
					TEXT("Mixtormat.ErosionPlacementMask"))
				: PeelFieldDummy;

			// Erosion runs at twice the composition resolution, capped at 4096,
			// then resamples back. Wear is high-frequency work: the derivative spans
			// are texel-space, so at composition resolution the readings would run
			// out of samples before the wear could read as anything but noise.
			// Above 4096 the cost stops buying visible detail.
			const FIntPoint EroRes(
				FMath::Min(Request.Resolution.X * 2, 4096),
				FMath::Min(Request.Resolution.Y * 2, 4096));
			const bool bResample = EroRes != Request.Resolution;

			// The height chain is R32F, not R16F like the rest of the compositor.
			// Every quantity this filter derives is a difference of two nearly
			// equal heights, and half floats do not survive that.
			//
			// A half around mid height has a ULP of 2^-11, about 4.9e-4. The wear
			// response thresholds a slope built from differences of neighbouring
			// heights, so quantisation alone puts visible noise on that slope and
			// the threshold turns it into dithered carve before the normal pass
			// amplifies anything.
			//
			// The normal pass is the second victim: it differences the carve depth
			// between neighbours, and those differences are far smaller than the
			// carve itself, so they land on nought, one or two ULP -- a handful of
			// distinct slopes over the whole carve. The Hessian is the third, since
			// a second difference divided by StepUV squared multiplies its error by
			// about a million.
			//
			// The wear analysis reads the same chain, so it keeps the R32F format too:
			// its quadrant means and central differences are differences of nearly equal
			// heights, and half floats would quantize the slope response before the
			// threshold ever sees it.
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
			FRDGTextureRef EroSeed = GraphBuilder.CreateTexture(EroDesc, TEXT("Mixtormat.ErosionSeedHeight"));
			FRDGTextureRef EroN = GraphBuilder.CreateTexture(EroNormalDesc, TEXT("Mixtormat.ErosionN"));
			// The layer normal every carving pass reads, lifted to erosion resolution.
			FRDGTextureRef EroSrcN = GraphBuilder.CreateTexture(EroNormalDesc, TEXT("Mixtormat.ErosionSrcN"));

			// Stores the authored downhill tangent for the optional post-solve deposit smear.
			const FRDGTextureDesc EroVelDesc = FRDGTextureDesc::Create2D(
				EroRes,
				PF_FloatRGBA,
				FClearValueBinding::Black,
				TexCreate_ShaderResource | TexCreate_UAV);
			FRDGTextureRef EroVel[2] = {
				GraphBuilder.CreateTexture(EroVelDesc, TEXT("Mixtormat.ErosionVelA")),
				GraphBuilder.CreateTexture(EroVelDesc, TEXT("Mixtormat.ErosionVelB"))};
			FRDGTextureRef EroVelDummy = GraphBuilder.CreateTexture(
				FRDGTextureDesc::Create2D(
					FIntPoint(1, 1),
					PF_FloatRGBA,
					FClearValueBinding::Black,
					TexCreate_ShaderResource | TexCreate_UAV),
				TEXT("Mixtormat.ErosionVelDummy"));
			// Iterations never write normals -- the shared height-derived pass owns them,
			// after the final iteration -- so they bind this 1x1 stand-in instead of
			// paying a full-screen normal copy per pass.
			FRDGTextureRef ErosionNormalDummy = GraphBuilder.CreateTexture(
				FRDGTextureDesc::Create2D(
					FIntPoint(1, 1),
					EroNormalDesc.Format,
					FClearValueBinding::White,
					TexCreate_ShaderResource | TexCreate_UAV),
				TEXT("Mixtormat.ErosionNormalDummy"));

			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(EroRidge), FVector4f(0.0f, 0.0f, 0.0f, 0.0f));
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(EroVel[0]), FVector4f(0.0f));
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(EroVelDummy), FVector4f(0.0f));
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(ErosionNormalDummy), FVector4f(0.0f));

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
				RP->ResamplePass = 1;
				RP->ResampleRidge = bCarryRidge ? 1 : 0;
				RP->SmearPass = 0;
				RP->SeedPass = 0;
				RP->WriteRidge = 0u;
				RP->Stride = 1;
				RP->PreviousRidge = InRidge;
				RP->PreviousHeight = InH;
				RP->SourceHeight = InH;
				RP->SeedHeight = EroSeed;
				RP->PreviousVelocity = EroVelDummy;
				RP->LayerMask = PendingErosion.FeatureMask;
				RP->UsePlacementMask = bUseLegacyPlacementMask ? 1u : 0u;
				RP->PlacementMaskTiling = Ero.ErosionMaskTiling;
				RP->PlacementMaskTexture = ErosionPlacementMask;
				RP->PreviousNormal = InN;
				RP->LinearWrapSampler =
					TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
				RP->OutputHeight = GraphBuilder.CreateUAV(OutH);
				RP->OutputRidge = GraphBuilder.CreateUAV(
					bCarryRidge ? OutRidgeTarget : ResampleRidgeDummy);
				RP->OutputVelocity = GraphBuilder.CreateUAV(EroVelDummy);
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

			// Bound the number of eikonal relaxations to keep dispatch work predictable.
			const int32 ErosionIterations = FMath::Clamp(Ero.ErosionIterations, 1, 128);

			// Horizon exposure always reads SourceH. Iterations only relax the source-relative
			// carve offset, so wear cannot feed back into its own exposure or reshape the source.
			auto SetErosionParameters = [&](FMixtormatErosionCS::FParameters* Parameters)
			{
				Parameters->OutputSize = EroRes;
				Parameters->ResamplePass = 0;
				Parameters->ResampleRidge = 0;
				Parameters->SmearPass = 0;
				Parameters->SeedPass = 0;
				Parameters->WriteRidge = 1u;
				Parameters->Amount = Ero.ErosionAmount;
				Parameters->Depth = Ero.ErosionDepth;
				Parameters->UnitDistance = Ero.ErosionUnitDistance;
				Parameters->Direction = Ero.ErosionDirection;
				Parameters->Radius = Ero.ErosionRadius;
				Parameters->GravityForce = Ero.ErosionGravityForce;
				Parameters->SlopePower = Ero.ErosionSlopePower;
				Parameters->Deposit = Ero.ErosionDeposit;
				Parameters->PreserveFlats = Ero.ErosionPreserveFlats;
				Parameters->Smoothing = Ero.ErosionSmoothing;
				Parameters->SecondaryAmount = Ero.ErosionSecondaryAmount;
				Parameters->Variation = Ero.ErosionVariation;
				Parameters->Seed = (uint32)Ero.ErosionSeed;
				// Overwritten per iteration below; 1 is the accumulate-at-home stride the
				// resample lambda and any pass that does not carry a schedule keeps.
				Parameters->Stride = 1;
				Parameters->PreviousVelocity = EroVelDummy;
				Parameters->OutputVelocity = GraphBuilder.CreateUAV(EroVelDummy);
				Parameters->UsePlacementMask = bUseLegacyPlacementMask ? 1u : 0u;
				Parameters->PlacementMaskTiling = Ero.ErosionMaskTiling;
				Parameters->InvertMask =
					!PendingErosion.bHasScopedMask && Ero.bErosionInvertMask ? 1u : 0u;
				Parameters->SourceHeight = SourceH;
				Parameters->SeedHeight = EroSeed;
				Parameters->PreviousNormal = EroSrcN;
				Parameters->LayerMask = PendingErosion.FeatureMask;
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

			// Compute horizon visibility once from the immutable source. The relaxations
			// reuse this seed texture instead of repeating the ray march every iteration.
			FMixtormatErosionCS::FParameters* SeedParameters =
				GraphBuilder.AllocParameters<FMixtormatErosionCS::FParameters>();
			SetErosionParameters(SeedParameters);
			SeedParameters->SeedPass = 1;
			SeedParameters->WriteRidge = 1u;
			SeedParameters->PreviousHeight = SourceH;
			SeedParameters->SeedHeight = SourceH;
			SeedParameters->OutputHeight = GraphBuilder.CreateUAV(EroSeed);
			SeedParameters->OutputRidge = GraphBuilder.CreateUAV(EroRidge);
			SeedParameters->OutputVelocity = GraphBuilder.CreateUAV(EroVelDummy);
			SeedParameters->OutputNormal = GraphBuilder.CreateUAV(ErosionNormalDummy);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.Erosion.L%d.HorizonSeeds", LayerIndex),
				ErosionShader,
				SeedParameters,
				ErosionGroups);

			// Ping-pong the eikonal envelope; immutable SourceH remains the obstacle and
			// seed reference on every pass.

			for (int32 Iteration = 0; Iteration < ErosionIterations; ++Iteration)
			{
				FMixtormatErosionCS::FParameters* IterationParameters =
					GraphBuilder.AllocParameters<FMixtormatErosionCS::FParameters>();
				SetErosionParameters(IterationParameters);
				IterationParameters->Stride = ErosionJumpStride(Iteration, ErosionJumpStart);
				IterationParameters->PreviousHeight =
					Iteration == 0 ? EroSeed : EroH[(Iteration - 1) & 1];
				IterationParameters->OutputHeight =
					GraphBuilder.CreateUAV(EroH[Iteration & 1]);
				// Direction is carried for optional deposit smear. The shared height-derived
				// pass owns normals.
				IterationParameters->PreviousVelocity =
					Iteration == 0 ? EroVel[0] : EroVel[(Iteration - 1) & 1];
				IterationParameters->OutputVelocity =
					GraphBuilder.CreateUAV(EroVel[Iteration & 1]);
				IterationParameters->WriteRidge = 0u;
				IterationParameters->OutputRidge = GraphBuilder.CreateUAV(ResampleRidgeDummy);
				IterationParameters->OutputNormal =
					GraphBuilder.CreateUAV(ErosionNormalDummy);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("Mixtormat.Erosion.L%d.Filter%d", LayerIndex, Iteration),
					ErosionShader,
					IterationParameters,
					ErosionGroups);
			}

			// Optional refill-only downstream smear. It remains capped by SourceH and is skipped
			// exactly when Deposit is zero.
			FRDGTextureRef Result = EroH[(ErosionIterations - 1) & 1];
			if (Ero.ErosionDeposit > 0.0f)
			{
				FMixtormatErosionCS::FParameters* SmearParameters =
					GraphBuilder.AllocParameters<FMixtormatErosionCS::FParameters>();
				SetErosionParameters(SmearParameters);
				SmearParameters->SmearPass = 1;
				SmearParameters->SeedPass = 0;
				SmearParameters->WriteRidge = 0u;
				SmearParameters->PreviousHeight = Result;
				SmearParameters->SourceHeight = SourceH;
				SmearParameters->PreviousVelocity = EroVel[(ErosionIterations - 1) & 1];
				SmearParameters->OutputHeight =
					GraphBuilder.CreateUAV(EroH[ErosionIterations & 1]);
				SmearParameters->OutputVelocity = GraphBuilder.CreateUAV(EroVelDummy);
				SmearParameters->OutputRidge = GraphBuilder.CreateUAV(ResampleRidgeDummy);
				SmearParameters->OutputNormal = GraphBuilder.CreateUAV(ErosionNormalDummy);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("Mixtormat.Erosion.L%d.Smear", LayerIndex),
					ErosionShader,
					SmearParameters,
					ErosionGroups);
				Result = EroH[ErosionIterations & 1];
			}

			AddHeightDerivedNormalPass(
				Ctx,
				SourceH,
				Result,
				EroSrcN,
				OutputRAM[WriteIndex],
				EroN,
				nullptr,
				EroRes,
				HeightDerivedNormalStrength,
				false,
				TEXT("Erosion"));

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

			// Roughness is the only packed surface channel erosion changes. Skip the
			// full-resolution pass when its mask weight is neutral.
			if (Ero.ErosionRoughnessAmount != 0.0f)
			{
				// Through scratch and back rather than in place: RAM cannot be bound as
				// both SRV and UAV on the same dispatch.
				FRDGTextureRef ShadeRAM = GraphBuilder.CreateTexture(
					OutputRAM[WriteIndex]->Desc, TEXT("Mixtormat.ErosionShadeRAM"));

				FMixtormatCarveShadeCS::FParameters* SP =
					GraphBuilder.AllocParameters<FMixtormatCarveShadeCS::FParameters>();
				SP->OutputSize = Request.Resolution;
				SP->RoughnessAmount = Ero.ErosionRoughnessAmount;
				SP->CarveDepth = Ero.ErosionCarveDepth;

				// Erosion recovers coverage from the height pair, so the coverage
				// slot is unread here. Bind an existing valid scalar texture.
				SP->UseCoverageTexture = 0;
				SP->CoverageTexture = SourceH;

				SP->SourceHeight = SourceH;
				SP->CarvedHeight = Result;
				SP->SourceRAM = OutputRAM[WriteIndex];
				SP->LinearWrapSampler =
					TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
				SP->OutputRAM = GraphBuilder.CreateUAV(ShadeRAM);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("Mixtormat.Erosion.L%d.Roughness", LayerIndex),
					CarveShadeShader,
					SP,
					FIntVector(
						FMath::DivideAndRoundUp(Request.Resolution.X, 8),
						FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
						1));

				AddCopyTexturePass(GraphBuilder, ShadeRAM, OutputRAM[WriteIndex]);
			}
		}
	}
}
