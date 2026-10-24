// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormatWelcomeCard.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatStyle.h"
#include "Style/MixtormatThemeStore.h"
#include "Style/MixtormatTypography.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "UI/Atoms/SMixtormatIconButton.h"
#include "UI/Containers/SMixtormatInspectorCard.h"
#include "UI/Controls/SMixtormatSegmentedControl.h"
#include "UI/Menus/SMixtormatHelp.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SNullWidget.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

void SMixtormatWelcomeCard::Construct(const FArguments& InArgs)
{
	OnDismiss = InArgs._OnDismiss;

	ViewSwitcher = SNew(SWidgetSwitcher)
		+ SWidgetSwitcher::Slot()
		[
			BuildOverviewView()
		]
		+ SWidgetSwitcher::Slot()
		[
			BuildGuideView()
		];

	ChildSlot
	[
		SNew(SBox)
		.WidthOverride_Lambda([]() { return FMixtormatThemeStore::GetResolved().ControlLayout.WelcomeCardWidth; })
		.MaxDesiredHeight_Lambda([]() { return FMixtormatThemeStore::GetResolved().ControlLayout.WelcomeCardMaxHeight; })
		[
			SNew(SMixtormatInspectorCard)
			.Title(LOCTEXT("WelcomeTitle", "Welcome to Mixtormat"))
			.HeaderAction(
				SNew(SMixtormatIconButton)
				.Icon(MixtormatIcons::Close())
				.ToolTip(LOCTEXT("CloseWelcomeHint", "Dismiss Welcome"))
				.OnClicked(this, &SMixtormatWelcomeCard::HandleDismiss)
			)
			[
				ViewSwitcher.ToSharedRef()
			]
		]
	];
}

TSharedRef<SWidget> SMixtormatWelcomeCard::BuildFeatureRow(const FText& Title, const FText& Description)
{
	const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
	const FTextBlockStyle TitleStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::ControlLabel),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text));
	const FTextBlockStyle DescStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::Caption),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted));

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(Title)
			.Font(TitleStyle.Font)
			.ColorAndOpacity(TitleStyle.ColorAndOpacity)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(Description)
			.Font(DescStyle.Font)
			.ColorAndOpacity(DescStyle.ColorAndOpacity)
			.AutoWrapText(true)
		];
}

TSharedRef<SWidget> SMixtormatWelcomeCard::BuildOverviewView()
{
	const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
	const FTextBlockStyle BodyStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::Body),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text));

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SScrollBox)
			.Orientation(Orient_Vertical)
			+ SScrollBox::Slot()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 12.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("WelcomeOverviewIntro",
						"Mixtormat is a real-time procedural material mixer for Unreal Engine 5.8. "
						"Author non-destructive material layers, procedural patterns, and curvature masks directly on the GPU, "
						"then bake optimized textures and material instances."))
					.Font(BodyStyle.Font)
					.ColorAndOpacity(BodyStyle.ColorAndOpacity)
					.AutoWrapText(true)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 8.0f)
				[
					BuildFeatureRow(
						LOCTEXT("FeatureLayersTitle", "Layers & Hierarchy"),
						LOCTEXT("FeatureLayersDesc", "Non-destructive layer stack supporting standard blend modes, normal map blending, and group hierarchies."))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 8.0f)
				[
					BuildFeatureRow(
						LOCTEXT("FeatureGeneratorsTitle", "Generators & Procedurals"),
						LOCTEXT("FeatureGeneratorsDesc", "GPU-evaluated procedural patterns including cellular, perlin, weave, and tile."))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 8.0f)
				[
					BuildFeatureRow(
						LOCTEXT("FeatureMasksTitle", "Smart Masks & IDs"),
						LOCTEXT("FeatureMasksDesc", "Real-time surface curvature, ambient occlusion, slope, and pattern ID region masking."))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 8.0f)
				[
					BuildFeatureRow(
						LOCTEXT("FeaturePreviewTitle", "3D Viewport & PBR Inspection"),
						LOCTEXT("FeaturePreviewDesc", "Interactive lighting, customizable meshes, and isolated channel debug preview."))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 8.0f)
				[
					BuildFeatureRow(
						LOCTEXT("FeatureBakingTitle", "Material Master Baking"),
						LOCTEXT("FeatureBakingDesc", "Export production-ready textures and Material Instances to default or custom studio material masters."))
				]
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 12.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
				.ContentPadding(FMargin(12.0f, 4.0f))
				.OnClicked_Lambda([this]()
				{
					SwitchToGuide();
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("QuickGuideBtn", "Quick Guide"))
					.Font(BodyStyle.Font)
					.ColorAndOpacity(FSlateColor(Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text)))
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNullWidget::NullWidget
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.PrimaryButton")))
				.ContentPadding(FMargin(16.0f, 4.0f))
				.OnClicked_Lambda([this]()
				{
					HandleDismiss();
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("StartCreatingBtn", "Start Creating"))
					.Font(BodyStyle.Font)
					.ColorAndOpacity(FSlateColor(Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text)))
				]
			]
		];
}

