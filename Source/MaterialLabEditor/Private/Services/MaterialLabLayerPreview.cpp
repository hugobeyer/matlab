#include "Services/MaterialLabLayerPreview.h"

#include "Engine/Texture2D.h"
#include "MaterialLabMaterial.h"
#include "MaterialLabSurface.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionClamp.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionNormalize.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace MaterialLabLayerPreview
{
	constexpr int32 LayerCount = 4;

	FName ParameterName(const int32 LayerIndex, const TCHAR* Suffix)
	{
		return FName(*FString::Printf(TEXT("ML_L%d_%s"), LayerIndex, Suffix));
	}

	template <typename ExpressionType>
	ExpressionType* AddExpression(UMaterial& Material)
	{
		ExpressionType* Expression = NewObject<ExpressionType>(&Material);
		Material.GetExpressionCollection().AddExpression(Expression);
		return Expression;
	}

	UMaterialExpressionConstant* Constant(UMaterial& Material, const float Value)
	{
		UMaterialExpressionConstant* Expression = AddExpression<UMaterialExpressionConstant>(Material);
		Expression->R = Value;
		return Expression;
	}

	UMaterialExpressionConstant3Vector* Constant3(
		UMaterial& Material,
		const FLinearColor& Value)
	{
		UMaterialExpressionConstant3Vector* Expression =
			AddExpression<UMaterialExpressionConstant3Vector>(Material);
		Expression->Constant = Value;
		return Expression;
	}

	UMaterialExpressionScalarParameter* ScalarParameter(
		UMaterial& Material,
		const FName Name,
		const float DefaultValue)
	{
		UMaterialExpressionScalarParameter* Expression =
			AddExpression<UMaterialExpressionScalarParameter>(Material);
		Expression->SetParameterName(Name);
		Expression->ExpressionGUID = FGuid::NewGuid();
		Expression->DefaultValue = DefaultValue;
		return Expression;
	}

	UMaterialExpressionVectorParameter* VectorParameter(
		UMaterial& Material,
		const FName Name,
		const FLinearColor& DefaultValue)
	{
		UMaterialExpressionVectorParameter* Expression =
			AddExpression<UMaterialExpressionVectorParameter>(Material);
		Expression->SetParameterName(Name);
		Expression->ExpressionGUID = FGuid::NewGuid();
		Expression->DefaultValue = DefaultValue;
		return Expression;
	}

	UMaterialExpressionMultiply* Multiply(
		UMaterial& Material,
		UMaterialExpression* A,
		UMaterialExpression* B)
	{
		UMaterialExpressionMultiply* Expression = AddExpression<UMaterialExpressionMultiply>(Material);
		Expression->A.Expression = A;
		Expression->B.Expression = B;
		return Expression;
	}

	UMaterialExpressionAdd* Add(
		UMaterial& Material,
		UMaterialExpression* A,
		UMaterialExpression* B)
	{
		UMaterialExpressionAdd* Expression = AddExpression<UMaterialExpressionAdd>(Material);
		Expression->A.Expression = A;
		Expression->B.Expression = B;
		return Expression;
	}

	UMaterialExpressionDivide* Divide(
		UMaterial& Material,
		UMaterialExpression* A,
		UMaterialExpression* B)
	{
		UMaterialExpressionDivide* Expression = AddExpression<UMaterialExpressionDivide>(Material);
		Expression->A.Expression = A;
		Expression->B.Expression = B;
		return Expression;
	}

	UMaterialExpressionClamp* Clamp(
		UMaterial& Material,
		UMaterialExpression* Input,
		const float Min,
		const float Max)
	{
		UMaterialExpressionClamp* Expression = AddExpression<UMaterialExpressionClamp>(Material);
		Expression->Input.Expression = Input;
		Expression->MinDefault = Min;
		Expression->MaxDefault = Max;
		return Expression;
	}

	UMaterialExpressionSubtract* Subtract(
		UMaterial& Material,
		UMaterialExpression* A,
		UMaterialExpression* B)
	{
		UMaterialExpressionSubtract* Expression = AddExpression<UMaterialExpressionSubtract>(Material);
		Expression->A.Expression = A;
		Expression->B.Expression = B;
		return Expression;
	}

	UMaterialExpressionLinearInterpolate* Lerp(
		UMaterial& Material,
		UMaterialExpression* A,
		UMaterialExpression* B,
		UMaterialExpression* Alpha)
	{
		UMaterialExpressionLinearInterpolate* Expression =
			AddExpression<UMaterialExpressionLinearInterpolate>(Material);
		Expression->A.Expression = A;
		Expression->B.Expression = B;
		Expression->Alpha.Expression = Alpha;
		return Expression;
	}

	UMaterialExpressionSaturate* Saturate(UMaterial& Material, UMaterialExpression* Input)
	{
		UMaterialExpressionSaturate* Expression = AddExpression<UMaterialExpressionSaturate>(Material);
		Expression->Input.Expression = Input;
		return Expression;
	}

	UMaterialExpressionOneMinus* OneMinus(UMaterial& Material, UMaterialExpression* Input)
	{
		UMaterialExpressionOneMinus* Expression = AddExpression<UMaterialExpressionOneMinus>(Material);
		Expression->Input.Expression = Input;
		return Expression;
	}

	UMaterialExpressionComponentMask* Channel(
		UMaterial& Material,
		UMaterialExpression* Input,
		const int32 ChannelIndex)
	{
		UMaterialExpressionComponentMask* Expression =
			AddExpression<UMaterialExpressionComponentMask>(Material);
		Expression->Input.Expression = Input;
		Expression->R = ChannelIndex == 0;
		Expression->G = ChannelIndex == 1;
		Expression->B = ChannelIndex == 2;
		Expression->A = ChannelIndex == 3;
		return Expression;
	}

	UMaterialExpressionTextureSampleParameter2D* TextureParameter(
		UMaterial& Material,
		const FName Name,
		UTexture2D* DefaultTexture,
		const EMaterialSamplerType SamplerType,
		UMaterialExpression* Coordinates)
	{
		UMaterialExpressionTextureSampleParameter2D* Expression =
			AddExpression<UMaterialExpressionTextureSampleParameter2D>(Material);
		Expression->SetParameterName(Name);
		Expression->ExpressionGUID = FGuid::NewGuid();
		Expression->Texture = DefaultTexture;
		Expression->SamplerType = SamplerType;
		Expression->Coordinates.Expression = Coordinates;
		return Expression;
	}

	struct FLayerExpressions
	{
		UMaterialExpression* BaseColor = nullptr;
		UMaterialExpression* Roughness = nullptr;
		UMaterialExpression* Metallic = nullptr;
		UMaterialExpression* Specular = nullptr;
		UMaterialExpression* AmbientOcclusion = nullptr;
		UMaterialExpression* Normal = nullptr;
		UMaterialExpression* Alpha = nullptr;
		UMaterialExpression* Coat = nullptr;
		UMaterialExpression* CoatWeight = nullptr;
	};

	FLayerExpressions BuildLayer(
		UMaterial& Material,
		const int32 LayerIndex,
		UTexture2D* WhiteTexture,
		UTexture2D* NormalTexture)
	{
		UMaterialExpressionTextureCoordinate* TextureCoordinates =
			AddExpression<UMaterialExpressionTextureCoordinate>(Material);
		UMaterialExpression* SurfaceCoordinates = Multiply(
			Material,
			TextureCoordinates,
			ScalarParameter(Material, ParameterName(LayerIndex, TEXT("Tiling")), 1.0f));

		UMaterialExpression* BaseColorTexture = TextureParameter(
			Material,
			ParameterName(LayerIndex, TEXT("BaseColor")),
			WhiteTexture,
			SAMPLERTYPE_Color,
			SurfaceCoordinates);
		UMaterialExpression* NormalSample = TextureParameter(
			Material,
			ParameterName(LayerIndex, TEXT("Normal")),
			NormalTexture,
			SAMPLERTYPE_Normal,
			SurfaceCoordinates);
		UMaterialExpression* RamSample = TextureParameter(
			Material,
			ParameterName(LayerIndex, TEXT("RAM")),
			WhiteTexture,
			SAMPLERTYPE_LinearColor,
			SurfaceCoordinates);

		UMaterialExpression* RawRoughness = Channel(Material, RamSample, 0);
		UMaterialExpression* SafeBias = Clamp(
			Material,
			ScalarParameter(Material, ParameterName(LayerIndex, TEXT("RoughnessBias")), 0.5f),
			0.01f,
			0.99f);
		UMaterialExpression* BiasDenominator = Add(
			Material,
			Multiply(
				Material,
				Subtract(Material, Divide(Material, Constant(Material, 1.0f), SafeBias), Constant(Material, 2.0f)),
				OneMinus(Material, RawRoughness)),
			Constant(Material, 1.0f));
		UMaterialExpression* RoughnessBiased = Saturate(
			Material,
			Divide(Material, RawRoughness, BiasDenominator));
		UMaterialExpression* RoughnessContrasted = Lerp(
			Material,
			Constant(Material, 0.5f),
			RoughnessBiased,
			ScalarParameter(Material, ParameterName(LayerIndex, TEXT("RoughnessContrast")), 1.0f));
		UMaterialExpression* AdjustedRoughness = Saturate(
			Material,
			Add(
				Material,
				RoughnessContrasted,
				ScalarParameter(Material, ParameterName(LayerIndex, TEXT("RoughnessOffset")), 0.0f)));

		UMaterialExpression* OverrideBaseColor = ScalarParameter(
			Material,
			ParameterName(LayerIndex, TEXT("OverrideBaseColor")),
			0.0f);
		UMaterialExpression* OverrideRoughness = ScalarParameter(
			Material,
			ParameterName(LayerIndex, TEXT("OverrideRoughness")),
			0.0f);
		UMaterialExpression* OverrideMetallic = ScalarParameter(
			Material,
			ParameterName(LayerIndex, TEXT("OverrideMetallic")),
			0.0f);
		UMaterialExpression* OverrideSpecular = ScalarParameter(
			Material,
			ParameterName(LayerIndex, TEXT("OverrideIOR")),
			0.0f);

		FLayerExpressions Result;
		Result.BaseColor = Lerp(
			Material,
			BaseColorTexture,
			VectorParameter(Material, ParameterName(LayerIndex, TEXT("FillColor")), FLinearColor::White),
			OverrideBaseColor);
		Result.Roughness = Lerp(
			Material,
			AdjustedRoughness,
			ScalarParameter(Material, ParameterName(LayerIndex, TEXT("FillRoughness")), 0.5f),
			OverrideRoughness);
		Result.Metallic = Lerp(
			Material,
			Channel(Material, RamSample, 2),
			ScalarParameter(Material, ParameterName(LayerIndex, TEXT("FillMetallic")), 0.0f),
			OverrideMetallic);
		Result.Specular = Lerp(
			Material,
			Constant(Material, 0.5f),
			ScalarParameter(Material, ParameterName(LayerIndex, TEXT("FillSpecular")), 0.5f),
			OverrideSpecular);
		Result.AmbientOcclusion = Lerp(
			Material,
			Channel(Material, RamSample, 1),
			Constant(Material, 1.0f),
			OverrideBaseColor);

		UMaterialExpression* NormalStrength = ScalarParameter(
			Material,
			ParameterName(LayerIndex, TEXT("NormalIntensity")),
			1.0f);
		UMaterialExpression* BlendedNormal = Lerp(
			Material,
			Constant3(Material, FLinearColor(0.0f, 0.0f, 1.0f)),
			NormalSample,
			NormalStrength);
		UMaterialExpressionNormalize* NormalizedNormal = AddExpression<UMaterialExpressionNormalize>(Material);
		NormalizedNormal->VectorInput.Expression = BlendedNormal;
		Result.Normal = NormalizedNormal;

		UMaterialExpressionTextureCoordinate* MaskCoordinates =
			AddExpression<UMaterialExpressionTextureCoordinate>(Material);
		UMaterialExpression* ScaledMaskCoordinates = Multiply(
			Material,
			MaskCoordinates,
			ScalarParameter(Material, ParameterName(LayerIndex, TEXT("MaskTiling")), 1.0f));
		UMaterialExpression* MaskSample = Channel(
			Material,
			TextureParameter(
				Material,
				ParameterName(LayerIndex, TEXT("Mask")),
				WhiteTexture,
				SAMPLERTYPE_LinearColor,
				ScaledMaskCoordinates),
			0);
		UMaterialExpression* BalancedMask = Subtract(
			Material,
			MaskSample,
			ScalarParameter(Material, ParameterName(LayerIndex, TEXT("MaskBalance")), 0.5f));
		UMaterialExpression* ContrastedMask = Saturate(
			Material,
			Add(
				Material,
				Multiply(
					Material,
					BalancedMask,
					ScalarParameter(Material, ParameterName(LayerIndex, TEXT("MaskContrast")), 1.0f)),
				Constant(Material, 0.5f)));
		UMaterialExpression* InvertedMask = OneMinus(Material, ContrastedMask);
		UMaterialExpression* FinalMask = Lerp(
			Material,
			ContrastedMask,
			InvertedMask,
			ScalarParameter(Material, ParameterName(LayerIndex, TEXT("MaskInvert")), 0.0f));
		UMaterialExpression* MaskWeight = Lerp(
			Material,
			Constant(Material, 1.0f),
			FinalMask,
			ScalarParameter(Material, ParameterName(LayerIndex, TEXT("HasMask")), 0.0f));
		Result.Alpha = Saturate(
			Material,
			Multiply(
				Material,
				Multiply(
					Material,
					ScalarParameter(Material, ParameterName(LayerIndex, TEXT("Enabled")), LayerIndex == 0 ? 1.0f : 0.0f),
					ScalarParameter(Material, ParameterName(LayerIndex, TEXT("Opacity")), 1.0f)),
				MaskWeight));
		Result.Coat = ScalarParameter(
			Material,
			ParameterName(LayerIndex, TEXT("Coat")),
			0.0f);
		Result.CoatWeight = Multiply(Material, Result.Alpha, Result.Coat);
		return Result;
	}

	UTexture2D* LoadTexture(const TCHAR* ObjectPath)
	{
		return LoadObject<UTexture2D>(nullptr, ObjectPath);
	}

	void SetScalar(
		UMaterialInstanceDynamic& MaterialInstance,
		const int32 LayerIndex,
		const TCHAR* Suffix,
		const float Value)
	{
		MaterialInstance.SetScalarParameterValue(ParameterName(LayerIndex, Suffix), Value);
	}

	void SetTexture(
		UMaterialInstanceDynamic& MaterialInstance,
		const int32 LayerIndex,
		const TCHAR* Suffix,
		UTexture* Texture)
	{
		if (Texture)
		{
			MaterialInstance.SetTextureParameterValue(ParameterName(LayerIndex, Suffix), Texture);
		}
	}
}

