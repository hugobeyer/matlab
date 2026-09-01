#include "Misc/AutomationTest.h"

#include "MaterialLabNormalHeightGenerator.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace MaterialLabNormalHeightTests
{
	constexpr int32 TestResolution = 1024;


	FColor EncodeNormal(const FVector3f& Normal)
	{
		return FColor(
			static_cast<uint8>(FMath::RoundToInt((Normal.X * 0.5f + 0.5f) * 255.0f)),
			static_cast<uint8>(FMath::RoundToInt((Normal.Y * 0.5f + 0.5f) * 255.0f)),
			static_cast<uint8>(FMath::RoundToInt((Normal.Z * 0.5f + 0.5f) * 255.0f)),
			255);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMaterialLabNormalDerivedHeightTest,
	"MaterialLab.Import.NormalDerivedHeight",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMaterialLabNormalDerivedHeightTest::RunTest(const FString& Parameters)
{
	using namespace MaterialLabNormalHeightTests;
	(void)Parameters;

	TArray<FColor> FlatPixels;
	FlatPixels.Init(FColor(128, 128, 255, 255), TestResolution * TestResolution);
	TArray<uint8> Height;
	FString Error;
	TestTrue(
		TEXT("Flat normal height generation succeeds"),
		FMaterialLabNormalHeightGenerator::Generate(
			FlatPixels,
			FIntPoint(TestResolution, TestResolution),
			false,
			Height,
			Error));
	TestEqual(TEXT("Flat height pixel count"), Height.Num(), TestResolution * TestResolution);
	if (Height.Num() == TestResolution * TestResolution)
	{
		uint8 Minimum = 255;
		uint8 Maximum = 0;
		for (const uint8 Value : Height)
		{
			Minimum = FMath::Min(Minimum, Value);
			Maximum = FMath::Max(Maximum, Value);
		}
		TestEqual(TEXT("Flat normal minimum is neutral"), Minimum, static_cast<uint8>(128));
		TestEqual(TEXT("Flat normal maximum is neutral"), Maximum, static_cast<uint8>(128));
	}

	TArray<FColor> RidgePixels;
	RidgePixels.SetNumUninitialized(TestResolution * TestResolution);
	for (int32 Y = 0; Y < TestResolution; ++Y)
	{
		for (int32 X = 0; X < TestResolution; ++X)
		{
			const float Phase = 2.0f * PI * static_cast<float>(X) / TestResolution;
			const float GradientX = 0.5f * FMath::Cos(Phase);
			const FVector3f Normal = FVector3f(-GradientX, 0.0f, 1.0f).GetSafeNormal();
			RidgePixels[Y * TestResolution + X] = EncodeNormal(Normal);
		}
	}
	Height.Reset();
	Error.Reset();
	TestTrue(
		TEXT("Periodic ridge height generation succeeds"),
		FMaterialLabNormalHeightGenerator::Generate(
			RidgePixels,
			FIntPoint(TestResolution, TestResolution),
			false,
			Height,
			Error));
	if (Height.Num() == TestResolution * TestResolution)
	{
		uint8 Minimum = 255;
		uint8 Maximum = 0;
		for (const uint8 Value : Height)
		{
			Minimum = FMath::Min(Minimum, Value);
			Maximum = FMath::Max(Maximum, Value);
		}
		TestTrue(TEXT("Periodic ridge produces valleys"), Minimum < 96);
		TestTrue(TEXT("Periodic ridge produces ridges"), Maximum > 160);
		const int32 CenterRow = TestResolution / 2;
		const uint8 ZeroPhase = Height[CenterRow * TestResolution];
		const uint8 PositivePeak = Height[
			CenterRow * TestResolution + TestResolution / 4];
		const uint8 NegativePeak = Height[
			CenterRow * TestResolution + 3 * TestResolution / 4];
		TestTrue(TEXT("Periodic ridge zero phase stays near neutral"),
			FMath::Abs(static_cast<int32>(ZeroPhase) - 128) <= 8);
		TestTrue(TEXT("Periodic ridge positive phase reconstructs a ridge"), PositivePeak > 224);
		TestTrue(TEXT("Periodic ridge negative phase reconstructs a valley"), NegativePeak < 32);
	}
	return true;
}

#endif
