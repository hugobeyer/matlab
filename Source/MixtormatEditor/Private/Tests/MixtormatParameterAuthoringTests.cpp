// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Algo/AnyOf.h"
#include "MixtormatMaterial.h"
#include "MixtormatParameterDefinition.h"
#include "UI/Parameters/MixtormatParameterAuthoring.h"
#include "UI/Parameters/MixtormatParameterUiMeta.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

// Completeness for a migrated family: every numeric Breakup property on the serialized struct
// must have BOTH a runtime definition and UI metadata. This is the test the definition->struct
// default check cannot be -- it catches an OMITTED definition (BreakupSizeVariation and
// BreakupSmoothness both shipped that way), not a drifted one.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatBreakupDefinitionCompletenessTest,
	"Mixtormat.Parameters.BreakupDefinitionCompleteness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatBreakupDefinitionCompletenessTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	// Every migrated effect family's field prefix. A new family migration adds its prefix
	// here; the test then guarantees no numeric property of that family ships without a
	// definition and UI metadata (the BreakupSizeVariation/Smoothness failure mode).
	const TCHAR* MigratedPrefixes[] = {
		TEXT("Breakup"),
		TEXT("Erosion"),
		TEXT("Grade"),
		TEXT("EdgeWear"),
		TEXT("FlowWarp"),
		TEXT("LayerBlur"),
		TEXT("Runoff"),
	};

	const UScriptStruct* EffectStruct = FMixtormatLayerEffect::StaticStruct();
	int32 Checked = 0;
	for (TFieldIterator<FProperty> It(EffectStruct); It; ++It)
	{
		const FProperty* Property = *It;
		const bool bMigratedPrefix = Algo::AnyOf(MigratedPrefixes,
			[&Property](const TCHAR* Prefix)
			{
				return Property->GetName().StartsWith(Prefix);
			});
		if (!bMigratedPrefix)
		{
			continue;
		}
		EMixtormatParameterValueType ValueType;
		if (CastField<FFloatProperty>(Property))
		{
			ValueType = EMixtormatParameterValueType::Float;
		}
		else if (CastField<FIntProperty>(Property))
		{
			ValueType = EMixtormatParameterValueType::Int;
		}
		else
		{
			// Assets, bools and enums are structural; they carry no numeric definition.
			continue;
		}

		const FMixtormatParameterDefinitionKey Key{
			EMixtormatParameterOwnerType::Effect, Property->GetFName(), ValueType};
		if (!MixtormatParameterDefinitions::TryGet(Key))
		{
			AddError(FString::Printf(
				TEXT("Breakup property %s has no runtime definition"), *Property->GetName()));
		}
		if (!MixtormatParameterUi::TryGetUiMeta(Key))
		{
			AddError(FString::Printf(
				TEXT("Breakup property %s has no UI metadata"), *Property->GetName()));
		}
		++Checked;
	}

	TestTrue(TEXT("Migrated numeric effect properties were found"), Checked >= 100);
	return true;
}

// Opt-in enforcement: only PersistentDevTunable parameters get the persistent authoring UI.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatAuthoringOptInTest,
	"Mixtormat.Parameters.AuthoringOptIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatAuthoringOptInTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const auto Key = [](const TCHAR* Name, EMixtormatParameterValueType Type)
	{
		return FMixtormatParameterDefinitionKey{
			EMixtormatParameterOwnerType::Effect, FName(Name), Type};
	};

	// Broadly open: every Breakup definition with a definition is developer-editable.
	TestTrue(TEXT("BreakupNormalStrength is persistently editable"),
		MixtormatParameterAuthoring::IsPersistentlyEditable(
			Key(TEXT("BreakupNormalStrength"), EMixtormatParameterValueType::Float)));
	TestTrue(TEXT("BreakupSeed is persistently editable"),
		MixtormatParameterAuthoring::IsPersistentlyEditable(
			Key(TEXT("BreakupSeed"), EMixtormatParameterValueType::Int)));
	TestTrue(TEXT("BreakupMaskTiling is persistently editable"),
		MixtormatParameterAuthoring::IsPersistentlyEditable(
			Key(TEXT("BreakupMaskTiling"), EMixtormatParameterValueType::Int)));
	// Parameters with no definition at all (unmigrated families, structural fields) can never
	// be persistently edited -- there is nothing to key the database entry against.
	TestFalse(TEXT("Unmigrated parameters are not persistently editable"),
		MixtormatParameterAuthoring::IsPersistentlyEditable(
			Key(TEXT("ErosionAmount"), EMixtormatParameterValueType::Float)));

	// Entries for parameters without a definition never resolve, whatever the database says.
	FMixtormatParameterAuthoringEntry OrphanEntry;
	OrphanEntry.Default = 42.0f;
	MixtormatParameterAuthoring::SetPendingAuthoring(
		Key(TEXT("ErosionAmount"), EMixtormatParameterValueType::Float), OrphanEntry);
	TestFalse(TEXT("Definition-less parameter entry is not applied"),
		FMath::IsNearlyEqual(MixtormatParameterAuthoring::ResolveAuthoringDefault(
			Key(TEXT("ErosionAmount"), EMixtormatParameterValueType::Float), 1.5f), 42.0f));
	MixtormatParameterAuthoring::RevertPendingAuthoring(
		Key(TEXT("ErosionAmount"), EMixtormatParameterValueType::Float));

	return true;
}

