using UnrealBuildTool;

public class OfficeHost : ModuleRules
{
    public OfficeHost(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicIncludePaths.Add(ModuleDirectory);

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "HeadMountedDisplay",
            "InputCore",
            "EnhancedInput",
            "Json",
            "JsonUtilities",
            "HTTP",
            "glTFRuntime"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "UMG",
            "Slate",
            "SlateCore",
            "ProceduralMeshComponent"
        });

        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.Add("MeshDescription");
        }
    }
}
