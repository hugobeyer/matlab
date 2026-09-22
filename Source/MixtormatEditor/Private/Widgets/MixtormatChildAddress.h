// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MixtormatMaterial.h"

// What kind of container OwnerId names. A child's own FGuid fields (SourceLayerId,
// Mask.PublishedSourceLayerId, ...) already double as either a LayerId or a GroupId depending on
// which array the id is found in -- FMixtormatBindingScope documents the same convention on the
// runtime side. This enum exists only where the editor has not yet resolved which one it is, so a
// clipboard entry or a menu callback does not have to search both arrays to find out.
enum class EMixtormatChildOwnerType : uint8
{
	Layer,
	Group
};

// A child's address, stable across a reorder within its own container (it is never an index).
// LayerIndex/ChildIndex pairs, which most of SMixtormat still uses, only ever named a layer child;
// this is their generalization to "layer or group".
struct FMixtormatChildAddress
{
	EMixtormatChildOwnerType OwnerType = EMixtormatChildOwnerType::Layer;
	FGuid OwnerId;
	FGuid ChildId;

	bool IsValid() const
	{
		return OwnerId.IsValid() && ChildId.IsValid();
	}

	bool operator==(const FMixtormatChildAddress& Other) const
	{
		return OwnerType == Other.OwnerType && OwnerId == Other.OwnerId && ChildId == Other.ChildId;
	}
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
	// Set only for Mode::PublishedOutput: which of Source's outputs this is.
	FName PublishedOutput;
};
