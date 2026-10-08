// Copyright 2025 Tracer Interactive, LLC. All Rights Reserved.
using UnrealBuildTool;
using UnrealBuildBase;
using System.IO;
using System.Collections.Generic;
#if UE_5_3_OR_LATER
using EpicGames.Core;
#endif

public class CEFUI : ModuleRules
{
	public CEFUI(ReadOnlyTargetRules Target) : base(Target)
	{
		string CEFVersion  = "";
		string CEFPlatform = "";

		Type = ModuleType.External;
#if UE_5_5_OR_LATER
        IWYUSupport = IWYUSupport.None;
#endif

        string RuntimePlatform = Target.Platform.ToString();
        if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			if (Target.Architecture == UnrealArch.Arm64)
			{
				RuntimePlatform = "WinArm64";
			}

			CEFVersion  = Target.Architecture == UnrealArch.Arm64 ? "128.4.13+ge76af7e+chromium-128.0.6613.138" : "124.3.9+g9bd638f+chromium-124.0.6367.207";
			CEFPlatform = Target.Architecture == UnrealArch.Arm64 ? "windowsarm64" : "windows64";
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			CEFVersion = "128.4.13+ge76af7e+chromium-128.0.6613.138";
			// the wrapper.la that is in macosarm64 is universal, so we always point to this one for the lib
			CEFPlatform = "macos_universal";
		}

		if (CEFPlatform.Length > 0 && CEFVersion.Length > 0)
		{
			string PluginThirdPartyBinariesDirectory = Path.Combine(PluginDirectory, "Binaries", "ThirdParty");
			string PluginThirdPartySourceDirectory   = Path.Combine(PluginDirectory, "Source",   "ThirdParty");

			string PlatformPath = Path.Combine(PluginThirdPartySourceDirectory, "CEFUI", "cef_binary_" + CEFVersion + "_" + CEFPlatform);
			string LibraryPath  = Path.Combine(PlatformPath, "Release");

			PublicSystemIncludePaths.Add(PlatformPath);
			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				string RuntimePath = Path.Combine(PluginThirdPartyBinariesDirectory, "CEF3", RuntimePlatform);
				if (Target.Architecture == UnrealArch.Arm64)
				{
					RuntimePath = Path.Combine(RuntimePath, CEFVersion);
				}

                PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "libcef.lib"));

                // There are different versions of the C++ wrapper lib depending on the version of VS we're using
                string VSVersionFolderName = "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName();
                string WrapperLibraryPath  = Path.Combine(PlatformPath, VSVersionFolderName, "libcef_dll_wrapper");

                if (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT)
                {
                    WrapperLibraryPath += "/Debug";
                }
                else
                {
                    WrapperLibraryPath += "/Release";
                }

                PublicAdditionalLibraries.Add(Path.Combine(WrapperLibraryPath, "libcef_dll_wrapper.lib"));

				List<string> Dlls = new List<string>();

				Dlls.Add("chrome_elf.dll");
				Dlls.Add("d3dcompiler_47.dll");
				Dlls.Add("dxcompiler.dll");
				Dlls.Add("dxil.dll");
				Dlls.Add("libcef.dll");
				Dlls.Add("libEGL.dll");
				Dlls.Add("libGLESv2.dll");
				Dlls.Add("vk_swiftshader.dll");
				Dlls.Add("vulkan-1.dll");

				PublicDelayLoadDLLs.AddRange(Dlls);

                // Add the runtime dlls to the build receipt
                foreach (string Dll in Dlls)
				{
					RuntimeDependencies.Add(Path.Combine(RuntimePath, Dll));
                }
				// We also need the icu translations table required by CEF
				RuntimeDependencies.Add(Path.Combine(RuntimePath, "icudtl.dat"));
				if (Target.Architecture == UnrealArch.Arm64)
				{
					// The .pak files are required next to the binaries
					RuntimeDependencies.Add(Path.Combine(RuntimePath, "chrome_100_percent.pak"));
					RuntimeDependencies.Add(Path.Combine(RuntimePath, "chrome_200_percent.pak"));
					RuntimeDependencies.Add(Path.Combine(RuntimePath, "resources.pak"));
				}

				// Add the V8 binary data files as well
				RuntimeDependencies.Add(Path.Combine(RuntimePath, "v8_context_snapshot.bin"));
				RuntimeDependencies.Add(Path.Combine(RuntimePath, "snapshot_blob.bin"));

				// And the entire Resources folder. Enumerate the entire directory instead of mentioning each file manually here.
				foreach (string FilePath in Directory.EnumerateFiles(Path.Combine(RuntimePath, "Resources"), "*", SearchOption.AllDirectories))
                {
                    if (Target.Architecture == UnrealArch.Arm64)
                    {
                        RuntimeDependencies.Add(FilePath);
                    }
					else
					{
						string DependencyName = FilePath.Substring(PluginThirdPartyBinariesDirectory.Length).Replace('\\', '/');
						if (DependencyName.StartsWith("/"))
							DependencyName = DependencyName.Substring(1);
					
						RuntimeDependencies.Add(Path.Combine(PluginDirectory, "Binaries", "ThirdParty", DependencyName));
					}
				}
            }
			else if (Target.Platform == UnrealTargetPlatform.Mac)
			{
                string RuntimePath = Path.Combine(PluginThirdPartyBinariesDirectory, "CEF3", RuntimePlatform);
#if UE_5_3_OR_LATER
                PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "libcef_dll_wrapper.a"));
				
				DirectoryReference FrameworkLocation = DirectoryReference.Combine(new DirectoryReference(RuntimePath), "Chromium Embedded Framework.framework");
			  //if (Target.LinkType == TargetLinkType.Modular || !AppleExports.UseModernXcode(Target.ProjectFile))
				{
					// Add contents of framework directory as runtime dependencies
					foreach (string FilePath in Directory.EnumerateFiles(FrameworkLocation.FullName, "*", SearchOption.AllDirectories))
					{
						RuntimeDependencies.Add(FilePath);
					}
				}
				// for modern 
			  //else
			  //{
			  //	FileReference ZipFile = new FileReference(FrameworkLocation.FullName + ".zip");
					// this is relative to module dir
			  //	string FrameworkPath = ZipFile.MakeRelativeTo(new DirectoryReference(ModuleDirectory));

			  //	PublicAdditionalFrameworks.Add(
			  //		new Framework("Chromium Embedded Framework", FrameworkPath, Framework.FrameworkMode.Copy, null)
			  //		);
			  //}
#else
                string WrapperPath   = LibraryPath + "/libcef_dll_wrapper.a";
                string FrameworkPath = RuntimePath + "/Chromium Embedded Framework.framework";

                PublicAdditionalLibraries.Add(WrapperPath);

                if (Directory.Exists(LibraryPath + "/locale"))
                {
                    var LocaleFolders = Directory.GetFileSystemEntries(LibraryPath + "/locale", "*.lproj");
                    foreach (var FolderName in LocaleFolders)
                    {
                        AdditionalBundleResources.Add(new BundleResource(FolderName, bShouldLog: false));
                    }
                }

                // Add contents of framework directory as runtime dependencies
                foreach (string FilePath in Directory.EnumerateFiles(FrameworkPath, "*", SearchOption.AllDirectories))
                {
                    RuntimeDependencies.Add(FilePath);
                }
#endif
            }
        }
	}
}
