// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Containers/SMixtormatInspectorGroup.h"


#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatPalette.h"
#include "Style/MixtormatStyle.h"
#include "Style/MixtormatRecipes.h"
#include "Style/MixtormatThemeStore.h"
#include "Style/MixtormatTypography.h"
#include "UI/Atoms/MixtormatIcons.h"
#include "UI/Atoms/SMixtormatIconButton.h"
#include "UI/Containers/SMixtormatFoldoutHeader.h"
#include "UI/Menus/MixtormatMenuBuilder.h"
#include "UI/Primitives/SMixtormatSurfaceBox.h"
#include "UI/Rows/SMixtormatRow.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMenuAnchor.h"

#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "Mixtormat"

namespace
{
	// Weak, so a group that is torn down and rebuilt on the next selection leaves nothing behind.
	TArray<TWeakPtr<SMixtormatInspectorGroup>> LiveInspectorGroups;

	// Opacity applied to a shared role's alpha, so two weights of one colour differ in strength
	// rather than in hue. Lerping the RGB toward transparent instead would darken the colour too.
	FLinearColor TintAt(const FLinearColor& Color, const float Opacity)
	{
		FLinearColor Result = Color;
		Result.A *= Opacity;
		return Result;
	}
}

TArray<TSharedRef<SMixtormatInspectorGroup>> SMixtormatInspectorGroup::GetLiveGroups()
{
	TArray<TSharedRef<SMixtormatInspectorGroup>> Alive;
	for (int32 Index = LiveInspectorGroups.Num() - 1; Index >= 0; --Index)
	{
		const TSharedPtr<SMixtormatInspectorGroup> Group = LiveInspectorGroups[Index].Pin();
		if (Group.IsValid())
		{
			Alive.Add(Group.ToSharedRef());
		}
		else
		{
			LiveInspectorGroups.RemoveAtSwap(Index);
		}
	}
	return Alive;
}

void SMixtormatInspectorGroup::SetAllExpanded(const bool bInExpanded)
{
	for (const TSharedRef<SMixtormatInspectorGroup>& Group : GetLiveGroups())
	{
		Group->SetExpanded(bInExpanded);
	}
}

SMixtormatInspectorGroup::SMixtormatInspectorGroup() = default;

SMixtormatInspectorGroup::~SMixtormatInspectorGroup()
{
	// Pruned here as well as on access, so a long session does not accumulate dead entries for
	// every selection change.
	LiveInspectorGroups.RemoveAll([](const TWeakPtr<SMixtormatInspectorGroup>& Entry)
	{
		return !Entry.IsValid();
	});
}

FReply SMixtormatInspectorGroup::OnMouseButtonDown(const FGeometry&, const FPointerEvent& MouseEvent)
{
	// Right button only. Left is the header button's, and a press in the body must reach the row
	// the user is aiming at rather than the group around it.
	if (MouseEvent.GetEffectingButton() != EKeys::RightMouseButton || !ContextAnchor.IsValid())
	{
		return FReply::Unhandled();
	}
	ContextAnchor->SetIsOpen(true);
	return FReply::Handled();
}

