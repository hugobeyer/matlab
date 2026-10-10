// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatLayerTypes.h"

// What kind of container OwnerId names. A child's own FGuid fields (SourceLayerId,
// Mask.PublishedSourceLayerId, ...) already double as either a LayerId or a GroupId depending on
// which array the id is found in -- FMixtormatBindingScope documents the same convention on the
// runtime side. This enum exists only where the editor has not yet resolved which one it is, so a
// clipboard entry or a menu callback does not have to search both arrays to find out.
enum class EMixtormatChildOwnerType : uint8
{
	Layer,
	Group,
	// A Sources shelf entry: OwnerId is the entry's SourceId, the child is Entry.Child. A source
	// is one child in its own entry, not a child container, so ResolveContainer returns null for
	// this owner and ResolveChildAt resolves the entry's single child directly.
	Source
};

// A child's address, stable across a reorder within its own container (it is never an index).
// LayerIndex/ChildIndex pairs, which most of SMixtormat still uses, only ever named a layer child;
// this is their generalization to "layer or group". A layer producer uses OwnerId with an invalid
// ChildId for scalar Copy Output and root flow creation; IsValid still means a concrete child.
struct FMixtormatChildAddress
{
	EMixtormatChildOwnerType OwnerType = EMixtormatChildOwnerType::Layer;
	FGuid OwnerId;
	FGuid ChildId;

	bool IsValid() const
	{
		return OwnerId.IsValid() && ChildId.IsValid();
	}

	friend uint32 GetTypeHash(const FMixtormatChildAddress& Address)
	{
		// FGuid's hidden-friend hash requires argument-dependent lookup.
		return HashCombine(HashCombine(::GetTypeHash(static_cast<uint8>(Address.OwnerType)),
			GetTypeHash(Address.OwnerId)), GetTypeHash(Address.ChildId));
	}

	bool operator==(const FMixtormatChildAddress& Other) const
	{
		return OwnerType == Other.OwnerType && OwnerId == Other.OwnerId && ChildId == Other.ChildId;
	}
};

// A display row: one child, plus where it sits in the indented tree. Display only --
// indices into a row array never address an authored child array, which is why the
// authored position travels alongside as its own field rather than being implied.
//
// A row carries nothing about structural endpoints. Those went with the Height Push
// and Structural Warp modules: a Behavior names its generator through ScopeOwnerChildId
// and its fields through typed sockets, so the only nesting a row has is scope.
struct FMixtormatProjectedChildRow
{
	FMixtormatChildAddress Address;
	int32 AuthoredChildIndex = INDEX_NONE;
	int32 VisualParentRowIndex = INDEX_NONE;
	int32 AuthoredScopeDepth = 0;
	// Exclusive authored boundary; INDEX_NONE means no safe contiguous subtree boundary.
	int32 AuthoredSubtreeEnd = INDEX_NONE;
};

// What the clipboard is holding and what a paste should do with it.
enum class EMixtormatChildClipboardMode : uint8
{
	// A duplicate: Payload's data, severed from Source. Paste gives it a fresh identity.
	Copy,
	// A live instance: Paste points the new child's SourceLayerId/SourceChildId at Source and
	// takes no payload of its own until the next resolve mirrors it in.
	Instance,
	// A published-source mask built from one of Source's copyable outputs (Copy Output, formerly
	// CopyInstanceMaskFromWear/Breakup/PatternGap). Payload is the ready-to-insert Mask child;
	// PublishedOutput records which output it was built from, for status text and re-derivation.
	PublishedOutput
};

// The clipboard's one piece of state. Replaces the former quartet of ChildClipboard /
// ChildClipboardSourceLayerId / ChildClipboardSourceChildId / bChildClipboardIsInstance -- Mode
// says what Copy as Instance used to mean via that bool, and Source is an address rather than a
// bare LayerId so it can name a group child as well as a layer child.
struct FMixtormatChildClipboard
{
	EMixtormatChildClipboardMode Mode = EMixtormatChildClipboardMode::Copy;
	FMixtormatLayerChild Payload;
	// The child that was copied. For Mode::Copy of an instance, this is the instance's own source
	// (copying an instance as a plain copy still severs identity, but a copy of an instance's
	// current values has no better address to report than the source it was mirroring). For
	// Mode::Instance, this is what the pasted instance will point at.
	FMixtormatChildAddress Source;
	// Set only for Mode::PublishedOutput: which of Source's outputs this is. A layer source has no ChildId.
	FName PublishedOutput;
};