TSharedRef<SWidget> SMixtormatWelcomeCard::BuildGuideView()
{
	const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
	const FTextBlockStyle BodyStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::Body),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text));

	const TArray<FText> TopicOptions = {
		LOCTEXT("TopicLayers", "Layers"),
		LOCTEXT("TopicGenerators", "Generators"),
		LOCTEXT("TopicMasks", "Masks"),
		LOCTEXT("TopicPreview", "Preview"),
		LOCTEXT("TopicBaking", "Baking")
	};

	GuideTopicSwitcher = SNew(SWidgetSwitcher)
		.WidgetIndex_Lambda([this]() { return ActiveTopicIndex; })
		+ SWidgetSwitcher::Slot() [ BuildGuideContent(0) ]
		+ SWidgetSwitcher::Slot() [ BuildGuideContent(1) ]
		+ SWidgetSwitcher::Slot() [ BuildGuideContent(2) ]
		+ SWidgetSwitcher::Slot() [ BuildGuideContent(3) ]
		+ SWidgetSwitcher::Slot() [ BuildGuideContent(4) ];

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 10.0f)
		[
			SNew(SMixtormatSegmentedControl)
			.Options(TopicOptions)
			.ActiveIndex_Lambda([this]() { return ActiveTopicIndex; })
			.OnChosen_Lambda([this](int32 Index)
			{
				ActiveTopicIndex = FMath::Clamp(Index, 0, 4);
				if (GuideTopicSwitcher.IsValid())
				{
					GuideTopicSwitcher->SetActiveWidgetIndex(ActiveTopicIndex);
				}
			})
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SScrollBox)
			.Orientation(Orient_Vertical)
			+ SScrollBox::Slot()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				GuideTopicSwitcher.ToSharedRef()
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 12.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
				.ContentPadding(FMargin(12.0f, 4.0f))
				.OnClicked_Lambda([this]()
				{
					SwitchToOverview();
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("GuideBackBtn", "Back"))
					.Font(BodyStyle.Font)
					.ColorAndOpacity(FSlateColor(Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text)))
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNullWidget::NullWidget
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.PrimaryButton")))
				.ContentPadding(FMargin(16.0f, 4.0f))
				.OnClicked_Lambda([this]()
				{
					HandleDismiss();
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("StartCreatingBtn", "Start Creating"))
					.Font(BodyStyle.Font)
					.ColorAndOpacity(FSlateColor(Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text)))
				]
			]
		];
}

