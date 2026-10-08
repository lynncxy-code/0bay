// Copyright 2025 Tracer Interactive, LLC. All Rights Reserved.
#include "WebBrowserUtils.h"
#include "Containers/UnrealString.h"
#include "Logging/LogMacros.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "Misc/OutputDeviceFile.h"
#include "Interfaces/IPluginManager.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "WebBrowserUtilsLog.h"

#if WITH_CEF3
#	if PLATFORM_WINDOWS || PLATFORM_MAC
#		include "TracerWebAcceleratedPaint.h"
#	endif
#endif

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#if WITH_ENGINE
#include "RHI.h"
#endif

DEFINE_LOG_CATEGORY(LogWebBrowserUtils);

IMPLEMENT_MODULE(FDefaultModuleImpl, WebBrowserUtils);

#if WITH_CEF3
#	if PLATFORM_WINDOWS || PLATFORM_MAC
#		define CEF3_BIN_DIR TEXT("Plugins/Marketplace/WebUI/Binaries")
#		define CEF3_BIN_DIR_THIRD_PARTY CEF3_BIN_DIR TEXT("/ThirdParty/CEF3")
#	endif
#	if PLATFORM_WINDOWS
#		define CEF3_SUBPROCESS_EXE CEF3_BIN_DIR TEXT("/Win64/TracerWebHelper.exe")
#	elif PLATFORM_MAC
#		define CEF3_SUBPROCESS_EXE CEF3_BIN_DIR TEXT("/Mac/TracerWebHelper")
#	endif
#	if PLATFORM_MAC
#     include "include/wrapper/cef_library_loader.h"
#     define CEF3_FRAMEWORK_DIR CEF3_BIN_DIR_THIRD_PARTY TEXT("/Mac/Chromium Embedded Framework.framework")
#     define CEF3_FRAMEWORK_EXE CEF3_FRAMEWORK_DIR TEXT("/Chromium Embedded Framework")

#     define CEF3_BUNDLE_DIR TEXT("../Frameworks/Chromium Embedded Framework.framework")
#     define CEF3_BUNDLE_EXE CEF3_BUNDLE_DIR TEXT("/Chromium Embedded Framework")
#	endif
namespace WebBrowserUtils
{
#if PLATFORM_WINDOWS
    void* TracerHandle  = nullptr;
    void* CEF3DLLHandle = nullptr;
	void* ElfHandle     = nullptr;
	void* D3DHandle     = nullptr;
	void* DXHandle      = nullptr;
	void* DXILHandle    = nullptr;
	void* SwiftHandle   = nullptr;
	void* VulkanHandle  = nullptr;
	void* GLESHandle    = nullptr;
    void* EGLHandle     = nullptr;
	FString DllPath;
#elif PLATFORM_MAC
	// Dynamically load the CEF framework library.
	CefScopedLibraryLoader *CEFLibraryLoader = nullptr;
	FString FrameworkPath;
#endif

	void* LoadDllCEF(const FString& Path)
	{
		if (Path.IsEmpty())
		{
			return nullptr;
		}
		void* Handle = FPlatformProcess::GetDllHandle(*Path);
		if (!Handle)
		{
			int32 ErrorNum = FPlatformMisc::GetLastError();
			TCHAR ErrorMsg[1024];
			FPlatformMisc::GetSystemErrorMessage(ErrorMsg, 1024, ErrorNum);
			UE_LOG(LogWebBrowserUtils, Error, TEXT("Failed to get CEF3 DLL handle for %s: %s (%d)"), *Path, ErrorMsg, ErrorNum);
		}
		return Handle;
	}