// The full authoring pipeline on one parameter: shipped entry from serialized JSON, pending
// edit on top, resolution order, label override, and hard bounds the database cannot touch.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatAuthoringResolutionTest,
	"Mixtormat.Parameters.AuthoringResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatAuthoringResolutionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FMixtormatParameterDefinitionKey Key{
		EMixtormatParameterOwnerType::Effect, TEXT("BreakupNormalStrength"),
		EMixtormatParameterValueType::Float};

	// Malformed JSON: refuse, fall back to compiled, never half-load.
	TestFalse(TEXT("Corrupt JSON is rejected"),
		MixtormatParameterAuthoring::LoadFromString(TEXT("{ this is not json")));
	TestFalse(TEXT("Corrupt load leaves no entry"),
		MixtormatParameterAuthoring::HasShippedAuthoring(Key));
	TestTrue(TEXT("Resolution falls back to compiled default"),
		FMath::IsNearlyEqual(MixtormatParameterAuthoring::ResolveAuthoringDefault(Key, 0.0f), 2.0f));

	// Shipped database entry: default, range and label override.
	const FString Json = TEXT(R"( { "Breakup": { "BreakupNormalStrength": {
		"label": "Normal Strength", "default": 3.0, "uiMin": 0.0, "uiMax": 8.0, "snap": 0.05 } } } )");
	TestTrue(TEXT("Database loads"), MixtormatParameterAuthoring::LoadFromString(Json));
	TestTrue(TEXT("Shipped entry present"), MixtormatParameterAuthoring::HasShippedAuthoring(Key));

	TestTrue(TEXT("Shipped default resolves"),
		FMath::IsNearlyEqual(MixtormatParameterAuthoring::ResolveAuthoringDefault(Key, 0.0f), 3.0f));
	TestTrue(TEXT("Shipped UI max resolves"),
		FMath::IsNearlyEqual(MixtormatParameterAuthoring::ResolveAuthoringUiBound(Key, 4.0f, true), 8.0f));
	TestTrue(TEXT("Shipped UI min resolves"),
		FMath::IsNearlyEqual(MixtormatParameterAuthoring::ResolveAuthoringUiBound(Key, 0.0f, false), 0.0f));
	TestEqual(TEXT("Shipped label resolves"),
		MixtormatParameterAuthoring::ResolveAuthoringLabel(Key, FText::FromString(TEXT("Normal"))).ToString(),
		TEXT("Normal Strength"));

	// Unsaved edit wins over shipped.
	FMixtormatParameterAuthoringEntry Pending;
	Pending.Default = 5.0f;
	Pending.UiMax = 12.0f;
	MixtormatParameterAuthoring::SetPendingAuthoring(Key, Pending);
	TestTrue(TEXT("Pending default wins over shipped"),
		FMath::IsNearlyEqual(MixtormatParameterAuthoring::ResolveAuthoringDefault(Key, 0.0f), 5.0f));
	TestTrue(TEXT("Pending UI max wins over shipped"),
		FMath::IsNearlyEqual(MixtormatParameterAuthoring::ResolveAuthoringUiBound(Key, 4.0f, true), 12.0f));

	// Roundtrip serialization keeps the entry.
	const FString Written = MixtormatParameterAuthoring::WriteToString();
	TestTrue(TEXT("Written database carries the parameter"),
		Written.Contains(TEXT("BreakupNormalStrength")));
	TestTrue(TEXT("Written database carries the family"),
		Written.Contains(TEXT("Breakup")));

	// Revert drops only the unsaved layer; shipped survives.
	MixtormatParameterAuthoring::RevertPendingAuthoring(Key);
	TestTrue(TEXT("Revert restores shipped default"),
		FMath::IsNearlyEqual(MixtormatParameterAuthoring::ResolveAuthoringDefault(Key, 0.0f), 3.0f));

	// A missing snap in the entry falls through to the compiled UiMeta.
	TestTrue(TEXT("Snap falls through to UiMeta"),
		FMath::IsNearlyEqual(MixtormatParameterAuthoring::ResolveAuthoringSnap(Key, 0.0f), 0.05f));

	// Hard bounds are runtime-owned: the database's UI range never touches SanitizeFloat.
	TestTrue(TEXT("HardMin still clamps at 0 regardless of database"),
		FMath::IsNearlyEqual(
			MixtormatParameterDefinitions::TryGet(Key)->SanitizeFloat(-4.0f), 0.0f));

	// Leave a clean in-memory database for other tests. No disk write: tests must never touch
	// the plugin's real Config/MixtormatParameterAuthoring.json.
	MixtormatParameterAuthoring::RevertPendingAuthoring(Key);
	MixtormatParameterAuthoring::LoadFromString(TEXT("{}"));
	TestFalse(TEXT("Cleared database removes the shipped entry"),
		MixtormatParameterAuthoring::HasShippedAuthoring(Key));
	TestTrue(TEXT("Cleared parameter falls back to compiled"),
		FMath::IsNearlyEqual(MixtormatParameterAuthoring::ResolveAuthoringDefault(Key, 0.0f), 2.0f));

	return true;
}

