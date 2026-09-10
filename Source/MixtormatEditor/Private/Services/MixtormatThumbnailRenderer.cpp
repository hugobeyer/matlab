#include "Services/MixtormatThumbnailRenderer.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "AdvancedPreviewScene.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetViewerSettings.h"
#include "CanvasTypes.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EditorFramework/AssetImportData.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "LegacyScreenPercentageDriver.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "Modules/ModuleManager.h"
#include "PixelFormat.h"
#include "Preview/MixtormatPreviewSceneSettings.h"
#include "RendererInterface.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "SceneView.h"
#include "Services/MixtormatPaths.h"
#include "Style/MixtormatPalette.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/SMixtormatPreviewViewport.h"

namespace
{
	bool ReadSourcePixels(UTexture2D& Texture, TArray<FColor>& OutPixels, FString& OutError)
	{
		const int32 Width = Texture.Source.GetSizeX();
		const int32 Height = Texture.Source.GetSizeY();
		if (Width <= 0 || Height <= 0)
		{
			OutError = FString::Printf(TEXT("Mask %s has no source pixels."), *Texture.GetPathName());
			return false;
		}
		const ETextureSourceFormat SourceFormat = Texture.Source.GetFormat();
		if (SourceFormat != TSF_G8 && SourceFormat != TSF_BGRA8)
		{
			OutError = FString::Printf(
				TEXT("Mask %s must use G8 or BGRA8 source pixels; received format %d."),
				*Texture.GetPathName(),
				static_cast<int32>(SourceFormat));
			return false;
		}

		const uint8* SourceData = Texture.Source.LockMip(0);
		if (!SourceData)
		{
			OutError = FString::Printf(TEXT("Failed to read mask source pixels for %s."), *Texture.GetPathName());
			return false;
		}
		OutPixels.SetNumUninitialized(Width * Height);
		if (SourceFormat == TSF_G8)
		{
			for (int32 PixelIndex = 0; PixelIndex < OutPixels.Num(); ++PixelIndex)
			{
				const uint8 Value = SourceData[PixelIndex];
				OutPixels[PixelIndex] = FColor(Value, Value, Value, 255);
			}
		}
		else
		{
			FMemory::Memcpy(OutPixels.GetData(), SourceData, OutPixels.Num() * sizeof(FColor));
		}
		Texture.Source.UnlockMip(0);
		return true;
	}

