#include "MixtormatNormalHeightGenerator.h"

#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "ShaderParameterStruct.h"
#include "TextureResource.h"
#include "UObject/StrongObjectPtr.h"

class FMixtormatDecodeGradientCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatDecodeGradientCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatDecodeGradientCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, NormalYSign)
		SHADER_PARAMETER(float, MinNormalZ)
		SHADER_PARAMETER(float, MaxGradient)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, InputNormal)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputComplex)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatDecodeGradientCS,
	"/Plugin/MaterialLab/Private/MixtormatNormalHeight.usf",
	"DecodeGradientCS",
	SF_Compute);

class FMixtormatBitReverseCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatBitReverseCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatBitReverseCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Log2Size)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, InputComplex)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputComplex)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatBitReverseCS,
	"/Plugin/MaterialLab/Private/MixtormatNormalHeight.usf",
	"BitReverseCS",
	SF_Compute);

class FMixtormatFFTRadix2CS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatFFTRadix2CS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatFFTRadix2CS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Axis)
		SHADER_PARAMETER(uint32, StageSize)
		SHADER_PARAMETER(uint32, Inverse)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, InputComplex)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputComplex)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatFFTRadix2CS,
	"/Plugin/MaterialLab/Private/MixtormatNormalHeight.usf",
	"FFTCS",
	SF_Compute);

class FMixtormatPoissonCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatPoissonCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatPoissonCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, InputComplex)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputComplex)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatPoissonCS,
	"/Plugin/MaterialLab/Private/MixtormatNormalHeight.usf",
	"PoissonCS",
	SF_Compute);

class FMixtormatExtractHeightCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatExtractHeightCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatExtractHeightCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, InputComplex)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputHeight)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatExtractHeightCS,
	"/Plugin/MaterialLab/Private/MixtormatNormalHeight.usf",
	"ExtractHeightCS",
	SF_Compute);

namespace MixtormatNormalHeight
{
	constexpr int32 ThreadGroupSize = 8;
	constexpr float FlatHeightEpsilon = 1.0e-8f;

	bool IsSupportedResolution(const FIntPoint Size)
	{
		return Size.X == Size.Y
			&& (Size.X == 1024 || Size.X == 2048 || Size.X == 4096);
	}

	float SelectKth(TArray<float>& Values, const int32 K)
	{
		int32 Left = 0;
		int32 Right = Values.Num() - 1;
		while (Left < Right)
		{
			const float Pivot = Values[Left + (Right - Left) / 2];
			int32 Low = Left;
			int32 High = Right;
			while (Low <= High)
			{
				while (Values[Low] < Pivot)
				{
					++Low;
				}
				while (Values[High] > Pivot)
				{
					--High;
				}
				if (Low <= High)
				{
					Values.Swap(Low, High);
					++Low;
					--High;
				}
			}
			if (K <= High)
			{
				Right = High;
			}
			else if (K >= Low)
			{
				Left = Low;
			}
			else
			{
				break;
			}
		}
		return Values[K];
	}

	TStrongObjectPtr<UTexture2D> CreateNormalUpload(
		const TConstArrayView<FColor> Pixels,
		const FIntPoint Size,
		FString& OutError)
	{
		if (Pixels.Num() != Size.X * Size.Y)
		{
			OutError = FString::Printf(
				TEXT("Normal source pixel count does not match %dx%d."),
				Size.X,
				Size.Y);
			return TStrongObjectPtr<UTexture2D>();
		}

		UTexture2D* Texture = UTexture2D::CreateTransient(
			Size.X,
			Size.Y,
			PF_B8G8R8A8);
		if (!Texture || !Texture->GetPlatformData()
			|| Texture->GetPlatformData()->Mips.IsEmpty())
		{
			OutError = TEXT("Failed to allocate the uncompressed normal upload texture.");
			return TStrongObjectPtr<UTexture2D>();
		}

		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		const int64 RequiredBytes = static_cast<int64>(Pixels.Num()) * sizeof(FColor);
		if (Mip.BulkData.GetBulkDataSize() < RequiredBytes)
		{
			OutError = TEXT("The uncompressed normal upload texture has insufficient mip storage.");
			return TStrongObjectPtr<UTexture2D>();
		}
		void* Destination = Mip.BulkData.Lock(LOCK_READ_WRITE);
		if (!Destination)
		{
			OutError = TEXT("Failed to lock the uncompressed normal upload texture.");
			return TStrongObjectPtr<UTexture2D>();
		}
		FMemory::Memcpy(
			Destination,
			Pixels.GetData(),
			RequiredBytes);
		Mip.BulkData.Unlock();
		Texture->SRGB = false;
		Texture->NeverStream = true;
		Texture->Filter = TF_Nearest;
		Texture->UpdateResource();
		FlushRenderingCommands();
		return TStrongObjectPtr<UTexture2D>(Texture);
	}

