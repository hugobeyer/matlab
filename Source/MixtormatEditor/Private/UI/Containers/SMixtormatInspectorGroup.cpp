#include "UI/Containers/SMixtormatInspectorGroup.h"

#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatPalette.h"
#include "Style/MixtormatStyle.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "UI/Atoms/SMixtormatIconButton.h"
#include "UI/Primitives/SMixtormatGradientBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "Mixtormat"

void SMixtormatInspectorGroup::Construct(const FArguments& InArgs)
{
	bExpanded = InArgs._InitiallyExpanded;

	TSharedRef<SHorizontalBox> Header = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, MixtormatTokens::GroupHeaderItemGap, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(MixtormatTokens::ChevronSize)
			.HeightOverride(MixtormatTokens::ChevronSize)
			[
				SNew(SImage)
				.Image_Lambda([this]()
				{
					return bExpanded ? MixtormatIcons::ChevronDown() : MixtormatIcons::ChevronRight();
				})
				.ColorAndOpacity(FSlateColor(MixtormatPalette::HeaderText()))
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.SectionHeader")))
			.Text(InArgs._Title)
		];

	// State, when the group has any. Hidden rather than blank so it takes no width when empty.
	if (InArgs._StateText.IsSet())
	{
		Header->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(MixtormatTokens::RowLabelGap, 0.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.TextStyle(&FMixtormatStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("Mixtormat.BadgeText")))
			.ColorAndOpacity(InArgs._StateColor.IsSet()
				? InArgs._StateColor
				: TAttribute<FSlateColor>(FSlateColor(MixtormatPalette::Modified())))
			.Text(InArgs._StateText)
			.Visibility_Lambda([State = InArgs._StateText]()
			{
				return State.Get(FText::GetEmpty()).IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
			})
		];
	}

	if (InArgs._HeaderAction.IsValid())
	{
		Header->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(MixtormatTokens::GroupHeaderItemGap, 0.0f, 0.0f, 0.0f)
		[
			InArgs._HeaderAction.ToSharedRef()
		];
	}

	// Reset is always last and right-aligned, so its position never shifts between groups.
	if (InArgs._OnReset.IsBound())
	{
		Header->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(MixtormatTokens::GroupHeaderItemGap, 0.0f, 0.0f, 0.0f)
		[
			SNew(SMixtormatIconButton)
			.Icon(MixtormatIcons::Refresh())
			.Size(MixtormatTokens::IconButtonSize)
			.ToolTipText(LOCTEXT("ResetGroup", "Reset this group to its defaults"))
			.OnClicked(InArgs._OnReset)
		];
	}

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			// The bar itself is the click target and the hover surface. A button on top of it would
			// light a button-shaped patch inside the header instead of the header.
			SNew(SMixtormatGradientBox)
			.StartColor(this, &SMixtormatInspectorGroup::GetHeaderTint)
			.EndColor(MixtormatPalette::Panel())
			.Orientation(Orient_Vertical)
			[
				// The lip sits over the header's top edge and spans its full width, so it is
				// outside the gutter padding -- inside it, the line would stop short of both ends
				// and read as an underline on the title rather than as the seam of the group.
				SNew(SOverlay)
				+ SOverlay::Slot()
				.VAlign(VAlign_Top)
				[
					SNew(SBox)
					.HeightOverride(MixtormatTokens::HairlineThickness)
					[
						SNew(SImage)
						.Image(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.HeaderHairline")))
					]
				]
				+ SOverlay::Slot()
				[
					// The whole bar is the click target. An invisible button rather than a mouse
					// handler on the group, because the group also contains the body and a press
					// down there must not collapse what the user is reaching into.
					SNew(SButton)
					.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(
						TEXT("Mixtormat.InspectorHeaderButton")))
					.ContentPadding(FMargin(0.0f))
					.OnClicked(this, &SMixtormatInspectorGroup::ToggleExpanded)
					[
						SNew(SBox)
						.HeightOverride(MixtormatTokens::GroupHeaderHeight)
						.Padding(FMargin(MixtormatTokens::PanelGutter, 0.0f))
						.VAlign(VAlign_Center)
						[
							Header
						]
					]
				]
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.Visibility_Lambda([this]() { return bExpanded ? EVisibility::Visible : EVisibility::Collapsed; })
			.BorderImage(FMixtormatStyle::Get().GetBrush(TEXT("Mixtormat.GroupBody")))
			.Padding(FMargin(
				MixtormatTokens::PanelGutter,
				MixtormatTokens::GroupBodyTopInset,
				MixtormatTokens::PanelGutter,
				MixtormatTokens::PanelGutter))
			[
				InArgs._Content.Widget
			]
		]
	];
}

FLinearColor SMixtormatInspectorGroup::GetHeaderTint() const
{
	return IsHovered() ? MixtormatPalette::HeaderHover() : MixtormatPalette::HeaderTint();
}

FReply SMixtormatInspectorGroup::ToggleExpanded()
{
	bExpanded = !bExpanded;
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
