// Copyright 2026 Hugo Beyer. All Rights Reserved.

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
			// FPlatformApplicationMisc (clipboard) resolves to FWindowsPlatformApplicationMisc,
			// implemented in ApplicationCore; without this the editor module fails to link it.
			"ApplicationCore",
			"AssetRegistry",
			"AssetTools",
			"ContentBrowser",
			"DesktopPlatform",
			"InputCore",
			"Settings",
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
