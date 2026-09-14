#include "UEAssetCatalogSyncComponent.h"

#include "TwinSceneManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Dom/JsonObject.h"
#include "Engine/Blueprint.h"
#include "Engine/Engine.h"
#include "Engine/LevelScriptActor.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "HAL/PlatformMisc.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/App.h"
#include "Misc/Base64.h"
#include "Misc/CommandLine.h"
#include "Misc/ObjectThumbnail.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#if WITH_EDITOR
#include "ObjectTools.h"
#include "Settings/ProjectPackagingSettings.h"
#endif


namespace
{
FString NormalizeRoot(FString Value)
{
    Value.TrimStartAndEndInline();
    Value.ReplaceInline(TEXT("\\"), TEXT("/"));
    while (Value.EndsWith(TEXT("/")))
    {
        Value.LeftChopInline(1);
    }
    return Value;
}

FString ThumbnailDataUrl(const FAssetData& AssetData, const bool bEnabled)
{
#if WITH_EDITOR
    if (!bEnabled)
    {
        return FString();
    }

    FObjectThumbnail Thumbnail;
    if (!ThumbnailTools::LoadThumbnailFromPackage(AssetData, Thumbnail)
        || !Thumbnail.HasValidImageData())
    {
        return FString();
    }

    Thumbnail.CompressImageData();
    const TArray<uint8>& Compressed = Thumbnail.AccessCompressedImageData();
    if (Compressed.Num() < 4)
    {
        return FString();
    }

    const bool bPng = Compressed[0] == 0x89 && Compressed[1] == 0x50
        && Compressed[2] == 0x4e && Compressed[3] == 0x47;
    const bool bJpeg = Compressed[0] == 0xff && Compressed[1] == 0xd8;
    if (!bPng && !bJpeg)
    {
        return FString();
    }
    return FString::Printf(
        TEXT("data:%s;base64,%s"),
        bPng ? TEXT("image/png") : TEXT("image/jpeg"),
        *FBase64::Encode(Compressed));
#else
    return FString();
#endif
}

FString ResolveBackendUrl(const ATwinSceneManager* Manager)
{
    FString BackendUrl = Manager ? Manager->BackendBaseUrl : TEXT("http://localhost:5000");
    FString Override;
    if (FParse::Value(FCommandLine::Get(), TEXT("OntoTwinBackendBaseUrl="), Override))
    {
        Override.TrimStartAndEndInline();
        if (!Override.IsEmpty())
        {
            BackendUrl = Override;
        }
    }
    BackendUrl.RemoveFromEnd(TEXT("/"));
    return BackendUrl;
}

bool IsSupportedActorBlueprintClass(const UClass* GeneratedClass, FString& OutReason)
{
    if (!GeneratedClass || !GeneratedClass->IsChildOf(AActor::StaticClass()))
    {
        OutReason = TEXT("生成类不是 AActor");
        return false;
    }
    if (GeneratedClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
    {
        OutReason = TEXT("生成类不可实例化或已废弃");
        return false;
    }
    if (GeneratedClass->IsChildOf(APawn::StaticClass())
        || GeneratedClass->IsChildOf(AController::StaticClass())
        || GeneratedClass->IsChildOf(ALevelScriptActor::StaticClass()))
    {
        OutReason = TEXT("Pawn、Controller 与 LevelScriptActor 不属于普通表现 Actor");
        return false;
    }
    OutReason = TEXT("Actor Blueprint");
    return true;
}
}


UOntoTwinUEAssetCatalogSyncComponent::UOntoTwinUEAssetCatalogSyncComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
    bTickInEditor = true;
}


void UOntoTwinUEAssetCatalogSyncComponent::BeginPlay()
{
    Super::BeginPlay();
    PublishFolderIndex();
    if (bSyncOnBeginPlay)
    {
        SyncCatalog();
    }
}


void UOntoTwinUEAssetCatalogSyncComponent::TickComponent(
    const float DeltaTime,
    const ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!bEnableOnDemandCatalog || IsTemplate() || !GetOwner())
    {
        return;
    }

    FolderIndexRefreshElapsed += DeltaTime;
    ScanRequestPollElapsed += DeltaTime;
    if (!bFolderIndexPublished
        || FolderIndexRefreshElapsed >= FMath::Max(15.0f, FolderIndexRefreshIntervalSeconds))
    {
        PublishFolderIndex();
    }
    if (ScanRequestPollElapsed >= FMath::Max(1.0f, ScanRequestPollIntervalSeconds))
    {
        PollScanRequest();
    }
}


