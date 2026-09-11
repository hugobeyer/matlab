#include "MixtormatEditorSettings.h"

#include "Style/MixtormatPalette.h"

UMixtormatEditorSettings::UMixtormatEditorSettings()
	: MaskColor(MixtormatPalette::DebugMask())
	, IdColorA(MixtormatPalette::DebugIdA())
	, IdColorB(MixtormatPalette::DebugIdB())
	, InvalidGroutColor(MixtormatPalette::DebugInvalidGrout())
{
}
