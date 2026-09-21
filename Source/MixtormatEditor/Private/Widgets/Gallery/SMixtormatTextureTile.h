// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

// Moved out of SMixtormatInternal.h unchanged.

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SCompoundWidget.h"

class SMixtormatTextureTile final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatTextureTile) {}
		SLATE_ARGUMENT(UObject*, Texture)
		SLATE_ARGUMENT(FVector2D, ImageSize)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Brush.SetResourceObject(InArgs._Texture);
		Brush.SetImageSize(InArgs._ImageSize);
		Brush.DrawAs = ESlateBrushDrawType::Image;
		Brush.Tiling = ESlateBrushTileType::NoTile;
		ChildSlot[SNew(SImage).Image(&Brush)];
	}

private:
	FSlateBrush Brush;
};
