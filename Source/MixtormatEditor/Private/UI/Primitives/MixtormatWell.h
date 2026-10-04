// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rendering/DrawElements.h"

// The shared control well: the recess a slider trough, a dropdown chip and a toggle all sit in.
//
// It is no longer built here. The recipe comes from MixtormatRecipes and the painting from
// MixtormatSurfacePainter, so this file is only the two-phase shape a widget needs:
//
//   PaintBackground  the ground and the black vertical recess (0.64 -> 0.13, Multiply)
//   PaintBorder      the hairline outline, whose alpha falls top to bottom
//
// The split exists because the slider paints its fill between them. The fill has to sit under the
// rim, and the recess has to sit under the fill, so "one call that does everything" cannot express
// the order.
//
// It is deliberately still the only place the well is built. The slider, the chip and the toggle each
// used to assemble their own version, which is why they drifted apart: the slider had a border, the
// chip had a border and no recess, and the toggle had neither.
namespace MixtormatWell
{
	// What the caller is telling the painter about itself. Only the hover flag exists because that
	// is the only state the well's own paint changes on; enabled, editing and disabled are the
	// caller's business, since each of those also changes the content drawn inside the well.
	struct FParams
	{
		// Lifts the recess and brightens the border to the hover set.
		bool bHovered = false;
		// Paints the flat ground with no recess and no border, for a control that is mid-edit and
		// needs the field itself to read.
		bool bFlat = false;
	};

	// Paint the well's background: ground, then the black shade ramp.
	//
	// Drawn at the full allotted size. The caller is responsible for the border, which sits on top
	// and is painted separately by PaintBorder so the two can be layered around content.
	void PaintBackground(
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FGeometry& Geometry,
		const FVector2f& Size,
		const FParams& Params);

	// Paint the top and bottom border bars, each at its own authored intensity.
	//
	// Separate from PaintBackground because the slider paints its fill between the two, and the
	// border has to stay above the fill while the shade stays below it.
	void PaintBorder(
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FGeometry& Geometry,
		const FVector2f& Size,
		const FParams& Params);
}