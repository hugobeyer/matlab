// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "UI/Parameters/MixtormatParameterAuthoring.h"

#include "UI/Parameters/MixtormatParameterUiMeta.h"
#include "Containers/StringConv.h"
#include "Interfaces/IPluginManager.h"
#include "Logging/LogMacros.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/PlatformFile.h"
#include "UObject/UnrealType.h"

namespace
{
	// Family name in the database and in debug keys: the enum's own name ("Breakup",
	// "WornEdges", ...) -- stable, serialization-safe, and distinct from the display name.
	FString FamilyNameOf(EMixtormatEffectType Family)
	{
		return StaticEnum<EMixtormatEffectType>()->GetNameByValue(static_cast<int64>(Family)).ToString();
	}

	// "Effect.BreakupNormalStrength" / "Effect.BreakupSeed:Int" -- value type only annotated
	// when it is not the float default, so existing keys stay short and stable.
	FString KeyNameOf(const FMixtormatParameterDefinitionKey& Key)
	{
		FString Name = StaticEnum<EMixtormatParameterOwnerType>()
			->GetNameByValue(static_cast<int64>(Key.Owner)).ToString();
		Name += TEXT(".");
		Name += Key.Parameter.ToString();
		if (Key.ValueType != EMixtormatParameterValueType::Float)
		{
			Name += TEXT(":");
			Name += StaticEnum<EMixtormatParameterValueType>()
				->GetNameByValue(static_cast<int64>(Key.ValueType)).ToString();
		}
		return Name;
	}

	TOptional<float> ReadFloat(const TSharedPtr<FJsonValue>& Value)
	{
		// FJsonValue exposes its discriminated union as Type, not as IsNumber-style queries.
		if (!Value.IsValid() || Value->Type != EJson::Number)
		{
			return {};
		}
		return static_cast<float>(Value->AsNumber());
	}

	void WriteFloat(TSharedRef<FJsonObject> Object, const TCHAR* Field, const TOptional<float>& Value)
	{
		if (Value.IsSet())
		{
			Object->SetNumberField(Field, static_cast<double>(Value.GetValue()));
		}
		else
		{
			Object->RemoveField(Field);
		}
	}

	TOptional<float> ReadFloatField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		return ReadFloat(Object->TryGetField(Field));
	}

	// The shipped database, loaded lazily from the plugin JSON. Function-local statics keep
	// the file read to once per session and keep the mutable state out of global ctors.
	TMap<FMixtormatParameterDefinitionKey, FMixtormatParameterAuthoringEntry>& ShippedEntries()
	{
		static TMap<FMixtormatParameterDefinitionKey, FMixtormatParameterAuthoringEntry> Map;
		return Map;
	}

	TMap<FMixtormatParameterDefinitionKey, FMixtormatParameterAuthoringEntry>& PendingEntries()
	{
		static TMap<FMixtormatParameterDefinitionKey, FMixtormatParameterAuthoringEntry> Map;
		return Map;
	}

	// Once a load has happened -- from disk or from a string (tests) -- the database must not
	// be silently replaced by the file on the next failed lookup.
	bool& DatabaseLoaded()
	{
		static bool bLoaded = false;
		return bLoaded;
	}

	const FMixtormatParameterAuthoringEntry* FindEffective(
		const FMixtormatParameterDefinitionKey& Key)
	{
		if (const FMixtormatParameterAuthoringEntry* Pending = PendingEntries().Find(Key))
		{
			return Pending;
		}
		return ShippedEntries().Find(Key);
	}
}

namespace MixtormatParameterAuthoring
{
	bool IsPersistentlyEditable(const FMixtormatParameterDefinitionKey& Key)
	{
		const FMixtormatParameterDefinition* Definition = MixtormatParameterDefinitions::TryGet(Key);
		return Definition
			&& Definition->Policy == EMixtormatParameterAuthoringPolicy::PersistentDevTunable;
	}

