using UnrealBuildTool;

public class BoxingUETarget : TargetRules
{
	public BoxingUETarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("BoxingUE");
	}
}
