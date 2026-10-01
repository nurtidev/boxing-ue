using UnrealBuildTool;

public class BoxingUE : ModuleRules
{
	public BoxingUE(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
			"PhysicsControl", "AnimGraphRuntime"
		});
	}
}
