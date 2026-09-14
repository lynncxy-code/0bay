using UnrealBuildTool;
public class OfficeHostEditorTarget : TargetRules
{
    public OfficeHostEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V5;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_6;
        ExtraModuleNames.Add("OfficeHost");
    }
}
