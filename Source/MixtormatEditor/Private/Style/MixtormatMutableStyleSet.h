#pragma once

#include "Styling/SlateStyle.h"
#include "Styling/SlateWidgetStyle.h"
#include <type_traits>

// Slate widgets retain raw pointers into a style set. Refresh the owned values, never replace
// their allocations, so open menus, tooltips and the retained viewport cannot dangle.
class FMixtormatMutableStyleSet final : public FSlateStyleSet
{
public:
	explicit FMixtormatMutableStyleSet(const FName Name) : FSlateStyleSet(Name) {}

	void Set(const FName Name, FSlateBrush* Brush)
	{
		if (BrushKeys.Contains(Name))
		{
			*const_cast<FSlateBrush*>(GetBrush(Name)) = *Brush;
			delete Brush;
		}
		else
		{
			FSlateStyleSet::Set(Name, Brush);
			BrushKeys.Add(Name);
		}
	}

	template <typename T, std::enable_if_t<std::is_base_of_v<FSlateWidgetStyle, T>, int> = 0>
	void Set(const FName Name, const T& Value)
	{
		if (WidgetKeys.Contains(Name))
		{
			check(WidgetKeys[Name] == T::TypeName);
			const_cast<T&>(GetWidgetStyle<T>(Name)) = Value;
		}
		else
		{
			FSlateStyleSet::Set(Name, Value);
			WidgetKeys.Add(Name, T::TypeName);
		}
	}

private:
	TSet<FName> BrushKeys;
	TMap<FName, FName> WidgetKeys;
};
