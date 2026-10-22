// Copyright 2026 Hugo Beyer. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "UObject/SoftObjectPath.h"

class UScriptStruct;

// Private reflected codec. Decoding never loads an asset or imports arbitrary object text.
namespace MixtormatPresetJson
{
	constexpr int32 MaxArrayItems = 4096;
	constexpr int32 MaxDepth = 64;
	bool Encode(const UScriptStruct* Type, const void* Data, bool bParametersOnly,
		TSharedPtr<FJsonObject>& OutJson, FText& Error);
	// Caller supplies temporary initialized storage; failure may change that temporary only.
	bool Decode(const UScriptStruct* Type, void* Data, bool bParametersOnly,
		const TSharedPtr<FJsonObject>& Json, FText& Error);
	bool ValidateAssetClasses(const UScriptStruct* Type, const void* Data, FText& Error);
	void Visit(const UScriptStruct* Type, void* Data,
		TFunctionRef<void(const UScriptStruct*, void*)> OnStruct,
		TFunctionRef<void(const FSoftObjectPath&)> OnAsset);
}
