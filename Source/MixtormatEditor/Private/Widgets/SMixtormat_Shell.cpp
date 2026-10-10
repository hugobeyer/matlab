// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "Style/MixtormatDesignTokens.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "Widgets/Gallery/SMixtormatGalleryTab.h"
#include "Widgets/SMixtormatInternal.h"
#include "UI/Controls/SMixtormatIconRail.h"
#include "UI/Controls/SMixtormatGroupAction.h"
#include "UI/Menus/SMixtormatHelp.h"
#include "UI/Controls/MixtormatShellSplitterStyle.h"
#include "Style/MixtormatThemeStore.h"
#include "Style/MixtormatTypography.h"
#include "HAL/PlatformProcess.h"
#include "ISettingsModule.h"
#include "MixtormatEditorSettings.h"
#include "Services/MixtormatSurfaceImporter.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"


namespace
{
	// Horizontal shadow under the rail buttons, above the Layers page; not a per-tab shade.
	class SMixtormatRailFade final : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SMixtormatRailFade) {} SLATE_END_ARGS()
		void Construct(const FArguments&) { SetVisibility(EVisibility::HitTestInvisible); }
		FVector2D ComputeDesiredSize(float) const override
		{
			const auto& L = FMixtormatThemeStore::GetResolved().PreviewLayout;
			return FVector2D(L.LeftRailButtonWidth + L.LeftRailFadeExtension, 1.0f);
		}
		int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&,
			FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool) const override
		{
			const auto& L = FMixtormatThemeStore::GetResolved().PreviewLayout;
			const float Width = G.GetLocalSize().X;
			const float Edge = FMath::Min(FMath::Max(L.LeftRailButtonWidth, 0.0f), Width);
			const float Opacity = FMath::Clamp(L.LeftRailFadeOpacity, 0.0f, 1.0f);
			if (Width <= Edge || Opacity <= 0.0f) return Layer;
			FLinearColor Shade = FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Shade);
			Shade.A *= Opacity * Style.GetColorAndOpacityTint().A;
			TArray<FSlateGradientStop> Stops;
			// A shadow *beneath* the rail: full strength at its outer edge, then
			// transparent over the Layers page. The buttons paint on top of this pass.
			Stops.Add(FSlateGradientStop(FVector2D(0.0f, 0.0f), Shade));
			Stops.Add(FSlateGradientStop(FVector2D(Edge, 0.0f), Shade));
			Shade.A = 0.0f;
			Stops.Add(FSlateGradientStop(FVector2D(Width, 0.0f), Shade));
			FSlateDrawElement::MakeGradient(Elements, Layer + 10, G.ToPaintGeometry(), Stops, Orient_Horizontal);
			return Layer + 11;
		}
	};

	using SMixtormatShellAction = SMixtormatGroupAction;

	float TopBarActionHeight()
	{
		const Mixtormat::FMixtormatShellMetrics& S = FMixtormatThemeStore::GetResolved().ShellLayout;
		return FMath::Max(1.0f, S.TopBarHeight - S.TopBarActionInset * 2.0f);
	}

	float TopBarIconSize()
	{
		return FMixtormatThemeStore::GetResolved().Icons.Roles[
			static_cast<uint8>(Mixtormat::EMixtormatIconRole::TopBar)].GlyphSize;
	}

	FSlateColor TopBarIconTint()
	{
		const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
		const Mixtormat::FMixtormatIconStyle& Icon = Resolved.Icons.Roles[
			static_cast<uint8>(Mixtormat::EMixtormatIconRole::TopBar)];
		return FSlateColor(Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text)
			.CopyWithNewOpacity(Icon.RestOpacity));
	}
}

// Window chrome: top bar, page routing, splitters, status bar.

#define LOCTEXT_NAMESPACE "SMixtormat"

FReply SMixtormat::ShowLeftPage(const int32 PageIndex)
{
	LeftTabIndex = PageIndex;
	if (PageIndex != 0)
	{
		SelectedChildIndex = INDEX_NONE;
		SelectedChildScope = EMixtormatChildScope::Layer;
	}
	if (LeftSwitcher.IsValid())
	{
		LeftSwitcher->SetActiveWidgetIndex(PageIndex);
	}
	return FReply::Handled();
}