	UTextureRenderTarget2D* CreateRawHeightTarget(const FIntPoint Size)
	{
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
		Target->ClearColor = FLinearColor::Black;
		Target->bCanCreateUAV = true;
		Target->bAutoGenerateMips = false;
		Target->Filter = TF_Nearest;
		Target->AddressX = TA_Clamp;
		Target->AddressY = TA_Clamp;
		Target->InitCustomFormat(Size.X, Size.Y, PF_R16F, true);
		Target->UpdateResourceImmediate(true);
		FlushRenderingCommands();
		return Target;
	}

	FIntVector GetDispatchGroupCount(const FIntPoint Size)
	{
		return FIntVector(
			FMath::DivideAndRoundUp(Size.X, ThreadGroupSize),
			FMath::DivideAndRoundUp(Size.Y, ThreadGroupSize),
			1);
	}
}

bool FMixtormatNormalHeightGenerator::Generate(
	const TConstArrayView<FColor> NormalPixels,
	const FIntPoint Size,
	const bool bFlipNormalY,
	TArray<uint8>& OutHeight,
	FString& OutError)
{
	using namespace MixtormatNormalHeight;
	check(IsInGameThread());

	OutHeight.Reset();
	OutError.Reset();
	if (!IsSupportedResolution(Size))
	{
		OutError = FString::Printf(
			TEXT("Normal-derived height requires a square 1024, 2048, or 4096 texture; received %dx%d."),
			Size.X,
			Size.Y);
		return false;
	}

	TStrongObjectPtr<UTexture2D> NormalUpload = CreateNormalUpload(
		NormalPixels,
		Size,
		OutError);
	if (!NormalUpload.IsValid()
		|| !NormalUpload->GetResource()
		|| !NormalUpload->GetResource()->TextureRHI.IsValid())
	{
		if (OutError.IsEmpty())
		{
			OutError = TEXT("The uncompressed normal upload has no render resource.");
		}
		return false;
	}

	TStrongObjectPtr<UTextureRenderTarget2D> RawHeightTarget(CreateRawHeightTarget(Size));
	if (!RawHeightTarget.IsValid()
		|| !RawHeightTarget->GameThread_GetRenderTargetResource())
	{
		OutError = TEXT("Failed to allocate the normal-derived height readback target.");
		return false;
	}

	const FTextureRHIRef NormalRHI = NormalUpload->GetResource()->TextureRHI;
	const FTextureRHIRef HeightRHI =
		RawHeightTarget->GameThread_GetRenderTargetResource()->GetRenderTargetTexture();
	if (!NormalRHI.IsValid())
	{
		OutError = TEXT("The normal upload GPU resource became invalid before FFT dispatch.");
		return false;
	}
	if (!HeightRHI.IsValid())
	{
		OutError = TEXT("Failed to create the R16F normal-derived height output target.");
		return false;
	}

	const uint32 Log2Size = FMath::FloorLog2(Size.X);
	ENQUEUE_RENDER_COMMAND(MixtormatGenerateNormalHeight)(
		[NormalRHI, HeightRHI, Size, Log2Size, bFlipNormalY](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);
			FRDGTextureRef InputNormal = GraphBuilder.RegisterExternalTexture(
				CreateRenderTarget(NormalRHI, TEXT("Mixtormat.NormalHeight.InputNormal")));
			FRDGTextureRef OutputHeight = GraphBuilder.RegisterExternalTexture(
				CreateRenderTarget(HeightRHI, TEXT("Mixtormat.NormalHeight.Output")));

			const FRDGTextureDesc SpectrumDescription = FRDGTextureDesc::Create2D(
				Size,
				PF_A32B32G32R32F,
				FClearValueBinding::None,
				TexCreate_ShaderResource | TexCreate_UAV);
			FRDGTextureRef SpectrumA = GraphBuilder.CreateTexture(
				SpectrumDescription,
				TEXT("Mixtormat.NormalHeight.SpectrumA"));
			FRDGTextureRef SpectrumB = GraphBuilder.CreateTexture(
				SpectrumDescription,
				TEXT("Mixtormat.NormalHeight.SpectrumB"));
			FRDGTextureRef Current = SpectrumA;
			FRDGTextureRef Other = SpectrumB;
			const FIntVector GroupCount = GetDispatchGroupCount(Size);

			TShaderMapRef<FMixtormatDecodeGradientCS> DecodeShader(
				GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FMixtormatDecodeGradientCS::FParameters* DecodeParameters =
				GraphBuilder.AllocParameters<FMixtormatDecodeGradientCS::FParameters>();
			DecodeParameters->OutputSize = Size;
			DecodeParameters->NormalYSign = bFlipNormalY ? -1.0f : 1.0f;
			DecodeParameters->MinNormalZ = 1.0e-4f;
			DecodeParameters->MaxGradient = 64.0f;
			DecodeParameters->InputNormal = InputNormal;
			DecodeParameters->OutputComplex = GraphBuilder.CreateUAV(Current);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.NormalHeight.DecodeGradient"),
				DecodeShader,
				DecodeParameters,
				GroupCount);

			const auto AddBitReversePass = [&]()
			{
				TShaderMapRef<FMixtormatBitReverseCS> Shader(
					GetGlobalShaderMap(GMaxRHIFeatureLevel));
				FMixtormatBitReverseCS::FParameters* Parameters =
					GraphBuilder.AllocParameters<FMixtormatBitReverseCS::FParameters>();
				Parameters->OutputSize = Size;
				Parameters->Log2Size = Log2Size;
				Parameters->InputComplex = Current;
				Parameters->OutputComplex = GraphBuilder.CreateUAV(Other);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("Mixtormat.NormalHeight.BitReverse"),
					Shader,
					Parameters,
					GroupCount);
				Swap(Current, Other);
			};

			const auto AddTransformPasses = [&](const uint32 bInverse)
			{
				for (uint32 Axis = 0; Axis < 2; ++Axis)
				{
					for (uint32 Stage = 1; Stage <= Log2Size; ++Stage)
					{
						TShaderMapRef<FMixtormatFFTRadix2CS> Shader(
							GetGlobalShaderMap(GMaxRHIFeatureLevel));
						FMixtormatFFTRadix2CS::FParameters* Parameters =
							GraphBuilder.AllocParameters<FMixtormatFFTRadix2CS::FParameters>();
						Parameters->OutputSize = Size;
						Parameters->Axis = Axis;
						Parameters->StageSize = 1u << Stage;
						Parameters->Inverse = bInverse;
						Parameters->InputComplex = Current;
						Parameters->OutputComplex = GraphBuilder.CreateUAV(Other);
						FComputeShaderUtils::AddPass(
							GraphBuilder,
							RDG_EVENT_NAME(
								"Mixtormat.NormalHeight.FFT.%s.Axis%d.Stage%d",
								bInverse != 0 ? TEXT("Inverse") : TEXT("Forward"),
								Axis,
								Stage),
							Shader,
							Parameters,
							GroupCount);
						Swap(Current, Other);
					}
				}
			};

			AddBitReversePass();
			AddTransformPasses(0u);

			TShaderMapRef<FMixtormatPoissonCS> PoissonShader(
				GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FMixtormatPoissonCS::FParameters* PoissonParameters =
				GraphBuilder.AllocParameters<FMixtormatPoissonCS::FParameters>();
			PoissonParameters->OutputSize = Size;
			PoissonParameters->InputComplex = Current;
			PoissonParameters->OutputComplex = GraphBuilder.CreateUAV(Other);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.NormalHeight.Poisson"),
				PoissonShader,
				PoissonParameters,
				GroupCount);
			Swap(Current, Other);

			AddBitReversePass();
			AddTransformPasses(1u);

			TShaderMapRef<FMixtormatExtractHeightCS> ExtractShader(
				GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FMixtormatExtractHeightCS::FParameters* ExtractParameters =
				GraphBuilder.AllocParameters<FMixtormatExtractHeightCS::FParameters>();
			ExtractParameters->OutputSize = Size;
			ExtractParameters->InputComplex = Current;
			ExtractParameters->OutputHeight = GraphBuilder.CreateUAV(OutputHeight);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.NormalHeight.Extract"),
				ExtractShader,
				ExtractParameters,
				GroupCount);
			GraphBuilder.SetTextureAccessFinal(OutputHeight, ERHIAccess::SRVMask);
			GraphBuilder.Execute();
		});
	FlushRenderingCommands();

	TArray<FLinearColor> LinearPixels;
	FReadSurfaceDataFlags ReadFlags(RCM_MinMax);
	ReadFlags.SetLinearToGamma(false);
	if (!RawHeightTarget->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(
		LinearPixels,
		ReadFlags)
		|| LinearPixels.Num() != Size.X * Size.Y)
	{
		OutError = TEXT("Failed to read back the reconstructed height field.");
		return false;
	}

	TArray<float> RawHeight;
	RawHeight.SetNumUninitialized(LinearPixels.Num());
	int32 NonFiniteCount = 0;
	for (int32 Index = 0; Index < LinearPixels.Num(); ++Index)
	{
		if (!FMath::IsFinite(LinearPixels[Index].R))
		{
			++NonFiniteCount;
			RawHeight[Index] = 0.0f;
		}
		else
		{
			RawHeight[Index] = LinearPixels[Index].R;
		}
	}
	LinearPixels.Empty();
	if (NonFiniteCount > 0)
	{
		OutError = FString::Printf(
			TEXT("Normal-derived height produced %d non-finite pixels."),
			NonFiniteCount);
		return false;
	}

	TArray<float> Scratch = RawHeight;
	const int32 UpperMedianIndex = Scratch.Num() / 2;
	const float UpperMedian = SelectKth(Scratch, UpperMedianIndex);
	const float LowerMedian = SelectKth(Scratch, UpperMedianIndex - 1);
	const float Median = 0.5f * (LowerMedian + UpperMedian);
	for (int32 Index = 0; Index < Scratch.Num(); ++Index)
	{
		Scratch[Index] = FMath::Abs(RawHeight[Index] - Median);
	}
	const int32 PercentileIndex = FMath::CeilToInt(0.99 * static_cast<double>(Scratch.Num() - 1));
	const float Extent = SelectKth(Scratch, PercentileIndex);

	OutHeight.SetNumUninitialized(RawHeight.Num());
	if (!FMath::IsFinite(Extent) || Extent <= FlatHeightEpsilon)
	{
		FMemory::Memset(OutHeight.GetData(), 128, OutHeight.Num());
		return true;
	}

	for (int32 Index = 0; Index < RawHeight.Num(); ++Index)
	{
		const float Normalized = FMath::Clamp(
			0.5f + 0.5f * (RawHeight[Index] - Median) / Extent,
			0.0f,
			1.0f);
		OutHeight[Index] = static_cast<uint8>(FMath::RoundToInt(Normalized * 255.0f));
	}
	return true;
}
