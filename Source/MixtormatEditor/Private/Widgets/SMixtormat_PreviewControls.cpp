// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "Widgets/SMixtormatInternal.h"
#include "Style/MixtormatRecipes.h"
#include "Style/MixtormatThemeStore.h"
#include "Style/MixtormatTypography.h"
#include "UI/Menus/SMixtormatHelp.h"
#include "UI/Primitives/SMixtormatSurfaceBox.h"
#include "UI/Primitives/MixtormatSurfacePainter.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Rendering/SlateRenderTransform.h"

// The preview's control clusters, extracted from BuildPreviewPanel so the viewport overlay and the
// GLOBAL Preview / Viewport section build the same controls from one place instead of two copies.
//
// Every builder returns the control content only. Where it goes -- a viewport cluster, a GLOBAL
// card -- is the caller's decision, and every builder reads and writes the same SMixtormat state
// the viewport already uses, so no view can drift from another.

#define LOCTEXT_NAMESPACE "SMixtormat"

namespace
{
	class SMixtormatPreviewPlate final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SMixtormatPreviewPlate) : _bChecked(false) {}
			SLATE_ATTRIBUTE(bool, bChecked)
			SLATE_ATTRIBUTE(FText, ToolTip)
			SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_END_ARGS()

		void Construct(const FArguments& Args)
		{
			bChecked = Args._bChecked;
			ChildSlot
			[
				SNew(SMixtormatHelp)
				.Text(Args._ToolTip)
				.Enabled_Lambda([this]() { return IsEnabled(); })
				[Args._Content.Widget]
			];
		}

		int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
			FSlateWindowElementList& Elements, const int32 LayerId, const FWidgetStyle& WidgetStyle,
			const bool bParentEnabled) const override
		{
			using namespace Mixtormat;
			const bool bPointerPressed = IsHovered()
				&& FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton);
			const EMixtormatPreviewPlateState State = bPointerPressed
				? EMixtormatPreviewPlateState::Pressed
				: bChecked.Get(false) ? EMixtormatPreviewPlateState::Checked
				: IsHovered() ? EMixtormatPreviewPlateState::Hover : EMixtormatPreviewPlateState::Rest;
			const FMixtormatSurfaceRecipe Recipe = MakePreviewPlateRecipe(FMixtormatThemeStore::GetResolved(), State);
			FMixtormatSurfaceDrawStyle DrawStyle;
			DrawStyle.Tint = WidgetStyle.GetColorAndOpacityTint();
			DrawStyle.Effects = ShouldBeEnabled(bParentEnabled)
				? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
			const int32 SurfaceLayer = FMixtormatSurfacePainter::PaintSurface(
				Elements, LayerId, Geometry, Recipe, FMixtormatThemeStore::GetResolved().Palette,
				WidgetStyle, DrawStyle);
			return SCompoundWidget::OnPaint(Args, Geometry, CullingRect, Elements,
				SurfaceLayer + 1, WidgetStyle, bParentEnabled);
		}

	private:
		TAttribute<bool> bChecked;
	};

	// The foreground a viewport-overlay control's label draws in.
	//
	// components.css states this three ways: a rail button's colour is `text` at
	// `--overlay-icon-rest-opacity`, full `text` on hover, and `accent` when pressed. Resolving it
	// here rather than leaving every label on FSlateColor::UseForeground() is what gives that token a
	// production reader and what keeps hover/pressed response from depending on a legacy style brush.
	FSlateColor GetPreviewOverlayLabelColor(const bool bHovered, const bool bPressed)
	{
		const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
		const float TypographyOpacity = Mixtormat::FMixtormatTypography::GetSpec(
			Resolved.Typography, Mixtormat::EMixtormatTextRole::PreviewLabel).Opacity;
		if (bPressed)
		{
			FLinearColor Color = Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Accent);
			Color.A *= TypographyOpacity;
			return FSlateColor(Color);
		}
		// Preview's rest opacity is the authored state treatment for this overlay label; the text
		// role opacity is its independent typography treatment. Both are explicit source values.
		return FSlateColor(Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text)
			.CopyWithNewOpacity((bHovered ? 1.0f : Resolved.Preview.IconRestOpacity) * TypographyOpacity));
	}

	// The glyph inside an overlay control: `.viewport-overlay .asset-icon` is Text at the PreviewToolbar
	// role's rest opacity, lifts to full on hover, and takes the accent when its control is pressed.
	// Reading the role's own opacities rather than a second authored copy is what stops --overlay-icon-
	// opacity from existing twice under two names.
	FSlateColor GetPreviewOverlayIconColor(const bool bHovered, const bool bPressed = false)
	{
		const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
		const Mixtormat::FMixtormatIconStyle& Icon = Resolved.Icons.Roles[
			static_cast<uint8>(Mixtormat::EMixtormatIconRole::PreviewToolbar)];
		if (bPressed)
		{
			return FSlateColor(Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Accent));
		}
		return FSlateColor(Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text)
			.CopyWithNewOpacity(bHovered ? Icon.HoverOpacity : Icon.RestOpacity));
	}

	// A resourceless button style for overlay controls, so the SMixtormatPreviewPlate recipe is the only
	// thing that draws a plate. Mixtormat.ViewportOverlayButton bakes a legacy plate brush into every
	// state, which would double up on top of the recipe's.
	const FButtonStyle& GetPreviewOverlayButtonStyle()
	{
		static FButtonStyle Style;
		Style = FButtonStyle()
			.SetNormal(FSlateNoResource()).SetHovered(FSlateNoResource())
			.SetPressed(FSlateNoResource()).SetDisabled(FSlateNoResource());
		Style.SetNormalPadding(FMargin(0.0f)).SetPressedPadding(FMargin(0.0f));
		return Style;
	}

	const FCheckBoxStyle& GetPreviewOverlayToggleStyle()
	{
		// Rebuilt per call rather than held in a function-static: the padding comes from the resolved
		// style, and a static captured it on first call only -- so a live-theme change to
		// TogglePadding would silently not reach the widget.
		static FCheckBoxStyle Style;
		Style = FCheckBoxStyle().SetCheckBoxType(ESlateCheckBoxType::ToggleButton);
		Style.SetUncheckedImage(FSlateNoResource()).SetUncheckedHoveredImage(FSlateNoResource())
			.SetUncheckedPressedImage(FSlateNoResource()).SetCheckedImage(FSlateNoResource())
			.SetCheckedHoveredImage(FSlateNoResource()).SetCheckedPressedImage(FSlateNoResource())
			.SetUndeterminedImage(FSlateNoResource()).SetUndeterminedHoveredImage(FSlateNoResource())
			.SetUndeterminedPressedImage(FSlateNoResource()).SetBackgroundImage(FSlateNoResource())
			.SetBackgroundHoveredImage(FSlateNoResource()).SetBackgroundPressedImage(FSlateNoResource())
			.SetPadding(FMargin(FMixtormatThemeStore::GetResolved().PreviewLayout.TogglePadding));
		return Style;
	}

	FTextBlockStyle MakePreviewLabelStyle()
	{
		const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
		return Mixtormat::FMixtormatTypography::MakeTextStyle(
			Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::PreviewLabel),
			Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text));
	}

	// One rail button: the plate, the toggle, and whatever the caller puts inside it. The mesh and
	// lighting rails were the same widget twice, differing only in what they set and what they show.
	TSharedRef<SWidget> MakePreviewRailButton(
		const TAttribute<bool>& bChecked,
		const TAttribute<bool>& bEnabled,
		const TAttribute<FText>& ToolTip,
		const FOnCheckStateChanged& OnChanged,
		const TSharedRef<SWidget>& Content)
	{
		const Mixtormat::FMixtormatIconStyle& Icon = FMixtormatThemeStore::GetResolved().Icons.Roles[
			static_cast<uint8>(Mixtormat::EMixtormatIconRole::PreviewToolbar)];
		return SNew(SBox)
			.WidthOverride(Icon.ButtonSize)
			.HeightOverride(Icon.ButtonSize)
			[
				SNew(SMixtormatPreviewPlate)
				.bChecked(bChecked)
				[
					SNew(SCheckBox)
					.Style(&GetPreviewOverlayToggleStyle())
					.IsEnabled(bEnabled)
					.ToolTipText(ToolTip)
					.IsChecked_Lambda([bChecked]()
					{
						return bChecked.Get(false) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged(OnChanged)
					[
						Content
					]
				]
			];
	}

	// The glyph inside a rail button, at the role's glyph size.
	TSharedRef<SWidget> MakeRailGlyph(const FSlateBrush* Icon, const TAttribute<FSlateColor>& Color)
	{
		const Mixtormat::FMixtormatIconStyle& Role = FMixtormatThemeStore::GetResolved().Icons.Roles[
			static_cast<uint8>(Mixtormat::EMixtormatIconRole::PreviewToolbar)];
		return SNew(SBox)
			.WidthOverride(Role.GlyphSize)
			.HeightOverride(Role.GlyphSize)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SImage)
				.Image(Icon)
				.ColorAndOpacity(Color)
			];
	}
}