	const FMixtormatParameterAuthoringEntry* TryGetShipped(const FMixtormatParameterDefinitionKey& Key)
	{
		LoadFromDisk();
		return ShippedEntries().Find(Key);
	}

	bool HasShippedAuthoring(const FMixtormatParameterDefinitionKey& Key)
	{
		return TryGetShipped(Key) != nullptr;
	}

	const FMixtormatParameterAuthoringEntry* TryGetPending(const FMixtormatParameterDefinitionKey& Key)
	{
		return PendingEntries().Find(Key);
	}

	bool HasPendingAuthoring(const FMixtormatParameterDefinitionKey& Key)
	{
		return PendingEntries().Contains(Key);
	}

	bool HasAnyPendingAuthoring()
	{
		return !PendingEntries().IsEmpty();
	}

	void SetPendingAuthoring(
		const FMixtormatParameterDefinitionKey& Key,
		const FMixtormatParameterAuthoringEntry& Entry)
	{
		// An entry equal to "nothing" clears the pending edit rather than storing it, so the
		// unsaved set only ever contains deltas that mean something.
		if (Entry.IsEmpty())
		{
			PendingEntries().Remove(Key);
		}
		else
		{
			PendingEntries().Add(Key, Entry);
		}
	}

	void RevertPendingAuthoring(const FMixtormatParameterDefinitionKey& Key)
	{
		PendingEntries().Remove(Key);
	}

	bool SavePendingToPluginDefaults()
	{
		if (PendingEntries().IsEmpty())
		{
			return true;
		}
		for (const TPair<FMixtormatParameterDefinitionKey, FMixtormatParameterAuthoringEntry>& Pair
			: PendingEntries())
		{
			if (Pair.Value.IsEmpty())
			{
				ShippedEntries().Remove(Pair.Key);
			}
			else
			{
				ShippedEntries().Add(Pair.Key, Pair.Value);
			}
		}
		// Commit pending only after a successful write: a read-only plugin directory must
		// leave the unsaved edits in memory, not silently drop them.
		if (!SaveToDisk())
		{
			return false;
		}
		PendingEntries().Reset();
		return true;
	}

	bool RestoreShippedAuthoring(const FMixtormatParameterDefinitionKey& Key)
	{
		PendingEntries().Remove(Key);
		FMixtormatParameterAuthoringEntry Removed;
		if (ShippedEntries().RemoveAndCopyValue(Key, Removed) == 0)
		{
			return true; // Nothing shipped: already restored.
		}
		if (!SaveToDisk())
		{
			// Roll the entry back so the in-memory database still matches the file.
			ShippedEntries().Add(Key, MoveTemp(Removed));
			return false;
		}
		return true;
	}

	float ResolveAuthoringDefault(const FMixtormatParameterDefinitionKey& Key, const float FallbackStored)
	{
		if (const FMixtormatParameterAuthoringEntry* Entry = FindEffective(Key))
		{
			if (Entry->Default.IsSet())
			{
				return Entry->Default.GetValue();
			}
		}
		if (const FMixtormatParameterDefinition* Definition = MixtormatParameterDefinitions::TryGet(Key))
		{
			return Definition->Default;
		}
		return FallbackStored;
	}

	float ResolveAuthoringUiBound(const FMixtormatParameterDefinitionKey& Key, const float Fallback, const bool bMax)
	{
		if (const FMixtormatParameterAuthoringEntry* Entry = FindEffective(Key))
		{
			const TOptional<float> Bound = bMax ? Entry->UiMax : Entry->UiMin;
			if (Bound.IsSet())
			{
				return Bound.GetValue();
			}
		}
		// Falls through to the session override / UiMeta / literal chain.
		return MixtormatParameterUi::ResolveUiBound(Key, Fallback, bMax);
	}

	float ResolveAuthoringSnap(const FMixtormatParameterDefinitionKey& Key, const float Fallback)
	{
		if (const FMixtormatParameterAuthoringEntry* Entry = FindEffective(Key))
		{
			if (Entry->Snap.IsSet())
			{
				return Entry->Snap.GetValue();
			}
		}
		return MixtormatParameterUi::ResolveUiSnap(Key, Fallback);
	}

