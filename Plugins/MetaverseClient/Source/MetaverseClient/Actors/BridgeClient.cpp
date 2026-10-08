#include "Actors/BridgeClient.h"
#include "Actors/AvatarPool.h"
#include "Core/StateFrameParser.h"
#include "Core/TargetSyncManager.h"
#include "Dto/StateFrame.h"
#include "IWebSocket.h"
#include "WebSocketsModule.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "TimerManager.h"
#if METAVERSE_WITH_ONTOTWIN
#include "Features/IModularFeatures.h"
#endif

ABridgeClient::ABridgeClient()
{
    PrimaryActorTick.bCanEverTick = false;   // WS 回调在游戏线程触发,无需 Tick
}

void ABridgeClient::Configure(
    AAvatarPool* InAvatarPool,
    const FString& InUrl,
    double InGroundZ,
    const FString& InStreamId)
{
    AvatarPool = InAvatarPool;
    Url = InUrl;
    GroundZ = InGroundZ;
    StreamId = InStreamId.IsEmpty() ? TEXT("metaverse.targets.primary") : InStreamId;
}

void ABridgeClient::BeginPlay()
{
    Super::BeginPlay();

    if (AvatarPool == nullptr)
    {
        UE_LOG(LogTemp, Warning, TEXT("[BridgeClient] AvatarPool 未设置"));
        return;
    }
    SyncManager = MakeShared<FTargetSyncManager>(AvatarPool, GroundZ);

#if METAVERSE_WITH_ONTOTWIN
    if (HasCompetingProvider())
    {
        // 不注册、不创建第二条 socket；TwinSceneManager/监控台仍可从已有
        // Provider 读取真实 owner。保留此实例状态便于日志诊断。
        bRejectedAsDuplicateOwner = true;
        bStreamEnabled = false;
        ConnectionState = TEXT("duplicate_owner");
        LastConnectionError = TEXT("duplicate realtime stream owner");
        UE_LOG(LogTemp, Error,
            TEXT("[BridgeClient] 拒绝重复外部流 owner stream=%s url=%s"),
            *StreamId, *Url);
        return;
    }
    IModularFeatures::Get().RegisterModularFeature(
        IOntoTwinRealtimeStreamProvider::GetModularFeatureName(), this);
    bProviderRegistered = true;
#endif

    if (!FModuleManager::Get().IsModuleLoaded(TEXT("WebSockets")))
    {
        FModuleManager::Get().LoadModule(TEXT("WebSockets"));
    }
    // AutoStart 只负责注册 Provider；是否连接由 Manager 的持久化命令决定。
    Connect();
}

