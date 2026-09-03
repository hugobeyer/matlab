#pragma once

#include "CoreMinimal.h"

class FSlateStyleSet;
class ISlateStyle;

/** Premium dark Slate visual system for the Mixtormat editor. */
class FMixtormatStyle final
{
public:
	static void Initialize();
	static void Shutdown();

	static const ISlateStyle& Get();
	static FName GetStyleSetName();

private:
	static TSharedPtr<FSlateStyleSet> StyleInstance;
};
