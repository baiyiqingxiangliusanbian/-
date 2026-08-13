using UnrealBuildTool;

public class AscendSpire : ModuleRules
{
	public AscendSpire(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"HTTP",
			"Json",
			"JsonUtilities",
			"UMG",
			"Slate",
			"SlateCore",
			"InputCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });
	}
}
