// Copyright 2025 Tracer Interactive, LLC. All Rights Reserved.
using UnrealBuildTool;

public class WebBrowserUtils : ModuleRules
{
	public WebBrowserUtils(ReadOnlyTargetRules Target) : base(Target)
	{
		int EngineVersion = Target.Version.MajorVersion * 100 + Target.Version.MinorVersion;
		PublicDefinitions.Add("ENGINE_VERSION=" + EngineVersion.ToString());
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"RHI",
				"Projects"
			}
		);

		if (Target.Platform == UnrealTargetPlatform.Win64
		 || Target.Platform == UnrealTargetPlatform.Mac)
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target,
				"CEFUI"
				);
		}

		if (Target.Type != TargetType.Server)
		{
			if (Target.Platform == UnrealTargetPlatform.Win64
			 || Target.Platform == UnrealTargetPlatform.Mac)
			{
				AddEngineThirdPartyPrivateStaticDependencies(Target,
					"TracerWebAcceleratedPaint"
					);
			}
		}
	}
}
