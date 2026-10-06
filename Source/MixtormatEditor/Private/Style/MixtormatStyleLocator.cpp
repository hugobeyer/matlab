// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Style/MixtormatStyleLocator.h"

#include "Style/MixtormatThemeSchema.h"

#include "Framework/Application/SlateApplication.h"
#include "Layout/Children.h"
#include "Widgets/SWidget.h"
#include "Widgets/SWindow.h"

namespace Mixtormat
{
	namespace
	{
		struct FLocatedWidget
		{
			TWeakPtr<SWidget> Widget;
			float RenderOpacity = 1.0f;
		};

		TOptional<FLocatedWidget> GLocated;

		bool Is(const FName Type, const TCHAR* Name)
		{
			return Type == FName(Name);
		}

		bool Matches(const EMixtormatStyleTarget Target, const FName Type)
		{
			switch (Target)
			{
			case EMixtormatStyleTarget::Global:
			case EMixtormatStyleTarget::Shell:
				return Is(Type, TEXT("SMixtormat"));

			case EMixtormatStyleTarget::ControlWell:
				return Is(Type, TEXT("SMixtormatSlider"))
					|| Is(Type, TEXT("SMixtormatToggle"))
					|| Is(Type, TEXT("SMixtormatWellBox"))
					|| Is(Type, TEXT("SMixtormatInspectorWell"));

			case EMixtormatStyleTarget::ControlFill:
				return Is(Type, TEXT("SMixtormatSlider"))
					|| Is(Type, TEXT("SMixtormatToggle"));

			case EMixtormatStyleTarget::ControlToggle:
				return Is(Type, TEXT("SMixtormatToggle"));

			case EMixtormatStyleTarget::ControlLayout:
				return Is(Type, TEXT("SMixtormatSlider"))
					|| Is(Type, TEXT("SMixtormatToggle"))
					|| Is(Type, TEXT("SMixtormatWellBox"))
					|| Is(Type, TEXT("SMixtormatInspectorWell"))
					|| Is(Type, TEXT("SMixtormatScalarRamp"))
					|| Is(Type, TEXT("SMixtormatColorRamp"))
					|| Is(Type, TEXT("SMixtormatSegmentedControl"))
					|| Is(Type, TEXT("SMixtormatTabStrip"));

			case EMixtormatStyleTarget::Foldout:
				return Is(Type, TEXT("SMixtormatFoldoutHeader"));

			case EMixtormatStyleTarget::Card:
				return Is(Type, TEXT("SMixtormatInspectorCard"));

			case EMixtormatStyleTarget::Layer:
				return Is(Type, TEXT("SMixtormatLayerSurface"))
					|| Is(Type, TEXT("SMixtormatLayerRow"))
					|| Is(Type, TEXT("SMixtormatLayerGroupRow"))
					|| Is(Type, TEXT("SMixtormatLayerChildRow"));

			case EMixtormatStyleTarget::Button:
				return Is(Type, TEXT("SMixtormatGroupButtonSurface"))
					|| Is(Type, TEXT("SMixtormatGroupAction"));

			case EMixtormatStyleTarget::Menu:
				return Is(Type, TEXT("SMixtormatMenuPanel"))
					|| Is(Type, TEXT("SMixtormatMenuItem"));

			case EMixtormatStyleTarget::Preview:
				return Is(Type, TEXT("SMixtormatPreviewPlate"))
					|| Is(Type, TEXT("SMixtormatPreviewViewport"));

			case EMixtormatStyleTarget::Gallery:
				return Is(Type, TEXT("SMixtormatTile"));

			case EMixtormatStyleTarget::None:
			default:
				return false;
			}
		}

		bool CollectFirst(const TSharedRef<SWidget>& Widget, const EMixtormatStyleTarget Target)
		{
			const FName Type = Widget->GetType();

			if (Is(Type, TEXT("SMixtormatThemePanel")))
			{
				return false;
			}

			const FVector2f Size = Widget->GetCachedGeometry().GetLocalSize();
			if (Matches(Target, Type)
				&& Widget->GetVisibility().IsVisible()
				&& Size.X > 1.0f && Size.Y > 1.0f)
			{
				FLocatedWidget Entry;
				Entry.Widget = Widget;
				Entry.RenderOpacity = Widget->GetRenderOpacity();
				GLocated = Entry;
				return true;
			}

			FChildren* Children = Widget->GetChildren();
			if (!Children)
			{
				return false;
			}
			for (int32 Index = 0; Index < Children->Num(); ++Index)
			{
				if (CollectFirst(Children->GetChildAt(Index), Target))
				{
					return true;
				}
			}
			return false;
		}
	}

