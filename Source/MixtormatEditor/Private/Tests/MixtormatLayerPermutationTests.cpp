// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "MixtormatLayerGroups.h"
#include "MixtormatMaterial.h"
#include "Widgets/SMixtormatInternal.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// Named layers, so a reordering assertion reads as an order rather than as a list of GUIDs.
	TArray<FMixtormatLayer> MakeNamedLayers(const int32 Count)
	{
		TArray<FMixtormatLayer> Layers;
		Layers.AddDefaulted(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Layers[Index].DisplayName = FText::FromString(FString::Printf(TEXT("L%d"), Index));
		}
		return Layers;
	}

	FString OrderOf(const TArray<FMixtormatLayer>& Layers)
	{
		TArray<FString> Names;
		for (const FMixtormatLayer& Layer : Layers)
		{
			Names.Add(Layer.DisplayName.ToString());
		}
		return FString::Join(Names, TEXT(","));
	}

	// What CreateGroupFromSelection builds: the selection gathered into one block ending where its
	// topmost member already sat.
	TArray<int32> GatherOrder(const int32 LayerCount, const TArray<int32>& Selected)
	{
		const int32 TopSelected = Selected.Last();
		TArray<int32> Others;
		int32 InsertAt = 0;
		for (int32 Index = 0; Index < LayerCount; ++Index)
		{
			if (Selected.Contains(Index))
			{
				continue;
			}
			if (Index < TopSelected)
			{
				++InsertAt;
			}
			Others.Add(Index);
		}
		TArray<int32> NewOrder;
		NewOrder.Append(Others.GetData(), InsertAt);
		NewOrder.Append(Selected);
		for (int32 Index = InsertAt; Index < Others.Num(); ++Index)
		{
			NewOrder.Add(Others[Index]);
		}
		return NewOrder;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatPermutationIdentityTest,
	"Mixtormat.LayerGroups.PermutationIdentityChangesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatPermutationIdentityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TArray<FMixtormatLayer> Layers = MakeNamedLayers(4);
	Layers[2].HeightReferenceLayerIndex = 0;
	Layers[3].HeightReferenceLayerIndex = 1;

	const int32 Dropped = MixtormatUI::ReorderLayersByPermutation(Layers, { 0, 1, 2, 3 });

	TestEqual(TEXT("Identity drops nothing"), Dropped, 0);
	TestEqual(TEXT("Order is unchanged"), OrderOf(Layers), FString(TEXT("L0,L1,L2,L3")));
	TestEqual(TEXT("References are unchanged"), Layers[2].HeightReferenceLayerIndex, 0);
	TestEqual(TEXT("References are unchanged"), Layers[3].HeightReferenceLayerIndex, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatPermutationFollowsReferencesTest,
	"Mixtormat.LayerGroups.PermutationCarriesHeightReferences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatPermutationFollowsReferencesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	// L3 reads L1's height. Gathering {0,3} into a block must leave L3 still reading L1 -- by its
	// new index, because the reference is positional.
	TArray<FMixtormatLayer> Layers = MakeNamedLayers(5);
	Layers[3].HeightReferenceLayerIndex = 1;

	const TArray<int32> NewOrder = GatherOrder(5, { 0, 3 });
	TestEqual(TEXT("Selection gathers below its topmost member"),
		FString::Join(TArray<FString>{
			FString::FromInt(NewOrder[0]), FString::FromInt(NewOrder[1]),
			FString::FromInt(NewOrder[2]), FString::FromInt(NewOrder[3]),
			FString::FromInt(NewOrder[4])}, TEXT(",")),
		FString(TEXT("1,2,0,3,4")));

	const int32 Dropped = MixtormatUI::ReorderLayersByPermutation(Layers, NewOrder);

	TestEqual(TEXT("Layers land in the gathered order"),
		OrderOf(Layers), FString(TEXT("L1,L2,L0,L3,L4")));
	TestEqual(TEXT("Nothing is dropped when the reference stays below"), Dropped, 0);
	// L1 is now at index 0, and L3 is now at index 3.
	TestEqual(TEXT("L3 still reads L1"), Layers[3].HeightReferenceLayerIndex, 0);
	TestEqual(TEXT("The layer it reads is still L1"),
		Layers[0].DisplayName.ToString(), FString(TEXT("L1")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatPermutationDropsForwardReferencesTest,
	"Mixtormat.LayerGroups.PermutationDropsReferencesThatTurnForward",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatPermutationDropsForwardReferencesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	// A gather does not necessarily invert anything. L2 reads L1, and gathering {0,2} leaves L2
	// above L1, so the reference survives.
	TArray<FMixtormatLayer> Layers = MakeNamedLayers(4);
	Layers[2].HeightReferenceLayerIndex = 1;

	const TArray<int32> NewOrder = GatherOrder(4, { 0, 2 });
	const int32 Dropped = MixtormatUI::ReorderLayersByPermutation(Layers, NewOrder);

	TestEqual(TEXT("Layers land in the gathered order"),
		OrderOf(Layers), FString(TEXT("L1,L0,L2,L3")));
	// L1 is at 0 and L2 at 2, so the reference still points downward and survives.
	TestEqual(TEXT("A reference that stays below survives"), Dropped, 0);
	TestEqual(TEXT("L2 still reads L1"), Layers[2].HeightReferenceLayerIndex, 0);

	// Now the inverting case, built directly: L0 ends up above the layer it reads.
	TArray<FMixtormatLayer> Inverting = MakeNamedLayers(3);
	Inverting[2].HeightReferenceLayerIndex = 0;
	const int32 InvertedDropped =
		MixtormatUI::ReorderLayersByPermutation(Inverting, { 2, 0, 1 });

	TestEqual(TEXT("Layers land in the requested order"),
		OrderOf(Inverting), FString(TEXT("L2,L0,L1")));
	TestEqual(TEXT("A reference that turns forward is counted"), InvertedDropped, 1);
	TestEqual(TEXT("A reference that turns forward is cleared"),
		Inverting[0].HeightReferenceLayerIndex, static_cast<int32>(INDEX_NONE));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatPermutationGroupContiguityTest,
	"Mixtormat.LayerGroups.GatherMakesSelectionContiguous",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatPermutationGroupContiguityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TArray<FMixtormatLayer> Layers = MakeNamedLayers(6);
	const TArray<int32> Selected = { 0, 2, 5 };
	const TArray<int32> NewOrder = GatherOrder(6, Selected);
	MixtormatUI::ReorderLayersByPermutation(Layers, NewOrder);

	TestEqual(TEXT("The unselected layers keep their relative order"),
		OrderOf(Layers), FString(TEXT("L1,L3,L4,L0,L2,L5")));

	// The block occupies a single run, which is what a group requires.
	const int32 InsertAt = 3;
	TArray<FMixtormatLayerGroup> Groups;
	FMixtormatLayerGroup& Group = Groups.AddDefaulted_GetRef();
	for (int32 Index = InsertAt; Index < InsertAt + Selected.Num(); ++Index)
	{
		Layers[Index].GroupId = Group.GroupId;
	}

	int32 First = INDEX_NONE;
	int32 Last = INDEX_NONE;
	TestTrue(TEXT("The group has members"),
		MixtormatLayerGroups::GetGroupRange(Layers, Group.GroupId, First, Last));
	TestEqual(TEXT("Members form one run"), Last - First + 1, Selected.Num());

	// Validation should find nothing to repair.
	MixtormatLayerGroups::ValidateGroups(Layers, Groups);
	TestEqual(TEXT("The group survives validation"), Groups.Num(), 1);
	TestEqual(TEXT("Every selected layer is still a member"),
		Layers[3].GroupId, Layers[5].GroupId);
	return true;
}

namespace
{
	// The rule HandleLayerDropped applies after a move: a layer is in a group when it lands inside
	// or against that group's run, and out of one when it lands anywhere else.
	FGuid MembershipAt(const TArray<FMixtormatLayer>& Layers, const int32 Index)
	{
		const FGuid Below = Layers.IsValidIndex(Index - 1) ? Layers[Index - 1].GroupId : FGuid();
		const FGuid Above = Layers.IsValidIndex(Index + 1) ? Layers[Index + 1].GroupId : FGuid();
		if (Below.IsValid() && Below == Above)
		{
			return Below;
		}
		const FGuid Own = Layers[Index].GroupId;
		if (Own.IsValid() && (Own == Below || Own == Above))
		{
			return Own;
		}
		return FGuid();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatLayerGroupMembershipByPositionTest,
	"Mixtormat.LayerGroups.MembershipFollowsWhereALayerLands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatLayerGroupMembershipByPositionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	// L1,L2,L3 are a group; L0 and L4 are loose.
	TArray<FMixtormatLayer> Layers = MakeNamedLayers(5);
	TArray<FMixtormatLayerGroup> Groups;
	Groups.AddDefaulted(1);
	const FGuid GroupId = Groups[0].GroupId;
	for (int32 Index = 1; Index <= 3; ++Index)
	{
		Layers[Index].GroupId = GroupId;
	}

	// Dropped between two members: in, even though it arrived ungrouped.
	TestEqual(TEXT("Landing between two members joins the group"),
		MembershipAt(Layers, 2), GroupId);

	// A loose layer landing against one edge stays loose -- it has no membership to keep.
	TestFalse(TEXT("Landing against one edge does not silently join"),
		MembershipAt(Layers, 0).IsValid());
	TestFalse(TEXT("Landing past the far edge does not join"),
		MembershipAt(Layers, 4).IsValid());

	// A member reordered to the edge of its own run keeps its membership.
	TestEqual(TEXT("A member at the bottom edge of its run stays in"),
		MembershipAt(Layers, 1), GroupId);
	TestEqual(TEXT("A member at the top edge of its run stays in"),
		MembershipAt(Layers, 3), GroupId);

	// Moved clear of the run, a member leaves it.
	TArray<FMixtormatLayer> Moved = Layers;
	MixtormatUI::ReorderLayersByPermutation(Moved, { 3, 0, 1, 2, 4 });
	TestEqual(TEXT("The moved layer is the one that was at index 3"),
		Moved[0].DisplayName.ToString(), FString(TEXT("L3")));
	TestFalse(TEXT("A member dragged clear of its run leaves the group"),
		MembershipAt(Moved, 0).IsValid());

	// And the run that remains is still contiguous, so validation finds nothing to repair.
	Moved[0].GroupId.Invalidate();
	MixtormatLayerGroups::ValidateGroups(Moved, Groups);
	TestEqual(TEXT("The group survives a member leaving"), Groups.Num(), 1);
	int32 First = INDEX_NONE;
	int32 Last = INDEX_NONE;
	TestTrue(TEXT("The group still has members"),
		MixtormatLayerGroups::GetGroupRange(Moved, GroupId, First, Last));
	TestEqual(TEXT("What remains is one contiguous run"), Last - First + 1, 2);
	return true;
}

namespace
{
	// InsertIndexToMoveTarget, duplicated here rather than reached through SMixtormat: the widget
	// is not constructible in a test, and this is the arithmetic under test, not the widget.
	int32 MoveTargetFor(const int32 SourceIndex, const int32 InsertIndex)
	{
		return SourceIndex < InsertIndex ? InsertIndex - 1 : InsertIndex;
	}

	// What HandleLayerDropped does with that target.
	TArray<FMixtormatLayer> MoveOne(
		const TArray<FMixtormatLayer>& Layers,
		const int32 SourceIndex,
		const int32 TargetIndex)
	{
		TArray<int32> NewOrder;
		for (int32 Index = 0; Index < Layers.Num(); ++Index)
		{
			if (Index != SourceIndex)
			{
				NewOrder.Add(Index);
			}
		}
		NewOrder.Insert(SourceIndex, TargetIndex);
		TArray<FMixtormatLayer> Result = Layers;
		MixtormatUI::ReorderLayersByPermutation(Result, NewOrder);
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatDropInsertIndexTest,
	"Mixtormat.LayerGroups.DropLandsOnTheLineThatWasDrawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatDropInsertIndexTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	// The insertion line is a slot in the array as it stands, counted before the source is taken
	// out of it. The two directions shift differently, and picking the wrong one puts the layer
	// one row from the line the user was looking at -- which compiles perfectly either way.
	const TArray<FMixtormatLayer> Layers = MakeNamedLayers(5);

	// Source below the line: removing L1 shifts everything above it down, so the slot does too.
	TestEqual(TEXT("Source below the line shifts the target down one"),
		MoveTargetFor(1, 4), 3);
	TestEqual(TEXT("L1 dropped on the line above L4 lands between L3 and L4"),
		OrderOf(MoveOne(Layers, 1, MoveTargetFor(1, 4))),
		FString(TEXT("L0,L2,L3,L1,L4")));

	// Source above the line: nothing between them moves, so the slot is taken as written.
	TestEqual(TEXT("Source above the line leaves the target alone"),
		MoveTargetFor(4, 1), 1);
	TestEqual(TEXT("L4 dropped on the line above L1 lands between L0 and L1"),
		OrderOf(MoveOne(Layers, 4, MoveTargetFor(4, 1))),
		FString(TEXT("L0,L4,L1,L2,L3")));

	// The two edges of the source's own row are both no-ops, and must not move it by one.
	TestEqual(TEXT("Dropping on a layer's own leading edge does not move it"),
		OrderOf(MoveOne(Layers, 2, MoveTargetFor(2, 2))), OrderOf(Layers));
	TestEqual(TEXT("Dropping on a layer's own trailing edge does not move it"),
		OrderOf(MoveOne(Layers, 2, MoveTargetFor(2, 3))), OrderOf(Layers));

	// The very end of the stack is a legal line: one past the last row.
	TestEqual(TEXT("Dropping past the last row moves to the end"),
		OrderOf(MoveOne(Layers, 0, MoveTargetFor(0, 5))),
		FString(TEXT("L1,L2,L3,L4,L0")));
	return true;
}

#endif
