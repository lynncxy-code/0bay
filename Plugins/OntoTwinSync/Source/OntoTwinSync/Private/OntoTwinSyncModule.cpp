// OntoTwinSync 插件模块入口。Renderer Spike 必须在首个 World 初始化前预留共享 Slate Postbuffer。

#include "Modules/ModuleManager.h"
#include "HttpModule.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "UI/OntoTwinGlassRenderer.h"

namespace
{
void EnsureLoopbackHttpBypassesProxy()
{
	FString HttpNoProxy = FHttpModule::Get().GetHttpNoProxy();
	TArray<FString> Entries;
	HttpNoProxy.ParseIntoArray(Entries, TEXT(","), true);

	auto AddIfMissing = [&Entries](const TCHAR* Host)
	{
		const bool bExists = Entries.ContainsByPredicate([Host](const FString& Entry)
		{
			return Entry.TrimStartAndEnd().Equals(Host, ESearchCase::IgnoreCase);
		});
		if (!bExists)
		{
			Entries.Add(Host);
		}
	};

	AddIfMissing(TEXT("127.0.0.1"));
	AddIfMissing(TEXT("localhost"));
	AddIfMissing(TEXT("::1"));

	const FString MergedNoProxy = FString::Join(Entries, TEXT(","));
	if (MergedNoProxy == HttpNoProxy)
	{
		return;
	}

	// FHttpModule reloads HttpNoProxy when the HTTP config section changes. Updating it
	// here keeps local OntoTwin traffic away from an OS proxy without disabling the proxy
	// for unrelated external UE requests.
	GConfig->SetString(TEXT("HTTP"), TEXT("HttpNoProxy"), *MergedNoProxy, GEngineIni);
	TSet<FString> ChangedSections;
	ChangedSections.Add(TEXT("HTTP"));
	FCoreDelegates::TSOnConfigSectionsChanged().Broadcast(GEngineIni, ChangedSections);

	UE_LOG(LogTemp, Log,
		TEXT("[OntoTwinHTTP] 已自动绕过本机 HTTP 代理: %s"),
		*MergedNoProxy);
}
}

class FOntoTwinSyncModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		EnsureLoopbackHttpBypassesProxy();
		// Slate creates configured processors during pre-world initialization, so reserve the slot now.
		FOntoTwinGlassRenderer::ConfigureSlatePostBuffer();
	}
};

IMPLEMENT_MODULE(FOntoTwinSyncModule, OntoTwinSync)
