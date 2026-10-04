// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Style/MixtormatThemeSchema.h"

namespace Mixtormat
{
	class FMixtormatStyleLocator final
	{
	public:
		static EMixtormatStyleTarget TargetFor(const FMixtormatThemeProperty& Property);
		static FText Label(EMixtormatStyleTarget Target);
		static bool Begin(EMixtormatStyleTarget Target);
		static void SetDimmed(bool bDimmed);
		static void End();
	};
}
