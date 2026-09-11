#include "Style/MixtormatLiveTheme.h"

#include "Services/MixtormatPaths.h"
#include "Style/MixtormatDesignTokens.h"
#include "Style/MixtormatPalette.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"

#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	TMap<FName, FLinearColor>& ColorOverrides()
	{
		static TMap<FName, FLinearColor> Values;
		return Values;
	}

	bool IsValidColor(const FLinearColor& Value)
	{
		return FMath::IsFinite(Value.R) && FMath::IsFinite(Value.G)
			&& FMath::IsFinite(Value.B) && FMath::IsFinite(Value.A)
			&& Value.R >= 0.0f && Value.R <= 1.0f
			&& Value.G >= 0.0f && Value.G <= 1.0f
			&& Value.B >= 0.0f && Value.B <= 1.0f
			&& Value.A >= 0.0f && Value.A <= 1.0f;
	}
}


void FMixtormatLiveTheme::Initialize()
{
	// Capture authored defaults before any panel edit or file load.
	Numbers();
	Colors();
	MixtormatTokens::RecomputeDerived();
}

const TArray<FMixtormatThemeNumber>& FMixtormatLiveTheme::Numbers()
{
#define THEME_NUMBER(Category, Name, Min, Max) \
	{TEXT(#Name), TEXT(Category), &MixtormatTokens::Name, MixtormatTokens::Name, Min, Max}
	static const TArray<FMixtormatThemeNumber> Entries = {
		THEME_NUMBER("Rows", RowHeight, 12.0f, 48.0f),
		THEME_NUMBER("Rows", RowGap, 0.0f, 24.0f),
		THEME_NUMBER("Rows", SliderRowGap, 0.0f, 24.0f),
		THEME_NUMBER("Rows", RowTextInset, 1.0f, 24.0f),
		THEME_NUMBER("Rows", RowLabelGap, 0.0f, 32.0f),
		THEME_NUMBER("Rows", RowFieldMinWidth, 40.0f, 400.0f),
		THEME_NUMBER("Surfaces", CornerRadius, 0.0f, 12.0f),
		THEME_NUMBER("Surfaces", OutlineWidth, 0.0f, 4.0f),
		THEME_NUMBER("Surfaces", PanelGutter, 0.0f, 32.0f),
		THEME_NUMBER("Surfaces", GroupHeaderHeight, 16.0f, 64.0f),
		THEME_NUMBER("Surfaces", GroupOuterGap, 0.0f, 24.0f),
		THEME_NUMBER("Surfaces", GroupHeaderItemGap, 0.0f, 24.0f),
		THEME_NUMBER("Surfaces", CardPadding, 0.0f, 32.0f),
		THEME_NUMBER("Surfaces", CardGap, 0.0f, 32.0f),
		THEME_NUMBER("Surfaces", CardTitleGap, 0.0f, 24.0f),
		THEME_NUMBER("Surfaces", HeaderContentGap, 0.0f, 24.0f),
		THEME_NUMBER("Buttons", ButtonHeight, 16.0f, 48.0f),
		THEME_NUMBER("Buttons", ButtonPaddingCompact, 0.0f, 32.0f),
		THEME_NUMBER("Buttons", ButtonPaddingPrimary, 0.0f, 32.0f),
		THEME_NUMBER("Buttons", ButtonPaddingTab, 0.0f, 32.0f),
		THEME_NUMBER("Buttons", TabWidth, 48.0f, 200.0f),
		THEME_NUMBER("Buttons", ToolbarIconSize, 8.0f, 32.0f),
		THEME_NUMBER("Buttons", ToolbarButtonMargin, 0.0f, 24.0f),
		THEME_NUMBER("Buttons", ToolbarLabelPadding, 0.0f, 24.0f),
		THEME_NUMBER("Preview overlays", ViewportOverlayInset, 0.0f, 48.0f),
		THEME_NUMBER("Preview overlays", ViewportOverlayClusterInset, 0.0f, 24.0f),
		THEME_NUMBER("Preview overlays", ViewportOverlayItemGap, 0.0f, 24.0f),
		THEME_NUMBER("Preview overlays", ViewportOverlayButtonGap, 0.0f, 24.0f),
		THEME_NUMBER("Inspector", InspectorTopMargin, 0.0f, 48.0f),
		THEME_NUMBER("Inspector", InspectorMaskGalleryMaxHeight, 120.0f, 1000.0f),
		THEME_NUMBER("Inspector", InspectorFeatureButtonGap, 0.0f, 24.0f),
		THEME_NUMBER("Inspector", InspectorColorSwatchWidth, 32.0f, 240.0f),
		THEME_NUMBER("Inspector", InspectorColorSwatchHeight, 8.0f, 48.0f),
		THEME_NUMBER("Shell", PanelPadding, 0.0f, 32.0f),
		THEME_NUMBER("Shell", LayerStackWidth, 160.0f, 640.0f),
		THEME_NUMBER("Shell", InspectorWidth, 200.0f, 720.0f),
		THEME_NUMBER("Shell", TopBarHeight, 24.0f, 64.0f),
		THEME_NUMBER("Shell", StatusBarHeight, 14.0f, 48.0f),
		THEME_NUMBER("Shell", BottomLibraryCollapseButtonWidth, 4.0f, 32.0f),
		THEME_NUMBER("Shell", BottomLibraryCollapseButtonHeight, 1.0f, 12.0f),
		THEME_NUMBER("Layers", LayerRowHeight, 20.0f, 64.0f),
		THEME_NUMBER("Layers", LayerChildRowHeight, 16.0f, 48.0f),
		THEME_NUMBER("Layers", LayerThumbnailSize, 12.0f, 48.0f),
		THEME_NUMBER("Layers", LayerChildIndent, 0.0f, 80.0f),
		THEME_NUMBER("Layers", LayerItemGap, 0.0f, 24.0f),
		THEME_NUMBER("Layers", LayerRowInsetLeading, 0.0f, 32.0f),
		THEME_NUMBER("Layers", LayerRowInsetTrailing, 0.0f, 32.0f),
		THEME_NUMBER("Layers", LayerRowGap, 0.0f, 24.0f),
		THEME_NUMBER("Layers", LayerStackHeaderHeight, 14.0f, 32.0f),
		THEME_NUMBER("Layers", MaskBarTileSize, 32.0f, 160.0f),
		THEME_NUMBER("Menus and dialogs", MenuWidth, 140.0f, 400.0f),
		THEME_NUMBER("Galleries", MaskPickerWidth, 200.0f, 1200.0f),
		THEME_NUMBER("Galleries", MaskPickerMaxHeight, 200.0f, 1200.0f),
		THEME_NUMBER("Galleries", MaskGalleryTileGap, 0.0f, 24.0f),
		THEME_NUMBER("Menus and dialogs", MenuPanelPadding, 0.0f, 24.0f),
		THEME_NUMBER("Menus and dialogs", DialogPadding, 0.0f, 48.0f),
		THEME_NUMBER("Menus and dialogs", DialogButtonGap, 0.0f, 32.0f),
		THEME_NUMBER("Menus and dialogs", DialogActionsTopMargin, 0.0f, 48.0f),
		THEME_NUMBER("Typography", FontBody, 8.0f, 24.0f),
		THEME_NUMBER("Typography", FontCaption, 6.0f, 20.0f),
		THEME_NUMBER("Typography", FontTile, 6.0f, 20.0f),
		THEME_NUMBER("Typography", FontGroupHeader, 6.0f, 20.0f),
		THEME_NUMBER("Typography", FontLayerSource, 6.0f, 20.0f),
		THEME_NUMBER("Typography", FontBadge, 6.0f, 20.0f),
		THEME_NUMBER("Typography", FontDialogLabel, 6.0f, 24.0f),
		THEME_NUMBER("Typography", FontMaskBarHeading, 6.0f, 24.0f),
		THEME_NUMBER("Typography", FontDragGhostLabel, 6.0f, 24.0f)
	};
#undef THEME_NUMBER
	return Entries;
}

const TArray<FMixtormatThemeColor>& FMixtormatLiveTheme::Colors()
{
#define THEME_COLOR(Name) {TEXT(#Name), MixtormatPalette::Name()}
	static const TArray<FMixtormatThemeColor> Entries = {
		THEME_COLOR(Window), THEME_COLOR(TopBar), THEME_COLOR(Shell),
		THEME_COLOR(Panel), THEME_COLOR(RaisedPanel), THEME_COLOR(RaisedPanelHover),
		THEME_COLOR(GroupSurround), THEME_COLOR(HeaderTint), THEME_COLOR(HeaderText),
		THEME_COLOR(RowText), THEME_COLOR(CaptionText), THEME_COLOR(LayerName),
		THEME_COLOR(LayerSource), THEME_COLOR(LayerEdge), THEME_COLOR(Accent),
		THEME_COLOR(AccentBright), THEME_COLOR(SelectionFill), THEME_COLOR(FocusFill),
		THEME_COLOR(Border), THEME_COLOR(BorderStrong), THEME_COLOR(WellTop),
		THEME_COLOR(WellBottom), THEME_COLOR(FillTop), THEME_COLOR(FillBottom),
		THEME_COLOR(FillTopHover), THEME_COLOR(FillBottomHover), THEME_COLOR(Modified),
		THEME_COLOR(MenuGround), THEME_COLOR(MenuTint)
	};
#undef THEME_COLOR
	return Entries;
}

FLinearColor FMixtormatLiveTheme::ResolveColor(const FName Name, const FLinearColor& Default)
{
	const FLinearColor* Override = ColorOverrides().Find(Name);
	return Override ? *Override : Default;
}

bool FMixtormatLiveTheme::SetNumber(const FName Name, const float Value)
{
	const FMixtormatThemeNumber* Entry = Numbers().FindByPredicate(
		[Name](const FMixtormatThemeNumber& Item) { return Item.Name == Name; });
	if (!Entry || !FMath::IsFinite(Value) || Value < Entry->Minimum || Value > Entry->Maximum)
	{
		return false;
	}
	*Entry->Value = Value;
	MixtormatTokens::RecomputeDerived();
	return true;
}

bool FMixtormatLiveTheme::SetColor(const FName Name, const FLinearColor& Value)
{
	if (!IsValidColor(Value) || !Colors().ContainsByPredicate(
		[Name](const FMixtormatThemeColor& Item) { return Item.Name == Name; }))
	{
		return false;
	}
	ColorOverrides().Add(Name, Value);
	return true;
}

void FMixtormatLiveTheme::Reset()
{
	for (const FMixtormatThemeNumber& Entry : Numbers())
	{
		*Entry.Value = Entry.Default;
	}
	ColorOverrides().Reset();
	MixtormatTokens::RecomputeDerived();
}

FString FMixtormatLiveTheme::Serialize()
{
	Initialize();
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	const TSharedRef<FJsonObject> Numeric = MakeShared<FJsonObject>();
	const TSharedRef<FJsonObject> Palette = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("version"), 1);
	for (const FMixtormatThemeNumber& Entry : Numbers())
	{
		Numeric->SetNumberField(Entry.Name.ToString(), *Entry.Value);
	}
	for (const FMixtormatThemeColor& Entry : Colors())
	{
		const FLinearColor Value = ResolveColor(Entry.Name, Entry.Default);
		Palette->SetArrayField(Entry.Name.ToString(), {
			MakeShared<FJsonValueNumber>(Value.R), MakeShared<FJsonValueNumber>(Value.G),
			MakeShared<FJsonValueNumber>(Value.B), MakeShared<FJsonValueNumber>(Value.A)});
	}
	Root->SetObjectField(TEXT("numbers"), Numeric);
	Root->SetObjectField(TEXT("colors"), Palette);
	FString Text;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text));
	return Text;
}