	TArray<FColor> ResizeMaskToThumbnail(
		const TArray<FColor>& SourcePixels,
		const int32 SourceWidth,
		const int32 SourceHeight)
	{
		const int32 Resolution = MixtormatPreviewSceneSettings::ThumbnailResolution;
		TArray<FColor> Output;
		Output.SetNumUninitialized(Resolution * Resolution);

		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			const float SourceY = FMath::Clamp(
				((static_cast<float>(Y) + 0.5f) * SourceHeight / Resolution) - 0.5f,
				0.0f,
				static_cast<float>(SourceHeight - 1));
			const int32 Y0 = FMath::FloorToInt(SourceY);
			const int32 Y1 = FMath::Min(Y0 + 1, SourceHeight - 1);
			const float YAlpha = SourceY - FMath::FloorToFloat(SourceY);

			for (int32 X = 0; X < Resolution; ++X)
			{
				const float SourceX = FMath::Clamp(
					((static_cast<float>(X) + 0.5f) * SourceWidth / Resolution) - 0.5f,
					0.0f,
					static_cast<float>(SourceWidth - 1));
				const int32 X0 = FMath::FloorToInt(SourceX);
				const int32 X1 = FMath::Min(X0 + 1, SourceWidth - 1);
				const float XAlpha = SourceX - FMath::FloorToFloat(SourceX);

				const float Top = FMath::Lerp(
					static_cast<float>(SourcePixels[Y0 * SourceWidth + X0].R),
					static_cast<float>(SourcePixels[Y0 * SourceWidth + X1].R),
					XAlpha);
				const float Bottom = FMath::Lerp(
					static_cast<float>(SourcePixels[Y1 * SourceWidth + X0].R),
					static_cast<float>(SourcePixels[Y1 * SourceWidth + X1].R),
					XAlpha);
				const uint8 Value = static_cast<uint8>(FMath::RoundToInt(FMath::Lerp(Top, Bottom, YAlpha)));
				Output[Y * Resolution + X] = FColor(Value, Value, Value, 255);
			}
		}
		return Output;
	}

	bool SourcePixelsMatch(UTexture2D& Texture, const TArray<FColor>& Pixels)
	{
		const int32 Resolution = MixtormatPreviewSceneSettings::ThumbnailResolution;
		if (Texture.Source.GetSizeX() != Resolution
			|| Texture.Source.GetSizeY() != Resolution
			|| Texture.Source.GetFormat() != TSF_BGRA8
			|| Pixels.Num() != Resolution * Resolution)
		{
			return false;
		}

		const uint8* ExistingData = Texture.Source.LockMip(0);
		if (!ExistingData)
		{
			return false;
		}
		const bool bMatches = FMemory::Memcmp(
			ExistingData,
			Pixels.GetData(),
			Pixels.Num() * sizeof(FColor)) == 0;
		Texture.Source.UnlockMip(0);
		return bMatches;
	}

	FMixtormatThumbnailUpdate CreateOrUpdateThumbnailTexture(
		const FString& DestinationPath,
		const FString& AssetName,
		const TArray<FColor>& Pixels)
	{
		FMixtormatThumbnailUpdate Result;
		const int32 Resolution = MixtormatPreviewSceneSettings::ThumbnailResolution;
		if (Pixels.Num() != Resolution * Resolution)
		{
			Result.Error = FString::Printf(
				TEXT("Thumbnail %s has %d pixels; expected %d."),
				*AssetName,
				Pixels.Num(),
				Resolution * Resolution);
			return Result;
		}

		const FString PackageName = FString::Printf(TEXT("%s/%s"), *DestinationPath, *AssetName);
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, *AssetName);
		UTexture2D* Thumbnail = LoadObject<UTexture2D>(nullptr, *ObjectPath);
		const bool bCreated = Thumbnail == nullptr;
		if (bCreated)
		{
			if (UObject* ExistingObject = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath))
			{
				Result.Error = FString::Printf(
					TEXT("Cannot create thumbnail because an incompatible asset exists at %s."),
					*ExistingObject->GetPathName());
				return Result;
			}
			UPackage* Package = CreatePackage(*PackageName);
			Thumbnail = NewObject<UTexture2D>(
				Package,
				*AssetName,
				RF_Public | RF_Standalone | RF_Transactional);
		}
		if (!Thumbnail)
		{
			Result.Error = FString::Printf(TEXT("Failed to create thumbnail %s."), *ObjectPath);
			return Result;
		}
		if (!bCreated && Thumbnail->AssetImportData
			&& !Thumbnail->AssetImportData->GetSourceData().SourceFiles.IsEmpty())
		{
			Result.Error = FString::Printf(
				TEXT("Refused to overwrite imported texture at thumbnail path %s."),
				*ObjectPath);
			return Result;
		}

		const bool bPixelsChanged = !SourcePixelsMatch(*Thumbnail, Pixels);
		const bool bSettingsChanged = !Thumbnail->SRGB
			|| Thumbnail->CompressionSettings != TC_EditorIcon
			|| Thumbnail->LODGroup != TEXTUREGROUP_UI
			|| Thumbnail->MipGenSettings != TMGS_FromTextureGroup
			|| !Thumbnail->NeverStream
			|| Thumbnail->Filter != TF_Bilinear
			|| Thumbnail->AddressX != TA_Clamp
			|| Thumbnail->AddressY != TA_Clamp;
		Result.Texture = Thumbnail;
		Result.bChanged = bCreated || bPixelsChanged || bSettingsChanged;
		if (!Result.bChanged)
		{
			return Result;
		}

		Thumbnail->Modify();
		Thumbnail->PreEditChange(nullptr);
		if (bCreated || bPixelsChanged)
		{
			Thumbnail->Source.Init(
				Resolution,
				Resolution,
				1,
				1,
				TSF_BGRA8,
				reinterpret_cast<const uint8*>(Pixels.GetData()));
		}
		Thumbnail->SRGB = true;
		Thumbnail->CompressionSettings = TC_EditorIcon;
		Thumbnail->CompressionNoAlpha = true;
		Thumbnail->LODGroup = TEXTUREGROUP_UI;
		Thumbnail->MipGenSettings = TMGS_FromTextureGroup;
		Thumbnail->NeverStream = true;
		Thumbnail->Filter = TF_Bilinear;
		Thumbnail->AddressX = TA_Clamp;
		Thumbnail->AddressY = TA_Clamp;
		Thumbnail->PostEditChange();
		Thumbnail->MarkPackageDirty();
		if (bCreated)
		{
			FAssetRegistryModule::AssetCreated(Thumbnail);
		}
		return Result;
	}
}