TSharedRef<SWidget> SMixtormat::MakePreviewCluster(const TSharedRef<SWidget>& Content)
{
	return SNew(SMixtormatSurfaceBox)
		.Recipe_Lambda([]() { return Mixtormat::MakePreviewClusterRecipe(FMixtormatThemeStore::GetTheme()); })
		.Padding(FMargin(FMixtormatThemeStore::GetResolved().PreviewLayout.OverlayClusterInset))
		[Content];
}

TSharedRef<SWidget> SMixtormat::MakePreviewScaleRow()
{
	return MakeSlider(
		LOCTEXT("PreviewScaleLabel", "Scale"),
		TAttribute<double>::CreateLambda([this]() { return static_cast<double>(PreviewScreenPercentage); }),
		static_cast<double>(MixtormatPreviewScreenPercentage::Minimum),
		static_cast<double>(MixtormatPreviewScreenPercentage::Maximum),
		static_cast<double>(MixtormatPreviewScreenPercentage::Default),
		1.0, false,
		FMixtormatOnSliderValueChanged::CreateLambda([this](const double Value)
		{
			SetPreviewScreenPercentage(FMath::RoundToInt(Value));
		}),
		FSimpleDelegate::CreateLambda([this]()
		{
			SetPreviewScreenPercentage(MixtormatPreviewScreenPercentage::Default);
		}),
		LOCTEXT("PreviewScaleHint", "Render resolution as a percentage of the viewport. Above 100 the extra samples are real, so it fixes shading aliasing rather than hiding it -- 150 with FXAA is the sharpest stable option here, at 2.25x the fill rate. Below 100 it buys back frame time on an expensive graph."));
}

