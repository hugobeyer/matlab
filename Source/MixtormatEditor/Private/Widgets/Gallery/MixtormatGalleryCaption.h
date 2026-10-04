// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatStyle.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace MixtormatGallery
{
	// The caption row under a gallery tile.
	//
	// The name is revealed on hover rather than printed under every swatch: a permanent strip costs a
	// line of readable text on every tile in the grid, and at these sizes most of those names are
	// illegible anyway -- the full name is what the hover is for.
	//
	// Only the TEXT is visibility-driven. The row still reserves its height at rest, so revealing a
	// name cannot reflow the wrap box and shuffle every tile after it. `TextVisibility` defaults to
	// visible for any caller that has no hover state of its own.
	inline TSharedRef<SWidget> WithCaption(
		const TSharedRef<SWidget>& Content,
		const FText& Name,
		const TAttribute<EVisibility>& TextVisibility = EVisibility::HitTestInvisible)
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
					.Visibility(TextVisibility)
					.Text(Name)
				]
			];
	}
}