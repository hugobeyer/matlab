// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatStyle.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace MixtormatGallery
{
	inline TSharedRef<SWidget> WithCaption(const TSharedRef<SWidget>& Content, const FText& Name)
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[Content]
			+ SVerticalBox::Slot().AutoHeight().Padding(MixtormatTokens::TileTextInset)
			[
				// Only the swatch contributes width; long captions cannot enlarge a gallery tile.
				SNew(SBox)
				.WidthOverride(0.0f)
				.HeightOverride(MixtormatTokens::TileNameStripHeight)
				.Visibility(EVisibility::HitTestInvisible)
				.Clipping(EWidgetClipping::ClipToBounds)
				[
					SNew(STextBlock)
					.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.Tile.Name")))
					.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					.Text(Name)
				]
			];
	}
}
