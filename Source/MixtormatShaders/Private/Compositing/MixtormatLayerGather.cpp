// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Compositing/MixtormatLayerGather.h"

#include "Compositing/MixtormatComposeHash.h"
#include "MixtormatParameterBinding.h"
#include "MixtormatParameterDefinition.h"
#include "MixtormatReliefScaling.h"
#include "MixtormatSurface.h"

namespace MixtormatGpuCompositor
{
UMixtormatMaterial* ResolveLayerReference(const FMixtormatLayer& Layer)
{
	return Layer.SourceComposition.LoadSynchronous();
}

bool IsInvalidLayerReference(const FMixtormatLayer& Layer,
	const TStrongObjectPtr<UMixtormatMaterial>& Source,
	const TSet<const UMixtormatMaterial*>& ActiveSources)
{
	return !Source.IsValid() || Layer.Type != EMixtormatLayerType::Material
		|| !Layer.SourceSurface.IsNull() || ActiveSources.Contains(Source.Get())
		|| ActiveSources.Num() >= 32;
}

void GatherLayerSourceCacheKey(FLayerRenderData& Data, const FMixtormatLayer& Layer)
{
	MixtormatComposeHash::FHasher SourceHasher;
	SourceHasher.SkipTopLevel.Add(TEXT("Children"));
	SourceHasher.Struct(FMixtormatLayer::StaticStruct(), &Layer);
	Data.SourceCacheKey = MixtormatComposeHash::Combine(SourceHasher.Get(), 0x536F75726365ull) | 1ull;
}

bool GatherLayerSource(FLayerRenderData& Data, const FMixtormatLayer& Layer,
	UTexture2D* WhiteTexture, UTexture2D* NormalTexture,
	const UMixtormatSurface*& Surface, UTexture2D*& LayerNormal)
{
	const bool bReference = !Layer.SourceComposition.IsNull();
	Surface = bReference ? nullptr : Layer.SourceSurface.LoadSynchronous();
	const bool bNormalOnly = Layer.ChannelMode == EMixtormatLayerChannelMode::NormalDetail;
	UTexture2D* LayerBaseColor = Surface && Surface->BaseColor ? Surface->BaseColor.Get() : WhiteTexture;
	LayerNormal = Surface && Surface->Normal ? Surface->Normal.Get() : NormalTexture;
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
	Data.LayerId = Layer.LayerId;
	Data.bGenerator = Layer.Type == EMixtormatLayerType::Generator;

	Data.bFill = Layer.Type == EMixtormatLayerType::Fill || Data.bGenerator;
	// Only a layer's combined mask is a usable signal this step. A child mask lives in the
	// rotating ping-pong pair and is gone by the composite; region IDs are not a scalar at
	// all. Both are refused here rather than approximated -- a Driver that cannot resolve
	// contributes nothing and the parameter keeps its authored value.
	{
		const FName DrivenScalars[2] = { TEXT("RoughnessInfluence"), TEXT("HeightBlendAmount") };
		for (int32 SlotIndex = 0; SlotIndex < 2; ++SlotIndex)
		{
			const FMixtormatParameterBinding* Binding = Layer.ParameterBindings.FindByPredicate(
				[&DrivenScalars, SlotIndex](const FMixtormatParameterBinding& Candidate)
				{
					return Candidate.DestinationOwner == EMixtormatParameterOwnerType::Layer
						&& Candidate.DestinationParameter == DrivenScalars[SlotIndex]
						&& Candidate.Driver.bEnabled
						&& (Candidate.Driver.SourceKind == EMixtormatDriverSourceKind::CombinedMask
							|| Candidate.Driver.SourceKind == EMixtormatDriverSourceKind::RegionIds)
						&& Candidate.Driver.SourceLayerId.IsValid();
				});
			if (!Binding)
			{
				continue;
			}
			FScalarDriverRenderData& Driver = Data.ScalarDrivers[SlotIndex];
			Driver.bEnabled = true;
			Driver.bRegionSource =
				Binding->Driver.SourceKind == EMixtormatDriverSourceKind::RegionIds;
			Driver.SourceLayerId = Binding->Driver.SourceLayerId;
			Driver.SourceChildIndex = Binding->Driver.SourceChildId.IsValid()
				? Layer.Children.IndexOfByPredicate(
					[Binding](const FMixtormatLayerChild& Candidate)
					{
						return Candidate.ChildId == Binding->Driver.SourceChildId;
					})
				: INDEX_NONE;
			Driver.Seed = static_cast<uint32>(Binding->Driver.Seed);
			Driver.IdRandomMin = Binding->Driver.IdRandomMin;
			Driver.IdRandomMax = Binding->Driver.IdRandomMax;
			Driver.bInvert = Binding->Driver.bInvert;
			Driver.InputMin = Binding->Driver.InputMin;
			Driver.InputMax = Binding->Driver.InputMax;
			Driver.OutputMin = Binding->Driver.OutputMin;
			Driver.OutputMax = Binding->Driver.OutputMax;
			Driver.Amount = Binding->Driver.Amount;
			Driver.Combine = static_cast<uint32>(Binding->Driver.Combine);
		}
	}
	Data.FillColor = FVector4f(
		Layer.BaseColor.R,
		Layer.BaseColor.G,
		Layer.BaseColor.B,
		Layer.BaseColor.A);
	return true;
}

uint64 GatherLayerPlacementKey(const FMixtormatLayer& Layer, const bool bGeneratorLayer)
{
	// The layer's UV transform moves every module's geometry, so it keys their cached fields.
	uint64 PlacementKey = 0;
	if (bGeneratorLayer)
	{
		MixtormatComposeHash::FHasher PlacementHasher;
		const float PlacementTiling = FMath::Max(1.0f, FMath::RoundToFloat(Layer.Tiling));
		const int32 PlacementScaleX = FMath::Max(Layer.UVScaleX, 1);
		const int32 PlacementScaleY = FMath::Max(Layer.UVScaleY, 1);
		const FVector2f PlacementOffset(Layer.UVOffsetX, Layer.UVOffsetY);
		const int32 PlacementRotation = static_cast<int32>(Layer.Rotation);
		PlacementHasher.Bytes(&PlacementTiling, sizeof(PlacementTiling));
		PlacementHasher.Bytes(&PlacementScaleX, sizeof(PlacementScaleX));
		PlacementHasher.Bytes(&PlacementScaleY, sizeof(PlacementScaleY));
		PlacementHasher.Bytes(&PlacementOffset, sizeof(PlacementOffset));
		PlacementHasher.Bytes(&Layer.bFlipU, sizeof(Layer.bFlipU));
		PlacementHasher.Bytes(&Layer.bFlipV, sizeof(Layer.bFlipV));
		PlacementHasher.Bytes(&PlacementRotation, sizeof(PlacementRotation));
		PlacementKey = PlacementHasher.Get();
	}
	return PlacementKey;
}

void GatherLayerFields(FLayerRenderData& Data, const FMixtormatLayer& Layer,
	const int32 LayerIndex, const UMixtormatSurface* Surface, UTexture2D* LayerNormal)
{
	const bool bNormalOnly = Layer.ChannelMode == EMixtormatLayerChannelMode::NormalDetail;
	Data.Opacity = Layer.Opacity;
	Data.Tiling = FMath::Max(1.0f, FMath::RoundToFloat(Layer.Tiling));

	// Integer, because the compositor wraps every source read in a frac() and a
	// fractional scale lands mid-cell at the wrap. Offset and flip are unclamped:
	// translating and mirroring a periodic function leave it periodic.
	Data.UVScaleX = FMath::Max(Layer.UVScaleX, 1);
	Data.UVScaleY = FMath::Max(Layer.UVScaleY, 1);
	Data.UVOffset = FVector2f(Layer.UVOffsetX, Layer.UVOffsetY);
	Data.Rotation = static_cast<int32>(Layer.Rotation);
	Data.bFlipU = Layer.bFlipU;
	Data.bFlipV = Layer.bFlipV;
	// Normal Strength is the authored normal map's own gain, and the only control that
	// steepens it. Height Booster is deliberately absent: it owns the height and the normals
	// derived from that height, and tying the two made a matching height+normal pair carry
	// the same bump twice. Pinning this to a neutral 1.0 removed that double count but left
	// no way to strengthen an authored map at all, since Normal Influence only attenuates.
	// See MixtormatReliefScaling.h.
	Data.NormalIntensity = MixtormatRelief::AuthoredNormalScale(
				MixtormatParameterContracts::SanitizeFloat(
					EMixtormatParameterOwnerType::Layer, TEXT("NormalIntensity"), Layer.NormalIntensity));
	Data.HueShift = Layer.HueShift;
	Data.Saturation = Layer.Saturation;
	Data.Value = Layer.Value;
	Data.RoughnessBias = Layer.RoughnessBias;
	Data.RoughnessContrast = Layer.RoughnessContrast;
	Data.RoughnessOffset = Layer.RoughnessOffset;
	Data.FillRoughness = Layer.Roughness;
	Data.FillMetallic = Layer.Metallic;
	const float SourceIOR = Surface ? Surface->DefaultIOR : 1.5f;
	const float AuthoredIOR = MixtormatParameterContracts::SanitizeFloat(
		EMixtormatParameterOwnerType::Layer, TEXT("IOR"), Layer.IOR);
	const float LayerIOR = FMath::Max(
		1.0f,
		(Layer.Type == EMixtormatLayerType::Fill || Layer.bOverrideIOR)
			? AuthoredIOR
			: SourceIOR);
	Data.LayerF0 = FMath::Square((LayerIOR - 1.0f) / (LayerIOR + 1.0f));
	Data.HeightBoost = MixtormatRelief::HeightScale(
				MixtormatParameterContracts::SanitizeFloat(
					EMixtormatParameterOwnerType::Layer, TEXT("HeightBoost"), Layer.HeightBoost));
	Data.HeightLevelOffset = Layer.HeightLevelOffset;
	Data.HeightShape = Layer.HeightShape;
	Data.HeightSmooth = Layer.HeightSmooth;
	Data.BaseColorBlendMode = Layer.BaseColorBlendMode;
	Data.BaseColorBlendAmount = Layer.BaseColorBlendAmount;
	Data.BaseColorInfluence = Layer.BaseColorInfluence;
	Data.RoughnessInfluence = Layer.RoughnessInfluence;
	Data.AOInfluence = Layer.AOInfluence;
	Data.MetallicInfluence = Layer.MetallicInfluence;
	Data.F0Influence = MixtormatParameterContracts::SanitizeFloat(
		EMixtormatParameterOwnerType::Layer, TEXT("F0Influence"), Layer.F0Influence);
	Data.NormalInfluence = MixtormatParameterContracts::SanitizeFloat(
		EMixtormatParameterOwnerType::Layer, TEXT("NormalInfluence"), Layer.NormalInfluence);
	Data.HeightInfluence = MixtormatParameterContracts::SanitizeFloat(
		EMixtormatParameterOwnerType::Layer, TEXT("HeightInfluence"), Layer.HeightInfluence);
	Data.HeightBlendAmount = Layer.HeightBlendAmount;
	Data.HeightBlend = Layer.HeightBlend;
	Data.HeightContrast = FMath::Max(Layer.HeightContrast, 0.01f);
	Data.ConstantHeight = Layer.ConstantHeight;
	Data.MaskHeightInfluence = Layer.MaskHeightInfluence;
	Data.HeightContactAOAmount = Layer.HeightContactAOAmount;
	Data.HeightContactAOWidth = FMath::Max(Layer.HeightContactAOWidth, 1.0e-4f);
	Data.HeightBorderLift = Layer.HeightBorderLift;
	Data.HeightBorderWidth = FMath::Max(Layer.HeightBorderWidth, 1.0e-4f);
	Data.HeightSmoothRadius = Layer.HeightSmoothRadius;
	Data.HeightSmoothAmount = Layer.HeightSmoothAmount;
	Data.HeightBorderSmoothing = FMath::Max(Layer.HeightBorderSmoothing, 1.0f);
	Data.FeatureInfluence = Layer.FeatureInfluence;
	Data.FeatureBias = Layer.FeatureBias;
	Data.HeightFeatureInfluence = Layer.HeightFeatureInfluence;
	Data.AOFeatureInfluence = Layer.AOFeatureInfluence;
	Data.CurvatureRadius = Layer.CurvatureRadius;
	Data.CurvatureSmoothing = FMath::Max(Layer.CurvatureSmoothing, 1);
	Data.CurvatureStrength = Layer.CurvatureStrength;
	Data.CurvaturePower = Layer.CurvaturePower;
	Data.bEnabled = Layer.bEnabled;
	Data.bHasPackedHeight = Data.SourceOutputs.IsValid() || (Surface && Surface->bHasBlendHeight);
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
	Data.bFill = Layer.Type == EMixtormatLayerType::Fill || Data.bGenerator;
	Data.bHasSurface = Data.SourceOutputs.IsValid() || (Surface
		&& Surface->BaseColor
		&& Surface->Normal
		&& Surface->RoughnessAOMetallic);
	Data.bHasNormal = Data.SourceOutputs.IsValid() || LayerNormal != nullptr;
	Data.bNormalOnly = bNormalOnly;
	Data.bOverrideNormal = Layer.NormalBlendMode == EMixtormatNormalBlendMode::Override;
	Data.bFlipNormalY = Layer.bFlipNormalY;
}
}
