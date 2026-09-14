using UnrealBuildTool;

public class OntoTwinIndustrialBehavior : ModuleRules
{
    public OntoTwinIndustrialBehavior(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "Json",
            "OntoTwinSync"
        });
    }
}