bool FMixtormatLiveTheme::Deserialize(const FString& Text, FString& Error)
{
	Initialize();
	Error.Reset();
	TSharedPtr<FJsonObject> Root;
	double Version = 0;
	const TSharedPtr<FJsonObject>* Numeric = nullptr;
	const TSharedPtr<FJsonObject>* Palette = nullptr;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root)
		|| !Root.IsValid() || !Root->TryGetNumberField(TEXT("version"), Version) || Version != 1
		|| !Root->TryGetObjectField(TEXT("numbers"), Numeric)
		|| !Root->TryGetObjectField(TEXT("colors"), Palette))
	{
		Error = TEXT("Invalid theme: expected version 1 with numbers and colors objects.");
		return false;
	}

	TMap<FName, float> PendingNumbers;
	TMap<FName, FLinearColor> PendingColors;
	for (const auto& Pair : (*Numeric)->Values)
	{
		const FName Name(*Pair.Key);
		const FMixtormatThemeNumber* Entry = Numbers().FindByPredicate(
			[Name](const FMixtormatThemeNumber& Item) { return Item.Name == Name; });
		double Value = 0;
		if (!Entry || !Pair.Value->TryGetNumber(Value) || !FMath::IsFinite(Value)
			|| Value < Entry->Minimum || Value > Entry->Maximum)
		{
			Error = FString::Printf(TEXT("Unknown or out-of-range numeric token: %s"), *Pair.Key);
			return false;
		}
		PendingNumbers.Add(Name, static_cast<float>(Value));
	}
	for (const auto& Pair : (*Palette)->Values)
	{
		const FName Name(*Pair.Key);
		const TArray<TSharedPtr<FJsonValue>>* Channels = nullptr;
		if (!Colors().ContainsByPredicate([Name](const FMixtormatThemeColor& Item) { return Item.Name == Name; })
			|| !Pair.Value->TryGetArray(Channels) || Channels->Num() != 4)
		{
			Error = FString::Printf(TEXT("Unknown color or invalid RGBA array: %s"), *Pair.Key);
			return false;
		}
		float Values[4];
		for (int32 Index = 0; Index < 4; ++Index)
		{
			double Value = 0;
			if (!(*Channels)[Index]->TryGetNumber(Value) || !FMath::IsFinite(Value) || Value < 0 || Value > 1)
			{
				Error = FString::Printf(TEXT("Color channels must be finite linear values from 0 to 1: %s"), *Pair.Key);
				return false;
			}
			Values[Index] = static_cast<float>(Value);
		}
		PendingColors.Add(Name, FLinearColor(Values[0], Values[1], Values[2], Values[3]));
	}

	// A valid file is a complete override layer: omitted entries take their authored defaults.
	Reset();
	for (const auto& Pair : PendingNumbers)
	{
		SetNumber(Pair.Key, Pair.Value);
	}
	ColorOverrides() = MoveTemp(PendingColors);
	return true;
}

FString FMixtormatLiveTheme::SavePath()
{
	return FMixtormatPaths::LiveThemePath();
}

bool FMixtormatLiveTheme::Save(FString& Error)
{
	Error.Reset();
	const FString Path = SavePath();
	const FString TemporaryPath = Path + TEXT(".tmp");
	if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true)
		|| !FFileHelper::SaveStringToFile(Serialize(), *TemporaryPath)
		|| !IFileManager::Get().Move(*Path, *TemporaryPath, true, true))
	{
		Error = FString::Printf(
			TEXT("Could not save the theme. Check the project's Saved/%s folder permissions."),
			*FMixtormatPaths::ProductName().ToString());
		return false;
	}
	return true;
}

bool FMixtormatLiveTheme::Load(FString& Error)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *SavePath()))
	{
		Error = FString::Printf(
			TEXT("Could not read Saved/%s/LiveTheme.json. Save a theme first."),
			*FMixtormatPaths::ProductName().ToString());
		return false;
	}
	return Deserialize(Text, Error);
}