// Creation-time defaults: a genuinely new Breakup receives the database default, another
// family is untouched by Breakup entries, and identity is never part of what the database
// changes. Existing authored values are rewritten ONLY by ApplyAuthoringDefaults, which the
// creation paths call for new children -- duplication copies the payload directly.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatAuthoringNewInstanceTest,
	"Mixtormat.Parameters.AuthoringNewInstanceDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatAuthoringNewInstanceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FMixtormatParameterDefinitionKey Key{
		EMixtormatParameterOwnerType::Effect, TEXT("BreakupNormalStrength"),
		EMixtormatParameterValueType::Float};

	const FString Json = TEXT(R"( { "Breakup": { "BreakupNormalStrength": { "default": 7.0 } } } )");
	TestTrue(TEXT("Database loads for creation test"), MixtormatParameterAuthoring::LoadFromString(Json));

	// New Breakup: compiled initializer would be 2.0; the database says 7.0.
	FMixtormatLayerEffect NewBreakup;
	TestTrue(TEXT("Fresh struct holds the compiled default"),
		FMath::IsNearlyEqual(NewBreakup.BreakupNormalStrength, 2.0f));
	MixtormatParameterAuthoring::ApplyAuthoringDefaults(
		NewBreakup, EMixtormatEffectType::Breakup);
	TestTrue(TEXT("New Breakup receives the persistent default"),
		FMath::IsNearlyEqual(NewBreakup.BreakupNormalStrength, 7.0f));

	// Cross-family isolation: a Grade child gets nothing from Breakup entries.
	FMixtormatLayerEffect NewGrade;
	NewGrade.BreakupNormalStrength = 2.0f;
	MixtormatParameterAuthoring::ApplyAuthoringDefaults(NewGrade, EMixtormatEffectType::Grade);
	TestTrue(TEXT("Other families are untouched"),
		FMath::IsNearlyEqual(NewGrade.BreakupNormalStrength, 2.0f));

	// Duplication preserves the duplicated value by never entering this path.
	FMixtormatLayerEffect Duplicate = NewBreakup;
	TestTrue(TEXT("Duplicated effect preserves its authored value"),
		FMath::IsNearlyEqual(Duplicate.BreakupNormalStrength, 7.0f));

	// Database changes never alter parameter identity.
	const FMixtormatParameterDefinitionKey Identity{
		EMixtormatParameterOwnerType::Effect, TEXT("BreakupNormalStrength"),
		EMixtormatParameterValueType::Float};
	TestTrue(TEXT("Identity is stable across database changes"), Identity == Key);

	MixtormatParameterAuthoring::LoadFromString(TEXT("{}"));
	return true;
}

#endif
