// Copyright 2025 Tracer Interactive, LLC. All Rights Reserved.
using UnrealBuildTool;
using System.IO;

public class TracerWebAcceleratedPaint : ModuleRules
{
    public TracerWebAcceleratedPaint(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;
		if (Target.Type != TargetType.Server)
		{
            string BinariesPath = Path.Combine(PluginDirectory, "Binaries", Target.Platform.ToString());
            string LibraryPath  = Path.Combine(ModuleDirectory, Target.Platform.ToString());
			string LibraryName  = "TracerWebAcceleratedPaint";

			PublicIncludePaths.Add(Path.Combine(LibraryPath, "include"));
			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, LibraryName + ".lib"));
				PublicDelayLoadDLLs.Add(LibraryName + ".dll");

				RuntimeDependencies.Add(Path.Combine(BinariesPath, LibraryName + ".dll"));
			}
			else if (Target.Platform == UnrealTargetPlatform.Mac)
			{
				PublicAdditionalLibraries.Add(Path.Combine(BinariesPath, "lib" + LibraryName + ".dylib"));
				RuntimeDependencies.Add(Path.Combine(BinariesPath, "lib" + LibraryName + ".dylib"));
			}
		}
	}
}
