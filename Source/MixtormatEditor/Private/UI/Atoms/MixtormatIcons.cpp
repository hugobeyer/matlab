// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Atoms/MixtormatIcons.h"

#include "Style/MixtormatStyle.h"

namespace
{
	const FSlateBrush* Get(const FName Key)
	{
		return FMixtormatStyle::Get().GetBrush(Key);
	}

}

namespace MixtormatIcons
{
	const FSlateBrush* Save() { return Get(TEXT("Mixtormat.Icon.Save")); }
	const FSlateBrush* SaveAs() { return Get(TEXT("Mixtormat.Icon.SaveAs")); }
	const FSlateBrush* Settings() { return Get(TEXT("Mixtormat.Icon.Settings")); }
	const FSlateBrush* Grip() { return Get(TEXT("Mixtormat.Icon.Grip")); }
	const FSlateBrush* ArrowUp() { return Get(TEXT("Mixtormat.Icon.ArrowUp")); }
	const FSlateBrush* ArrowDown() { return Get(TEXT("Mixtormat.Icon.ArrowDown")); }
	const FSlateBrush* Cube() { return Get(TEXT("Mixtormat.Icon.Cube")); }
	const FSlateBrush* Sphere() { return Get(TEXT("Mixtormat.Icon.Sphere")); }
	const FSlateBrush* Plane() { return Get(TEXT("Mixtormat.Icon.Plane")); }
	const FSlateBrush* Cylinder() { return Get(TEXT("Mixtormat.Icon.Cylinder")); }
	const FSlateBrush* Globe() { return Get(TEXT("Mixtormat.Icon.Globe")); }
	const FSlateBrush* Nodes() { return Get(TEXT("Mixtormat.Icon.Nodes")); }
	const FSlateBrush* Camera() { return Get(TEXT("Mixtormat.Icon.Camera")); }
	const FSlateBrush* Search() { return Get(TEXT("Mixtormat.Icon.Search")); }
	const FSlateBrush* Documentation() { return Get(TEXT("Mixtormat.Icon.Documentation")); }
	const FSlateBrush* Feedback() { return Get(TEXT("Mixtormat.Icon.Feedback")); }
	const FSlateBrush* LightNeutral() { return Get(TEXT("Mixtormat.Icon.LightNeutral")); }
	const FSlateBrush* LightSoft() { return Get(TEXT("Mixtormat.Icon.LightSoft")); }
	const FSlateBrush* LightDramatic() { return Get(TEXT("Mixtormat.Icon.LightDramatic")); }
	const FSlateBrush* LightRim() { return Get(TEXT("Mixtormat.Icon.LightRim")); }
	const FSlateBrush* QualityLow() { return Get(TEXT("Mixtormat.Icon.QualityLow")); }
	const FSlateBrush* QualityMedium() { return Get(TEXT("Mixtormat.Icon.QualityMedium")); }
	const FSlateBrush* QualityHigh() { return Get(TEXT("Mixtormat.Icon.QualityHigh")); }
	const FSlateBrush* ChevronUp() { return Get(TEXT("Mixtormat.Icon.ChevronUp")); }
	const FSlateBrush* ChevronDownBold() { return Get(TEXT("Mixtormat.Icon.ChevronDownBold")); }
	const FSlateBrush* HierarchyRoot() { return Get(TEXT("Mixtormat.Icon.HierarchyRoot")); }
	const FSlateBrush* Indent1() { return Get(TEXT("Mixtormat.Icon.Indent1")); }
	const FSlateBrush* Indent2() { return Get(TEXT("Mixtormat.Icon.Indent2")); }
	const FSlateBrush* Indent3() { return Get(TEXT("Mixtormat.Icon.Indent3")); }
	const FSlateBrush* TreeElbow() { return Get(TEXT("Mixtormat.Icon.TreeElbow")); }
	const FSlateBrush* TreeBranchDotted() { return Get(TEXT("Mixtormat.Icon.TreeBranchDotted")); }
	const FSlateBrush* TreeTee() { return Get(TEXT("Mixtormat.Icon.TreeTee")); }
	const FSlateBrush* TreeCross() { return Get(TEXT("Mixtormat.Icon.TreeCross")); }

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
	const FSlateBrush* Generator()    { return Get(TEXT("Mixtormat.Icon.Generator")); }
	const FSlateBrush* Generated()    { return Get(TEXT("Mixtormat.Icon.Generated")); }
	const FSlateBrush* Ids()          { return Get(TEXT("Mixtormat.Icon.Ids")); }

	const FSlateBrush* LayerMaterial() { return Get(TEXT("Mixtormat.Icon.LayerMaterial")); }
	const FSlateBrush* LayerFill()     { return Get(TEXT("Mixtormat.Icon.LayerFill")); }
	const FSlateBrush* Folder()        { return Get(TEXT("Mixtormat.Icon.Folder")); }
	const FSlateBrush* Check()        { return Get(TEXT("Mixtormat.Icon.Check")); }
	const FSlateBrush* Trash()        { return Get(TEXT("Mixtormat.Icon.Trash")); }
	const FSlateBrush* ScalarRampConstant() { return Get(TEXT("Mixtormat.Icon.ScalarRampConstant")); }
	const FSlateBrush* ScalarRampLinear() { return Get(TEXT("Mixtormat.Icon.ScalarRampLinear")); }
	const FSlateBrush* ScalarRampSpline() { return Get(TEXT("Mixtormat.Icon.ScalarRampSpline")); }
	const FSlateBrush* ScalarRampBSpline() { return Get(TEXT("Mixtormat.Icon.ScalarRampBSpline")); }
	const FSlateBrush* ScalarRampFrame() { return Get(TEXT("Mixtormat.Icon.ScalarRampFrame")); }
	const FSlateBrush* ScalarRampReset() { return Get(TEXT("Mixtormat.Icon.ScalarRampReset")); }
}
