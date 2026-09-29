#include "OfficeVR/OfficeAgentAdapter.h"
#include "OfficeHost.h"
#include "OfficeVR/OfficeVRPawn.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

static TAutoConsoleVariable<FString> CVarSidecarUrl(
	TEXT("officevr.SidecarUrl"),
	TEXT("http://127.0.0.1:8095"),
	TEXT("Office agent sidecar base URL"),
	ECVF_Default);

static void OfficeVRSayCommand(const TArray<FString>& Args)
{
	const FString Text = FString::Join(Args, TEXT(" ")).TrimStartAndEnd();
	if (Text.IsEmpty() || !GEngine)
	{
		UE_LOG(LogOfficeVR, Warning, TEXT("officevr.Say needs text, e.g. officevr.Say 你好"));
		return;
	}

	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		UWorld* World = Context.World();
		if (!World || (World->WorldType != EWorldType::Game && World->WorldType != EWorldType::PIE))
		{
			continue;
		}
		for (TActorIterator<AOfficeVRPawn> It(World); It; ++It)
		{
			It->SayToActiveAgent(Text);
			return;
		}
	}
	UE_LOG(LogOfficeVR, Warning, TEXT("officevr.Say: no OfficeVRPawn"));
}

static FAutoConsoleCommand OfficeVRSayConsole(
	TEXT("officevr.Say"),
	TEXT("Send typed text to the active office agent sidecar"),
	FConsoleCommandWithArgsDelegate::CreateStatic(&OfficeVRSayCommand));

FOfficeAgentAdapter& FOfficeAgentAdapter::Get()
{
	static FOfficeAgentAdapter Instance;
	return Instance;
}

FString FOfficeAgentAdapter::GetSidecarUrl() const
{
	return CVarSidecarUrl.GetValueOnGameThread().TrimStartAndEnd();
}

void FOfficeAgentAdapter::PostJson(const FString& Path, const FString& Body, FOfficeAgentHttpDone OnDone) const
{
	const FString Url = GetSidecarUrl() + Path;
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json; charset=utf-8"));
	Request->SetContentAsString(Body);
	Request->SetTimeout(180.f);
	Request->OnProcessRequestComplete().BindLambda(
		[OnDone](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
			if (!bConnected || !Response.IsValid())
			{
				OnDone.ExecuteIfBound(false, TEXT("sidecar unreachable. Run StartOfficeVR.bat"));
				return;
			}
			const int32 Code = Response->GetResponseCode();
			const FString Content = Response->GetContentAsString();
			OnDone.ExecuteIfBound(Code >= 200 && Code < 300, Content);
		});
	if (!Request->ProcessRequest())
	{
		OnDone.ExecuteIfBound(false, TEXT("http request failed to start"));
	}
}

void FOfficeAgentAdapter::BeginTalk(const FString& AgentId, FOfficeAgentHttpDone OnDone)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("agent_id"), AgentId);
	FString Body;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
	PostJson(TEXT("/v1/session/begin"), Body, MoveTemp(OnDone));
}

void FOfficeAgentAdapter::PushText(const FString& Text, FOfficeAgentHttpDone OnDone)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("text"), Text);
	FString Body;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
	PostJson(TEXT("/v1/session/prompt"), Body, MoveTemp(OnDone));
}

void FOfficeAgentAdapter::StartListen(FOfficeAgentHttpDone OnDone)
{
	PostJson(TEXT("/v1/session/listen/start"), TEXT("{}"), MoveTemp(OnDone));
}

void FOfficeAgentAdapter::StopListen(FOfficeAgentHttpDone OnDone)
{
	PostJson(TEXT("/v1/session/listen/stop"), TEXT("{}"), MoveTemp(OnDone));
}

void FOfficeAgentAdapter::EndTalk(FOfficeAgentHttpDone OnDone)
{
	PostJson(TEXT("/v1/session/end"), TEXT("{}"), MoveTemp(OnDone));
}

void FOfficeAgentAdapter::FetchTtsWav(FOfficeAgentBinaryDone OnDone)
{
	const FString Url = GetSidecarUrl() + TEXT("/v1/tts/last.wav");
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(TEXT("GET"));
	Request->SetTimeout(30.f);
	Request->OnProcessRequestComplete().BindLambda(
		[OnDone](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
			if (!bConnected || !Response.IsValid() || Response->GetResponseCode() < 200 || Response->GetResponseCode() >= 300)
			{
				OnDone.ExecuteIfBound(false, TArray<uint8>());
				return;
			}
			OnDone.ExecuteIfBound(true, Response->GetContent());
		});
	if (!Request->ProcessRequest())
	{
		OnDone.ExecuteIfBound(false, TArray<uint8>());
	}
}
