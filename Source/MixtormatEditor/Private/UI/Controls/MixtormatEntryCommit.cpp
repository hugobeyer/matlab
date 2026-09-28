// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Controls/MixtormatEntryCommit.h"

#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWidget.h"

namespace
{
	// Sees every mouse press in the app while a field is being typed into, including presses on
	// widgets that never take keyboard focus (viewports, empty panel space), which would otherwise
	// leave the field open. Left accepts, right cancels.
	class FMixtormatEntryClickWatcher final : public IInputProcessor
	{
	public:
		explicit FMixtormatEntryClickWatcher(const TWeakPtr<FMixtormatEntryCommit>& InSession)
			: Session(InSession)
		{
		}

		virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}

		virtual bool HandleMouseButtonDownEvent(FSlateApplication&, const FPointerEvent& MouseEvent) override
		{
			const TSharedPtr<FMixtormatEntryCommit> Pinned = Session.Pin();
			if (!Pinned.IsValid() || !Pinned->IsActive())
			{
				return false;
			}
			if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
			{
				Pinned->Cancel();
				// Swallowed, so the cancel does not also open a context menu.
				return true;
			}
			if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton
				&& !Pinned->IsOverEntry(MouseEvent.GetScreenSpacePosition()))
			{
				Pinned->Accept();
			}
			// A left click still reaches whatever was clicked.
			return false;
		}

		virtual const TCHAR* GetDebugName() const override { return TEXT("MixtormatEntryClickWatcher"); }

	private:
		TWeakPtr<FMixtormatEntryCommit> Session;
	};
}

FMixtormatEntryCommit::~FMixtormatEntryCommit()
{
	StopWatching();
}

void FMixtormatEntryCommit::Begin(const TSharedRef<SWidget>& EntryWidget, TFunction<void()> OnClosedWithoutCommit)
{
	StopWatching();
	Entry = EntryWidget;
	ClosedWithoutCommit = MoveTemp(OnClosedWithoutCommit);
	bCancel = false;
	if (FSlateApplication::IsInitialized())
	{
		Watcher = MakeShared<FMixtormatEntryClickWatcher>(AsShared());
		FSlateApplication::Get().RegisterInputPreProcessor(Watcher);
	}
}

bool FMixtormatEntryCommit::Finish()
{
	const bool bDiscard = bCancel;
	bCancel = false;
	StopWatching();
	return bDiscard;
}

FReply FMixtormatEntryCommit::HandleKeyDown(const FGeometry&, const FKeyEvent& KeyEvent)
{
	if (IsActive() && KeyEvent.GetKey() == EKeys::Escape)
	{
		Cancel();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void FMixtormatEntryCommit::Cancel()
{
	if (!IsActive())
	{
		return;
	}
	bCancel = true;
	// The field commits on losing focus; Finish() then reports the discard.
	FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::Cleared);
	if (IsActive())
	{
		// Focus was already elsewhere, so no commit is coming. Close it here.
		Finish();
		if (ClosedWithoutCommit)
		{
			ClosedWithoutCommit();
		}
	}
}

void FMixtormatEntryCommit::Accept()
{
	if (!IsActive())
	{
		return;
	}
	FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::Mouse);
	if (IsActive())
	{
		Finish();
		if (ClosedWithoutCommit)
		{
			ClosedWithoutCommit();
		}
	}
}

bool FMixtormatEntryCommit::IsOverEntry(const FVector2D& ScreenPosition) const
{
	const TSharedPtr<SWidget> Pinned = Entry.Pin();
	return Pinned.IsValid() && Pinned->GetTickSpaceGeometry().IsUnderLocation(ScreenPosition);
}

void FMixtormatEntryCommit::StopWatching()
{
	if (Watcher.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(Watcher);
	}
	Watcher.Reset();
}
