using UnrealBuildTool;

/** The Pose Wizard's measured core (Genesis 9 to DTH solve, bone map, retarget inverse, sequence writer, Daz samples), public so the Animation Wizard shares it. */
public class DTHPoseCore : ModuleRules
{
	public DTHPoseCore(ReadOnlyTargetRules Target) : base(Target)
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
			"UnrealEd",
			"AssetRegistry",
			"Json",
			"RenderCore",
			"ImageCore",
		});
	}
}
