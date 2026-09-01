using UnrealBuildTool;

public class MaterialLabShaders : ModuleRules
{
	public MaterialLabShaders(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"MaterialLabRuntime",
			"RenderCore",
			"RHI"
		});

		PrivateDependencyModuleNames.Add("Projects");
	}
}
