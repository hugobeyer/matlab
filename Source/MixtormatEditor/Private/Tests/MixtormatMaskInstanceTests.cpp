#include "Misc/AutomationTest.h"

#include "MixtormatMaterial.h"
#include "MixtormatParameterBinding.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatMaskInstanceLocalOverridesTest,
	"Mixtormat.Instances.MaskLocalBlendAndInvert",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMixtormatMaskInstanceLocalOverridesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FMixtormatLayer SourceLayer;
	FMixtormatLayerChild& Source = SourceLayer.Children.AddDefaulted_GetRef();
	Source.Type = EMixtormatLayerChildType::Mask;
	Source.Mask.BlendMode = EMixtormatMaskBlendMode::Replace;
	Source.Mask.Shaping.bInvert = false;
	Source.Mask.Weight = 0.35f;
	Source.Mask.TilingX = 4;

	FMixtormatParameterBinding BlendBinding;
	BlendBinding.DestinationOwner = EMixtormatParameterOwnerType::Mask;
	BlendBinding.DestinationParameter = GET_MEMBER_NAME_CHECKED(FMixtormatMaskLayer, BlendMode);
	Source.ParameterBindings.Add(BlendBinding);

	FMixtormatParameterBinding InvertBinding;
	InvertBinding.DestinationOwner = EMixtormatParameterOwnerType::MaskShaping;
	InvertBinding.DestinationParameter = GET_MEMBER_NAME_CHECKED(FMixtormatMaskShaping, bInvert);
	Source.ParameterBindings.Add(InvertBinding);

	FMixtormatLayer InstanceLayer;
	FMixtormatLayerChild& Instance = InstanceLayer.Children.AddDefaulted_GetRef();
	Instance.Type = EMixtormatLayerChildType::Mask;
	Instance.SourceLayerId = SourceLayer.LayerId;
	Instance.SourceChildId = Source.ChildId;
	Instance.Mask.BlendMode = EMixtormatMaskBlendMode::Subtract;
	Instance.Mask.Shaping.bInvert = true;
	Instance.Mask.Weight = 1.0f;
	Instance.Mask.TilingX = 1;

	TArray<FMixtormatLayer> Layers;
	Layers.Add(SourceLayer);
	Layers.Add(InstanceLayer);

	FMixtormatLayer Resolved = Layers[1];
	MixtormatParameterBinding::ResolveChildInstances(Layers, Resolved);
	const FMixtormatLayerChild& Result = Resolved.Children[0];

	TestEqual(TEXT("Instance keeps its local blend mode"),
		Result.Mask.BlendMode, EMixtormatMaskBlendMode::Subtract);
	TestTrue(TEXT("Instance keeps its local invert"), Result.Mask.Shaping.bInvert);
	TestEqual(TEXT("Instance inherits mask weight"), Result.Mask.Weight, 0.35f);
	TestEqual(TEXT("Instance inherits mask tiling"), Result.Mask.TilingX, 4);
	TestFalse(TEXT("Inherited blend binding does not override the local value"),
		Result.ParameterBindings.ContainsByPredicate([](const FMixtormatParameterBinding& Binding)
		{
			return Binding.DestinationOwner == EMixtormatParameterOwnerType::Mask
				&& Binding.DestinationParameter == GET_MEMBER_NAME_CHECKED(
					FMixtormatMaskLayer, BlendMode);
		}));
	TestFalse(TEXT("Inherited invert binding does not override the local value"),
		Result.ParameterBindings.ContainsByPredicate([](const FMixtormatParameterBinding& Binding)
		{
			return Binding.DestinationOwner == EMixtormatParameterOwnerType::MaskShaping
				&& Binding.DestinationParameter == GET_MEMBER_NAME_CHECKED(
					FMixtormatMaskShaping, bInvert);
		}));

	return true;
}

#endif
