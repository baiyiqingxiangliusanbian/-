using UnrealBuildTool;
using System.Collections.Generic;

public class AscendSpireEditorTarget : TargetRules
{
	public AscendSpireEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("AscendSpire");
	}
}
