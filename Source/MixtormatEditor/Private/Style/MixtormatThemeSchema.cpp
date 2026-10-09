// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Style/MixtormatThemeSchema.h"

#include "Style/MixtormatThemeStore.h"
#include "Services/MixtormatPaths.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace Mixtormat
{
	using ETab = EMixtormatThemeTab;
	using EKind = EMixtormatThemePropertyKind;
	using ETarget = EMixtormatStyleTarget;

	namespace
	{

FMixtormatThemeProperty Number(
		const TCHAR* Id, const ETab Tab, const TCHAR* Section, const TCHAR* Label,
		const double Min, const double Max, const double Step, const int32 Precision,
		TFunction<float(const FMixtormatTheme&)> Get,
		TFunction<void(FMixtormatTheme&, float)> Set,
		const TCHAR* Help = TEXT(""),
		EMixtormatThemeRefreshMode RefreshMode = EMixtormatThemeRefreshMode::Reconstruct)
		{
			FMixtormatThemeProperty P;
			P.Id = FName(Id);
			P.Label = Label;
			P.Section = Section;
			P.Help = Help;
			P.Tab = Tab;
			P.Kind = EKind::Number;
			P.Minimum = static_cast<float>(Min);
			P.Maximum = static_cast<float>(Max);
			P.Step = static_cast<float>(Step);
			P.Precision = Precision;
			P.GetNumber = MoveTemp(Get);
			P.SetNumber = MoveTemp(Set);
			P.RefreshMode = RefreshMode;
			return P;
		}

	FMixtormatThemeProperty Bool(
			const TCHAR* Id, const ETab Tab, const TCHAR* Section, const TCHAR* Label,
			TFunction<bool(const FMixtormatTheme&)> Get,
			TFunction<void(FMixtormatTheme&, bool)> Set,
			const TCHAR* Help = TEXT(""),
			EMixtormatThemeRefreshMode RefreshMode = EMixtormatThemeRefreshMode::Reconstruct)
		{
			FMixtormatThemeProperty P;
			P.Id = FName(Id);
			P.Label = Label;
			P.Section = Section;
			P.Help = Help;
			P.Tab = Tab;
			P.Kind = EKind::Bool;
			P.GetBool = MoveTemp(Get);
			P.SetBool = MoveTemp(Set);
			P.RefreshMode = RefreshMode;
			return P;
		}

	FMixtormatThemeProperty Choice(
			const TCHAR* Id, const ETab Tab, const TCHAR* Section, const TCHAR* Label,
			TArray<FString> Options,
			TFunction<int32(const FMixtormatTheme&)> Get,
			TFunction<void(FMixtormatTheme&, int32)> Set,
			const TCHAR* Help = TEXT(""),
			EMixtormatThemeRefreshMode RefreshMode = EMixtormatThemeRefreshMode::Reconstruct)
		{
			FMixtormatThemeProperty P;
			P.Id = FName(Id);
			P.Label = Label;
			P.Section = Section;
			P.Help = Help;
			P.Tab = Tab;
			P.Kind = EKind::Choice;
			P.Options = MoveTemp(Options);
			P.GetChoice = MoveTemp(Get);
			P.SetChoice = MoveTemp(Set);
			P.RefreshMode = RefreshMode;
			return P;
		}

	FMixtormatThemeProperty Color(
			const TCHAR* Id, const ETab Tab, const TCHAR* Section, const TCHAR* Label,
			TFunction<FLinearColor(const FMixtormatTheme&)> Get,
			TFunction<void(FMixtormatTheme&, const FLinearColor&)> Set,
			const TCHAR* Help = TEXT(""),
			EMixtormatThemeRefreshMode RefreshMode = EMixtormatThemeRefreshMode::Paint)
		{
			FMixtormatThemeProperty P;
			P.Id = FName(Id);
			P.Label = Label;
			P.Section = Section;
			P.Help = Help;
			P.Tab = Tab;
			P.Kind = EKind::Color;
			P.GetColor = MoveTemp(Get);
			P.SetColor = MoveTemp(Set);
			P.RefreshMode = RefreshMode;
			return P;
		}

		TArray<FString> BlendOptions()
		{
			return {
				TEXT("Normal"),
				TEXT("Additive / Plus Lighter"),
				TEXT("Multiply"),
				TEXT("Soft Light")
			};
		}

		TArray<FString> WeightOptions()
		{
			return { TEXT("Regular"), TEXT("SemiBold"), TEXT("Bold") };
		}

	void SetLocateTarget(
		TArray<FMixtormatThemeProperty>& Properties,
		const int32 BeginIndex,
		const ETarget Target)
	{
		for (int32 Index = BeginIndex; Index < Properties.Num(); ++Index)
		{
			Properties[Index].LocateTarget = Target;
		}
	}

void AddIconRole(
			TArray<FMixtormatThemeProperty>& Out,
			const EMixtormatIconRole Role,
			const TCHAR* Prefix,
			const TCHAR* Label,
			const ETarget LocateTarget,
			EMixtormatThemeRefreshMode RefreshMode = EMixtormatThemeRefreshMode::Reconstruct)
		{
			const int32 LocateBegin = Out.Num();
			const uint8 Index = static_cast<uint8>(Role);
			const EMixtormatThemeRefreshMode OpacityRefreshMode =
				Role == EMixtormatIconRole::PreviewToolbar || Role == EMixtormatIconRole::Menu
					? EMixtormatThemeRefreshMode::Paint
					: EMixtormatThemeRefreshMode::Reconstruct;
			const FString Section = FString::Printf(TEXT("Icons / %s"), Label);
			auto Id = [Prefix](const TCHAR* Suffix)
			{
				return FString::Printf(TEXT("Icons.%s.%s"), Prefix, Suffix);
			};
			Out.Add(Number(*Id(TEXT("GlyphSize")), ETab::Global, *Section, TEXT("Glyph Size"), 4.0f, 64.0f, 1.0f, 0,
				[Index](const FMixtormatTheme& T) { return T.Icons.Roles[Index].GlyphSize; },
				[Index](FMixtormatTheme& T, float V) { T.Icons.Roles[Index].GlyphSize = V; }, TEXT(""), RefreshMode));
			Out.Add(Number(*Id(TEXT("ButtonSize")), ETab::Global, *Section, TEXT("Button Size"), 4.0f, 96.0f, 1.0f, 0,
				[Index](const FMixtormatTheme& T) { return T.Icons.Roles[Index].ButtonSize; },
				[Index](FMixtormatTheme& T, float V) { T.Icons.Roles[Index].ButtonSize = V; }, TEXT(""), RefreshMode));
			Out.Add(Number(*Id(TEXT("HitSize")), ETab::Global, *Section, TEXT("Hit Size"), 4.0f, 128.0f, 1.0f, 0,
				[Index](const FMixtormatTheme& T) { return T.Icons.Roles[Index].HitSize; },
				[Index](FMixtormatTheme& T, float V) { T.Icons.Roles[Index].HitSize = V; }, TEXT(""), RefreshMode));

			Out.Add(Number(*Id(TEXT("RestOpacity")), ETab::Global, *Section, TEXT("Rest Opacity"), 0.0f, 1.0f, 0.01f, 2,
				[Index](const FMixtormatTheme& T) { return T.Icons.Roles[Index].RestOpacity; },
				[Index](FMixtormatTheme& T, float V) { T.Icons.Roles[Index].RestOpacity = V; }, TEXT(""), OpacityRefreshMode));
			Out.Add(Number(*Id(TEXT("HoverOpacity")), ETab::Global, *Section, TEXT("Hover Opacity"), 0.0f, 1.0f, 0.01f, 2,
				[Index](const FMixtormatTheme& T) { return T.Icons.Roles[Index].HoverOpacity; },
				[Index](FMixtormatTheme& T, float V) { T.Icons.Roles[Index].HoverOpacity = V; }, TEXT(""), OpacityRefreshMode));
			Out.Add(Number(*Id(TEXT("DisabledOpacity")), ETab::Global, *Section, TEXT("Disabled Opacity"), 0.0f, 1.0f, 0.01f, 2,
				[Index](const FMixtormatTheme& T) { return T.Icons.Roles[Index].DisabledOpacity; },
				[Index](FMixtormatTheme& T, float V) { T.Icons.Roles[Index].DisabledOpacity = V; }, TEXT(""), OpacityRefreshMode));
			if (Role == EMixtormatIconRole::LayerEye)
			{
				Out.Add(Number(*Id(TEXT("Radius")), ETab::Global, *Section, TEXT("Mark Radius"), 0, 12, .25f, 2,
					[Index](const FMixtormatTheme& T) { return T.Icons.Roles[Index].MarkRadius; },
					[Index](FMixtormatTheme& T, float V) { T.Icons.Roles[Index].MarkRadius = V; }, TEXT(""), RefreshMode));
				Out.Add(Number(*Id(TEXT("OutlineWidth")), ETab::Global, *Section, TEXT("Outline Width"), 0, 4, .25f, 2,
					[Index](const FMixtormatTheme& T) { return T.Icons.Roles[Index].MarkOutlineWidth; },
					[Index](FMixtormatTheme& T, float V) { T.Icons.Roles[Index].MarkOutlineWidth = V; }, TEXT(""), RefreshMode));
			}
			SetLocateTarget(Out, LocateBegin, LocateTarget);
		}

	void AddTextRole(
			TArray<FMixtormatThemeProperty>& Out,
			const EMixtormatTextRole Role,
			const TCHAR* Prefix,
			const TCHAR* Label,
			EMixtormatThemeRefreshMode RefreshMode = EMixtormatThemeRefreshMode::Reconstruct)
		{
			const uint8 Index = static_cast<uint8>(Role);
			const FString Section = FString::Printf(TEXT("Role / %s"), Label);
			auto Id = [Prefix](const TCHAR* Suffix)
			{
				return FString::Printf(TEXT("Typography.%s.%s"), Prefix, Suffix);
			};
			Out.Add(Number(*Id(TEXT("Size")), ETab::Typography, *Section, TEXT("Size"), 6.0f, 32.0f, 0.5f, 1,
				[Index](const FMixtormatTheme& T) { return T.Typography.Roles[Index].Size; },
				[Index](FMixtormatTheme& T, float V) { T.Typography.Roles[Index].Size = V; }, TEXT(""), RefreshMode));
			Out.Add(Choice(*Id(TEXT("Weight")), ETab::Typography, *Section, TEXT("Weight"), WeightOptions(),
				[Index](const FMixtormatTheme& T) { return static_cast<int32>(T.Typography.Roles[Index].Weight); },
				[Index](FMixtormatTheme& T, int32 V)
				{
					T.Typography.Roles[Index].Weight = static_cast<EMixtormatFontWeight>(FMath::Clamp(V, 0, 2));
				}, TEXT(""), RefreshMode));
			Out.Add(Number(*Id(TEXT("TrackingPx")), ETab::Typography, *Section, TEXT("Tracking (px)"), -2.0f, 8.0f, 0.1f, 1,
				[Index](const FMixtormatTheme& T) { return T.Typography.Roles[Index].TrackingPx; },
				[Index](FMixtormatTheme& T, float V) { T.Typography.Roles[Index].TrackingPx = V; }, TEXT(""), RefreshMode));
			Out.Add(Number(*Id(TEXT("Opacity")), ETab::Typography, *Section, TEXT("Opacity"), 0.0f, 1.0f, 0.01f, 2,
				[Index](const FMixtormatTheme& T) { return T.Typography.Roles[Index].Opacity; },
				[Index](FMixtormatTheme& T, float V) { T.Typography.Roles[Index].Opacity = V; }, TEXT(""), RefreshMode));
			Out.Add(Bool(*Id(TEXT("MonospacedNumbers")), ETab::Typography, *Section, TEXT("Monospaced Numbers"),
				[Index](const FMixtormatTheme& T) { return T.Typography.Roles[Index].bMonospacedNumbers; },
				[Index](FMixtormatTheme& T, bool V) { T.Typography.Roles[Index].bMonospacedNumbers = V; }, TEXT(""), RefreshMode));
		}

		const TArray<FMixtormatThemeProperty>& BuildProperties()
		{
			static const TArray<FMixtormatThemeProperty> Built = []()
			{
				TArray<FMixtormatThemeProperty> P;

#define NUM(Id, Tab, Section, Label, Path, Min, Max, Step, Prec, RefreshMode) \
	P.Add(Number(TEXT(Id), ETab::Tab, TEXT(Section), TEXT(Label), Min, Max, Step, Prec, \
		[](const FMixtormatTheme& T) { return T.Path; }, \
		[](FMixtormatTheme& T, float V) { T.Path = V; }, TEXT(""), RefreshMode))
#define NUM_DEF(Id, Tab, Section, Label, Path, Min, Max, Step, Prec) \
	NUM(Id, Tab, Section, Label, Path, Min, Max, Step, Prec, EMixtormatThemeRefreshMode::Paint)
#define COL(Id, Tab, Section, Label, Path, RefreshMode) \
	P.Add(Color(TEXT(Id), ETab::Tab, TEXT(Section), TEXT(Label), \
		[](const FMixtormatTheme& T) { return T.Path; }, \
		[](FMixtormatTheme& T, const FLinearColor& V) { T.Path = V; }, TEXT(""), RefreshMode))
#define COL_DEF(Id, Tab, Section, Label, Path) \
	COL(Id, Tab, Section, Label, Path, EMixtormatThemeRefreshMode::Paint)
#define BLEND(Id, Tab, Section, Label, Path, RefreshMode) \
	P.Add(Choice(TEXT(Id), ETab::Tab, TEXT(Section), TEXT(Label), BlendOptions(), \
		[](const FMixtormatTheme& T) { return static_cast<int32>(T.Path); }, \
		[](FMixtormatTheme& T, int32 V) { T.Path = static_cast<MixtormatCompositing::EMixtormatBlendMode>(FMath::Clamp(V, 0, 3)); }, TEXT(""), RefreshMode))
#define BLEND_DEF(Id, Tab, Section, Label, Path) \
	BLEND(Id, Tab, Section, Label, Path, EMixtormatThemeRefreshMode::Paint)

// GLOBAL / semantic palette. OverlayGround is consumed by preview cluster grounds.
		int32 LocateBegin = P.Num();
		COL_DEF("Palette.Ground", Global, "Palette", "Ground", Palette.Ground);
		COL_DEF("Palette.Shell", Global, "Palette", "Shell", Palette.Shell);
		COL_DEF("Palette.Panel", Global, "Palette", "Panel", Palette.Panel);
		COL_DEF("Palette.Text", Global, "Palette", "Text", Palette.Text);
		COL_DEF("Palette.TextMuted", Global, "Palette", "Text Muted", Palette.TextMuted);
		COL_DEF("Palette.Accent", Global, "Palette", "Accent", Palette.Accent);
		COL_DEF("Palette.Modified", Global, "Palette", "Modified", Palette.Modified);
		COL_DEF("Palette.Warning", Global, "Palette", "Warning", Palette.Warning);
		COL_DEF("Palette.Error", Global, "Palette", "Error", Palette.Error);
		COL_DEF("Palette.Shade", Global, "Palette", "Shade", Palette.Shade);
		COL_DEF("Palette.Hairline", Global, "Palette", "Hairline", Palette.Hairline);
		SetLocateTarget(P, LocateBegin, ETarget::Global);

		LocateBegin = P.Num();
		COL_DEF("Palette.MenuGround", Global, "Palette", "Menu Ground", Palette.MenuGround);
		SetLocateTarget(P, LocateBegin, ETarget::Menu);

		LocateBegin = P.Num();
		COL_DEF("Palette.ThumbnailGround", Global, "Palette", "Thumbnail Ground", Palette.ThumbnailGround);
		SetLocateTarget(P, LocateBegin, ETarget::Gallery);

		LocateBegin = P.Num();
		COL_DEF("Palette.OverlayGround", Global, "Palette", "Overlay Ground", Palette.OverlayGround);
		SetLocateTarget(P, LocateBegin, ETarget::Preview);

				// TopBar and PanelToolbar use generic icon-button widgets with no role identity,
				// so locating either would blink unrelated controls. Leave them disabled until
				// those consumers expose a semantic locator identity.
				AddIconRole(P, EMixtormatIconRole::TopBar, TEXT("TopBar"), TEXT("Top Bar"), ETarget::None);
				AddIconRole(P, EMixtormatIconRole::PanelToolbar, TEXT("PanelToolbar"), TEXT("Panel Toolbar"), ETarget::None);
				AddIconRole(P, EMixtormatIconRole::PreviewToolbar, TEXT("PreviewToolbar"), TEXT("Preview Toolbar"), ETarget::Preview);
				AddIconRole(P, EMixtormatIconRole::LayerEye, TEXT("LayerEye"), TEXT("Layer Eye"), ETarget::Layer);
				AddIconRole(P, EMixtormatIconRole::LayerDisclosure, TEXT("LayerDisclosure"), TEXT("Layer Disclosure"), ETarget::Layer);
				AddIconRole(P, EMixtormatIconRole::FoldoutDisclosure, TEXT("FoldoutDisclosure"), TEXT("Foldout Disclosure"), ETarget::Foldout);
				AddIconRole(P, EMixtormatIconRole::CardLeading, TEXT("CardLeading"), TEXT("Card Leading"), ETarget::None);
				AddIconRole(P, EMixtormatIconRole::Menu, TEXT("Menu"), TEXT("Menu"), ETarget::Menu);
				AddIconRole(P, EMixtormatIconRole::GalleryToolbar, TEXT("GalleryToolbar"), TEXT("Gallery Toolbar"), ETarget::Gallery);
				AddIconRole(P, EMixtormatIconRole::NavigationRail, TEXT("NavigationRail"), TEXT("Navigation Rail"), ETarget::None);


// CONTROLS
		LocateBegin = P.Num();
		NUM_DEF("Well.Radius", Controls, "Well", "Radius", Well.Radius, 0, 12, .5, 1);
		BLEND_DEF("Well.ShadeBlend", Controls, "Well", "Shade Blend", Well.ShadeBlend);
		NUM_DEF("Well.ShadeTop", Controls, "Well", "Shade Top", Well.ShadeTop, 0, 1, .01, 2);
		NUM_DEF("Well.ShadeBottom", Controls, "Well", "Shade Bottom", Well.ShadeBottom, 0, 1, .01, 2);
		NUM_DEF("Well.BorderWidth", Controls, "Well", "Border Width", Well.BorderWidth, 0, 4, .25, 2);
		NUM_DEF("Well.BorderOpacity", Controls, "Well", "Border Opacity", Well.BorderOpacity, 0, 1, .01, 2);
		NUM_DEF("Well.BorderTopOpacity", Controls, "Well", "Border Top Opacity", Well.BorderTopOpacity, 0, 1, .01, 2);
		NUM_DEF("Well.BorderBottomOpacity", Controls, "Well", "Border Bottom Opacity", Well.BorderBottomOpacity, 0, 1, .01, 2);
		NUM_DEF("Well.BorderHoverOpacity", Controls, "Well", "Hover Border Opacity", Well.BorderHoverOpacity, 0, 1, .01, 2);
		NUM_DEF("Well.BorderHoverTopOpacity", Controls, "Well", "Hover Border Top", Well.BorderHoverTopOpacity, 0, 1, .01, 2);
		NUM_DEF("Well.BorderHoverBottomOpacity", Controls, "Well", "Hover Border Bottom", Well.BorderHoverBottomOpacity, 0, 1, .01, 2);
		NUM_DEF("Well.BorderSaturation", Controls, "Well", "Border Saturation", Well.BorderSaturation, 0, 4, .05, 2);
		NUM_DEF("Well.HoverLiftOpacity", Controls, "Well", "Hover Lift", Well.HoverLiftOpacity, 0, 1, .01, 2);
		SetLocateTarget(P, LocateBegin, ETarget::ControlWell);

		LocateBegin = P.Num();
		BLEND_DEF("Fill.BodyBlend", Controls, "Fill", "Body Blend", Fill.BodyBlend);
		BLEND_DEF("Fill.ShadeBlend", Controls, "Fill", "Shade Blend", Fill.ShadeBlend);
		NUM_DEF("Fill.Top", Controls, "Fill", "Top", Fill.Top, 0, 1, .01, 2);
		NUM_DEF("Fill.Bottom", Controls, "Fill", "Bottom", Fill.Bottom, 0, 1, .01, 2);
		NUM_DEF("Fill.HoverTop", Controls, "Fill", "Hover Top", Fill.HoverTop, 0, 1, .01, 2);
		NUM_DEF("Fill.HoverBottom", Controls, "Fill", "Hover Bottom", Fill.HoverBottom, 0, 1, .01, 2);
		NUM_DEF("Fill.ActiveTop", Controls, "Fill", "Active Top", Fill.ActiveTop, 0, 1, .01, 2);
		NUM_DEF("Fill.ActiveBottom", Controls, "Fill", "Active Bottom", Fill.ActiveBottom, 0, 1, .01, 2);
		NUM_DEF("Fill.Saturation", Controls, "Fill", "Saturation", Fill.Saturation, 0, 4, .05, 2);
		NUM_DEF("Fill.HoverSaturation", Controls, "Fill", "Hover Saturation", Fill.HoverSaturation, 0, 4, .05, 2);
		NUM_DEF("Fill.ActiveSaturation", Controls, "Fill", "Active Saturation", Fill.ActiveSaturation, 0, 4, .05, 2);
		NUM_DEF("Fill.DisabledOpacity", Controls, "Fill", "Disabled Opacity", Fill.DisabledOpacity, 0, 1, .01, 2);
		NUM_DEF("Fill.DisabledSaturation", Controls, "Fill", "Disabled Saturation", Fill.DisabledSaturation, 0, 4, .05, 2);
		NUM_DEF("Fill.ShadeStart", Controls, "Fill", "Shade Start", Fill.ShadeStart, 0, 1, .01, 2);
		NUM_DEF("Fill.ShadeMid", Controls, "Fill", "Shade Mid", Fill.ShadeMid, 0, 1, .01, 2);
		NUM_DEF("Fill.ShadeEnd", Controls, "Fill", "Shade End", Fill.ShadeEnd, 0, 1, .01, 2);
		NUM_DEF("Fill.ShadeMidPosition", Controls, "Fill", "Shade Mid Position", Fill.ShadeMidPosition, 0, 1, .01, 2);
		NUM_DEF("Fill.FalloffPower", Controls, "Fill", "Falloff Power", Fill.FalloffPower, .01, 4, .05, 2);
		SetLocateTarget(P, LocateBegin, ETarget::ControlFill);

		LocateBegin = P.Num();
		NUM("Toggle.Size", Controls, "Toggle", "Size", Toggle.Size, 8, 32, 1, 0, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("Toggle.FillInset", Controls, "Toggle", "Fill Inset", Toggle.FillInset, 0, 12, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("Toggle.DisabledShadeTop", Controls, "Toggle", "Disabled Shade Top", Toggle.DisabledShadeTop, 0, 1, .01, 2, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("Toggle.DisabledShadeBottom", Controls, "Toggle", "Disabled Shade Bottom", Toggle.DisabledShadeBottom, 0, 1, .01, 2, EMixtormatThemeRefreshMode::Reconstruct);
		SetLocateTarget(P, LocateBegin, ETarget::ControlToggle);

		LocateBegin = P.Num();
		NUM("ControlLayout.RowHeight", Controls, "Layout", "Row Height", ControlLayout.RowHeight, 12, 48, 1, 0, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.RowGap", Controls, "Layout", "Row Gap", ControlLayout.RowGap, 0, 24, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.PairedGap", Controls, "Layout", "Paired Gap", ControlLayout.PairedGap, 0, 24, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.RowTextInset", Controls, "Layout", "Row Text Inset", ControlLayout.RowTextInset, 0, 32, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.RowLabelGap", Controls, "Layout", "Row Label Gap", ControlLayout.RowLabelGap, 0, 32, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.RowFieldMinWidth", Controls, "Layout", "Field Min Width", ControlLayout.RowFieldMinWidth, 0, 400, 1, 0, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.ColorSwatchWidth", Controls, "Layout", "Color Swatch Width", ControlLayout.ColorSwatchWidth, 0, 240, 1, 0, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.ColorSwatchHeight", Controls, "Layout", "Color Swatch Height", ControlLayout.ColorSwatchHeight, 0, 64, 1, 0, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.ButtonHeight", Controls, "Layout", "Button Height", ControlLayout.ButtonHeight, 12, 48, 1, 0, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.SegmentedControlGap", Controls, "Layout", "Segmented Gap", ControlLayout.SegmentedControlGap, 0, 24, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.DropdownLabelRatio", Controls, "Layout", "Dropdown Label Ratio", ControlLayout.DropdownLabelRatio, 0, 1, .01, 2, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.PanelGutter", Controls, "Layout", "Panel Gutter", ControlLayout.PanelGutter, 0, 32, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
		NUM_DEF("ControlLayout.DisabledLabelOpacity", Controls, "Layout", "Disabled Label Opacity", ControlLayout.DisabledLabelOpacity, 0, 1, .01, 2);
		NUM("ControlLayout.CornerRadius", Controls, "Layout", "Corner Radius", ControlLayout.CornerRadius, 0, 24, .25, 2, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.OutlineWidth", Controls, "Layout", "Outline Width", ControlLayout.OutlineWidth, 0, 8, .05, 2, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.IconBrushSize", Controls, "Layout", "Icon Brush Size", ControlLayout.IconBrushSize, 8, 64, 1, 0, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.IconBrushSizeLarge", Controls, "Layout", "Icon Brush Size Large", ControlLayout.IconBrushSizeLarge, 8, 96, 1, 0, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.IconButtonSize", Controls, "Layout", "Icon Button Size", ControlLayout.IconButtonSize, 8, 48, 1, 0, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.StatusDotSize", Controls, "Layout", "Status Dot Size", ControlLayout.StatusDotSize, 2, 24, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.ToolbarLabelPadding", Controls, "Layout", "Toolbar Label Padding", ControlLayout.ToolbarLabelPadding, 0, 32, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.CornerRadiusInner", Controls, "Layout", "Corner Radius Inner", ControlLayout.CornerRadiusInner, 0, 24, .25, 2, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.DraggerTextInset", Controls, "Layout", "Dragger Text Inset", ControlLayout.DraggerTextInset, 0, 32, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.ButtonPaddingCompact", Controls, "Layout", "Button Padding Compact", ControlLayout.ButtonPaddingCompact, 0, 32, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.ButtonPaddingTab", Controls, "Layout", "Button Padding Tab", ControlLayout.ButtonPaddingTab, 0, 32, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.InspectorFeatureButtonGap", Controls, "Layout", "Inspector Feature Gap", ControlLayout.InspectorFeatureButtonGap, 0, 24, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.DriverPopoverInnerGap", Controls, "Layout", "Driver Popover Gap", ControlLayout.DriverPopoverInnerGap, 0, 24, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.DialogPadding", Controls, "Layout", "Dialog Padding", ControlLayout.DialogPadding, 0, 48, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.DialogButtonGap", Controls, "Layout", "Dialog Button Gap", ControlLayout.DialogButtonGap, 0, 24, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.GroupOuterGap", Controls, "Layout", "Group Outer Gap", ControlLayout.GroupOuterGap, 0, 24, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.LayerChildIconSize", Controls, "Layout", "Layer Child Icon Size", ControlLayout.LayerChildIconSize, 8, 48, 1, 0, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.MaskPickerWidth", Controls, "Layout", "Mask Picker Width", ControlLayout.MaskPickerWidth, 200, 1200, 1, 0, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.ThumbnailCardPadding", Controls, "Layout", "Thumbnail Card Padding", ControlLayout.ThumbnailCardPadding, 0, 24, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.MaskGalleryTileGap", Controls, "Layout", "Mask Gallery Tile Gap", ControlLayout.MaskGalleryTileGap, 0, 32, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.MaskGalleryTileMaximum", Controls, "Layout", "Mask Gallery Tile Max", ControlLayout.MaskGalleryTileMaximum, 32, 256, 1, 0, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.ScalarRampIconSize", Controls, "Layout", "Scalar Ramp Icon Size", ControlLayout.ScalarRampIconSize, 8, 48, 1, 0, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.ScalarRampToolbarHeight", Controls, "Layout", "Scalar Ramp Toolbar Height", ControlLayout.ScalarRampToolbarHeight, 12, 48, 1, 0, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.ScalarRampToolbarGap", Controls, "Layout", "Scalar Ramp Toolbar Gap", ControlLayout.ScalarRampToolbarGap, 0, 24, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.ScalarRampViewportPadding", Controls, "Layout", "Scalar Ramp Viewport Padding", ControlLayout.ScalarRampViewportPadding, 0, 32, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.ScalarRampIconGap", Controls, "Layout", "Scalar Ramp Icon Gap", ControlLayout.ScalarRampIconGap, 0, 24, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.ScalarRampGridOpacity", Controls, "Layout", "Scalar Ramp Grid Opacity", ControlLayout.ScalarRampGridOpacity, 0, 1, .01, 2, EMixtormatThemeRefreshMode::Paint);
		NUM("ControlLayout.ScalarRampGridMajorOpacity", Controls, "Layout", "Scalar Ramp Grid Major Opacity", ControlLayout.ScalarRampGridMajorOpacity, 0, 1, .01, 2, EMixtormatThemeRefreshMode::Paint);
		NUM("ControlLayout.ScalarRampBackgroundOpacity", Controls, "Layout", "Scalar Ramp Background Opacity", ControlLayout.ScalarRampBackgroundOpacity, 0, 1, .01, 2, EMixtormatThemeRefreshMode::Paint);
		NUM("ControlLayout.ScalarRampShadeOpacity", Controls, "Layout", "Scalar Ramp Shade Opacity", ControlLayout.ScalarRampShadeOpacity, 0, 1, .01, 2, EMixtormatThemeRefreshMode::Paint);
		NUM("ControlLayout.ScalarRampBorderThickness", Controls, "Layout", "Scalar Ramp Border Thickness", ControlLayout.ScalarRampBorderThickness, 0, 4, .05, 2, EMixtormatThemeRefreshMode::Paint);
		NUM("ControlLayout.ScalarRampBorderOpacity", Controls, "Layout", "Scalar Ramp Border Opacity", ControlLayout.ScalarRampBorderOpacity, 0, 1, .01, 2, EMixtormatThemeRefreshMode::Paint);
		NUM("ControlLayout.DragGhostOpacity", Controls, "Layout", "Drag Ghost Opacity", ControlLayout.DragGhostOpacity, 0, 1, .01, 2, EMixtormatThemeRefreshMode::Paint);
		NUM("ControlLayout.DragGhostThumbnailSize", Controls, "Layout", "Drag Ghost Thumbnail", ControlLayout.DragGhostThumbnailSize, 16, 128, 1, 0, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.DragGhostPadding", Controls, "Layout", "Drag Ghost Padding", ControlLayout.DragGhostPadding, 0, 32, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("ControlLayout.DragGhostShadowInset", Controls, "Layout", "Drag Ghost Shadow Inset", ControlLayout.DragGhostShadowInset, 0, 32, .5, 1, EMixtormatThemeRefreshMode::Paint);
		NUM("ControlLayout.DragGhostShadowOffsetY", Controls, "Layout", "Drag Ghost Shadow Y", ControlLayout.DragGhostShadowOffsetY, 0, 32, .5, 1, EMixtormatThemeRefreshMode::Paint);
		SetLocateTarget(P, LocateBegin, ETarget::ControlLayout);

// FOLDOUTS
		LocateBegin = P.Num();
		COL_DEF("Foldout.LiftTint", Foldouts, "Surface", "Lift Tint", Foldout.LiftTint);
		COL_DEF("Foldout.HoverTint", Foldouts, "Surface", "Hover Tint", Foldout.HoverTint);
		COL_DEF("Foldout.HairlineHoverTint", Foldouts, "Surface", "Hover Hairline Tint", Foldout.HairlineHoverTint);
		NUM_DEF("Foldout.LiftOpacity", Foldouts, "Surface", "Lift Opacity", Foldout.LiftOpacity, 0, 1, .01, 2);
		NUM_DEF("Foldout.HoverTintOpacity", Foldouts, "Surface", "Hover Tint Opacity", Foldout.HoverTintOpacity, 0, 1, .01, 2);
		BLEND_DEF("Foldout.LiftBlend", Foldouts, "Surface", "Lift Blend", Foldout.LiftBlend);
		BLEND_DEF("Foldout.AccentBlend", Foldouts, "Surface", "Accent Blend", Foldout.AccentBlend);
		NUM_DEF("Foldout.LiftSaturation", Foldouts, "Surface", "Lift Saturation", Foldout.LiftSaturation, 0, 4, .05, 2);
		NUM_DEF("Foldout.HoverSaturation", Foldouts, "Surface", "Hover Saturation", Foldout.HoverSaturation, 0, 4, .05, 2);
		NUM_DEF("Foldout.AccentOpacity", Foldouts, "Surface", "Accent Opacity", Foldout.AccentOpacity, 0, 1, .01, 2);
		NUM_DEF("Foldout.AccentHoverOpacity", Foldouts, "Surface", "Accent Hover Opacity", Foldout.AccentHoverOpacity, 0, 1, .01, 2);
		NUM_DEF("Foldout.HairlineOpacity", Foldouts, "Surface", "Hairline Opacity", Foldout.HairlineOpacity, 0, 1, .01, 2);
		NUM_DEF("Foldout.HairlineHoverOpacity", Foldouts, "Surface", "Hairline Hover Opacity", Foldout.HairlineHoverOpacity, 0, 1, .01, 2);
		NUM_DEF("Foldout.HairlineSaturation", Foldouts, "Surface", "Hairline Saturation", Foldout.HairlineSaturation, 0, 4, .05, 2);
		NUM_DEF("Foldout.HairlineHoverSaturation", Foldouts, "Surface", "Hover Hairline Saturation", Foldout.HairlineHoverSaturation, 0, 4, .05, 2);
		NUM_DEF("Foldout.LiftFalloff.Start", Foldouts, "Falloff", "Start", Foldout.LiftFalloff.Start, 0, 1, .01, 2);
		NUM_DEF("Foldout.LiftFalloff.End", Foldouts, "Falloff", "End", Foldout.LiftFalloff.End, 0, 1, .01, 2);
		NUM_DEF("Foldout.LiftFalloff.Power", Foldouts, "Falloff", "Power", Foldout.LiftFalloff.Power, .01, 4, .05, 2);
			NUM("Foldout.LiftFalloff.Samples", Foldouts, "Falloff", "Samples", Foldout.LiftFalloff.Samples, 2, 16, 1, 0, EMixtormatThemeRefreshMode::Paint);
		NUM("FoldoutLayout.Height", Foldouts, "Layout", "Height", FoldoutLayout.Height, 12, 48, 1, 0, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("FoldoutLayout.Gutter", Foldouts, "Layout", "Gutter", FoldoutLayout.Gutter, 0, 24, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("FoldoutLayout.BodyTop", Foldouts, "Layout", "Body Top", FoldoutLayout.BodyTop, 0, 24, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("FoldoutLayout.BodyBottom", Foldouts, "Layout", "Body Bottom", FoldoutLayout.BodyBottom, 0, 24, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("FoldoutLayout.OuterTop", Foldouts, "Layout", "Outer Top", FoldoutLayout.OuterTop, 0, 24, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("FoldoutLayout.OuterBottom", Foldouts, "Layout", "Outer Bottom", FoldoutLayout.OuterBottom, 0, 24, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("FoldoutLayout.HeaderPaddingTop", Foldouts, "Layout", "Header Padding Top", FoldoutLayout.HeaderPaddingTop, 0, 16, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("FoldoutLayout.HeaderPaddingBottom", Foldouts, "Layout", "Header Padding Bottom", FoldoutLayout.HeaderPaddingBottom, 0, 16, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
		NUM_DEF("FoldoutLayout.Radius", Foldouts, "Layout", "Radius", FoldoutLayout.Radius, 0, 12, .5, 1);
		SetLocateTarget(P, LocateBegin, ETarget::Foldout);

// CARDS
		LocateBegin = P.Num();
		BLEND_DEF("Card.Blend", Cards, "Surface", "Blend", Card.Blend);
		NUM_DEF("Card.HeaderOpacity", Cards, "Surface", "Header Opacity", Card.HeaderOpacity, 0, 1, .01, 2);
		NUM_DEF("Card.BodyOpacity", Cards, "Surface", "Body Opacity", Card.BodyOpacity, 0, 1, .01, 2);
		NUM_DEF("Card.HeaderSaturation", Cards, "Surface", "Header Saturation", Card.HeaderSaturation, 0, 4, .05, 2);
		NUM_DEF("Card.BodySaturation", Cards, "Surface", "Body Saturation", Card.BodySaturation, 0, 4, .05, 2);
		NUM_DEF("Card.FalloffPower", Cards, "Surface", "Falloff Power", Card.FalloffPower, .01, 4, .05, 2);
		NUM_DEF("Card.Reach", Cards, "Surface", "Reach", Card.Reach, 0, 128, 1, 0);
		NUM_DEF("Card.Radius", Cards, "Surface", "Radius", Card.Radius, 0, 12, .5, 1);
		NUM("CardLayout.HeaderHeight", Cards, "Layout", "Header Height", CardLayout.HeaderHeight, 8, 48, 1, 0, EMixtormatThemeRefreshMode::StyleRefresh);
		NUM("CardLayout.HeaderLeft", Cards, "Layout", "Header Left", CardLayout.HeaderLeft, 0, 32, .5, 1, EMixtormatThemeRefreshMode::StyleRefresh);
			NUM("CardLayout.HeaderTop", Cards, "Layout", "Header Top", CardLayout.HeaderTop, 0, 32, .5, 1, EMixtormatThemeRefreshMode::StyleRefresh);
		NUM("CardLayout.HeaderRight", Cards, "Layout", "Header Right", CardLayout.HeaderRight, 0, 32, .5, 1, EMixtormatThemeRefreshMode::StyleRefresh);
			NUM("CardLayout.HeaderBottom", Cards, "Layout", "Header Bottom", CardLayout.HeaderBottom, 0, 32, .5, 1, EMixtormatThemeRefreshMode::StyleRefresh);
		NUM("CardLayout.HeaderMarginTop", Cards, "Layout", "Header Margin Top", CardLayout.HeaderMarginTop, 0, 24, .5, 1, EMixtormatThemeRefreshMode::StyleRefresh);
		NUM("CardLayout.HeaderMarginBottom", Cards, "Layout", "Header Margin Bottom", CardLayout.HeaderMarginBottom, 0, 24, .5, 1, EMixtormatThemeRefreshMode::StyleRefresh);
		NUM("CardLayout.OuterTop", Cards, "Layout", "Outer Top", CardLayout.OuterTop, 0, 24, .5, 1, EMixtormatThemeRefreshMode::StyleRefresh);
		NUM("CardLayout.OuterBottom", Cards, "Layout", "Outer Bottom", CardLayout.OuterBottom, 0, 24, .5, 1, EMixtormatThemeRefreshMode::StyleRefresh);
		NUM("CardLayout.BodyHorizontal", Cards, "Layout", "Body Horizontal", CardLayout.BodyHorizontal, 0, 32, .5, 1, EMixtormatThemeRefreshMode::StyleRefresh);
		NUM("CardLayout.BodyTop", Cards, "Layout", "Body Top", CardLayout.BodyTop, 0, 32, .5, 1, EMixtormatThemeRefreshMode::StyleRefresh);
		NUM("CardLayout.BodyBottom", Cards, "Layout", "Body Bottom", CardLayout.BodyBottom, 0, 32, .5, 1, EMixtormatThemeRefreshMode::StyleRefresh);
		NUM("CardLayout.Padding", Cards, "Layout", "Padding", CardLayout.Padding, 0, 32, .5, 1, EMixtormatThemeRefreshMode::StyleRefresh);
		NUM("CardLayout.Gap", Cards, "Layout", "Gap", CardLayout.Gap, 0, 32, .5, 1, EMixtormatThemeRefreshMode::StyleRefresh);
		SetLocateTarget(P, LocateBegin, ETarget::Card);

// LAYERS
		LocateBegin = P.Num();
		BLEND_DEF("Layer.Blend", Layers, "Rows", "Blend", Layer.Blend);
		BLEND_DEF("Layer.GroupBlend", Layers, "Rows", "Group Blend", Layer.GroupBlend);
		NUM_DEF("Layer.Radius", Layers, "Surface", "Corner Radius", Layer.Radius, 0, 8, .25, 2);
		NUM_DEF("Layer.RestSaturation", Layers, "Rows", "Rest Saturation", Layer.RestSaturation, 0, 4, .05, 2);
		NUM_DEF("Layer.HoverSaturation", Layers, "Rows", "Hover Saturation", Layer.HoverSaturation, 0, 4, .05, 2);
		NUM_DEF("Layer.SelectedSaturation", Layers, "Rows", "Selected Saturation", Layer.SelectedSaturation, 0, 4, .05, 2);
		NUM_DEF("Layer.RestStrength", Layers, "Rows", "Rest Strength", Layer.RestStrength, 0, 1, .01, 2);
		NUM_DEF("Layer.HoverStrength", Layers, "Rows", "Hover Strength", Layer.HoverStrength, 0, 1, .01, 2);
		NUM_DEF("Layer.SelectedStrength", Layers, "Rows", "Selected Strength", Layer.SelectedStrength, 0, 1, .01, 2);
		COL_DEF("Layer.RowBottom", Layers, "Row Colors", "Row Bottom", Layer.RowBottom);
		COL_DEF("Layer.HoverTop", Layers, "Row Colors", "Hover Top", Layer.HoverTop);
		COL_DEF("Layer.HoverBottom", Layers, "Row Colors", "Hover Bottom", Layer.HoverBottom);
		COL_DEF("Layer.SelectedTop", Layers, "Row Colors", "Selected Top", Layer.SelectedTop);
		COL_DEF("Layer.SelectedBottom", Layers, "Row Colors", "Selected Bottom", Layer.SelectedBottom);
		COL_DEF("Layer.ChildLeft", Layers, "Child Colors", "Child Left", Layer.ChildLeft);
		COL_DEF("Layer.ChildRight", Layers, "Child Colors", "Child Right", Layer.ChildRight);
		COL_DEF("Layer.ChildSelectedLeft", Layers, "Child Colors", "Child Selected Left", Layer.ChildSelectedLeft);
		COL_DEF("Layer.ChildSelectedRight", Layers, "Child Colors", "Child Selected Right", Layer.ChildSelectedRight);
		COL_DEF("Layer.Cross", Layers, "Group", "Cross", Layer.Cross);
		COL_DEF("Layer.HiddenTop", Layers, "Reference", "Hidden Top", Layer.HiddenTop);
		COL_DEF("Layer.HiddenEnd", Layers, "Reference", "Hidden End", Layer.HiddenEnd);
		NUM_DEF("Layer.GroupTintStrength", Layers, "Group", "Group Tint Strength", Layer.GroupTintStrength, 0, 1, .01, 2);
		NUM_DEF("Layer.GroupTintSelectedStrength", Layers, "Group", "Selected Group Tint", Layer.GroupTintSelectedStrength, 0, 1, .01, 2);
		NUM_DEF("Layer.ReferenceHiddenTint", Layers, "Reference", "Hidden Tint", Layer.ReferenceHiddenTint, 0, 1, .01, 2);
		NUM_DEF("Layer.ReferenceSelectedTint", Layers, "Reference", "Selected Tint", Layer.ReferenceSelectedTint, 0, 1, .01, 2);
		NUM_DEF("Layer.ReferenceHoverTint", Layers, "Reference", "Hover Tint", Layer.ReferenceHoverTint, 0, 1, .01, 2);
		NUM_DEF("Layer.ReferenceRestTint", Layers, "Reference", "Rest Tint", Layer.ReferenceRestTint, 0, 1, .01, 2);
		NUM_DEF("Layer.InstanceSourceLeftTint", Layers, "Reference", "Instance Left Tint", Layer.InstanceSourceLeftTint, 0, 1, .01, 2);
		NUM_DEF("Layer.InstanceSourceRightTint", Layers, "Reference", "Instance Right Tint", Layer.InstanceSourceRightTint, 0, 1, .01, 2);
		NUM_DEF("Layer.HairlineWidth", Layers, "Rows", "Hairline Width", Layer.HairlineWidth, 0, 4, .25, 2);
		NUM_DEF("Layer.HairlineOpacity", Layers, "Rows", "Hairline Opacity", Layer.HairlineOpacity, 0, 1, .01, 2);
		NUM_DEF("Layer.GroupSaturation", Layers, "Group", "Group Saturation", Layer.GroupSaturation, 0, 4, .05, 2);
		NUM_DEF("Layer.GroupStrength", Layers, "Group", "Group Strength", Layer.GroupStrength, 0, 1, .01, 2);
		NUM_DEF("Layer.ChildSaturation", Layers, "Child", "Child Saturation", Layer.ChildSaturation, 0, 4, .05, 2);
		NUM_DEF("Layer.ChildHoverSaturation", Layers, "Child", "Child Hover Saturation", Layer.ChildHoverSaturation, 0, 4, .05, 2);
		NUM_DEF("Layer.ChildSelectedSaturation", Layers, "Child", "Child Selected Saturation", Layer.ChildSelectedSaturation, 0, 4, .05, 2);
		NUM_DEF("Layer.ChildLeftOpacity", Layers, "Child", "Child Left Opacity", Layer.ChildLeftOpacity, 0, 1, .01, 2);
		NUM_DEF("Layer.ChildHoverLeftOpacity", Layers, "Child", "Hover Left Opacity", Layer.ChildHoverLeftOpacity, 0, 1, .01, 2);
		NUM_DEF("Layer.ChildSelectedLeftOpacity", Layers, "Child", "Selected Left Opacity", Layer.ChildSelectedLeftOpacity, 0, 1, .01, 2);
		NUM_DEF("Layer.ChildStrength", Layers, "Child", "Child Strength", Layer.ChildStrength, 0, 1, .01, 2);
		NUM_DEF("Layer.ChildHoverStrength", Layers, "Child", "Child Hover Strength", Layer.ChildHoverStrength, 0, 1, .01, 2);
		NUM_DEF("Layer.ChildSelectedStrength", Layers, "Child", "Child Selected Strength", Layer.ChildSelectedStrength, 0, 1, .01, 2);
		NUM_DEF("Layer.ActiveGlow.Opacity", Layers, "Active", "Glow Opacity", Layer.ActiveGlow.Opacity, 0, 1, .01, 2);
		NUM_DEF("Layer.ActiveGlow.Saturation", Layers, "Active", "Glow Saturation", Layer.ActiveGlow.Saturation, 0, 4, .05, 2);
		NUM_DEF("Layer.ActiveGlow.Reach", Layers, "Active", "Glow Reach", Layer.ActiveGlow.Reach, 0, 128, 1, 0);
		NUM_DEF("Layer.ActiveHairlineWidth", Layers, "Active", "Active Hairline Width", Layer.ActiveHairlineWidth, 0, 4, .25, 2);
		NUM_DEF("Layer.ActiveHairlineOpacity", Layers, "Active", "Active Hairline Opacity", Layer.ActiveHairlineOpacity, 0, 1, .01, 2);
		NUM("LayerHierarchy.Indent", Layers, "Hierarchy", "Indent", LayerHierarchy.Indent, 0, 80, 1, 0, EMixtormatThemeRefreshMode::StyleRefresh);
		NUM("LayerHierarchy.Width", Layers, "Hierarchy", "Line Width", LayerHierarchy.Width, 0, 4, .25, 2, EMixtormatThemeRefreshMode::StyleRefresh);
		NUM("LayerHierarchy.Opacity", Layers, "Hierarchy", "Opacity", LayerHierarchy.Opacity, 0, 1, .01, 2, EMixtormatThemeRefreshMode::StyleRefresh);
		NUM("LayerHierarchy.ParentJoinOffset", Layers, "Hierarchy", "Parent Join Offset", LayerHierarchy.ParentJoinOffset, -32, 32, .5, 1, EMixtormatThemeRefreshMode::StyleRefresh);
		NUM("LayerHierarchy.ChildArmLength", Layers, "Hierarchy", "Child Arm Length", LayerHierarchy.ChildArmLength, 0, 32, .5, 1, EMixtormatThemeRefreshMode::StyleRefresh);
		NUM("LayerLayout.RowHeight", Layers, "Layout", "Row Height", LayerLayout.RowHeight, 16, 64, 1, 0, EMixtormatThemeRefreshMode::StyleRefresh);
		NUM("LayerLayout.GroupRowHeight", Layers, "Layout", "Group Row Height", LayerLayout.GroupRowHeight, 12, 48, 1, 0, EMixtormatThemeRefreshMode::StyleRefresh);
		NUM("LayerLayout.ChildRowHeight", Layers, "Layout", "Child Row Height", LayerLayout.ChildRowHeight, 12, 48, 1, 0, EMixtormatThemeRefreshMode::StyleRefresh);
		NUM("LayerLayout.Gap", Layers, "Layout", "Gap", LayerLayout.Gap, 0, 24, .5, 1, EMixtormatThemeRefreshMode::StyleRefresh);
		NUM("LayerLayout.ColumnGutter", Layers, "Layout", "Column Gutter", LayerLayout.ColumnGutter, 0, 32, .5, 1, EMixtormatThemeRefreshMode::StyleRefresh);
		NUM("LayerLayout.PaddingX", Layers, "Layout", "Padding X", LayerLayout.PaddingX, 0, 32, .5, 1, EMixtormatThemeRefreshMode::StyleRefresh);
		NUM("LayerLayout.ThumbnailSize", Layers, "Layout", "Thumbnail Size", LayerLayout.ThumbnailSize, 8, 64, 1, 0, EMixtormatThemeRefreshMode::StyleRefresh);
		NUM("LayerLayout.ItemGap", Layers, "Layout", "Item Gap", LayerLayout.ItemGap, 0, 24, .5, 1, EMixtormatThemeRefreshMode::StyleRefresh);
		NUM("LayerLayout.ChildIndent", Layers, "Layout", "Child Indent", LayerLayout.ChildIndent, 0, 80, 1, 0, EMixtormatThemeRefreshMode::StyleRefresh);
		NUM("LayerConnections.Indent", Layers, "Connections", "Indent", LayerConnections.Indent, 0, 40, 1, 0, EMixtormatThemeRefreshMode::StyleRefresh);
		NUM("LayerConnections.Inset", Layers, "Connections", "Inset", LayerConnections.Inset, 0, 12, 1, 0, EMixtormatThemeRefreshMode::StyleRefresh);
		NUM("LayerConnections.TextGap", Layers, "Connections", "Text Gap", LayerConnections.TextGap, 0, 12, .5, 1, EMixtormatThemeRefreshMode::StyleRefresh);
		NUM("LayerConnections.PickerWidth", Layers, "Connections", "Picker Width", LayerConnections.PickerWidth, 220, 480, 1, 0, EMixtormatThemeRefreshMode::StyleRefresh);
		NUM("LayerConnections.PickerListMaxHeight", Layers, "Connections", "Picker List Max Height", LayerConnections.PickerListMaxHeight, 100, 600, 1, 0, EMixtormatThemeRefreshMode::StyleRefresh);
		SetLocateTarget(P, LocateBegin, ETarget::Layer);

// BUTTONS
		LocateBegin = P.Num();
		NUM("Button.Height", Buttons, "Body", "Height", Button.Height, 12, 48, 1, 0, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("Button.HorizontalPadding", Buttons, "Body", "Horizontal Padding", Button.HorizontalPadding, 0, 32, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
		BLEND_DEF("Button.BodyBlend", Buttons, "Body", "Body Blend", Button.BodyBlend);
		BLEND_DEF("Button.HairlineBlend", Buttons, "Hairline", "Hairline Blend", Button.HairlineBlend);
		NUM_DEF("Button.RestTop", Buttons, "Body", "Rest Top", Button.RestTop, 0, 1, .01, 2);
		NUM_DEF("Button.RestBottom", Buttons, "Body", "Rest Bottom", Button.RestBottom, 0, 1, .01, 2);
		NUM_DEF("Button.HoverTop", Buttons, "Body", "Hover Top", Button.HoverTop, 0, 1, .01, 2);
		NUM_DEF("Button.HoverBottom", Buttons, "Body", "Hover Bottom", Button.HoverBottom, 0, 1, .01, 2);
		NUM_DEF("Button.SelectedTop", Buttons, "Body", "Selected Top", Button.SelectedTop, 0, 1, .01, 2);
		NUM_DEF("Button.SelectedBottom", Buttons, "Body", "Selected Bottom", Button.SelectedBottom, 0, 1, .01, 2);
		NUM_DEF("Button.GradientSaturation", Buttons, "Body", "Gradient Saturation", Button.GradientSaturation, 0, 4, .05, 2);
		NUM_DEF("Button.HairlineWidth", Buttons, "Hairline", "Hairline Width", Button.HairlineWidth, 0, 4, .25, 2);
		NUM_DEF("Button.HairlineOpacity", Buttons, "Hairline", "Rest Opacity", Button.HairlineOpacity, 0, 1, .01, 2);
		NUM_DEF("Button.HairlineHoverOpacity", Buttons, "Hairline", "Hover Opacity", Button.HairlineHoverOpacity, 0, 1, .01, 2);
		NUM_DEF("Button.HairlineSelectedOpacity", Buttons, "Hairline", "Selected Opacity", Button.HairlineSelectedOpacity, 0, 1, .01, 2);
		NUM_DEF("Button.HairlineSaturation", Buttons, "Hairline", "Saturation", Button.HairlineSaturation, 0, 4, .05, 2);
		NUM_DEF("Button.SeparatorWidth", Buttons, "Separator", "Width", Button.SeparatorWidth, 0, 4, .25, 2);
		NUM_DEF("Button.SeparatorHeight", Buttons, "Separator", "Height", Button.SeparatorHeight, 0, 32, 1, 0);
		NUM_DEF("Button.SeparatorOpacity", Buttons, "Separator", "Opacity", Button.SeparatorOpacity, 0, 1, .01, 2);
		NUM_DEF("Button.TextOpacity", Buttons, "Text", "Text Opacity", Button.TextOpacity, 0, 1, .01, 2);
		SetLocateTarget(P, LocateBegin, ETarget::Button);

// MENUS
		LocateBegin = P.Num();
		COL_DEF("Menu.LipSource", Menus, "Surface", "Lip Source", Menu.LipSource);
		COL_DEF("Menu.DestructiveText", Menus, "Surface", "Destructive Text", Menu.DestructiveText);
		COL_DEF("Menu.DestructiveHover", Menus, "Surface", "Destructive Hover", Menu.DestructiveHover);
		NUM_DEF("Menu.LipTintOpacity", Menus, "Surface", "Lip Tint Opacity", Menu.LipTintOpacity, 0, 1, .01, 2);
		NUM_DEF("Menu.BorderOpacity", Menus, "Surface", "Border Opacity", Menu.BorderOpacity, 0, 1, .01, 2);
		NUM_DEF("Menu.ItemHoverOpacity", Menus, "Rows", "Hover Opacity", Menu.ItemHoverOpacity, 0, 1, .01, 2);
		NUM_DEF("Menu.ItemCheckedOpacity", Menus, "Rows", "Checked Opacity", Menu.ItemCheckedOpacity, 0, 1, .01, 2);
		NUM_DEF("Menu.ItemDisabledOpacity", Menus, "Rows", "Disabled Opacity", Menu.ItemDisabledOpacity, 0, 1, .01, 2);
		NUM_DEF("Menu.CornerRadius", Menus, "Surface", "Corner Radius", Menu.CornerRadius, 0, 12, .5, 1);
		NUM("MenuLayout.Width", Menus, "Layout", "Width", MenuLayout.Width, 120, 480, 1, 0, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("MenuLayout.LipHeight", Menus, "Layout", "Lip Height", MenuLayout.LipHeight, 4, 80, 1, 0, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("MenuLayout.RowHeight", Menus, "Layout", "Row Height", MenuLayout.RowHeight, 12, 48, 1, 0, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("MenuLayout.ItemInset", Menus, "Layout", "Item Inset", MenuLayout.ItemInset, 0, 24, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("MenuLayout.ItemGap", Menus, "Layout", "Item Gap", MenuLayout.ItemGap, 0, 24, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("MenuLayout.PanelPadding", Menus, "Layout", "Panel Padding", MenuLayout.PanelPadding, 0, 24, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("MenuLayout.CaptionInsetAbove", Menus, "Layout", "Caption Inset Above", MenuLayout.CaptionInsetAbove, 0, 24, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("MenuLayout.CaptionInsetBelow", Menus, "Layout", "Caption Inset Below", MenuLayout.CaptionInsetBelow, 0, 24, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("MenuLayout.SeparatorMargin", Menus, "Layout", "Separator Margin", MenuLayout.SeparatorMargin, 0, 24, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
		NUM("MenuLayout.ChevronSize", Menus, "Layout", "Chevron Size", MenuLayout.ChevronSize, 4, 32, 1, 0, EMixtormatThemeRefreshMode::Reconstruct);
		SetLocateTarget(P, LocateBegin, ETarget::Menu);

				// PREVIEW
				LocateBegin = P.Num();
				COL("Preview.PlateSource", Preview, "Overlay Plate", "Plate Source", Preview.PlateSource, EMixtormatThemeRefreshMode::Reconstruct);
				NUM("Preview.PlateOpacity", Preview, "Overlay Plate", "Plate Opacity", Preview.PlateOpacity, 0, 1, .01, 2, EMixtormatThemeRefreshMode::Reconstruct);
				NUM("Preview.IconRestOpacity", Preview, "Overlay Plate", "Icon / Label Rest Opacity", Preview.IconRestOpacity, 0, 1, .01, 2, EMixtormatThemeRefreshMode::Reconstruct);
				NUM("Preview.HoverAccent", Preview, "Overlay Plate", "Hover Accent", Preview.HoverAccent, 0, 1, .01, 2, EMixtormatThemeRefreshMode::Reconstruct);
				NUM("Preview.PressAccent", Preview, "Overlay Plate", "Press Accent", Preview.PressAccent, 0, 1, .01, 2, EMixtormatThemeRefreshMode::Reconstruct);
				NUM("PreviewLayout.OverlayInset", Preview, "Layout", "Overlay Inset", PreviewLayout.OverlayInset, 0, 48, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
				NUM("PreviewLayout.OverlayClusterInset", Preview, "Layout", "Cluster Inset", PreviewLayout.OverlayClusterInset, 0, 24, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);

				NUM("PreviewLayout.ToolbarGap", Preview, "Layout", "Toolbar Gap", PreviewLayout.ToolbarGap, 0, 24, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
				NUM("PreviewLayout.OverlayButtonGap", Preview, "Layout", "Button Gap", PreviewLayout.OverlayButtonGap, 0, 24, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
				NUM("PreviewLayout.ComparisonToggleGap", Preview, "Layout", "Comparison Gap", PreviewLayout.ComparisonToggleGap, 0, 24, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
				NUM("PreviewLayout.ResolutionControlWidth", Preview, "Layout", "Resolution Width", PreviewLayout.ResolutionControlWidth, 40, 240, 1, 0, EMixtormatThemeRefreshMode::Reconstruct);
				NUM("PreviewLayout.TogglePadding", Preview, "Layout", "Toggle Padding", PreviewLayout.TogglePadding, 0, 16, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
				NUM("PreviewLayout.FinalPopupWidth", Preview, "Layout", "Final Popup Width", PreviewLayout.FinalPopupWidth, 160, 420, 1, 0, EMixtormatThemeRefreshMode::Reconstruct);
				NUM("PreviewLayout.LeftRailButtonGap", Preview, "Layout", "Rail Button Gap", PreviewLayout.LeftRailButtonGap, 0, 24, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
				NUM("PreviewLayout.LeftOverlayWidth", Preview, "Layout", "Layers Pop-out Initial Width", PreviewLayout.LeftOverlayWidth, 260, 720, 4, 0, EMixtormatThemeRefreshMode::Reconstruct);
				NUM("PreviewLayout.LeftOverlaySurfaceOpacity", Preview, "Layout", "Layers Pop-out Surface Opacity", PreviewLayout.LeftOverlaySurfaceOpacity, 0, 1, .01, 2, EMixtormatThemeRefreshMode::Paint);
				NUM("PreviewLayout.QuickControlsCentreGap", Preview, "Layout", "Quick Controls Centre Gap", PreviewLayout.QuickControlsCentreGap, 80, 400, 2, 0, EMixtormatThemeRefreshMode::Reconstruct);
				NUM("PreviewLayout.QuickControlsRowGap", Preview, "Layout", "Quick Controls Row Gap", PreviewLayout.QuickControlsRowGap, 0, 80, 1, 0, EMixtormatThemeRefreshMode::Reconstruct);
				NUM("PreviewLayout.QuickControlsFadeStartDistance", Preview, "Layout", "Quick Controls Fade Start Distance", PreviewLayout.QuickControlsFadeStartDistance, 0, 300, 4, 0, EMixtormatThemeRefreshMode::Paint);
								NUM("PreviewLayout.QuickControlsFadeRange", Preview, "Layout", "Quick Controls Fade Range", PreviewLayout.QuickControlsFadeRange, 1, 600, 4, 0, EMixtormatThemeRefreshMode::Paint);
								NUM("PreviewLayout.QuickControlsGuideAxisLength", Preview, "Layout", "Quick Controls Guide Length", PreviewLayout.QuickControlsGuideAxisLength, 0, 400, 2, 0, EMixtormatThemeRefreshMode::Paint);
				NUM("PreviewLayout.QuickControlsGuideAxisThickness", Preview, "Layout", "Quick Controls Guide Thickness", PreviewLayout.QuickControlsGuideAxisThickness, .25, 4, .25, 2, EMixtormatThemeRefreshMode::Paint);
				NUM("PreviewLayout.QuickControlsGuideAxisOpacity", Preview, "Layout", "Quick Controls Guide Opacity", PreviewLayout.QuickControlsGuideAxisOpacity, 0, 1, .01, 2, EMixtormatThemeRefreshMode::Paint);
				NUM("PreviewLayout.QuickControlsGuideGlowDiameter", Preview, "Layout", "Marking Menu Vignette Diameter", PreviewLayout.QuickControlsGuideGlowDiameter, 0, 1200, 2, 0, EMixtormatThemeRefreshMode::Paint);
				NUM("PreviewLayout.QuickControlsGuideGlowOpacity", Preview, "Layout", "Marking Menu Vignette Darkness", PreviewLayout.QuickControlsGuideGlowOpacity, 0, 1, .001, 3, EMixtormatThemeRefreshMode::Paint);
				SetLocateTarget(P, LocateBegin, ETarget::Preview);

				// GALLERY / SHELL. TileSize intentionally omitted: runtime zoom owns it after construction.
				LocateBegin = P.Num();
				NUM_DEF("Gallery.BorderWidth", GalleryShell, "Gallery Surface", "Border Width", Gallery.BorderWidth, 0, 4, .25, 2);
				NUM_DEF("Gallery.BorderOpacity", GalleryShell, "Gallery Surface", "Border Opacity", Gallery.BorderOpacity, 0, 1, .01, 2);
				NUM_DEF("Gallery.HoverLiftOpacity", GalleryShell, "Gallery Surface", "Hover Lift", Gallery.HoverLiftOpacity, 0, 1, .01, 2);
				NUM_DEF("Gallery.SelectedEdgeWidth", GalleryShell, "Gallery Surface", "Selected Edge Width", Gallery.SelectedEdgeWidth, 0, 4, .25, 2);
				NUM_DEF("Gallery.SelectedEdgeOpacity", GalleryShell, "Gallery Surface", "Selected Edge Opacity", Gallery.SelectedEdgeOpacity, 0, 1, .01, 2);
				NUM_DEF("Gallery.CornerRadius", GalleryShell, "Gallery Surface", "Corner Radius", Gallery.CornerRadius, 0, 12, .5, 1);
				NUM("GalleryLayout.TileGap", GalleryShell, "Gallery Layout", "Tile Gap", GalleryLayout.TileGap, 0, 24, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
				NUM("GalleryLayout.TilePadding", GalleryShell, "Gallery Layout", "Tile Padding", GalleryLayout.TilePadding, 0, 24, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
				NUM("GalleryLayout.CaptionHeight", GalleryShell, "Gallery Layout", "Caption Height", GalleryLayout.CaptionHeight, 0, 32, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
				NUM("GalleryLayout.CaptionInset", GalleryShell, "Gallery Layout", "Caption Inset", GalleryLayout.CaptionInset, 0, 24, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
				NUM("GalleryLayout.OverlayInset", GalleryShell, "Gallery Layout", "Overlay Inset", GalleryLayout.OverlayInset, 0, 24, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
				NUM("GalleryLayout.HeaderGap", GalleryShell, "Gallery Layout", "Header Gap", GalleryLayout.HeaderGap, 0, 24, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
				NUM("GalleryLayout.MaskHeaderInset", GalleryShell, "Gallery Layout", "Masks Header Inset", GalleryLayout.MaskHeaderInset, 0, 32, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
				NUM("GalleryLayout.DrawerAutoCollapseDistance", GalleryShell, "Gallery Layout", "Drawer Auto-Collapse Distance", GalleryLayout.DrawerAutoCollapseDistance, 0, 32, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
				NUM("GalleryLayout.DrawerInset", GalleryShell, "Gallery Layout", "Drawer Inset", GalleryLayout.DrawerInset, 0, 48, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
				NUM("GalleryLayout.DrawerSideInset", GalleryShell, "Gallery Layout", "Drawer Side Inset", GalleryLayout.DrawerSideInset, 0, 96, 1, 0, EMixtormatThemeRefreshMode::Reconstruct);
				NUM("GalleryLayout.DrawerHeaderHeight", GalleryShell, "Gallery Layout", "Drawer Header Height", GalleryLayout.DrawerHeaderHeight, 14, 32, 1, 0, EMixtormatThemeRefreshMode::Reconstruct);
				NUM("GalleryLayout.DrawerCollapsedHeight", GalleryShell, "Gallery Layout", "Drawer Collapsed Height", GalleryLayout.DrawerCollapsedHeight, 14, 32, 1, 0, EMixtormatThemeRefreshMode::Reconstruct);
				NUM("GalleryLayout.DrawerInitialHeight", GalleryShell, "Gallery Layout", "Drawer Initial Height", GalleryLayout.DrawerInitialHeight, 120, 640, 4, 0, EMixtormatThemeRefreshMode::Reconstruct);
				NUM("GalleryLayout.ModeSwitchGap", GalleryShell, "Gallery Layout", "Mode Switch Gap", GalleryLayout.ModeSwitchGap, 0, 24, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
				NUM("GalleryLayout.DrawerSurfaceOpacity", GalleryShell, "Gallery Surface", "Drawer Surface Opacity", GalleryLayout.DrawerSurfaceOpacity, 0, 1, .01, 2, EMixtormatThemeRefreshMode::Paint);
				SetLocateTarget(P, LocateBegin, ETarget::Gallery);

				LocateBegin = P.Num();
				COL_DEF("ShellTheme.SplitterHoverSource", GalleryShell, "Shell / Splitter", "Hover Source", ShellTheme.SplitterHoverSource);
				NUM_DEF("ShellTheme.SplitterOpacity", GalleryShell, "Shell / Splitter", "Rest Opacity", ShellTheme.SplitterOpacity, 0, 1, .01, 2);
				NUM_DEF("ShellTheme.SplitterHoverOpacity", GalleryShell, "Shell / Splitter", "Hover Opacity", ShellTheme.SplitterHoverOpacity, 0, 1, .01, 2);
			NUM("ShellTheme.ColumnShadowOpacity", GalleryShell, "Shell / Shadow", "Opacity", ShellTheme.ColumnShadowOpacity, 0, 1, .01, 2, EMixtormatThemeRefreshMode::Reconstruct);
			NUM("ShellTheme.ColumnShadowRange", GalleryShell, "Shell / Shadow", "Range", ShellTheme.ColumnShadowRange, 0, 64, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
			NUM("ShellTheme.ColumnShadowFalloffPower", GalleryShell, "Shell / Shadow", "Falloff Power", ShellTheme.ColumnShadowFalloffPower, .01, 4, .05, 2, EMixtormatThemeRefreshMode::Reconstruct);
				NUM("Shell.TopBarHeight", GalleryShell, "Shell / Layout", "Top Bar Height", Shell.TopBarHeight, 20, 64, 1, 0, EMixtormatThemeRefreshMode::Reconstruct);
				NUM("Shell.TopBarActionInset", GalleryShell, "Shell / Layout", "Top Bar Action Inset", Shell.TopBarActionInset, 0, 12, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
				NUM("Shell.StatusBarHeight", GalleryShell, "Shell / Layout", "Status Bar Height", Shell.StatusBarHeight, 12, 48, 1, 0, EMixtormatThemeRefreshMode::Reconstruct);
				NUM("Shell.PanelPadding", GalleryShell, "Shell / Layout", "Panel Padding", Shell.PanelPadding, 0, 32, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
				NUM("Shell.ScrollbarThickness", GalleryShell, "Shell / Scrollbar", "Thickness", Shell.ScrollbarThickness, 2, 12, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
				NUM_DEF("Shell.ScrollbarThumbOpacity", GalleryShell, "Shell / Scrollbar", "Thumb Opacity", Shell.ScrollbarThumbOpacity, 0, 1, .01, 2);
				NUM_DEF("Shell.ScrollbarHoverOpacity", GalleryShell, "Shell / Scrollbar", "Hover Opacity", Shell.ScrollbarHoverOpacity, 0, 1, .01, 2);
				NUM("Shell.SplitterVisualWidth", GalleryShell, "Shell / Layout", "Splitter Visual Width", Shell.SplitterVisualWidth, 0, 12, .25, 2, EMixtormatThemeRefreshMode::Reconstruct);
				NUM("Shell.SplitterHitWidth", GalleryShell, "Shell / Layout", "Splitter Hit Width", Shell.SplitterHitWidth, 2, 24, .5, 1, EMixtormatThemeRefreshMode::Reconstruct);
				SetLocateTarget(P, LocateBegin, ETarget::Shell);

				AddTextRole(P, EMixtormatTextRole::Body, TEXT("Body"), TEXT("Body"));
				AddTextRole(P, EMixtormatTextRole::ControlLabel, TEXT("ControlLabel"), TEXT("Control Label"));
				AddTextRole(P, EMixtormatTextRole::ControlValue, TEXT("ControlValue"), TEXT("Control Value"));
				AddTextRole(P, EMixtormatTextRole::Caption, TEXT("Caption"), TEXT("Caption"));
				AddTextRole(P, EMixtormatTextRole::FoldoutTitle, TEXT("FoldoutTitle"), TEXT("Foldout Title"));
				AddTextRole(P, EMixtormatTextRole::CardTitle, TEXT("CardTitle"), TEXT("Card Title"));
				AddTextRole(P, EMixtormatTextRole::LayerName, TEXT("LayerName"), TEXT("Layer Name"));
				AddTextRole(P, EMixtormatTextRole::LayerSource, TEXT("LayerSource"), TEXT("Layer Source"));
				AddTextRole(P, EMixtormatTextRole::Menu, TEXT("Menu"), TEXT("Menu"));
				AddTextRole(P, EMixtormatTextRole::MenuShortcut, TEXT("MenuShortcut"), TEXT("Menu Shortcut"));
				AddTextRole(P, EMixtormatTextRole::GalleryCaption, TEXT("GalleryCaption"), TEXT("Gallery Caption"));
				AddTextRole(P, EMixtormatTextRole::TopBar, TEXT("TopBar"), TEXT("Top Bar"));
				AddTextRole(P, EMixtormatTextRole::PreviewLabel, TEXT("PreviewLabel"), TEXT("Preview Label"));
				AddTextRole(P, EMixtormatTextRole::Badge, TEXT("Badge"), TEXT("Badge"));

#undef BLEND
#undef COL
#undef NUM
				return P;
			}();
			return Built;
		}

		bool IsFiniteColor(const FLinearColor& C)
		{
			return FMath::IsFinite(C.R) && FMath::IsFinite(C.G) && FMath::IsFinite(C.B) && FMath::IsFinite(C.A)
				&& C.R >= 0.0f && C.R <= 1.0f
				&& C.G >= 0.0f && C.G <= 1.0f
				&& C.B >= 0.0f && C.B <= 1.0f
				&& C.A >= 0.0f && C.A <= 1.0f;
		}
	}

	const TArray<FMixtormatThemeProperty>& FMixtormatThemeSchema::Properties()
	{
		return BuildProperties();
	}

	const FMixtormatThemeProperty* FMixtormatThemeSchema::Find(const FName Id)
	{
		return Properties().FindByPredicate([Id](const FMixtormatThemeProperty& P) { return P.Id == Id; });
	}

	FString FMixtormatThemeSchema::TabLabel(const EMixtormatThemeTab Tab)
	{
		switch (Tab)
		{
		case ETab::Global: return TEXT("GLOBAL");
		case ETab::Controls: return TEXT("CONTROLS");
		case ETab::Foldouts: return TEXT("FOLDOUTS");
		case ETab::Cards: return TEXT("CARDS");
		case ETab::Layers: return TEXT("LAYERS");
		case ETab::Buttons: return TEXT("BUTTONS");
		case ETab::Menus: return TEXT("MENUS");
		case ETab::Preview: return TEXT("PREVIEW");
		case ETab::GalleryShell: return TEXT("GALLERY / SHELL");
		case ETab::Typography: return TEXT("TYPOGRAPHY");
		default: return TEXT("UNKNOWN");
		}
	}

	FString FMixtormatThemeSchema::TabKey(const EMixtormatThemeTab Tab)
	{
		switch (Tab)
		{
		case ETab::Global: return TEXT("global");
		case ETab::Controls: return TEXT("controls");
		case ETab::Foldouts: return TEXT("foldouts");
		case ETab::Cards: return TEXT("cards");
		case ETab::Layers: return TEXT("layers");
		case ETab::Buttons: return TEXT("buttons");
		case ETab::Menus: return TEXT("menus");
		case ETab::Preview: return TEXT("preview");
		case ETab::GalleryShell: return TEXT("galleryShell");
		case ETab::Typography: return TEXT("typography");
		default: return TEXT("unknown");
		}
	}

	FString FMixtormatThemeSchema::SavePath()
	{
		const FString PluginDir = FMixtormatPaths::PluginBaseDir();
		return PluginDir.IsEmpty()
			? FString()
			: FPaths::Combine(PluginDir, TEXT("Config"), TEXT("UIStyleTheme.json"));
	}

	bool FMixtormatThemeSchema::Save(FString& OutError)
	{
		OutError.Reset();
		const FMixtormatTheme& Theme = FMixtormatThemeStore::GetTheme();
		const FString Path = SavePath();

		// Retain unrecognized properties from a previously saved compatible theme. This lets a
		// newer or temporarily unavailable schema survive a save from this build unchanged.
		TSharedPtr<FJsonObject> ExistingRoot;
		FString ExistingText;
		if (!Path.IsEmpty() && FFileHelper::LoadFileToString(ExistingText, *Path))
		{
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ExistingText), ExistingRoot);
			double ExistingVersion = 0.0;
			FString ExistingSchema;
			if (!ExistingRoot.IsValid()
				|| !ExistingRoot->TryGetNumberField(TEXT("version"), ExistingVersion)
				|| ExistingVersion != 1.0
				|| !ExistingRoot->TryGetStringField(TEXT("schema"), ExistingSchema)
				|| ExistingSchema != TEXT("MixtormatUIStyle"))
			{
				ExistingRoot.Reset();
			}
		}

		const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetNumberField(TEXT("version"), 1);
		Root->SetStringField(TEXT("schema"), TEXT("MixtormatUIStyle"));

		TMap<ETab, TSharedPtr<FJsonObject>> Sections;
		for (uint8 I = 0; I < static_cast<uint8>(ETab::Count); ++I)
		{
			const ETab Tab = static_cast<ETab>(I);
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			const TSharedPtr<FJsonObject>* ExistingSection = nullptr;
			if (ExistingRoot.IsValid()
				&& ExistingRoot->TryGetObjectField(TabKey(Tab), ExistingSection)
				&& ExistingSection && ExistingSection->IsValid())
			{
				Obj = *ExistingSection;
			}
			Sections.Add(Tab, Obj);
			Root->SetObjectField(TabKey(Tab), Obj);
		}

		for (const FMixtormatThemeProperty& P : Properties())
		{
			TSharedPtr<FJsonObject> Obj = Sections.FindRef(P.Tab);
			if (!Obj.IsValid())
			{
				continue;
			}
			const FString Key = P.Id.ToString();
			switch (P.Kind)
			{
			case EKind::Number:
				Obj->SetNumberField(Key, P.GetNumber(Theme));
				break;
			case EKind::Bool:
				Obj->SetBoolField(Key, P.GetBool(Theme));
				break;
			case EKind::Choice:
			{
				const int32 Index = P.GetChoice(Theme);
				Obj->SetStringField(Key, P.Options.IsValidIndex(Index) ? P.Options[Index] : FString());
				break;
			}
			case EKind::Color:
			{
				const FLinearColor C = P.GetColor(Theme);
				Obj->SetArrayField(Key, {
					MakeShared<FJsonValueNumber>(C.R),
					MakeShared<FJsonValueNumber>(C.G),
					MakeShared<FJsonValueNumber>(C.B),
					MakeShared<FJsonValueNumber>(C.A)
				});
				break;
			}
			}
		}

		FString Text;
		FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text));
		if (Path.IsEmpty())
		{
			OutError = TEXT("Could not resolve the Mixtormat plugin directory.");
			return false;
		}
		const FString Temp = Path + TEXT(".tmp");
		if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true)
			|| !FFileHelper::SaveStringToFile(Text, *Temp)
			|| !IFileManager::Get().Move(*Path, *Temp, true, true))
		{
			OutError = FString::Printf(TEXT("Could not save UI style theme to %s"), *Path);
			return false;
		}
		return true;
	}

	bool FMixtormatThemeSchema::Load(FString& OutError, TArray<FText>& OutValidationIssues)
	{
		OutError.Reset();
		OutValidationIssues.Reset();

		const FString Path = SavePath();
		if (Path.IsEmpty())
		{
			OutError = TEXT("Could not resolve the Mixtormat plugin directory.");
			return false;
		}

		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			OutError = FString::Printf(TEXT("Could not read %s. Save a UI style theme first."), *Path);
			return false;
		}

		TSharedPtr<FJsonObject> Root;
		double Version = 0.0;
		FString Schema;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root)
			|| !Root.IsValid()
			|| !Root->TryGetNumberField(TEXT("version"), Version)
			|| Version != 1.0
			|| !Root->TryGetStringField(TEXT("schema"), Schema)
			|| Schema != TEXT("MixtormatUIStyle"))
		{
			OutError = TEXT("Invalid UI style theme. Expected MixtormatUIStyle version 1.");
			return false;
		}

		// Merge saved schema values over the current theme so properties intentionally outside the
		// editable/save schema are not silently reset when loading an older theme file.
		FMixtormatTheme Pending = FMixtormatThemeStore::GetTheme();
		for (const FMixtormatThemeProperty& P : Properties())
		{
			const TSharedPtr<FJsonObject>* Section = nullptr;
			if (!Root->TryGetObjectField(TabKey(P.Tab), Section) || !Section || !Section->IsValid())
			{
				continue;
			}
			const FString PropertyId = P.Id.ToString();
			if (!(*Section)->HasField(PropertyId))
			{
				continue;
			}

			switch (P.Kind)
			{
			case EKind::Number:
			{
				double NumberValue = 0.0;
				if (!(*Section)->TryGetNumberField(PropertyId, NumberValue) || !FMath::IsFinite(NumberValue)
					|| NumberValue < P.Minimum || NumberValue > P.Maximum)
				{
					OutError = FString::Printf(TEXT("Invalid numeric value for %s"), *PropertyId);
					return false;
				}
				P.SetNumber(Pending, static_cast<float>(NumberValue));
				break;
			}
			case EKind::Bool:
			{
				bool BoolValue = false;
				if (!(*Section)->TryGetBoolField(PropertyId, BoolValue))
				{
					OutError = FString::Printf(TEXT("Invalid boolean value for %s"), *PropertyId);
					return false;
				}
				P.SetBool(Pending, BoolValue);
				break;
			}
			case EKind::Choice:
			{
				FString ChoiceValue;
				if (!(*Section)->TryGetStringField(PropertyId, ChoiceValue))
				{
					OutError = FString::Printf(TEXT("Invalid choice value for %s"), *PropertyId);
					return false;
				}
				const int32 Index = P.Options.IndexOfByKey(ChoiceValue);
				if (Index == INDEX_NONE)
				{
					OutError = FString::Printf(TEXT("Unknown choice '%s' for %s"), *ChoiceValue, *PropertyId);
					return false;
				}
				P.SetChoice(Pending, Index);
				break;
			}
			case EKind::Color:
			{
				const TArray<TSharedPtr<FJsonValue>>* Channels = nullptr;
				if (!(*Section)->TryGetArrayField(PropertyId, Channels) || !Channels || Channels->Num() != 4)
				{
					OutError = FString::Printf(TEXT("Invalid color value for %s"), *PropertyId);
					return false;
				}
				float C[4] = {};
				for (int32 I = 0; I < 4; ++I)
				{
					double Channel = 0.0;
					if (!(*Channels)[I]->TryGetNumber(Channel) || !FMath::IsFinite(Channel))
					{
						OutError = FString::Printf(TEXT("Invalid color channel for %s"), *PropertyId);
						return false;
					}
					C[I] = static_cast<float>(Channel);
				}
				const FLinearColor ColorValue(C[0], C[1], C[2], C[3]);
				if (!IsFiniteColor(ColorValue))
				{
					OutError = FString::Printf(TEXT("Color channels must be in 0..1 for %s"), *P.Id.ToString());
					return false;
				}
				P.SetColor(Pending, ColorValue);
				break;
			}
			}
		}

		FMixtormatThemeStore::SetTheme(MoveTemp(Pending));
		OutValidationIssues = FMixtormatThemeStore::GetValidationIssues();
		return true;
	}
}
