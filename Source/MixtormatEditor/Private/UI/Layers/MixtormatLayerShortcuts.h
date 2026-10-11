// Copyright 2026 Hugo Beyer. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Input/Events.h"

namespace MixtormatLayerShortcuts
{
	enum class EAction : uint8
	{
		Rename, Remove, Copy, Paste, Duplicate, CopyAsInstance, Group, Solo, ToggleEnabled
	};

	inline FText Label(EAction Action)
	{
		switch (Action)
		{
		case EAction::Rename: return FText::FromString(TEXT("F2"));
		case EAction::Remove: return FText::FromString(TEXT("Del"));
		case EAction::Copy: return FText::FromString(TEXT("Ctrl+C"));
		case EAction::Paste: return FText::FromString(TEXT("Ctrl+V"));
		case EAction::Duplicate: return FText::FromString(TEXT("Ctrl+D"));
		case EAction::CopyAsInstance: return FText::FromString(TEXT("Ctrl+Shift+C"));
		case EAction::Group: return FText::FromString(TEXT("Ctrl+G"));
		case EAction::Solo: return FText::FromString(TEXT("Alt+L"));
		case EAction::ToggleEnabled: return FText::FromString(TEXT("Shift+L"));
		}
		return FText::GetEmpty();
	}

	inline bool Matches(const FKeyEvent& Event, EAction Action)
	{
		if (Event.IsRepeat()) return false;
		const bool Ctrl = Event.IsControlDown() || Event.IsCommandDown();
		const bool Alt = Event.IsAltDown();
		const bool Shift = Event.IsShiftDown();
		const FKey Key = Event.GetKey();
		switch (Action)
		{
		case EAction::Rename: return !Ctrl && !Alt && !Shift && (Key == EKeys::F2 || Key == EKeys::F12);
		case EAction::Remove: return !Ctrl && !Alt && !Shift && Key == EKeys::Delete;
		case EAction::Copy: return Ctrl && !Alt && !Shift && Key == EKeys::C;
		case EAction::Paste: return Ctrl && !Alt && !Shift && Key == EKeys::V;
		case EAction::Duplicate: return Ctrl && !Alt && !Shift && Key == EKeys::D;
		case EAction::CopyAsInstance: return Ctrl && !Alt && Shift && Key == EKeys::C;
		case EAction::Group: return Ctrl && !Alt && !Shift && Key == EKeys::G;
		case EAction::Solo: return !Ctrl && Alt && !Shift && Key == EKeys::L;
		case EAction::ToggleEnabled: return !Ctrl && !Alt && Shift && Key == EKeys::L;
		}
		return false;
	}
}
