// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatColorRamp.h"
#include "MixtormatScalarRamp.h"

// Ramp payloads retain their authored domain. Applying to another domain requires an explicit remap.
namespace MixtormatPresetStore
{
	enum class ERampKind : uint8 { Scalar, Color };

	struct FRampPreset
	{
		FGuid Id;
		FString Name;
		ERampKind Kind = ERampKind::Scalar;
		float DomainMin = 0.0f;
		float DomainMax = 1.0f;
		FMixtormatScalarRamp Scalar;
		FMixtormatColorRamp Color;
	};

	bool ListRamps(ERampKind Kind, TArray<FRampPreset>& OutPresets, FText& OutError);
	bool SaveRamp(const FRampPreset& Preset, FText& OutError);
	bool ImportRamp(const FString& Filename, FRampPreset& OutPreset, FText& OutError);
	bool ExportRamp(const FString& Filename, const FRampPreset& Preset, FText& OutError);
}
