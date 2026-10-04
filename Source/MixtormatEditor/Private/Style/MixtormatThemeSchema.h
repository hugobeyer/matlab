// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Style/MixtormatTheme.h"

namespace Mixtormat
{
	enum class EMixtormatThemeTab : uint8
	{
		Global,
		Controls,
		Foldouts,
		Cards,
		Layers,
		Buttons,
		Menus,
		Preview,
		GalleryShell,
		Typography,
		Count,
	};

	enum class EMixtormatThemePropertyKind : uint8
	{
		Number,
		Bool,
		Choice,
		Color,
	};

	// Refresh mode for a theme property change. Strongest wins when coalescing.
	// Paint < Layout < StyleRefresh < Reconstruct
	enum class EMixtormatThemeRefreshMode : uint8
	{
		Paint,          // Invalidate paint only (OnPaint, recipes, palette colors, opacities, borders)
		Layout,         // Invalidate layout (desired size, padding where widgets read dynamically)
		StyleRefresh,   // Call FMixtormatStyle::Refresh() for legacy style-set consumers
		Reconstruct     // Full workspace reconstruction (construction-cached values, topology changes)
	};

	struct FMixtormatThemeProperty
	{
		FName Id;
		FString Label;
		FString Section;
		FString Help;
		EMixtormatThemeTab Tab = EMixtormatThemeTab::Global;
		EMixtormatThemePropertyKind Kind = EMixtormatThemePropertyKind::Number;
		EMixtormatThemeRefreshMode RefreshMode = EMixtormatThemeRefreshMode::Reconstruct;

		float Minimum = 0.0f;
		float Maximum = 1.0f;
		float Step = 0.01f;
		int32 Precision = 2;
		TArray<FString> Options;

		TFunction<float(const FMixtormatTheme&)> GetNumber;
		TFunction<void(FMixtormatTheme&, float)> SetNumber;
		TFunction<bool(const FMixtormatTheme&)> GetBool;
		TFunction<void(FMixtormatTheme&, bool)> SetBool;
		TFunction<int32(const FMixtormatTheme&)> GetChoice;
		TFunction<void(FMixtormatTheme&, int32)> SetChoice;
		TFunction<FLinearColor(const FMixtormatTheme&)> GetColor;
		TFunction<void(FMixtormatTheme&, const FLinearColor&)> SetColor;
	};

	class FMixtormatThemeSchema final
	{
	public:
		static const TArray<FMixtormatThemeProperty>& Properties();
		static const FMixtormatThemeProperty* Find(FName Id);
		static FString TabLabel(EMixtormatThemeTab Tab);
		static FString TabKey(EMixtormatThemeTab Tab);
		static FString SavePath();
		static bool Save(FString& OutError);
		static bool Load(FString& OutError, TArray<FText>& OutValidationIssues);
	};
}
