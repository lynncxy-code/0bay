using UnrealBuildTool;
public class OfficeHost : ModuleRules
{
    public OfficeHost(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "MediaAssets" });
        PrivateDependencyModuleNames.AddRange(new string[] { "UMG", "Slate", "SlateCore", "InputCore", "Json", "ProceduralMeshComponent" });
        if (Target.bBuildEditor) PrivateDependencyModuleNames.Add("MeshDescription");
    }
}
