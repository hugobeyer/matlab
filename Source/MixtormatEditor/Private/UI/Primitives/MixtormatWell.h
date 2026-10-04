// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rendering/DrawElements.h"

// The shared control well: the recess a slider trough, a dropdown chip and a toggle all sit in.
//
// The prototype composes it from three stacked layers, and the order matters because each one is
// a different operation against the same ground (components.css:212-218):
//
//   1. ground      the surface colour itself
//   2. shade       a black vertical ramp at 0.64 -> 0.13, multiply
//   3. border      a 1px outline, plus-lighter, whose alpha falls top to bottom
//
// That third layer is why this is a painter and not a brush. `FSlateRoundedBoxBrush` carries one
// border colour for all four edges, so the authored falloff (0.33 at the top, 0.11 at the bottom)
// cannot be expressed in it at all -- every well read as evenly outlined all the way round. Two
// thin bars, one per edge, carry it exactly and cost one draw element each.
//
// This is deliberately the only place the well is built. The slider, the chip and the toggle each
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