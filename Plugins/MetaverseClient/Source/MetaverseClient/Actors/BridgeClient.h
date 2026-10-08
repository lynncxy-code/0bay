#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#if METAVERSE_WITH_ONTOTWIN
#include "Realtime/OntoTwinRealtimeStreamProvider.h"
#endif
#include "BridgeClient.generated.h"

class AAvatarPool;
class IWebSocket;
class FTargetSyncManager;

/** 连接中间层 /ws/targets,OnMessage 解析状态帧并驱动 AGV 分身;带断线自动重连。 */
UCLASS()
class ABridgeClient : public AActor
#if METAVERSE_WITH_ONTOTWIN
    , public IOntoTwinRealtimeStreamProvider
#endif
{
    GENERATED_BODY()

public:
    ABridgeClient();

    /** 自动启动器在 BeginPlay 前注入场景池、总线地址与地面高度。 */
    void Configure(
        AAvatarPool* InAvatarPool,
        const FString& InUrl,
        double InGroundZ,
        const FString& InStreamId = TEXT("metaverse.targets.primary"));

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if METAVERSE_WITH_ONTOTWIN
    virtual UWorld* GetRealtimeStreamWorld() const override;
    virtual FString GetRealtimeStreamId() const override;
    virtual void ApplyRealtimeStreamEnabled(bool bEnabled) override;
    virtual TSharedRef<FJsonObject> BuildRealtimeStreamHealth() const override;
#endif

protected:
    UPROPERTY(EditAnywhere, Category = "Metaverse")
    FString Url = TEXT("ws://127.0.0.1:8080/ws/targets");

    UPROPERTY(EditAnywhere, Category = "Metaverse")
    FString StreamId = TEXT("metaverse.targets.primary");

    UPROPERTY(EditAnywhere, Category = "Metaverse")
    double GroundZ = 0.0;   // 地面高度(cm),AGV 贴地

    UPROPERTY(EditAnywhere, Category = "Metaverse")
    float ReconnectSeconds = 3.0f;   // 断线重连间隔

    UPROPERTY(EditAnywhere, Category = "Metaverse")
    AAvatarPool* AvatarPool = nullptr;

private:
    void Connect();
    void OnMessage(const FString& Message);
    void ScheduleReconnect();
    void ClearRealtimeTargets(const TCHAR* Reason);
#if METAVERSE_WITH_ONTOTWIN
    /** 同一 World/URL 或 stream 只允许一个 Provider，重复实例只做诊断不连 socket。 */
    bool HasCompetingProvider() const;
#endif

    TSharedPtr<IWebSocket> Socket;
    TSharedPtr<FTargetSyncManager> SyncManager;
    FTimerHandle ReconnectTimer;
    int32 ConnGeneration = 0;          // 每次 Connect 自增;回调按代号过滤,忽略旧连接
    bool bReconnectScheduled = false;  // 防 OnError/OnClosed 重复调度重连
    bool bClosing = false;
    // Provider 注册后先保持关闭，等待 OntoTwin Manager 的持久化控制命令；
    // 不能因为 AutoStart actor 被创建就绕过页面唯一开关自动连 WS。
    bool bStreamEnabled = false;
    bool bCommandConfigured = false;
    bool bDesiredEnabled = false;
    FString ConnectionState = TEXT("disconnected");
    FString LastConnectionError;
    double LastFramePlatformSeconds = -1.0;
    int64 LastSourceTimestampMs = 0;
    int64 FrameCount = 0;
    int32 LastTargetCount = 0;
    FString LastParseError;
#if METAVERSE_WITH_ONTOTWIN
    bool bProviderRegistered = false;
    bool bRejectedAsDuplicateOwner = false;
#endif
};
