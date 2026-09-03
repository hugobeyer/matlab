#pragma once

#include "CoreMinimal.h"

struct FSlateBrush;

// Named access to the icon set, so a widget never spells a style key.
//
// Every glyph is a Lucide SVG registered by MixtormatStyle. Going through here means a renamed or
// re-sized icon is one edit, and a missing one fails at a single call site instead of silently
// resolving to an empty brush wherever it was typed.
namespace MixtormatIcons
{
	const FSlateBrush* Get(FName Key);

	const FSlateBrush* Eye();
	const FSlateBrush* EyeOff();
	const FSlateBrush* ChevronDown();
	const FSlateBrush* ChevronRight();
	const FSlateBrush* Refresh();
	const FSlateBrush* Overflow();
	const FSlateBrush* Add();
	const FSlateBrush* Duplicate();
}