TSharedRef<SWidget> SMixtormat::BuildTopBar()
{
	const ISlateStyle& Style = FMixtormatStyle::Get();
	const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
	const FTextBlockStyle TopBarTextStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::TopBar),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text));
	const bool bHasDeveloperSources =
		!FMixtormatSurfaceImporter::EnumerateShippedSourceDirectories().IsEmpty();
	return SNew(SBox)
		.HeightOverride(FMixtormatThemeStore::GetResolved().ShellLayout.TopBarHeight)
		[\
			SNew(SBorder)
			.Padding(FMargin(FMixtormatThemeStore::GetResolved().ShellLayout.PanelPadding, 0.0f))
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
			.BorderBackgroundColor(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Ground))
			[\
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[\
					SNew(SMixtormatShellAction, true, TopBarActionHeight())
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.IsEnabled_Lambda([this]() { return !UndoHistory.IsEmpty(); })
					.Text(LOCTEXT("UndoMaterialEditCompact", "Undo"))
					.ToolTipText(LOCTEXT("UndoMaterialEditHint", "Undo the last Mixtormat recipe edit (Ctrl+Z)."))
					.OnClicked(this, &SMixtormat::UndoMaterialEdit)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[\
					SNew(SMixtormatShellAction, true, TopBarActionHeight())
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.IsEnabled_Lambda([this]() { return !RedoHistory.IsEmpty(); })
					.Text(LOCTEXT("RedoMaterialEditCompact", "Redo"))
					.ToolTipText(LOCTEXT("RedoMaterialEditHint", "Redo the last Mixtormat recipe edit (Ctrl+Y or Ctrl+Shift+Z)."))
					.OnClicked(this, &SMixtormat::RedoMaterialEdit)
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(FMixtormatThemeStore::GetResolved().ShellLayout.PanelPadding, 0.0f)
				[\
					SNew(STextBlock)
					.Text_Lambda([this]() { return FText::FromString(WorkingMaterialName); })
					.Clipping(EWidgetClipping::ClipToBounds)
					.Font(TopBarTextStyle.Font)
					.ColorAndOpacity(TopBarTextStyle.ColorAndOpacity)
				]

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[\
					SNew(SMixtormatShellAction, true, TopBarActionHeight())
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.ToolTipText(LOCTEXT("NewMaterialTopHint", "Start a new material workspace, confirming unsaved changes first."))
					.IsEnabled_Lambda([this]() { return !bIsBaking; })
					.OnClicked(this, &SMixtormat::NewWorkingMaterial)
					[\
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[\
							SNew(SBox)
							.WidthOverride(TopBarIconSize())
							.HeightOverride(TopBarIconSize())
							[\
								SNew(SImage).Image(MixtormatIcons::Add()).ColorAndOpacity(TopBarIconTint())
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(FMixtormatThemeStore::GetResolved().ControlLayout.ToolbarLabelPadding, 0.0f).VAlign(VAlign_Center)
						[\
							SNew(STextBlock).Text(LOCTEXT("NewMaterialTop", "NEW"))
							.Font(TopBarTextStyle.Font)
							.RenderOpacity(TopBarTextStyle.ColorAndOpacity.GetSpecifiedColor().A)
							.ColorAndOpacity(FSlateColor::UseForeground())
						]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[\
					SNew(SMixtormatShellAction, true, TopBarActionHeight())
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.OnClicked(this, &SMixtormat::OpenWorkingMaterial)
					[\
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[\
							SNew(SBox)
							.WidthOverride(TopBarIconSize())
							.HeightOverride(TopBarIconSize())
							[\
								SNew(SImage).Image(MixtormatIcons::Folder()).ColorAndOpacity(TopBarIconTint())
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(FMixtormatThemeStore::GetResolved().ControlLayout.ToolbarLabelPadding, 0.0f).VAlign(VAlign_Center)
						[\
							SNew(STextBlock).Text(LOCTEXT("LoadMaterialTop", "LOAD"))
							.Font(TopBarTextStyle.Font)
							.RenderOpacity(TopBarTextStyle.ColorAndOpacity.GetSpecifiedColor().A)
							.ColorAndOpacity(FSlateColor::UseForeground())
						]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[\
					SNew(SMixtormatShellAction, true, TopBarActionHeight())
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.IsEnabled_Lambda([this]() { return bHasWorkingMaterial && !bIsBaking; })
					.ToolTipText(LOCTEXT("SaveMaterialTopHint", "Save changes to the active material recipe."))
					.OnClicked(this, &SMixtormat::SaveWorkingMaterial)
					[\
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[\
							SNew(SBox)
							.WidthOverride(TopBarIconSize())
							.HeightOverride(TopBarIconSize())
							[\
								SNew(SImage).Image(MixtormatIcons::Save()).ColorAndOpacity(TopBarIconTint())
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(FMixtormatThemeStore::GetResolved().ControlLayout.ToolbarLabelPadding, 0.0f).VAlign(VAlign_Center)
						[\
							SNew(STextBlock).Text(LOCTEXT("SaveMaterialTop", "SAVE"))
							.Font(TopBarTextStyle.Font)
							.RenderOpacity(TopBarTextStyle.ColorAndOpacity.GetSpecifiedColor().A)
							.ColorAndOpacity(FSlateColor::UseForeground())
						]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[\
					SNew(SMixtormatShellAction, true, TopBarActionHeight())
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.IsEnabled_Lambda([this]() { return bHasWorkingMaterial && !bIsBaking; })
					.ToolTipText(LOCTEXT("SaveAsMaterialTopHint", "Save the active material recipe under a new name."))
					.OnClicked(this, &SMixtormat::SaveAsWorkingMaterial)
					[\
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[\
							SNew(SBox)
							.WidthOverride(TopBarIconSize())
							.HeightOverride(TopBarIconSize())
							[\
								SNew(SImage).Image(MixtormatIcons::Save()).ColorAndOpacity(TopBarIconTint())
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(FMixtormatThemeStore::GetResolved().ControlLayout.ToolbarLabelPadding, 0.0f).VAlign(VAlign_Center)
						[\
							SNew(STextBlock).Text(LOCTEXT("SaveAsMaterialTop", "SAVE AS..."))
							.Font(TopBarTextStyle.Font)
							.RenderOpacity(TopBarTextStyle.ColorAndOpacity.GetSpecifiedColor().A)
							.ColorAndOpacity(FSlateColor::UseForeground())
						]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[\
					SNew(SMixtormatShellAction, true, TopBarActionHeight())
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.Visibility_Lambda([bHasDeveloperSources]()
					{
						return bHasDeveloperSources ? EVisibility::Visible : EVisibility::Collapsed;
					})
					.ToolTipText(LOCTEXT("ImportShippedSourcesTopHint", "Import every unpacked texture folder found in Content/Mixtormat/Sources into UMixtormatSurface assets."))
					.OnClicked_Lambda([this]()
					{
						const int32 Count = FMixtormatSurfaceImporter::ImportAllShippedSources();
						RefreshGallerySurfaces();
						SetStatusMessage(FString::Printf(TEXT("Imported %d surfaces"), Count));
						return FReply::Handled();
					})
					[\
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[\
							SNew(SBox)
							.WidthOverride(TopBarIconSize())
							.HeightOverride(TopBarIconSize())
							[\
								SNew(SImage).Image(MixtormatIcons::ImportSources()).ColorAndOpacity(TopBarIconTint())
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(FMixtormatThemeStore::GetResolved().ControlLayout.ToolbarLabelPadding, 0.0f).VAlign(VAlign_Center)
						[\
							SNew(STextBlock).Text(LOCTEXT("ImportShippedSourcesTop", "IMPORT SOURCES"))
							.Font(TopBarTextStyle.Font)
							.RenderOpacity(TopBarTextStyle.ColorAndOpacity.GetSpecifiedColor().A)
							.ColorAndOpacity(FSlateColor::UseForeground())
						]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[\
					SNew(SMixtormatShellAction, true, TopBarActionHeight())
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.ToolTipText(LOCTEXT("OpenDocumentationTopHint", "Open the offline HTML manual."))
					.OnClicked_Lambda([]()
					{
						const FString DocsPath = FPaths::Combine(
							FPaths::ProjectPluginsDir(), TEXT("Mixtormat"), TEXT("Docs"), TEXT("Documentation.html"));
						const FString AbsolutePath = FPaths::ConvertRelativePathToFull(DocsPath);
						FPlatformProcess::LaunchFileInDefaultExternalApplication(*AbsolutePath);
						return FReply::Handled();
					})
					[\
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[\
							SNew(SBox)
							.WidthOverride(TopBarIconSize())
							.HeightOverride(TopBarIconSize())
							[\
								SNew(SImage).Image(MixtormatIcons::Docs()).ColorAndOpacity(TopBarIconTint())
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(FMixtormatThemeStore::GetResolved().ControlLayout.ToolbarLabelPadding, 0.0f).VAlign(VAlign_Center)
						[\
							SNew(STextBlock).Text(LOCTEXT("DocsTop", "DOCS"))
							.Font(TopBarTextStyle.Font)
							.RenderOpacity(TopBarTextStyle.ColorAndOpacity.GetSpecifiedColor().A)
							.ColorAndOpacity(FSlateColor::UseForeground())
						]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[\
					SNew(SMixtormatShellAction, true, TopBarActionHeight())
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.TopButton")))
					.ToolTipText(LOCTEXT("ThemeEditorTopHint", "Author and customize UI tokens, layout metrics, and color palettes live."))
					.OnClicked(this, &SMixtormat::OpenThemeEditor)
					[\
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[\
							SNew(SBox)
							.WidthOverride(TopBarIconSize())
							.HeightOverride(TopBarIconSize())
							[\
								SNew(SImage).Image(MixtormatIcons::Theme()).ColorAndOpacity(TopBarIconTint())
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(FMixtormatThemeStore::GetResolved().ControlLayout.ToolbarLabelPadding, 0.0f).VAlign(VAlign_Center)
						[\
							SNew(STextBlock).Text(LOCTEXT("ThemeEditorTop", "THEME"))
							.Font(TopBarTextStyle.Font)
							.RenderOpacity(TopBarTextStyle.ColorAndOpacity.GetSpecifiedColor().A)
							.ColorAndOpacity(FSlateColor::UseForeground())
						]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[\
					SNew(SMixtormatShellAction, true, TopBarActionHeight())
					.ButtonStyle(&Style.GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.PrimaryButton")))
					.IsEnabled_Lambda([this]() { return bHasWorkingMaterial && !bIsBaking; })
					.ToolTipText(LOCTEXT("BakeDialogTopHint", "Bake the active material to textures and create a material instance."))
					.OnClicked(this, &SMixtormat::OpenBakeDialog)
					[\
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[\
							SNew(SBox)
							.WidthOverride(TopBarIconSize())
							.HeightOverride(TopBarIconSize())
							[\
								SNew(SImage).Image(MixtormatIcons::Bake()).ColorAndOpacity(TopBarIconTint())
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(FMixtormatThemeStore::GetResolved().ControlLayout.ToolbarLabelPadding, 0.0f).VAlign(VAlign_Center)
						[\
							SNew(STextBlock).Text(LOCTEXT("BakeMaterialTop", "BAKE"))
							.Font(TopBarTextStyle.Font)
							.RenderOpacity(TopBarTextStyle.ColorAndOpacity.GetSpecifiedColor().A)
							.ColorAndOpacity(FSlateColor::UseForeground())
						]
					]
				]
			]
		];
}

void SMixtormat::ReconstructLeftPanel()
{
	if (LeftPanelDockHost.IsValid()) LeftPanelDockHost->SetContent(SNullWidget::NullWidget);
	LeftPanel.Reset();
	LeftPanel = BuildLayerStackPanel();
	if (LeftPanelDockHost.IsValid() && LeftPanel.IsValid())
	{
		LeftPanelDockHost->SetContent(LeftPanel.ToSharedRef());
	}
}

TSharedRef<SWidget> SMixtormat::BuildUserLibraryPage()
{
	return SNew(SScrollBox)
		.ScrollBarStyle(&FMixtormatStyle::Get().GetWidgetStyle<FScrollBarStyle>(TEXT("Mixtormat.ScrollBar")))
		+ SScrollBox::Slot()
		.Padding(FMargin(FMixtormatThemeStore::GetResolved().ShellLayout.GlobalPagePadding, 0.0f))
		[\
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[\
				SNew(SMixtormatInspectorGroup)
				.Title(LOCTEXT("UserLibraryHeading", "LIBRARY"))
				.InitiallyExpanded(true)
				[\
					SNew(STextBlock)
					.Text(LOCTEXT("UserLibraryEmpty", "Saved materials and user assets will appear here."))
					.ColorAndOpacity(FSlateColor(FMixtormatThemeStore::GetResolved().Palette.Get(
						Mixtormat::EMixtormatColorRole::Text).CopyWithNewOpacity(MixtormatTokens::EmptyStateOpacity)))
				]
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildShell()
{
	const ISlateStyle& Style = FMixtormatStyle::Get();
	const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
	const float Gutter = Resolved.ShellLayout.PanelGutter;

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[BuildTopBar()]
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[\
			SNew(SBorder)
			.Padding(0.0f)
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
			.BorderBackgroundColor(Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Ground))
			[\
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot().AutoWidth()
				[\
					BuildLeftColumn()
				]

				+ SHorizontalBox::Slot().AutoWidth()
				[\
					SNew(SBox)
					.WidthOverride(Gutter)
					[\
						SNew(SBorder)
						.Padding(0.0f)
						.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
						.BorderBackgroundColor(Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Ground))
					]
				]

				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[\
					SNew(SMixtormatSplitter)
					.Orientation(Orient_Horizontal)
					.Style(&FMixtormatShellSplitterStyle::Get())
					.PhysicalSplitterHandleSize(Resolved.ShellLayout.SplitterHandleSize)
					.HitDetectionSplitterHandleSize(Resolved.ShellLayout.SplitterHitSize)

					+ SMixtormatSplitter::Slot()
					.Value_Lambda([this]() -> float
					{
						return InspectorPlacementMode == EMixtormatInspectorPlacement::DockRight
							? CenterFraction : 1.0f;
					})
					.OnSlotResized_Lambda([this](float NewValue)
					{
						if (InspectorPlacementMode == EMixtormatInspectorPlacement::DockRight)
						{
							CenterFraction = FMath::Clamp(NewValue, 0.1f, 0.9f);
						}
					})
					[\
						BuildCenterColumn()
					]

					+ SMixtormatSplitter::Slot()
					.Value_Lambda([this]() -> float
					{
						return InspectorPlacementMode == EMixtormatInspectorPlacement::DockRight
							? (1.0f - CenterFraction) : 0.0f;
					})
					.OnSlotResized_Lambda([this](float NewValue)
					{
						if (InspectorPlacementMode == EMixtormatInspectorPlacement::DockRight)
						{
							CenterFraction = 1.0f - FMath::Clamp(NewValue, 0.1f, 0.9f);
						}
					})
					[\
						SAssignNew(InspectorDockHost, SBox)
						.Visibility_Lambda([this]()
						{
							return InspectorPlacementMode == EMixtormatInspectorPlacement::DockRight
								? EVisibility::Visible : EVisibility::Collapsed;
						})
						[InspectorPanel.ToSharedRef()]
					]
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight()[BuildStatusBar()];
}

TSharedRef<SWidget> SMixtormat::BuildCenterColumn()
{
	const ISlateStyle& Style = FMixtormatStyle::Get();

	return SNew(SOverlay)
		+ SOverlay::Slot()
		[\
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().FillHeight(1.0f)
			[\
				SAssignNew(PreviewViewportHost, SBox)
				[BuildPreviewViewport()]
			]

			+ SVerticalBox::Slot().AutoHeight()
			[\
				SNew(SOverlay)
				+ SOverlay::Slot()
				[\
					SNew(SBox)
					.Visibility_Lambda([this]()
					{
						return bBottomLibraryCollapsed && !bGalleryDrawerAnimating
							? EVisibility::Collapsed : EVisibility::Visible;
					})
					[BuildBottomLibrary()]
				]
				+ SOverlay::Slot().HAlign(HAlign_Center)
				[\
					// A centred handle, not a bar: the collapsed drawer is a fixed-width tab above the
					// status bar, so it stops competing with the layer column's bottom edge.
					SNew(SBox)
					.WidthOverride(MixtormatTokens::GalleryTabWidth)
					.Visibility_Lambda([this]()
					{
						return bBottomLibraryCollapsed && !bGalleryDrawerAnimating
							? EVisibility::Visible : EVisibility::Collapsed;
					})
					[\
						// The action returns FReply; the tab's delegate takes void.
						SNew(SMixtormatHelp)
						.Text(LOCTEXT("RestoreGalleryStyledHint", "Open the material and mask gallery (G)."))
						[\
							SNew(SMixtormatGalleryTab)
							.OnActivated(FSimpleDelegate::CreateLambda([this]() { ToggleBottomLibraryCollapsed(); }))
						]
					]
				]
			]
			]
		]
	;
}

TSharedRef<SWidget> SMixtormat::BuildLeftColumn()
{
	// One continuous page surface. The rail overlays the page's leading inset
	// instead of reserving a separate horizontal column beside it.
	const Mixtormat::FMixtormatPreviewMetrics& Layout =
		FMixtormatThemeStore::GetResolved().PreviewLayout;
	return SNew(SBorder)
		.Padding(0.0f)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
		.BorderBackgroundColor(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Shell))
		[\
			SNew(SOverlay)
			+ SOverlay::Slot()
			[\
				// Only page content is inset; the rail overlays the same dark column.
				SNew(SBox)
				.Padding(FMargin(Layout.LeftRailContentInset, 0.0f, 0.0f, 0.0f))
				[\
					SAssignNew(LeftSwitcher, SWidgetSwitcher)
					.WidgetIndex(LeftTabIndex)
					+ SWidgetSwitcher::Slot()
					[\
						SAssignNew(LeftPanelDockHost, SBox)
						[LeftPanel.ToSharedRef()]
					]
					+ SWidgetSwitcher::Slot()[BuildUserLibraryPage()]
					+ SWidgetSwitcher::Slot()[BuildGlobalPage()]
				]
			]
			+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Fill)
			[\
				SNew(SBox)
				.WidthOverride(Layout.LeftRailButtonWidth + Layout.LeftRailFadeExtension)
				[SNew(SMixtormatRailFade)]
			]
			+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Fill)
			[\
				SNew(SBox)
				.WidthOverride(Layout.LeftRailButtonWidth)
				[\
					SNew(SMixtormatIconRail)
					.Options({\
						MixtormatIcons::Layers(),\
						MixtormatIcons::Library(),\
						MixtormatIcons::Global() })
					.Labels({\
						LOCTEXT("LayersRailLabel", "Layers"),\
						LOCTEXT("LibraryRailLabel", "Library"),\
						LOCTEXT("GlobalRailLabel", "Global") })
					.ToolTips({\
						LOCTEXT("LayersRailHint", "The layer stack: layers, their masks, effects and filters."),
						LOCTEXT("LibraryRailHint", "Saved mixes and imported user surfaces."),
						LOCTEXT("GlobalRailHint", "Document-wide variables and preview settings.") })
					.ActiveIndex_Lambda([this]() { return LeftTabIndex; })
					.OnChosen_Lambda([this](const int32 Index) { ShowLeftPage(Index); })
				]
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildGlobalPage()
{
	// One switch per viewport group: the H/Space master flag still hides everything, and these hide
	// one group each. Session state on the retained workspace, so a theme rebuild keeps them and a
	// hidden group keeps its hotkeys.
	const auto AddGroupToggle = [this](const TSharedRef<SVerticalBox>& Panel, const FText& Label,
		const FText& ToolTip, bool& bFlag)
	{
		AddSliderRow(Panel, MixtormatRow::Make(
			Label,
			MixtormatRow::MakeCheckbox(
				TAttribute<ECheckBoxState>::CreateLambda([&bFlag]()
				{
					return bFlag ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				}),
				FOnCheckStateChanged::CreateLambda([&bFlag](const ECheckBoxState State)
				{
					bFlag = State == ECheckBoxState::Checked;
				}),
				ToolTip)));
	};
	// The settings are the same builders the viewport overlay uses, so the two views cannot drift:
	// a control added to a strip appears here, and both write the same state. Cards, because a flat
	// list of every preview control reads as a wall; each card is one feature, and the icon buttons
	// run inline inside it rather than as a column of full-width bars.
	TSharedRef<SVerticalBox> PreviewSection = SNew(SVerticalBox);
	// Reuse the Inspector's Card builder; page-level gaps remain independent
	// of preview-overlay button spacing or marking-menu geometry.
	const auto AddGlobalCard = [this](const TSharedRef<SVerticalBox>& Panel, const FText& Title)
	{
		if (Panel->GetChildren()->Num() > 0)
		{
			Panel->AddSlot().AutoHeight()
			[\
				SNew(SBox).HeightOverride(FMixtormatThemeStore::GetResolved().ShellLayout.GlobalCardGap)
			];
		}
		return AddCard(Panel, Title);
	};
	{
		const TSharedRef<SVerticalBox> Card = AddGlobalCard(PreviewSection, LOCTEXT("GlobalPreviewVisibility", "VISIBILITY"));
		AddGroupToggle(Card, LOCTEXT("PreviewGroupRender", "Render strip"),
			LOCTEXT("PreviewGroupRenderHint", "Render scale and the Final popup on the viewport."),
			bPreviewGroupRenderVisible);
		AddGroupToggle(Card, LOCTEXT("PreviewGroupLighting", "Lighting"),
			LOCTEXT("PreviewGroupLightingHint", "Studio presets, the camera and lighting reset, and the light and skylight sliders."),
			bPreviewGroupLightingVisible);
		AddGroupToggle(Card, LOCTEXT("PreviewGroupGeometry", "Geometry"),
			LOCTEXT("PreviewGroupGeometryHint", "The preview mesh buttons and the UV 90° toggle."),
			bPreviewGroupGeometryVisible);
		AddGroupToggle(Card, LOCTEXT("PreviewGroupCamera", "Camera"),
			LOCTEXT("PreviewGroupCameraHint", "The preview mode label and the FOV slider."),
			bPreviewGroupCameraVisible);
		AddGroupToggle(Card, LOCTEXT("PreviewGroupOutput", "Output"),
			LOCTEXT("PreviewGroupOutputHint", "Composition resolution and the clear-debug control."),
			bPreviewGroupOutputVisible);
	}
	{
		const TSharedRef<SVerticalBox> Card = AddGlobalCard(PreviewSection, LOCTEXT("GlobalPreviewRender", "RENDER"));
		AddSliderRow(Card, BuildPreviewRenderControls());
	}
	{
		// Presets and the light sliders are one feature: what the surface is lit by.
		const TSharedRef<SVerticalBox> Card = AddGlobalCard(PreviewSection, LOCTEXT("GlobalPreviewLighting", "LIGHTING"));
		AddGroupToggle(Card, LOCTEXT("PreviewLightGizmo", "Light Gizmo"),
			LOCTEXT("PreviewLightGizmoHint", "Show the lighting direction gizmo while rotating lighting with RMB."),
			bPreviewLightGizmoVisible);
		AddSliderRow(Card, BuildPreviewLightingControls(EPreviewControlLayout::Inline));
		AddSliderRow(Card, BuildPreviewSceneControls());
	}
	{
		const TSharedRef<SVerticalBox> Card = AddGlobalCard(PreviewSection, LOCTEXT("GlobalPreviewGeometry", "GEOMETRY"));
		AddSliderRow(Card, BuildPreviewGeometryControls(EPreviewControlLayout::Inline));
	}
	{
		const TSharedRef<SVerticalBox> Card = AddGlobalCard(PreviewSection, LOCTEXT("GlobalPreviewCamera", "CAMERA"));
		AddSliderRow(Card, BuildPreviewCameraControls());
	}
	{
		const TSharedRef<SVerticalBox> Card = AddGlobalCard(PreviewSection, LOCTEXT("GlobalPreviewOutput", "OUTPUT"));
		AddSliderRow(Card, BuildPreviewOutputControls());
	}

	return SNew(SScrollBox)
		.ScrollBarStyle(&FMixtormatStyle::Get().GetWidgetStyle<FScrollBarStyle>(TEXT("Mixtormat.ScrollBar")))
		+ SScrollBox::Slot()
		.Padding(FMargin(FMixtormatThemeStore::GetResolved().ShellLayout.GlobalPagePadding, 0.0f))
		[\
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[\
				SNew(SMixtormatInspectorGroup)
				.Title(LOCTEXT("GlobalHeading", "GLOBAL"))
				.InitiallyExpanded(true)
				[\
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						return bHasWorkingMaterial
							? LOCTEXT("GlobalEmpty", "No global variables yet.")
							: LOCTEXT("GlobalNoMaterial", "Create or open a material to add global variables.");
					})
					.ColorAndOpacity(FSlateColor(FMixtormatThemeStore::GetResolved().Palette.Get(
						Mixtormat::EMixtormatColorRole::Text).CopyWithNewOpacity(MixtormatTokens::EmptyStateOpacity)))
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[\
				SNew(SMixtormatInspectorGroup)
				.Title(LOCTEXT("GlobalPreviewHeading", "PREVIEW / VIEWPORT"))
				.InitiallyExpanded(true)
				[PreviewSection]
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildStatusBar()
{
	const ISlateStyle& Style = FMixtormatStyle::Get();
	const FTextBlockStyle& MutedStyle =
		Style.GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.MutedText"));

	return SNew(SBox)
		.HeightOverride(FMixtormatThemeStore::GetResolved().ShellLayout.StatusBarHeight)
		[\
			SNew(SBorder)
			.Padding(FMargin(FMixtormatThemeStore::GetResolved().ShellLayout.PanelPadding, 0.0f))
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
			.BorderBackgroundColor(
				FMixtormatThemeStore::GetResolved().Palette.Get(
					Mixtormat::EMixtormatColorRole::Ground))
			[\
				SNew(SOverlay)

				+ SOverlay::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[\
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						return FText::FromString(WorkingStatusText);
					})
					.TextStyle(&MutedStyle)
					.ColorAndOpacity(FSlateColor(
						FMixtormatThemeStore::GetResolved().Palette.Get(
							Mixtormat::EMixtormatColorRole::TextMuted)))
				]

				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[\
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						const FText QualityText =
							PreviewQuality == EMixtormatPreviewQuality::Lumen
								? LOCTEXT("StatusQualityLumen", "Lumen On")
								: LOCTEXT("StatusQualityDefault", "Default · Studio AO");

						return FText::Format(
							LOCTEXT(
								"RealtimeStatusDynamic",
								"Real-time Preview · {0} · SM6"),
							QualityText);
					})
					.TextStyle(&MutedStyle)
					.ColorAndOpacity(FSlateColor(
						FMixtormatThemeStore::GetResolved().Palette.Get(
							Mixtormat::EMixtormatColorRole::TextMuted)))
				]

				+ SOverlay::Slot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[\
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						return FText::Format(
							LOCTEXT("LayerStatus", "Layers {0}"),
							FText::AsNumber(WorkingLayers.Num()));
					})
					.TextStyle(&MutedStyle)
					.ColorAndOpacity(FSlateColor(
						FMixtormatThemeStore::GetResolved().Palette.Get(
							Mixtormat::EMixtormatColorRole::TextMuted)))
				]
			]
		];
}

FReply SMixtormat::ToggleBottomLibraryCollapsed()
{
	if (!bGalleryDrawerAnimating)
	{
		GalleryDrawerAnimatedHeight = bBottomLibraryCollapsed
			? FMixtormatThemeStore::GetResolved().GalleryLayout.DrawerCollapsedHeight
			: (GalleryDrawerHeight > 0.0f
				? GalleryDrawerHeight
				: FMixtormatThemeStore::GetResolved().GalleryLayout.DrawerInitialHeight);
	}
	bBottomLibraryCollapsed = !bBottomLibraryCollapsed;
	bGalleryPointerInside = !bBottomLibraryCollapsed;

	if (!bGalleryDrawerAnimating)
	{
		bGalleryDrawerAnimating = true;
		RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda([this](double, const float DeltaTime)
		{
			const Mixtormat::FMixtormatGalleryMetrics& Layout = FMixtormatThemeStore::GetResolved().GalleryLayout;
			const float TargetHeight = bBottomLibraryCollapsed
				? Layout.DrawerCollapsedHeight
				: (GalleryDrawerHeight > 0.0f ? GalleryDrawerHeight : Layout.DrawerInitialHeight);
			GalleryDrawerAnimatedHeight = FMath::FInterpTo(
				GalleryDrawerAnimatedHeight, TargetHeight, DeltaTime, 14.0f);
			if (FMath::Abs(GalleryDrawerAnimatedHeight - TargetHeight) <= 0.5f)
			{
				GalleryDrawerAnimatedHeight = TargetHeight;
				bGalleryDrawerAnimating = false;
				Invalidate(EInvalidateWidgetReason::Layout);
				return EActiveTimerReturnType::Stop;
			}
			Invalidate(EInvalidateWidgetReason::Layout);
			return EActiveTimerReturnType::Continue;
		}));
	}
	return FReply::Handled();
}


#undef LOCTEXT_NAMESPACE
