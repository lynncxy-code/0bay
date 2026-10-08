#include "MetaverseClient.h"
#include "Actors/AvatarPool.h"
#include "Actors/BridgeClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"

void FMetaverseClientModule::StartupModule()
{
    WorldInitHandle = FWorldDelegates::OnPostWorldInitialization.AddRaw(
        this, &FMetaverseClientModule::OnPostWorldInitialization);
}

void FMetaverseClientModule::ShutdownModule()
{
    if (WorldInitHandle.IsValid())
    {
        FWorldDelegates::OnPostWorldInitialization.Remove(WorldInitHandle);
        WorldInitHandle.Reset();
    }
}

void FMetaverseClientModule::OnPostWorldInitialization(
    UWorld* World, const UWorld::InitializationValues IVS)
{
    if (World == nullptr || IsRunningCommandlet()
        || (World->WorldType != EWorldType::PIE && World->WorldType != EWorldType::Game))
    {
        return;
    }

    static const TCHAR* Section = TEXT("MetaverseClient.AutoStart");
    bool bEnabled = false;
    if (!GConfig->GetBool(Section, TEXT("bEnabled"), bEnabled, GGameIni) || !bEnabled)
    {
        return;
    }

    for (TActorIterator<ABridgeClient> It(World); It; ++It)
    {
        UE_LOG(LogTemp, Log, TEXT("[MetaverseClient] 场景已有 BridgeClient，跳过自动启动"));
        return;
    }

    FString Url = TEXT("ws://127.0.0.1:8080/ws/targets");
    FString StreamId = TEXT("metaverse.targets.primary");
    FString ObjectTypeRid = TEXT("ri.obj.acdd3050-3159-5219-a2da-256bebf0e8f1");
    FString Clazz = TEXT("person");
    double GroundZ = 0.0;
    int32 RandomSeed = 20260821;
    GConfig->GetString(Section, TEXT("Url"), Url, GGameIni);
    GConfig->GetString(Section, TEXT("StreamId"), StreamId, GGameIni);
    GConfig->GetString(Section, TEXT("ObjectTypeRid"), ObjectTypeRid, GGameIni);
    GConfig->GetString(Section, TEXT("Clazz"), Clazz, GGameIni);
    GConfig->GetDouble(Section, TEXT("GroundZ"), GroundZ, GGameIni);
    GConfig->GetInt(Section, TEXT("RandomSeed"), RandomSeed, GGameIni);

    TArray<FString> AssetStrings;
    GConfig->GetArray(Section, TEXT("CharacterAssets"), AssetStrings, GGameIni);
    TArray<FSoftObjectPath> AssetPaths;
    for (const FString& AssetString : AssetStrings)
    {
        FSoftObjectPath Path(AssetString);
        if (Path.IsValid())
        {
            AssetPaths.Add(MoveTemp(Path));
        }
    }
    if (AssetPaths.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("[MetaverseClient] 自动启动已开启，但 CharacterAssets 为空"));
        return;
    }

    FActorSpawnParameters PoolParams;
    PoolParams.Name = TEXT("Metaverse_AutoAvatarPool");
    PoolParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AAvatarPool* Pool = World->SpawnActor<AAvatarPool>(
        AAvatarPool::StaticClass(), FTransform::Identity, PoolParams);
    if (Pool == nullptr)
    {
        UE_LOG(LogTemp, Error, TEXT("[MetaverseClient] 自动创建 AvatarPool 失败"));
        return;
    }
    Pool->ConfigurePeopleAssets(AssetPaths, ObjectTypeRid, Clazz, RandomSeed);

    ABridgeClient* Bridge = World->SpawnActorDeferred<ABridgeClient>(
        ABridgeClient::StaticClass(), FTransform::Identity, nullptr, nullptr,
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (Bridge == nullptr)
    {
        Pool->Destroy();
        UE_LOG(LogTemp, Error, TEXT("[MetaverseClient] 自动创建 BridgeClient 失败"));
        return;
    }
    Bridge->Configure(Pool, Url, GroundZ, StreamId);
    UGameplayStatics::FinishSpawningActor(Bridge, FTransform::Identity);

    UE_LOG(LogTemp, Log,
        TEXT("[MetaverseClient] PIE Provider 已注册，等待监控台开关 stream=%s url=%s assets=%d rid=%s clazz=%s"),
        *StreamId, *Url, AssetPaths.Num(), *ObjectTypeRid, *Clazz);
}

IMPLEMENT_MODULE(FMetaverseClientModule, MetaverseClient);
