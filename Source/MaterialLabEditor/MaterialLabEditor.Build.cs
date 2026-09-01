using UnrealBuildTool;

public class MaterialLabEditor : ModuleRules
{
	public MaterialLabEditor(ReadOnlyTargetRules Target) : base(Target)
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
			"MaterialLabRuntime",
			"MaterialLabShaders",
			"Projects",
			"RenderCore",
			"RHI",
			"ToolMenus",
			"UnrealEd"
		});
	}
}
