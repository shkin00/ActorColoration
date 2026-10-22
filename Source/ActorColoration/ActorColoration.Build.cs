using UnrealBuildTool;

public class ActorColoration : ModuleRules
{
	public ActorColoration(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[] { "Core" });
	}
}