TSharedRef<SWidget> SMixtormat::MakePreviewFinalButton()
{
	const Mixtormat::FMixtormatPreviewMetrics& Layout = FMixtormatThemeStore::GetResolved().PreviewLayout;
	const FTextBlockStyle LabelStyle = MakePreviewLabelStyle();
	return SNew(SMixtormatPreviewPlate)
		.ToolTip(LOCTEXT("FinalCompositeHint", "Final AO for the whole composite. Relief normals always come from the final height."))
		[
			SNew(SComboButton)
				.ButtonStyle(&GetPreviewOverlayButtonStyle())
				.Method(EPopupMethod::UseCurrentWindow)
				.ContentPadding(FMargin(Layout.TogglePadding))
				.IsEnabled_Lambda([this]() { return bHasWorkingMaterial; })
				.OnGetMenuContent(this, &SMixtormat::BuildFinalSettingsControls)
				.ButtonContent()
				[
					SNew(STextBlock)
					.Font(LabelStyle.Font)
					.ColorAndOpacity_Lambda([this]() { return GetPreviewOverlayLabelColor(false, false); })
					.Text(LOCTEXT("FinalCompositeButton", "Final"))
				]
		];
}

TSharedRef<SWidget> SMixtormat::BuildPreviewRenderStrip()
{
	// The overlay's copy: render scale and the Final popup, the two the user reaches for while
	// looking at the surface. AA, Default/Lumen and displacement are set once and then left alone,
	// so they live in GLOBAL -- on the viewport they were five rows of chrome over the material.
	const float Gap = FMixtormatThemeStore::GetResolved().PreviewLayout.ToolbarGap;
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			MakePreviewScaleRow()
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		.Padding(Gap, 0.0f, 0.0f, 0.0f)
		[
			MakePreviewFinalButton()
		];
}

