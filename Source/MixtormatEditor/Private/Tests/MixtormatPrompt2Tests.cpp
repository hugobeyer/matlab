// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatMaterial.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatPrompt2ChildEnumTest,
	"Mixtormat.Prompt2.Compatibility.ChildEnumAppendOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatPrompt2ChildEnumTest::RunTest(const FString&)
{
	const EMixtormatLayerChildType Types[] = {
		EMixtormatLayerChildType::Mask,
		EMixtormatLayerChildType::Effect,
		EMixtormatLayerChildType::Generated,
		EMixtormatLayerChildType::Craquelure,
		EMixtormatLayerChildType::ColorId,
		EMixtormatLayerChildType::Filter,
		EMixtormatLayerChildType::HsvFilter,
		EMixtormatLayerChildType::RandomId,
		EMixtormatLayerChildType::RampId,
		EMixtormatLayerChildType::PatternId,
		EMixtormatLayerChildType::Blur,
		EMixtormatLayerChildType::Curvature,
		EMixtormatLayerChildType::CombineId,
		EMixtormatLayerChildType::Generator,
		EMixtormatLayerChildType::UvFromIds,
		EMixtormatLayerChildType::ReliefFromIds
	};
	const UEnum* Enum = StaticEnum<EMixtormatLayerChildType>();
	if (!TestNotNull(TEXT("Child type remains reflected"), Enum))
	{
		return false;
	}
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Types); ++Index)
	{
		TestEqual(FString::Printf(TEXT("Serialized child type at slot %d"), Index),
			static_cast<int32>(Types[Index]), Index);
		TestTrue(FString::Printf(TEXT("Child slot %d remains reflected"), Index),
			Enum->IsValidEnumValue(static_cast<int64>(Types[Index])));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatPrompt2LegacyFieldsTest,
	"Mixtormat.Prompt2.Compatibility.LegacyPatternFields",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatPrompt2LegacyFieldsTest::RunTest(const FString&)
{
	const UScriptStruct* Pattern = FMixtormatPatternFilter::StaticStruct();
	const auto CheckSerializable = [this](const TCHAR* Name, const FProperty* Property)
	{
		if (TestNotNull(FString::Printf(TEXT("Legacy field %s retains its reflected type"), Name), Property))
		{
			TestFalse(FString::Printf(TEXT("Legacy field %s remains serializable"), Name),
				Property->HasAnyPropertyFlags(CPF_Transient | CPF_SkipSerialization | CPF_Deprecated));
		}
	};
	for (const TCHAR* Name : {
		TEXT("bUVVariation"), TEXT("bOrthogonalUV"), TEXT("bRandomFlipU"),
		TEXT("bRandomFlipV"), TEXT("bRelativeEdgeWidth") })
	{
		CheckSerializable(Name, FindFProperty<FBoolProperty>(Pattern, FName(Name)));
	}
	for (const TCHAR* Name : {
		TEXT("UVRotationMin"), TEXT("UVRotationMax"), TEXT("UVScaleMin"),
		TEXT("UVScaleMax"), TEXT("UVOffset"), TEXT("HeightAmount"), TEXT("HeightRandom"),
		TEXT("Profile"), TEXT("ProfileRandom"), TEXT("Feather"), TEXT("FeatherRandom"),
		TEXT("FeatherGain"), TEXT("BevelHeight"), TEXT("BevelWidthPixels"),
		TEXT("BevelWidthCells"), TEXT("BevelVariation"), TEXT("BevelInsetPixels"),
		TEXT("GapHeight"), TEXT("EdgeRoughness"), TEXT("EdgeRoughnessAmount"),
		TEXT("AOAmount"), TEXT("AOSpread") })
	{
		CheckSerializable(Name, FindFProperty<FFloatProperty>(Pattern, FName(Name)));
	}
	CheckSerializable(TEXT("Seed"), FindFProperty<FIntProperty>(Pattern, TEXT("Seed")));
	const FStructProperty* Payload = FindFProperty<FStructProperty>(
		FMixtormatLayerChild::StaticStruct(), TEXT("PatternId"));
	CheckSerializable(TEXT("PatternId"), Payload);
	if (Payload)
	{
		TestTrue(TEXT("Legacy child payload remains FMixtormatPatternFilter"), Payload->Struct == Pattern);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