void ABridgeClient::Connect()
{
    if (bClosing || !bStreamEnabled)
    {
        ConnectionState = bStreamEnabled ? TEXT("disconnected") : TEXT("disabled");
        return;
    }

#if METAVERSE_WITH_ONTOTWIN
    // 每次首次连接、重连和重新开启都再次确认 owner。另一个 Provider
    // 可能在本次连接之后才注册；发现竞争时保留诊断状态，不再创建第二条 socket。
    if (HasCompetingProvider())
    {
        bRejectedAsDuplicateOwner = true;
        bStreamEnabled = false;
        bReconnectScheduled = false;
        if (GetWorld() != nullptr)
        {
            GetWorld()->GetTimerManager().ClearTimer(ReconnectTimer);
        }
        ConnectionState = TEXT("duplicate_owner");
        LastConnectionError = TEXT("duplicate realtime stream owner");
        ClearRealtimeTargets(TEXT("duplicate_owner"));
        UE_LOG(LogTemp, Error,
            TEXT("[BridgeClient] 重连前发现竞争 owner，保持 duplicate_owner stream=%s url=%s"),
            *StreamId, *Url);
        return;
    }
#endif

    // 先让旧回调失效,再关闭旧连接;Close() 若同步触发 OnClosed,也不会误排重连。
    const int32 MyGen = ++ConnGeneration;   // 本次连接代号
    if (Socket.IsValid())
    {
        if (Socket->IsConnected())
        {
            Socket->Close();
        }
        Socket.Reset();
    }

    ConnectionState = TEXT("connecting");
    Socket = FWebSocketsModule::Get().CreateWebSocket(Url, TEXT("ws"));

    Socket->OnConnected().AddWeakLambda(this, [this, MyGen]()
    {
        if (MyGen != ConnGeneration) return;
        ConnectionState = TEXT("connected");
        LastConnectionError.Empty();
        UE_LOG(LogTemp, Log, TEXT("[BridgeClient] connected %s"), *Url);
    });
    Socket->OnConnectionError().AddWeakLambda(this, [this, MyGen](const FString& Error)
    {
        if (MyGen != ConnGeneration) return;
        ConnectionState = TEXT("error");
        LastConnectionError = Error;
        ClearRealtimeTargets(TEXT("connection_error"));
        UE_LOG(LogTemp, Warning, TEXT("[BridgeClient] error %s"), *Error);
        ScheduleReconnect();
    });
    Socket->OnClosed().AddWeakLambda(this, [this, MyGen](int32 StatusCode, const FString& Reason, bool bWasClean)
    {
        if (MyGen != ConnGeneration) return;
        ConnectionState = TEXT("disconnected");
        LastConnectionError = Reason;
        ClearRealtimeTargets(TEXT("connection_closed"));
        UE_LOG(LogTemp, Log, TEXT("[BridgeClient] closed %d %s"), StatusCode, *Reason);
        if (!bClosing)
        {
            ScheduleReconnect();
        }
    });
    Socket->OnMessage().AddWeakLambda(this, [this, MyGen](const FString& Message)
    {
        if (MyGen != ConnGeneration) return;   // 只接受当前连接的消息,丢弃旧连接残留回调
        OnMessage(Message);                    // 已在游戏线程
    });

    Socket->Connect();
}

void ABridgeClient::OnMessage(const FString& Message)
{
    FStateFrame Frame;
    if (FStateFrameParser::Parse(Message, Frame) && SyncManager.IsValid())
    {
        LastFramePlatformSeconds = FPlatformTime::Seconds();
        LastSourceTimestampMs = FMath::Max<int64>(0, Frame.Ts);
        ++FrameCount;
        SyncManager->ApplyFrame(Frame);   // 驱动分身:spawn/update/recycle
        LastTargetCount = SyncManager->LastTargetCount();
        LastParseError.Empty();
        if (FrameCount == 1 || FrameCount % 300 == 0)
        {
            UE_LOG(LogTemp, Log,
                TEXT("[BridgeClient] frame=%lld targets=%d applied=%d"),
                FrameCount, SyncManager->LastTargetCount(), SyncManager->LastAppliedCount());
        }
    }
    else
    {
        LastParseError = TEXT("invalid realtime frame");
        UE_LOG(LogTemp, Warning, TEXT("[BridgeClient] 无法解析实时状态帧"));
    }
}

void ABridgeClient::ScheduleReconnect()
{
    // 应对 Pod 重启 / ingress 超时 / 网络抖动;OnError 与 OnClosed 可能都触发,用标志去重
    if (bClosing || !bStreamEnabled || bReconnectScheduled || GetWorld() == nullptr)
    {
        return;
    }
    bReconnectScheduled = true;
    GetWorld()->GetTimerManager().SetTimer(ReconnectTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
    {
        bReconnectScheduled = false;
        UE_LOG(LogTemp, Log, TEXT("[BridgeClient] 重连中… %s"), *Url);
        Connect();
    }), ReconnectSeconds, false);
}

void ABridgeClient::ClearRealtimeTargets(const TCHAR* Reason)
{
    const int32 PreviousTargetCount = SyncManager.IsValid()
        ? SyncManager->ActiveCount()
        : LastTargetCount;
    if (SyncManager.IsValid())
    {
        SyncManager->ClearActiveTargets();
    }
    LastTargetCount = 0;
    LastParseError.Empty();
    LastFramePlatformSeconds = -1.0;
    LastSourceTimestampMs = 0;

    if (PreviousTargetCount > 0)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[BridgeClient] 已清理实时目标 reason=%s count=%d"),
            Reason, PreviousTargetCount);
    }
}