TSharedRef<SWidget> SMixtormatWelcomeCard::BuildGuideContent(int32 TopicIndex)
{
	const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
	const FTextBlockStyle TitleStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::ControlLabel),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text));

	FText HeaderText;
	TArray<TPair<FText, FText>> Points;

	switch (TopicIndex)
	{
	case 0: // Layers
		HeaderText = LOCTEXT("GuideLayersHeader", "Layer Hierarchy & Composition");
		Points = {
			{ LOCTEXT("GuideLayersP1Title", "Stack Hierarchy"), LOCTEXT("GuideLayersP1Desc", "Layers evaluate from bottom to top. The base layer defines the initial surface, while higher layers modify or paint over it.") },
			{ LOCTEXT("GuideLayersP2Title", "Blend Modes"), LOCTEXT("GuideLayersP2Desc", "Each layer supports standard blend modes (Normal, Multiply, Screen, Overlay) for Base Color and Roughness.") },
			{ LOCTEXT("GuideLayersP3Title", "Normal Blending"), LOCTEXT("GuideLayersP3Desc", "Control how normal maps interact using Combine (Reoriented Normal Mapping), Override (replaces previous normals), or Detail (fine surface overlay).") },
			{ LOCTEXT("GuideLayersP4Title", "Layer Groups"), LOCTEXT("GuideLayersP4Desc", "Organize complex materials into folders and groups. Group visibility and master opacity apply down through child layers.") }
		};
		break;

	case 1: // Generators
		HeaderText = LOCTEXT("GuideGeneratorsHeader", "Procedural Generators & Patterns");
		Points = {
			{ LOCTEXT("GuideGeneratorsP1Title", "GPU Evaluation"), LOCTEXT("GuideGeneratorsP1Desc", "Generators produce procedural textures on the GPU in real time, avoiding disk storage and texture resolution limits during authoring.") },
			{ LOCTEXT("GuideGeneratorsP2Title", "Pattern Library"), LOCTEXT("GuideGeneratorsP2Desc", "Includes cellular noise, perlin noise, Voronoi cells, bricks, tiles, and weave.") },
			{ LOCTEXT("GuideGeneratorsP3Title", "Height Modulation"), LOCTEXT("GuideGeneratorsP3Desc", "Generators contribute to the layer's height map, producing displacement and normal gradients automatically.") },
			{ LOCTEXT("GuideGeneratorsP4Title", "Driver Linkage"), LOCTEXT("GuideGeneratorsP4Desc", "Generator seeds and parameters can be linked to spatial drivers or modulated across layers.") }
		};
		break;

	case 2: // Masks
		HeaderText = LOCTEXT("GuideMasksHeader", "Smart Masks & Spatial Drivers");
		Points = {
			{ LOCTEXT("GuideMasksP1Title", "Curvature & Edges"), LOCTEXT("GuideMasksP1Desc", "Extract convex surface edges to apply edge wear, chipped paint, or highlighted corners.") },
			{ LOCTEXT("GuideMasksP2Title", "Cavity & Occlusion"), LOCTEXT("GuideMasksP2Desc", "Extract concave crevices to accumulate dirt, grime, or moisture in surface recesses.") },
			{ LOCTEXT("GuideMasksP3Title", "Normal & Slope"), LOCTEXT("GuideMasksP3Desc", "Mask by surface direction or world-space angle, ideal for directional dust, moss, or fallen snow.") },
			{ LOCTEXT("GuideMasksP4Title", "ID Regions"), LOCTEXT("GuideMasksP4Desc", "Author ID pattern maps to assign distinct layer properties to specific procedural tile or brick regions.") }
		};
		break;

	case 3: // Preview
		HeaderText = LOCTEXT("GuidePreviewHeader", "3D Viewport & PBR Inspection");
		Points = {
			{ LOCTEXT("GuidePreviewP1Title", "Orbit & Navigation"), LOCTEXT("GuidePreviewP1Desc", "Hold Left Mouse Button and drag in the viewport to orbit around the 3D preview model.") },
			{ LOCTEXT("GuidePreviewP2Title", "Lighting Rotation"), LOCTEXT("GuidePreviewP2Desc", "Hold Right Mouse Button and drag horizontally to rotate the HDRI environment lighting around the asset.") },
			{ LOCTEXT("GuidePreviewP3Title", "Preview Geometry"), LOCTEXT("GuidePreviewP3Desc", "Press Q over the viewport and use Geometry to select Sphere, Plane, Cube, or Cylinder.") },
			{ LOCTEXT("GuidePreviewP4Title", "Channel Isolation"), LOCTEXT("GuidePreviewP4Desc", "Press V to cycle material channels and Shift+V to return to Material. Use W/E for eligible mask/Flow outputs on the selected module, or choose an output in the viewport Output strip.") }
		};
		break;

	case 4: // Baking
		HeaderText = LOCTEXT("GuideBakingHeader", "Production Baking & Material Masters");
		Points = {
			{ LOCTEXT("GuideBakingP1Title", "Output Package"), LOCTEXT("GuideBakingP1Desc", "Baking generates 7 coordinated assets: Base Color, Normal, packed RAM, Height, RAMH, a Material Instance, and a Surface definition.") },
			{ LOCTEXT("GuideBakingP2Title", "Default Master"), LOCTEXT("GuideBakingP2Desc", "Out of the box, Mixtormat binds DA_BaseColor, DA_Normal, and DA_RAMH to the optimized default master material.") },
			{ LOCTEXT("GuideBakingP3Title", "Custom Material Masters"), LOCTEXT("GuideBakingP3Desc", "Select any project Material or Material Instance. Mixtormat discovers its texture parameters and maps outputs without requiring DA_* naming.") },
			{ LOCTEXT("GuideBakingP4Title", "Safe Re-Baking"), LOCTEXT("GuideBakingP4Desc", "Re-baking only replaces overrides Mixtormat originally wrote. Any artist-authored scalar, vector, or unrelated texture overrides remain intact.") }
		};
		break;
	}

	TSharedRef<SVerticalBox> ContentBox = SNew(SVerticalBox);
	ContentBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			SNew(STextBlock)
			.Text(HeaderText)
			.Font(TitleStyle.Font)
			.ColorAndOpacity(TitleStyle.ColorAndOpacity)
		];

	for (const auto& Point : Points)
	{
		ContentBox->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				BuildFeatureRow(Point.Key, Point.Value)
			];
	}

	return ContentBox;
}

void SMixtormatWelcomeCard::SwitchToGuide()
{
	if (ViewSwitcher.IsValid())
	{
		ViewSwitcher->SetActiveWidgetIndex(1);
	}
}

void SMixtormatWelcomeCard::SwitchToOverview()
{
	if (ViewSwitcher.IsValid())
	{
		ViewSwitcher->SetActiveWidgetIndex(0);
	}
}

void SMixtormatWelcomeCard::HandleDismiss()
{
	OnDismiss.ExecuteIfBound();
}

#undef LOCTEXT_NAMESPACE