	FText ResolveAuthoringLabel(const FMixtormatParameterDefinitionKey& Key, const FText& Fallback)
	{
		if (const FMixtormatParameterAuthoringEntry* Entry = FindEffective(Key))
		{
			if (!Entry->Label.IsEmpty())
			{
				return FText::FromString(Entry->Label);
			}
		}
		return Fallback;
	}

	void ApplyAuthoringDefaults(FMixtormatLayerEffect& Effect, const EMixtormatEffectType Family)
	{
		UScriptStruct* const EffectStruct = FMixtormatLayerEffect::StaticStruct();
		MixtormatParameterDefinitions::ForEach(
			[&Effect, EffectStruct, Family](const FMixtormatParameterDefinition& Definition)
		{
			// Only genuinely-new instances of the same family, and only params the database
			// actually speaks for. The compiled struct initializer already holds every
			// compiled default; the database layers persistent retunes on top of it.
			if (Definition.EffectFamily != Family
				|| Definition.Policy != EMixtormatParameterAuthoringPolicy::PersistentDevTunable)
			{
				return;
			}
			const FMixtormatParameterAuthoringEntry* Entry = TryGetShipped(Definition.GetKey());
			if (!Entry || !Entry->Default.IsSet())
			{
				return;
			}

			const FProperty* Property = EffectStruct->FindPropertyByName(Definition.Parameter);
			if (const FFloatProperty* Float = CastField<FFloatProperty>(Property))
			{
				*Float->ContainerPtrToValuePtr<float>(&Effect) = Entry->Default.GetValue();
			}
			else if (const FIntProperty* Int = CastField<FIntProperty>(Property))
			{
				*Int->ContainerPtrToValuePtr<int32>(&Effect) =
					FMath::RoundToInt(Entry->Default.GetValue());
			}
		});
	}