	EMixtormatStyleTarget FMixtormatStyleLocator::TargetFor(const FMixtormatThemeProperty& Property)
	{
		return Property.LocateTarget;
	}

	FText FMixtormatStyleLocator::Label(const EMixtormatStyleTarget Target)
	{
		switch (Target)
		{
		case EMixtormatStyleTarget::Global: return NSLOCTEXT("MixtormatStyleLocator", "Global", "Mixtormat workspace");
		case EMixtormatStyleTarget::ControlWell: return NSLOCTEXT("MixtormatStyleLocator", "ControlWell", "Control wells");
		case EMixtormatStyleTarget::ControlFill: return NSLOCTEXT("MixtormatStyleLocator", "ControlFill", "Slider / toggle fills");
		case EMixtormatStyleTarget::ControlToggle: return NSLOCTEXT("MixtormatStyleLocator", "ControlToggle", "Toggles");
		case EMixtormatStyleTarget::ControlLayout: return NSLOCTEXT("MixtormatStyleLocator", "ControlLayout", "Inspector controls");
		case EMixtormatStyleTarget::Foldout: return NSLOCTEXT("MixtormatStyleLocator", "Foldout", "Foldout");
		case EMixtormatStyleTarget::Card: return NSLOCTEXT("MixtormatStyleLocator", "Card", "Inspector card");
		case EMixtormatStyleTarget::Layer: return NSLOCTEXT("MixtormatStyleLocator", "Layer", "Layer rows");
		case EMixtormatStyleTarget::Button: return NSLOCTEXT("MixtormatStyleLocator", "Button", "Shared action button");
		case EMixtormatStyleTarget::Menu: return NSLOCTEXT("MixtormatStyleLocator", "Menu", "Menu / popup");
		case EMixtormatStyleTarget::Preview: return NSLOCTEXT("MixtormatStyleLocator", "Preview", "Preview controls");
		case EMixtormatStyleTarget::Gallery: return NSLOCTEXT("MixtormatStyleLocator", "Gallery", "Gallery tiles");
		case EMixtormatStyleTarget::Shell: return NSLOCTEXT("MixtormatStyleLocator", "Shell", "Editor shell");
		case EMixtormatStyleTarget::None:
		default: return NSLOCTEXT("MixtormatStyleLocator", "None", "No live target");
		}
	}

	bool FMixtormatStyleLocator::Begin(const EMixtormatStyleTarget Target)
	{
		End();
		if (Target == EMixtormatStyleTarget::None || !FSlateApplication::IsInitialized())
		{
			return false;
		}

		for (const TSharedRef<SWindow>& Window : FSlateApplication::Get().GetTopLevelWindows())
		{
			if (CollectFirst(Window, Target))
			{
				break;
			}
		}

		if (!GLocated.IsSet())
		{
			return false;
		}

		SetDimmed(true);
		return true;
	}

	void FMixtormatStyleLocator::SetDimmed(const bool bDimmed)
	{
		if (!GLocated.IsSet())
		{
			return;
		}

		const FLocatedWidget& Entry = GLocated.GetValue();
		if (const TSharedPtr<SWidget> Widget = Entry.Widget.Pin())
		{
			// SWidget has a generic render-opacity API in UE 5.8, but no generic tint API.
			// Pulse only the exact located widget instead of dimming every widget in its category.
			Widget->SetRenderOpacity(bDimmed ? Entry.RenderOpacity * 0.18f : Entry.RenderOpacity);
			Widget->Invalidate(EInvalidateWidgetReason::Paint);
		}
	}

	void FMixtormatStyleLocator::End()
	{
		if (GLocated.IsSet())
		{
			const FLocatedWidget& Entry = GLocated.GetValue();
			if (const TSharedPtr<SWidget> Widget = Entry.Widget.Pin())
			{
				Widget->SetRenderOpacity(Entry.RenderOpacity);
				Widget->Invalidate(EInvalidateWidgetReason::Paint);
			}
		}
		GLocated.Reset();
	}
}
