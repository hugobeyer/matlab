// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Style/MixtormatThemeSchema.h"

class SWindow;
class FSlateRect;

namespace Mixtormat
{
	class FMixtormatStyleLocator final
	{
	public:
		static EMixtormatStyleTarget TargetFor(const FMixtormatThemeProperty& Property);
		static FText Label(EMixtormatStyleTarget Target);
		// Finds the visible widget closest to AnchorCenter whose type matches Target and starts
		// the outline pulse over its exact bounds. Without an anchor the first match wins.
		static bool Begin(EMixtormatStyleTarget Target, const TOptional<FVector2f>& AnchorCenter);
		// Expires the pulse once its duration is over; true while a target is still located.
		static bool Tick();
		// The pulse's 0..1 triangle at this instant, for the outline to draw with.
		static float GetPulseAlpha();
		// The located widget's absolute-space rect; false when nothing is located.
		static bool GetTargetRect(FSlateRect& OutRect);
		// The window the target lives in, so an outline in another window declines to draw.
		static TWeakPtr<SWindow> GetTargetWindow();
		static void End();
	};
}