void ABridgeClient::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // 先使回调/重连失效并关闭 socket，再注销 Provider，避免
    // OntoTwinSync 在注销与 bClosing 之间的极短窗口抢开兼容回退连接。
    bClosing = true;
    ++ConnGeneration;
    if (GetWorld() != nullptr)
    {
        GetWorld()->GetTimerManager().ClearTimer(ReconnectTimer);
    }
    if (Socket.IsValid() && Socket->IsConnected())
    {
        Socket->Close();
    }
    Socket.Reset();
    ClearRealtimeTargets(TEXT("end_play"));
#if METAVERSE_WITH_ONTOTWIN
    if (bProviderRegistered && IModularFeatures::Get().IsModularFeatureAvailable(
        IOntoTwinRealtimeStreamProvider::GetModularFeatureName()))
    {
        IModularFeatures::Get().UnregisterModularFeature(
            IOntoTwinRealtimeStreamProvider::GetModularFeatureName(), this);
    }
    bProviderRegistered = false;
#endif
    SyncManager.Reset();
    Super::EndPlay(EndPlayReason);
}

#if METAVERSE_WITH_ONTOTWIN
bool ABridgeClient::HasCompetingProvider() const
{
    const TArray<IOntoTwinRealtimeStreamProvider*> Providers =
        IModularFeatures::Get().GetModularFeatureImplementations<IOntoTwinRealtimeStreamProvider>(
            IOntoTwinRealtimeStreamProvider::GetModularFeatureName());
    for (IOntoTwinRealtimeStreamProvider* Provider : Providers)
    {
        if (Provider == nullptr || Provider == this
            || Provider->GetRealtimeStreamWorld() != GetWorld())
        {
            continue;
        }
        const bool bSameStream = !StreamId.IsEmpty()
            && Provider->GetRealtimeStreamId().Equals(StreamId, ESearchCase::IgnoreCase);
        // 现场旧版 OntoTwinSync/MetaverseClient 使用过不同 stream_id 但同 URL；
        // URL 相同也视为同一真实 socket owner，避免重复消费同一帧。
        FString ExistingUrl;
        const TSharedRef<FJsonObject> ExistingHealth = Provider->BuildRealtimeStreamHealth();
        const bool bHasExistingUrl = ExistingHealth->TryGetStringField(TEXT("url"), ExistingUrl);
        const bool bSameUrl = bHasExistingUrl && !Url.IsEmpty()
            && ExistingUrl.Equals(Url, ESearchCase::IgnoreCase);
        if (bSameStream || bSameUrl)
        {
            return true;
        }
    }
    return false;
}

UWorld* ABridgeClient::GetRealtimeStreamWorld() const
{
    return GetWorld();
}

FString ABridgeClient::GetRealtimeStreamId() const
{
    return StreamId;
}

void ABridgeClient::ApplyRealtimeStreamEnabled(bool bEnabled)
{
    if (bClosing)
    {
        return;
    }

    bCommandConfigured = true;
    bDesiredEnabled = bEnabled;
    if (bStreamEnabled == bEnabled)
    {
        if (!bEnabled)
        {
            ClearRealtimeTargets(TEXT("disabled_repeat"));
        }
        return;
    }

    bStreamEnabled = bEnabled;
    ++ConnGeneration;
    bReconnectScheduled = false;
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(ReconnectTimer);
    }

    if (!bEnabled)
    {
        if (Socket.IsValid())
        {
            if (Socket->IsConnected())
            {
                Socket->Close();
            }
            Socket.Reset();
        }
        ConnectionState = TEXT("disabled");
        LastConnectionError.Empty();
        ClearRealtimeTargets(TEXT("disabled"));
        UE_LOG(LogTemp, Log,
            TEXT("[BridgeClient] 外部数据监控台已关闭流 %s；OntoTwin HTTP 快照继续运行"),
            *StreamId);
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("[BridgeClient] 外部数据监控台已开启流 %s"), *StreamId);
    Connect();
}

