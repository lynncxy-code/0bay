// Copyright 2025 Tracer Interactive, LLC. All Rights Reserved.
namespace UnrealBuildTool.Rules
{
	public class JsonLibrary : ModuleRules
	{
		public JsonLibrary(ReadOnlyTargetRules Target) : base(Target)
		{
			int EngineVersion = Target.Version.MajorVersion * 100 + Target.Version.MinorVersion;
			PublicDefinitions.Add("ENGINE_VERSION=" + EngineVersion.ToString());

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"Json",
					"JsonUtilities"
				}
			);
		}
	}
}