TSharedRef<SWidget> SMixtormat::BuildPreviewRenderControls()
{
	const Mixtormat::FMixtormatPreviewMetrics& Layout = FMixtormatThemeStore::GetResolved().PreviewLayout;
	const float RowGap = FMixtormatThemeStore::GetResolved().ControlLayout.RowGap;

	const TArray<FText> AntiAliasingOptions = {
		LOCTEXT("PreviewAaFxaa", "FXAA"),
		LOCTEXT("PreviewAaTsr", "TSR")};
	const TArray<FText> AntiAliasingToolTips = {
		LOCTEXT("PreviewAaFxaaHint", "Single frame, no history. Click again to turn anti-aliasing off."),
		LOCTEXT("PreviewAaTsrHint", "Temporal Super-Resolution, the project default. Click again to turn anti-aliasing off.")};
	const TArray<FText> QualityOptions = {
		LOCTEXT("PreviewQualityDefault", "DEFAULT"),
		LOCTEXT("PreviewQualityLumen", "LUMEN")};
	const TArray<FText> QualityToolTips = {
		LOCTEXT("PreviewQualityDefaultHint", "Stable studio key and cubemap lighting; viewport AO, Lumen, and screen-space reflections are off."),
		LOCTEXT("PreviewQualityLumenHint", "Enable Lumen global illumination and reflections for an optional lighting check.")};

	TSharedRef<SVerticalBox> Controls = SNew(SVerticalBox);
	Controls->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, RowGap)
	[
		SNew(SMixtormatSegmentedControl)
		.Options(AntiAliasingOptions)
		.ToolTips(AntiAliasingToolTips)
		.ActiveIndex_Lambda([this]() -> int32
		{
			if (PreviewAntiAliasing == EMixtormatPreviewAntiAliasing::Off)
			{
				return INDEX_NONE;
			}
			return PreviewAntiAliasing == EMixtormatPreviewAntiAliasing::Fxaa ? 0 : 1;
		})
		.OnChosen_Lambda([this](const int32 Index)
		{
			const EMixtormatPreviewAntiAliasing Chosen = Index == 0
				? EMixtormatPreviewAntiAliasing::Fxaa
				: EMixtormatPreviewAntiAliasing::Temporal;
			SetPreviewAntiAliasing(PreviewAntiAliasing == Chosen
				? EMixtormatPreviewAntiAliasing::Off
				: Chosen);
		})
	];
	Controls->AddSlot().AutoHeight()
	[
		MakePreviewScaleRow()
	];
	// Default / Lumen and the Final popup on one row: both answer how the frame is resolved, which
	// is why they belong to the render strip rather than to the material.
	Controls->AddSlot().AutoHeight().Padding(0.0f, RowGap, 0.0f, 0.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SMixtormatSegmentedControl)
			.Options(QualityOptions)
			.ToolTips(QualityToolTips)
			.ActiveIndex_Lambda([this]()
			{
				return PreviewQuality == EMixtormatPreviewQuality::Lumen ? 1 : 0;
			})
			.OnChosen_Lambda([this](const int32 Index)
			{
				SetPreviewQuality(Index == 1
					? EMixtormatPreviewQuality::Lumen
					: EMixtormatPreviewQuality::Default);
			})
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		.Padding(Layout.ToolbarGap, 0.0f, 0.0f, 0.0f)
		[
			MakePreviewFinalButton()
		]
	];
	Controls->AddSlot().AutoHeight().Padding(0.0f, RowGap, 0.0f, 0.0f)
	[
		MixtormatRow::Make(
			LOCTEXT("PreviewDisplacement", "Displacement"),
			MixtormatRow::MakeCheckbox(
				TAttribute<ECheckBoxState>::CreateLambda([this]()
				{
					return bPreviewDisplacementEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				}),
				FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
				{
					SetPreviewDisplacementEnabled(State == ECheckBoxState::Checked);
				}),
				LOCTEXT("PreviewDisplacementHint", "Preview the composited Height through the protected master's authored displacement path.")))
	];
	Controls->AddSlot().AutoHeight()
	[
		SNew(SBox)
		.IsEnabled_Lambda([this]() { return bPreviewDisplacementEnabled; })
		[
			MakeSlider(
				LOCTEXT("PreviewDisplacementAmountLabel", "Amount"),
				TAttribute<double>::CreateLambda([this]() { return static_cast<double>(PreviewDisplacementAmount); }),
				0.0, 4.0, 1.0, 0.05, false,
				FMixtormatOnSliderValueChanged::CreateLambda([this](const double Value)
				{
					SetPreviewDisplacementAmount(static_cast<float>(Value));
				}),
				FSimpleDelegate::CreateLambda([this]() { SetPreviewDisplacementAmount(1.0f); }),
				LOCTEXT("PreviewDisplacementAmountHint", "Scale the centered composited Height used by the authored displacement path."))
		]
	];
	return Controls;
}

