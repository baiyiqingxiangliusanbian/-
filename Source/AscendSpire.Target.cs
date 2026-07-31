using UnrealBuildTool;
using System.Collections.Generic;

public class AscendSpireTarget : TargetRules
{
	public AscendSpireTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("AscendSpire");
	}
}
