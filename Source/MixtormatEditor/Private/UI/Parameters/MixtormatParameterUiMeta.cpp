// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Parameters/MixtormatParameterUiMeta.h"

#include "HAL/IConsoleManager.h"
#include "Logging/LogMacros.h"
#include "Math/Range.h"

namespace
{
	// Hidden until asked for: the whole Developer surface is behind this one flag, which ships
	// only inside the editor module and therefore never reaches a packaged game.
	TAutoConsoleVariable<int32> CVarMixtormatDevParameterMeta(
		TEXT("Mixtormat.Developer.ParameterMeta"),
		0,
		TEXT("Enable Mixtormat's developer parameter surface (Parameter Info, UI range overrides) in the Inspector context menu."));

	// The Breakup pilot's ergonomic ranges, transcribed from the existing slider literals in
	// BuildBreakupControls. A family migrates by adding its rows here; its call-site literals
	// become the verified fallback and are deleted in the same change.
	struct FUiMetaRow
	{
		const TCHAR* Name;
		FMixtormatParameterUiMeta Meta;
		bool bInt = false;
	};

	const TArray<FUiMetaRow>& UiMetaRows()
	{
		static const TArray<FUiMetaRow> Rows = []()
		{
			return TArray<FUiMetaRow>({
				// Shape.
				{TEXT("BreakupScale"), {1.0f, 64.0f, 1.0f}, true},
				{TEXT("BreakupDensity"), {0.0f, 1.0f, 0.01f}},
				{TEXT("BreakupSize"), {0.001f, 1.0f, 0.005f}},
				{TEXT("BreakupStretch"), {1.0f, 4.0f, 0.01f,
					TEXT("Per-piece elongation magnitude; the shader randomizes each piece's orientation. Below 1 folds into the same >= 1 magnitude, so 1 is the floor.")}},
				{TEXT("BreakupAngularity"), {0.0f, 1.0f, 0.01f}},
				{TEXT("BreakupIrregularity"), {0.0f, 1.0f, 0.01f}},
				{TEXT("BreakupInset"), {-64.0f, 64.0f, 0.25f}},
				{TEXT("BreakupDistortion"), {0.0f, 32.0f, 0.1f}},
				{TEXT("BreakupDistortionFrequency"), {1.0f, 16.0f, 1.0f}, true},
				// Structure.
				{TEXT("BreakupRelief"), {-0.5f, 0.5f, 0.0025f}},
				{TEXT("BreakupThicknessVariation"), {0.0f, 1.0f, 0.01f}},
				{TEXT("BreakupGapWidth"), {0.0f, 64.0f, 0.1f}},
				{TEXT("BreakupGapDepth"), {-0.5f, 0.5f, 0.001f}},
				{TEXT("BreakupGapVariation"), {0.0f, 1.0f, 0.01f}},
				{TEXT("BreakupFold"), {-0.5f, 0.5f, 0.0025f}},
				{TEXT("BreakupFoldWidth"), {0.001f, 64.0f, 0.25f}},
				{TEXT("BreakupCrease"), {-0.5f, 0.5f, 0.001f}},
				{TEXT("BreakupCreaseWidth"), {0.001f, 32.0f, 0.05f}},
				{TEXT("BreakupPush"), {-128.0f, 128.0f, 0.25f}},
				{TEXT("BreakupPushWidth"), {0.001f, 64.0f, 0.5f}},
				{TEXT("BreakupPushRelief"), {0.0f, 0.5f, 0.001f}},
				{TEXT("BreakupSizeVariation"), {0.0f, 0.75f, 0.01f}},
				{TEXT("BreakupSmoothness"), {0.0f, 1.0f, 0.01f}},
				// Variation.
				{TEXT("BreakupDetail"), {0.0f, 1.0f, 0.01f}},
				{TEXT("BreakupVariation"), {0.0f, 1.0f, 0.01f}},
				// Shading / output.
				{TEXT("BreakupAmount"), {0.0f, 1.0f, 0.01f}},
				{TEXT("BreakupRoughnessAmount"), {-1.0f, 1.0f, 0.01f,
					TEXT("Signed roughness offset; the RAM channel saturates after the offset, so magnitudes past 1 keep pushing further but read the same.")}},
				{TEXT("BreakupNormalStrength"), {0.0f, 4.0f, 0.05f,
					TEXT("Feeds the shade-pass gradient directly -- no /8 normalization, unlike the peel-family normal paths.")}},
				{TEXT("BreakupNormalSharpness"), {0.0f, 1.0f, 0.01f}},
				{TEXT("BreakupAOAmount"), {0.0f, 1.0f, 0.01f}},
				{TEXT("BreakupAORadius"), {1.0f, 32.0f, 0.25f}},
				// Placement.
				{TEXT("BreakupMaskTiling"), {1.0f, 16.0f, 1.0f}, true},
				{TEXT("BreakupSeed"), {0.0f, 9999.0f, 1.0f}, true},
			});
		}();

		return Rows;
	}

