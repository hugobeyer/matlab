// Copyright 2026 Hugo Beyer. All Rights Reserved.

using UnrealBuildTool;

public class MixtormatShaders : ModuleRules
{
	public MixtormatShaders(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"MixtormatRuntime",
			"RenderCore",
			"RHI"
		});

		PrivateDependencyModuleNames.Add("Projects");
	}
}
