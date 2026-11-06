// OntoTwinSync 插件模块定义
// 依赖与原 test0316 游戏模块一致；4.0 人物漫游使用 UE 内置 Enhanced Input。

using UnrealBuildTool;
using System;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;

public class OntoTwinSync : ModuleRules
{
	public OntoTwinSync(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Embed the source identity in the DLL, not in a mutable sidecar file.
		string IdentityRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "../.."));
		var IdentityFiles = Directory.GetFiles(Path.Combine(IdentityRoot, "Source"), "*", SearchOption.AllDirectories)
			.Concat(new[] { Path.Combine(IdentityRoot, "OntoTwinSync.uplugin") })
			.OrderBy(P => Path.GetRelativePath(IdentityRoot, P).Replace('\\', '/'), StringComparer.Ordinal);
		var IdentityText = new StringBuilder();
		using (var Hash = SHA256.Create())
		{
			foreach (string File in IdentityFiles)
			{
				ExternalDependencies.Add(File);
				string Digest = Convert.ToHexString(Hash.ComputeHash(System.IO.File.ReadAllBytes(File))).ToLowerInvariant();
				IdentityText.Append(Path.GetRelativePath(IdentityRoot, File).Replace('\\', '/')).Append('\0').Append(Digest).Append('\n');
			}
			string Fingerprint = Convert.ToHexString(Hash.ComputeHash(Encoding.UTF8.GetBytes(IdentityText.ToString()))).ToLowerInvariant();
			PrivateDefinitions.Add("ONTOTWIN_BUILD_SOURCE_HASH=\"" + Fingerprint + "\"");
		}
		// Several UI translation units intentionally use the same anonymous
		// namespace color names. Keep them as separate translation units.
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"ApplicationCore",
			"AudioMixer",
			"Core",
			"CoreUObject",
			"DeveloperSettings",
			"Engine",
			"EnhancedInput",
			"HTTP",
			"InputCore",
			"Json",
			"JsonUtilities",
			"MediaAssets",
			"Niagara",
			"SlateRHIRenderer",
			"UMG",
			"glTFRuntime"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AssetRegistry",
			"Projects",
			"RHI",
			"RenderCore",
			"Renderer",
			"Slate",
			"SlateCore",
			"WebUI",
			"WebSockets"
		});

		RuntimeDependencies.Add("$(PluginDir)/Resources/Fonts/Inter-Regular.ttf");
		RuntimeDependencies.Add("$(PluginDir)/Resources/Fonts/Inter-SemiBold.ttf");
		RuntimeDependencies.Add("$(PluginDir)/Resources/Fonts/NotoSansCJKsc-Regular.otf");
		RuntimeDependencies.Add("$(PluginDir)/Resources/Fonts/NotoSansCJKsc-Medium.otf");
		RuntimeDependencies.Add("$(PluginDir)/Resources/Fonts/LICENSE-Inter.txt");
		RuntimeDependencies.Add("$(PluginDir)/Resources/Fonts/LICENSE-NotoSansCJK.txt");

		// FR-6 迁移工具：编辑器选择集 / EditorDestroyActor 需要 UnrealEd（仅编辑器构建）
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemLibraries.Add("bcrypt.lib");
		}

		if (Target.bBuildEditor)
		{
			PublicDependencyModuleNames.AddRange(new string[]
			{
				"DeveloperToolSettings",
				"UnrealEd"
			});
		}
	}
}
