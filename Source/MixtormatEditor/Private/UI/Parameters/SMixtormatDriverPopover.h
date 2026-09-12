// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

// Dedicated surface for a parameter Driver. It deliberately owns only layout/styling; the
// inspector supplies the live rows so source/output menus stay tied to the current material.
class SMixtormatDriverPopover final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMixtormatDriverPopover) {}
		SLATE_ARGUMENT(FText, Title)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};
