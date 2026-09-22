// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatLayerGroups.h"

#include "Misc/SecureHash.h"

DEFINE_LOG_CATEGORY_STATIC(LogMixtormatLayerGroups, Log, All);

namespace
{
	void AppendGuid(TArray<uint8>& Buffer, const FGuid& Id)
	{
		const uint32 Words[4] = { Id.A, Id.B, Id.C, Id.D };
		for (const uint32 Word : Words)
		{
			// Byte order fixed here rather than taken from the host, so an effective ID derived on
			// one platform matches the one derived on another for the same authored data.
			Buffer.Add(static_cast<uint8>(Word & 0xFF));
			Buffer.Add(static_cast<uint8>((Word >> 8) & 0xFF));
			Buffer.Add(static_cast<uint8>((Word >> 16) & 0xFF));
			Buffer.Add(static_cast<uint8>((Word >> 24) & 0xFF));
		}
	}
}

namespace MixtormatLayerGroups
{
	FGuid MakeEffectiveChildId(
		const FGuid& GroupId,
		const FGuid& GroupChildId,
		const FGuid& MemberLayerId)
	{
		TArray<uint8> Buffer;
		Buffer.Reserve(48);
		AppendGuid(Buffer, GroupId);
		AppendGuid(Buffer, GroupChildId);
		AppendGuid(Buffer, MemberLayerId);

		uint8 Digest[16] = {};
		FMD5 Hash;
		Hash.Update(Buffer.GetData(), Buffer.Num());
		Hash.Final(Digest);

		const auto Word = [&Digest](const int32 Offset)
		{
			return static_cast<uint32>(Digest[Offset])
				| (static_cast<uint32>(Digest[Offset + 1]) << 8)
				| (static_cast<uint32>(Digest[Offset + 2]) << 16)
				| (static_cast<uint32>(Digest[Offset + 3]) << 24);
		};
		FGuid Result(Word(0), Word(4), Word(8), Word(12));
		if (!Result.IsValid())
		{
			// All-zero is the one digest FGuid reads as no identity at all. Astronomically
			// unlikely, but a child with an invalid ID would drop out of every lookup silently.
			Result = FGuid(1, Word(4), Word(8), Word(12));
		}
		return Result;
	}

	const FMixtormatLayerGroup* FindGroup(
		const TArray<FMixtormatLayerGroup>& Groups,
		const FGuid& GroupId)
	{
		if (!GroupId.IsValid())
		{
			return nullptr;
		}
		return Groups.FindByPredicate(
			[&GroupId](const FMixtormatLayerGroup& Candidate)
			{
				return Candidate.GroupId == GroupId;
			});
	}

	FMixtormatLayerGroup* FindGroup(
		TArray<FMixtormatLayerGroup>& Groups,
		const FGuid& GroupId)
	{
		return const_cast<FMixtormatLayerGroup*>(
			FindGroup(static_cast<const TArray<FMixtormatLayerGroup>&>(Groups), GroupId));
	}

	bool GetGroupRange(
		const TArray<FMixtormatLayer>& Layers,
		const FGuid& GroupId,
		int32& OutFirstIndex,
		int32& OutLastIndex)
	{
		OutFirstIndex = INDEX_NONE;
		OutLastIndex = INDEX_NONE;
		if (!GroupId.IsValid())
		{
			return false;
		}
		for (int32 Index = 0; Index < Layers.Num(); ++Index)
		{
			if (Layers[Index].GroupId != GroupId)
			{
				continue;
			}
			if (OutFirstIndex == INDEX_NONE)
			{
				OutFirstIndex = Index;
			}
			OutLastIndex = Index;
		}
		return OutFirstIndex != INDEX_NONE;
	}

	void ValidateGroups(
		TArray<FMixtormatLayer>& Layers,
		TArray<FMixtormatLayerGroup>& Groups)
	{
		// Give every group an identity of its own before anything is matched against it, or two
		// groups sharing a GUID would swallow each other's members.
		TSet<FGuid> GroupIds;
		for (FMixtormatLayerGroup& Group : Groups)
		{
			if (!Group.GroupId.IsValid() || GroupIds.Contains(Group.GroupId))
			{
				Group.GroupId = FGuid::NewGuid();
			}
			GroupIds.Add(Group.GroupId);
		}

		// Membership naming a group that is not here is not membership.
		for (FMixtormatLayer& Layer : Layers)
		{
			if (Layer.GroupId.IsValid() && !GroupIds.Contains(Layer.GroupId))
			{
				Layer.GroupId.Invalidate();
			}
		}

		// Contiguity, without reordering. A run that has been split -- by an older build, a merge,
		// or hand-edited data -- keeps its first block and the strays become ungrouped. Moving
		// layers to close the gap would rewrite composition order to satisfy a UI invariant.
		for (const FGuid& GroupId : GroupIds)
		{
			int32 FirstIndex = INDEX_NONE;
			int32 RunEnd = INDEX_NONE;
			bool bBroken = false;
			for (int32 Index = 0; Index < Layers.Num(); ++Index)
			{
				if (Layers[Index].GroupId != GroupId)
				{
					continue;
				}
				if (FirstIndex == INDEX_NONE)
				{
					FirstIndex = Index;
					RunEnd = Index;
					continue;
				}
				if (Index == RunEnd + 1)
				{
					RunEnd = Index;
					continue;
				}
				Layers[Index].GroupId.Invalidate();
				bBroken = true;
			}
			if (bBroken)
			{
				UE_LOG(LogMixtormatLayerGroups, Warning,
					TEXT("Group %s had members outside its contiguous run; those layers were ungrouped. Layer order is unchanged."),
					*GroupId.ToString(EGuidFormats::DigitsWithHyphens));
			}
		}

		// A group with nothing in it has no position in the stack and nothing to broadcast to.
		for (int32 Index = Groups.Num() - 1; Index >= 0; --Index)
		{
			int32 FirstIndex = INDEX_NONE;
			int32 LastIndex = INDEX_NONE;
			if (!GetGroupRange(Layers, Groups[Index].GroupId, FirstIndex, LastIndex))
			{
				Groups.RemoveAt(Index);
			}
		}

		// Group children share the child-ID space with layer children, because a reference names a
		// child by GUID and resolves against whichever container holds it.
		TSet<FGuid> ChildIds;
		for (const FMixtormatLayer& Layer : Layers)
		{
			for (const FMixtormatLayerChild& Child : Layer.Children)
			{
				ChildIds.Add(Child.ChildId);
			}
		}
		for (FMixtormatLayerGroup& Group : Groups)
		{
			for (FMixtormatLayerChild& Child : Group.Children)
			{
				if (!Child.ChildId.IsValid() || ChildIds.Contains(Child.ChildId))
				{
					Child.ChildId = FGuid::NewGuid();
				}
				ChildIds.Add(Child.ChildId);
			}
		}
	}

