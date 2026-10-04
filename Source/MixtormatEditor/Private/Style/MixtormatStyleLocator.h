// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace Mixtormat
{
	struct FMixtormatThemeProperty;

	enum class EMixtormatStyleTarget : uint8
	{
		None,
		Global,
		Controls,
		Foldout,
		Card,
		Layer,
		Button,
		Menu,
		Preview,
		Gallery,
		Shell,
	};

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
