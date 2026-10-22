// Copyright 2026 Hugo Beyer. All Rights Reserved.
#pragma once
#include "Services/MixtormatPresetStore.h"

namespace MixtormatRecipeService
{
	bool Capture(const TArray<FMixtormatLayer>& Layers, const TArray<FMixtormatLayerGroup>& Groups,
		const FGuid& GroupId, MixtormatPresetStore::FRecipePreset& OutRecipe, FText& Error);
	// Strict closure validation: never repairs, materializes or drops a dependency.
	bool Validate(const MixtormatPresetStore::FRecipePreset& Recipe, FText& Error);
	bool PrepareInsertion(const MixtormatPresetStore::FRecipePreset& Recipe, int32 LayerOffset,
		MixtormatPresetStore::FRecipePreset& OutCopy, FText& Error);
}