	bool LoadFromString(const FString& Json)
	{
		ShippedEntries().Reset();
		DatabaseLoaded() = true;

		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Mixtormat authoring database: unreadable JSON, falling back to compiled defaults."));
			return false;
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& FamilyPair : Root->Values)
		{
			const TSharedPtr<FJsonObject>* FamilyObject;
			if (!FamilyPair.Value->TryGetObject(FamilyObject) || !FamilyObject->IsValid())
			{
				continue;
			}

			// Family -> EMixtormatEffectType by enum name, so keys stay stable across
			// display-name changes and the definition table can match them back.
			const UEnum* FamilyEnum = StaticEnum<EMixtormatEffectType>();
			int64 FamilyValue = FamilyEnum->GetValueByNameString(FamilyPair.Key);
			if (FamilyValue == INDEX_NONE)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("Mixtormat authoring database: unknown family '%s' skipped."), *FamilyPair.Key);
				continue;
			}

			for (const TPair<FString, TSharedPtr<FJsonValue>>& ParamPair : (*FamilyObject)->Values)
			{
				const TSharedPtr<FJsonObject>* EntryObject;
				if (!ParamPair.Value->TryGetObject(EntryObject) || !EntryObject->IsValid())
				{
					continue;
				}

				FMixtormatParameterAuthoringEntry Entry;
				Entry.Label = (*EntryObject)->GetStringField(TEXT("label"));
				Entry.Default = ReadFloatField(*EntryObject, TEXT("default"));
				Entry.UiMin = ReadFloatField(*EntryObject, TEXT("uiMin"));
				Entry.UiMax = ReadFloatField(*EntryObject, TEXT("uiMax"));
				Entry.Snap = ReadFloatField(*EntryObject, TEXT("snap"));

				// Value type suffix on the key: ":Int" when not the float default.
				EMixtormatParameterValueType ValueType = EMixtormatParameterValueType::Float;
				FString ParameterName = ParamPair.Key;
				if (ParameterName.EndsWith(TEXT(":Int")))
				{
					ValueType = EMixtormatParameterValueType::Int;
					ParameterName.LeftChopInline(4);
				}

				FMixtormatParameterDefinitionKey Key;
				Key.Owner = EMixtormatParameterOwnerType::Effect;
				Key.Parameter = FName(*ParameterName);
				Key.ValueType = ValueType;
				if (!Entry.IsEmpty())
				{
					ShippedEntries().Add(Key, MoveTemp(Entry));
				}
			}
		}
		return true;
	}

	FString WriteToString()
	{
		// { "<Family>": { "<Parameter>": { ... } } } -- family-scoped so a future family
		// migration cannot collide with another's parameter block.
		const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		TMap<FString, TSharedRef<FJsonObject>> Families;

		for (const TPair<FMixtormatParameterDefinitionKey, FMixtormatParameterAuthoringEntry>& Pair
			: ShippedEntries())
		{
			const FMixtormatParameterDefinition* Definition =
				MixtormatParameterDefinitions::TryGet(Pair.Key);
			const FString Family = Definition
				? FamilyNameOf(Definition->EffectFamily)
				: TEXT("Unknown");

			TSharedRef<FJsonObject>* FamilyObject = Families.Find(Family);
			if (!FamilyObject)
			{
				FamilyObject = &Families.Add(Family, MakeShared<FJsonObject>());
			}

			const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			if (!Pair.Value.Label.IsEmpty())
			{
				Entry->SetStringField(TEXT("label"), Pair.Value.Label);
			}
			WriteFloat(Entry, TEXT("default"), Pair.Value.Default);
			WriteFloat(Entry, TEXT("uiMin"), Pair.Value.UiMin);
			WriteFloat(Entry, TEXT("uiMax"), Pair.Value.UiMax);
			WriteFloat(Entry, TEXT("snap"), Pair.Value.Snap);
			(*FamilyObject)->SetObjectField(KeyNameOf(Pair.Key), Entry);
		}

		for (const TPair<FString, TSharedRef<FJsonObject>>& Family : Families)
		{
			Root->SetObjectField(Family.Key, Family.Value);
		}

		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Root, Writer);
		return Out;
	}

	FString GetDatabaseFilePath()
	{
		// Resolved through the plugin manager so the database follows the plugin wherever it
		// is installed (project Plugins/, engine Marketplace copy, mounted from elsewhere).
		if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Mixtormat")))
		{
			return FPaths::Combine(
				Plugin->GetBaseDir(), TEXT("Config"),
				TEXT("MixtormatParameterAuthoring.json"));
		}
		// Fallback for exotic setups where the plugin is not registered under its own name.
		return FPaths::Combine(
			FPaths::ProjectPluginsDir(), TEXT("Mixtormat"), TEXT("Config"),
			TEXT("MixtormatParameterAuthoring.json"));
	}

	bool LoadFromDisk()
	{
		// Loaded once per session: an empty shipped map after a successful load is a valid
		// state (no entries), so the disk is not re-read on every failed lookup.
		if (DatabaseLoaded())
		{
			return true;
		}
		DatabaseLoaded() = true;

		const FString Path = GetDatabaseFilePath();
		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *Path))
		{
			return false; // No database yet: compiled defaults only.
		}
		return LoadFromString(Json);
	}

	bool SaveToDisk()
	{
		// Written as raw UTF-8 bytes (no BOM): SaveStringToFile's write-flag enums have been
		// renamed across engine versions, and encoding the bytes ourselves is stable.
		const FString Path = GetDatabaseFilePath();
		FTCHARToUTF8 Utf8(*WriteToString());
		TArray64<uint8> Bytes;
		Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		if (!FFileHelper::SaveArrayToFile(Bytes, *Path))
		{
			// Loud and explicit: a read-only plugin directory must fail the save visibly, never
			// silently redirect the database somewhere else.
			UE_LOG(LogTemp, Error,
				TEXT("Mixtormat authoring database: FAILED to write %s -- the plugin directory "
					"may be read-only. Pending changes are still in memory; nothing was saved."),
				*Path);
			return false;
		}
		return true;
	}
}
