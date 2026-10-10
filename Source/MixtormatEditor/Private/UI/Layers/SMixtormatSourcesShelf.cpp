// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Layers/SMixtormatSourcesShelf.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatRecipes.h"
#include "Style/MixtormatStyle.h"
#include "Style/MixtormatThemeStore.h"
#include "Style/MixtormatTypography.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "UI/Containers/SMixtormatFoldoutHeader.h"
#include "UI/Primitives/SMixtormatSurfaceBox.h"
#include "UI/Rows/SMixtormatRow.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void SMixtormatSourcesShelf::Construct(const FArguments& InArgs)
{
	Expanded = InArgs._Expanded;
	OnToggle = InArgs._OnToggle;

	const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
	const Mixtormat::FMixtormatFoldoutMetrics& Layout = Resolved.FoldoutLayout;
	const Mixtormat::FMixtormatIconStyle& DisclosureIcon = Resolved.Icons.Roles[
		static_cast<uint8>(Mixtormat::EMixtormatIconRole::FoldoutDisclosure)];
	// Composed from the resolved typography rather than read off the style set, which is the
	// inspector foldout's own construction: the font is copied, not pointed at, so no style-set
	// entry outlives a theme refresh here.
	const FTextBlockStyle FoldoutTitleStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::FoldoutTitle),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text));

	// The chevron reads at the title's own colour and rest opacity, so it sits at the words'
	// weight rather than as a separate mark beside them.
	FLinearColor ChevronColor = Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text);
	ChevronColor.A *= DisclosureIcon.RestOpacity;

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, Layout.OuterTop, 0.0f, 0.0f)
		[
			SNew(SMixtormatFoldoutHeader)
			.IsHovered_Lambda([this]() { return bHovered; })
			[
				// Behaviour only. The header paints the bar; a button with plates of its own would
				// light a button-shaped patch inside it.
				SNew(SButton)
				.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(
					TEXT("Mixtormat.InspectorHeaderButton")))
				.ContentPadding(FMargin(0.0f))
				.OnClicked(this, &SMixtormatSourcesShelf::ToggleExpanded)
				[
					SNew(SBox)
					.Padding(FMargin(
						Layout.Gutter,
						Layout.HeaderPaddingTop,
						Layout.Gutter,
						Layout.HeaderPaddingBottom))
					.VAlign(VAlign_Center)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, MixtormatTokens::FoldoutHeaderGap, 0.0f)
						[
							// Glyph centred in a box of glyph plus padding, the inspector foldout's
							// anatomy, so the gap to the title matches theirs.
							SNew(SBox)
							.WidthOverride(
								DisclosureIcon.GlyphSize + MixtormatTokens::FoldoutIconPadding * 2.0f)
							.HeightOverride(
								DisclosureIcon.GlyphSize + MixtormatTokens::FoldoutIconPadding * 2.0f)
							.HAlign(HAlign_Center)
							.VAlign(VAlign_Center)
							[
								SNew(SBox)
								.WidthOverride(DisclosureIcon.GlyphSize)
								.HeightOverride(DisclosureIcon.GlyphSize)
								[
									SNew(SImage)
									.Image_Lambda([this]()
									{
										return Expanded.Get(false)
											? MixtormatIcons::ChevronDown()
											: MixtormatIcons::ChevronRight();
									})
									.ColorAndOpacity(FSlateColor(ChevronColor))
								]
							]
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(InArgs._Title)
							.Font(FoldoutTitleStyle.Font)
							.ColorAndOpacity(FoldoutTitleStyle.ColorAndOpacity)
							.Justification(MixtormatRow::JustifyFor(MixtormatTokens::GroupHeaderAlign))
						]
					]
				]
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, Layout.OuterBottom)
		[
			// The body sits on its own ground surface, the foldout's seam, and hides with the
			// chevron. Explicit rather than built only when open, so the slot keeps its shape.
			SNew(SMixtormatSurfaceBox)
			.Visibility_Lambda([this]()
			{
				return Expanded.Get(false) ? EVisibility::Visible : EVisibility::Collapsed;
			})
			.Recipe(Mixtormat::MakeGroundRecipe())
			.InheritWidgetStyle(true)
			.Padding(FMargin(Layout.Gutter, Layout.BodyTop, Layout.Gutter, Layout.BodyBottom))
			[
				InArgs._Content.Widget
			]
		]
	];
}

void SMixtormatSourcesShelf::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);
	bHovered = true;
}

void SMixtormatSourcesShelf::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseLeave(MouseEvent);
	bHovered = false;
}

FReply SMixtormatSourcesShelf::ToggleExpanded()
{
	// Just the notification: the caller flips its own state and the bound attributes follow.
	OnToggle.ExecuteIfBound();
	return FReply::Handled();
}
