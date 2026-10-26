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
			"UnrealEd",
			"DeveloperSettings",
			"Json",
			"Projects",
			"AssetTools",
			"AssetRegistry",
			"InterchangeCore",
			"InterchangeEngine",
			"InterchangePipelines",
		});
	}
}
