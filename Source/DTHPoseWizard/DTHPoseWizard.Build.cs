using UnrealBuildTool;

public class DTHPoseWizard : ModuleRules
{
	public DTHPoseWizard(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"InputCore",
			"ToolMenus",
			"UnrealEd",
			"WorkspaceMenuStructure",
			"PropertyEditor",
			"AdvancedPreviewScene",
			"ApplicationCore",
			"DeveloperSettings",
			"Json",
			"Projects",
			"AssetTools",
			"AssetRegistry",
			"RenderCore",
			"ImageCore",
			"InterchangeCore",
			"InterchangeEngine",
			"InterchangePipelines",
		});
	}
}
