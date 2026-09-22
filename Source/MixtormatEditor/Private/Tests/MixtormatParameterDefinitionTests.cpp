// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "MixtormatMaterial.h"
#include "MixtormatParameterDefinition.h"

#if WITH_DEV_AUTOMATION_TESTS

// The contract's sanitization contract, pinned: non-finite falls back to the REFLECTED CDO
// default (no second copy of the number exists anywhere), hard bounds clamp where set, and
// a parameter without a contract passes through untouched.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatParameterSanitizeTest,
	"Mixtormat.Parameters.SanitizeContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatParameterSanitizeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	using ET = EMixtormatParameterOwnerType;
	const auto Name = [](const TCHAR* N) { return FName(N); };

	// BreakupNormalStrength: HardMin 0, no HardMax, CDO default 2. A typed value past the UI
	// range must survive untouched.
	TestEqual(TEXT("Finite value passes"),
		MixtormatParameterContracts::SanitizeFloat(ET::Effect, Name(TEXT("BreakupNormalStrength")), 8.0f),
		8.0f);
	TestEqual(TEXT("NaN falls back to the reflected CDO default"),
		MixtormatParameterContracts::SanitizeFloat(ET::Effect, Name(TEXT("BreakupNormalStrength")),
			std::numeric_limits<float>::quiet_NaN()),
		2.0f);
	TestEqual(TEXT("HardMin clamps"),
		MixtormatParameterContracts::SanitizeFloat(ET::Effect, Name(TEXT("BreakupNormalStrength")), -4.0f),
		0.0f);

	// A hard-bounded int: 1..16.
	TestEqual(TEXT("Int clamp low"),
		MixtormatParameterContracts::SanitizeInt32(ET::Effect, Name(TEXT("BreakupDistortionFrequency")), 0),
		1);
	TestEqual(TEXT("Int clamp high"),
		MixtormatParameterContracts::SanitizeInt32(ET::Effect, Name(TEXT("BreakupDistortionFrequency")), 99),
		16);
	TestEqual(TEXT("Int in range passes"),
		MixtormatParameterContracts::SanitizeInt32(ET::Effect, Name(TEXT("BreakupDistortionFrequency")), 5),
		5);

	// No contract: pass-through. ErosionAmount deliberately has no bounds.
	TestEqual(TEXT("Contract-less parameter passes through"),
		MixtormatParameterContracts::SanitizeFloat(ET::Effect, Name(TEXT("ErosionAmount")), 123.0f),
		123.0f);

	// DefaultFloat reads the CDO through reflection -- the struct initializer is the only
	// copy of the number.
	TestEqual(TEXT("DefaultFloat reflects the CDO"),
		MixtormatParameterContracts::DefaultFloat(ET::Effect, Name(TEXT("BreakupNormalStrength")), 0.0f),
		2.0f);

	return true;
}

#endif
