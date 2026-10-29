using UnrealBuildTool;

public class DTHRigKeying : ModuleRules
{
	public DTHRigKeying(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",
			"ControlRig",
			"RigVM",
			"DeveloperSettings",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"InputCore",
			"PropertyEditor",
			"AssetRegistry",
			"MovieScene",
			"MovieSceneTracks",
			"LevelSequence",
			"LevelSequenceEditor",
			"ControlRigEditor",
		});
	}
}
