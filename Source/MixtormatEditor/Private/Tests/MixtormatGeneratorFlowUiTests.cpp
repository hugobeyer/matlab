// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "MixtormatEffect.h"
#include "Widgets/MixtormatChildCapabilities.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatGeneratorFlowOutputsTest,
	"Mixtormat.Editor.GeneratorFlow.Outputs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatGeneratorFlowOutputsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	for (const EMixtormatEffectType Type : {EMixtormatEffectType::ShapeDeform,
		EMixtormatEffectType::GeneratorFlow, EMixtormatEffectType::FlowCarve})
	{
		const FMixtormatChildCapabilities Capabilities = GetChildCapabilitiesForEffectType(Type);
		TestEqual(TEXT("Four applicable previews"), Capabilities.Outputs.Num(), 4);
		TestEqual(TEXT("Flow diagnostics are not copyable masks"), GetCopyableOutputs(Capabilities).Num(), 0);
		const FName ExpectedPrimary = Type == EMixtormatEffectType::ShapeDeform
			? FName(TEXT("FlowDirection")) : Type == EMixtormatEffectType::GeneratorFlow
				? FName(TEXT("WarpedUVGrid")) : FName(TEXT("CarveMask"));
		int32 PrimaryCount = 0;
		TSet<FName> Names;
		for (const FMixtormatPublishedOutputDesc& Output : Capabilities.Outputs)
		{
			TestTrue(TEXT("Output is previewable"), Output.bPreviewable);
			TestFalse(TEXT("Output is not copyable"), Output.bCopyableAsMask);
			TestTrue(TEXT("No region-ID gap dependency"), Output.PreviewGapMaskName.IsNone());
			Names.Add(Output.Name);
			if (!Output.bSecondaryPreview)
			{
				++PrimaryCount;
				TestEqual(TEXT("Mode-specific primary preview"), Output.Name, ExpectedPrimary);
			}
			const EMixtormatPreviewOutputKind ExpectedKind = Output.Name == FName(TEXT("FlowDirection"))
				? EMixtormatPreviewOutputKind::FlowDirection : Output.Name == FName(TEXT("WarpedUVGrid"))
					? EMixtormatPreviewOutputKind::WarpedUVGrid : EMixtormatPreviewOutputKind::Mask;
			TestTrue(TEXT("Typed preview routing"), Output.Kind == ExpectedKind);
		}
		TestEqual(TEXT("Exactly one primary"), PrimaryCount, 1);
		TestEqual(TEXT("Output names are unique"), Names.Num(), Capabilities.Outputs.Num());
		TestTrue(TEXT("Direction output"), Names.Contains(FName(TEXT("FlowDirection"))));
		TestTrue(TEXT("Influence output"), Names.Contains(FName(TEXT("Influence"))));
		TestTrue(TEXT("Validity output"), Names.Contains(FName(TEXT("Validity"))));
		TestEqual(TEXT("Carve mask only on Flow Carve"), Names.Contains(FName(TEXT("CarveMask"))),
			Type == EMixtormatEffectType::FlowCarve);
		TestEqual(TEXT("UV grid on deformation modes"), Names.Contains(FName(TEXT("WarpedUVGrid"))),
			Type != EMixtormatEffectType::FlowCarve);
	}
	TestEqual(TEXT("Existing Flow Warp capabilities are unchanged"),
		GetChildCapabilitiesForEffectType(EMixtormatEffectType::FlowWarp).Outputs.Num(), 0);
	return true;
}

#endif
