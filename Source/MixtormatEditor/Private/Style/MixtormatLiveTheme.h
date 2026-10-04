// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FMixtormatThemeNumber
{
	FName Name;
	FString Category;
	float* Value;
	float Default;
	float Minimum;
	float Maximum;
	bool bExposeInUI = true;
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
	static const TArray<FMixtormatThemeColor>& Colors();
	static const TArray<FString>& Categories();
	static FLinearColor ResolveColor(FName Name, const FLinearColor& Default);
	static bool SetNumber(FName Name, float Value);
	static bool SetColor(FName Name, const FLinearColor& Value);
	static void Reset();
	static FString Serialize();
	// Validate the entire document before changing any live value.
	static bool Deserialize(const FString& Text, FString& Error);
	static FString SavePath();
	static bool Save(FString& Error);
	static bool Load(FString& Error);
};
