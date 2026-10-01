using UnrealBuildTool;

public class BoxingUEEditorTarget : TargetRules
{
	public BoxingUEEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("BoxingUE");
	}
}
