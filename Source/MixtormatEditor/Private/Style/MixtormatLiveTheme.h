// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Style/MixtormatCompositing.h"

// Three registry types, one per editor control kind: a number gets a spinbox, a bool gets a
// toggle, a choice gets a dropdown, and a colour gets a swatch. The panel renders all four from
// the same row shell, so the *type* lives in the registry rather than being inferred from a
// category string at runtime.
struct FMixtormatThemeNumber
{
	FName Name;
	FString Category;
	float* Value;
	float Default;
	float Minimum;
	float Maximum;
	// Editor metadata. Step and Precision are authored per token: a 1px geometry nudge, a 0.01
	// opacity nudge and a 0.05 falloff-power nudge are three different gestures, and deriving them
	// from the category string made every control move in the same wrong increments.
	float Step = 1.0f;
	int32 Precision = 2;
	bool bExposeInUI = true;
};

// A genuinely binary value. The weight switches (`*Bold`) and the on/off paint flags used to be
// numbers on a 0..1 range, which meant a weight was edited by dragging a slider and never read as
// the switch it is.
struct FMixtormatThemeBool
{
	FName Name;
	FString Category;
	bool* Value;
	bool Default;
	bool bExposeInUI = true;
};

// A value from a fixed, authored set. The stored value is an index into Options rather than a
// string, so a paint site reads an int32 and never compares text on a hot path; Options supplies
// both the dropdown labels and the persistence mapping.
struct FMixtormatThemeChoice
{
	FName Name;
	FString Category;
	int32* Value;
	TArray<FString> Options;
	int32 Default = 0;
	bool bExposeInUI = true;

	// Exact, case-sensitive lookup: option labels are authored strings and a near-miss must not
	// silently resolve to a different blend mode.
	int32 FindOption(const FString& Option) const
	{
		return Options.IndexOfByPredicate([&Option](const FString& Candidate)
		{
			return Candidate.Equals(Option, ESearchCase::CaseSensitive);
		});
	}
};

struct FMixtormatThemeColor
{
	FName Name;
	FLinearColor Default;
	FString Category;
	bool bExposeInUI = true;
};

// Category and exposure are UI metadata only; hidden legacy entries remain valid persisted keys.
// The registry is shared by the panel, validation, reset and persistence.
class FMixtormatLiveTheme final
{
public:

	static void Initialize();
	static const TArray<FMixtormatThemeNumber>& Numbers();
	static const TArray<FMixtormatThemeBool>& Booleans();
	static const TArray<FMixtormatThemeChoice>& Choices();
	static const TArray<FMixtormatThemeColor>& Colors();
	static const TArray<FString>& Categories();
	static FLinearColor ResolveColor(FName Name, const FLinearColor& Default);
	static bool SetNumber(FName Name, float Value);
	static bool SetBool(FName Name, bool Value);
	static bool SetChoice(FName Name, int32 Value);
	static bool SetColor(FName Name, const FLinearColor& Value);
	static void Reset();
	static FString Serialize();
	// Validate the entire document before changing any live value.
	static bool Deserialize(const FString& Text, FString& Error);
	static FString SavePath();
	static bool Save(FString& Error);
	static bool Load(FString& Error);
};
