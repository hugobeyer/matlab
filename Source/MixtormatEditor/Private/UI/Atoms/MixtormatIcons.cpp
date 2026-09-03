#include "UI/Atoms/MixtormatIcons.h"

#include "Style/MixtormatStyle.h"

namespace MixtormatIcons
{
	const FSlateBrush* Get(const FName Key)
	{
		return FMixtormatStyle::Get().GetBrush(Key);
	}

	const FSlateBrush* Eye()          { return Get(TEXT("Mixtormat.Icon.Eye")); }
	const FSlateBrush* EyeOff()       { return Get(TEXT("Mixtormat.Icon.EyeOff")); }
	// Chevrons are ours rather than FAppStyle's. Borrowing the editor's meant the one glyph in a
	// layer row that we did not control changed weight with the editor theme, next to an eye and a
	// badge that did not.
	const FSlateBrush* ChevronDown()  { return Get(TEXT("Mixtormat.Icon.ChevronDown")); }
	const FSlateBrush* ChevronRight() { return Get(TEXT("Mixtormat.Icon.ChevronRight")); }
	const FSlateBrush* Refresh()      { return Get(TEXT("Mixtormat.Icon.Refresh")); }
	const FSlateBrush* Overflow()     { return Get(TEXT("Mixtormat.Icon.Overflow")); }
	const FSlateBrush* Add()          { return Get(TEXT("Mixtormat.Icon.Add")); }
	const FSlateBrush* Duplicate()    { return Get(TEXT("Mixtormat.Icon.Duplicate")); }

	const FSlateBrush* Mask()         { return Get(TEXT("Mixtormat.Icon.Mask")); }
	const FSlateBrush* Effect()       { return Get(TEXT("Mixtormat.Icon.Effect")); }
	const FSlateBrush* Generated()    { return Get(TEXT("Mixtormat.Icon.Generated")); }
}