void UOntoTwinUEAssetCatalogSyncComponent::PublishFolderIndex()
{
    if (bFolderIndexRequestInFlight || IsTemplate())
    {
        return;
    }

    const ATwinSceneManager* Manager = Cast<ATwinSceneManager>(GetOwner());
    const FString UEProjectName = Manager && !Manager->UEProjectName.IsEmpty()
        ? Manager->UEProjectName
        : FString(FApp::GetProjectName());
    const FString UEProjectId = Manager && !Manager->UEProjectId.IsEmpty()
        ? Manager->UEProjectId
        : FString::Printf(TEXT("ueproj_%s"), *UEProjectName);
    const FString BackendUrl = ResolveBackendUrl(Manager);

    FAssetRegistryModule& AssetRegistryModule =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    TArray<FString> FolderPaths;
    AssetRegistryModule.Get().GetSubPaths(TEXT("/Game"), FolderPaths, true);
    FolderPaths.AddUnique(TEXT("/Game"));
    FolderPaths.RemoveAll([](const FString& Path)
    {
        return Path != TEXT("/Game") && !Path.StartsWith(TEXT("/Game/"));
    });
    FolderPaths.Sort([](const FString& Left, const FString& Right)
    {
        return Left.Compare(Right, ESearchCase::IgnoreCase) < 0;
    });

    TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
    Body->SetStringField(TEXT("ue_project_id"), UEProjectId);
    Body->SetStringField(TEXT("ue_project_name"), UEProjectName);
    TArray<TSharedPtr<FJsonValue>> FoldersJson;
    FoldersJson.Reserve(FolderPaths.Num());
    for (const FString& FolderPath : FolderPaths)
    {
        FoldersJson.Add(MakeShared<FJsonValueString>(NormalizeRoot(FolderPath)));
    }
    Body->SetArrayField(TEXT("folders"), FoldersJson);

    FString BodyString;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyString);
    FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(FString::Printf(TEXT("%s/api/v2/ue/assets/folders"), *BackendUrl));
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->SetHeader(TEXT("X-OntoTwin-UE-Project-Id"), UEProjectId);
    Request->SetHeader(TEXT("X-OntoTwin-UE-Project-Name"), UEProjectName);
    Request->SetContentAsString(BodyString);
    bFolderIndexRequestInFlight = true;
    FolderIndexRefreshElapsed = 0.0f;

    const TWeakObjectPtr<UOntoTwinUEAssetCatalogSyncComponent> WeakThis(this);
    const int32 FolderCount = FolderPaths.Num();
    Request->OnProcessRequestComplete().BindLambda(
        [WeakThis, FolderCount](FHttpRequestPtr, FHttpResponsePtr Response, const bool bOk)
        {
            UOntoTwinUEAssetCatalogSyncComponent* Self = WeakThis.Get();
            if (!Self)
            {
                return;
            }
            Self->bFolderIndexRequestInFlight = false;
            const int32 Code = Response.IsValid() ? Response->GetResponseCode() : -1;
            Self->bFolderIndexPublished = bOk && Response.IsValid() && Code >= 200 && Code < 300;
            if (Self->bFolderIndexPublished)
            {
                UE_LOG(LogTemp, Verbose, TEXT("[UE资产目录] 可选目录已刷新 | folders=%d"), FolderCount);
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("[UE资产目录] 可选目录刷新失败 | code=%d"), Code);
            }
        });
    Request->ProcessRequest();
}


