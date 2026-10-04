// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Primitives/MixtormatWell.h"

#include "Style/MixtormatRecipes.h"
#include "Style/MixtormatThemeStore.h"
#include "UI/Primitives/MixtormatSurfacePainter.h"

namespace MixtormatWell
{
	namespace
	{
		// The recipe this call is painting, chosen from the two flags the caller has.
		//
		// The state is a parameter rather than a second recipe (§16). Rest and Hover share their
		// ground, their recess and their geometry; only the border's four opacity numbers and the
		// hover lift differ, which is what the prototype actually authors separately.
		Mixtormat::FMixtormatSurfaceRecipe BuildRecipe(const FParams& Params)
		{
			// Global scope, like FMixtormatStyle -- not inside namespace Mixtormat.
			const Mixtormat::FMixtormatTheme& Theme = FMixtormatThemeStore::GetTheme();

			if (Params.bFlat)
			{
				return Mixtormat::MakeFlatWellRecipe(Theme);
			}

			return Mixtormat::MakeWellRecipe(
				Theme,
				Params.bHovered ? Mixtormat::EMixtormatWellState::Hover : Mixtormat::EMixtormatWellState::Rest);
		}

		// The body's composited colours.
		//
		// Recomputed rather than carried from PaintBackground: the two are called from different
		// places in the widget's paint, and threading samples between them would mean changing both
		// call sites for a handful of blends on pure numbers -- cheaper than the draw call that
		// follows them.
		Mixtormat::FMixtormatSurfaceSamples CompositeBody(const Mixtormat::FMixtormatSurfaceRecipe& Recipe)
		{
			Mixtormat::FMixtormatSurfaceSamples Samples;
			Mixtormat::CompositeSurface(
				Recipe,
				FMixtormatThemeStore::GetResolved().Palette,
				Mixtormat::FMixtormatStateModifier(),
				Samples);
			return Samples;
		}
	}

	void PaintBackground(
		FSlateWindowElementList& OutDrawElements,
		const int32 LayerId,
		const FGeometry& Geometry,
		const FVector2f& Size,
		const FParams& Params)
	{
		// Kept in the signature because both callers already pass it and it reads as the surface's
		// extent at the call site. The painter derives the same value from the geometry.
		(void)Size;

		const Mixtormat::FMixtormatSurfaceRecipe Recipe = BuildRecipe(Params);

		// Body only. The border is painted by PaintBorder, on top of whatever the caller draws in
		// between -- the slider's fill -- which is the entire reason this is two functions.
		Mixtormat::FMixtormatSurfacePainter::PaintBody(
			OutDrawElements, LayerId, Geometry, Recipe, CompositeBody(Recipe));
	}

	void PaintBorder(
		FSlateWindowElementList& OutDrawElements,
		const int32 LayerId,
		const FGeometry& Geometry,
		const FVector2f& Size,
		const FParams& Params)
	{
		(void)Size;

		if (Params.bFlat)
		{
			return;
		}

		const Mixtormat::FMixtormatSurfaceRecipe Recipe = BuildRecipe(Params);

		Mixtormat::FMixtormatSurfacePainter::PaintBorders(
			OutDrawElements,
			LayerId,
			Geometry,
			Recipe,
			FMixtormatThemeStore::GetResolved().Palette,
			FWidgetStyle(),
			CompositeBody(Recipe));
	}
}