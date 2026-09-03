#include "UI/Atoms/MixtormatIcons.h"

#include "Style/MixtormatStyle.h"
#include "Styling/AppStyle.h"

namespace MixtormatIcons
{
	const FSlateBrush* Get(const FName Key)
	{
		return FMixtormatStyle::Get().GetBrush(Key);
	}

	const FSlateBrush* Eye()          { return Get(TEXT("Mixtormat.Icon.Eye")); }
	const FSlateBrush* EyeOff()       { return Get(TEXT("Mixtormat.Icon.EyeOff")); }
	// Chevrons come from the app style: they are pure geometry and match the editor's own
	// disclosure arrows, which is what a user expects a twisty to look like.
	const FSlateBrush* ChevronDown()  { return FAppStyle::GetBrush(TEXT("Icons.ChevronDown")); }
	const FSlateBrush* ChevronRight() { return FAppStyle::GetBrush(TEXT("Icons.ChevronRight")); }
	const FSlateBrush* Refresh()      { return Get(TEXT("Mixtormat.Icon.Refresh")); }
	const FSlateBrush* Overflow()     { return Get(TEXT("Mixtormat.Icon.Overflow")); }
	const FSlateBrush* Add()          { return Get(TEXT("Mixtormat.Icon.Add")); }
	const FSlateBrush* Duplicate()    { return Get(TEXT("Mixtormat.Icon.Duplicate")); }
}