	bool LoadCEF3Modules(bool bIsMainApp)
	{
#if PLATFORM_WINDOWS
	#if PLATFORM_64BITS
		#if PLATFORM_CPU_ARM_FAMILY && !defined(_M_ARM64EC)
			DllPath = FPaths::Combine(*FPaths::EngineDir(), TEXT("Binaries/ThirdParty/CEF3/WinArm64/"), TEXT(CEF_VERSION));
		#else
			DllPath = FPaths::Combine(*FPaths::EngineDir(), CEF3_BIN_DIR_THIRD_PARTY TEXT("/Win64"));
		#endif
	#else
		DllPath = FPaths::Combine(*FPaths::EngineDir(), CEF3_BIN_DIR_THIRD_PARTY TEXT("/Win32"));
	#endif

		if (bIsMainApp)
			DllPath = FixupPluginPath(DllPath);

		FPlatformProcess::PushDllDirectory(*DllPath);
		CEF3DLLHandle = LoadDllCEF(FPaths::Combine(*DllPath, TEXT("libcef.dll")));
		if (CEF3DLLHandle)
		{
			ElfHandle    = LoadDllCEF(FPaths::Combine(*DllPath, TEXT("chrome_elf.dll")));
			D3DHandle    = LoadDllCEF(FPaths::Combine(*DllPath, TEXT("d3dcompiler_47.dll")));
			DXHandle     = LoadDllCEF(FPaths::Combine(*DllPath, TEXT("dxcompiler.dll")));
			DXILHandle   = LoadDllCEF(FPaths::Combine(*DllPath, TEXT("dxil.dll")));
			SwiftHandle  = LoadDllCEF(FPaths::Combine(*DllPath, TEXT("vk_swiftshader.dll")));
			VulkanHandle = LoadDllCEF(FPaths::Combine(*DllPath, TEXT("vulkan-1.dll")));
			GLESHandle   = LoadDllCEF(FPaths::Combine(*DllPath, TEXT("libGLESv2.dll")));
			EGLHandle    = LoadDllCEF(FPaths::Combine(*DllPath, TEXT("libEGL.dll")));
		}
		FPlatformProcess::PopDllDirectory(*DllPath);

	#if PLATFORM_64BITS
		DllPath = FPaths::Combine(*FPaths::EngineDir(), CEF3_BIN_DIR TEXT("/Win64"));
	#else
		DllPath = FPaths::Combine(*FPaths::EngineDir(), CEF3_BIN_DIR TEXT("/Win32"));
	#endif

		if (bIsMainApp)
			DllPath = FixupPluginPath(DllPath);

		if (!IsRunningCommandlet())
		{
			FPlatformProcess::PushDllDirectory(*DllPath);
			TracerHandle = FPlatformProcess::GetDllHandle(*FPaths::Combine(*DllPath, TEXT("TracerWebAcceleratedPaint.dll")));
			FPlatformProcess::PopDllDirectory(*DllPath);
		}

		return CEF3DLLHandle != nullptr;
#elif PLATFORM_MAC
		// Dynamically load the CEF framework library.
		CEFLibraryLoader = new CefScopedLibraryLoader();
		
		// look for proper framework bundle, and failing that, fall back to old location
		FrameworkPath = FPaths::Combine(FPaths::GetPath(FPlatformProcess::ExecutablePath()), CEF3_BUNDLE_EXE);
		if (!FPaths::FileExists(FrameworkPath))
		{
			FrameworkPath = (FPaths::Combine(*FPaths::EngineDir(), CEF3_FRAMEWORK_EXE));
		}
		FrameworkPath = FPaths::ConvertRelativePathToFull(FrameworkPath);
		if (bIsMainApp)
			FrameworkPath = FixupPluginPath(FrameworkPath);
		
		bool bLoaderInitialized = false;
		if (bIsMainApp)
		{
			// first look in standard Frameworks dir, then look in old UE path
			bLoaderInitialized = CEFLibraryLoader->LoadInMain(TCHAR_TO_ANSI(*FrameworkPath));
			if (!bLoaderInitialized)
			{
				UE_LOG(LogWebBrowserUtils, Error, TEXT("Chromium loader initialization failed"));
			}
		}
		else
		{
			bLoaderInitialized = CEFLibraryLoader->LoadInHelper(TCHAR_TO_ANSI(*FrameworkPath));
			if (!bLoaderInitialized)
			{
				UE_LOG(LogWebBrowserUtils, Error, TEXT("Chromium helper loader initialization failed"));
			}
		}
		return bLoaderInitialized;
#elif PLATFORM_LINUX
		return true;
#else
		return false;
#endif
	}

	void UnloadCEF3Modules()
	{
#if PLATFORM_WINDOWS
		if (!IsRunningCommandlet())
		FPlatformProcess::FreeDllHandle(TracerHandle);  TracerHandle  = nullptr;
		FPlatformProcess::FreeDllHandle(CEF3DLLHandle); CEF3DLLHandle = nullptr;
		FPlatformProcess::FreeDllHandle(ElfHandle);     ElfHandle     = nullptr;
		FPlatformProcess::FreeDllHandle(D3DHandle);     D3DHandle     = nullptr;
		FPlatformProcess::FreeDllHandle(DXHandle);      DXHandle      = nullptr;
		FPlatformProcess::FreeDllHandle(DXILHandle);    DXILHandle    = nullptr;
		FPlatformProcess::FreeDllHandle(SwiftHandle);   SwiftHandle   = nullptr;
		FPlatformProcess::FreeDllHandle(VulkanHandle);  VulkanHandle  = nullptr;
		FPlatformProcess::FreeDllHandle(GLESHandle);    GLESHandle    = nullptr;
		FPlatformProcess::FreeDllHandle(EGLHandle);     EGLHandle     = nullptr;
#elif PLATFORM_MAC
		delete CEFLibraryLoader;
		CEFLibraryLoader = nullptr;
#endif
	}

#if PLATFORM_WINDOWS
	WEBBROWSERUTILS_API void* GetCEF3ModuleHandle()
	{
		return CEF3DLLHandle;
	}
#endif
	