TSharedRef<SWidget> SMixtormat::BuildPreviewLightingControls()
{
	const float ButtonGap = FMixtormatThemeStore::GetResolved().PreviewLayout.OverlayButtonGap;
	TSharedRef<SVerticalBox> Controls = SNew(SVerticalBox);
	const auto AddPresetButton = [this, &Controls, ButtonGap](
		const EMixtormatStudioLighting Preset,
		const FText& ToolTip,
		const FSlateBrush* Icon)
	{
		Controls->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, ButtonGap)
		[
			MakePreviewRailButton(
				TAttribute<bool>::CreateLambda([this, Preset]() { return StudioLighting == Preset; }),
				true,
				ToolTip,
				FOnCheckStateChanged::CreateLambda([this, Preset](ECheckBoxState) { SetStudioLighting(Preset); }),
				MakeRailGlyph(Icon, TAttribute<FSlateColor>::CreateLambda([this, Preset]()
				{
					return GetPreviewOverlayIconColor(false, StudioLighting == Preset);
				})))
		];
	};
	AddPresetButton(EMixtormatStudioLighting::Neutral, LOCTEXT("NeutralStudioButton", "Neutral studio"), MixtormatIcons::LightNeutral());
	AddPresetButton(EMixtormatStudioLighting::Soft, LOCTEXT("SoftStudioButton", "Soft studio"), MixtormatIcons::LightSoft());
	AddPresetButton(EMixtormatStudioLighting::Dramatic, LOCTEXT("DramaticStudioButton", "Dramatic studio"), MixtormatIcons::LightDramatic());
	AddPresetButton(EMixtormatStudioLighting::Rim, LOCTEXT("RimStudioButton", "Rim lighting"), MixtormatIcons::LightRim());
	AddPresetButton(EMixtormatStudioLighting::Workshop, LOCTEXT("WorkshopStudioButton", "Workshop lighting"), MixtormatIcons::Globe());
	Controls->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, ButtonGap)
	[
		SNew(SBox)
		.WidthOverride(FMixtormatThemeStore::GetResolved().Icons.Roles[
			static_cast<uint8>(Mixtormat::EMixtormatIconRole::PreviewToolbar)].ButtonSize)
		.HeightOverride(FMixtormatThemeStore::GetResolved().Icons.Roles[
			static_cast<uint8>(Mixtormat::EMixtormatIconRole::PreviewToolbar)].ButtonSize)
		[
			SNew(SMixtormatPreviewPlate)
			[
			// The resourceless overlay style, not Mixtormat.ViewportOverlayButton. That button style
			// bakes its own legacy plate brush into every state, so keeping it here painted a second
			// plate on top of the recipe's -- the one overlay control that did not match its neighbours.
			SNew(SButton)
			.ButtonStyle(&GetPreviewOverlayButtonStyle())
			.ContentPadding(FMargin(FMixtormatThemeStore::GetResolved().PreviewLayout.TogglePadding))
			.ToolTipText(LOCTEXT("ResetPreviewCameraLightingHint", "Reset camera, FOV, and lighting"))
			.OnClicked(this, &SMixtormat::ResetPreviewCameraAndLighting)
			[
				MakeRailGlyph(MixtormatIcons::Refresh(), TAttribute<FSlateColor>::CreateLambda([this]()
				{
					return GetPreviewOverlayIconColor(false);
				}))
			]
			]
		]
	];
	return Controls;
}

TSharedRef<SWidget> SMixtormat::BuildPreviewGeometryControls()
{
	const float ButtonGap = FMixtormatThemeStore::GetResolved().PreviewLayout.OverlayButtonGap;
	const FTextBlockStyle LabelStyle = MakePreviewLabelStyle();
	TSharedRef<SVerticalBox> Controls = SNew(SVerticalBox);
	const auto AddMeshButton = [this, &Controls, ButtonGap](
		const EMixtormatPreviewMesh MeshType,
		const FText& ToolTip,
		const FSlateBrush* Icon)
	{
		// The Plane icon turns 90 degrees when the Plane is stood upright, so the button reads its
		// state before the tooltip is hovered. Only the glyph is transformed -- the checkbox around
		// it is the hit target, so the layout size is unchanged.
		TSharedRef<SImage> Glyph = SNew(SImage)
			.Image(Icon)
			.ColorAndOpacity_Lambda([this, MeshType]()
			{
				// A selected rail button is aria-pressed in the prototype, so the accent reads
				// as pressed rather than as a separate "selected" colour.
				return GetPreviewOverlayIconColor(false, PreviewMesh == MeshType);
			});
		Glyph->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		Glyph->SetRenderTransform(TAttribute<TOptional<FSlateRenderTransform>>::CreateLambda([this, MeshType]()
		{
			return MeshType == EMixtormatPreviewMesh::Plane
					&& PlaneOrientation == EMixtormatPlaneOrientation::VerticalX
				? TOptional<FSlateRenderTransform>(FSlateRenderTransform(FQuat2D(FMath::DegreesToRadians(90.0f))))
				: TOptional<FSlateRenderTransform>();
		}));
		Controls->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, ButtonGap)
		[
			MakePreviewRailButton(
				TAttribute<bool>::CreateLambda([this, MeshType]() { return PreviewMesh == MeshType; }),
				true,
				TAttribute<FText>::CreateLambda([this, MeshType, ToolTip]()
				{
					if (MeshType == EMixtormatPreviewMesh::Plane)
					{
						return PlaneOrientation == EMixtormatPlaneOrientation::VerticalX
							? LOCTEXT("PlanePreviewVerticalX", "Plane — Vertical +X")
							: LOCTEXT("PlanePreviewHorizontal", "Plane — Horizontal");
					}
					return ToolTip;
				}),
				FOnCheckStateChanged::CreateLambda([this, MeshType](ECheckBoxState) { SetPreviewMesh(MeshType); }),
				SNew(SBox)
				.WidthOverride(FMixtormatThemeStore::GetResolved().Icons.Roles[
					static_cast<uint8>(Mixtormat::EMixtormatIconRole::PreviewToolbar)].GlyphSize)
				.HeightOverride(FMixtormatThemeStore::GetResolved().Icons.Roles[
					static_cast<uint8>(Mixtormat::EMixtormatIconRole::PreviewToolbar)].GlyphSize)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[Glyph])
		];
	};
	AddMeshButton(EMixtormatPreviewMesh::Sphere, LOCTEXT("SpherePreview", "Sphere"), MixtormatIcons::Sphere());
	AddMeshButton(EMixtormatPreviewMesh::Cylinder, LOCTEXT("CylinderPreview", "Cylinder"), MixtormatIcons::Cylinder());
	AddMeshButton(EMixtormatPreviewMesh::Cube, LOCTEXT("CubePreview", "Cube"), MixtormatIcons::Cube());
	AddMeshButton(EMixtormatPreviewMesh::Plane, LOCTEXT("PlanePreview", "Plane"), MixtormatIcons::Plane());
	Controls->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, ButtonGap)
	[
		MakePreviewRailButton(
			TAttribute<bool>::CreateLambda([this]() { return bGlobalUVRotation90; }),
			TAttribute<bool>::CreateLambda([this]() { return bHasWorkingMaterial; }),
			LOCTEXT("GlobalUVRotation90Hint", "Rotate the complete material UVs 90 degrees. All channels rotate together, including tangent-space normal direction."),
			FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
			{
				SetGlobalUVRotation90(State == ECheckBoxState::Checked);
			}),
			SNew(STextBlock)
			.Text(LOCTEXT("GlobalUVRotation90", "90°"))
			.Font(LabelStyle.Font)
			.ColorAndOpacity_Lambda([this]()
			{
				return GetPreviewOverlayLabelColor(false, bGlobalUVRotation90);
			}))
	];
	return Controls;
}

