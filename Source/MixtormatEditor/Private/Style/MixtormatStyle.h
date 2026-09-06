#pragma once

#include "CoreMinimal.h"

class FMixtormatMutableStyleSet;
class ISlateStyle;

/** Premium dark Slate visual system for the Mixtormat editor. */
class FMixtormatStyle final
{
public:
	static void Initialize();
	static void Shutdown();
	static void Refresh();

	static const ISlateStyle& Get();
	static FName GetStyleSetName();

private:
	static TSharedPtr<FMixtormatMutableStyleSet> StyleInstance;
};
