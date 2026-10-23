// Copyright 2026 Hugo Beyer. All Rights Reserved.
#include "Services/MixtormatPresetStore.h"
#include "Services/MixtormatPresetJson.h"
#include "Services/MixtormatRecipeService.h"
#include "MixtormatSurfacingTypes.h"
#include "Widgets/MixtormatChildCapabilities.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"

namespace MixtormatPresetStore
{
	namespace Documents
	{
		constexpr int32 Schema = 1;
		constexpr int64 MaxBytes = 8 * 1024 * 1024;
		constexpr int32 MaxName = 128;
		constexpr int32 MaxMetadata = 4096;

		bool Fail(FText& Error, const FString& Message) { Error = FText::FromString(Message); return false; }
		FString UserDirectory(const TCHAR* Kind) { return FPaths::Combine(GetUserPresetDirectory(), Kind); }
		FString ShippedDirectory(const TCHAR* Kind)
		{
			const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Mixtormat"));
			return Plugin ? FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources/Presets"), Kind) : FString();
		}
		bool ValidInfo(const FDocumentInfo& Info, FText& Error)
		{
			if (!Info.Id.IsValid() || Info.Name.TrimStartAndEnd().IsEmpty() || Info.Name.Len() > MaxName
				|| Info.Description.Len() > MaxMetadata || Info.Category.Len() > MaxName)
			{
				return Fail(Error, TEXT("Preset needs a valid ID, a name/category of at most 128 characters and a description of at most 4096 characters."));
			}
			for (TCHAR Ch : Info.Name + Info.Category)
			{
				if (Ch < TCHAR(0x20) || Ch == TCHAR(0x7f) || Ch == TCHAR(0x2028) || Ch == TCHAR(0x2029))
								{ return Fail(Error, TEXT("Preset names and categories must be single-line text without control characters.")); }
			}
			return true;
		}

		FStructProperty* ModuleProperty(const FMixtormatLayerChild& Child)
		{
			FName Name;
			switch (Child.Type)
			{
#define PAYLOAD(Tag, Field) case EMixtormatLayerChildType::Tag: Name = TEXT(#Field); break;
			PAYLOAD(Mask, Mask) PAYLOAD(Effect, Effect) PAYLOAD(Generated, Generated)
			PAYLOAD(Craquelure, Craquelure) PAYLOAD(ColorId, ColorId) PAYLOAD(Filter, Filter)
			PAYLOAD(HsvFilter, HsvFilter) PAYLOAD(RandomId, RandomId) PAYLOAD(RampId, RampId)
			PAYLOAD(PatternId, PatternId) PAYLOAD(Blur, Blur) PAYLOAD(Curvature, Curvature)
			PAYLOAD(CombineId, CombineId) PAYLOAD(Generator, Generator) PAYLOAD(Boolean, Boolean)
			PAYLOAD(Surfacing, Surfacing)
			PAYLOAD(UvFromIds, UvId) PAYLOAD(ReliefFromIds, ReliefId) PAYLOAD(IdGroup, IdGroup)
			PAYLOAD(OutputReference, OutputReference) PAYLOAD(BoundaryFromIds, BoundaryId)
			PAYLOAD(HeightBlend, HeightBlend) PAYLOAD(HeightCurve, HeightCurve)
			PAYLOAD(HeightColorRamp, HeightColorRamp) PAYLOAD(Behavior, Behavior)
#undef PAYLOAD
			default: return nullptr;
			}
			return FindFProperty<FStructProperty>(FMixtormatLayerChild::StaticStruct(), Name);
		}