void SMixtormatInspectorGroup::Construct(const FArguments& InArgs)
{
	bCollapsible = InArgs._Collapsible;
	bExpanded = InArgs._InitiallyExpanded;
	const Mixtormat::FMixtormatResolvedStyle& Resolved = FMixtormatThemeStore::GetResolved();
	const Mixtormat::FMixtormatTextSpec FoldoutTitleSpec = Mixtormat::FMixtormatTypography::GetSpec(
		Resolved.Typography, Mixtormat::EMixtormatTextRole::FoldoutTitle);
	const FTextBlockStyle FoldoutTitleStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		FoldoutTitleSpec, Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text));
	const FTextBlockStyle BadgeTextStyle = Mixtormat::FMixtormatTypography::MakeTextStyle(
		Mixtormat::FMixtormatTypography::GetSpec(Resolved.Typography, Mixtormat::EMixtormatTextRole::Badge),
		Resolved.Palette.Get(Mixtormat::EMixtormatColorRole::Text));
	const Mixtormat::FMixtormatFoldoutMetrics& Layout = Resolved.FoldoutLayout;
	const Mixtormat::FMixtormatIconStyle& DisclosureIcon = FMixtormatThemeStore::GetResolved().Icons.Roles[
		static_cast<uint8>(Mixtormat::EMixtormatIconRole::FoldoutDisclosure)];

	TSharedRef<SHorizontalBox> Header = SNew(SHorizontalBox);
	if (bCollapsible)
	{
		Header->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, MixtormatTokens::FoldoutHeaderGap, 0.0f)
		[
			// The disclosure is a box of glyph-plus-padding with the glyph centred in it, matching
			// the prototype's calc(icon-size + 2 * icon-padding). The padding goes INSIDE the box; it
			// is not a leading offset on the row, which would shift the chevron out of the gutter and
			// make the gap to the title unequal.
			SNew(SBox)
			.WidthOverride(DisclosureIcon.GlyphSize + MixtormatTokens::FoldoutIconPadding * 2.0f)
			.HeightOverride(DisclosureIcon.GlyphSize + MixtormatTokens::FoldoutIconPadding * 2.0f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(DisclosureIcon.GlyphSize)
				.HeightOverride(DisclosureIcon.GlyphSize)
				[
				SNew(SImage)
				// The chevron is an immediate state swap between the two authored PNGs, not a rotation. A rotation
									// would need a painted transform per frame; the animation phase can interpolate
									// between the two orientations there. Hit testing is unaffected either way: the
									// glyph is decoration inside the slot, and the click target is the bar around it.
									.Image_Lambda([this]()
				{
					return bExpanded ? MixtormatIcons::ChevronDown() : MixtormatIcons::ChevronRight();
				})
				.ColorAndOpacity(TAttribute<FSlateColor>(FSlateColor(
										// The chevron reads at the title's own colour and opacity, so it sits at the
										// same weight as the words beside it rather than as a separate mark.
										TintAt(FMixtormatThemeStore::GetResolved().Palette.Get(Mixtormat::EMixtormatColorRole::Text), DisclosureIcon.RestOpacity))))
					]
				]
		];
	}
	Header->AddSlot()
	.FillWidth(1.0f)
	.VAlign(VAlign_Center)
	[
		// A plain STextBlock. The foldout title's style is resolved once, here, from the style set's
		// entry -- and deliberately not mutated afterwards.
		//
		// STextBlock declares TextStyle with SLATE_STYLE_ARGUMENT, which takes a raw
		// const FTextBlockStyle* and offers no TAttribute overload, so the style cannot be bound.
		// Per-frame style mutation is unnecessary: LiveTheme rebuilds the workspace after refresh.
		//
		// Font size, weight and tracking therefore only change when the workspace is rebuilt, which is
		// exactly what a LiveTheme edit already does. Only the colour needs to follow the enabled
		// state at runtime, and ColorAndOpacity is a real SLATE_ATTRIBUTE, so it binds.
		SNew(STextBlock)
		.Font(FoldoutTitleStyle.Font)
		// Title only: the chevron, state, action and reset keep their slots.
		.Justification_Lambda([]() { return MixtormatRow::JustifyFor(MixtormatTokens::GroupHeaderAlign); })
		.ColorAndOpacity_Lambda([this, FoldoutTitleSpec]()
		{
			// Colours are rebuilt per read rather than held as pointers: a pointer into the style set
			// would outlive a theme refresh, and this costs one FLinearColor copy.
			const Mixtormat::FMixtormatResolvedStyle& Current = FMixtormatThemeStore::GetResolved();
			const FLinearColor Color = IsEnabled()
				? Current.Palette.Get(Mixtormat::EMixtormatColorRole::Text)
				: Current.Palette.Get(Mixtormat::EMixtormatColorRole::TextMuted);
			return Mixtormat::FMixtormatTypography::MakeTextStyle(FoldoutTitleSpec, Color).ColorAndOpacity;
		})
		.Text(InArgs._Title)
	];

	// State, when the group has any. Hidden rather than blank so it takes no width when empty.
	if (InArgs._StateText.IsSet())
	{
		Header->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(MixtormatTokens::FoldoutHeaderGap, 0.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Font(BadgeTextStyle.Font)
			.RenderOpacity(BadgeTextStyle.ColorAndOpacity.GetSpecifiedColor().A)
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
		.Padding(MixtormatTokens::FoldoutHeaderGap, 0.0f, 0.0f, 0.0f)
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
		.Padding(MixtormatTokens::FoldoutHeaderGap, 0.0f, 0.0f, 0.0f)
		[
			SNew(SMixtormatIconButton)
			.Icon(MixtormatIcons::Refresh())
			.Size(MixtormatTokens::IconButtonSize)
			.ToolTipText(LOCTEXT("ResetGroup", "Reset this group to its defaults"))
			.OnClicked(InArgs._OnReset)
		];
	}

	LiveInspectorGroups.Add(SharedThis(this));
	OnReset = InArgs._OnReset;

	ChildSlot
	[
		SAssignNew(ContextAnchor, SMenuAnchor)
		.Placement(MenuPlacement_MenuRight)
		.OnGetMenuContent(InArgs._OnGetContextMenu.IsBound()
			? InArgs._OnGetContextMenu
			: FOnGetContent::CreateSP(this, &SMixtormatInspectorGroup::BuildDefaultContextMenu))
		[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, Layout.OuterTop, 0.0f, 0.0f)
		[
			// The bar itself is the click target and the hover surface. A button on top of it would
			// light a button-shaped patch inside the header instead of the header.
			//
			// The header surface is painted behind this whole slot, not inside it: the button must
			// stay behaviour-only, or its own hover and pressed plates would draw over the lift.
			SNew(SMixtormatFoldoutHeader)
			.IsHovered(this, &SMixtormatInspectorGroup::IsHovered)
			.bEnabled(this, &SMixtormatInspectorGroup::IsEnabled)
			[
				// The whole bar is the click target. An invisible button rather than a mouse
				// handler on the group, because the group also contains the body and a press
				// down there must not collapse what the user is reaching into.
				bCollapsible
				? StaticCastSharedRef<SWidget>(
					SNew(SButton)
					.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(
						TEXT("Mixtormat.InspectorHeaderButton")))
					.ContentPadding(FMargin(0.0f))
					.OnClicked(this, &SMixtormatInspectorGroup::ToggleExpanded)
					[
						SNew(SBox)
						.Padding(FMargin(
							Layout.Gutter,
							Layout.HeaderPaddingTop,
							Layout.Gutter,
							Layout.HeaderPaddingBottom))
						.VAlign(VAlign_Center)
						[
							Header
						]
					])
				: StaticCastSharedRef<SWidget>(
					SNew(SBox)
					.Padding(FMargin(
						Layout.Gutter,
						Layout.HeaderPaddingTop,
						Layout.Gutter,
						Layout.HeaderPaddingBottom))
					.VAlign(VAlign_Center)
					[
						Header
					])
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, Layout.OuterBottom)
		[
			SNew(SMixtormatSurfaceBox)
			.Visibility_Lambda([this]() { return IsExpanded() ? EVisibility::Visible : EVisibility::Collapsed; })
			.Recipe(Mixtormat::MakeGroundRecipe())
			.InheritWidgetStyle(true)
			.Padding(FMargin(
				Layout.Gutter,
				Layout.BodyTop,
				Layout.Gutter,
				Layout.BodyBottom))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, Layout.BodyTop)
				[
					MixtormatRow::MakeInspectorHairline(TAttribute<bool>::CreateLambda([]()
					{
						return MixtormatTokens::InspectorHairlineUnderHeader >= 0.5f;
					}))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					InArgs._Content.Widget
				]
			]
		]
		]
	];
}

TSharedRef<SWidget> SMixtormatInspectorGroup::BuildDefaultContextMenu()
{
	MixtormatMenu::FBuilder Menu;

	// Reset first, because it is the one entry that acts on this group rather than all of them --
	// and it is hidden when the group has no defaults to return to, rather than shown greyed,
	// since a permanently disabled entry teaches nothing.
	if (OnReset.IsBound())
	{
		Menu.Item(
			LOCTEXT("ResetGroupContext", "Reset Group"),
			MixtormatIcons::Refresh(),
			FSimpleDelegate::CreateLambda([Reset = OnReset]() { Reset.ExecuteIfBound(); }));
		Menu.Separator();
	}

	Menu.Item(
		LOCTEXT("ExpandAllGroups", "Expand All"),
		MixtormatIcons::ChevronDown(),
		FSimpleDelegate::CreateLambda([]() { SetAllExpanded(true); }));
	Menu.Item(
		LOCTEXT("CollapseAllGroups", "Collapse All"),
		MixtormatIcons::ChevronRight(),
		FSimpleDelegate::CreateLambda([]() { SetAllExpanded(false); }));

	return Menu.Build();
}

FReply SMixtormatInspectorGroup::ToggleExpanded()
{
	bExpanded = !bExpanded;
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
