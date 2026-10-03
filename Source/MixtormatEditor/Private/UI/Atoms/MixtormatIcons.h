// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FSlateBrush;

// Named access to the icon set, so a widget never spells a style key.
//
// Every glyph is an owned PNG registered by MixtormatStyle. Going through here means a renamed or
// re-sized icon is one edit, and a missing one fails at a single call site instead of silently
// resolving to an empty brush wherever it was typed.
namespace MixtormatIcons
{
	const FSlateBrush* Save();
	const FSlateBrush* SaveAs();
	const FSlateBrush* Settings();
	const FSlateBrush* Grip();
	const FSlateBrush* ArrowUp();
	const FSlateBrush* ArrowDown();
	const FSlateBrush* Cube();
	const FSlateBrush* Sphere();
	const FSlateBrush* Plane();
	const FSlateBrush* Cylinder();
	const FSlateBrush* Globe();
	const FSlateBrush* Nodes();
	const FSlateBrush* Camera();
	const FSlateBrush* Search();
	const FSlateBrush* Documentation();
	const FSlateBrush* Feedback();
	const FSlateBrush* LightNeutral();
	const FSlateBrush* LightSoft();
	const FSlateBrush* LightDramatic();
	const FSlateBrush* LightRim();
	const FSlateBrush* QualityLow();
	const FSlateBrush* QualityMedium();
	const FSlateBrush* QualityHigh();
	const FSlateBrush* ChevronUp();
	const FSlateBrush* ChevronDownBold();
	const FSlateBrush* HierarchyRoot();
	const FSlateBrush* Indent1();
	const FSlateBrush* Indent2();
	const FSlateBrush* Indent3();
	const FSlateBrush* TreeElbow();
	const FSlateBrush* TreeBranchDotted();
	const FSlateBrush* TreeTee();
	const FSlateBrush* TreeCross();

	const FSlateBrush* Eye();
	const FSlateBrush* EyeOff();
	const FSlateBrush* ChevronDown();
	const FSlateBrush* ChevronRight();
	const FSlateBrush* Refresh();
	const FSlateBrush* Overflow();
	const FSlateBrush* Add();
	const FSlateBrush* Duplicate();

	// A layer child, by what kind of thing it is.
	const FSlateBrush* Mask();
	const FSlateBrush* Effect();
	const FSlateBrush* Generator();
	const FSlateBrush* Generated();
	// IDs and anything derived from them. Distinct from Generated: Generated Mask emits 0..1
	// coverage and joins the mask chain, an ID map does not.
	const FSlateBrush* Ids();

	// A layer, by what kind of thing it is: a square for a material, a circle for a fill -- the
	// same two shapes the add bar and the rows use, so one glyph teaches both.
	const FSlateBrush* LayerMaterial();
	const FSlateBrush* LayerFill();

	// A layer group. One glyph for both states: the chevron beside it already says open or shut,
	// and swapping the folder too would say it twice.
	const FSlateBrush* Folder();

	// The tick in a menu's icon gutter.
	const FSlateBrush* Check();
	const FSlateBrush* Trash();
}
