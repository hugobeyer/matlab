// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "Brushes/SlateColorBrush.h"
#include "Style/MixtormatThemeStore.h"
#include "Styling/SlateTypes.h"

namespace MixtormatShell
{
	inline const FSplitterStyle& GetSplitterStyle()
	{
		// SSplitter retains the style address. Refresh the stable object from the resolved Shell style.
		static FSplitterStyle Style;
		const Mixtormat::FMixtormatResolvedShellStyle& Shell = FMixtormatThemeStore::GetResolved().Shell;
		Style.SetHandleNormalBrush(FSlateColorBrush(Shell.Separator));
		Style.SetHandleHighlightBrush(FSlateColorBrush(Shell.SeparatorHover));
		return Style;
	}
}
