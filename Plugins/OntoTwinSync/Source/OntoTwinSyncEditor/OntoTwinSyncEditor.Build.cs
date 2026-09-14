using UnrealBuildTool;

public class OntoTwinSyncEditor : ModuleRules
{
	public OntoTwinSyncEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"LevelEditor"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Blutility",
			"Slate",
			"SlateCore",
			"ToolMenus",
			"UMGEditor"
		});
	}
}
