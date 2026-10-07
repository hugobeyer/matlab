// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatParameterTypes.h"
#include "MixtormatLayerTypes.h"
#include "UObject/Class.h"
#include "MixtormatParameterDefinition.h"

// The plugin-owned authoring database: persistent overrides of an authoring setup --
// display label, default/reset value, UI range and snap -- for numeric properties, plus stable
// enum defaults for reflected enum properties. Everything else about a parameter (identity, type,
// default, family, UI ergonomics) is discovered from the UPROPERTY itself; only the shader contract
// lives in the runtime table (MixtormatParameterDefinition.h).
//
// Storage is a JSON file inside the plugin (Config/MixtormatParameterAuthoring.json),
// source-controlled, shipped with Mixtormat, reloaded every editor session and re-editable in
// future versions. Nothing here is ever written into a material asset: the database changes
// what NEW instances and Reset produce, never what an already-authored material contains.
//
// Resolution order for every authoring lookup:
//
//     unsaved developer edit  ->  shipped plugin database  ->  compiled C++ fallback
//
// Hard runtime safety is untouched by all of it: numeric sanitizers and the shader still own
// what is legal. Numeric authoring can widen a slider; it can never widen a Hard bound.
struct FMixtormatParameterAuthoringEntry
{
	// Display-only. Empty means "no override" -- the Inspector's LOCTEXT label stays. Never
	// touches the parameter FName, the binding address, serialization or shader uniforms.
	FString Label;

	// Stored parameter units (the units the member itself holds, pre-ValueScale).
	TOptional<float> Default;
	// Stable reflected enum entry name; never stored as its numeric value.
	TOptional<FName> DefaultEnum;

	// Drag-range ergonomics. Never clamps: typed values still pass through, and these never
	// become shader clamps or hard bounds.
	TOptional<float> UiMin;
	TOptional<float> UiMax;
	TOptional<float> Snap;

	// Back-end clamp, enforced by the editor only: a drag and a typed value both stop here, and
	// a slider's range never grows past it. Never a shader or gather clamp. Unset = unlimited
	// (falls back to the runtime contract's HardMin/HardMax when that has one).
	TOptional<float> ClampMin;
	TOptional<float> ClampMax;

	bool IsEmpty() const
	{
		return Label.IsEmpty() && !Default.IsSet() && !DefaultEnum.IsSet()
			&& !UiMin.IsSet() && !UiMax.IsSet() && !Snap.IsSet()
			&& !ClampMin.IsSet() && !ClampMax.IsSet();
	}
};

namespace MixtormatParameterAuthoring
{
	// True only for definitions whose policy opted them in.
	bool IsPersistentlyEditable(const FMixtormatParameterDefinitionKey& Key);
	const UEnum* ResolveParameterEnum(const FMixtormatParameterDefinitionKey& Key);
	FName ResolveCompiledEnumDefault(const FMixtormatParameterDefinitionKey& Key);
	FName ResolveAuthoringEnumDefault(const FMixtormatParameterDefinitionKey& Key);

	// Shipped database (loaded from the plugin JSON at first use).
	const FMixtormatParameterAuthoringEntry* TryGetShipped(const FMixtormatParameterDefinitionKey& Key);
	bool HasShippedAuthoring(const FMixtormatParameterDefinitionKey& Key);

	// Unsaved developer edits (editor session only, discarded unless saved).
	const FMixtormatParameterAuthoringEntry* TryGetPending(const FMixtormatParameterDefinitionKey& Key);
	bool HasPendingAuthoring(const FMixtormatParameterDefinitionKey& Key);
	bool HasAnyPendingAuthoring();
	void SetPendingAuthoring(const FMixtormatParameterDefinitionKey& Key, const FMixtormatParameterAuthoringEntry& Entry);
	void RevertPendingAuthoring(const FMixtormatParameterDefinitionKey& Key);

	// Merges every pending entry into the shipped database and writes the plugin JSON.
	// Returns false (with no partial write) if serialization fails.
	bool SavePendingToPluginDefaults();

	// Drops the shipped entry (and any pending edit) for one parameter and writes the plugin
	// JSON. The parameter falls back to its compiled definition everywhere.
	bool RestoreShippedAuthoring(const FMixtormatParameterDefinitionKey& Key);

	// Resolution chain: pending -> shipped -> compiled definition -> Fallback.
	// Default/UiBound/Snap Fallbacks are in stored units; Label's fallback is the literal FText.
	float ResolveAuthoringDefault(const FMixtormatParameterDefinitionKey& Key, float FallbackStored);
	float ResolveAuthoringUiBound(const FMixtormatParameterDefinitionKey& Key, float Fallback, bool bMax);
	float ResolveAuthoringSnap(const FMixtormatParameterDefinitionKey& Key, float Fallback);
	// Editor clamp: pending -> shipped -> runtime contract Hard bound -> unset (unlimited).
	TOptional<float> ResolveAuthoringClamp(const FMixtormatParameterDefinitionKey& Key, bool bMax);
	FText ResolveAuthoringLabel(const FMixtormatParameterDefinitionKey& Key, const FText& Fallback);

	// Creation-time defaults for the active child payload; duplication and instance resolve
	// never call this, so existing authored values remain untouched.
	void ApplyAuthoringDefaults(FMixtormatLayerChild& Child);


	// Database serialization. LoadFromString replaces the entire shipped database and is the
	// corruption boundary: malformed input returns false and leaves the database empty rather
	// than half-loaded. WriteToString serializes the current shipped database.
	bool LoadFromString(const FString& Json);
	FString WriteToString();

	// Disk path: <PluginDir>/Config/MixtormatParameterAuthoring.json.
	FString GetDatabaseFilePath();
	bool LoadFromDisk();
	bool SaveToDisk();
}