void UOntoTwinUEAssetCatalogSyncComponent::PollScanRequest()
{
    ScanRequestPollElapsed = 0.0f;
    if (bScanPollRequestInFlight || bRequestInFlight || !ActiveScanRequestId.IsEmpty() || IsTemplate())
    {
        return;
    }

    const ATwinSceneManager* Manager = Cast<ATwinSceneManager>(GetOwner());
    const FString UEProjectName = Manager && !Manager->UEProjectName.IsEmpty()
        ? Manager->UEProjectName
        : FString(FApp::GetProjectName());
    const FString UEProjectId = Manager && !Manager->UEProjectId.IsEmpty()
        ? Manager->UEProjectId
        : FString::Printf(TEXT("ueproj_%s"), *UEProjectName);
    const FString BackendUrl = ResolveBackendUrl(Manager);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(FString::Printf(TEXT("%s/api/v2/ue/assets/scan-requests/pending"), *BackendUrl));
    Request->SetVerb(TEXT("GET"));
    Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
    Request->SetHeader(TEXT("X-OntoTwin-UE-Project-Id"), UEProjectId);
    Request->SetHeader(TEXT("X-OntoTwin-UE-Project-Name"), UEProjectName);
    bScanPollRequestInFlight = true;

    const TWeakObjectPtr<UOntoTwinUEAssetCatalogSyncComponent> WeakThis(this);
    Request->OnProcessRequestComplete().BindLambda(
        [WeakThis](FHttpRequestPtr, FHttpResponsePtr Response, const bool bOk)
        {
            UOntoTwinUEAssetCatalogSyncComponent* Self = WeakThis.Get();
            if (!Self)
            {
                return;
            }
            Self->bScanPollRequestInFlight = false;
            if (!bOk || !Response.IsValid() || Response->GetResponseCode() != 200)
            {
                return;
            }

            TSharedPtr<FJsonObject> Result;
            const TSharedRef<TJsonReader<>> Reader =
                TJsonReaderFactory<>::Create(Response->GetContentAsString());
            if (!FJsonSerializer::Deserialize(Reader, Result) || !Result.IsValid())
            {
                return;
            }
            FString Status;
            Result->TryGetStringField(TEXT("status"), Status);
            if (!Status.Equals(TEXT("pending"), ESearchCase::IgnoreCase))
            {
                return;
            }

            FString RequestId;
            FString FolderPath;
            Result->TryGetStringField(TEXT("request_id"), RequestId);
            Result->TryGetStringField(TEXT("folder_path"), FolderPath);
            FolderPath = NormalizeRoot(FolderPath);
            if (RequestId.IsEmpty() || (FolderPath != TEXT("/Game") && !FolderPath.StartsWith(TEXT("/Game/"))))
            {
                return;
            }

            FAssetRegistryModule& AssetRegistryModule =
                FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
            TArray<FString> KnownFolders;
            AssetRegistryModule.Get().GetSubPaths(TEXT("/Game"), KnownFolders, true);
            const bool bFolderExists = FolderPath == TEXT("/Game")
                || KnownFolders.ContainsByPredicate([&FolderPath](const FString& Known)
                {
                    return Known.Equals(FolderPath, ESearchCase::IgnoreCase);
                });
            if (!bFolderExists)
            {
                Self->ReportScanRequestFailure(
                    RequestId,
                    FString::Printf(TEXT("UE 工程中不存在目录 %s"), *FolderPath));
                return;
            }

            Self->ActiveScanRequestId = RequestId;
            Self->SyncCatalogForRoots({FolderPath}, RequestId);
        });
    Request->ProcessRequest();
}


void UOntoTwinUEAssetCatalogSyncComponent::ReportScanRequestFailure(
    const FString& ScanRequestId,
    const FString& ErrorMessage)
{
    if (ScanRequestId.IsEmpty())
    {
        return;
    }
    const ATwinSceneManager* Manager = Cast<ATwinSceneManager>(GetOwner());
    const FString UEProjectName = Manager && !Manager->UEProjectName.IsEmpty()
        ? Manager->UEProjectName
        : FString(FApp::GetProjectName());
    const FString UEProjectId = Manager && !Manager->UEProjectId.IsEmpty()
        ? Manager->UEProjectId
        : FString::Printf(TEXT("ueproj_%s"), *UEProjectName);
    const FString BackendUrl = ResolveBackendUrl(Manager);

    TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
    Body->SetStringField(TEXT("ue_project_id"), UEProjectId);
    Body->SetStringField(TEXT("request_id"), ScanRequestId);
    Body->SetStringField(TEXT("status"), TEXT("failed"));
    Body->SetStringField(TEXT("error"), ErrorMessage);
    FString BodyString;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyString);
    FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(FString::Printf(TEXT("%s/api/v2/ue/assets/scan-requests/result"), *BackendUrl));
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->SetHeader(TEXT("X-OntoTwin-UE-Project-Id"), UEProjectId);
    Request->SetHeader(TEXT("X-OntoTwin-UE-Project-Name"), UEProjectName);
    Request->SetContentAsString(BodyString);
    Request->ProcessRequest();
    ActiveScanRequestId.Empty();
}