	FMixtormatParameterDefinitionKey KeyOf(const FUiMetaRow& Row)
	{
		return {
			EMixtormatParameterOwnerType::Effect,
			FName(Row.Name),
			Row.bInt ? EMixtormatParameterValueType::Int : EMixtormatParameterValueType::Float
		};
	}

	const TMap<FMixtormatParameterDefinitionKey, const FMixtormatParameterUiMeta*>& UiMetaIndex()
	{
		static const TMap<FMixtormatParameterDefinitionKey, const FMixtormatParameterUiMeta*> Map = []()
		{
			TMap<FMixtormatParameterDefinitionKey, const FMixtormatParameterUiMeta*> Built;
			for (const FUiMetaRow& Row : UiMetaRows())
			{
				Built.Add(KeyOf(Row), &Row.Meta);
			}
			return Built;
		}();

		return Map;
	}

	// Session-lifetime dev overrides. Deliberately a function-local static in this translation
	// unit: nothing outside can reach it except through the three functions below, none of
	// which touches disk.
	TMap<FMixtormatParameterDefinitionKey, FFloatRange>& DevUiRangeOverrides()
	{
		static TMap<FMixtormatParameterDefinitionKey, FFloatRange> Map;
		return Map;
	}

	// One warning per parameter, ever. The comparison runs from slider attributes (per paint),
	// so the warned-set is what makes this cheap.
	TSet<FMixtormatParameterDefinitionKey>& WarnedKeys()
	{
		static TSet<FMixtormatParameterDefinitionKey> Set;
		return Set;
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

	const FMixtormatParameterUiMeta* TryGetUiMeta(const FMixtormatParameterDefinitionKey& Key)
	{
		return UiMetaIndex().FindRef(Key);
	}

	float ResolveUiBound(
		const FMixtormatParameterDefinitionKey& Key,
		const float Fallback,
		const bool bMax)
	{
		if (const FFloatRange* Override = DevUiRangeOverrides().Find(Key))
		{
			const TOptional<float> Bound = bMax ? Override->GetUpperBoundValue() : Override->GetLowerBoundValue();
			if (Bound.IsSet())
			{
				return Bound.GetValue();
			}
		}
		if (const FMixtormatParameterUiMeta* Meta = TryGetUiMeta(Key))
		{
			return bMax ? Meta->UiMax : Meta->UiMin;
		}
		return Fallback;
	}

	float ResolveUiSnap(const FMixtormatParameterDefinitionKey& Key, const float Fallback)
	{
		if (const FMixtormatParameterUiMeta* Meta = TryGetUiMeta(Key))
		{
			return Meta->Snap;
		}
		return Fallback;
	}

	bool HasUiRangeOverride(const FMixtormatParameterDefinitionKey& Key)
	{
		return DevUiRangeOverrides().Contains(Key);
	}

	void SetUiRangeOverride(
		const FMixtormatParameterDefinitionKey& Key,
		const float Min,
		const float Max)
	{
		DevUiRangeOverrides().Add(Key, FFloatRange(Min, FMath::Max(Min, Max)));
	}

	void ClearUiRangeOverride(const FMixtormatParameterDefinitionKey& Key)
	{
		DevUiRangeOverrides().Remove(Key);
	}

	void ReportLiteralMismatch(
		const FMixtormatParameterDefinitionKey& Key,
		const float LiteralMin,
		const float LiteralMax,
		const float LiteralSnap)
	{
		const FMixtormatParameterUiMeta* Meta = TryGetUiMeta(Key);
		if (!Meta || WarnedKeys().Contains(Key))
		{
			return;
		}
		const bool bMismatch =
			!FMath::IsNearlyEqual(Meta->UiMin, LiteralMin)
			|| !FMath::IsNearlyEqual(Meta->UiMax, LiteralMax)
			|| !FMath::IsNearlyEqual(Meta->Snap, LiteralSnap, 1.0e-6f);
		if (bMismatch)
		{
			WarnedKeys().Add(Key);
			UE_LOG(LogTemp, Warning,
				TEXT("Mixtormat dev: UI metadata and slider literals disagree for %s.%s "
					"(meta %.4g..%.4g snap %.4g vs literals %.4g..%.4g snap %.4g). "
					"The metadata wins; update the call site or the table."),
				*StaticEnum<EMixtormatParameterOwnerType>()->GetNameByValue(static_cast<int64>(Key.Owner)).ToString(),
				*Key.Parameter.ToString(),
				Meta->UiMin, Meta->UiMax, Meta->Snap,
				LiteralMin, LiteralMax, LiteralSnap);
		}
	}
}
