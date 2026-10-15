using System.IO;
using UnrealBuildTool;
public class OfficeHost : ModuleRules
{
    public OfficeHost(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "MediaAssets" });
        PrivateDependencyModuleNames.AddRange(new string[] { "UMG", "Slate", "SlateCore", "InputCore", "Json", "ProceduralMeshComponent", "HTTP" });
        if (Target.bBuildEditor) PrivateDependencyModuleNames.Add("MeshDescription");

        // FFmpeg 8.1 (LGPL, shared build from BtbN/FFmpeg-Builds) reads the cameras' RTSP directly: VideoTwinRtspReader.
        // The DLLs are delay-loaded and loaded by full path on first use; RuntimeDependencies copies them next to the
        // module in Binaries/Win64, for the editor and for a packaged build alike.
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            string FFmpeg = Path.Combine(ModuleDirectory, "..", "ThirdParty", "FFmpeg");
            PublicSystemIncludePaths.Add(Path.Combine(FFmpeg, "include"));
            foreach (string Lib in new string[] { "avformat", "avcodec", "avutil", "swscale" })
            {
                PublicAdditionalLibraries.Add(Path.Combine(FFmpeg, "lib", "Win64", Lib + ".lib"));
            }
            foreach (string Dll in new string[] { "avutil-60.dll", "swresample-6.dll", "swscale-9.dll", "avcodec-62.dll", "avformat-62.dll" })
            {
                PublicDelayLoadDLLs.Add(Dll);
                RuntimeDependencies.Add("$(BinaryOutputDir)/" + Dll, Path.Combine(FFmpeg, "bin", "Win64", Dll));
            }
        }
    }
}