TSharedRef<SWidget> SMixtormat::BuildPreviewSceneControls()
{
	const float RowGap = FMixtormatThemeStore::GetResolved().ControlLayout.RowGap;
	TSharedRef<SVerticalBox> Controls = SNew(SVerticalBox);
	// Light and skylight: they change how the surface reads without changing what the surface is,
	// which is the same class of control as displacement preview.
	Controls->AddSlot().AutoHeight()
	[
		MakeSlider(
			LOCTEXT("PreviewLightIntensityLabel", "Light"),
			TAttribute<double>::CreateLambda([this]() { return static_cast<double>(PreviewLightIntensity); }),
			0.0, 2.0, 0.8, 0.01, false,
			FMixtormatOnSliderValueChanged::CreateLambda([this](const double Value)
			{
				SetPreviewLightIntensity(static_cast<float>(Value));
			}),
			FSimpleDelegate::CreateLambda([this]() { SetPreviewLightIntensity(0.8f); }),
			LOCTEXT("PreviewLightIntensityHint", "Scales the preset key light. The default 0.8 uses 80% of the preset's authored brightness."))
	];
	Controls->AddSlot().AutoHeight().Padding(0.0f, RowGap, 0.0f, 0.0f)
	[
		MakeSlider(
			LOCTEXT("PreviewSkylightIntensityLabel", "Skylight"),
			TAttribute<double>::CreateLambda([this]() { return static_cast<double>(PreviewSkylightIntensity); }),
			0.0, 2.0, 0.1, 0.01, false,
			FMixtormatOnSliderValueChanged::CreateLambda([this](const double Value)
			{
				SetPreviewSkylightIntensity(static_cast<float>(Value));
			}),
			FSimpleDelegate::CreateLambda([this]() { SetPreviewSkylightIntensity(0.1f); }),
			LOCTEXT("PreviewSkylightIntensityHint", "Scales the boosted plugin-cubemap lighting and reflection capture. The default is 10% intensity."))
	];
	return Controls;
}

TSharedRef<SWidget> SMixtormat::BuildPreviewCameraControls()
{
	const FTextBlockStyle LabelStyle = MakePreviewLabelStyle();
	TSharedRef<SVerticalBox> Controls = SNew(SVerticalBox);
	// Which view is on screen, above the FOV: the shaded material, a raw channel, or a debug view.
	Controls->AddSlot().AutoHeight().HAlign(HAlign_Center)
		.Padding(0.0f, 0.0f, 0.0f, FMixtormatThemeStore::GetResolved().PreviewLayout.ToolbarGap)
	[
		SNew(STextBlock)
		.Font(LabelStyle.Font)
		.ColorAndOpacity(LabelStyle.ColorAndOpacity)
		.Text_Lambda([this]()
		{
			return !PreviewViewports.IsEmpty() && PreviewViewports[0].IsValid()
				? PreviewViewports[0]->GetPreviewModeLabel()
				: FText::GetEmpty();
		})
	];
	Controls->AddSlot().AutoHeight()
	[
		MakeSlider(
			LOCTEXT("PreviewFovLabel", "FOV"),
			TAttribute<double>::CreateLambda([this]() { return static_cast<double>(PreviewFov); }),
			static_cast<double>(MixtormatPreviewCamera::FovMinimum),
			static_cast<double>(MixtormatPreviewCamera::FovMaximum),
			static_cast<double>(MixtormatPreviewCamera::OverlayFovDefault),
			0.5, false,
			FMixtormatOnSliderValueChanged::CreateLambda([this](const double Value)
			{
				SetPreviewFov(static_cast<float>(Value));
			}),
			FSimpleDelegate::CreateLambda([this]()
			{
				SetPreviewFov(MixtormatPreviewCamera::OverlayFovDefault);
			}),
			LOCTEXT("PreviewFovHint", "Preview camera field of view."))
	];
	return Controls;
}

