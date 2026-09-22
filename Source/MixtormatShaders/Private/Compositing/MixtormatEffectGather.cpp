// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Compositing/MixtormatEffectGather.h"

#include "MixtormatParameterDefinition.h"

// The shared sanitize pair every migrated family gathers through: non-finite falls back to the
// reflected CDO default, then the contract's HardMin/HardMax clamp where they exist. These were
// per-block lambdas in RequestComposeInternal; one file-local pair serves every family here.
namespace
{
	float EffectFloat(const FName Name, const float Value)
	{
		return MixtormatParameterContracts::SanitizeFloat(
			EMixtormatParameterOwnerType::Effect, Name, Value);
	}

	int32 EffectInt(const FName Name, const int32 Value)
	{
		return MixtormatParameterContracts::SanitizeInt32(
			EMixtormatParameterOwnerType::Effect, Name, Value);
	}
}

namespace MixtormatGpuCompositor
{

void GatherErosion(FEffectRenderData& EffectData, const FMixtormatLayerEffect& LayerEffect)
{
	// The shader keeps its own epsilon guards at the division sites; SanitizeFloat
	// adds the finite guard and the definition hard floors without clamping art.
	EffectData.ErosionAmount = EffectFloat(TEXT("ErosionAmount"), LayerEffect.ErosionAmount);
	EffectData.ErosionDepth = EffectFloat(TEXT("ErosionDepth"), LayerEffect.ErosionDepth);
	EffectData.ErosionRadius = EffectInt(TEXT("ErosionRadius"), LayerEffect.ErosionRadius);
	EffectData.ErosionIterations = EffectInt(TEXT("ErosionIterations"), LayerEffect.ErosionIterations);
	EffectData.ErosionGravityForce = EffectFloat(TEXT("ErosionGravityForce"), LayerEffect.ErosionGravityForce);
	EffectData.ErosionSlopePower = EffectFloat(TEXT("ErosionSlopePower"), LayerEffect.ErosionSlopePower);
	EffectData.ErosionDeposit = EffectFloat(TEXT("ErosionDeposit"), LayerEffect.ErosionDeposit);
	EffectData.ErosionPreserveFlats = EffectFloat(TEXT("ErosionPreserveFlats"), LayerEffect.ErosionPreserveFlats);
	EffectData.ErosionSmoothing = EffectFloat(TEXT("ErosionSmoothing"), LayerEffect.ErosionSmoothing);
	EffectData.ErosionVariation = EffectFloat(TEXT("ErosionVariation"), LayerEffect.ErosionVariation);
	EffectData.ErosionSeed = EffectInt(TEXT("ErosionSeed"), LayerEffect.ErosionSeed);
	EffectData.ErosionMaskTiling = static_cast<float>(
		EffectInt(TEXT("ErosionMaskTiling"), LayerEffect.ErosionMaskTiling));
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

	EffectData.ErosionRoughnessAmount = EffectFloat(TEXT("ErosionRoughnessAmount"), LayerEffect.ErosionRoughnessAmount);
	EffectData.ErosionCarveDepth = EffectFloat(TEXT("ErosionCarveDepth"), LayerEffect.ErosionCarveDepth);
}

void GatherGrade(FEffectRenderData& EffectData, const FMixtormatLayerEffect& LayerEffect)
{
	EffectData.GradeAmount = EffectFloat(TEXT("GradeAmount"), LayerEffect.GradeAmount);
	EffectData.GradeTonemap = static_cast<int32>(LayerEffect.GradeTonemap);
	EffectData.GradeTonemapStrength =
		EffectFloat(TEXT("GradeTonemapStrength"), LayerEffect.GradeTonemapStrength);
	EffectData.GradeBrightness = EffectFloat(TEXT("GradeBrightness"), LayerEffect.GradeBrightness);
	EffectData.GradeContrast = EffectFloat(TEXT("GradeContrast"), LayerEffect.GradeContrast);
	EffectData.GradeContrastPivot = EffectFloat(TEXT("GradeContrastPivot"), LayerEffect.GradeContrastPivot);
	EffectData.GradeGamma = EffectFloat(TEXT("GradeGamma"), LayerEffect.GradeGamma);
	EffectData.GradeInputMin = EffectFloat(TEXT("GradeInputMin"), LayerEffect.GradeInputMin);
	EffectData.GradeInputMax = EffectFloat(TEXT("GradeInputMax"), LayerEffect.GradeInputMax);
	EffectData.GradeOutputMin = EffectFloat(TEXT("GradeOutputMin"), LayerEffect.GradeOutputMin);
	EffectData.GradeOutputMax = EffectFloat(TEXT("GradeOutputMax"), LayerEffect.GradeOutputMax);
	EffectData.GradeChannelBias = FVector3f(
		EffectFloat(TEXT("GradeBiasR"), LayerEffect.GradeBiasR),
		EffectFloat(TEXT("GradeBiasG"), LayerEffect.GradeBiasG),
		EffectFloat(TEXT("GradeBiasB"), LayerEffect.GradeBiasB));
	EffectData.bGradeInvertMask = LayerEffect.bGradeInvertMask;
}

void GatherBreakup(FEffectRenderData& EffectData, const FMixtormatLayerEffect& LayerEffect)
{
	// Authored values are validated against the canonical parameter definitions
	// (MixtormatParameterDefinition) rather than per-field literals: the hard bounds
	// and NaN fallbacks live in one table the serialized defaults and the render data
	// also read. Anything genuinely derived -- the cell-count ratios and the size
	// range -- stays local to the derivation below.

	EffectData.BreakupAmount = EffectFloat(TEXT("BreakupAmount"), LayerEffect.BreakupAmount);

	// Mid and Detail are derived rather than authored, so the three families stay in
	// a sensible ratio instead of being three sliders to keep in step by hand. Each
	// is forced at least one cell above the family below it: two families at the same
	// count would beat identically and the pair would read as one.
	const int32 MacroCells = EffectInt(TEXT("BreakupScale"), LayerEffect.BreakupScale);
	const float Detail = EffectFloat(TEXT("BreakupDetail"), LayerEffect.BreakupDetail);
	const int32 MidCells = FMath::Max(
		MacroCells + 1,
		FMath::RoundToInt(MacroCells * FMath::Lerp(1.50f, 2.15f, Detail)));
	const int32 DetailCells = FMath::Max(
		MidCells + 1,
		FMath::RoundToInt(MacroCells * FMath::Lerp(2.60f, 4.10f, Detail)));
	EffectData.BreakupMacroCells = MacroCells;
	EffectData.BreakupMidCells = MidCells;
	EffectData.BreakupDetailCells = DetailCells;

	const float Size = EffectFloat(TEXT("BreakupSize"), LayerEffect.BreakupSize);
	const float SizeVariation = EffectFloat(TEXT("BreakupSizeVariation"), LayerEffect.BreakupSizeVariation);
	// Floored above zero: a zero or negative radius divides by itself in the shape
	// metric, and a piece that cannot exist is not the same thing as Density 0.
	EffectData.BreakupSizeMin = FMath::Max(Size * (1.0f - SizeVariation), 1.0e-4f);
	EffectData.BreakupSizeMax = FMath::Max(Size * (1.0f + SizeVariation), EffectData.BreakupSizeMin);

	EffectData.BreakupDensity = EffectFloat(TEXT("BreakupDensity"), LayerEffect.BreakupDensity);
	EffectData.BreakupStretch = EffectFloat(TEXT("BreakupStretch"), LayerEffect.BreakupStretch);
	EffectData.BreakupAngularity = EffectFloat(TEXT("BreakupAngularity"), LayerEffect.BreakupAngularity);
	EffectData.BreakupIrregularity = EffectFloat(TEXT("BreakupIrregularity"), LayerEffect.BreakupIrregularity);

	EffectData.BreakupMidOperation = static_cast<int32>(LayerEffect.BreakupMidOperation);
	EffectData.BreakupDetailOperation = static_cast<int32>(LayerEffect.BreakupDetailOperation);
	EffectData.BreakupSmoothness = EffectFloat(TEXT("BreakupSmoothness"), LayerEffect.BreakupSmoothness);
	EffectData.BreakupInset = EffectFloat(TEXT("BreakupInset"), LayerEffect.BreakupInset);

	EffectData.BreakupDistortion = EffectFloat(TEXT("BreakupDistortion"), LayerEffect.BreakupDistortion);
	EffectData.BreakupDistortionFrequency =
		EffectInt(TEXT("BreakupDistortionFrequency"), LayerEffect.BreakupDistortionFrequency);
	EffectData.bBreakupInvert = LayerEffect.bBreakupInvert;

	// Structural amounts, finite-guarded by SanitizeFloat and not range-clamped past
	// what the shader needs to stay safe: these are artistic heights and widths, and
	// every divisor they reach is floored in the shader.
	EffectData.BreakupRelief = EffectFloat(TEXT("BreakupRelief"), LayerEffect.BreakupRelief);
	EffectData.BreakupThicknessVariation = EffectFloat(TEXT("BreakupThicknessVariation"), LayerEffect.BreakupThicknessVariation);
	EffectData.BreakupGapWidth = EffectFloat(TEXT("BreakupGapWidth"), LayerEffect.BreakupGapWidth);
	EffectData.BreakupGapDepth = EffectFloat(TEXT("BreakupGapDepth"), LayerEffect.BreakupGapDepth);
	EffectData.BreakupGapVariation = EffectFloat(TEXT("BreakupGapVariation"), LayerEffect.BreakupGapVariation);
	EffectData.BreakupFold = EffectFloat(TEXT("BreakupFold"), LayerEffect.BreakupFold);
	EffectData.BreakupFoldWidth = EffectFloat(TEXT("BreakupFoldWidth"), LayerEffect.BreakupFoldWidth);
	EffectData.BreakupCrease = EffectFloat(TEXT("BreakupCrease"), LayerEffect.BreakupCrease);
	EffectData.BreakupCreaseWidth = EffectFloat(TEXT("BreakupCreaseWidth"), LayerEffect.BreakupCreaseWidth);
	EffectData.BreakupPush = EffectFloat(TEXT("BreakupPush"), LayerEffect.BreakupPush);
	EffectData.BreakupPushWidth = EffectFloat(TEXT("BreakupPushWidth"), LayerEffect.BreakupPushWidth);
	EffectData.BreakupPushRelief = EffectFloat(TEXT("BreakupPushRelief"), LayerEffect.BreakupPushRelief);
	EffectData.BreakupVariation = EffectFloat(TEXT("BreakupVariation"), LayerEffect.BreakupVariation);
	EffectData.BreakupRoughnessAmount = EffectFloat(TEXT("BreakupRoughnessAmount"), LayerEffect.BreakupRoughnessAmount);
	EffectData.BreakupNormalStrength = EffectFloat(TEXT("BreakupNormalStrength"), LayerEffect.BreakupNormalStrength);
	EffectData.BreakupNormalSharpness = EffectFloat(TEXT("BreakupNormalSharpness"), LayerEffect.BreakupNormalSharpness);
	EffectData.BreakupAOAmount = EffectFloat(TEXT("BreakupAOAmount"), LayerEffect.BreakupAOAmount);
	EffectData.BreakupAORadius = EffectFloat(TEXT("BreakupAORadius"), LayerEffect.BreakupAORadius);

	EffectData.BreakupMaskTiling = static_cast<float>(
		EffectInt(TEXT("BreakupMaskTiling"), LayerEffect.BreakupMaskTiling));
	EffectData.bBreakupInvertMask = LayerEffect.bBreakupInvertMask;
	{
		UTexture2D* PlacementMask = LayerEffect.BreakupMaskTexture.LoadSynchronous();
		if (!PlacementMask)
		{
			if (const UMixtormatMask* MaskAsset = LayerEffect.BreakupMask.LoadSynchronous())
			{
				PlacementMask = MaskAsset->MaskTexture.Get();
			}
		}
		if (PlacementMask)
		{
			EffectData.BreakupPlacementMask = GetTextureRHI(PlacementMask);
		}
	}
	EffectData.BreakupSeed = static_cast<uint32>(
		EffectInt(TEXT("BreakupSeed"), LayerEffect.BreakupSeed));
}

void GatherLayerBlur(FEffectRenderData& EffectData, const FMixtormatLayerEffect& LayerEffect)
{
	EffectData.LayerBlurRadiusX = EffectFloat(TEXT("LayerBlurRadiusX"), LayerEffect.LayerBlurRadiusX);
	EffectData.LayerBlurRadiusY = EffectFloat(TEXT("LayerBlurRadiusY"), LayerEffect.LayerBlurRadiusY);
	EffectData.LayerBlurScope = static_cast<uint32>(LayerEffect.LayerBlurScope);
	EffectData.LayerBlurAmount = EffectFloat(TEXT("LayerBlurAmount"), LayerEffect.LayerBlurAmount);
	EffectData.bLayerBlurHeight = LayerEffect.bLayerBlurHeight;
}

void GatherFlowWarp(FEffectRenderData& EffectData, const FMixtormatLayerEffect& LayerEffect)
{
	EffectData.FlowWarpAmount = EffectFloat(TEXT("FlowWarpAmount"), LayerEffect.FlowWarpAmount);
	EffectData.FlowWarpWeight = EffectFloat(TEXT("FlowWarpWeight"), LayerEffect.FlowWarpWeight);
	EffectData.FlowWarpScale = EffectInt(TEXT("FlowWarpScale"), LayerEffect.FlowWarpScale);
	EffectData.FlowWarpDirection = EffectFloat(TEXT("FlowWarpDirection"), LayerEffect.FlowWarpDirection);
	EffectData.FlowWarpSeed = static_cast<uint32>(
		EffectInt(TEXT("FlowWarpSeed"), LayerEffect.FlowWarpSeed));
	EffectData.FlowWarpMaskSlopeInfluence =
		EffectFloat(TEXT("FlowWarpMaskSlopeInfluence"), LayerEffect.FlowWarpMaskSlopeInfluence);
	EffectData.FlowWarpHeightSlopeInfluence =
		EffectFloat(TEXT("FlowWarpHeightSlopeInfluence"), LayerEffect.FlowWarpHeightSlopeInfluence);
	EffectData.FlowWarpDerivativeKernel = FVector2f(
		EffectFloat(TEXT("FlowWarpDerivativeKernelX"), LayerEffect.FlowWarpDerivativeKernelX),
		EffectFloat(TEXT("FlowWarpDerivativeKernelY"), LayerEffect.FlowWarpDerivativeKernelY));
	EffectData.FlowWarpBlendMode = static_cast<uint32>(LayerEffect.FlowWarpBlendMode);
}

void GatherWornEdges(FEffectRenderData& EffectData, const FMixtormatLayerEffect& LayerEffect)
{
	EffectData.EdgeWearRadius = EffectInt(TEXT("EdgeWearRadius"), LayerEffect.EdgeWearRadius);
	EffectData.EdgeWearSlope = EffectFloat(TEXT("EdgeWearSlope"), LayerEffect.EdgeWearSlope);
	EffectData.EdgeWearStrength = EffectFloat(TEXT("EdgeWearStrength"), LayerEffect.EdgeWearStrength);
	EffectData.EdgeWearFeather = EffectFloat(TEXT("EdgeWearFeather"), LayerEffect.EdgeWearFeather);
	EffectData.EdgeWearDirections = EffectInt(TEXT("EdgeWearDirections"), LayerEffect.EdgeWearDirections);
	EffectData.EdgeWearAngularAA = EffectFloat(TEXT("EdgeWearAngularAA"), LayerEffect.EdgeWearAngularAA);
	EffectData.EdgeWearGravity = EffectFloat(TEXT("EdgeWearGravity"), LayerEffect.EdgeWearGravity);
	EffectData.EdgeWearGravityAngle = EffectFloat(TEXT("EdgeWearGravityAngle"), LayerEffect.EdgeWearGravityAngle);
	EffectData.EdgeWearSeed = static_cast<uint32>(
		EffectInt(TEXT("EdgeWearSeed"), LayerEffect.EdgeWearSeed));
	EffectData.EdgeWearMacroScale = EffectInt(TEXT("EdgeWearMacroScale"), LayerEffect.EdgeWearMacroScale);
	EffectData.EdgeWearMacroAmount = EffectFloat(TEXT("EdgeWearMacroAmount"), LayerEffect.EdgeWearMacroAmount);
	EffectData.EdgeWearCellScale = EffectInt(TEXT("EdgeWearCellScale"), LayerEffect.EdgeWearCellScale);
	EffectData.EdgeWearCellAmount = EffectFloat(TEXT("EdgeWearCellAmount"), LayerEffect.EdgeWearCellAmount);
	EffectData.EdgeWearRidgeScale = EffectInt(TEXT("EdgeWearRidgeScale"), LayerEffect.EdgeWearRidgeScale);
	EffectData.EdgeWearRidgeAmount = EffectFloat(TEXT("EdgeWearRidgeAmount"), LayerEffect.EdgeWearRidgeAmount);
	EffectData.EdgeWearMicroScale = EffectInt(TEXT("EdgeWearMicroScale"), LayerEffect.EdgeWearMicroScale);
	EffectData.EdgeWearMicroAmount = EffectFloat(TEXT("EdgeWearMicroAmount"), LayerEffect.EdgeWearMicroAmount);
	EffectData.EdgeWearWarpScale = EffectInt(TEXT("EdgeWearWarpScale"), LayerEffect.EdgeWearWarpScale);
	EffectData.EdgeWearWarpAmount = EffectFloat(TEXT("EdgeWearWarpAmount"), LayerEffect.EdgeWearWarpAmount);
	EffectData.EdgeWearNoiseContrast = EffectFloat(TEXT("EdgeWearNoiseContrast"), LayerEffect.EdgeWearNoiseContrast);
	EffectData.EdgeWearIdVariation = EffectFloat(TEXT("EdgeWearIdVariation"), LayerEffect.EdgeWearIdVariation);
	EffectData.EdgeWearIdRadius = EffectFloat(TEXT("EdgeWearIdRadius"), LayerEffect.EdgeWearIdRadius);
	EffectData.EdgeWearIdSlope = EffectFloat(TEXT("EdgeWearIdSlope"), LayerEffect.EdgeWearIdSlope);
	EffectData.EdgeWearIdStrength = EffectFloat(TEXT("EdgeWearIdStrength"), LayerEffect.EdgeWearIdStrength);
	EffectData.EdgeWearIdNoise = EffectFloat(TEXT("EdgeWearIdNoise"), LayerEffect.EdgeWearIdNoise);
	EffectData.EdgeWearRoughnessWeight = EffectFloat(TEXT("EdgeWearRoughnessWeight"), LayerEffect.EdgeWearRoughnessWeight);
	EffectData.EdgeWearRoughnessOffset = EffectFloat(TEXT("EdgeWearRoughnessOffset"), LayerEffect.EdgeWearRoughnessOffset);
}

void GatherStain(
	FEffectRenderData& EffectData, const FMixtormatLayerEffect& LayerEffect, bool& bHasMask)
{
	EffectData.StainMode = static_cast<int32>(LayerEffect.StainMode);
	EffectData.StainSourceMaskTiling = FMath::Max(
		1.0f, static_cast<float>(LayerEffect.StainSourceMaskTiling));
	EffectData.StainDirtMaskTiling = FMath::Max(
		1.0f, static_cast<float>(LayerEffect.StainDirtMaskTiling));
	EffectData.bStainSourceMaskInvert = LayerEffect.bStainSourceMaskInvert;
	EffectData.bStainDirtMaskInvert = LayerEffect.bStainDirtMaskInvert;
	EffectData.StainIterations = FMath::Clamp(LayerEffect.StainIterations, 4, 64);
	EffectData.StainSeed = static_cast<uint32>(FMath::Max(LayerEffect.StainSeed, 1));
	EffectData.StainSourceAmount = LayerEffect.StainSourceAmount;
	EffectData.StainGravity = LayerEffect.StainGravity;
	EffectData.StainSurfaceFollow = LayerEffect.StainSurfaceFollow;
	EffectData.StainSpread = LayerEffect.StainSpread;
	EffectData.StainAccumulation = LayerEffect.StainAccumulation;
	EffectData.StainAbsorption = LayerEffect.StainAbsorption;
	EffectData.StainDrying = LayerEffect.StainDrying;
	EffectData.StainDirtAmount = LayerEffect.StainDirtAmount;
	EffectData.StainConcavityWeight = LayerEffect.StainConcavityWeight;
	EffectData.StainConvexityWeight = LayerEffect.StainConvexityWeight;
	EffectData.StainOcclusionWeight = LayerEffect.StainOcclusionWeight;
	EffectData.StainHeightWeight = LayerEffect.StainHeightWeight;
	EffectData.StainSourceHeightBias = LayerEffect.StainSourceHeightBias;
	EffectData.StainSlopeWeight = LayerEffect.StainSlopeWeight;
	EffectData.StainSurfaceResponse = LayerEffect.StainSurfaceResponse;

	const auto ResolveStainMask = [](const TSoftObjectPtr<UMixtormatMask>& MaskAssetRef,
		const TSoftObjectPtr<UTexture2D>& TextureRef) -> FTextureRHIRef
	{
		UTexture2D* Texture = TextureRef.LoadSynchronous();
		if (!Texture)
		{
			if (const UMixtormatMask* MaskAsset = MaskAssetRef.LoadSynchronous())
			{
				Texture = MaskAsset->MaskTexture.Get();
			}
		}
		return Texture ? GetTextureRHI(Texture) : FTextureRHIRef();
	};
	EffectData.StainSourceMask = ResolveStainMask(
		LayerEffect.StainSourceMask, LayerEffect.StainSourceMaskTexture);
	EffectData.StainDirtMask = ResolveStainMask(
		LayerEffect.StainDirtMask, LayerEffect.StainDirtMaskTexture);

	// Stain resolves into the layer's mask chain, so it makes the layer masked in
	// exactly the way an authored, generated, craquelure or colour-ID child does.
	// Without this the composite took HasMask 0, ignored the chain the stain had
	// just written, and the layer covered fully -- which is why a stain only
	// appeared to work once some other mask child was added in front of it.
	bHasMask = true;
}

void GatherRunoff(
	FEffectRenderData& EffectData, const FMixtormatLayerEffect& LayerEffect, bool& bHasMask)
{
	// Degrees to radians here rather than in the shader: the angle is constant across
	// every texel and every stratum, so converting it per-pixel would be the one bit
	// of arithmetic in the whole effect that is pure waste.
	EffectData.RunoffGravityAngle = FMath::DegreesToRadians(
		EffectFloat(TEXT("RunoffGravityAngle"), LayerEffect.RunoffGravityAngle));

	// Texels to UV, and this is what makes the effect resolution-independent.
	//
	// The control is authored in texels because that is how an artist reads a streak
	// length, but texels are the wrong unit to store it in: a 320-texel run is a third
	// of the way down a 1K texture and a twelfth of the way down a 4K one, so the same
	// material would grow a different streak at every output size -- and cost sixteen
	// times as much to grow the shorter one, because the tap count scales with texels
	// too.
	//
	// Dividing by 1024 pins the number to what it means at 1K and leaves it a
	// fraction of the texture from then on. The shader works entirely in UV, derives
	// its tap count from the UV reach rather than from a texel count, and so produces
	// the same run at every resolution for the same cost. The reference resolution is
	// not exposed: moving it would rescale every runoff in every material at once,
	// which is what Streak Radius is already for.
	constexpr float RunoffReferenceResolution = 1024.0f;
	EffectData.RunoffStreakRadius =
		EffectFloat(TEXT("RunoffStreakRadius"), LayerEffect.RunoffStreakRadius)
		/ RunoffReferenceResolution;

	EffectData.RunoffStreakSoftness =
		EffectFloat(TEXT("RunoffStreakSoftness"), LayerEffect.RunoffStreakSoftness);
	EffectData.RunoffSurfaceInfluence =
		EffectFloat(TEXT("RunoffSurfaceInfluence"), LayerEffect.RunoffSurfaceInfluence);
	EffectData.RunoffStrataAmount =
		EffectFloat(TEXT("RunoffStrataAmount"), LayerEffect.RunoffStrataAmount);
	EffectData.RunoffWarpScale =
		EffectFloat(TEXT("RunoffWarpScale"), LayerEffect.RunoffWarpScale);
	EffectData.RunoffWarpAmount =
		EffectFloat(TEXT("RunoffWarpAmount"), LayerEffect.RunoffWarpAmount);
	EffectData.RunoffLipStrength =
		EffectFloat(TEXT("RunoffLipStrength"), LayerEffect.RunoffLipStrength);
	EffectData.RunoffStrength =
		EffectFloat(TEXT("RunoffStrength"), LayerEffect.RunoffStrength);
	EffectData.RunoffSeed = static_cast<uint32>(
		EffectInt(TEXT("RunoffSeed"), LayerEffect.RunoffSeed));

	// Strata count follows the reach rather than being a control of its own. A short
	// run has no room to show five layered deposits -- they would land on top of each
	// other and read as one thicker run -- so the number of them is a function of how
	// much length there is to spread them over. Derived on the CPU because it decides
	// a loop bound the shader has to unroll against.
	//
	// The thresholds are in authored texels, not in the converted UV reach: what an
	// artist means by a long streak is the number they typed, and it should pick the
	// same stratification at every composition size.
	const float AuthoredRadius =
		EffectFloat(TEXT("RunoffStreakRadius"), LayerEffect.RunoffStreakRadius);
	int32 StrataCount = 2;
	StrataCount += AuthoredRadius >= 160.0f ? 1 : 0;
	StrataCount += AuthoredRadius >= 320.0f ? 1 : 0;
	StrataCount += AuthoredRadius >= 480.0f ? 1 : 0;
	EffectData.RunoffStrataCount = FMath::Clamp(StrataCount, 2, 5);

	// Same reason Stain sets it: Runoff resolves into the layer's mask chain, so the
	// layer is masked by it. Without this the composite takes HasMask 0 and covers
	// fully, ignoring the chain the runoff just wrote.
	bHasMask = true;
}

}
