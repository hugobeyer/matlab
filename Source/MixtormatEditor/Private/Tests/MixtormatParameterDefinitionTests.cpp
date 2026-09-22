// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "MixtormatMaterial.h"
#include "MixtormatParameterDefinition.h"
#include "UObject/UnrealType.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// The drift guard for the Breakup pilot: every registered definition that names a serialized
// FMixtormatLayerEffect property must agree with that property's initializer. When this fails,
// someone changed one side without the other -- the table is canonical, so fix the struct (or
// the table, if the old default was the mistake).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatBreakupDefinitionDefaultsTest,
	"Mixtormat.Parameters.BreakupDefinitionDefaultsMatchStruct",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatBreakupDefinitionDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FMixtormatLayerEffect EffectDefaults;
	const UScriptStruct* EffectStruct = FMixtormatLayerEffect::StaticStruct();
	int32 Checked = 0;

	MixtormatParameterDefinitions::ForEach(
		[&EffectDefaults, EffectStruct, &Checked, this](const FMixtormatParameterDefinition& Definition)
	{
		if (Definition.Owner != EMixtormatParameterOwnerType::Effect)
		{
			return;
		}
		const FProperty* Property = EffectStruct->FindPropertyByName(Definition.Parameter);
		if (!Property)
		{
			AddError(FString::Printf(
				TEXT("Definition Effect.%s names no property on FMixtormatLayerEffect"),
				*Definition.Parameter.ToString()));
			return;
		}

		float StructDefault = 0.0f;
		if (const FFloatProperty* Float = CastField<FFloatProperty>(Property))
		{
			StructDefault = *Float->ContainerPtrToValuePtr<float>(&EffectDefaults);
		}
		else if (const FIntProperty* Int = CastField<FIntProperty>(Property))
		{
			StructDefault = static_cast<float>(*Int->ContainerPtrToValuePtr<int32>(&EffectDefaults));
		}
		else
		{
			AddError(FString::Printf(
				TEXT("Definition Effect.%s is neither a float nor an int property"),
				*Definition.Parameter.ToString()));
			return;
		}

		TestEqual(
			FString::Printf(TEXT("Effect.%s default"), *Definition.Parameter.ToString()),
			StructDefault, Definition.Default);
		++Checked;
	});

	TestTrue(TEXT("The Breakup table registered definitions"), Checked > 0);
	return true;
}

// SanitizeFloat/SanitizeInt32 are the compositor's only validation path for migrated values,
// so their contract is pinned here: NaN falls back to the default, unset bounds never clamp,
// set bounds always do.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatParameterSanitizeTest,
	"Mixtormat.Parameters.SanitizeFloatContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatParameterSanitizeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FName Name(TEXT("BreakupNormalStrength"));
	const EMixtormatParameterOwnerType Owner = EMixtormatParameterOwnerType::Effect;
	const EMixtormatParameterValueType Type = EMixtormatParameterValueType::Float;

	// HardMin 0, HardMax unset: typed values past the UI range must survive untouched.
	TestEqual(TEXT("Finite value passes"),
		MixtormatParameterDefinitions::SanitizeFloat(Owner, Name, Type, 8.0f), 8.0f);
	TestEqual(TEXT("NaN falls back to default"),
		MixtormatParameterDefinitions::SanitizeFloat(Owner, Name, Type, std::numeric_limits<float>::quiet_NaN()),
		2.0f);
	TestEqual(TEXT("HardMin clamps"),
		MixtormatParameterDefinitions::SanitizeFloat(Owner, Name, Type, -4.0f), 0.0f);

	// A hard-bounded int: 1..16.
	TestEqual(TEXT("Int clamp low"),
		MixtormatParameterDefinitions::SanitizeInt32(Owner, FName(TEXT("BreakupDistortionFrequency")), 0), 1);
	TestEqual(TEXT("Int clamp high"),
		MixtormatParameterDefinitions::SanitizeInt32(Owner, FName(TEXT("BreakupDistortionFrequency")), 99), 16);
	TestEqual(TEXT("Int in range passes"),
		MixtormatParameterDefinitions::SanitizeInt32(Owner, FName(TEXT("BreakupDistortionFrequency")), 5), 5);

	return true;
}

#endif
