using UnrealBuildTool;

/**
 * The face fit shared by the Expression, Pose and Animation Wizards: DTH's face as Unreal shows it (face evaluator,
 * measured face rig), the Daz face region and its targets, and the shape fit onto DTH's face board and morph curves.
 */
public class DTHFaceCore : ModuleRules
{
	public DTHFaceCore(ReadOnlyTargetRules Target) : base(Target)
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
			"AnimationCore",
			"DTHRigKeying",
		});
	}
}