class FMixtormatSurfaceThumbnailScene final
{
public:
	FMixtormatSurfaceThumbnailScene()
		: PreviewScene(FPreviewScene::ConstructionValues())
	{
		Profile = PreviewScene.GetCurrentProfile()
			? MakeUnique<FPreviewSceneProfile>(*PreviewScene.GetCurrentProfile())
			: MakeUnique<FPreviewSceneProfile>();
		MixtormatPreviewSceneSettings::ConfigureLookdevProfile(*Profile);

		MeshComponent = NewObject<UStaticMeshComponent>();
		MeshComponent->SetMobility(EComponentMobility::Movable);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		UStaticMesh* Sphere = LoadObject<UStaticMesh>(
			nullptr,
			*FMixtormatPaths::SphereMeshObjectPath());
		if (!Sphere)
		{
			Sphere = LoadObject<UStaticMesh>(
				nullptr,
				TEXT("/Engine/EditorMeshes/EditorSphere.EditorSphere"));
		}
		MeshComponent->SetStaticMesh(Sphere);
		PreviewScene.AddComponent(MeshComponent, FTransform::Identity);
		MeshComponent->UpdateBounds();
		const float FloorClearance = 0.5f;
		const float HeightAboveFloor = -MeshComponent->Bounds.GetBox().Min.Z + FloorClearance;
		MeshComponent->SetRelativeLocation(FVector(0.0f, 0.0f, HeightAboveFloor));
		MeshComponent->UpdateBounds();

		FogComponent = NewObject<UExponentialHeightFogComponent>();
		FogComponent->SetFogHeightFalloff(0.01f);
		FogComponent->SetFogMaxOpacity(1.0f);
		FogComponent->SetFogInscatteringColor(MixtormatPalette::PreviewFog());
		PreviewScene.AddComponent(FogComponent, FTransform::Identity);

		const FMixtormatStudioLightSettings LightSettings =
			MixtormatPreviewSceneSettings::GetStudioLighting(EMixtormatStudioLighting::Rim);
		const FString EnvironmentPath = MixtormatPreviewSceneSettings::GetStudioEnvironmentObjectPath(
			EMixtormatStudioLighting::Rim);
		EnvironmentCubemap.Reset(LoadObject<UTextureCube>(nullptr, *EnvironmentPath));
		Profile->EnvironmentCubeMap = EnvironmentCubemap.Get();
		Profile->EnvironmentCubeMapPath = EnvironmentPath;
		Profile->SkyLightIntensity = 2.0f;
		Profile->DirectionalLightIntensity = LightSettings.LightBrightness;
		PreviewScene.UpdateScene(*Profile, true, true, false, true);
		PreviewScene.SetEnvironmentVisibility(false, true);
		PreviewScene.SetFloorVisibility(true, true);
		PreviewScene.SetLightBrightness(LightSettings.LightBrightness);
		PreviewScene.SetSkyBrightness(2.0f);
		PreviewScene.SetLightDirection(LightSettings.LightRotation);
		if (PreviewScene.DirectionalLight)
		{
			PreviewScene.DirectionalLight->SetLightSourceAngle(LightSettings.LightSourceAngle);
			PreviewScene.DirectionalLight->SetLightSourceSoftAngle(LightSettings.LightSourceAngle * 0.5f);
			PreviewScene.DirectionalLight->SetShadowBias(0.75f);
			PreviewScene.DirectionalLight->SetShadowSlopeBias(0.8f);
			PreviewScene.DirectionalLight->ShadowSharpen = 0.0f;
			PreviewScene.DirectionalLight->ContactShadowLength = 0.0f;
			PreviewScene.DirectionalLight->MarkRenderStateDirty();
		}
	}

	~FMixtormatSurfaceThumbnailScene()
	{
		if (MeshComponent)
		{
			PreviewScene.RemoveComponent(MeshComponent);
		}
		if (FogComponent)
		{
			PreviewScene.RemoveComponent(FogComponent);
		}
	}

