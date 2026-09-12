// Copyright 2026 Hugo Beyer. All Rights Reserved.

using UnrealBuildTool;

public class MixtormatRuntime : ModuleRules
{
	public MixtormatRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});
	}
}
