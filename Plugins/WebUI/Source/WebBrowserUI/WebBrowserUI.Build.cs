// Copyright 2025 Tracer Interactive, LLC. All Rights Reserved.
using UnrealBuildTool;
using System.IO;

public class WebBrowserUI : ModuleRules
{
	public WebBrowserUI(ReadOnlyTargetRules Target) : base(Target)
	{
		int EngineVersion = Target.Version.MajorVersion * 100 + Target.Version.MinorVersion;
		PublicDefinitions.Add("ENGINE_VERSION=" + EngineVersion.ToString());

		if (EngineVersion == 503)
			CppStandard = CppStandardVersion.Cpp17;
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"ApplicationCore",
				"RHI",
                "RHICore",
                "RenderCore",
				"InputCore",
				"ImageWriteQueue",
				"Serialization",
				"HTTP"
			}
		);

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
				"Slate",
				"SlateCore"
            }
        );

        if (Target.Platform == UnrealTargetPlatform.Android
		 || Target.Platform == UnrealTargetPlatform.IOS
		 || Target.Platform == UnrealTargetPlatform.TVOS)
		{
			// We need these on mobile for external texture support
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Launch",
					"WebBrowserTextureUI"
				}
			);

			// We need this one on Android for URL decoding
			PrivateDependencyModuleNames.Add("HTTP");
		}

		if (Target.Type != TargetType.Program && Target.Platform == UnrealTargetPlatform.Win64)
        {
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"D3D12RHI",
					"D3D11RHI"
				}
			);

			if (EngineVersion < 427)
				PrivateIncludePaths.AddRange(
                    new string[]
					{
						Path.Combine(EngineDirectory, "Source/Runtime/Windows/D3D11RHI/Private"),
                        Path.Combine(EngineDirectory, "Source/Runtime/Windows/D3D11RHI/Private/Windows"),
						Path.Combine(EngineDirectory, "Source/Runtime/D3D12RHI/Private"),
						Path.Combine(EngineDirectory, "Source/Runtime/D3D12RHI/Private/Windows")
					}
				);

			PublicIncludePaths.AddRange(
				new string[]
				{
					Path.Combine(EngineDirectory, "Source/Runtime/Windows/D3D11RHI/Public"),
					Path.Combine(EngineDirectory, "Source/Runtime/D3D12RHI/Public"),
				}
			);

			AddEngineThirdPartyPrivateStaticDependencies(Target, "DX12", "DX11");
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
            PublicDependencyModuleNames.AddRange(
				new string[]
				{
                    "MetalRHI"
                }
			);

#if UE_5_4_OR_LATER
			AddEngineThirdPartyPrivateStaticDependencies(Target, "MetalCPP");
#else
			AddEngineThirdPartyPrivateStaticDependencies(Target, "MTLPP");
#endif

			PublicWeakFrameworks.Add("Metal");
            PublicFrameworks.AddRange(new string[] { "IOSurface" });
		}

		if (Target.Platform == UnrealTargetPlatform.Android)
		{
#if UE_4_24_OR_LATER
			PublicSystemLibraries.Add("libjnigraphics");
#endif

			string PluginPath = Utils.MakePathRelativeTo(ModuleDirectory, Target.RelativeEnginePath);
			AdditionalPropertiesForReceipt.Add("AndroidPlugin", Path.Combine(PluginPath, "WebBrowserUI_UPL.xml"));
		}

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PrivateDependencyModuleNames.Add("WebBrowserUtils");
			AddEngineThirdPartyPrivateStaticDependencies(Target,
				"CEFUI"
				);
			
			if (Target.Type != TargetType.Server)
			{
				if (Target.Architecture == UnrealArch.Arm64 || Target.Architecture == UnrealArch.Arm64ec)
                {
                    RuntimeDependencies.Add("$(EngineDir)/Binaries/" + Target.Platform.ToString() + "/EpicWebHelperarm64.exe");
                }
				else
                {
					AddEngineThirdPartyPrivateStaticDependencies(Target,
						"TracerWebAcceleratedPaint"
						);
                    RuntimeDependencies.Add(Path.Combine(PluginDirectory, "Binaries", Target.Platform.ToString(), "TracerWebHelper.exe"));
                }
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PrivateDependencyModuleNames.Add("WebBrowserUtils");
			AddEngineThirdPartyPrivateStaticDependencies(Target,
				"CEFUI"
				);

			if (Target.Type != TargetType.Server)
            {
                AddEngineThirdPartyPrivateStaticDependencies(Target,
                    "TracerWebAcceleratedPaint"
                    );

				RuntimeDependencies.Add(Path.Combine(PluginDirectory, "Binaries", Target.Platform.ToString(), "TracerWebHelper"));
                foreach (string FilePath in Directory.EnumerateFiles(Path.Combine(PluginDirectory, "Binaries", Target.Platform.ToString()), "lib*.dylib", SearchOption.AllDirectories))
                {
                    RuntimeDependencies.Add(FilePath);
                }
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			PrivateDependencyModuleNames.Add("CEF3Utils");
			AddEngineThirdPartyPrivateStaticDependencies(Target,
				"CEF3"
				);

			if (Target.Type != TargetType.Server)
			{
				string ProcessName = EngineVersion >= 500 ? "EpicWebHelper" : "UnrealCEFSubProcess";
				if (Target.Platform == UnrealTargetPlatform.Mac && EngineVersion < 504)
				{
					// Add contents of *.app directory as runtime dependencies
					foreach (string FilePath in Directory.EnumerateFiles(Target.RelativeEnginePath + "/Binaries/Mac/" + ProcessName + ".app", "*", SearchOption.AllDirectories))
					{
						RuntimeDependencies.Add(FilePath);
					}
				}
				else
				{
					RuntimeDependencies.Add("$(EngineDir)/Binaries/" + Target.Platform.ToString() + "/" + ProcessName);
				}
			}
		}
	}
}