TSharedRef<FJsonObject> ABridgeClient::BuildRealtimeStreamHealth() const
{
    const TSharedRef<FJsonObject> Health = MakeShared<FJsonObject>();
    const double FrameAgeMs = LastFramePlatformSeconds >= 0.0
        ? FMath::Max(0.0, (FPlatformTime::Seconds() - LastFramePlatformSeconds) * 1000.0)
        : -1.0;
    const bool bFrameFresh = bStreamEnabled
        && ConnectionState == TEXT("connected")
        && FrameAgeMs >= 0.0
        && FrameAgeMs <= 5000.0;

    Health->SetBoolField(TEXT("enabled"), bStreamEnabled);
    Health->SetBoolField(TEXT("reported_enabled"), bStreamEnabled);
    Health->SetBoolField(TEXT("command_configured"), bCommandConfigured);
    TSharedPtr<FJsonValue> DesiredEnabledValue = bCommandConfigured
        ? TSharedPtr<FJsonValue>(MakeShared<FJsonValueBoolean>(bDesiredEnabled))
        : TSharedPtr<FJsonValue>(MakeShared<FJsonValueNull>());
    Health->SetField(TEXT("desired_enabled"), DesiredEnabledValue);
    Health->SetBoolField(
        TEXT("in_sync"),
        bCommandConfigured && bDesiredEnabled == bStreamEnabled);
    Health->SetStringField(TEXT("stream_id"), StreamId);
    Health->SetStringField(TEXT("owner"), TEXT("MetaverseClient"));
    Health->SetStringField(TEXT("owner_state"),
        bRejectedAsDuplicateOwner ? TEXT("rejected_duplicate") : TEXT("registered"));
    Health->SetStringField(TEXT("url"), Url);
    Health->SetStringField(TEXT("connection_state"),
        bStreamEnabled ? ConnectionState : TEXT("disabled"));
    Health->SetStringField(TEXT("active_source"), bFrameFresh ? TEXT("websocket") : TEXT("none"));
    if (FrameAgeMs >= 0.0)
    {
        Health->SetNumberField(TEXT("last_frame_age_ms"), FrameAgeMs);
    }
    else
    {
        Health->SetField(TEXT("last_frame_age_ms"), MakeShared<FJsonValueNull>());
    }
    Health->SetNumberField(TEXT("frame_count"), static_cast<double>(FrameCount));
    Health->SetNumberField(TEXT("source_timestamp_ms"), static_cast<double>(LastSourceTimestampMs));
    const int32 TargetCount = SyncManager.IsValid()
        ? SyncManager->LastTargetCount() : LastTargetCount;
    const int32 AppliedCount = SyncManager.IsValid()
        ? SyncManager->LastAppliedCount() : 0;
    Health->SetNumberField(TEXT("target_count"), TargetCount);
    Health->SetNumberField(TEXT("applied_target_count"), AppliedCount);
    Health->SetNumberField(TEXT("unapplied_target_count"), FMath::Max(0, TargetCount - AppliedCount));

    FString ExecutionState = TEXT("waiting");
    if (!bStreamEnabled)
    {
        ExecutionState = TEXT("waiting");
    }
    else if (TargetCount > 0 && AppliedCount == 0)
    {
        ExecutionState = TEXT("none_applied");
    }
    else if (TargetCount > 0 && AppliedCount < TargetCount)
    {
        ExecutionState = TEXT("partial");
    }
    else if (TargetCount > 0 && AppliedCount == TargetCount)
    {
        ExecutionState = TEXT("ready");
    }
    Health->SetStringField(TEXT("execution_state"), ExecutionState);

    TMap<FString, int32> AggregateReasons;
    TArray<FString> CategoryIds;
    if (SyncManager.IsValid())
    {
        SyncManager->LastCategoryStats().GetKeys(CategoryIds);
    }
    CategoryIds.Sort();
    TArray<TSharedPtr<FJsonValue>> Categories;
    for (const FString& CategoryId : CategoryIds)
    {
        const FTargetSyncCategoryStats* Stats = SyncManager->LastCategoryStats().Find(CategoryId);
        if (Stats == nullptr)
        {
            continue;
        }
        const TSharedRef<FJsonObject> Category = MakeShared<FJsonObject>();
        Category->SetStringField(TEXT("category_id"), CategoryId);
        if (CategoryId == TEXT("person"))
        {
            Category->SetStringField(TEXT("label"), TEXT("人员"));
            Category->SetStringField(TEXT("lifecycle"), TEXT("transient_dynamic"));
            Category->SetStringField(TEXT("handler_id"), TEXT("metaverse.people"));
        }
        else if (CategoryId == TEXT("agv"))
        {
            Category->SetStringField(TEXT("label"), TEXT("AGV"));
            Category->SetStringField(TEXT("lifecycle"), TEXT("formal_instance"));
            Category->SetStringField(TEXT("handler_id"), TEXT("ontotwin.formal_instance"));
        }
        else if (CategoryId == TEXT("unknown"))
        {
            Category->SetStringField(TEXT("label"), TEXT("未识别"));
        }
        else
        {
            Category->SetStringField(TEXT("label"), CategoryId);
        }
        Category->SetNumberField(TEXT("target_count"), Stats->TargetCount);
        Category->SetNumberField(TEXT("applied_count"), Stats->AppliedCount);
        Category->SetNumberField(
            TEXT("unapplied_count"),
            FMath::Max(0, Stats->TargetCount - Stats->AppliedCount));

        TArray<FString> ReasonIds;
        Stats->UnappliedReasons.GetKeys(ReasonIds);
        ReasonIds.Sort();
        TArray<TSharedPtr<FJsonValue>> Reasons;
        for (const FString& ReasonId : ReasonIds)
        {
            const int32 Count = Stats->UnappliedReasons.FindRef(ReasonId);
            AggregateReasons.FindOrAdd(ReasonId) += Count;
            const TSharedRef<FJsonObject> Reason = MakeShared<FJsonObject>();
            Reason->SetStringField(TEXT("code"), ReasonId);
            Reason->SetNumberField(TEXT("count"), Count);
            if (ReasonId == TEXT("handler_unavailable"))
            {
                Reason->SetStringField(TEXT("message"), TEXT("没有可用的场景处理器"));
            }
            else if (ReasonId == TEXT("instance_missing"))
            {
                Reason->SetStringField(TEXT("message"), TEXT("尚未找到对应的正式实例"));
            }
            else
            {
                Reason->SetStringField(TEXT("message"), TEXT("目标数据无效或已离场"));
            }
            Reasons.Add(MakeShared<FJsonValueObject>(Reason));
        }
        Category->SetArrayField(TEXT("reasons"), Reasons);
        Categories.Add(MakeShared<FJsonValueObject>(Category));
    }
    Health->SetArrayField(TEXT("categories"), Categories);

    TArray<FString> ReasonIds;
    AggregateReasons.GetKeys(ReasonIds);
    ReasonIds.Sort();
    TArray<TSharedPtr<FJsonValue>> UnappliedReasons;
    for (const FString& ReasonId : ReasonIds)
    {
        const TSharedRef<FJsonObject> Reason = MakeShared<FJsonObject>();
        Reason->SetStringField(TEXT("code"), ReasonId);
        Reason->SetNumberField(TEXT("count"), AggregateReasons.FindRef(ReasonId));
        UnappliedReasons.Add(MakeShared<FJsonValueObject>(Reason));
    }
    Health->SetArrayField(TEXT("unapplied_reasons"), UnappliedReasons);
    Health->SetStringField(TEXT("error"),
        !LastConnectionError.IsEmpty() ? LastConnectionError : LastParseError);
    Health->SetArrayField(TEXT("targets"), TArray<TSharedPtr<FJsonValue>>());
    return Health;
}
#endif
