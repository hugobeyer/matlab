using UnrealBuildTool;

public class MixtormatEditor : ModuleRules
{
	public MixtormatEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore"
		});

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"AdvancedPreviewScene",
			"AppFramework",
			"AssetRegistry",
			"AssetTools",
			"ContentBrowser",
			"DesktopPlatform",
			"InputCore",
			"ImageCore",
			"LevelEditor",
			"MaterialEditor",
			"MixtormatRuntime",
			"MixtormatShaders",
			"Projects",
			"RenderCore",
			"RHI",
			"ToolMenus",
			"UnrealEd"
		});
	}
}
