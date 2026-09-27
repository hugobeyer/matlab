// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UObject;
class UStruct;
class FProperty;

// Content hashes for compose caching.
//
// A hash says "these pixels would come out the same", so it walks everything a pass could read:
// every reflected field of the layer (after direct parameter references are applied), and every
// asset those fields point at. Mixtormat's own assets (surfaces, masks, source compositions) are
// walked by reflection too, so editing one invalidates what reads it. Engine assets are hashed by
// identity plus a change stamp -- a texture by its resource and lighting GUID, which a reimport or
// an edit replaces.
//
// Session-only values (object addresses, FName indices) are hashed on purpose: nothing here is
// ever persisted, and a stale entry can only miss, never match wrongly across sessions.
//
// Display text is skipped -- renaming a layer must not recomposite it.
namespace MixtormatComposeHash
{
	class FHasher
	{
	public:
		void Bytes(const void* Data, int32 Size);
		void Value(uint64 V);
		void Struct(const UStruct* Type, const void* Container);
		void Object(const UObject* Object);
		uint64 Get() const { return Hash; }

		// Top-level fields of the next Struct() call to leave out (e.g. a layer's Children when
		// hashing only what the layer's own maps depend on).
		TArray<FName> SkipTopLevel;

	private:
		void Property(const FProperty* Property, const void* ValuePtr);

		int32 Depth = 0;

		uint64 Hash = 0x9E3779B97F4A7C15ull;
		TSet<const UObject*> Visited;
	};

	uint64 Combine(uint64 A, uint64 B);
}
