// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Parameters/MixtormatParameterUiMeta.h"

#include "HAL/IConsoleManager.h"
#include "Logging/LogMacros.h"
#include "Math/Range.h"
#include "Misc/AssertionMacros.h"
#include "Misc/Parse.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

namespace
{
	// Hidden until asked for: the whole Developer surface is behind this one flag, which
	// ships only inside the editor module and never reaches a packaged game.
	TAutoConsoleVariable<int32> CVarMixtormatDevParameterMeta(
		TEXT("Mixtormat.Developer.ParameterMeta"),
		0,
		TEXT("Enable Mixtormat's developer parameter surface (Parameter Info, authoring editing) in the Inspector context menu."));

	// Session-lifetime dev UI range overrides. Deliberately a function-local static: nothing
	// outside can reach it except through the three functions below, none of which touches
	// disk.
	TMap<FMixtormatParameterDefinitionKey, FFloatRange>& DevUiRangeOverrides()
	{
		static TMap<FMixtormatParameterDefinitionKey, FFloatRange> Map;
		return Map;
	}

	// One warning per parameter, ever; the comparison runs from slider attributes per paint.
	TSet<FMixtormatParameterDefinitionKey>& WarnedKeys()
	{
		static TSet<FMixtormatParameterDefinitionKey> Set;
		return Set;
	}

	// The reflected UI facts for one parameter, parsed once and cached per key. Everything
	// comes from the FProperty: UIMin/UIMax/Delta meta strings and the CDO initializer.
	struct FReflectedUi
	{
		bool bPropertyFound = false;
		bool bUiMetaFound = false;
		float UiMin = 0.0f;
		float UiMax = 1.0f;
		float Snap = 0.0f;
		float Default = 0.0f;
	};

	const FReflectedUi& GetReflectedUi(const FMixtormatParameterDefinitionKey& Key)
	{
		static TMap<FMixtormatParameterDefinitionKey, FReflectedUi> Cache;

		if (const FReflectedUi* Cached = Cache.Find(Key))
		{
			return *Cached;
		}

		FReflectedUi Resolved;
		// Only effect parameters are UI-resolved today; extend per owner struct as families
		// beyond FMixtormatLayerEffect migrate.
		if (Key.Owner == EMixtormatParameterOwnerType::Effect)
		{
			static const FMixtormatLayerEffect CDODefaults;
			const UScriptStruct* Struct = FMixtormatLayerEffect::StaticStruct();
			if (const FProperty* Property = Struct->FindPropertyByName(Key.Parameter))
			{
				Resolved.bPropertyFound = true;

				if (const FFloatProperty* Float = CastField<FFloatProperty>(Property))
				{
					Resolved.Default = *Float->ContainerPtrToValuePtr<float>(&CDODefaults);
				}
				else if (const FIntProperty* Int = CastField<FIntProperty>(Property))
				{
					Resolved.Default = static_cast<float>(*Int->ContainerPtrToValuePtr<int32>(&CDODefaults));
				}

				const auto ReadMeta = [&Property](const TCHAR* Field, float& Out) -> bool
				{
					const FString Text = Property->GetMetaData(Field);
					float Parsed = 0.0f;
					if (!Text.IsEmpty() && LexTryParseString(Parsed, *Text))
					{
						Out = Parsed;
						return true;
					}
					return false;
				};

				const bool bMin = ReadMeta(TEXT("UIMin"), Resolved.UiMin);
				const bool bMax = ReadMeta(TEXT("UIMax"), Resolved.UiMax);
				const bool bSnap = ReadMeta(TEXT("Delta"), Resolved.Snap);
				// A ClampMin with no UIMin still bounds the scrub from below; treat it as the
				// ergonomic floor since the typed value was never clamped by it anyway.
				if (!bMin)
				{
					float ClampMin = 0.0f;
					if (ReadMeta(TEXT("ClampMin"), ClampMin))
					{
						Resolved.UiMin = ClampMin;
						Resolved.bUiMetaFound = true;
					}
				}
				Resolved.bUiMetaFound |= bMin || bMax || bSnap;
			}
		}

		FReflectedUi& Stored = Cache.Add(Key, Resolved);
		return Stored;
	}
}