	bool Render(UMaterialInterface& Material, TArray<FColor>& OutPixels, FString& OutError)
	{
		if (!MeshComponent || !MeshComponent->GetStaticMesh())
		{
			OutError = TEXT("The Mixtormat thumbnail sphere could not be loaded.");
			return false;
		}
		TStrongObjectPtr<UMaterialInstanceDynamic> ThumbnailMaterial(
			UMaterialInstanceDynamic::Create(&Material, MeshComponent));
		if (!ThumbnailMaterial.IsValid())
		{
			OutError = TEXT("Failed to create the surface thumbnail material instance.");
			return false;
		}
		ThumbnailMaterial->SetScalarParameterValue(TEXT("DA_UseHeight"), 0.0f);
		ThumbnailMaterial->SetScalarParameterValue(TEXT("DA_HeightAmount"), 1.0f);

		const int32 MaterialSlotCount = FMath::Max(MeshComponent->GetNumMaterials(), 1);
		for (int32 MaterialIndex = 0; MaterialIndex < MaterialSlotCount; ++MaterialIndex)
		{
			MeshComponent->SetMaterial(MaterialIndex, ThumbnailMaterial.Get());
		}
		MeshComponent->MarkRenderStateDirty();

		const int32 Resolution = MixtormatPreviewSceneSettings::ThumbnailResolution;
		const FVector Target = MeshComponent->Bounds.Origin;
		const float CameraDistance = MixtormatPreviewSceneSettings::CalculateFocusDistance(
			static_cast<float>(MeshComponent->Bounds.SphereRadius),
			MixtormatPreviewCamera::FovDefault,
			FVector2D(Resolution, Resolution));
		const FVector ViewDirection = FRotator(
			MixtormatPreviewCamera::PitchDefault,
			MixtormatPreviewCamera::YawDefault,
			0.0f).Vector();
		const FVector CameraLocation = Target - ViewDirection * CameraDistance;
		const FRotator CameraRotation = (Target - CameraLocation).Rotation();

		const float MeshRadius = FMath::Max(MeshComponent->Bounds.SphereRadius, 0.5f);
		const float StrongFadeDistance = MeshRadius * 4.0f;
		FogComponent->SetStartDistance(CameraDistance * 2.0f);
		FogComponent->SetFogDensity(FMath::Clamp(
			-FMath::Loge(0.05f) * 1000.0f / StrongFadeDistance,
			0.001f,
			20.0f));

		TStrongObjectPtr<UTextureRenderTarget2D> RenderTarget(
			NewObject<UTextureRenderTarget2D>(GetTransientPackage()));
		RenderTarget->ClearColor = MixtormatPalette::PreviewBackground();
		RenderTarget->bAutoGenerateMips = false;
		RenderTarget->InitCustomFormat(Resolution, Resolution, PF_B8G8R8A8, false);
		RenderTarget->UpdateResourceImmediate(true);
		FTextureRenderTargetResource* Resource = RenderTarget->GameThread_GetRenderTargetResource();
		if (!Resource)
		{
			OutError = TEXT("Failed to create the surface thumbnail render target.");
			return false;
		}

		FEngineShowFlags ShowFlags(ESFIM_Editor);
		MixtormatPreviewSceneSettings::ConfigureQuality(
			ShowFlags,
			EMixtormatPreviewQuality::Medium);
		ShowFlags.SetAntiAliasing(false);
		ShowFlags.SetTemporalAA(false);
		ShowFlags.SetMotionBlur(false);
		ShowFlags.SetOnScreenDebug(false);
		ShowFlags.SetSelectionOutline(false);
		ShowFlags.SetCompositeEditorPrimitives(false);
		ShowFlags.SetGrid(false);

		FSceneViewFamilyContext ViewFamily(
			FSceneViewFamily::ConstructionValues(
				Resource,
				PreviewScene.GetScene(),
				ShowFlags)
			.SetRealtimeUpdate(false));
		FSceneViewInitOptions ViewOptions;
		ViewOptions.ViewFamily = &ViewFamily;
		ViewOptions.SetViewRectangle(FIntRect(0, 0, Resolution, Resolution));
		ViewOptions.ViewOrigin = CameraLocation;
		ViewOptions.ViewRotationMatrix = FInverseRotationMatrix(CameraRotation) * FMatrix(
			FPlane(0, 0, 1, 0),
			FPlane(1, 0, 0, 0),
			FPlane(0, 1, 0, 0),
			FPlane(0, 0, 0, 1));
		const float HalfFov = FMath::DegreesToRadians(MixtormatPreviewCamera::FovDefault) * 0.5f;
		ViewOptions.ProjectionMatrix = FReversedZPerspectiveMatrix(
			HalfFov,
			1.0f,
			1.0f,
			GNearClippingPlane);

		FSceneView* View = new FSceneView(ViewOptions);
		ViewFamily.Views.Add(View);
		View->StartFinalPostprocessSettings(CameraLocation);
		View->OverridePostProcessSettings(Profile->PostProcessingSettings, 1.0f);
		View->EndFinalPostprocessSettings(ViewOptions);
		ViewFamily.SetScreenPercentageInterface(new FLegacyScreenPercentageDriver(
			ViewFamily,
			1.0f));

		FCanvas Canvas(
			Resource,
			nullptr,
			PreviewScene.GetWorld(),
			GMaxRHIFeatureLevel);
		Canvas.Clear(MixtormatPalette::PreviewBackground());
		Canvas.Flush_GameThread();
		FModuleManager::LoadModuleChecked<IRendererModule>(TEXT("Renderer"))
			.BeginRenderingViewFamily(&Canvas, &ViewFamily);
		FlushRenderingCommands();

		FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
		ReadFlags.SetLinearToGamma(false);
		if (!Resource->ReadPixels(OutPixels, ReadFlags)
			|| OutPixels.Num() != Resolution * Resolution)
		{
			OutError = TEXT("Failed to read the rendered surface thumbnail pixels.");
			return false;
		}
		return true;
	}

private:
	FAdvancedPreviewScene PreviewScene;
	TUniquePtr<FPreviewSceneProfile> Profile;
	TStrongObjectPtr<UTextureCube> EnvironmentCubemap;
	UStaticMeshComponent* MeshComponent = nullptr;
	UExponentialHeightFogComponent* FogComponent = nullptr;
};