	FString GetCEF3ModulePath()
	{
#if PLATFORM_WINDOWS
		return DllPath;
#elif PLATFORM_MAC
		return FrameworkPath;
#else
		return FString();
#endif
	}

	void BackupCEF3Logfile(const FString& LogFilePath)
	{
		const FString Cef3LogFile = FPaths::Combine(*LogFilePath,TEXT("cef3.log"));
		IFileManager& FileManager = IFileManager::Get();
		if (FileManager.FileSize(*Cef3LogFile) > 0) // file exists and is not empty
		{
			FString Name, Extension;
			FString(Cef3LogFile).Split(TEXT("."), &Name, &Extension, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			FDateTime OriginalTime = FileManager.GetTimeStamp(*Cef3LogFile);
			FString BackupFilename = FString::Printf(TEXT("%s%s%s.%s"), *Name, BACKUP_LOG_FILENAME_POSTFIX, *OriginalTime.ToString(), *Extension);
			// do not retry resulting in an error if log still in use
			if (!FileManager.Move(*BackupFilename, *Cef3LogFile, false, false, false, true))
			{
				UE_LOG(LogWebBrowserUtils, Warning, TEXT("Failed to backup cef3.log"));
			}
		}
	}

#if UE_EDITOR
	bool CheckCEF3Modules()
	{
		if (FindPluginDirectory().IsEmpty())
			return false;

		FString ThirdPartyPath(FPaths::Combine(*FPaths::EngineDir(), CEF3_BIN_DIR_THIRD_PARTY));
		ThirdPartyPath = FixupPluginPath(FPaths::ConvertRelativePathToFull(ThirdPartyPath));
		
		IFileManager& FileManager = IFileManager::Get();
		return FileManager.DirectoryExists(*ThirdPartyPath);
	}

	bool CheckCEF3Helper()
	{
		if (FindPluginDirectory().IsEmpty())
			return false;

		FString SubProcessPath(FPaths::Combine(*FPaths::EngineDir(), CEF3_SUBPROCESS_EXE));
		SubProcessPath = FixupPluginPath(FPaths::ConvertRelativePathToFull(SubProcessPath));
		
		IFileManager& FileManager = IFileManager::Get();
		return FileManager.FileExists(*SubProcessPath);
	}
#endif

	bool RenameCEF3Helper()
	{
		if (FindPluginDirectory().IsEmpty())
			return false;

		FString SubProcessPath(FPaths::Combine(*FPaths::EngineDir(), CEF3_SUBPROCESS_EXE));
		SubProcessPath = FixupPluginPath(FPaths::ConvertRelativePathToFull(SubProcessPath));
		
		IFileManager& FileManager = IFileManager::Get();
		if (FileManager.FileExists(*SubProcessPath))
			return true;

		FString FilePath = FPaths::ChangeExtension(SubProcessPath, "");
		if (!FileManager.FileExists(*FilePath) || FilePath == SubProcessPath)
			return true;

		return FileManager.Move(*SubProcessPath, *FilePath, true, true);
	}

	bool CanSupportAcceleratedPaint()
	{
		if (IsRunningCommandlet())
			return false;

		bool bForceDisableAcceleratedPaint = false;
		if (GConfig->GetBool(TEXT("/Script/WebUI.WebInterfaceSettings"), TEXT("bForceDisableAcceleratedPaint"), bForceDisableAcceleratedPaint, GGameIni))
		{
			if (bForceDisableAcceleratedPaint)
				return false;
		}

#if PLATFORM_WINDOWS || PLATFORM_MAC
		if (!IsTracerAcceleratedPaintSupported())
			return false;
#endif

#if WITH_ENGINE && ENGINE_VERSION >= 501
		switch (RHIGetInterfaceType())
		{
			case ERHIInterfaceType::D3D12:
			case ERHIInterfaceType::D3D11:
			case ERHIInterfaceType::Metal:
				return true;
			default:
				return false;
		}
#else
		static bool sbD3D12Renderer = TCString<TCHAR>::Stricmp(GDynamicRHI->GetName(), TEXT("D3D12")) == 0;
		static bool sbD3D11Renderer = TCString<TCHAR>::Stricmp(GDynamicRHI->GetName(), TEXT("D3D11")) == 0;
		static bool sbMetalRenderer = TCString<TCHAR>::Stricmp(GDynamicRHI->GetName(), TEXT("Metal")) == 0;
		return sbD3D12Renderer || sbD3D11Renderer || sbMetalRenderer;
#endif
	}
	
	FString FindPluginDirectory()
	{
		static FString PluginDir = "";
		if (PluginDir.IsEmpty())
		{
			PluginDir = IPluginManager::Get().FindPlugin(TEXT("WebUI"))->GetBaseDir();
			if (PluginDir.IsEmpty())
				return FString();
		}

		FString Search = TEXT("Engine/Plugins/Marketplace/");
		int32   Index  = PluginDir.Find(Search, ESearchCase::IgnoreCase);
		if (Index < 0)
			return FPaths::GetCleanFilename(PluginDir);

		FString PluginFolder = PluginDir.Mid(Index + Search.Len());
		Index = PluginFolder.Find("/");
		if (Index > 0)
			PluginFolder = PluginFolder.Left(Index);

		return PluginFolder;
	}

	FString FixupPluginPath(const FString& Path)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("WebUI"));
		if (!Plugin.IsValid())
			return Path;

