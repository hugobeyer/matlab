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

		TArray<FLocatedWidget> GLocated;

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

			case EMixtormatStyleTarget::Controls:
				return Is(Type, TEXT("SMixtormatSlider"))
					|| Is(Type, TEXT("SMixtormatToggle"))
					|| Is(Type, TEXT("SMixtormatWellBox"))
					|| Is(Type, TEXT("SMixtormatInspectorWell"));

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

		void Collect(const TSharedRef<SWidget>& Widget, const EMixtormatStyleTarget Target)
		{
			const FName Type = Widget->GetType();

			if (Is(Type, TEXT("SMixtormatLiveThemePanel")))
			{
				return;
			}

			if (Matches(Target, Type))
			{
				FLocatedWidget& Entry = GLocated.AddDefaulted_GetRef();
				Entry.Widget = Widget;
				Entry.RenderOpacity = Widget->GetRenderOpacity();
				return;
			}

			FChildren* Children = Widget->GetChildren();
			if (!Children)
			{
				return;
			}
			for (int32 Index = 0; Index < Children->Num(); ++Index)
			{
				Collect(Children->GetChildAt(Index), Target);
			}
		}

		bool Starts(const FString& Value, const TCHAR* Prefix)
		{
			return Value.StartsWith(Prefix, ESearchCase::CaseSensitive);
		}
	}

	EMixtormatStyleTarget FMixtormatStyleLocator::TargetFor(const FMixtormatThemeProperty& Property)
	{
		const FString Id = Property.Id.ToString();

		switch (Property.Tab)
		{
		case EMixtormatThemeTab::Controls: return EMixtormatStyleTarget::Controls;
		case EMixtormatThemeTab::Foldouts: return EMixtormatStyleTarget::Foldout;
		case EMixtormatThemeTab::Cards: return EMixtormatStyleTarget::Card;
		case EMixtormatThemeTab::Layers: return EMixtormatStyleTarget::Layer;
		case EMixtormatThemeTab::Buttons: return EMixtormatStyleTarget::Button;
		case EMixtormatThemeTab::Menus: return EMixtormatStyleTarget::Menu;
		case EMixtormatThemeTab::Preview: return EMixtormatStyleTarget::Preview;

		case EMixtormatThemeTab::GalleryShell:
			return Starts(Id, TEXT("Shell"))
				? EMixtormatStyleTarget::Shell
				: EMixtormatStyleTarget::Gallery;

		case EMixtormatThemeTab::Typography:
			// Role-to-widget registration is not implemented yet. A generic component target is
			// misleading for semantic typography, so every Typography locate button stays disabled.
			return EMixtormatStyleTarget::None;

		case EMixtormatThemeTab::Global:
			if (Id == TEXT("Palette.MenuGround")) return EMixtormatStyleTarget::Menu;
			if (Id == TEXT("Palette.ThumbnailGround")) return EMixtormatStyleTarget::Gallery;

			if (Starts(Id, TEXT("Icons.TopBar."))) return EMixtormatStyleTarget::Button;
			if (Starts(Id, TEXT("Icons.PanelToolbar."))) return EMixtormatStyleTarget::Button;
			if (Starts(Id, TEXT("Icons.PreviewToolbar."))) return EMixtormatStyleTarget::Preview;
			if (Starts(Id, TEXT("Icons.LayerEye."))
				|| Starts(Id, TEXT("Icons.LayerDisclosure."))) return EMixtormatStyleTarget::Layer;
			if (Starts(Id, TEXT("Icons.FoldoutDisclosure."))) return EMixtormatStyleTarget::Foldout;
			if (Starts(Id, TEXT("Icons.CardLeading."))) return EMixtormatStyleTarget::Card;
			if (Starts(Id, TEXT("Icons.Menu."))) return EMixtormatStyleTarget::Menu;
			if (Starts(Id, TEXT("Icons.GalleryToolbar."))) return EMixtormatStyleTarget::Gallery;
			return EMixtormatStyleTarget::Global;

		default:
			return EMixtormatStyleTarget::None;
		}
	}

	FText FMixtormatStyleLocator::Label(const EMixtormatStyleTarget Target)
	{
		switch (Target)
		{
		case EMixtormatStyleTarget::Global: return NSLOCTEXT("MixtormatStyleLocator", "Global", "Mixtormat workspace");
		case EMixtormatStyleTarget::Controls: return NSLOCTEXT("MixtormatStyleLocator", "Controls", "Controls");
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
			Collect(Window, Target);
		}

		if (GLocated.IsEmpty())
		{
			return false;
		}

		SetDimmed(true);
		return true;
	}

	void FMixtormatStyleLocator::SetDimmed(const bool bDimmed)
	{
		for (const FLocatedWidget& Entry : GLocated)
		{
			if (const TSharedPtr<SWidget> Widget = Entry.Widget.Pin())
			{
				Widget->SetRenderOpacity(bDimmed ? Entry.RenderOpacity * 0.22f : Entry.RenderOpacity);
				Widget->Invalidate(EInvalidateWidgetReason::Paint);
			}
		}
	}

	void FMixtormatStyleLocator::End()
	{
		SetDimmed(false);
		GLocated.Reset();
	}
}
