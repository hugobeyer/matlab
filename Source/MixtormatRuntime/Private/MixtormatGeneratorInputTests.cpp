// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "MixtormatMaterial.h"
#include "MixtormatOutputReference.h"

#include "MixtormatLayerGroups.h"


#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMixtormatGeneratorInputOrderTest,
	"Mixtormat.Runtime.GeneratorInputs.OrderAndKinds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatGeneratorInputOrderTest::RunTest(const FString& Parameters)
{
	TArray<FMixtormatLayer> Layers;
	Layers.SetNum(2);
	for (FMixtormatLayer& Layer : Layers)
	{
		Layer.LayerId = FGuid::NewGuid();
		Layer.Type = EMixtormatLayerType::Generator;
		Layer.bEnabled = true;
		Layer.Children.SetNum(4);
		for (FMixtormatLayerChild& Child : Layer.Children)
		{
			Child.ChildId = FGuid::NewGuid();
			Child.Type = EMixtormatLayerChildType::Generator;
			Child.Generator.bEnabled = true;
		}
		FMixtormatLayerChild& Tool = Layer.Children[1];
		Tool.Type = EMixtormatLayerChildType::Behavior;
		Tool.ScopeOwnerChildId = Layer.Children[0].ChildId;
		Tool.Behavior.bEnabled = true;
		Tool.Behavior.Type = EMixtormatBehaviorType::FlowField;
	}
	FMixtormatOutputReference Reference;
	Reference.SourceLayerId = Layers[0].LayerId;
	Reference.SourceChildId = Layers[0].Children[0].ChildId;
	Reference.Kind = EMixtormatPublishedFieldKind::ScalarSigned;
	Reference.OutputName = FName(TEXT("Height"));
	const auto Resolve = [&](const int32 Layer, const int32 Child)
	{
		return MixtormatOutputReferences::ResolveGeneratorInputSource(Layers, Layer, Child, Reference);
	};
	TestFalse(TEXT("Height socket defaults disabled"), Layers[0].Children[2].Generator.HeightSource.bEnabled);
	TestFalse(TEXT("Warp socket defaults disabled"), Layers[0].Children[2].Generator.WarpSource.bEnabled);
	TestEqual(TEXT("Earlier same-layer height"), Resolve(0, 2), 0);
	TestEqual(TEXT("Earlier-layer height"), Resolve(1, 0), 0);
	TestEqual(TEXT("Self read rejected"), Resolve(0, 0), INDEX_NONE);
	Reference.SourceChildId = Layers[0].Children[3].ChildId;
	TestEqual(TEXT("Forward read rejected; cannot close a cycle"), Resolve(0, 2), INDEX_NONE);
	Reference.SourceLayerId = Layers[1].LayerId;
	Reference.SourceChildId = Layers[1].Children[0].ChildId;
	TestEqual(TEXT("Later-layer read rejected"), Resolve(0, 2), INDEX_NONE);
	Reference.SourceLayerId = Layers[0].LayerId;
	Reference.SourceChildId = Layers[0].Children[0].ChildId;

	const EMixtormatGeneratorType Types[] = {
		EMixtormatGeneratorType::StrataCarver, EMixtormatGeneratorType::RockFormation,
		EMixtormatGeneratorType::Pebbles, EMixtormatGeneratorType::Cracks,
		EMixtormatGeneratorType::CliffStrata, EMixtormatGeneratorType::Noise};
	for (const EMixtormatGeneratorType Type : Types)
	{
		Layers[0].Children[0].Generator.Type = Type;
		TestEqual(TEXT("Every generator publishes completed signed height"), Resolve(0, 2), 0);
	}
	Layers[0].Children[0].Generator.Type = EMixtormatGeneratorType::StrataCarver;
	Reference.Kind = EMixtormatPublishedFieldKind::Vector2;
	TestEqual(TEXT("Vector2 is not Flow"), Resolve(0, 2), INDEX_NONE);
	Reference.Kind = EMixtormatPublishedFieldKind::ScalarSigned;
	Reference.OutputName = FName(TEXT("Value"));
	TestEqual(TEXT("Height socket does not silently select Noise Value"), Resolve(0, 2), INDEX_NONE);
	Reference.OutputName = FName(TEXT("Height"));
	Layers[0].Children[0].Generator.bEnabled = false;
	TestEqual(TEXT("Disabled height source"), Resolve(0, 2), INDEX_NONE);
	Layers[0].Children[0].Generator.bEnabled = true;
	Layers[0].bEnabled = false;
	TestEqual(TEXT("Disabled source layer"), Resolve(1, 0), INDEX_NONE);
	Layers[0].bEnabled = true;
	Layers[0].Children[2].Generator.bEnabled = false;
	TestEqual(TEXT("Disabled destination"), Resolve(0, 2), INDEX_NONE);
	Layers[0].Children[2].Generator.bEnabled = true;

	Reference.SourceChildId = Layers[0].Children[1].ChildId;
	Reference.Kind = EMixtormatPublishedFieldKind::Flow;
	Reference.OutputName = FName(TEXT("FlowDirection"));
	TestEqual(TEXT("Earlier same-layer flow tool"), Resolve(0, 2), 1);
	TestEqual(TEXT("Earlier-layer flow tool"), Resolve(1, 0), 1);
	TestFalse(TEXT("Legacy layer-wide Flow remains earlier-layer only"),
		MixtormatOutputReferences::ValidateDependency(Layers, Layers[0].LayerId,
			Layers[0].Children[2].ChildId, Reference));
	Layers[0].Children[0].Generator.bEnabled = false;
	TestEqual(TEXT("Disabled flow owner"), Resolve(0, 2), INDEX_NONE);
	Layers[0].Children[0].Generator.bEnabled = true;
	Layers[0].Children[0].Generator.Type = EMixtormatGeneratorType::CliffStrata;
	TestEqual(TEXT("Cliff flow ownership remains unavailable"), Resolve(0, 2), INDEX_NONE);
	Layers[0].Children[0].Generator.Type = EMixtormatGeneratorType::StrataCarver;
	Layers[0].Children[1].Behavior.bEnabled = false;
	TestEqual(TEXT("Disabled flow tool"), Resolve(0, 2), INDEX_NONE);
	Layers[0].Children[1].Behavior.bEnabled = true;
	Reference.Kind = EMixtormatPublishedFieldKind::UVMap;
	Reference.OutputName = FName(TEXT("WarpedUV"));
	TestEqual(TEXT("Flow Field has no UVMap output"), Resolve(0, 2), INDEX_NONE);
	Layers[0].Children[1].Behavior.Type = EMixtormatBehaviorType::Warp;
	Layers[0].Children[1].Behavior.Flow.bUseTracedFlow = true;
	TestEqual(TEXT("Traced Warp publishes UVMap"), Resolve(0, 2), 1);
	Layers[0].Children[1].Behavior.Type = EMixtormatBehaviorType::Carve;
	TestEqual(TEXT("Traced Carve has no UVMap output"), Resolve(0, 2), INDEX_NONE);
	Reference.Kind = EMixtormatPublishedFieldKind::Flow;
	Reference.OutputName = FName(TEXT("FlowDirection"));
	TestEqual(TEXT("Traced Carve still publishes Flow"), Resolve(0, 2), 1);
	Reference.bEnabled = false;
	TestEqual(TEXT("Disabled connection"), Resolve(0, 2), INDEX_NONE);
	return true;
}

#endif
