// Copyright 2025 Tracer Interactive, LLC. All Rights Reserved.
#include "WebInterfaceBrowserModule.h"
#include "WebInterfaceBrowserLog.h"
#include "WebInterfaceBrowserSingleton.h"
#include "Misc/App.h"
#include "Misc/CoreMisc.h"
#include "Misc/EngineVersion.h"
#include "Misc/Paths.h"
#if WITH_CEF3
#if PLATFORM_WINDOWS || PLATFORM_MAC
#include "WebBrowserUtils.h"
#else
#include "CEF3Utils.h"
#endif
#	if PLATFORM_WINDOWS
#		include "Windows/AllowWindowsPlatformTypes.h"
#	endif
#	pragma push_macro("OVERRIDE")
#		undef OVERRIDE // cef headers provide their own OVERRIDE macro
THIRD_PARTY_INCLUDES_START
#if PLATFORM_APPLE
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#endif
#		include "include/cef_app.h"
#		include "include/cef_version.h"
#		if PLATFORM_MAC
#			include "include/wrapper/cef_library_loader.h"
#		endif
#if PLATFORM_APPLE
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#endif
THIRD_PARTY_INCLUDES_END
#	pragma pop_macro("OVERRIDE")
#	if PLATFORM_WINDOWS
#		include "Windows/HideWindowsPlatformTypes.h"
#	endif
#endif

DEFINE_LOG_CATEGORY(LogWebInterfaceBrowser);

static FWebInterfaceBrowserSingleton* WebBrowserSingleton = nullptr;

FWebInterfaceBrowserInitSettings::FWebInterfaceBrowserInitSettings()
{
#if WITH_CEF3
	ProductVersion = FString::Printf(TEXT("%s/%s UnrealEngine/%s Chrome/%d.%d.%d.%d"), FApp::GetProjectName(), FApp::GetBuildVersion(), *FEngineVersion::Current().ToString(EVersionComponent::Patch), CHROME_VERSION_MAJOR,
		                                                                                                                                                                                               CHROME_VERSION_MINOR,
		                                                                                                                                                                                               CHROME_VERSION_BUILD,
		                                                                                                                                                                                               CHROME_VERSION_PATCH);
#else
	ProductVersion = FString::Printf(TEXT("%s/%s UnrealEngine/%s"), FApp::GetProjectName(), FApp::GetBuildVersion(), *FEngineVersion::Current().ToString(EVersionComponent::Patch));
#endif
}

class FWebInterfaceBrowserModule : public IWebInterfaceBrowserModule
{
private:
	// IModuleInterface Interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

public:
	virtual bool IsWebModuleAvailable() const override;
	virtual IWebInterfaceBrowserSingleton* GetSingleton() override;
	virtual bool CustomInitialize(const FWebInterfaceBrowserInitSettings& WebBrowserInitSettings) override;

private:
#if WITH_CEF3
	bool bLoadedCEFModule = false;
#if PLATFORM_MAC
	// Dynamically load the CEF framework library.
	CefScopedLibraryLoader *CEFLibraryLoader = nullptr;
#endif
#endif
};

IMPLEMENT_MODULE( FWebInterfaceBrowserModule, WebBrowserUI );

