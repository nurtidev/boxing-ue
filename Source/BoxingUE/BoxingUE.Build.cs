using UnrealBuildTool;

public class BoxingUE : ModuleRules
{
	public BoxingUE(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
			"PhysicsControl", "AnimGraphRuntime", "AnimationCore", "IKRig"
		});

		// Оболочка игры (S-55): UMG-экраны и ростер из JSON.
		PublicDependencyModuleNames.AddRange(new string[] { "UMG", "Slate", "SlateCore" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Json" });
	}
}
