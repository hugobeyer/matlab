// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"

#include "Style/MixtormatStyle.h"

#include "UI/Containers/SMixtormatInspectorGroup.h"
#include "Widgets/SMixtormatLiveThemePanel.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/Children.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SWindow.h"

#define LOCTEXT_NAMESPACE "SMixtormatTheme"

namespace
{
	struct FThemeLayoutState
	{
		TArray<float> ScrollOffsets;
		TArray<bool> ExpandedGroups;
	};

	// A style-only rebuild has the same widget topology and selection. Traverse in tree order,
	// not the global group registry (which can also contain popup widgets).
	void TransferLayoutState(
		const TSharedRef<SWidget>& Widget,
		FThemeLayoutState& State,
		const bool bRestore,
		int32& ScrollIndex,
		int32& GroupIndex)
	{
		if (Widget->GetType() == FName(TEXT("SMixtormatPreviewViewport")))
		{
			return;
		}
		if (Widget->GetType() == FName(TEXT("SScrollBox")))
		{
			const TSharedRef<SScrollBox> Scroll = StaticCastSharedRef<SScrollBox>(Widget);
			if (bRestore)
			{
				if (State.ScrollOffsets.IsValidIndex(ScrollIndex))
				{
					Scroll->SetScrollOffset(State.ScrollOffsets[ScrollIndex]);
				}
			}
			else
			{
				State.ScrollOffsets.Add(Scroll->GetScrollOffset());
			}
			++ScrollIndex;
		}
		if (Widget->GetType() == FName(TEXT("SMixtormatInspectorGroup")))
		{
			const TSharedRef<SMixtormatInspectorGroup> Group = StaticCastSharedRef<SMixtormatInspectorGroup>(Widget);
			if (bRestore)
			{
				if (State.ExpandedGroups.IsValidIndex(GroupIndex))
				{
					Group->SetExpanded(State.ExpandedGroups[GroupIndex]);
				}
			}
			else
			{
				State.ExpandedGroups.Add(Group->IsExpanded());
			}
			++GroupIndex;
		}
		FChildren* Children = Widget->GetChildren();
		for (int32 Index = 0; Index < Children->Num(); ++Index)
		{
			TransferLayoutState(Children->GetChildAt(Index), State, bRestore, ScrollIndex, GroupIndex);
		}
	}
}

SMixtormat::~SMixtormat()
{
	if (FSlateApplication::IsInitialized())
	{
		if (const TSharedPtr<SWindow> Window = LiveThemeWindow.Pin())
		{
			Window->RequestDestroyWindow();
		}
	}
}

FReply SMixtormat::OpenLiveThemePanel()
{

	if (const TSharedPtr<SWindow> Existing = LiveThemeWindow.Pin())
	{
		Existing->BringToFront();
		return FReply::Handled();
	}
	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("ThemeWindowTitle", "Mixtormat — UI Style"))
		.ClientSize(FVector2D(620.0f, 720.0f))
		.SupportsMaximize(true)
		.SupportsMinimize(true)
		[
			SNew(SMixtormatLiveThemePanel)
			.CanEdit_Lambda([Owner = TWeakPtr<SMixtormat>(SharedThis(this))]()
			{
				const TSharedPtr<SMixtormat> Editor = Owner.Pin();
				return Editor.IsValid() && !Editor->bIsBaking;
			})
			.OnThemeChanged(FSimpleDelegate::CreateSP(this, &SMixtormat::RequestThemeRefresh))
		];
	LiveThemeWindow = Window;
	FSlateApplication::Get().AddWindow(Window);
	return FReply::Handled();
}

void SMixtormat::RequestThemeRefresh()
{
	if (!bThemeRefreshPending)
	{
		bThemeRefreshPending = true;
		RegisterActiveTimer(0.1f, FWidgetActiveTimerDelegate::CreateSP(this, &SMixtormat::ApplyPendingTheme));
	}
}

EActiveTimerReturnType SMixtormat::ApplyPendingTheme(double CurrentTime, float DeltaTime)
{
	(void)CurrentTime;
	(void)DeltaTime;
	if (bIsBaking)
	{
		return EActiveTimerReturnType::Continue;
	}
	bThemeRefreshPending = false;
	const int32 Page = MainSwitcher.IsValid() ? MainSwitcher->GetActiveWidgetIndex() : 0;
	FThemeLayoutState LayoutState;
	int32 ScrollIndex = 0;
	int32 GroupIndex = 0;
	TransferLayoutState(ChildSlot.GetWidget(), LayoutState, false, ScrollIndex, GroupIndex);

	// Retain SMixtormat, its recipe/history and its existing viewport. Only layout widgets go
	// away. In-place style refresh also keeps raw brush/style references in open popups valid.
	ChildSlot[SNullWidget::NullWidget];
	MainSwitcher.Reset();
	LeftSwitcher.Reset();
	NumericResetBindings.Reset();
	FMixtormatStyle::Refresh();
	BuildWorkspaceUI();
	MainSwitcher->SetActiveWidgetIndex(Page);
	SyncSelectedLayerControls();
	ScrollIndex = 0;
	GroupIndex = 0;
	TransferLayoutState(ChildSlot.GetWidget(), LayoutState, true, ScrollIndex, GroupIndex);
	Invalidate(EInvalidateWidgetReason::Layout);
	return EActiveTimerReturnType::Stop;
}

#undef LOCTEXT_NAMESPACE