void FWebInterfaceBrowserModule::StartupModule()
{
#if WITH_CEF3
	if (!IsRunningCommandlet())
	{
#if PLATFORM_WINDOWS || PLATFORM_MAC
	#if UE_EDITOR
		checkf(WebBrowserUtils::CheckCEF3Modules(), TEXT("WebUI PLUGIN NOT IN /Engine/Plugins/Marketplace DIRECTORY!"));
	#endif
	#if PLATFORM_WINDOWS
		checkf(WebBrowserUtils::RenameCEF3Helper(), TEXT("TracerWebHelper IN /Binaries/Win64 DIRECTORY MUST BE RENAMED TO TracerWebHelper.exe"));
	#endif
	#if UE_EDITOR
#if PLATFORM_WINDOWS
			checkf(WebBrowserUtils::CheckCEF3Helper(), TEXT("TracerWebHelper.exe NOT IN /Binaries/Win64 DIRECTORY!"));
#elif PLATFORM_MAC
			checkf(WebBrowserUtils::CheckCEF3Helper(), TEXT("TracerWebHelper NOT IN /Binaries/Mac DIRECTORY!"));
#elif PLATFORM_LINUX
			checkf(WebBrowserUtils::CheckCEF3Helper(), TEXT("TracerWebHelper NOT IN /Binaries/Linux DIRECTORY!"));
#else
			checkf(WebBrowserUtils::CheckCEF3Helper(), TEXT("TracerWebHelper NOT IN /Binaries DIRECTORY!"));
#endif
	#endif

		WebBrowserUtils::BackupCEF3Logfile(FPaths::ProjectLogDir());
#elif ENGINE_VERSION >= 501
		CEF3Utils::BackupCEF3Logfile(FPaths::ProjectLogDir());
#endif
	}
#if PLATFORM_WINDOWS || PLATFORM_MAC
	bLoadedCEFModule = WebBrowserUtils::LoadCEF3Modules(true);
	if (bLoadedCEFModule)
	{
		WebBrowserUtils::RegisterLicenseForProject();
		if (WebBrowserUtils::CanSupportAcceleratedPaint())
		{
			const bool bHasKey = WebBrowserUtils::HasKey();
			if (bHasKey && !WebBrowserUtils::VerifyLicenseForProject())
			{
				FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, TEXT("Invalid WebUI License Key! Please remove the LicenseKey and ProjectKey settings from your DefaultEngine.ini config."), TEXT("Error"));
			}
	#if !UE_EDITOR
			else if (!bHasKey)
				FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, TEXT("Missing WebUI License Key! You must obtain a license or disable \"Accelerated Paint\" in the Project Settings under the \"Plugins > WebUI\" section."), TEXT("Error"));
	#endif
		}
	}
#else
	bLoadedCEFModule = CEF3Utils::LoadCEF3Modules(true);
#endif
#if PLATFORM_MAC
	// Dynamically load the CEF framework library into this dylibs memory space.
	// CEF now loads function pointers at runtime so we need this to be dylib specific.
	CEFLibraryLoader = new CefScopedLibraryLoader();
	if (!CEFLibraryLoader->LoadInMain(TCHAR_TO_ANSI(*WebBrowserUtils::GetCEF3ModulePath())))
	{
		UE_LOG(LogWebInterfaceBrowser, Error, TEXT("Chromium loader initialization failed"));
	}
#endif // PLATFORM_MAC
#endif
}

void FWebInterfaceBrowserModule::ShutdownModule()
{
	if (WebBrowserSingleton != nullptr)
	{
		delete WebBrowserSingleton;
		WebBrowserSingleton = nullptr;
	}

#if WITH_CEF3
#if PLATFORM_WINDOWS || PLATFORM_MAC
	WebBrowserUtils::UnloadCEF3Modules();
#else
	CEF3Utils::UnloadCEF3Modules();
#endif
#if PLATFORM_MAC
	delete CEFLibraryLoader;
	CEFLibraryLoader = nullptr;
#endif // PLATFORM_MAC
#endif
}

bool FWebInterfaceBrowserModule::CustomInitialize(const FWebInterfaceBrowserInitSettings& WebBrowserInitSettings)
{
	if (WebBrowserSingleton == nullptr)
	{
		WebBrowserSingleton = new FWebInterfaceBrowserSingleton(WebBrowserInitSettings);
		return true;
	}
	return false;
}

IWebInterfaceBrowserSingleton* FWebInterfaceBrowserModule::GetSingleton()
{
	if (WebBrowserSingleton == nullptr)
	{
		WebBrowserSingleton = new FWebInterfaceBrowserSingleton(FWebInterfaceBrowserInitSettings());
	}
	return WebBrowserSingleton;
}


bool FWebInterfaceBrowserModule::IsWebModuleAvailable() const
{
#if WITH_CEF3
	return bLoadedCEFModule;
#else
	return true;
#endif
}