		const FString Marker = TEXT("Plugins/Marketplace/WebUI/");
		const int32 Index = Path.Find(Marker, ESearchCase::IgnoreCase);
		if (Index == INDEX_NONE)
			return Path;

		return FPaths::Combine(FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir()), Path.Mid(Index + Marker.Len()));
	}
	bool HasKey()
	{
		FString LicenseKey = GetLicenseKey();
		if (!LicenseKey.IsEmpty())
			return true;

		FString ProjectKey = GetProjectKey();
		if (!ProjectKey.IsEmpty())
			return true;

		return false;
	}

	FString GetLicenseKey()
	{
		FString LicenseKey;
		if (GConfig->GetString(TEXT("/Script/WebUI.WebInterfaceSettings"), TEXT("LicenseKey"), LicenseKey, GGameIni))
			return LicenseKey;

		return FString();
	}

	FString GetProjectKey()
	{
		FString ProjectKey;
		if (GConfig->GetString(TEXT("/Script/WebUI.WebInterfaceSettings"), TEXT("ProjectKey"), ProjectKey, GGameIni))
			return ProjectKey;

		return FString();
	}

	void RegisterLicenseForProject()
	{
#if !UE_EDITOR
		FString LicenseKey = GetLicenseKey();
		FString ProjectKey = GetProjectKey();
		if (LicenseKey.IsEmpty())
			return;

#if PLATFORM_WINDOWS || PLATFORM_MAC
		SetTracerAcceleratedPaintLicenseKey(TCHAR_TO_UTF8(*LicenseKey), TCHAR_TO_UTF8(*ProjectKey));
#endif
#endif
	}

	bool VerifyLicenseForProject()
	{
		FString LicenseKey = GetLicenseKey();
		FString ProjectKey = GetProjectKey();
		if (LicenseKey.IsEmpty() || ProjectKey.IsEmpty())
			return false;

		TArray<uint8> LicenseKeyBytes;
		LicenseKeyBytes.SetNumZeroed(LicenseKey.Len() / 2);
		if (HexToBytes(LicenseKey, LicenseKeyBytes.GetData()) != LicenseKeyBytes.Num())
			return false;

		TArray<uint8> ProjectKeyBytes;
		ProjectKeyBytes.SetNumZeroed(ProjectKey.Len() / 2);
		if (HexToBytes(ProjectKey, ProjectKeyBytes.GetData()) != ProjectKeyBytes.Num())
			return false;

		FString ProjectName;
		for (int32 i = 0; i < LicenseKeyBytes.Num() && i < ProjectKeyBytes.Num(); ++i)
		{
			TCHAR ProjectChar = LicenseKeyBytes[i] ^ ProjectKeyBytes[i];
			if (ProjectChar == '\0')
				break;

			ProjectName.AppendChar(ProjectChar);
		}

		if (ProjectName.IsEmpty())
			return false;

#if UE_EDITOR
		FString AppName = FApp::GetProjectName();
		if (AppName.IsEmpty())
			return true;
#else
		FString AppName = FPlatformProcess::ExecutableName(true);
		if (AppName.IsEmpty())
			return false;

		int32 Index;
		if (AppName.FindChar('-', Index))
			AppName = AppName.Left(Index);
		if (AppName.Len() > ProjectName.Len())
			AppName = AppName.Left(ProjectName.Len());
#endif

		return ProjectName == AppName;
	}
};
#endif // WITH_CEF3
