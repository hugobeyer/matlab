// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Compositing/MixtormatComposeHash.h"

#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Hash/CityHash.h"
#include "TextureResource.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/UnrealType.h"

namespace MixtormatComposeHash
{
	uint64 Combine(const uint64 A, const uint64 B)
	{
		const uint64 Pair[2] = {A, B};
		return CityHash64(reinterpret_cast<const char*>(Pair), sizeof(Pair));
	}

	void FHasher::Bytes(const void* Data, const int32 Size)
	{
		if (Size > 0)
		{
			Hash = CityHash64WithSeed(static_cast<const char*>(Data), Size, Hash);
		}
	}

	void FHasher::Value(const uint64 V)
	{
		Bytes(&V, sizeof(V));
	}

	void FHasher::Struct(const UStruct* Type, const void* Container)
	{
		if (!Type || !Container)
		{
			Value(0);
			return;
		}
		const bool bTopLevel = Depth == 0;
		++Depth;
		for (TFieldIterator<FProperty> It(Type); It; ++It)
		{
			if (bTopLevel && SkipTopLevel.Contains(It->GetFName()))
			{
				continue;
			}
			for (int32 Index = 0; Index < It->ArrayDim; ++Index)
			{
				Property(*It, It->ContainerPtrToValuePtr<void>(Container, Index));
			}
		}
		--Depth;
	}

	void FHasher::Object(const UObject* Obj)
	{
		Value(reinterpret_cast<UPTRINT>(Obj));
		if (!Obj)
		{
			return;
		}
		if (const UTexture* Texture = Cast<UTexture>(Obj))
		{
			// The resource is replaced when the texture is rebuilt; the lighting GUID changes on
			// every source edit or reimport. Either one moving is a different image.
			//
			// Streaming changes the image without either: the RHI texture is reallocated as mips
			// arrive, so a composite made from a half-streamed texture must not be kept once the
			// full mip chain is resident.
			const FTextureResource* Resource = Texture->GetResource();
			Value(reinterpret_cast<UPTRINT>(Resource));
			Value(Resource ? reinterpret_cast<UPTRINT>(Resource->TextureRHI.GetReference()) : 0);
			if (const UTexture2D* Texture2D = Cast<UTexture2D>(Texture))
			{
				Value(static_cast<uint64>(Texture2D->GetNumResidentMips()));
			}
			const FGuid Guid = Texture->GetLightingGuid();
			Bytes(&Guid, sizeof(Guid));
			return;
		}
		// Only Mixtormat's own data is walked. Materials, meshes and engine assets are identity.
		const UPackage* ScriptPackage = Obj->GetClass()->GetOutermost();
		if (!ScriptPackage || !ScriptPackage->GetName().StartsWith(TEXT("/Script/Mixtormat")))
		{
			return;
		}
		bool bAlreadyVisited = false;
		Visited.Add(Obj, &bAlreadyVisited);
		if (bAlreadyVisited)
		{
			return;
		}
		Struct(Obj->GetClass(), Obj);
	}

	void FHasher::Property(const FProperty* Prop, const void* ValuePtr)
	{
		if (const FBoolProperty* Bool = CastField<FBoolProperty>(Prop))
		{
			Value(Bool->GetPropertyValue(ValuePtr) ? 1u : 0u);
		}
		else if (CastField<FNumericProperty>(Prop) || CastField<FEnumProperty>(Prop))
		{
			Bytes(ValuePtr, Prop->GetElementSize());
		}
		else if (const FStrProperty* Str = CastField<FStrProperty>(Prop))
		{
			const FString& S = Str->GetPropertyValue(ValuePtr);
			Bytes(*S, S.Len() * sizeof(TCHAR));
			Value(S.Len());
		}
		else if (CastField<FNameProperty>(Prop))
		{
			Bytes(ValuePtr, sizeof(FName));
		}
		else if (CastField<FTextProperty>(Prop))
		{
			// Display only.
		}
		else if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			Struct(StructProp->Struct, ValuePtr);
		}
		else if (const FArrayProperty* Array = CastField<FArrayProperty>(Prop))
		{
			FScriptArrayHelper Helper(Array, ValuePtr);
			Value(Helper.Num());
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
			{
				Property(Array->Inner, Helper.GetRawPtr(Index));
			}
		}
		else if (const FSetProperty* Set = CastField<FSetProperty>(Prop))
		{
			FScriptSetHelper Helper(Set, ValuePtr);
			Value(Helper.Num());
			TArray<uint64> ElementHashes;
			ElementHashes.Reserve(Helper.Num());
			TSet<const UObject*> ContainerVisited = Visited;
			for (FScriptSetHelper::FIterator It(Helper); It; ++It)
			{
				// Preserve depth/skips and ancestor visits, but isolate sibling traversal state.
				FHasher ElementHasher = *this;
				ElementHasher.Hash = FHasher().Hash;
				ElementHasher.Property(Set->ElementProp, Helper.GetElementPtr(It));
				ElementHashes.Add(ElementHasher.Get());
				ContainerVisited.Append(ElementHasher.Visited);
			}
			Visited = MoveTemp(ContainerVisited);
			ElementHashes.Sort();
			for (const uint64 ElementHash : ElementHashes)
			{
				Value(ElementHash);
			}
		}
		else if (const FMapProperty* Map = CastField<FMapProperty>(Prop))
		{
			FScriptMapHelper Helper(Map, ValuePtr);
			Value(Helper.Num());
			TArray<uint64> EntryHashes;
			EntryHashes.Reserve(Helper.Num());
			TSet<const UObject*> ContainerVisited = Visited;
			for (FScriptMapHelper::FIterator It(Helper); It; ++It)
			{
				// Key and value share one traversal; other entries cannot affect it.
				FHasher EntryHasher = *this;
				EntryHasher.Hash = FHasher().Hash;
				EntryHasher.Property(Map->KeyProp, Helper.GetKeyPtr(It));
				EntryHasher.Property(Map->ValueProp, Helper.GetValuePtr(It));
				EntryHashes.Add(EntryHasher.Get());
				ContainerVisited.Append(EntryHasher.Visited);
			}
			Visited = MoveTemp(ContainerVisited);
			EntryHashes.Sort();
			for (const uint64 EntryHash : EntryHashes)
			{
				Value(EntryHash);
			}
		}
		else if (CastField<FSoftObjectProperty>(Prop))
		{
			// Get(), never a load: an asset that is not in memory cannot have been sampled.
			const FSoftObjectPtr& Soft = *static_cast<const FSoftObjectPtr*>(ValuePtr);
			const FString Path = Soft.ToSoftObjectPath().ToString();
			Bytes(*Path, Path.Len() * sizeof(TCHAR));
			Object(Soft.Get());
		}
		else if (const FObjectPropertyBase* ObjectProp = CastField<FObjectPropertyBase>(Prop))
		{
			if (CastField<FObjectProperty>(Prop))
			{
				Object(ObjectProp->GetObjectPropertyValue(ValuePtr));
			}
		}
		// Delegates, interfaces and anything else carry no pixels.
	}
}