void UOntoTwinUEAssetCatalogSyncComponent::ConfigureAssetRootsForCook()
{
#if WITH_EDITOR
    UProjectPackagingSettings* PackagingSettings = GetMutableDefault<UProjectPackagingSettings>();
    if (!PackagingSettings)
    {
        UE_LOG(LogTemp, Error, TEXT("[UE资产目录] 无法读取项目打包设置"));
        return;
    }

    bool bChanged = false;
    PackagingSettings->Modify();
    for (const FDirectoryPath& RawRoot : AssetRoots)
    {
        const FString Root = NormalizeRoot(RawRoot.Path);
        if (!Root.StartsWith(TEXT("/Game")))
        {
            continue;
        }

        const bool bCovered = PackagingSettings->DirectoriesToAlwaysCook.ContainsByPredicate(
            [&Root](const FDirectoryPath& Directory)
            {
                const FString CookRoot = NormalizeRoot(Directory.Path);
                return Root == CookRoot || Root.StartsWith(CookRoot + TEXT("/"));
            });
        if (!bCovered)
        {
            FDirectoryPath Directory;
            Directory.Path = Root;
            PackagingSettings->DirectoriesToAlwaysCook.Add(Directory);
            bChanged = true;
            UE_LOG(LogTemp, Log, TEXT("[UE资产目录] 已加入 Cook 目录: %s"), *Root);
        }
    }

    if (bChanged)
    {
        PackagingSettings->TryUpdateDefaultConfigFile();
        UE_LOG(LogTemp, Log, TEXT("[UE资产目录] 项目 Cook 配置已保存"));
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("[UE资产目录] 扫描目录已具备 Cook 覆盖"));
    }
#else
    UE_LOG(LogTemp, Warning, TEXT("[UE资产目录] Cook 配置只能在编辑器中修改"));
#endif
}


void UOntoTwinUEAssetCatalogSyncComponent::SyncCatalog()
{
    TArray<FString> RequestedRoots;
    RequestedRoots.Reserve(AssetRoots.Num());
    for (const FDirectoryPath& Root : AssetRoots)
    {
        RequestedRoots.Add(Root.Path);
    }
    SyncCatalogForRoots(RequestedRoots, FString());
}


void UOntoTwinUEAssetCatalogSyncComponent::SyncCatalogForRoots(
    const TArray<FString>& RequestedRoots,
    const FString& ScanRequestId)
{
    if (bRequestInFlight)
    {
        UE_LOG(LogTemp, Warning, TEXT("[UE资产目录] 已有同步请求在进行中"));
        return;
    }

    TArray<FString> EffectiveRoots;
    for (const FString& RequestedRoot : RequestedRoots)
    {
        const FString Root = NormalizeRoot(RequestedRoot);
        if ((Root.StartsWith(TEXT("/Game")) || Root.StartsWith(TEXT("/Engine")))
            && !EffectiveRoots.ContainsByPredicate([&Root](const FString& Existing)
            {
                return Existing.Equals(Root, ESearchCase::IgnoreCase);
            }))
        {
            EffectiveRoots.Add(Root);
        }
    }
    if (EffectiveRoots.IsEmpty())
    {
        const FString Error = TEXT("没有有效扫描根目录");
        UE_LOG(LogTemp, Error, TEXT("[UE资产目录] %s"), *Error);
        if (!ScanRequestId.IsEmpty())
        {
            ReportScanRequestFailure(ScanRequestId, Error);
        }
        return;
    }

    const ATwinSceneManager* Manager = Cast<ATwinSceneManager>(GetOwner());
    const FString UEProjectName = Manager && !Manager->UEProjectName.IsEmpty()
        ? Manager->UEProjectName
        : FString(FApp::GetProjectName());
    const FString UEProjectId = Manager && !Manager->UEProjectId.IsEmpty()
        ? Manager->UEProjectId
        : FString::Printf(TEXT("ueproj_%s"), *UEProjectName);
    const FString BackendUrl = ResolveBackendUrl(Manager);

#if WITH_EDITOR
    if (const UProjectPackagingSettings* PackagingSettings = GetDefault<UProjectPackagingSettings>())
    {
        for (const FString& Root : EffectiveRoots)
        {
            if (!Root.StartsWith(TEXT("/Game"))) continue;
            const bool bCovered = PackagingSettings->DirectoriesToAlwaysCook.ContainsByPredicate(
                [&Root](const FDirectoryPath& Directory)
                {
                    const FString CookRoot = NormalizeRoot(Directory.Path);
                    return Root == CookRoot || Root.StartsWith(CookRoot + TEXT("/"));
                });
            if (!bCovered)
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[UE资产目录] 扫描目录未纳入 Cook: %s；打包前请点击“配置扫描目录用于打包”"),
                    *Root);
            }
        }
    }
