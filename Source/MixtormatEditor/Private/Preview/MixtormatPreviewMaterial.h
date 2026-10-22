// Copyright 2026 Hugo Beyer. All Rights Reserved.
#pragma once

class UMaterial;
class UMaterialExpressionTextureSampleParameter2D;
class UMaterialInterface;

namespace MixtormatPreview
{
	// Diagnostic materials copy displacement settings from the displayed preview material.
	void AddHeightDisplacement(
		UMaterial* Material,
		UMaterialExpressionTextureSampleParameter2D* HeightSample,
		UMaterialInterface* MasterInterface);
}
