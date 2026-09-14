using UnrealBuildTool;
public class OfficeHostTarget : TargetRules
{
    public OfficeHostTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V5;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_6;
        ExtraModuleNames.Add("OfficeHost");
    }
}