namespace MixtormatParameterUi
{
	TOptional<FMixtormatParameterDefinitionKey> DefinitionKeyOf(
		const FMixtormatParameterAddress& Address)
	{
		if (Address.Parameter.IsNone())
		{
			return {};
		}
		return FMixtormatParameterDefinitionKey{Address.Owner, Address.Parameter, Address.ValueType};
	}

	bool IsDeveloperMetaEnabled()
	{
		return CVarMixtormatDevParameterMeta.GetValueOnGameThread() != 0;
	}

	FProperty* TryFindNumericProperty(const FMixtormatParameterDefinitionKey& Key)
	{
		if (Key.Owner != EMixtormatParameterOwnerType::Effect || Key.Parameter.IsNone())
		{
			return nullptr;
		}
		const FProperty* Property =
			FMixtormatLayerEffect::StaticStruct()->FindPropertyByName(Key.Parameter);
		return Property && (CastField<FFloatProperty>(Property) || CastField<FIntProperty>(Property))
			? const_cast<FProperty*>(Property)
			: nullptr;
	}

	bool TryResolveUi(const FMixtormatParameterDefinitionKey& Key, FMixtormatParameterUiResolution& Out)
	{
		const FReflectedUi& Reflected = GetReflectedUi(Key);
		if (!Reflected.bUiMetaFound)
		{
			return false;
		}
		Out.UiMin = Reflected.UiMin;
		Out.UiMax = Reflected.UiMax;
		Out.Snap = Reflected.Snap;
		Out.Default = Reflected.Default;
		return true;
	}

	bool HasUiRangeOverride(const FMixtormatParameterDefinitionKey& Key)
	{
		return DevUiRangeOverrides().Contains(Key);
	}

	void SetUiRangeOverride(const FMixtormatParameterDefinitionKey& Key, const float Min, const float Max)
	{
		DevUiRangeOverrides().Add(Key, FFloatRange(Min, FMath::Max(Min, Max)));
	}

	void ClearUiRangeOverride(const FMixtormatParameterDefinitionKey& Key)
	{
		DevUiRangeOverrides().Remove(Key);
	}

	float ResolveUiBound(const FMixtormatParameterDefinitionKey& Key, const float Fallback, const bool bMax)
	{
		if (const FFloatRange* Override = DevUiRangeOverrides().Find(Key))
		{
			const TOptional<float> Bound = bMax ? Override->GetUpperBoundValue() : Override->GetLowerBoundValue();
			if (Bound.IsSet())
			{
				return Bound.GetValue();
			}
		}
		const FReflectedUi& Reflected = GetReflectedUi(Key);
		if (Reflected.bUiMetaFound)
		{
			return bMax ? Reflected.UiMax : Reflected.UiMin;
		}
		return Fallback;
	}

	float ResolveUiSnap(const FMixtormatParameterDefinitionKey& Key, const float Fallback)
	{
		const FReflectedUi& Reflected = GetReflectedUi(Key);
		return Reflected.bUiMetaFound ? Reflected.Snap : Fallback;
	}

	float ResolveUiDefault(const FMixtormatParameterDefinitionKey& Key, const float Fallback)
	{
		const FReflectedUi& Reflected = GetReflectedUi(Key);
		return Reflected.bPropertyFound ? Reflected.Default : Fallback;
	}

	void ReportLiteralMismatch(
		const FMixtormatParameterDefinitionKey& Key,
		const float LiteralMin,
		const float LiteralMax,
		const float LiteralSnap)
	{
		const FReflectedUi& Reflected = GetReflectedUi(Key);
		if (!Reflected.bUiMetaFound || WarnedKeys().Contains(Key))
		{
			return;
		}
		const bool bMismatch =
			!FMath::IsNearlyEqual(Reflected.UiMin, LiteralMin)
			|| !FMath::IsNearlyEqual(Reflected.UiMax, LiteralMax)
			|| !FMath::IsNearlyEqual(Reflected.Snap, LiteralSnap, 1.0e-6f);
		if (bMismatch)
		{
			WarnedKeys().Add(Key);
			UE_LOG(LogTemp, Warning,
				TEXT("Mixtormat dev: UPROPERTY meta and slider literals disagree for %s "
					"(meta %.4g..%.4g snap %.4g vs literals %.4g..%.4g snap %.4g). "
					"The meta wins; update the call site or the annotation."),
				*Key.Parameter.ToString(),
				Reflected.UiMin, Reflected.UiMax, Reflected.Snap,
				LiteralMin, LiteralMax, LiteralSnap);
		}
	}
}
