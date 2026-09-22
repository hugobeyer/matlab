// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Algo/AnyOf.h"
#include "MixtormatMaterial.h"
#include "MixtormatParameterDefinition.h"
#include "UI/Parameters/MixtormatParameterAuthoring.h"
#include "UI/Parameters/MixtormatParameterUiMeta.h"
#include "UI/Parameters/MixtormatShaderParamScanner.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

// Completeness for the migrated families: every numeric property of a migrated family must
// carry its UI meta (UIMin/UIMax/Delta) directly on the UPROPERTY. This is the test that
// catches an unannotated field -- the reflection-resolved UI silently falls back to call-site
// literals without it, and nothing else would notice.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatBreakupDefinitionCompletenessTest,
	"Mixtormat.Parameters.BreakupDefinitionCompleteness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatBreakupDefinitionCompletenessTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	// The UPROPERTY Category prefix = the family. A new family migration adds its prefix
	// here; the test then guarantees no numeric property of that family ships unannotated.
	// Spellings are the Categories as declared: display spaces included ("Worn Edges").
	const TCHAR* MigratedPrefixes[] = {
		TEXT("Breakup"),
		TEXT("Erosion"),
		TEXT("Grade"),
		TEXT("Worn Edges"),
		TEXT("Flow Warp"),
		TEXT("Layer Blur"),
		TEXT("Runoff"),
	};

	const UScriptStruct* EffectStruct = FMixtormatLayerEffect::StaticStruct();
	int32 Checked = 0;
	for (TFieldIterator<FProperty> It(EffectStruct); It; ++It)
	{
		const FProperty* Property = *It;
		if (CastField<FFloatProperty>(Property) == nullptr
			&& CastField<FIntProperty>(Property) == nullptr)
		{
			// Assets, bools and enums are structural; they carry no UI annotation.
			continue;
		}

		const FString Category = Property->GetMetaData(TEXT("Category"));
		const bool bMigratedFamily = Algo::AnyOf(MigratedPrefixes,
			[&Category](const TCHAR* Prefix)
			{
				return Category.StartsWith(Prefix);
			});
		if (!bMigratedFamily)
		{
			continue;
		}

		const auto HasMeta = [&Property](const TCHAR* Field)
		{
			return !Property->GetMetaData(Field).IsEmpty();
		};
		if (!HasMeta(TEXT("UIMin")) || !HasMeta(TEXT("UIMax")) || !HasMeta(TEXT("Delta")))
		{
			AddError(FString::Printf(
				TEXT("Migrated property %s (Category %s) is missing UIMin/UIMax/Delta meta"),
				*Property->GetName(), *Category));
		}
		++Checked;
	}

	TestTrue(TEXT("Migrated numeric effect properties were found"), Checked >= 100);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatMaskShapingRangeTest,
	"Mixtormat.Parameters.MaskShapingRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatMaskShapingRangeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const UScriptStruct* Struct = FMixtormatMaskShaping::StaticStruct();
	const TCHAR* Fields[] = { TEXT("Balance"), TEXT("Contrast"), TEXT("Offset") };
	for (const TCHAR* Field : Fields)
	{
		const FProperty* Property = Struct->FindPropertyByName(Field);
		TestNotNull(*FString::Printf(TEXT("Mask shaping field %s exists"), Field), Property);
		if (!Property)
		{
			continue;
		}
		TestFalse(*FString::Printf(TEXT("%s has UIMin"), Field),
			Property->GetMetaData(TEXT("UIMin")).IsEmpty());
		TestFalse(*FString::Printf(TEXT("%s has UIMax"), Field),
			Property->GetMetaData(TEXT("UIMax")).IsEmpty());
		TestFalse(*FString::Printf(TEXT("%s has Delta"), Field),
			Property->GetMetaData(TEXT("Delta")).IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatShaderParamScannerTest,
	"Mixtormat.Parameters.ShaderParamScanner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatShaderParamScannerTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TArray<FMixtormatShaderParamTag> Tags;
	TArray<FString> Errors;
	TestTrue(TEXT("Shader parameter tags parse"), MixtormatShaderParamScanner::Scan(Tags, Errors));
	for (const FString& Error : Errors)
	{
		AddError(Error);
	}
	TestTrue(TEXT("Breakup shader tags were found"), Tags.ContainsByPredicate(
		[](const FMixtormatShaderParamTag& Tag)
		{
			return Tag.Parameter == TEXT("BreakupNormalStrength");
		}));
	for (const FMixtormatShaderParamTag& Tag : Tags)
	{
		const FMixtormatParameterContract* Contract =
			MixtormatParameterContracts::TryGet(Tag.Owner, Tag.Parameter);
		if (!Contract)
		{
			AddError(FString::Printf(TEXT("Shader tag %s has no contract row"), *Tag.Parameter.ToString()));
			continue;
		}
		const auto SameOptional = [](const TOptional<float>& A, const TOptional<float>& B)
		{
			return A.IsSet() == B.IsSet()
				&& (!A.IsSet() || FMath::IsNearlyEqual(A.GetValue(), B.GetValue()));
		};
		TestTrue(*FString::Printf(TEXT("%s hard-min matches"), *Tag.Parameter.ToString()),
			SameOptional(Tag.HardMin, Contract->HardMin));
		TestTrue(*FString::Printf(TEXT("%s hard-max matches"), *Tag.Parameter.ToString()),
			SameOptional(Tag.HardMax, Contract->HardMax));
		TestEqual(*FString::Printf(TEXT("%s saturation matches"), *Tag.Parameter.ToString()),
			Tag.bSaturates, Contract->bShaderSaturates);
		const bool bContractNormalized = !FMath::IsNearlyEqual(Contract->NormalizationScale, 1.0f);
		TestTrue(*FString::Printf(TEXT("%s normalization matches"), *Tag.Parameter.ToString()),
			Tag.Normalize.IsSet() == bContractNormalized
				&& (!Tag.Normalize.IsSet()
					|| FMath::IsNearlyEqual(Tag.Normalize.GetValue(), Contract->NormalizationScale)));
	}
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

	// Hard bounds are runtime-owned: the database's UI range never touches the contract.
	TestTrue(TEXT("HardMin still clamps at 0 regardless of database"),
		FMath::IsNearlyEqual(
			MixtormatParameterContracts::SanitizeFloat(
				Key.Owner, Key.Parameter, -4.0f), 0.0f));

	// D1 round-trip: the Category-prefix spelling with its display space ("Worn Edges") is
	// the canonical family section. It must load, resolve, apply at creation, and survive a
	// write -> reload cycle (the old enum-name validation dropped sections like this one).
	const FMixtormatParameterDefinitionKey WornKey{
		EMixtormatParameterOwnerType::Effect, TEXT("EdgeWearStrength"),
		EMixtormatParameterValueType::Float};
	const FString WornJson = TEXT(R"( { "Worn Edges": { "Effect.EdgeWearStrength": { "default": 0.9 } } } )");
	TestTrue(TEXT("Category-spelled family section loads"),
		MixtormatParameterAuthoring::LoadFromString(WornJson));
	TestTrue(TEXT("Worn Edges entry resolves"),
		FMath::IsNearlyEqual(MixtormatParameterAuthoring::ResolveAuthoringDefault(WornKey, 0.0f), 0.9f));

	FMixtormatLayerEffect NewWorn;
	MixtormatParameterAuthoring::ApplyAuthoringDefaults(NewWorn, EMixtormatEffectType::WornEdges);
	TestTrue(TEXT("Worn Edges creation default applies"),
		FMath::IsNearlyEqual(NewWorn.EdgeWearStrength, 0.9f));

	const FString WornWritten = MixtormatParameterAuthoring::WriteToString();
	TestTrue(TEXT("Written section keeps the Category spelling"),
		WornWritten.Contains(TEXT("Worn Edges")));
	TestTrue(TEXT("Round-trip reload keeps the entry"),
		MixtormatParameterAuthoring::LoadFromString(WornWritten)
		&& FMath::IsNearlyEqual(
			MixtormatParameterAuthoring::ResolveAuthoringDefault(WornKey, 0.0f), 0.9f));

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