UMaterial* FMaterialLabLayerPreview::CreateMaterial()
{
	using namespace MaterialLabLayerPreview;
	UMaterial* Material = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
	if (!Material)
	{
		return nullptr;
	}

	Material->MaterialDomain = MD_Surface;
	Material->BlendMode = BLEND_Opaque;
	Material->SetShadingModel(MSM_ClearCoat);

	UTexture2D* WhiteTexture = LoadTexture(
		TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
	UTexture2D* NormalTexture = LoadTexture(
		TEXT("/Engine/EngineMaterials/DefaultNormal.DefaultNormal"));

	TArray<FLayerExpressions> Layers;
	for (int32 LayerIndex = 0; LayerIndex < LayerCount; ++LayerIndex)
	{
		Layers.Add(BuildLayer(*Material, LayerIndex, WhiteTexture, NormalTexture));
	}

	UMaterialExpression* BaseColor = Layers[0].BaseColor;
	UMaterialExpression* Roughness = Layers[0].Roughness;
	UMaterialExpression* Metallic = Layers[0].Metallic;
	UMaterialExpression* Specular = Layers[0].Specular;
	UMaterialExpression* AmbientOcclusion = Layers[0].AmbientOcclusion;
	UMaterialExpression* Normal = Layers[0].Normal;
	UMaterialExpression* CoatAmount = Constant(*Material, 0.0f);
	UMaterialExpression* CoatRoughness = Constant(*Material, 0.1f);

	for (int32 LayerIndex = 1; LayerIndex < Layers.Num(); ++LayerIndex)
	{
		const FLayerExpressions& Layer = Layers[LayerIndex];
		UMaterialExpression* ReplaceWeight = Multiply(
			*Material,
			Layer.Alpha,
			OneMinus(*Material, Layer.Coat));
		UMaterialExpression* SurfaceWeight = Saturate(
			*Material,
			Add(
				*Material,
				ReplaceWeight,
				Multiply(*Material, Layer.CoatWeight, Constant(*Material, 0.35f))));

		BaseColor = Lerp(*Material, BaseColor, Layer.BaseColor, SurfaceWeight);
		Roughness = Lerp(*Material, Roughness, Layer.Roughness, Layer.Alpha);
		Metallic = Lerp(*Material, Metallic, Layer.Metallic, ReplaceWeight);
		Specular = Lerp(*Material, Specular, Layer.Specular, Layer.Alpha);
		AmbientOcclusion = Lerp(*Material, AmbientOcclusion, Layer.AmbientOcclusion, ReplaceWeight);

		UMaterialExpression* BlendedNormal = Lerp(*Material, Normal, Layer.Normal, Layer.Alpha);
		UMaterialExpressionNormalize* NormalizedNormal = AddExpression<UMaterialExpressionNormalize>(*Material);
		NormalizedNormal->VectorInput.Expression = BlendedNormal;
		Normal = NormalizedNormal;

		CoatAmount = Saturate(*Material, Add(*Material, CoatAmount, Layer.CoatWeight));
		CoatRoughness = Lerp(*Material, CoatRoughness, Layer.Roughness, Layer.CoatWeight);
	}

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	EditorData->BaseColor.Expression = BaseColor;
	EditorData->Roughness.Expression = Roughness;
	EditorData->Metallic.Expression = Metallic;
	EditorData->Specular.Expression = Specular;
	EditorData->AmbientOcclusion.Expression = AmbientOcclusion;
	EditorData->Normal.Expression = Normal;
	EditorData->ClearCoat.Expression = CoatAmount;
	EditorData->ClearCoatRoughness.Expression = CoatRoughness;

	Material->PostEditChange();
	return Material;
}

void FMaterialLabLayerPreview::ApplyLayers(
	UMaterialInstanceDynamic& MaterialInstance,
	const TArray<FMaterialLabLayer>& Layers)
{
	using namespace MaterialLabLayerPreview;
	UTexture2D* WhiteTexture = LoadTexture(
		TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
	UTexture2D* NormalTexture = LoadTexture(
		TEXT("/Engine/EngineMaterials/DefaultNormal.DefaultNormal"));

	for (int32 LayerIndex = 0; LayerIndex < LayerCount; ++LayerIndex)
	{
		const FMaterialLabLayer* Layer = Layers.IsValidIndex(LayerIndex)
			? &Layers[LayerIndex]
			: nullptr;
		const bool bEnabled = Layer && Layer->bEnabled;
		const UMaterialLabSurface* Surface = Layer
			? Layer->SourceSurface.LoadSynchronous()
			: nullptr;

		SetScalar(MaterialInstance, LayerIndex, TEXT("Enabled"), bEnabled ? 1.0f : 0.0f);
		SetScalar(MaterialInstance, LayerIndex, TEXT("Opacity"), Layer ? Layer->Opacity : 0.0f);
		SetScalar(MaterialInstance, LayerIndex, TEXT("Coat"), Layer && Layer->CompositionMode == EMaterialLabCompositionMode::Coat ? 1.0f : 0.0f);
		SetScalar(
			MaterialInstance,
			LayerIndex,
			TEXT("Tiling"),
			Layer ? FMath::Max(1.0f, FMath::RoundToFloat(Layer->Tiling)) : 1.0f);
		SetScalar(MaterialInstance, LayerIndex, TEXT("RoughnessBias"), Layer ? Layer->RoughnessBias : 0.5f);
		SetScalar(MaterialInstance, LayerIndex, TEXT("RoughnessContrast"), Layer ? Layer->RoughnessContrast : 1.0f);
		SetScalar(MaterialInstance, LayerIndex, TEXT("RoughnessOffset"), Layer ? Layer->RoughnessOffset : 0.0f);
		SetScalar(MaterialInstance, LayerIndex, TEXT("NormalIntensity"), Layer && Layer->Type != EMaterialLabLayerType::Fill ? Layer->NormalIntensity : 0.0f);

		SetScalar(MaterialInstance, LayerIndex, TEXT("OverrideBaseColor"), Layer && Layer->bOverrideBaseColor ? 1.0f : 0.0f);
		SetScalar(MaterialInstance, LayerIndex, TEXT("OverrideRoughness"), Layer && Layer->bOverrideRoughness ? 1.0f : 0.0f);
		SetScalar(MaterialInstance, LayerIndex, TEXT("OverrideMetallic"), Layer && Layer->bOverrideMetallic ? 1.0f : 0.0f);
		SetScalar(MaterialInstance, LayerIndex, TEXT("OverrideIOR"), Layer && Layer->bOverrideIOR ? 1.0f : 0.0f);
		MaterialInstance.SetVectorParameterValue(
			ParameterName(LayerIndex, TEXT("FillColor")),
			Layer ? Layer->BaseColor : FLinearColor::White);
		SetScalar(MaterialInstance, LayerIndex, TEXT("FillRoughness"), Layer ? Layer->Roughness : 0.5f);
		SetScalar(MaterialInstance, LayerIndex, TEXT("FillMetallic"), Layer ? Layer->Metallic : 0.0f);
		const float IOR = Layer ? FMath::Max(1.0f, Layer->IOR) : 1.5f;
		const float F0 = FMath::Square((IOR - 1.0f) / (IOR + 1.0f));
		SetScalar(MaterialInstance, LayerIndex, TEXT("FillSpecular"), FMath::Clamp(F0 / 0.08f, 0.0f, 1.0f));

		UTexture2D* MaskTexture = Layer ? Layer->MaskTexture.LoadSynchronous() : nullptr;
		SetScalar(MaterialInstance, LayerIndex, TEXT("HasMask"), MaskTexture ? 1.0f : 0.0f);
		SetScalar(
			MaterialInstance,
			LayerIndex,
			TEXT("MaskTiling"),
			Layer ? FMath::Max(1.0f, FMath::RoundToFloat(Layer->MaskTiling)) : 1.0f);
		SetScalar(MaterialInstance, LayerIndex, TEXT("MaskBalance"), Layer ? Layer->MaskBalance : 0.5f);
		SetScalar(MaterialInstance, LayerIndex, TEXT("MaskContrast"), Layer ? Layer->MaskContrast : 1.0f);
		SetScalar(MaterialInstance, LayerIndex, TEXT("MaskInvert"), Layer && Layer->bInvertMask ? 1.0f : 0.0f);

		SetTexture(MaterialInstance, LayerIndex, TEXT("BaseColor"), Surface && Surface->BaseColor ? Surface->BaseColor.Get() : WhiteTexture);
		SetTexture(MaterialInstance, LayerIndex, TEXT("Normal"), Surface && Surface->Normal ? Surface->Normal.Get() : NormalTexture);
		SetTexture(MaterialInstance, LayerIndex, TEXT("RAM"), Surface && Surface->RoughnessAOMetallic ? Surface->RoughnessAOMetallic.Get() : WhiteTexture);
		SetTexture(MaterialInstance, LayerIndex, TEXT("Mask"), MaskTexture ? MaskTexture : WhiteTexture);
	}
}