		void GatherAssets(const UScriptStruct* Type, const void* Data, TArray<FSoftObjectPath>& Assets)
		{
			MixtormatPresetJson::Visit(Type, const_cast<void*>(Data), [](const UScriptStruct*, void*) {},
				[&Assets](const FSoftObjectPath& Path) { Assets.AddUnique(Path); });
			Assets.Sort([](const FSoftObjectPath& A, const FSoftObjectPath& B) { return A.ToString() < B.ToString(); });
		}
		bool CheckAssets(const TArray<FSoftObjectPath>& Assets, FText& Error)
		{
			FString Missing;
			for (const FSoftObjectPath& Path : Assets)
			{
				// Resolve the declared asset, not arbitrary serialized UObject text.
				if (!Path.IsValid() || !Path.TryLoad()) { Missing += TEXT("\n") + Path.ToString(); }
			}
			return Missing.IsEmpty() || Fail(Error, TEXT("Missing referenced assets (not embedded in JSON):") + Missing);
		}
		TSharedRef<FJsonObject> Envelope(const FDocumentInfo& Info, const TCHAR* Kind, const TArray<FSoftObjectPath>& Assets)
		{
			TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetNumberField(TEXT("schema"), Schema);
			Json->SetStringField(TEXT("kind"), Kind);
			Json->SetStringField(TEXT("id"), Info.Id.ToString(EGuidFormats::DigitsWithHyphens));
			Json->SetStringField(TEXT("name"), Info.Name);
			Json->SetStringField(TEXT("description"), Info.Description);
			Json->SetStringField(TEXT("category"), Info.Category);
			TArray<TSharedPtr<FJsonValue>> Dependencies;
			for (const FSoftObjectPath& Path : Assets) { Dependencies.Add(MakeShared<FJsonValueString>(Path.ToString())); }
			Json->SetArrayField(TEXT("referencedAssets"), Dependencies);
			return Json;
		}
		bool ReadEnvelope(const FString& Filename, const TCHAR* Kind, FDocumentInfo& Info, TSharedPtr<FJsonObject>& Json, FText& Error)
		{
			const int64 Size = IFileManager::Get().FileSize(*Filename);
			FString Text;
			if (Size <= 0 || Size > MaxBytes || !FFileHelper::LoadFileToString(Text, *Filename) || Text.Len() > MaxBytes)
			{ return Fail(Error, TEXT("Cannot read preset JSON, or file exceeds 8 MiB.")); }
			int32 Depth = 0;
			bool bString = false, bEscape = false;
			for (TCHAR Ch : Text)
			{
				if (bString)
				{
					if (bEscape) { bEscape = false; }
					else if (Ch == TEXT('\\')) { bEscape = true; }
					else if (Ch == TEXT('"')) { bString = false; }
				}
				else if (Ch == TEXT('"')) { bString = true; }
				else if (Ch == TEXT('{') || Ch == TEXT('['))
				{ if (++Depth > MixtormatPresetJson::MaxDepth) { return Fail(Error, TEXT("Preset JSON exceeds the nesting limit.")); } }
				else if (Ch == TEXT('}') || Ch == TEXT(']')) { --Depth; }
			}
			if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid())
			{ return Fail(Error, TEXT("Invalid preset JSON object.")); }
			double Version = 0;
			FString Id, ActualKind;
			const TArray<TSharedPtr<FJsonValue>>* Assets = nullptr;
			if (!Json->TryGetNumberField(TEXT("schema"), Version) || Version != Schema
				|| !Json->TryGetStringField(TEXT("kind"), ActualKind) || ActualKind != Kind
				|| !Json->TryGetStringField(TEXT("id"), Id) || !FGuid::Parse(Id, Info.Id)
				|| !Json->TryGetStringField(TEXT("name"), Info.Name)
				|| !Json->TryGetStringField(TEXT("description"), Info.Description)
				|| !Json->TryGetStringField(TEXT("category"), Info.Category)
				|| !Json->TryGetArrayField(TEXT("referencedAssets"), Assets) || Assets->Num() > MixtormatPresetJson::MaxArrayItems)
			{ return Fail(Error, TEXT("Unsupported schema/kind or incomplete preset envelope.")); }
			for (const TSharedPtr<FJsonValue>& Asset : *Assets)
			{
				FString Path;
				if (!Asset.IsValid() || !Asset->TryGetString(Path) || !FSoftObjectPath(Path).IsValid() || !Path.StartsWith(TEXT("/")))
				{ return Fail(Error, TEXT("Invalid referenced asset declaration.")); }
				Info.Dependencies.AddUnique(FSoftObjectPath(Path));
			}
			Info.Filename = Filename;
			return ValidInfo(Info, Error);
		}
		bool CheckDeclarations(const FDocumentInfo& Info, const TArray<FSoftObjectPath>& Actual, FText& Error)
		{
			if (Info.Dependencies.Num() != Actual.Num()) { return Fail(Error, TEXT("Asset dependency declarations do not match the payload.")); }
			for (const FSoftObjectPath& Path : Actual)
			{ if (!Info.Dependencies.Contains(Path)) { return Fail(Error, TEXT("Undeclared asset dependency: ") + Path.ToString()); } }
			return true;
		}
		bool Write(const FString& Filename, const TSharedRef<FJsonObject>& Json, FText& Error, bool bOverwrite = false)
		{
			if (FPaths::GetExtension(Filename).ToLower() != TEXT("json") || (!bOverwrite && IFileManager::Get().FileExists(*Filename)))
			{ return Fail(Error, TEXT("Choose a new .json filename. Existing presets are never overwritten.")); }
			FString Text;
			if (!FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text)) || FTCHARToUTF8(*Text).Length() > MaxBytes)
			{ return Fail(Error, TEXT("Cannot encode preset, or payload exceeds 8 MiB.")); }
			const FString Temporary = Filename + TEXT(".") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".tmp");
			if (!FFileHelper::SaveStringToFile(Text, *Temporary, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{ return Fail(Error, TEXT("Cannot write preset temporary file.")); }
			if (!IFileManager::Get().Move(*Filename, *Temporary, bOverwrite, false))
			{
				IFileManager::Get().Delete(*Temporary);
				return Fail(Error, TEXT("Cannot publish preset. Destination may exist or be read-only."));
			}
			return true;
		}
		template<typename T, typename Load>
		bool List(const TCHAR* Kind, TArray<T>& Out, FText& Error, Load Loader)
		{
			TArray<T> Entries;
			Error = FText::GetEmpty();
			const FString Roots[] = {UserDirectory(Kind), ShippedDirectory(Kind)};
			for (int32 RootIndex = 0; RootIndex < UE_ARRAY_COUNT(Roots); ++RootIndex)
			{
				if (Roots[RootIndex].IsEmpty()) { continue; }
				TArray<FString> Files;
				IFileManager::Get().FindFiles(Files, *FPaths::Combine(Roots[RootIndex], TEXT("*.json")), true, false);
				Files.Sort();
				for (const FString& File : Files)
				{
					T Entry;
					FText Issue;
					if (!Loader(FPaths::Combine(Roots[RootIndex], File), Entry, Issue))
					{
						Error = FText::FromString(Error.ToString() + File + TEXT(": ") + Issue.ToString() + TEXT("\n"));
						continue; // Keep good entries visible; bad entries are explicitly reported.
					}
					Entry.bShipped = RootIndex == 1;
					Entries.Add(MoveTemp(Entry));
				}
			}
			Entries.Sort([](const T& A, const T& B) { return A.Name < B.Name; });
			Out = MoveTemp(Entries);
			return Error.IsEmpty();
		}
		template<typename T, typename Export>
		bool Save(const TCHAR* Kind, const T& Preset, FText& Error, Export Exporter)
		{
			if (!ValidInfo(Preset, Error)) { return false; }
			const FString Root = UserDirectory(Kind);
			if (!IFileManager::Get().MakeDirectory(*Root, true)) { return Fail(Error, TEXT("Cannot create user preset directory.")); }
			// Name collisions are checked without loading assets or requiring corrupt entries to be deleted.
			TArray<FString> Files;
			IFileManager::Get().FindFiles(Files, *FPaths::Combine(Root, TEXT("*.json")), true, false);
			for (const FString& File : Files)
			{
				FDocumentInfo Existing;
				TSharedPtr<FJsonObject> Json;
				FText Issue;
				if (ReadEnvelope(FPaths::Combine(Root, File), Kind, Existing, Json, Issue)
					&& Existing.Name.Equals(Preset.Name, ESearchCase::IgnoreCase))
				{ return Fail(Error, TEXT("That preset name already exists. Choose a different name.")); }
			}
			return Exporter(FPaths::Combine(Root, Preset.Id.ToString(EGuidFormats::Digits) + TEXT(".json")), Preset, Error);
		}
	}

	bool IsModuleCompatible(const FMixtormatLayerChild& A, const FMixtormatLayerChild& B)
	{
		if (A.Type != B.Type) { return false; }
		if (A.Type == EMixtormatLayerChildType::Generator) { return A.Generator.Type == B.Generator.Type; }
		if (A.Type == EMixtormatLayerChildType::Surfacing) { return A.Surfacing.Type == B.Surfacing.Type; }
		if (A.Type == EMixtormatLayerChildType::Effect) { return ResolveChildEffectType(A) == ResolveChildEffectType(B); }
		if (A.Type == EMixtormatLayerChildType::Behavior) { return A.Behavior.Type == B.Behavior.Type; }
				if (A.Type == EMixtormatLayerChildType::OutputReference) { return A.OutputReference.Kind == B.OutputReference.Kind; }
		return true;
	}

	bool CaptureModule(const FMixtormatLayerChild& Child, FModulePreset& OutPreset, FText& Error)
	{
		FStructProperty* Property = Documents::ModuleProperty(Child);
		if (!Property || Child.IsInstance()) { return Documents::Fail(Error, TEXT("This module is unavailable or is an instance. Edit its source instead.")); }
		TSharedPtr<FJsonObject> Json;
		if (!MixtormatPresetJson::Encode(Property->Struct, Property->ContainerPtrToValuePtr<void>(&Child), true, Json, Error)) { return false; }
		FModulePreset Preset;
		Preset.Settings.Type = Child.Type;
		if (!MixtormatPresetJson::Decode(Property->Struct, Property->ContainerPtrToValuePtr<void>(&Preset.Settings), true, Json, Error)) { return false; }
		Documents::GatherAssets(Property->Struct, Property->ContainerPtrToValuePtr<void>(&Preset.Settings), Preset.Dependencies);
		OutPreset = MoveTemp(Preset);
		return true;
	}
	bool ApplyModule(const FModulePreset& Preset, FMixtormatLayerChild& Destination, FText& Error)
	{
		if (Destination.IsInstance() || !IsModuleCompatible(Preset.Settings, Destination))
		{ return Documents::Fail(Error, TEXT("Wrong module subtype, or destination is an instance.")); }
		FStructProperty* Property = Documents::ModuleProperty(Destination);
		if (!Property) { return Documents::Fail(Error, TEXT("Unsupported module payload.")); }
		TSharedPtr<FJsonObject> Json;
		if (!MixtormatPresetJson::Encode(Property->Struct, Property->ContainerPtrToValuePtr<void>(&Preset.Settings), true, Json, Error)) { return false; }
		FMixtormatLayerChild Copy = Destination;
		if (!MixtormatPresetJson::Decode(Property->Struct, Property->ContainerPtrToValuePtr<void>(&Copy), true, Json, Error)) { return false; }
		if (Destination.Type == EMixtormatLayerChildType::Surfacing
			&& Destination.Surfacing.Type == EMixtormatSurfacingType::Skin)
		{
			// A look changes tissue settings, never destination composition gates.
			// Masks, bindings and scope remain on the copied destination child.
			Copy.Surfacing.bEnabled = Destination.Surfacing.bEnabled;
			FMixtormatSkin& Skin = Copy.Surfacing.Skin;
			const FMixtormatSkin& Original = Destination.Surfacing.Skin;
			Skin.Amount = Original.Amount;
			Skin.bWriteHeight = Original.bWriteHeight;
			Skin.bWriteColor = Original.bWriteColor;
			Skin.bWriteRoughness = Original.bWriteRoughness;
			Skin.bWriteAO = Original.bWriteAO;
			Skin.bWriteF0 = Original.bWriteF0;
			Skin.bWriteFuzz = Original.bWriteFuzz;
			Skin.bWriteSSS = Original.bWriteSSS;
		}
		if (Destination.Type == EMixtormatLayerChildType::Surfacing
			&& Destination.Surfacing.Type == EMixtormatSurfacingType::Moss)
		{
			Copy.Surfacing.bEnabled = Destination.Surfacing.bEnabled;
			FMixtormatMoss& Moss = Copy.Surfacing.Moss;
			const FMixtormatMoss& Original = Destination.Surfacing.Moss;
			Moss.Amount = Original.Amount;
			Moss.bWriteHeight = Original.bWriteHeight;
			Moss.bWriteColor = Original.bWriteColor;
			Moss.bWriteRoughness = Original.bWriteRoughness;
			Moss.bWriteNormal = Original.bWriteNormal;
		}
		TArray<FSoftObjectPath> Assets;
		Documents::GatherAssets(Property->Struct, Property->ContainerPtrToValuePtr<void>(&Copy), Assets);
		if (!Documents::CheckAssets(Assets, Error)
			|| !MixtormatPresetJson::ValidateAssetClasses(Property->Struct, Property->ContainerPtrToValuePtr<void>(&Copy), Error)) { return false; }
		Destination = MoveTemp(Copy);
		return true;
	}
	static bool WriteModule(const FString& Filename, const FModulePreset& Preset, FText& Error, bool bOverwrite)
	{
		if (!Documents::ValidInfo(Preset, Error)) { return false; }
		FStructProperty* Property = Documents::ModuleProperty(Preset.Settings);
		if (!Property) { return Documents::Fail(Error, TEXT("Unsupported module type.")); }
		if (!MixtormatPresetJson::ValidateAssetClasses(Property->Struct, Property->ContainerPtrToValuePtr<void>(&Preset.Settings), Error)) { return false; }
		TSharedPtr<FJsonObject> Payload;
		if (!MixtormatPresetJson::Encode(Property->Struct, Property->ContainerPtrToValuePtr<void>(&Preset.Settings), true, Payload, Error)) { return false; }
		TArray<FSoftObjectPath> Assets;
		Documents::GatherAssets(Property->Struct, Property->ContainerPtrToValuePtr<void>(&Preset.Settings), Assets);
		TSharedRef<FJsonObject> Json = Documents::Envelope(Preset, TEXT("Modules"), Assets);
		Json->SetNumberField(TEXT("childType"), static_cast<int32>(Preset.Settings.Type));
		Json->SetObjectField(TEXT("payload"), Payload);
		return Documents::Write(Filename, Json, Error, bOverwrite);
	}
	bool ExportModule(const FString& Filename, const FModulePreset& Preset, FText& Error)
	{
		return WriteModule(Filename, Preset, Error, false);
	}
	bool OverwriteModule(const FModulePreset& Target, const FModulePreset& Current, FText& Error)
	{
		if (!IsModuleCompatible(Target.Settings, Current.Settings))
		{ return Documents::Fail(Error, TEXT("Cannot overwrite a different module subtype.")); }
		FModulePreset Replacement = Current;
		Replacement.Name = Target.Name;
		Replacement.Category = Target.Category;
		Replacement.Description = Target.Description;
		const FString Root = Documents::UserDirectory(TEXT("Modules"));
		if (!IFileManager::Get().MakeDirectory(*Root, true))
		{ return Documents::Fail(Error, TEXT("Cannot create user preset directory.")); }
		// Built-in and plugin presets are replaced in the user library, never in the install.
		FString Filename = FPaths::Combine(Root, Replacement.Id.ToString(EGuidFormats::Digits) + TEXT(".json"));
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *FPaths::Combine(Root, TEXT("*.json")), true, false);
		for (const FString& File : Files)
		{
			FModulePreset Existing;
			FText Issue;
			const FString Path = FPaths::Combine(Root, File);
			if (LoadModule(Path, Existing, Issue) && Existing.Name.Equals(Target.Name, ESearchCase::IgnoreCase)
				&& IsModuleCompatible(Existing.Settings, Target.Settings))
			{
				Replacement.Id = Existing.Id;
				Filename = Path;
				break;
			}
		}
		return WriteModule(Filename, Replacement, Error, true);
	}
	bool LoadModule(const FString& Filename, FModulePreset& OutPreset, FText& Error)
	{
		FModulePreset Preset;
		TSharedPtr<FJsonObject> Json;
		if (!Documents::ReadEnvelope(Filename, TEXT("Modules"), Preset, Json, Error)) { return false; }
		double ChildType = 0;
		if (!Json->TryGetNumberField(TEXT("childType"), ChildType) || ChildType < 0 || ChildType > MAX_uint8 || ChildType != FMath::FloorToDouble(ChildType))
		{ return Documents::Fail(Error, TEXT("Invalid module child type.")); }
		Preset.Settings.Type = static_cast<EMixtormatLayerChildType>(static_cast<uint8>(ChildType));
		FStructProperty* Property = Documents::ModuleProperty(Preset.Settings);
		const TSharedPtr<FJsonObject>* Payload = nullptr;
		if (Json->Values.Num() != 9 || !Property || !Json->TryGetObjectField(TEXT("payload"), Payload)) { return Documents::Fail(Error, TEXT("Unsupported module payload.")); }
		if (!MixtormatPresetJson::Decode(Property->Struct, Property->ContainerPtrToValuePtr<void>(&Preset.Settings), true, *Payload, Error)) { return false; }
		TArray<FSoftObjectPath> Assets;
		Documents::GatherAssets(Property->Struct, Property->ContainerPtrToValuePtr<void>(&Preset.Settings), Assets);
		if (!Documents::CheckDeclarations(Preset, Assets, Error)) { return false; }
		OutPreset = MoveTemp(Preset);
		return true;
	}
	bool ListModules(const FMixtormatLayerChild& Child, TArray<FModulePreset>& OutPresets, FText& Error)
	{
		TArray<FModulePreset> Presets;
		const bool bClean = Documents::List(TEXT("Modules"), Presets, Error, LoadModule);
		// User entries take precedence over matching plugin entries of the same subtype.
		TArray<FModulePreset> Visible;
		for (const FModulePreset& Preset : Presets)
		{
			const bool bOverridden = Preset.bShipped && Presets.ContainsByPredicate([&Preset](const FModulePreset& Other)
			{
				return !Other.bShipped && Other.Name.Equals(Preset.Name, ESearchCase::IgnoreCase)
					&& IsModuleCompatible(Other.Settings, Preset.Settings);
			});
			if (!bOverridden) { Visible.Add(Preset); }
		}
		Presets = MoveTemp(Visible);
		if (Child.Type == EMixtormatLayerChildType::Surfacing
			&& Child.Surfacing.Type == EMixtormatSurfacingType::Skin)
		{
			for (const FMixtormatSkinPreset& SkinPreset : MixtormatSurfacing::GetSkinPresets())
			{
				if (Presets.ContainsByPredicate([&](const FModulePreset& Existing)
				{
					return Existing.Name.Equals(SkinPreset.Name.ToString(), ESearchCase::IgnoreCase)
						&& IsModuleCompatible(Existing.Settings, Child);
				})) { continue; }
				FModulePreset& Preset = Presets.AddDefaulted_GetRef();
				Preset.Name = SkinPreset.Name.ToString();
				Preset.Description = SkinPreset.Description.ToString();
				Preset.Category = TEXT("Skin");
				Preset.bShipped = true;
				Preset.bBuiltIn = true;
				Preset.Settings.Type = EMixtormatLayerChildType::Surfacing;
				Preset.Settings.Surfacing.Type = EMixtormatSurfacingType::Skin;
				Preset.Settings.Surfacing.Skin = SkinPreset.Settings;
			}
			Presets.Sort([](const FModulePreset& A, const FModulePreset& B) { return A.Name < B.Name; });
		}
		if (Child.Type == EMixtormatLayerChildType::Surfacing
			&& Child.Surfacing.Type == EMixtormatSurfacingType::Moss)
		{
			for (const FMixtormatMossPreset& MossPreset : MixtormatSurfacing::GetMossPresets())
			{
				if (Presets.ContainsByPredicate([&](const FModulePreset& Existing)
				{
					return Existing.Name.Equals(MossPreset.Name.ToString(), ESearchCase::IgnoreCase)
						&& IsModuleCompatible(Existing.Settings, Child);
				})) { continue; }
				FModulePreset& Preset = Presets.AddDefaulted_GetRef();
				Preset.Name = MossPreset.Name.ToString();
				Preset.Description = MossPreset.Description.ToString();
				Preset.Category = TEXT("Moss");
				Preset.bShipped = true;
				Preset.bBuiltIn = true;
				Preset.Settings.Type = EMixtormatLayerChildType::Surfacing;
				Preset.Settings.Surfacing.Type = EMixtormatSurfacingType::Moss;
				Preset.Settings.Surfacing.Moss = MossPreset.Settings;
			}
			Presets.Sort([](const FModulePreset& A, const FModulePreset& B) { return A.Name < B.Name; });
		}
		Presets.RemoveAll([&Child](const FModulePreset& Preset) { return !IsModuleCompatible(Child, Preset.Settings); });
		OutPresets = MoveTemp(Presets);
		return bClean;
	}
	bool SaveModule(const FModulePreset& Preset, FText& Error)
	{
		// Saving over a created preset replaces it instead of failing on the name.
		TArray<FModulePreset> Existing;
		FText ListError;
		if (ListModules(Preset.Settings, Existing, ListError))
		{
			for (const FModulePreset& Entry : Existing)
			{
				if (!Entry.bBuiltIn && Entry.Name.Equals(Preset.Name, ESearchCase::IgnoreCase))
				{
					return OverwriteModule(Entry, Preset, Error);
				}
			}
		}
		return Documents::Save(TEXT("Modules"), Preset, Error, ExportModule);
	}
	bool ImportModule(const FString& Filename, const FMixtormatLayerChild& Expected, FText& Error)
	{
		FModulePreset Preset;
		if (!LoadModule(Filename, Preset, Error)) { return false; }
		if (!IsModuleCompatible(Preset.Settings, Expected)) { return Documents::Fail(Error, TEXT("This JSON preset belongs to a different module subtype.")); }
		if (!Documents::CheckAssets(Preset.Dependencies, Error)) { return false; }
		FStructProperty* Property = Documents::ModuleProperty(Preset.Settings);
		if (!Property || !MixtormatPresetJson::ValidateAssetClasses(Property->Struct, Property->ContainerPtrToValuePtr<void>(&Preset.Settings), Error)) { return false; }
		Preset.Id = FGuid::NewGuid();
		return SaveModule(Preset, Error);
	}
	bool ExportRecipe(const FString& Filename, const FRecipePreset& Preset, FText& Error)
	{
		if (!Documents::ValidInfo(Preset, Error) || !MixtormatRecipeService::Validate(Preset, Error)) { return false; }
		if (!MixtormatPresetJson::ValidateAssetClasses(FMixtormatLayerGroup::StaticStruct(), &Preset.Group, Error)) { return false; }
		for (const FMixtormatLayer& Layer : Preset.Layers)
		{
			if (!MixtormatPresetJson::ValidateAssetClasses(FMixtormatLayer::StaticStruct(), &Layer, Error)) { return false; }
		}
		TSharedPtr<FJsonObject> Group;
		if (!MixtormatPresetJson::Encode(FMixtormatLayerGroup::StaticStruct(), &Preset.Group, false, Group, Error)) { return false; }
		TArray<FSoftObjectPath> Assets;
		Documents::GatherAssets(FMixtormatLayerGroup::StaticStruct(), &Preset.Group, Assets);
		TArray<TSharedPtr<FJsonValue>> Layers;
		for (const FMixtormatLayer& Layer : Preset.Layers)
		{
			TSharedPtr<FJsonObject> Json;
			if (!MixtormatPresetJson::Encode(FMixtormatLayer::StaticStruct(), &Layer, false, Json, Error)) { return false; }
			Layers.Add(MakeShared<FJsonValueObject>(Json));
			Documents::GatherAssets(FMixtormatLayer::StaticStruct(), &Layer, Assets);
		}
		TSharedRef<FJsonObject> Json = Documents::Envelope(Preset, TEXT("Recipes"), Assets);
		Json->SetObjectField(TEXT("group"), Group);
		Json->SetArrayField(TEXT("layers"), Layers);
		return Documents::Write(Filename, Json, Error);
	}
	bool LoadRecipe(const FString& Filename, FRecipePreset& OutPreset, FText& Error)
	{
		FRecipePreset Preset;
		TSharedPtr<FJsonObject> Json;
		if (!Documents::ReadEnvelope(Filename, TEXT("Recipes"), Preset, Json, Error)) { return false; }
		const TSharedPtr<FJsonObject>* Group = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Layers = nullptr;
		if (Json->Values.Num() != 9 || !Json->TryGetObjectField(TEXT("group"), Group) || !Json->TryGetArrayField(TEXT("layers"), Layers)
			|| Layers->IsEmpty() || Layers->Num() > MixtormatPresetJson::MaxArrayItems)
		{ return Documents::Fail(Error, TEXT("Recipe requires one group and its ordered member layers.")); }
		if (!MixtormatPresetJson::Decode(FMixtormatLayerGroup::StaticStruct(), &Preset.Group, false, *Group, Error)) { return false; }
		TArray<FSoftObjectPath> Assets;
		Documents::GatherAssets(FMixtormatLayerGroup::StaticStruct(), &Preset.Group, Assets);
		for (const TSharedPtr<FJsonValue>& Value : *Layers)
		{
			if (!Value.IsValid() || Value->Type != EJson::Object) { return Documents::Fail(Error, TEXT("Invalid recipe layer object.")); }
			FMixtormatLayer& Layer = Preset.Layers.AddDefaulted_GetRef();
			if (!MixtormatPresetJson::Decode(FMixtormatLayer::StaticStruct(), &Layer, false, Value->AsObject(), Error)) { return false; }
			Documents::GatherAssets(FMixtormatLayer::StaticStruct(), &Layer, Assets);
		}
		if (!Documents::CheckDeclarations(Preset, Assets, Error) || !MixtormatRecipeService::Validate(Preset, Error)) { return false; }
		OutPreset = MoveTemp(Preset);
		return true;
	}
	bool SaveRecipe(const FRecipePreset& Preset, FText& Error) { return Documents::Save(TEXT("Recipes"), Preset, Error, ExportRecipe); }
	bool ListRecipes(TArray<FRecipePreset>& OutPresets, FText& Error) { return Documents::List(TEXT("Recipes"), OutPresets, Error, LoadRecipe); }
	bool ImportRecipe(const FString& Filename, FText& Error)
	{
		FRecipePreset Preset;
		if (!LoadRecipe(Filename, Preset, Error) || !Documents::CheckAssets(Preset.Dependencies, Error)) { return false; }
		Preset.Id = FGuid::NewGuid();
		return SaveRecipe(Preset, Error);
	}
}
