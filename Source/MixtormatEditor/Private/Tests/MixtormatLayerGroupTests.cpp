// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "MixtormatLayerGroups.h"
#include "MixtormatMaterial.h"
#include "MixtormatParameterBinding.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// Two layers in one group, each with a local child, and one shared child on the group.
	struct FGroupFixture
	{
		TArray<FMixtormatLayer> Layers;
		TArray<FMixtormatLayerGroup> Groups;

		FGroupFixture()
		{
			FMixtormatLayerGroup& Group = Groups.AddDefaulted_GetRef();
			Group.DisplayName = FText::FromString(TEXT("Weathering"));

			FMixtormatLayerChild& Shared = Group.Children.AddDefaulted_GetRef();
			Shared.Type = EMixtormatLayerChildType::Effect;

			for (int32 Index = 0; Index < 2; ++Index)
			{
				FMixtormatLayer& Layer = Layers.AddDefaulted_GetRef();
				Layer.GroupId = Group.GroupId;
				FMixtormatLayerChild& Local = Layer.Children.AddDefaulted_GetRef();
				Local.Type = EMixtormatLayerChildType::Mask;
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatLayerGroupNoGroupsUnchangedTest,
	"Mixtormat.LayerGroups.NoGroupsExpandUnchanged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatLayerGroupNoGroupsUnchangedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TArray<FMixtormatLayer> Layers;
	Layers.AddDefaulted(3);
	Layers[1].Children.AddDefaulted();
	Layers[2].bEnabled = false;
	Layers[2].HeightReferenceLayerIndex = 0;

	const TArray<FMixtormatLayerGroup> Groups;
	TestFalse(TEXT("A document with no groups needs no expansion"),
		MixtormatLayerGroups::RequiresExpansion(Layers, Groups));

	TArray<FMixtormatLayer> Effective;
	MixtormatLayerGroups::BuildEffectiveLayers(Layers, Groups, Effective);

	TestEqual(TEXT("Layer count is preserved"), Effective.Num(), Layers.Num());
	const UScriptStruct* LayerStruct = FMixtormatLayer::StaticStruct();
	for (int32 Index = 0; Index < Layers.Num(); ++Index)
	{
		TestTrue(
			FString::Printf(TEXT("Layer %d is untouched"), Index),
			LayerStruct->CompareScriptStruct(&Effective[Index], &Layers[Index], 0));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatLayerGroupBroadcastTest,
	"Mixtormat.LayerGroups.SharedChildrenBroadcastToMembers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatLayerGroupBroadcastTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FGroupFixture Fixture;
	const FGuid SharedId = Fixture.Groups[0].Children[0].ChildId;
	const FGuid LocalIdA = Fixture.Layers[0].Children[0].ChildId;

	TestTrue(TEXT("A group with shared children needs expansion"),
		MixtormatLayerGroups::RequiresExpansion(Fixture.Layers, Fixture.Groups));

	TArray<FMixtormatLayer> Effective;
	MixtormatLayerGroups::BuildEffectiveLayers(Fixture.Layers, Fixture.Groups, Effective);

	TestEqual(TEXT("Layer count is preserved"), Effective.Num(), 2);
	TestEqual(TEXT("Member LayerId is preserved"),
		Effective[0].LayerId, Fixture.Layers[0].LayerId);

	TestEqual(TEXT("Member gains the shared child"), Effective[0].Children.Num(), 2);
	TestEqual(TEXT("Local children run first"), Effective[0].Children[0].ChildId, LocalIdA);
	TestEqual(TEXT("Shared child is appended"),
		Effective[0].Children[1].Type, EMixtormatLayerChildType::Effect);

	const FGuid EffectiveA = Effective[0].Children[1].ChildId;
	const FGuid EffectiveB = Effective[1].Children[1].ChildId;
	TestTrue(TEXT("Each member gets its own effective child ID"), EffectiveA != EffectiveB);
	TestTrue(TEXT("The effective ID is not the authored one"), EffectiveA != SharedId);
	TestTrue(TEXT("The effective ID is usable"), EffectiveA.IsValid());

	// The whole point of deriving rather than generating: the same authored data has to produce
	// the same IDs on the next composite, or anything holding one goes stale every frame.
	TArray<FMixtormatLayer> Again;
	MixtormatLayerGroups::BuildEffectiveLayers(Fixture.Layers, Fixture.Groups, Again);
	TestEqual(TEXT("Effective child IDs are stable across composites"),
		Again[0].Children[1].ChildId, EffectiveA);

	TestEqual(TEXT("The authored group child is not modified"),
		Fixture.Groups[0].Children[0].ChildId, SharedId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatLayerGroupScopeRemapTest,
	"Mixtormat.LayerGroups.SharedSubtreeRemapsPerMember",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatLayerGroupScopeRemapTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FGroupFixture Fixture;
	FMixtormatLayerGroup& Group = Fixture.Groups[0];
	const FGuid OwnerId = Group.Children[0].ChildId;

	// A Blur scoped to the shared effect, plus a reference from the Blur back to its owner.
	FMixtormatLayerChild& Scoped = Group.Children.AddDefaulted_GetRef();
	Scoped.Type = EMixtormatLayerChildType::Blur;
	Scoped.ScopeOwnerChildId = OwnerId;
	FMixtormatParameterBinding& Binding = Scoped.ParameterBindings.AddDefaulted_GetRef();
	Binding.Reference.bEnabled = true;
	Binding.Reference.Source.ChildId = OwnerId;
	Binding.Driver.SourceChildId = OwnerId;

	TArray<FMixtormatLayer> Effective;
	MixtormatLayerGroups::BuildEffectiveLayers(Fixture.Layers, Fixture.Groups, Effective);

	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FMixtormatLayer& Member = Effective[Index];
		TestEqual(FString::Printf(TEXT("Member %d has local + two shared children"), Index),
			Member.Children.Num(), 3);

		const FGuid EffectiveOwner = Member.Children[1].ChildId;
		const FMixtormatLayerChild& EffectiveScoped = Member.Children[2];

		TestEqual(FString::Printf(TEXT("Member %d scope owner follows the clone"), Index),
			EffectiveScoped.ScopeOwnerChildId, EffectiveOwner);
		TestEqual(FString::Printf(TEXT("Member %d reference follows the clone"), Index),
			EffectiveScoped.ParameterBindings[0].Reference.Source.ChildId, EffectiveOwner);
		TestEqual(FString::Printf(TEXT("Member %d reference is readdressed to this layer"), Index),
			EffectiveScoped.ParameterBindings[0].Reference.Source.LayerId, Member.LayerId);
		TestEqual(FString::Printf(TEXT("Member %d driver follows the clone"), Index),
			EffectiveScoped.ParameterBindings[0].Driver.SourceChildId, EffectiveOwner);
	}

	TestTrue(TEXT("The two members' subtrees do not collide"),
		Effective[0].Children[2].ChildId != Effective[1].Children[2].ChildId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatLayerGroupExternalAddressTest,
	"Mixtormat.LayerGroups.ExternalAddressesSurviveExpansion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatLayerGroupExternalAddressTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FGroupFixture Fixture;
	// An ungrouped layer below, whose child the shared effect reads.
	FMixtormatLayer& Outside = Fixture.Layers.InsertDefaulted_GetRef(0);
	FMixtormatLayerChild& OutsideChild = Outside.Children.AddDefaulted_GetRef();
	OutsideChild.Type = EMixtormatLayerChildType::Mask;
	const FGuid OutsideLayerId = Outside.LayerId;
	const FGuid OutsideChildId = OutsideChild.ChildId;

	FMixtormatLayerChild& Shared = Fixture.Groups[0].Children[0];
	Shared.Mask.PublishedSourceLayerId = OutsideLayerId;
	Shared.Mask.PublishedSourceChildId = OutsideChildId;

	TArray<FMixtormatLayer> Effective;
	MixtormatLayerGroups::BuildEffectiveLayers(Fixture.Layers, Fixture.Groups, Effective);

	TestEqual(TEXT("The ungrouped layer is untouched"), Effective[0].Children.Num(), 1);
	const FMixtormatLayerChild& Clone = Effective[1].Children[1];
	TestEqual(TEXT("An address outside the group keeps its layer"),
		Clone.Mask.PublishedSourceLayerId, OutsideLayerId);
	TestEqual(TEXT("An address outside the group keeps its child"),
		Clone.Mask.PublishedSourceChildId, OutsideChildId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatLayerGroupEnableTest,
	"Mixtormat.LayerGroups.DisableGatesMembersWithoutEditingThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatLayerGroupEnableTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FGroupFixture Fixture;
	Fixture.Layers[1].bEnabled = false;
	Fixture.Groups[0].bEnabled = false;

	TArray<FMixtormatLayer> Effective;
	MixtormatLayerGroups::BuildEffectiveLayers(Fixture.Layers, Fixture.Groups, Effective);

	TestFalse(TEXT("A disabled group disables an enabled member"), Effective[0].bEnabled);
	TestFalse(TEXT("A disabled group leaves a disabled member disabled"), Effective[1].bEnabled);
	TestTrue(TEXT("The authored member keeps its own visibility"), Fixture.Layers[0].bEnabled);

	// Re-enabling the group has to give each member back the state the user gave it.
	Fixture.Groups[0].bEnabled = true;
	MixtormatLayerGroups::BuildEffectiveLayers(Fixture.Layers, Fixture.Groups, Effective);
	TestTrue(TEXT("Re-enabling restores the visible member"), Effective[0].bEnabled);
	TestFalse(TEXT("Re-enabling does not reveal the hidden member"), Effective[1].bEnabled);

	// An empty, enabled group changes nothing at all.
	Fixture.Groups[0].Children.Reset();
	TestFalse(TEXT("An empty enabled group needs no expansion"),
		MixtormatLayerGroups::RequiresExpansion(Fixture.Layers, Fixture.Groups));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatLayerGroupValidationTest,
	"Mixtormat.LayerGroups.ValidationRepairsWithoutReordering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatLayerGroupValidationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TArray<FMixtormatLayer> Layers;
	Layers.AddDefaulted(4);
	TArray<FGuid> OriginalOrder;
	for (const FMixtormatLayer& Layer : Layers)
	{
		OriginalOrder.Add(Layer.LayerId);
	}

	// Both groups up front: a later Add would move the array out from under any reference held
	// into it. The second one is never assigned a member, so validation should drop it.
	TArray<FMixtormatLayerGroup> Groups;
	Groups.AddDefaulted(2);
	Groups[1].Children.AddDefaulted();

	// Members 0 and 1 are a run; member 3 is stranded behind an ungrouped layer.
	Layers[0].GroupId = Groups[0].GroupId;
	Layers[1].GroupId = Groups[0].GroupId;
	Layers[3].GroupId = Groups[0].GroupId;
	// A layer pointing at a group that does not exist.
	Layers[2].GroupId = FGuid::NewGuid();

	// A group child colliding with a layer child.
	const FGuid CollidingId = Layers[0].Children.AddDefaulted_GetRef().ChildId;
	Groups[0].Children.AddDefaulted_GetRef().ChildId = CollidingId;

	MixtormatLayerGroups::ValidateGroups(Layers, Groups);

	for (int32 Index = 0; Index < Layers.Num(); ++Index)
	{
		TestEqual(FString::Printf(TEXT("Layer %d did not move"), Index),
			Layers[Index].LayerId, OriginalOrder[Index]);
	}
	TestEqual(TEXT("The first contiguous run keeps its membership"),
		Layers[0].GroupId, Layers[1].GroupId);
	TestTrue(TEXT("The first run is still grouped"), Layers[0].GroupId.IsValid());
	TestFalse(TEXT("A dangling group reference is cleared"), Layers[2].GroupId.IsValid());
	TestFalse(TEXT("A stranded member is ungrouped"), Layers[3].GroupId.IsValid());
	TestEqual(TEXT("The empty group is removed"), Groups.Num(), 1);
	TestTrue(TEXT("Colliding group child IDs are regenerated"),
		Groups[0].Children[0].ChildId != Layers[0].Children[0].ChildId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatLayerGroupSharedMaskTest,
	"Mixtormat.LayerGroups.SharedMaskReachesEveryMember",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatLayerGroupSharedMaskTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FGroupFixture Fixture;
	// A mask on the group, on top of each member's own local mask.
	FMixtormatLayerChild& Shared = Fixture.Groups[0].Children[0];
	Shared.Type = EMixtormatLayerChildType::Mask;
	// Multiply, not Replace: Replace discards the chain it lands on, so a shared mask set to it
	// would wipe each member's authored mask instead of restricting it.
	Shared.Mask.BlendMode = EMixtormatMaskBlendMode::Multiply;

	TArray<FMixtormatLayer> Effective;
	MixtormatLayerGroups::BuildEffectiveLayers(Fixture.Layers, Fixture.Groups, Effective);

	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FMixtormatLayer& Member = Effective[Index];
		TestEqual(FString::Printf(TEXT("Member %d keeps its local mask and gains the shared one"), Index),
			Member.Children.Num(), 2);
		TestEqual(FString::Printf(TEXT("Member %d local mask runs first"), Index),
			Member.Children[0].ChildId, Fixture.Layers[Index].Children[0].ChildId);
		TestEqual(FString::Printf(TEXT("Member %d shared child is a mask"), Index),
			Member.Children[1].Type, EMixtormatLayerChildType::Mask);
		TestEqual(FString::Printf(TEXT("Member %d shared mask does not replace the chain"), Index),
			Member.Children[1].Mask.BlendMode, EMixtormatMaskBlendMode::Multiply);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatLayerGroupChildEditTest,
	"Mixtormat.LayerGroups.EditingASharedChildReachesEveryMember",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatLayerGroupChildEditTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	// What the inspector does: write one value on the group's authored child, then compose.
	FGroupFixture Fixture;
	FMixtormatLayerChild& Shared = Fixture.Groups[0].Children[0];
	Shared.Type = EMixtormatLayerChildType::Mask;
	Shared.Mask.TilingX = 7;

	TArray<FMixtormatLayer> Effective;
	MixtormatLayerGroups::BuildEffectiveLayers(Fixture.Layers, Fixture.Groups, Effective);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		TestEqual(FString::Printf(TEXT("Member %d sees the edited value"), Index),
			Effective[Index].Children[1].Mask.TilingX, 7);
	}

	// And again after a second edit, because the members hold copies -- a stale copy would keep
	// the first value and the panel would look like it had stopped working.
	Shared.Mask.TilingX = 3;
	MixtormatLayerGroups::BuildEffectiveLayers(Fixture.Layers, Fixture.Groups, Effective);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		TestEqual(FString::Printf(TEXT("Member %d follows a later edit"), Index),
			Effective[Index].Children[1].Mask.TilingX, 3);
	}

	TestEqual(TEXT("The members' own masks are untouched"),
		Effective[0].Children[0].ChildId, Fixture.Layers[0].Children[0].ChildId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatGroupInstanceIdentityRemapTest,
	"Mixtormat.LayerGroups.IdentityRemap.GroupChildInstance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatGroupInstanceIdentityRemapTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FGroupFixture Fixture;
	FMixtormatLayerGroup& Group = Fixture.Groups[0];
	FMixtormatLayerChild& Instance = Group.Children.AddDefaulted_GetRef();
	Instance.SourceLayerId = Group.GroupId;
	Instance.SourceChildId = Group.Children[0].ChildId;

	MixtormatParameterBinding::RegenerateLayerIdentities(Fixture.Layers, Fixture.Groups);
	TestEqual(TEXT("Group instance owner follows regenerated group"), Instance.SourceLayerId, Group.GroupId);
	TestEqual(TEXT("Group instance child follows regenerated child"), Instance.SourceChildId, Group.Children[0].ChildId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatGroupPublishedOutputIdentityRemapTest,
	"Mixtormat.LayerGroups.IdentityRemap.GroupPublishedOutput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatGroupPublishedOutputIdentityRemapTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FGroupFixture Fixture;
	FMixtormatLayerGroup& Group = Fixture.Groups[0];
	FMixtormatLayerChild& Mask = Group.Children.AddDefaulted_GetRef();
	Mask.Type = EMixtormatLayerChildType::Mask;
	Mask.Mask.PublishedSourceLayerId = Group.GroupId;
	Mask.Mask.PublishedSourceChildId = Group.Children[0].ChildId;
	Mask.Mask.PublishedSourceOutput = TEXT("Wear");

	MixtormatParameterBinding::RegenerateLayerIdentities(Fixture.Layers, Fixture.Groups);
	TArray<FMixtormatLayer> Effective;
	MixtormatLayerGroups::BuildEffectiveLayers(Fixture.Layers, Fixture.Groups, Effective);
	TestEqual(TEXT("Published group owner becomes this effective member"),
		Effective[0].Children[2].Mask.PublishedSourceLayerId, Effective[0].LayerId);
	TestEqual(TEXT("Published group child becomes this effective child"),
		Effective[0].Children[2].Mask.PublishedSourceChildId, Effective[0].Children[1].ChildId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatLayerToGroupIdentityRemapTest,
	"Mixtormat.LayerGroups.IdentityRemap.LayerToGroup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatLayerToGroupIdentityRemapTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FGroupFixture Fixture;
	FMixtormatLayerChild& Mask = Fixture.Layers[0].Children.AddDefaulted_GetRef();
	Mask.Mask.PublishedSourceLayerId = Fixture.Groups[0].GroupId;
	Mask.Mask.PublishedSourceChildId = Fixture.Groups[0].Children[0].ChildId;

	MixtormatParameterBinding::RegenerateLayerIdentities(Fixture.Layers, Fixture.Groups);
	TestEqual(TEXT("Layer published owner follows regenerated group"), Mask.Mask.PublishedSourceLayerId, Fixture.Groups[0].GroupId);
	TestEqual(TEXT("Layer published child follows regenerated group child"), Mask.Mask.PublishedSourceChildId, Fixture.Groups[0].Children[0].ChildId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatGroupToLayerIdentityRemapTest,
	"Mixtormat.LayerGroups.IdentityRemap.GroupToLayer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatGroupToLayerIdentityRemapTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FGroupFixture Fixture;
	FMixtormatLayerChild& Mask = Fixture.Groups[0].Children.AddDefaulted_GetRef();
	Mask.Mask.PublishedSourceLayerId = Fixture.Layers[0].LayerId;
	Mask.Mask.PublishedSourceChildId = Fixture.Layers[0].Children[0].ChildId;

	MixtormatParameterBinding::RegenerateLayerIdentities(Fixture.Layers, Fixture.Groups);
	TestEqual(TEXT("Group published owner follows regenerated layer"), Mask.Mask.PublishedSourceLayerId, Fixture.Layers[0].LayerId);
	TestEqual(TEXT("Group published child follows regenerated layer child"), Mask.Mask.PublishedSourceChildId, Fixture.Layers[0].Children[0].ChildId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatEffectiveIdentityDeterminismAfterRemapTest,
	"Mixtormat.LayerGroups.IdentityRemap.EffectiveIdsStayDeterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatEffectiveIdentityDeterminismAfterRemapTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FGroupFixture Fixture;
	MixtormatParameterBinding::RegenerateLayerIdentities(Fixture.Layers, Fixture.Groups);
	TArray<FMixtormatLayer> First;
	TArray<FMixtormatLayer> Again;
	MixtormatLayerGroups::BuildEffectiveLayers(Fixture.Layers, Fixture.Groups, First);
	MixtormatLayerGroups::BuildEffectiveLayers(Fixture.Layers, Fixture.Groups, Again);
	TestEqual(TEXT("Regenerated authored data yields stable effective IDs"),
		First[0].Children[1].ChildId, Again[0].Children[1].ChildId);
	return true;
}

#endif
