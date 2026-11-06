using UnrealBuildTool;
using System.IO;
using EpicGames.Core;

public class MetaverseClient : ModuleRules
{
    public MetaverseClient(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // Headers are grouped directly under Actors/Core/Dto/Tests instead of
        // Unreal's conventional Public/Private folders.
        PrivateIncludePaths.Add(ModuleDirectory);

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core", "CoreUObject", "Engine"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "WebSockets",    // IWebSocket / FWebSocketsModule
            "Json",          // FJsonSerializer
            "JsonUtilities"  // FJsonObjectConverter
        });

        // OntoTwinSync is optional: the generic plugin still packages standalone, while
        // projects that already contain it can consume UTwinCharacterAsset task assets.
        bool bHasOntoTwinSync = false;
        if (Target.ProjectFile != null)
        {
            bHasOntoTwinSync = Directory.Exists(Path.Combine(
                Target.ProjectFile.Directory.FullName, "Plugins", "OntoTwinSync"));
        }
        if (!bHasOntoTwinSync)
        {
            string SiblingPlugin = Path.GetFullPath(Path.Combine(
                ModuleDirectory, "..", "..", "..", "OntoTwinSync"));
            bHasOntoTwinSync = Directory.Exists(SiblingPlugin);
        }

        // 0bay vendors OntoTwinSync in the project-local Plugins directory.
        if (!bHasOntoTwinSync && Target.ProjectFile != null
            && JsonObject.Read(Target.ProjectFile).TryGetObjectArrayField("Plugins", out JsonObject[] Plugins))
        {
            foreach (JsonObject Plugin in Plugins)
            {
                if (Plugin.TryGetStringField("Name", out string Name) && Name == "OntoTwinSync"
                    && Plugin.TryGetBoolField("Enabled", out bool Enabled) && Enabled)
                {
                    bHasOntoTwinSync = true;
                    break;
                }
            }
        }

        PublicDefinitions.Add("METAVERSE_WITH_ONTOTWIN=" + (bHasOntoTwinSync ? "1" : "0"));
        if (bHasOntoTwinSync)
        {
            PublicDependencyModuleNames.Add("OntoTwinSync");
        }
    }
}
