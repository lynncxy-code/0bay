#include "OntoTwinSyncEditor.h"
#include "OntoTwinBuildIdentity.h"
#include "TwinSceneManager.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Interfaces/IPluginManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/MessageDialog.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/FileManager.h"

namespace
{
void LaunchOntoTwinTool(const TCHAR* Mode)
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("OntoTwinSync"));
	if (!Plugin) return;
	const FString Python = FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Binaries/ThirdParty/Python3/Win64/pythonw.exe"));
	const FString Script = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Tools/ontotwin_delivery.py"));
	if (!FPaths::FileExists(Python) || !FPaths::FileExists(Script))
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("交付工具或 UE 内置 Python 缺失。请完整更新 OntoTwinSync 插件；本工具支持 Windows UE 5.6。")));
		return;
	}
	auto Context = MakeShared<FJsonObject>();
	Context->SetStringField(TEXT("project"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
	Context->SetStringField(TEXT("plugin"), FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir()));
	Context->SetStringField(TEXT("engine"), FPaths::ConvertRelativePathToFull(FPaths::EngineDir()));
	Context->SetStringField(TEXT("loaded_hash"), OntoTwinLoadedSourceHash());
	Context->SetNumberField(TEXT("editor_pid"), FPlatformProcess::GetCurrentProcessId());
	Context->SetStringField(TEXT("mode"), Mode);
	if (UWorld* World = GEditor->GetEditorWorldContext().World())
	{
		int32 Count = 0;
		for (TActorIterator<ATwinSceneManager> It(World); It; ++It)
		{
			++Count;
			Context->SetStringField(TEXT("backend"), It->BackendBaseUrl);
			Context->SetStringField(TEXT("ue_id"), It->UEProjectId);
		}
		// Never guess which manager's dataset to deliver.
		if (Count != 1) Context->SetStringField(TEXT("ue_id"), TEXT(""));
	}
	const FString Folder = FPaths::ProjectSavedDir() / TEXT("OntoTwinDelivery");
	IFileManager::Get().MakeDirectory(*Folder, true);
	const FString ContextFile = FPaths::ConvertRelativePathToFull(Folder / (FGuid::NewGuid().ToString() + TEXT(".json")));
	FString Json;
	FJsonSerializer::Serialize(Context, TJsonWriterFactory<>::Create(&Json));
	if (!FFileHelper::SaveStringToFile(Json, *ContextFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) return;
	const FString Args = FString::Printf(TEXT("\"%s\" --context \"%s\" --mode %s"), *Script, *ContextFile, Mode);
	FProcHandle Process = FPlatformProcess::CreateProc(*Python, *Args, true, false, false, nullptr, 0, nullptr, nullptr);
	if (Process.IsValid()) FPlatformProcess::CloseProc(Process);
	else FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("无法启动交付工具。请检查 UE 内置 Python。")));
}
}

void FOntoTwinSyncEditorModule::OpenPluginVersion()
{
	LaunchOntoTwinTool(TEXT("version"));
}

void FOntoTwinSyncEditorModule::OpenDeliveryProject()
{
	LaunchOntoTwinTool(TEXT("export"));
}