	bool RequiresExpansion(
		const TArray<FMixtormatLayer>& Layers,
		const TArray<FMixtormatLayerGroup>& Groups)
	{
		if (Groups.Num() == 0)
		{
			return false;
		}
		for (const FMixtormatLayer& Layer : Layers)
		{
			if (!Layer.GroupId.IsValid())
			{
				continue;
			}
			const FMixtormatLayerGroup* Group = FindGroup(Groups, Layer.GroupId);
			if (Group && (Group->Children.Num() > 0 || !Group->bEnabled))
			{
				return true;
			}
		}
		return false;
	}

	void BuildEffectiveLayers(
		const TArray<FMixtormatLayer>& Layers,
		const TArray<FMixtormatLayerGroup>& Groups,
		TArray<FMixtormatLayer>& OutLayers)
	{
		OutLayers = Layers;
		if (Groups.Num() == 0)
		{
			return;
		}

		for (FMixtormatLayer& Layer : OutLayers)
		{
			const FMixtormatLayerGroup* Group = FindGroup(Groups, Layer.GroupId);
			if (!Group)
			{
				continue;
			}

			// The group's visibility gates the render copy only. The authored bEnabled is what the
			// eye icon shows and what comes back when the group is switched on again.
			Layer.bEnabled = Layer.bEnabled && Group->bEnabled;

			if (Group->Children.Num() == 0)
			{
				continue;
			}

			// Every shared child's identity inside this member, resolved up front: a child may
			// name a sibling that has not been cloned yet.
			TMap<FGuid, FGuid> ChildIdRemap;
			ChildIdRemap.Reserve(Group->Children.Num());
			for (const FMixtormatLayerChild& GroupChild : Group->Children)
			{
				ChildIdRemap.Add(
					GroupChild.ChildId,
					MakeEffectiveChildId(Group->GroupId, GroupChild.ChildId, Layer.LayerId));
			}

			// A GUID naming a sibling shared child follows the clone into this member; one naming
			// anything else is an authored address and stays exactly as written.
			const FGuid MemberLayerId = Layer.LayerId;
			const FGuid AuthoredGroupId = Group->GroupId;
			const auto RemapPair =
				[&ChildIdRemap, &MemberLayerId, &AuthoredGroupId](FGuid& OwnerLayerId, FGuid& ChildId)
			{
				// Only an authored address to this group becomes member-local. A layer address that
				// happens to name a matching child must stay external to this expansion.
				if (OwnerLayerId == AuthoredGroupId)
				{
					if (const FGuid* Effective = ChildIdRemap.Find(ChildId))
					{
						ChildId = *Effective;
						OwnerLayerId = MemberLayerId;
					}
				}
			};
			const auto RemapChildReferences = [&RemapPair](FMixtormatLayerChild& Child)
			{
				RemapPair(Child.SourceLayerId, Child.SourceChildId);
				RemapPair(Child.Mask.PublishedSourceLayerId, Child.Mask.PublishedSourceChildId);
				for (FMixtormatParameterBinding& Binding : Child.ParameterBindings)
				{
					RemapPair(Binding.Reference.Source.LayerId, Binding.Reference.Source.ChildId);
					RemapPair(Binding.Driver.SourceLayerId, Binding.Driver.SourceChildId);
				}
			};

			// A member's local child may deliberately read an authored group child. The compositor
			// only sees effective layers, so convert that stable authored address for this member.
			for (FMixtormatLayerChild& LocalChild : Layer.Children)
			{
				RemapChildReferences(LocalChild);
			}

			Layer.Children.Reserve(Layer.Children.Num() + Group->Children.Num());
			for (const FMixtormatLayerChild& GroupChild : Group->Children)
			{
				// Local children first, shared children after: the group treats what the layer
				// already does, rather than the layer treating what the group did.
				FMixtormatLayerChild& Clone = Layer.Children.Add_GetRef(GroupChild);
				Clone.ChildId = ChildIdRemap.FindChecked(GroupChild.ChildId);

				if (Clone.ScopeOwnerChildId.IsValid())
				{
					if (const FGuid* Effective = ChildIdRemap.Find(Clone.ScopeOwnerChildId))
					{
						Clone.ScopeOwnerChildId = *Effective;
					}
					else
					{
						// Scoped beneath something that is not in this container. The owner is not
						// here to gate, so the clone joins the member's ordinary child chain
						// rather than hanging off an address that resolves to nothing.
						Clone.ScopeOwnerChildId.Invalidate();
					}
				}

				RemapChildReferences(Clone);
			}
		}
	}
}