#endif

    FARFilter Filter;
    Filter.bRecursivePaths = true;
    Filter.bRecursiveClasses = true;
    Filter.bIncludeOnlyOnDiskAssets = true;
    for (const FString& Root : EffectiveRoots)
    {
        if (Root.StartsWith(TEXT("/Game")) || Root.StartsWith(TEXT("/Engine")))
        {
            Filter.PackagePaths.Add(FName(*Root));
        }
    }
    if (Filter.PackagePaths.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("[UE资产目录] 没有有效扫描根目录"));
        return;
    }

    Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
    if (bIncludeUnsupportedKinds)
    {
        Filter.ClassPaths.Add(USkeletalMesh::StaticClass()->GetClassPathName());
        Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
    }

    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    TArray<FAssetData> AssetDataList;
    AssetRegistryModule.Get().GetAssets(Filter, AssetDataList, true);
    AssetDataList.Sort([](const FAssetData& Left, const FAssetData& Right)
    {
        return Left.GetSoftObjectPath().ToString() < Right.GetSoftObjectPath().ToString();
    });

    TArray<TSharedPtr<FJsonValue>> AssetsJson;
    AssetsJson.Reserve(AssetDataList.Num());
    for (const FAssetData& AssetData : AssetDataList)
    {
        const FString AssetKind = AssetData.AssetClassPath.GetAssetName().ToString();
        const bool bStaticMesh = AssetKind.Equals(TEXT("StaticMesh"), ESearchCase::IgnoreCase);
        const bool bSkeletalMesh = AssetKind.Equals(TEXT("SkeletalMesh"), ESearchCase::IgnoreCase);
        const bool bBlueprint = AssetKind.Equals(TEXT("Blueprint"), ESearchCase::IgnoreCase);
        TSharedPtr<FJsonObject> AssetJson = MakeShared<FJsonObject>();
        AssetJson->SetStringField(TEXT("object_path"), AssetData.GetSoftObjectPath().ToString());
        AssetJson->SetStringField(TEXT("package_name"), AssetData.PackageName.ToString());
        AssetJson->SetStringField(TEXT("asset_name"), AssetData.AssetName.ToString());
        AssetJson->SetStringField(TEXT("display_name"), AssetData.AssetName.ToString());
        AssetJson->SetStringField(TEXT("folder_path"), AssetData.PackagePath.ToString());
        AssetJson->SetStringField(TEXT("asset_kind"), AssetKind);

        bool bRuntimeLoadable = bStaticMesh || bSkeletalMesh;
        FString RuntimeReason = bStaticMesh
            ? TEXT("StaticMesh")
            : (bSkeletalMesh ? TEXT("SkeletalMesh（参考姿势）") : TEXT("不支持的资产类型"));
        if (bBlueprint)
        {
            FString GeneratedClassExportPath;
            FString GeneratedClassPath;
            if (AssetData.GetTagValue(FName(TEXT("GeneratedClass")), GeneratedClassExportPath))
            {
                GeneratedClassPath = FPackageName::ExportTextPathToObjectPath(GeneratedClassExportPath);
            }
            if (GeneratedClassPath.IsEmpty())
            {
                if (const UBlueprint* Blueprint = Cast<UBlueprint>(AssetData.GetAsset()))
                {
                    if (Blueprint->GeneratedClass)
                    {
                        GeneratedClassPath = Blueprint->GeneratedClass->GetPathName();
                    }
                }
            }
            UClass* GeneratedClass = GeneratedClassPath.IsEmpty()
                ? nullptr
                : LoadObject<UClass>(nullptr, *GeneratedClassPath);
            bRuntimeLoadable = IsSupportedActorBlueprintClass(GeneratedClass, RuntimeReason);
            if (!GeneratedClassPath.IsEmpty())
            {
                AssetJson->SetStringField(TEXT("generated_class_path"), GeneratedClassPath);
            }
        }
        AssetJson->SetBoolField(TEXT("runtime_loadable"), bRuntimeLoadable);
        AssetJson->SetStringField(TEXT("runtime_reason"), RuntimeReason);

        if ((bStaticMesh || bSkeletalMesh) && bIncludeStaticMeshBounds)
        {
            FVector Size = FVector::ZeroVector;
            if (const UStaticMesh* StaticMeshAsset = Cast<UStaticMesh>(AssetData.GetAsset()))
            {
                Size = StaticMeshAsset->GetBounds().BoxExtent * 2.0;
            }
            else if (const USkeletalMesh* SkeletalMeshAsset = Cast<USkeletalMesh>(AssetData.GetAsset()))
            {
                Size = SkeletalMeshAsset->GetBounds().BoxExtent * 2.0;
            }
            if (!Size.IsNearlyZero())
            {
                TSharedPtr<FJsonObject> SizeJson = MakeShared<FJsonObject>();
                SizeJson->SetNumberField(TEXT("x"), Size.X);
                SizeJson->SetNumberField(TEXT("y"), Size.Y);
                SizeJson->SetNumberField(TEXT("z"), Size.Z);
                AssetJson->SetObjectField(TEXT("size_cm"), SizeJson);
            }
        }

        const FString Thumbnail = ThumbnailDataUrl(AssetData, bIncludeEditorThumbnails);
        if (!Thumbnail.IsEmpty())
        {
            AssetJson->SetStringField(TEXT("thumbnail_data_url"), Thumbnail);
        }
        AssetsJson.Add(MakeShared<FJsonValueObject>(AssetJson));
    }

    TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
    Body->SetStringField(TEXT("ue_project_id"), UEProjectId);
    Body->SetStringField(TEXT("ue_project_name"), UEProjectName);
    TArray<TSharedPtr<FJsonValue>> RootsJson;
    for (const FString& Root : EffectiveRoots)
    {
        RootsJson.Add(MakeShared<FJsonValueString>(Root));
    }
    Body->SetArrayField(TEXT("roots"), RootsJson);
    Body->SetArrayField(TEXT("assets"), AssetsJson);
    if (!ScanRequestId.IsEmpty())
    {
        Body->SetStringField(TEXT("scan_request_id"), ScanRequestId);
    }

    FString BodyString;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyString);
    FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(FString::Printf(TEXT("%s/api/v2/ue/assets/catalog"), *BackendUrl));
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->SetHeader(TEXT("X-OntoTwin-UE-Project-Id"), UEProjectId);
    Request->SetHeader(TEXT("X-OntoTwin-UE-Project-Name"), UEProjectName);
    Request->SetContentAsString(BodyString);
    bRequestInFlight = true;

    const TWeakObjectPtr<UOntoTwinUEAssetCatalogSyncComponent> WeakThis(this);
    const int32 AssetCount = AssetDataList.Num();
    Request->OnProcessRequestComplete().BindLambda(
        [WeakThis, AssetCount, ScanRequestId](FHttpRequestPtr, FHttpResponsePtr Response, bool bOk)
        {
            UOntoTwinUEAssetCatalogSyncComponent* Self = WeakThis.Get();
            if (!Self)
            {
                return;
            }
            Self->bRequestInFlight = false;
            const int32 Code = Response.IsValid() ? Response->GetResponseCode() : -1;
            const bool bSuccess = bOk && Response.IsValid() && Code >= 200 && Code < 300;
            if (bSuccess)
            {
                UE_LOG(LogTemp, Log, TEXT("[UE资产目录] 同步成功 | assets=%d | code=%d"), AssetCount, Code);
                if (Self->ActiveScanRequestId == ScanRequestId)
                {
                    Self->ActiveScanRequestId.Empty();
                }
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("[UE资产目录] 同步失败 | assets=%d | code=%d"), AssetCount, Code);
                if (!ScanRequestId.IsEmpty())
                {
                    Self->ReportScanRequestFailure(
                        ScanRequestId,
                        FString::Printf(TEXT("UE 资产目录上报失败：HTTP %d"), Code));
                }
            }
            if (GEngine)
            {
                GEngine->AddOnScreenDebugMessage(
                    -1, 5.0f, bSuccess ? FColor::Green : FColor::Red,
                    bSuccess
                        ? FString::Printf(TEXT("OntoTwin 资产目录已同步：%d 项"), AssetCount)
                        : FString::Printf(TEXT("OntoTwin 资产目录同步失败：HTTP %d"), Code));
            }
        });
    Request->ProcessRequest();

    UE_LOG(LogTemp, Log, TEXT("[UE资产目录] 开始同步 | project=%s | assets=%d | payload=%.2f MB"),
        *UEProjectId, AssetCount, BodyString.Len() / 1024.0 / 1024.0);
}
