// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Style/MixtormatStyleLocator.h"

#include "Style/MixtormatThemeSchema.h"

#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
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
			TWeakPtr<SWindow> Window;
			double BeginTime = 0.0;
			// Distance from the locating panel's centre; the smallest wins, so the eye outlines
			// the control the artist is actually tuning rather than the first type match in the
			// tree. Zero when no anchor was supplied, which keeps the historical first match.
			float Score = 0.0f;
		};

		TOptional<FLocatedWidget> GLocated;

		// Two 0.6s triangle pulses, then the outline expires on its own.
		constexpr double LocateDuration = 1.2;
		constexpr double PulsePeriod = 0.6;

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

			case EMixtormatStyleTarget::TopBar:
				return Is(Type, TEXT("SMixtormatTopBar"));

			case EMixtormatStyleTarget::NavigationRail:
				return Is(Type, TEXT("SMixtormatIconRail"));

			case EMixtormatStyleTarget::Splitter:
				return Is(Type, TEXT("SSplitter"));

			case EMixtormatStyleTarget::ScrollArea:
				return Is(Type, TEXT("SScrollBox"));

			case EMixtormatStyleTarget::None:
			default:
				return false;
			}
		}

		// Depth-first over every top-level window, keeping the visible match nearest the anchor
		// rather than the first one found: type matches are not unique, and the first widget in
		// the tree is often a control on an unrelated panel.
		void CollectBest(const TSharedRef<SWidget>& Widget, const EMixtormatStyleTarget Target,
			const TOptional<FVector2f>& AnchorCenter)
		{
			const FName Type = Widget->GetType();

			if (Is(Type, TEXT("SMixtormatThemePanel")))
			{
				return;
			}

			const FVector2f Size = Widget->GetCachedGeometry().GetLocalSize();
			if (Matches(Target, Type)
				&& Widget->GetVisibility().IsVisible()
				&& Size.X > 1.0f && Size.Y > 1.0f)
			{
				const FVector2D Center = Widget->GetCachedGeometry().GetAbsolutePosition()
					+ FVector2D(Size) * 0.5;
				const float Score = AnchorCenter.IsSet()
					? static_cast<float>(FVector2D::Distance(
						FVector2D(AnchorCenter.GetValue()), Center))
					: 0.0f;
				if (!GLocated.IsSet() || Score < GLocated.GetValue().Score)
				{
					FLocatedWidget Entry;
					Entry.Widget = Widget;
					Entry.Score = Score;
					GLocated = Entry;
				}
			}

			FChildren* Children = Widget->GetChildren();
			if (!Children)
			{
				return;
			}
			for (int32 Index = 0; Index < Children->Num(); ++Index)
			{
				CollectBest(Children->GetChildAt(Index), Target, AnchorCenter);
			}
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
		case EMixtormatStyleTarget::TopBar: return NSLOCTEXT("MixtormatStyleLocator", "TopBar", "Top bar");
		case EMixtormatStyleTarget::NavigationRail: return NSLOCTEXT("MixtormatStyleLocator", "NavigationRail", "Left navigation rail");
		case EMixtormatStyleTarget::Splitter: return NSLOCTEXT("MixtormatStyleLocator", "Splitter", "Column splitter");
		case EMixtormatStyleTarget::ScrollArea: return NSLOCTEXT("MixtormatStyleLocator", "ScrollArea", "Scroll areas");
		case EMixtormatStyleTarget::None:
		default: return NSLOCTEXT("MixtormatStyleLocator", "None", "No live target");
		}
	}

	bool FMixtormatStyleLocator::Begin(const EMixtormatStyleTarget Target,
		const TOptional<FVector2f>& AnchorCenter)
	{
		End();
		if (Target == EMixtormatStyleTarget::None || !FSlateApplication::IsInitialized())
		{
			return false;
		}

		for (const TSharedRef<SWindow>& Window : FSlateApplication::Get().GetTopLevelWindows())
		{
			CollectBest(Window, Target, AnchorCenter);
		}

		if (!GLocated.IsSet())
		{
			return false;
		}

		FLocatedWidget& Entry = GLocated.GetValue();
		Entry.BeginTime = FPlatformTime::Seconds();
		if (const TSharedPtr<SWidget> Widget = Entry.Widget.Pin())
		{
			Entry.Window = FSlateApplication::Get().FindWidgetWindow(Widget.ToSharedRef());
		}
		return true;
	}

	bool FMixtormatStyleLocator::Tick()
	{
		if (!GLocated.IsSet())
		{
			return false;
		}
		if (FPlatformTime::Seconds() - GLocated.GetValue().BeginTime >= LocateDuration)
		{
			End();
			return false;
		}
		return true;
	}

	float FMixtormatStyleLocator::GetPulseAlpha()
	{
		if (!GLocated.IsSet())
		{
			return 0.0f;
		}
		const double Elapsed = FPlatformTime::Seconds() - GLocated.GetValue().BeginTime;
		const double Phase = FMath::Fmod(Elapsed, PulsePeriod) / PulsePeriod;
		return static_cast<float>(1.0 - FMath::Abs(Phase * 2.0 - 1.0));
	}

	bool FMixtormatStyleLocator::GetTargetRect(FSlateRect& OutRect)
	{
		if (!GLocated.IsSet())
		{
			return false;
		}
		const TSharedPtr<SWidget> Widget = GLocated.GetValue().Widget.Pin();
		if (!Widget.IsValid())
		{
			return false;
		}
		const FGeometry& Geometry = Widget->GetCachedGeometry();
		const FVector2D TopLeft = Geometry.GetAbsolutePosition();
		const FVector2D Size(Geometry.GetLocalSize());
		OutRect = FSlateRect(TopLeft.X, TopLeft.Y, TopLeft.X + Size.X, TopLeft.Y + Size.Y);
		return true;
	}

	TWeakPtr<SWindow> FMixtormatStyleLocator::GetTargetWindow()
	{
		return GLocated.IsSet() ? GLocated.GetValue().Window : TWeakPtr<SWindow>();
	}

	void FMixtormatStyleLocator::End()
	{
		GLocated.Reset();
	}
}