TSharedRef<SWidget> SMixtormat::BuildPreviewOutputControls()
{
	const ISlateStyle& Style = FMixtormatStyle::Get();
	const FTextBlockStyle LabelStyle = MakePreviewLabelStyle();
	const TArray<FText> ResolutionOptions = {
		LOCTEXT("Resolution1K", "1K"),
		LOCTEXT("Resolution2K", "2K"),
		LOCTEXT("Resolution4K", "4K")};
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SButton)
			.Visibility_Lambda([this]()
			{
				return DebugPreviewMode == EMixtormatDebugPreviewMode::None
					? EVisibility::Collapsed
					: EVisibility::Visible;
			})
			.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
			.ContentPadding(FMargin(FMixtormatThemeStore::GetResolved().ControlLayout.ButtonPaddingCompact, 0.0f))
			.ToolTipText(LOCTEXT("ClearDebugPreviewHint", "Return to the composite preview"))
			.OnClicked_Lambda([this]()
			{
				DebugPreviewMode = EMixtormatDebugPreviewMode::None;
				ChildPreviewTarget = FMixtormatChildPreviewTarget();
				RefreshLayeredPreview(false);
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Font(LabelStyle.Font)
				.ColorAndOpacity(FSlateColor::UseForeground())
				.Text_Lambda([this]()
				{
					switch (DebugPreviewMode)
					{
					case EMixtormatDebugPreviewMode::GeneratedFeature: return LOCTEXT("DebugGeneratedFeature", "Feature ×");
					case EMixtormatDebugPreviewMode::HeightBlend: return LOCTEXT("DebugHeightBlend", "Height blend ×");
					case EMixtormatDebugPreviewMode::ContactAO: return LOCTEXT("DebugContactAO", "Contact AO ×");
					case EMixtormatDebugPreviewMode::BorderNormal: return LOCTEXT("DebugBorderNormal", "Border normal ×");
					case EMixtormatDebugPreviewMode::LayerMask: return LOCTEXT("DebugLayerMask", "Layer mask ×");
					case EMixtormatDebugPreviewMode::Stain: return LOCTEXT("DebugStain", "Stain ×");
					case EMixtormatDebugPreviewMode::Runoff: return LOCTEXT("DebugRunoff", "Runoff ×");
					case EMixtormatDebugPreviewMode::LayerUV: return LOCTEXT("DebugLayerUV", "UV ×");
					case EMixtormatDebugPreviewMode::ChildOutput:
					{
						// Named after whichever output is active, not the mode: one mode covers
						// every child-published output, so "Child output ×" would tell the user
						// nothing "Gap ×" or "Region IDs ×" doesn't.
						if (const FMixtormatLayerChild* Child =
							ResolveChild(SelectedLayerIndex, GetSelectedChildIndex()))
						{
							const FMixtormatChildPreviewOutputSet OutputSet = GetChildPreviewOutputSet(*Child);
							const auto Matches = [this](const FMixtormatPreviewOutputDesc& Desc)
							{
								return Desc.Name == ChildPreviewTarget.OutputName
									&& Desc.Kind == ChildPreviewTarget.Kind;
							};
							if (OutputSet.Primary.IsSet() && Matches(OutputSet.Primary.GetValue()))
							{
								return FText::Format(
									LOCTEXT("DebugChildOutputFmt", "{0} ×"), OutputSet.Primary.GetValue().Label);
							}
							for (const FMixtormatPreviewOutputDesc& Desc : OutputSet.Secondary)
							{
								if (Matches(Desc))
								{
									return FText::Format(LOCTEXT("DebugChildOutputFmt", "{0} ×"), Desc.Label);
								}
							}
						}
						return LOCTEXT("DebugChildOutput", "Preview ×");
					}
					default: return FText::GetEmpty();
					}
				})
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(
			FMixtormatThemeStore::GetResolved().PreviewLayout.ToolbarGap, 0.0f, 0.0f, 0.0f).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(FMixtormatThemeStore::GetResolved().PreviewLayout.ResolutionControlWidth)
			[
				SNew(SMixtormatSegmentedControl)
				.Options(ResolutionOptions)
				.ActiveIndex_Lambda([this]()
				{
					return CompositionResolution >= 4096 ? 2 : CompositionResolution >= 2048 ? 1 : 0;
				})
				.OnChosen_Lambda([this](const int32 Index)
				{
					SetCompositionResolution(Index == 2 ? 4096 : Index == 1 ? 2048 : 1024);
				})
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildQuickControlsOverlay()
{
	// Top: render. Left: lighting. Right: geometry. Bottom: the Actions placeholder. The gap in the
	// middle row is where the pointer sits, which is what makes this a marking menu rather than a
	// panel that happens to be near the cursor.
	QuickControlsPanel = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			MakePreviewCluster(BuildPreviewRenderControls())
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				MakePreviewCluster(BuildPreviewLightingControls())
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SBox).MinDesiredWidth(MixtormatTokens::QuickControlsCentreGap)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				MakePreviewCluster(BuildPreviewGeometryControls())
			]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			// One disabled row: the context actions are a later pass, and a placeholder that says so
			// is more honest than a menu that does nothing.
			SNew(SBox)
			.IsEnabled(false)
			[
				MakePreviewCluster(
					SNew(STextBlock)
					.Text(LOCTEXT("QuickControlsActionsPlaceholder", "Context actions — later"))
					.ColorAndOpacity(FSlateColor(FMixtormatThemeStore::GetResolved().Palette.Get(
						Mixtormat::EMixtormatColorRole::TextMuted).CopyWithNewOpacity(MixtormatTokens::EmptyStateOpacity))))
			]
		];

	// Placed by padding, like the floating panels, so the pointer owns the position and the viewport
	// owns the clamp. Self-hit-test-invisible: empty viewport must still reach the viewport.
	return SNew(SBox)
		.Padding_Lambda([this]()
		{
			const FVector2D Bounds = GetPreviewViewportBounds();
			QuickControlsSize = QuickControlsPanel.IsValid()
				? QuickControlsPanel->GetDesiredSize()
				: FVector2D::ZeroVector;
			if (bQuickControlsNeedsCentre && QuickControlsSize.X > 0.0 && QuickControlsSize.Y > 0.0)
			{
				// The pointer belongs in the middle gap, so the popup centres itself on it once its
				// size is known -- one layout pass after it opens.
				QuickControlsPosition -= QuickControlsSize * 0.5;
				bQuickControlsNeedsCentre = false;
			}
			// Clamped like the floating panels: a Tab near an edge must not put controls offscreen.
			QuickControlsPosition.X = FMath::Clamp(QuickControlsPosition.X, 0.0,
				FMath::Max(0.0, Bounds.X - QuickControlsSize.X));
			QuickControlsPosition.Y = FMath::Clamp(QuickControlsPosition.Y, 0.0,
				FMath::Max(0.0, Bounds.Y - QuickControlsSize.Y));
			return FMargin(QuickControlsPosition.X, QuickControlsPosition.Y, 0.0f, 0.0f);
		})
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		.Visibility(EVisibility::SelfHitTestInvisible)
		[
			SNew(SBox)
			.Clipping(EWidgetClipping::ClipToBounds)
			.Visibility_Lambda([this]()
			{
				return bQuickControlsOpen ? EVisibility::Visible : EVisibility::Collapsed;
			})
			[QuickControlsPanel.ToSharedRef()]
		];
}

void SMixtormat::ToggleQuickControls()
{
	if (bQuickControlsOpen)
	{
		CloseQuickControls();
		return;
	}
	const FVector2D Local = GetPreviewViewportLocalPosition(FSlateApplication::Get().GetCursorPos());
	const FVector2D Bounds = GetPreviewViewportBounds();
	// The pointer has to be over exposed viewport content: a Tab while it sits on a floating panel
	// belongs to that panel, not to the viewport underneath.
	if (Local.X < 0.0 || Local.Y < 0.0 || Local.X > Bounds.X || Local.Y > Bounds.Y)
	{
		return;
	}
	if (InspectorPlacement == EInspectorPlacement::Overlay && MixtormatOverlay::IsHit(InspectorOverlay, Local))
	{
		return;
	}
	if (LeftPanelPlacement == ELeftPanelPlacement::Overlay && MixtormatOverlay::IsHit(LeftPanelOverlay, Local))
	{
		return;
	}
	QuickControlsPosition = Local;
	bQuickControlsNeedsCentre = true;
	bQuickControlsOpen = true;
}

void SMixtormat::CloseQuickControls()
{
	bQuickControlsOpen = false;
}

#undef LOCTEXT_NAMESPACE
