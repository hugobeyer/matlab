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
			"DeveloperSettings",
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
			"Json",
			"LevelEditor",
			"MaterialEditor",
			"MixtormatRuntime",
			"MixtormatShaders",
			"Projects",
			"PropertyEditor",
			"RenderCore",
			"Renderer",
			"RHI",
			"ToolMenus",
			"UnrealEd"
		});
	}
}