class FMixtormatThumbnailRenderer::FImpl
{
public:
	TUniquePtr<FMixtormatSurfaceThumbnailScene> SurfaceScene;
};

FMixtormatThumbnailRenderer::FMixtormatThumbnailRenderer()
	: Impl(MakeUnique<FImpl>())
{
}

FMixtormatThumbnailRenderer::~FMixtormatThumbnailRenderer() = default;

FMixtormatThumbnailUpdate FMixtormatThumbnailRenderer::CreateOrUpdateMaskThumbnail(
	UTexture2D& MaskTexture)
{
	FMixtormatThumbnailUpdate Result;
	const int32 SourceWidth = MaskTexture.Source.GetSizeX();
	const int32 SourceHeight = MaskTexture.Source.GetSizeY();
	TArray<FColor> SourcePixels;
	if (!ReadSourcePixels(MaskTexture, SourcePixels, Result.Error))
	{
		return Result;
	}

	const TArray<FColor> ThumbnailPixels = ResizeMaskToThumbnail(
		SourcePixels,
		SourceWidth,
		SourceHeight);
	return CreateOrUpdateThumbnailTexture(
		FMixtormatPaths::MaskThumbnailsRoot(),
		MaskTexture.GetName() + TEXT("_Thumbnail"),
		ThumbnailPixels);
}

FMixtormatThumbnailUpdate FMixtormatThumbnailRenderer::CreateOrUpdateSurfaceThumbnail(
	UMaterialInterface& PreviewMaterial,
	const FString& Family,
	const FString& SurfaceAssetName)
{
	FMixtormatThumbnailUpdate Result;
	if (!Impl->SurfaceScene)
	{
		Impl->SurfaceScene = MakeUnique<FMixtormatSurfaceThumbnailScene>();
	}

	TArray<FColor> Pixels;
	if (!Impl->SurfaceScene->Render(PreviewMaterial, Pixels, Result.Error))
	{
		return Result;
	}
	return CreateOrUpdateThumbnailTexture(
		FMixtormatPaths::SurfaceThumbnailFamilyRoot(Family),
		SurfaceAssetName + TEXT("_Thumbnail"),
		Pixels);
}
