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
	namespace
	{
		using ETab = EMixtormatThemeTab;
		using EKind = EMixtormatThemePropertyKind;

		FMixtormatThemeProperty Number(
			const TCHAR* Id, const ETab Tab, const TCHAR* Section, const TCHAR* Label,
			const double Min, const double Max, const double Step, const int32 Precision,
			TFunction<float(const FMixtormatTheme&)> Get,
			TFunction<void(FMixtormatTheme&, float)> Set,
			const TCHAR* Help = TEXT(""))
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
			return P;
		}

		FMixtormatThemeProperty Bool(
			const TCHAR* Id, const ETab Tab, const TCHAR* Section, const TCHAR* Label,
			TFunction<bool(const FMixtormatTheme&)> Get,
			TFunction<void(FMixtormatTheme&, bool)> Set,
			const TCHAR* Help = TEXT(""))
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
			return P;
		}

		FMixtormatThemeProperty Choice(
			const TCHAR* Id, const ETab Tab, const TCHAR* Section, const TCHAR* Label,
			TArray<FString> Options,
			TFunction<int32(const FMixtormatTheme&)> Get,
			TFunction<void(FMixtormatTheme&, int32)> Set,
			const TCHAR* Help = TEXT(""))
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
			return P;
		}

		FMixtormatThemeProperty Color(
			const TCHAR* Id, const ETab Tab, const TCHAR* Section, const TCHAR* Label,
			TFunction<FLinearColor(const FMixtormatTheme&)> Get,
			TFunction<void(FMixtormatTheme&, const FLinearColor&)> Set,
			const TCHAR* Help = TEXT(""))
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

		void AddIconRole(
			TArray<FMixtormatThemeProperty>& Out,
			const EMixtormatIconRole Role,
			const TCHAR* Prefix,
			const TCHAR* Label)
		{
			const uint8 Index = static_cast<uint8>(Role);
			const FString Section = FString::Printf(TEXT("Icons / %s"), Label);
			auto Id = [Prefix](const TCHAR* Suffix)
			{
				return FString::Printf(TEXT("Icons.%s.%s"), Prefix, Suffix);
			};
			Out.Add(Number(*Id(TEXT("GlyphSize")), ETab::Global, *Section, TEXT("Glyph Size"), 4.0f, 64.0f, 1.0f, 0,
				[Index](const FMixtormatTheme& T) { return T.Icons.Roles[Index].GlyphSize; },
				[Index](FMixtormatTheme& T, float V) { T.Icons.Roles[Index].GlyphSize = V; }));
			Out.Add(Number(*Id(TEXT("ButtonSize")), ETab::Global, *Section, TEXT("Button Size"), 4.0f, 96.0f, 1.0f, 0,
				[Index](const FMixtormatTheme& T) { return T.Icons.Roles[Index].ButtonSize; },
				[Index](FMixtormatTheme& T, float V) { T.Icons.Roles[Index].ButtonSize = V; }));

			Out.Add(Number(*Id(TEXT("RestOpacity")), ETab::Global, *Section, TEXT("Rest Opacity"), 0.0f, 1.0f, 0.01f, 2,
				[Index](const FMixtormatTheme& T) { return T.Icons.Roles[Index].RestOpacity; },
				[Index](FMixtormatTheme& T, float V) { T.Icons.Roles[Index].RestOpacity = V; }));
			Out.Add(Number(*Id(TEXT("HoverOpacity")), ETab::Global, *Section, TEXT("Hover Opacity"), 0.0f, 1.0f, 0.01f, 2,
				[Index](const FMixtormatTheme& T) { return T.Icons.Roles[Index].HoverOpacity; },
				[Index](FMixtormatTheme& T, float V) { T.Icons.Roles[Index].HoverOpacity = V; }));
			Out.Add(Number(*Id(TEXT("DisabledOpacity")), ETab::Global, *Section, TEXT("Disabled Opacity"), 0.0f, 1.0f, 0.01f, 2,
				[Index](const FMixtormatTheme& T) { return T.Icons.Roles[Index].DisabledOpacity; },
				[Index](FMixtormatTheme& T, float V) { T.Icons.Roles[Index].DisabledOpacity = V; }));
		}

		void AddTextRole(
			TArray<FMixtormatThemeProperty>& Out,
			const EMixtormatTextRole Role,
			const TCHAR* Prefix,
			const TCHAR* Label)
		{
			const uint8 Index = static_cast<uint8>(Role);
			const FString Section = FString::Printf(TEXT("Role / %s"), Label);
			auto Id = [Prefix](const TCHAR* Suffix)
			{
				return FString::Printf(TEXT("Typography.%s.%s"), Prefix, Suffix);
			};
			Out.Add(Number(*Id(TEXT("Size")), ETab::Typography, *Section, TEXT("Size"), 6.0f, 32.0f, 0.5f, 1,
				[Index](const FMixtormatTheme& T) { return T.Typography.Roles[Index].Size; },
				[Index](FMixtormatTheme& T, float V) { T.Typography.Roles[Index].Size = V; }));
			Out.Add(Choice(*Id(TEXT("Weight")), ETab::Typography, *Section, TEXT("Weight"), WeightOptions(),
				[Index](const FMixtormatTheme& T) { return static_cast<int32>(T.Typography.Roles[Index].Weight); },
				[Index](FMixtormatTheme& T, int32 V)
				{
					T.Typography.Roles[Index].Weight = static_cast<EMixtormatFontWeight>(FMath::Clamp(V, 0, 2));
				}));
			Out.Add(Number(*Id(TEXT("TrackingPx")), ETab::Typography, *Section, TEXT("Tracking (px)"), -2.0f, 8.0f, 0.1f, 1,
				[Index](const FMixtormatTheme& T) { return T.Typography.Roles[Index].TrackingPx; },
				[Index](FMixtormatTheme& T, float V) { T.Typography.Roles[Index].TrackingPx = V; }));
			Out.Add(Number(*Id(TEXT("Opacity")), ETab::Typography, *Section, TEXT("Opacity"), 0.0f, 1.0f, 0.01f, 2,
				[Index](const FMixtormatTheme& T) { return T.Typography.Roles[Index].Opacity; },
				[Index](FMixtormatTheme& T, float V) { T.Typography.Roles[Index].Opacity = V; }));
			Out.Add(Bool(*Id(TEXT("Uppercase")), ETab::Typography, *Section, TEXT("Uppercase"),
				[Index](const FMixtormatTheme& T) { return T.Typography.Roles[Index].bUppercase; },
				[Index](FMixtormatTheme& T, bool V) { T.Typography.Roles[Index].bUppercase = V; }));
			Out.Add(Bool(*Id(TEXT("MonospacedNumbers")), ETab::Typography, *Section, TEXT("Monospaced Numbers"),
				[Index](const FMixtormatTheme& T) { return T.Typography.Roles[Index].bMonospacedNumbers; },
				[Index](FMixtormatTheme& T, bool V) { T.Typography.Roles[Index].bMonospacedNumbers = V; }));
		}

		const TArray<FMixtormatThemeProperty>& BuildProperties()
		{
			static const TArray<FMixtormatThemeProperty> Built = []()
			{
				TArray<FMixtormatThemeProperty> P;

#define NUM(Id, Tab, Section, Label, Path, Min, Max, Step, Prec) \
	P.Add(Number(TEXT(Id), ETab::Tab, TEXT(Section), TEXT(Label), Min, Max, Step, Prec, \
		[](const FMixtormatTheme& T) { return T.Path; }, \
		[](FMixtormatTheme& T, float V) { T.Path = V; }))
#define COL(Id, Tab, Section, Label, Path) \
	P.Add(Color(TEXT(Id), ETab::Tab, TEXT(Section), TEXT(Label), \
		[](const FMixtormatTheme& T) { return T.Path; }, \
		[](FMixtormatTheme& T, const FLinearColor& V) { T.Path = V; }))
#define BLEND(Id, Tab, Section, Label, Path) \
	P.Add(Choice(TEXT(Id), ETab::Tab, TEXT(Section), TEXT(Label), BlendOptions(), \
		[](const FMixtormatTheme& T) { return static_cast<int32>(T.Path); }, \
		[](FMixtormatTheme& T, int32 V) { T.Path = static_cast<MixtormatCompositing::EMixtormatBlendMode>(FMath::Clamp(V, 0, 3)); }))

				// GLOBAL / semantic palette. OverlayGround is intentionally omitted: no production reader.
				COL("Palette.Ground", Global, "Palette", "Ground", Palette.Ground);
				COL("Palette.Shell", Global, "Palette", "Shell", Palette.Shell);
				COL("Palette.Panel", Global, "Palette", "Panel", Palette.Panel);
				COL("Palette.Text", Global, "Palette", "Text", Palette.Text);
				COL("Palette.TextMuted", Global, "Palette", "Text Muted", Palette.TextMuted);
				COL("Palette.Accent", Global, "Palette", "Accent", Palette.Accent);
				COL("Palette.Modified", Global, "Palette", "Modified", Palette.Modified);
				COL("Palette.Warning", Global, "Palette", "Warning", Palette.Warning);
				COL("Palette.Error", Global, "Palette", "Error", Palette.Error);
				COL("Palette.Shade", Global, "Palette", "Shade", Palette.Shade);
				COL("Palette.Hairline", Global, "Palette", "Hairline", Palette.Hairline);
				COL("Palette.MenuGround", Global, "Palette", "Menu Ground", Palette.MenuGround);
				COL("Palette.ThumbnailGround", Global, "Palette", "Thumbnail Ground", Palette.ThumbnailGround);

				AddIconRole(P, EMixtormatIconRole::TopBar, TEXT("TopBar"), TEXT("Top Bar"));
				AddIconRole(P, EMixtormatIconRole::PanelToolbar, TEXT("PanelToolbar"), TEXT("Panel Toolbar"));
				AddIconRole(P, EMixtormatIconRole::PreviewToolbar, TEXT("PreviewToolbar"), TEXT("Preview Toolbar"));
				AddIconRole(P, EMixtormatIconRole::LayerEye, TEXT("LayerEye"), TEXT("Layer Eye"));
				AddIconRole(P, EMixtormatIconRole::LayerDisclosure, TEXT("LayerDisclosure"), TEXT("Layer Disclosure"));
				AddIconRole(P, EMixtormatIconRole::FoldoutDisclosure, TEXT("FoldoutDisclosure"), TEXT("Foldout Disclosure"));

				AddIconRole(P, EMixtormatIconRole::Menu, TEXT("Menu"), TEXT("Menu"));


				// CONTROLS
				NUM("Well.Radius", Controls, "Well", "Radius", Well.Radius, 0, 12, .5, 1);
				BLEND("Well.ShadeBlend", Controls, "Well", "Shade Blend", Well.ShadeBlend);
				NUM("Well.ShadeTop", Controls, "Well", "Shade Top", Well.ShadeTop, 0, 1, .01, 2);
				NUM("Well.ShadeBottom", Controls, "Well", "Shade Bottom", Well.ShadeBottom, 0, 1, .01, 2);
				NUM("Well.BorderWidth", Controls, "Well", "Border Width", Well.BorderWidth, 0, 4, .25, 2);
				NUM("Well.BorderOpacity", Controls, "Well", "Border Opacity", Well.BorderOpacity, 0, 1, .01, 2);
				NUM("Well.BorderTopOpacity", Controls, "Well", "Border Top Opacity", Well.BorderTopOpacity, 0, 1, .01, 2);
				NUM("Well.BorderBottomOpacity", Controls, "Well", "Border Bottom Opacity", Well.BorderBottomOpacity, 0, 1, .01, 2);
				NUM("Well.BorderHoverOpacity", Controls, "Well", "Hover Border Opacity", Well.BorderHoverOpacity, 0, 1, .01, 2);
				NUM("Well.BorderHoverTopOpacity", Controls, "Well", "Hover Border Top", Well.BorderHoverTopOpacity, 0, 1, .01, 2);
				NUM("Well.BorderHoverBottomOpacity", Controls, "Well", "Hover Border Bottom", Well.BorderHoverBottomOpacity, 0, 1, .01, 2);
				NUM("Well.BorderSaturation", Controls, "Well", "Border Saturation", Well.BorderSaturation, 0, 4, .05, 2);
				NUM("Well.HoverLiftOpacity", Controls, "Well", "Hover Lift", Well.HoverLiftOpacity, 0, 1, .01, 2);

				BLEND("Fill.BodyBlend", Controls, "Fill", "Body Blend", Fill.BodyBlend);
				BLEND("Fill.ShadeBlend", Controls, "Fill", "Shade Blend", Fill.ShadeBlend);
				NUM("Fill.Top", Controls, "Fill", "Top", Fill.Top, 0, 1, .01, 2);
				NUM("Fill.Bottom", Controls, "Fill", "Bottom", Fill.Bottom, 0, 1, .01, 2);
				NUM("Fill.HoverTop", Controls, "Fill", "Hover Top", Fill.HoverTop, 0, 1, .01, 2);
				NUM("Fill.HoverBottom", Controls, "Fill", "Hover Bottom", Fill.HoverBottom, 0, 1, .01, 2);
				NUM("Fill.ActiveTop", Controls, "Fill", "Active Top", Fill.ActiveTop, 0, 1, .01, 2);
				NUM("Fill.ActiveBottom", Controls, "Fill", "Active Bottom", Fill.ActiveBottom, 0, 1, .01, 2);
				NUM("Fill.Saturation", Controls, "Fill", "Saturation", Fill.Saturation, 0, 4, .05, 2);
				NUM("Fill.HoverSaturation", Controls, "Fill", "Hover Saturation", Fill.HoverSaturation, 0, 4, .05, 2);
				NUM("Fill.ActiveSaturation", Controls, "Fill", "Active Saturation", Fill.ActiveSaturation, 0, 4, .05, 2);
				NUM("Fill.DisabledOpacity", Controls, "Fill", "Disabled Opacity", Fill.DisabledOpacity, 0, 1, .01, 2);
				NUM("Fill.DisabledSaturation", Controls, "Fill", "Disabled Saturation", Fill.DisabledSaturation, 0, 4, .05, 2);
				NUM("Fill.ShadeStart", Controls, "Fill", "Shade Start", Fill.ShadeStart, 0, 1, .01, 2);
				NUM("Fill.ShadeMid", Controls, "Fill", "Shade Mid", Fill.ShadeMid, 0, 1, .01, 2);
				NUM("Fill.ShadeEnd", Controls, "Fill", "Shade End", Fill.ShadeEnd, 0, 1, .01, 2);
				NUM("Fill.ShadeMidPosition", Controls, "Fill", "Shade Mid Position", Fill.ShadeMidPosition, 0, 1, .01, 2);
				NUM("Fill.FalloffPower", Controls, "Fill", "Falloff Power", Fill.FalloffPower, .01, 4, .05, 2);

				NUM("Toggle.Size", Controls, "Toggle", "Size", Toggle.Size, 8, 32, 1, 0);
				NUM("Toggle.FillInset", Controls, "Toggle", "Fill Inset", Toggle.FillInset, 0, 12, .5, 1);
				NUM("Toggle.DisabledShadeTop", Controls, "Toggle", "Disabled Shade Top", Toggle.DisabledShadeTop, 0, 1, .01, 2);
				NUM("Toggle.DisabledShadeBottom", Controls, "Toggle", "Disabled Shade Bottom", Toggle.DisabledShadeBottom, 0, 1, .01, 2);

				NUM("ControlLayout.RowHeight", Controls, "Layout", "Row Height", ControlLayout.RowHeight, 12, 48, 1, 0);
				NUM("ControlLayout.RowGap", Controls, "Layout", "Row Gap", ControlLayout.RowGap, 0, 24, .5, 1);
				NUM("ControlLayout.PairedGap", Controls, "Layout", "Paired Gap", ControlLayout.PairedGap, 0, 24, .5, 1);
				NUM("ControlLayout.RowTextInset", Controls, "Layout", "Row Text Inset", ControlLayout.RowTextInset, 0, 32, .5, 1);
				NUM("ControlLayout.RowLabelGap", Controls, "Layout", "Row Label Gap", ControlLayout.RowLabelGap, 0, 32, .5, 1);
				NUM("ControlLayout.RowFieldMinWidth", Controls, "Layout", "Field Min Width", ControlLayout.RowFieldMinWidth, 0, 400, 1, 0);
				NUM("ControlLayout.ColorSwatchWidth", Controls, "Layout", "Color Swatch Width", ControlLayout.ColorSwatchWidth, 0, 240, 1, 0);
				NUM("ControlLayout.ColorSwatchHeight", Controls, "Layout", "Color Swatch Height", ControlLayout.ColorSwatchHeight, 0, 64, 1, 0);
				NUM("ControlLayout.ButtonHeight", Controls, "Layout", "Button Height", ControlLayout.ButtonHeight, 12, 48, 1, 0);
				NUM("ControlLayout.SegmentedControlGap", Controls, "Layout", "Segmented Gap", ControlLayout.SegmentedControlGap, 0, 24, .5, 1);
				NUM("ControlLayout.DropdownLabelRatio", Controls, "Layout", "Dropdown Label Ratio", ControlLayout.DropdownLabelRatio, 0, 1, .01, 2);
				NUM("ControlLayout.PanelGutter", Controls, "Layout", "Panel Gutter", ControlLayout.PanelGutter, 0, 32, .5, 1);
				NUM("ControlLayout.DisabledLabelOpacity", Controls, "Layout", "Disabled Label Opacity", ControlLayout.DisabledLabelOpacity, 0, 1, .01, 2);

				// FOLDOUTS
				COL("Foldout.LiftTint", Foldouts, "Surface", "Lift Tint", Foldout.LiftTint);
				COL("Foldout.HoverTint", Foldouts, "Surface", "Hover Tint", Foldout.HoverTint);
				COL("Foldout.HairlineHoverTint", Foldouts, "Surface", "Hover Hairline Tint", Foldout.HairlineHoverTint);
				NUM("Foldout.LiftOpacity", Foldouts, "Surface", "Lift Opacity", Foldout.LiftOpacity, 0, 1, .01, 2);
				NUM("Foldout.HoverTintOpacity", Foldouts, "Surface", "Hover Tint Opacity", Foldout.HoverTintOpacity, 0, 1, .01, 2);
				BLEND("Foldout.LiftBlend", Foldouts, "Surface", "Lift Blend", Foldout.LiftBlend);
				BLEND("Foldout.AccentBlend", Foldouts, "Surface", "Accent Blend", Foldout.AccentBlend);
				NUM("Foldout.LiftSaturation", Foldouts, "Surface", "Lift Saturation", Foldout.LiftSaturation, 0, 4, .05, 2);
				NUM("Foldout.HoverSaturation", Foldouts, "Surface", "Hover Saturation", Foldout.HoverSaturation, 0, 4, .05, 2);
				NUM("Foldout.AccentOpacity", Foldouts, "Surface", "Accent Opacity", Foldout.AccentOpacity, 0, 1, .01, 2);
				NUM("Foldout.AccentHoverOpacity", Foldouts, "Surface", "Accent Hover Opacity", Foldout.AccentHoverOpacity, 0, 1, .01, 2);
				NUM("Foldout.HairlineOpacity", Foldouts, "Surface", "Hairline Opacity", Foldout.HairlineOpacity, 0, 1, .01, 2);
				NUM("Foldout.HairlineHoverOpacity", Foldouts, "Surface", "Hairline Hover Opacity", Foldout.HairlineHoverOpacity, 0, 1, .01, 2);
				NUM("Foldout.HairlineSaturation", Foldouts, "Surface", "Hairline Saturation", Foldout.HairlineSaturation, 0, 4, .05, 2);
				NUM("Foldout.HairlineHoverSaturation", Foldouts, "Surface", "Hover Hairline Saturation", Foldout.HairlineHoverSaturation, 0, 4, .05, 2);
				NUM("Foldout.LiftFalloff.Start", Foldouts, "Falloff", "Start", Foldout.LiftFalloff.Start, 0, 1, .01, 2);
				NUM("Foldout.LiftFalloff.End", Foldouts, "Falloff", "End", Foldout.LiftFalloff.End, 0, 1, .01, 2);
				NUM("Foldout.LiftFalloff.Power", Foldouts, "Falloff", "Power", Foldout.LiftFalloff.Power, .01, 4, .05, 2);
				NUM("FoldoutLayout.Height", Foldouts, "Layout", "Height", FoldoutLayout.Height, 12, 48, 1, 0);
				NUM("FoldoutLayout.Gutter", Foldouts, "Layout", "Gutter", FoldoutLayout.Gutter, 0, 24, .5, 1);
				NUM("FoldoutLayout.BodyTop", Foldouts, "Layout", "Body Top", FoldoutLayout.BodyTop, 0, 24, .5, 1);
				NUM("FoldoutLayout.BodyBottom", Foldouts, "Layout", "Body Bottom", FoldoutLayout.BodyBottom, 0, 24, .5, 1);
				NUM("FoldoutLayout.OuterTop", Foldouts, "Layout", "Outer Top", FoldoutLayout.OuterTop, 0, 24, .5, 1);
				NUM("FoldoutLayout.OuterBottom", Foldouts, "Layout", "Outer Bottom", FoldoutLayout.OuterBottom, 0, 24, .5, 1);
				NUM("FoldoutLayout.HeaderPaddingTop", Foldouts, "Layout", "Header Padding Top", FoldoutLayout.HeaderPaddingTop, 0, 16, .5, 1);
				NUM("FoldoutLayout.HeaderPaddingBottom", Foldouts, "Layout", "Header Padding Bottom", FoldoutLayout.HeaderPaddingBottom, 0, 16, .5, 1);
				NUM("FoldoutLayout.Radius", Foldouts, "Layout", "Radius", FoldoutLayout.Radius, 0, 12, .5, 1);

				// CARDS
				BLEND("Card.Blend", Cards, "Surface", "Blend", Card.Blend);
				NUM("Card.HeaderOpacity", Cards, "Surface", "Header Opacity", Card.HeaderOpacity, 0, 1, .01, 2);
				NUM("Card.BodyOpacity", Cards, "Surface", "Body Opacity", Card.BodyOpacity, 0, 1, .01, 2);
				NUM("Card.HeaderSaturation", Cards, "Surface", "Header Saturation", Card.HeaderSaturation, 0, 4, .05, 2);
				NUM("Card.BodySaturation", Cards, "Surface", "Body Saturation", Card.BodySaturation, 0, 4, .05, 2);
				NUM("Card.FalloffPower", Cards, "Surface", "Falloff Power", Card.FalloffPower, .01, 4, .05, 2);
				NUM("Card.Reach", Cards, "Surface", "Reach", Card.Reach, 0, 128, 1, 0);
				NUM("Card.Radius", Cards, "Surface", "Radius", Card.Radius, 0, 12, .5, 1);
				NUM("CardLayout.HeaderHeight", Cards, "Layout", "Header Height", CardLayout.HeaderHeight, 8, 48, 1, 0);
				NUM("CardLayout.HeaderLeft", Cards, "Layout", "Header Left", CardLayout.HeaderLeft, 0, 32, .5, 1);
				NUM("CardLayout.HeaderRight", Cards, "Layout", "Header Right", CardLayout.HeaderRight, 0, 32, .5, 1);
				NUM("CardLayout.HeaderMarginTop", Cards, "Layout", "Header Margin Top", CardLayout.HeaderMarginTop, 0, 24, .5, 1);
				NUM("CardLayout.HeaderMarginBottom", Cards, "Layout", "Header Margin Bottom", CardLayout.HeaderMarginBottom, 0, 24, .5, 1);
				NUM("CardLayout.OuterTop", Cards, "Layout", "Outer Top", CardLayout.OuterTop, 0, 24, .5, 1);
				NUM("CardLayout.OuterBottom", Cards, "Layout", "Outer Bottom", CardLayout.OuterBottom, 0, 24, .5, 1);
				NUM("CardLayout.BodyHorizontal", Cards, "Layout", "Body Horizontal", CardLayout.BodyHorizontal, 0, 32, .5, 1);
				NUM("CardLayout.BodyTop", Cards, "Layout", "Body Top", CardLayout.BodyTop, 0, 32, .5, 1);
				NUM("CardLayout.BodyBottom", Cards, "Layout", "Body Bottom", CardLayout.BodyBottom, 0, 32, .5, 1);
				NUM("CardLayout.Padding", Cards, "Layout", "Padding", CardLayout.Padding, 0, 32, .5, 1);
				NUM("CardLayout.Gap", Cards, "Layout", "Gap", CardLayout.Gap, 0, 32, .5, 1);

				// LAYERS
				BLEND("Layer.Blend", Layers, "Rows", "Blend", Layer.Blend);
				BLEND("Layer.GroupBlend", Layers, "Rows", "Group Blend", Layer.GroupBlend);
				NUM("Layer.RestSaturation", Layers, "Rows", "Rest Saturation", Layer.RestSaturation, 0, 4, .05, 2);
				NUM("Layer.HoverSaturation", Layers, "Rows", "Hover Saturation", Layer.HoverSaturation, 0, 4, .05, 2);
				NUM("Layer.SelectedSaturation", Layers, "Rows", "Selected Saturation", Layer.SelectedSaturation, 0, 4, .05, 2);
				NUM("Layer.RestStrength", Layers, "Rows", "Rest Strength", Layer.RestStrength, 0, 2, .01, 2);
				NUM("Layer.HoverStrength", Layers, "Rows", "Hover Strength", Layer.HoverStrength, 0, 2, .01, 2);
				NUM("Layer.SelectedStrength", Layers, "Rows", "Selected Strength", Layer.SelectedStrength, 0, 2, .01, 2);
				COL("Layer.RowBottom", Layers, "Row Colors", "Row Bottom", Layer.RowBottom);
				COL("Layer.HoverTop", Layers, "Row Colors", "Hover Top", Layer.HoverTop);
				COL("Layer.HoverBottom", Layers, "Row Colors", "Hover Bottom", Layer.HoverBottom);
				COL("Layer.SelectedTop", Layers, "Row Colors", "Selected Top", Layer.SelectedTop);
				COL("Layer.SelectedBottom", Layers, "Row Colors", "Selected Bottom", Layer.SelectedBottom);
				COL("Layer.ChildLeft", Layers, "Child Colors", "Child Left", Layer.ChildLeft);
				COL("Layer.ChildRight", Layers, "Child Colors", "Child Right", Layer.ChildRight);
				COL("Layer.ChildSelectedLeft", Layers, "Child Colors", "Child Selected Left", Layer.ChildSelectedLeft);
				COL("Layer.ChildSelectedRight", Layers, "Child Colors", "Child Selected Right", Layer.ChildSelectedRight);
				COL("Layer.Cross", Layers, "Group", "Cross", Layer.Cross);
				COL("Layer.HiddenTop", Layers, "Reference", "Hidden Top", Layer.HiddenTop);
				COL("Layer.HiddenEnd", Layers, "Reference", "Hidden End", Layer.HiddenEnd);
				NUM("Layer.GroupTintStrength", Layers, "Group", "Group Tint Strength", Layer.GroupTintStrength, 0, 1, .01, 2);
				NUM("Layer.GroupTintSelectedStrength", Layers, "Group", "Selected Group Tint", Layer.GroupTintSelectedStrength, 0, 1, .01, 2);
				NUM("Layer.ReferenceHiddenTint", Layers, "Reference", "Hidden Tint", Layer.ReferenceHiddenTint, 0, 1, .01, 2);
				NUM("Layer.ReferenceSelectedTint", Layers, "Reference", "Selected Tint", Layer.ReferenceSelectedTint, 0, 1, .01, 2);
				NUM("Layer.ReferenceHoverTint", Layers, "Reference", "Hover Tint", Layer.ReferenceHoverTint, 0, 1, .01, 2);
				NUM("Layer.ReferenceRestTint", Layers, "Reference", "Rest Tint", Layer.ReferenceRestTint, 0, 1, .01, 2);
				NUM("Layer.InstanceSourceLeftTint", Layers, "Reference", "Instance Left Tint", Layer.InstanceSourceLeftTint, 0, 1, .01, 2);
				NUM("Layer.InstanceSourceRightTint", Layers, "Reference", "Instance Right Tint", Layer.InstanceSourceRightTint, 0, 1, .01, 2);
				NUM("Layer.HairlineWidth", Layers, "Rows", "Hairline Width", Layer.HairlineWidth, 0, 4, .25, 2);
				NUM("Layer.HairlineOpacity", Layers, "Rows", "Hairline Opacity", Layer.HairlineOpacity, 0, 1, .01, 2);
				NUM("Layer.GroupSaturation", Layers, "Group", "Group Saturation", Layer.GroupSaturation, 0, 4, .05, 2);
				NUM("Layer.GroupStrength", Layers, "Group", "Group Strength", Layer.GroupStrength, 0, 2, .01, 2);
				NUM("Layer.ChildSaturation", Layers, "Child", "Child Saturation", Layer.ChildSaturation, 0, 4, .05, 2);
				NUM("Layer.ChildHoverSaturation", Layers, "Child", "Child Hover Saturation", Layer.ChildHoverSaturation, 0, 4, .05, 2);
				NUM("Layer.ChildSelectedSaturation", Layers, "Child", "Child Selected Saturation", Layer.ChildSelectedSaturation, 0, 4, .05, 2);
				NUM("Layer.ChildLeftOpacity", Layers, "Child", "Child Left Opacity", Layer.ChildLeftOpacity, 0, 1, .01, 2);
				NUM("Layer.ChildHoverLeftOpacity", Layers, "Child", "Hover Left Opacity", Layer.ChildHoverLeftOpacity, 0, 1, .01, 2);
				NUM("Layer.ChildSelectedLeftOpacity", Layers, "Child", "Selected Left Opacity", Layer.ChildSelectedLeftOpacity, 0, 1, .01, 2);
				NUM("Layer.ChildStrength", Layers, "Child", "Child Strength", Layer.ChildStrength, 0, 2, .01, 2);
				NUM("Layer.ChildHoverStrength", Layers, "Child", "Child Hover Strength", Layer.ChildHoverStrength, 0, 2, .01, 2);
				NUM("Layer.ChildSelectedStrength", Layers, "Child", "Child Selected Strength", Layer.ChildSelectedStrength, 0, 2, .01, 2);
				NUM("Layer.ActiveGlow.Opacity", Layers, "Active", "Glow Opacity", Layer.ActiveGlow.Opacity, 0, 1, .01, 2);
				NUM("Layer.ActiveGlow.Saturation", Layers, "Active", "Glow Saturation", Layer.ActiveGlow.Saturation, 0, 4, .05, 2);
				NUM("Layer.ActiveGlow.Reach", Layers, "Active", "Glow Reach", Layer.ActiveGlow.Reach, 0, 128, 1, 0);
				NUM("Layer.ActiveHairlineWidth", Layers, "Active", "Active Hairline Width", Layer.ActiveHairlineWidth, 0, 4, .25, 2);
				NUM("Layer.ActiveHairlineOpacity", Layers, "Active", "Active Hairline Opacity", Layer.ActiveHairlineOpacity, 0, 1, .01, 2);
				NUM("LayerHierarchy.Indent", Layers, "Hierarchy", "Indent", LayerHierarchy.Indent, 0, 80, 1, 0);
				NUM("LayerHierarchy.Width", Layers, "Hierarchy", "Line Width", LayerHierarchy.Width, 0, 4, .25, 2);
				NUM("LayerHierarchy.Opacity", Layers, "Hierarchy", "Opacity", LayerHierarchy.Opacity, 0, 1, .01, 2);
				NUM("LayerHierarchy.ParentJoinOffset", Layers, "Hierarchy", "Parent Join Offset", LayerHierarchy.ParentJoinOffset, -32, 32, .5, 1);
				NUM("LayerHierarchy.ChildArmLength", Layers, "Hierarchy", "Child Arm Length", LayerHierarchy.ChildArmLength, 0, 32, .5, 1);
				NUM("LayerLayout.RowHeight", Layers, "Layout", "Row Height", LayerLayout.RowHeight, 16, 64, 1, 0);
				NUM("LayerLayout.GroupRowHeight", Layers, "Layout", "Group Row Height", LayerLayout.GroupRowHeight, 12, 48, 1, 0);
				NUM("LayerLayout.ChildRowHeight", Layers, "Layout", "Child Row Height", LayerLayout.ChildRowHeight, 12, 48, 1, 0);
				NUM("LayerLayout.Gap", Layers, "Layout", "Gap", LayerLayout.Gap, 0, 24, .5, 1);
				NUM("LayerLayout.PaddingX", Layers, "Layout", "Padding X", LayerLayout.PaddingX, 0, 32, .5, 1);
				NUM("LayerLayout.ThumbnailSize", Layers, "Layout", "Thumbnail Size", LayerLayout.ThumbnailSize, 8, 64, 1, 0);
				NUM("LayerLayout.ItemGap", Layers, "Layout", "Item Gap", LayerLayout.ItemGap, 0, 24, .5, 1);
				NUM("LayerLayout.ChildIndent", Layers, "Layout", "Child Indent", LayerLayout.ChildIndent, 0, 80, 1, 0);

				// BUTTONS
				NUM("Button.Height", Buttons, "Body", "Height", Button.Height, 12, 48, 1, 0);
				NUM("Button.HorizontalPadding", Buttons, "Body", "Horizontal Padding", Button.HorizontalPadding, 0, 32, .5, 1);
				BLEND("Button.BodyBlend", Buttons, "Body", "Body Blend", Button.BodyBlend);
				BLEND("Button.HairlineBlend", Buttons, "Hairline", "Hairline Blend", Button.HairlineBlend);
				NUM("Button.RestTop", Buttons, "Body", "Rest Top", Button.RestTop, 0, 1, .01, 2);
				NUM("Button.RestBottom", Buttons, "Body", "Rest Bottom", Button.RestBottom, 0, 1, .01, 2);
				NUM("Button.HoverTop", Buttons, "Body", "Hover Top", Button.HoverTop, 0, 1, .01, 2);
				NUM("Button.HoverBottom", Buttons, "Body", "Hover Bottom", Button.HoverBottom, 0, 1, .01, 2);
				NUM("Button.SelectedTop", Buttons, "Body", "Selected Top", Button.SelectedTop, 0, 1, .01, 2);
				NUM("Button.SelectedBottom", Buttons, "Body", "Selected Bottom", Button.SelectedBottom, 0, 1, .01, 2);
				NUM("Button.GradientSaturation", Buttons, "Body", "Gradient Saturation", Button.GradientSaturation, 0, 4, .05, 2);
				NUM("Button.HairlineWidth", Buttons, "Hairline", "Hairline Width", Button.HairlineWidth, 0, 4, .25, 2);
				NUM("Button.HairlineOpacity", Buttons, "Hairline", "Rest Opacity", Button.HairlineOpacity, 0, 1, .01, 2);
				NUM("Button.HairlineHoverOpacity", Buttons, "Hairline", "Hover Opacity", Button.HairlineHoverOpacity, 0, 1, .01, 2);
				NUM("Button.HairlineSelectedOpacity", Buttons, "Hairline", "Selected Opacity", Button.HairlineSelectedOpacity, 0, 1, .01, 2);
				NUM("Button.HairlineSaturation", Buttons, "Hairline", "Saturation", Button.HairlineSaturation, 0, 4, .05, 2);
				NUM("Button.SeparatorWidth", Buttons, "Separator", "Width", Button.SeparatorWidth, 0, 4, .25, 2);
				NUM("Button.SeparatorHeight", Buttons, "Separator", "Height", Button.SeparatorHeight, 0, 32, 1, 0);
				NUM("Button.SeparatorOpacity", Buttons, "Separator", "Opacity", Button.SeparatorOpacity, 0, 1, .01, 2);
				NUM("Button.TextOpacity", Buttons, "Text", "Text Opacity", Button.TextOpacity, 0, 1, .01, 2);

				// MENUS
				COL("Menu.LipSource", Menus, "Surface", "Lip Source", Menu.LipSource);
				COL("Menu.DestructiveText", Menus, "Surface", "Destructive Text", Menu.DestructiveText);
				COL("Menu.DestructiveHover", Menus, "Surface", "Destructive Hover", Menu.DestructiveHover);
				NUM("Menu.LipTintOpacity", Menus, "Surface", "Lip Tint Opacity", Menu.LipTintOpacity, 0, 1, .01, 2);
				NUM("Menu.BorderOpacity", Menus, "Surface", "Border Opacity", Menu.BorderOpacity, 0, 1, .01, 2);
				NUM("Menu.ItemHoverOpacity", Menus, "Rows", "Hover Opacity", Menu.ItemHoverOpacity, 0, 1, .01, 2);
				NUM("Menu.ItemCheckedOpacity", Menus, "Rows", "Checked Opacity", Menu.ItemCheckedOpacity, 0, 1, .01, 2);
				NUM("Menu.ItemDisabledOpacity", Menus, "Rows", "Disabled Opacity", Menu.ItemDisabledOpacity, 0, 1, .01, 2);
				NUM("Menu.CornerRadius", Menus, "Surface", "Corner Radius", Menu.CornerRadius, 0, 12, .5, 1);
				NUM("MenuLayout.Width", Menus, "Layout", "Width", MenuLayout.Width, 120, 480, 1, 0);
				NUM("MenuLayout.LipHeight", Menus, "Layout", "Lip Height", MenuLayout.LipHeight, 4, 80, 1, 0);
				NUM("MenuLayout.RowHeight", Menus, "Layout", "Row Height", MenuLayout.RowHeight, 12, 48, 1, 0);
				NUM("MenuLayout.ItemInset", Menus, "Layout", "Item Inset", MenuLayout.ItemInset, 0, 24, .5, 1);
				NUM("MenuLayout.ItemGap", Menus, "Layout", "Item Gap", MenuLayout.ItemGap, 0, 24, .5, 1);
				NUM("MenuLayout.PanelPadding", Menus, "Layout", "Panel Padding", MenuLayout.PanelPadding, 0, 24, .5, 1);
				NUM("MenuLayout.CaptionInsetAbove", Menus, "Layout", "Caption Inset Above", MenuLayout.CaptionInsetAbove, 0, 24, .5, 1);
				NUM("MenuLayout.CaptionInsetBelow", Menus, "Layout", "Caption Inset Below", MenuLayout.CaptionInsetBelow, 0, 24, .5, 1);
				NUM("MenuLayout.SeparatorMargin", Menus, "Layout", "Separator Margin", MenuLayout.SeparatorMargin, 0, 24, .5, 1);
				NUM("MenuLayout.ChevronSize", Menus, "Layout", "Chevron Size", MenuLayout.ChevronSize, 4, 32, 1, 0);

				// PREVIEW
				COL("Preview.PlateSource", Preview, "Overlay Plate", "Plate Source", Preview.PlateSource);
				NUM("Preview.PlateOpacity", Preview, "Overlay Plate", "Plate Opacity", Preview.PlateOpacity, 0, 1, .01, 2);
				NUM("Preview.IconRestOpacity", Preview, "Overlay Plate", "Label Rest Opacity", Preview.IconRestOpacity, 0, 1, .01, 2);
				NUM("Preview.HoverAccent", Preview, "Overlay Plate", "Hover Accent", Preview.HoverAccent, 0, 1, .01, 2);
				NUM("Preview.PressAccent", Preview, "Overlay Plate", "Press Accent", Preview.PressAccent, 0, 1, .01, 2);
				NUM("PreviewLayout.OverlayInset", Preview, "Layout", "Overlay Inset", PreviewLayout.OverlayInset, 0, 48, .5, 1);

				NUM("PreviewLayout.ToolbarGap", Preview, "Layout", "Toolbar Gap", PreviewLayout.ToolbarGap, 0, 24, .5, 1);
				NUM("PreviewLayout.OverlayButtonGap", Preview, "Layout", "Button Gap", PreviewLayout.OverlayButtonGap, 0, 24, .5, 1);
				NUM("PreviewLayout.ComparisonToggleGap", Preview, "Layout", "Comparison Gap", PreviewLayout.ComparisonToggleGap, 0, 24, .5, 1);
				NUM("PreviewLayout.ResolutionControlWidth", Preview, "Layout", "Resolution Width", PreviewLayout.ResolutionControlWidth, 40, 240, 1, 0);
				NUM("PreviewLayout.TogglePadding", Preview, "Layout", "Toggle Padding", PreviewLayout.TogglePadding, 0, 16, .5, 1);

				// GALLERY / SHELL. TileSize intentionally omitted: runtime zoom owns it after construction.
				NUM("Gallery.BorderWidth", GalleryShell, "Gallery Surface", "Border Width", Gallery.BorderWidth, 0, 4, .25, 2);
				NUM("Gallery.BorderOpacity", GalleryShell, "Gallery Surface", "Border Opacity", Gallery.BorderOpacity, 0, 1, .01, 2);
				NUM("Gallery.HoverLiftOpacity", GalleryShell, "Gallery Surface", "Hover Lift", Gallery.HoverLiftOpacity, 0, 1, .01, 2);
				NUM("Gallery.SelectedEdgeWidth", GalleryShell, "Gallery Surface", "Selected Edge Width", Gallery.SelectedEdgeWidth, 0, 4, .25, 2);
				NUM("Gallery.SelectedEdgeOpacity", GalleryShell, "Gallery Surface", "Selected Edge Opacity", Gallery.SelectedEdgeOpacity, 0, 1, .01, 2);
				NUM("Gallery.CornerRadius", GalleryShell, "Gallery Surface", "Corner Radius", Gallery.CornerRadius, 0, 12, .5, 1);
				NUM("GalleryLayout.TileGap", GalleryShell, "Gallery Layout", "Tile Gap", GalleryLayout.TileGap, 0, 24, .5, 1);
				NUM("GalleryLayout.TilePadding", GalleryShell, "Gallery Layout", "Tile Padding", GalleryLayout.TilePadding, 0, 24, .5, 1);
				NUM("GalleryLayout.CaptionHeight", GalleryShell, "Gallery Layout", "Caption Height", GalleryLayout.CaptionHeight, 0, 32, .5, 1);
				NUM("GalleryLayout.CaptionInset", GalleryShell, "Gallery Layout", "Caption Inset", GalleryLayout.CaptionInset, 0, 24, .5, 1);
				NUM("GalleryLayout.OverlayInset", GalleryShell, "Gallery Layout", "Overlay Inset", GalleryLayout.OverlayInset, 0, 24, .5, 1);
				NUM("GalleryLayout.HeaderGap", GalleryShell, "Gallery Layout", "Header Gap", GalleryLayout.HeaderGap, 0, 24, .5, 1);
				COL("ShellTheme.SplitterHoverSource", GalleryShell, "Shell / Splitter", "Hover Source", ShellTheme.SplitterHoverSource);
				NUM("ShellTheme.SplitterOpacity", GalleryShell, "Shell / Splitter", "Rest Opacity", ShellTheme.SplitterOpacity, 0, 1, .01, 2);
				NUM("ShellTheme.SplitterHoverOpacity", GalleryShell, "Shell / Splitter", "Hover Opacity", ShellTheme.SplitterHoverOpacity, 0, 1, .01, 2);
				NUM("Shell.TopBarHeight", GalleryShell, "Shell / Layout", "Top Bar Height", Shell.TopBarHeight, 20, 64, 1, 0);
				NUM("Shell.StatusBarHeight", GalleryShell, "Shell / Layout", "Status Bar Height", Shell.StatusBarHeight, 12, 48, 1, 0);
				NUM("Shell.PanelPadding", GalleryShell, "Shell / Layout", "Panel Padding", Shell.PanelPadding, 0, 32, .5, 1);
				NUM("Shell.SplitterVisualWidth", GalleryShell, "Shell / Layout", "Splitter Visual Width", Shell.SplitterVisualWidth, 0, 12, .25, 2);
				NUM("Shell.SplitterHitWidth", GalleryShell, "Shell / Layout", "Splitter Hit Width", Shell.SplitterHitWidth, 2, 24, .5, 1);

				// GalleryCaption is the only typography role with a production reader in this pass.
				AddTextRole(P, EMixtormatTextRole::GalleryCaption, TEXT("GalleryCaption"), TEXT("Gallery Caption"));

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
		return FPaths::Combine(
			FPaths::ProjectSavedDir(),
			FMixtormatPaths::ProductName().ToString(),
			TEXT("UIStyleTheme.json"));
	}

	bool FMixtormatThemeSchema::Save(FString& OutError)
	{
		OutError.Reset();
		const FMixtormatTheme& Theme = FMixtormatThemeStore::GetTheme();
		const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetNumberField(TEXT("version"), 1);
		Root->SetStringField(TEXT("schema"), TEXT("MixtormatUIStyle"));

		TMap<ETab, TSharedPtr<FJsonObject>> Sections;
		for (uint8 I = 0; I < static_cast<uint8>(ETab::Count); ++I)
		{
			const ETab Tab = static_cast<ETab>(I);
			const TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
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
		const FString Path = SavePath();
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

		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *SavePath()))
		{
			OutError = FString::Printf(TEXT("Could not read %s. Save a UI style theme first."), *SavePath());
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

		FMixtormatTheme Pending = MakeDefaultTheme();
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
