// ============================================================================
// TwinSceneManager.cpp  (v3 — 数据库驱动，废弃固化)
//
// 职责：
//   1. 运行时轮询后端快照，动态 spawn / 更新 / 销毁 ATwinInstance（DB 唯一真源）
//   2. 编辑器：PullPreviewFromDB() 从数据库临时 spawn 预览（RF_Transient，不写 .umap）
//   3. 编辑器：FR-6 历史 actor 迁移工具（导出 / 清除已迁移）
//
// 已废弃（FR-4）：SnapshotToLevel 固化、TakeOverExistingInstances 接管、
//   bEditorPlaced 保护——孪生实例不再持久化进关卡，一律由数据库驱动。
// ============================================================================

#include "TwinSceneManager.h"
#include "Realtime/OntoTwinRealtimeStreamProvider.h"
#include "UEAssetCatalogSyncComponent.h"
#include "OntoTwinModelLoadingWidget.h"
#include "OntoTwinRuntimeEditorPanel.h"
#include "OntoTwinOverlayWidget.h"
#include "OntoTwinRuntimeGizmo.h"
#include "RuntimeEditor/TwinRuntimeEditorCameraPawn.h"
#include "TwinInstance.h"
#include "SceneInteraction/TwinInteractionManagerComponent.h"
#include "WebInteraction/OntoTwinWebInteractionComponent.h"
#include "IWebSocket.h"
#include "WebSocketsModule.h"
#include "Modules/ModuleManager.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "HAL/PlatformTime.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/UserWidget.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/MeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "MediaPlayer.h"
#include "MediaSoundComponent.h"
#include "MediaTexture.h"
// FR-6 迁移工具用
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Containers/StringConv.h"
#include "HAL/FileManager.h"
#include "Misc/SecureHash.h"
#include "Components/ActorComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "Serialization/JsonWriter.h"
#include "Features/IModularFeatures.h"
#if WITH_EDITOR
#include "Editor.h"
#endif

namespace
{
/** Provider 在 World 初始化时注册；回退连接最多等待这一窗口。 */
constexpr float RealtimeProviderStartupGraceSeconds = 0.35f;
constexpr double RealtimeFrameFreshnessSeconds = 5.0;

FString CleanFolderSegment(FString Segment)
{
    Segment.TrimStartAndEndInline();
    Segment.ReplaceInline(TEXT("\\"), TEXT("_"));
    Segment.ReplaceInline(TEXT("/"), TEXT("_"));
    return Segment;
}

FName BuildTwinFolderPath(const TSharedPtr<FJsonObject>& Snapshot, const TCHAR* RootFolder)
{
    TArray<FString> Segments;
    const TArray<TSharedPtr<FJsonValue>>* Hierarchy = nullptr;
    if (Snapshot.IsValid() && Snapshot->TryGetArrayField(TEXT("hierarchyPath"), Hierarchy))
    {
        for (const TSharedPtr<FJsonValue>& Value : *Hierarchy)
        {
            FString Segment;
            if (Value.IsValid() && Value->TryGetString(Segment))
            {
                Segment = CleanFolderSegment(Segment);
                if (!Segment.IsEmpty())
                {
                    Segments.Add(Segment);
                }
            }
        }
    }

    if (Segments.Num() == 0 && Snapshot.IsValid())
    {
        FString TypeName;
        if (Snapshot->TryGetStringField(TEXT("objectTypeName"), TypeName))
        {
            TypeName = CleanFolderSegment(TypeName);
            if (!TypeName.IsEmpty())
            {
                Segments.Add(TypeName);
            }
        }
    }

    FString Path(RootFolder);
    for (const FString& Segment : Segments)
    {
        Path += TEXT("/");
        Path += Segment;
    }
    return FName(*Path);
}

bool IsInFolderOrChild(const FName ActorFolderName, const FString& RootFolder)
{
    const FString ActorFolder = ActorFolderName.ToString().Replace(TEXT("\\"), TEXT("/"));
    FString CleanRoot = RootFolder;
    CleanRoot.TrimStartAndEndInline();
    CleanRoot.ReplaceInline(TEXT("\\"), TEXT("/"));
    CleanRoot.RemoveFromEnd(TEXT("/"));

    return !CleanRoot.IsEmpty()
        && (ActorFolder == CleanRoot || ActorFolder.StartsWith(CleanRoot + TEXT("/")));
}

FString TruncateRuntimeLabel(const FString& Value, int32 MaxLength)
{
    if (Value.Len() <= MaxLength || MaxLength < 6)
    {
        return Value;
    }
    return Value.Left(MaxLength - 3) + TEXT("...");
}

FString CollisionEnabledToMigrationString(const ECollisionEnabled::Type CollisionEnabled)
{
    switch (CollisionEnabled)
    {
    case ECollisionEnabled::NoCollision:      return TEXT("NoCollision");
    case ECollisionEnabled::QueryOnly:        return TEXT("QueryOnly");
    case ECollisionEnabled::PhysicsOnly:      return TEXT("PhysicsOnly");
    case ECollisionEnabled::QueryAndPhysics:  return TEXT("QueryAndPhysics");
    case ECollisionEnabled::ProbeOnly:        return TEXT("ProbeOnly");
    case ECollisionEnabled::QueryAndProbe:    return TEXT("QueryAndProbe");
    default:                                  return TEXT("NoCollision");
    }
}

ATwinInstance* ResolveTwinInstanceActor(AActor* Candidate)
{
    TSet<const AActor*> Visited;
    for (int32 Depth = 0; Candidate && Depth < 8 && !Visited.Contains(Candidate); ++Depth)
    {
        Visited.Add(Candidate);
        if (ATwinInstance* Instance = Cast<ATwinInstance>(Candidate))
        {
            return Instance;
        }
        AActor* Parent = Candidate->GetAttachParentActor();
        Candidate = Parent ? Parent : Candidate->GetOwner();
    }
    return nullptr;
}
}

// ── 构造函数 ─────────────────────────────────────────────────────────────────

ATwinSceneManager::ATwinSceneManager()
{
    PrimaryActorTick.bCanEverTick = true;
    USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);
    OverlayMediaSound = CreateDefaultSubobject<UMediaSoundComponent>(TEXT("OverlayMediaSound"));
    OverlayMediaSound->SetupAttachment(SceneRoot);
    OverlayMediaSound->SetAutoActivate(false);
    InteractionManager = CreateDefaultSubobject<UTwinInteractionManagerComponent>(TEXT("SceneInteractionManager"));
    WebInteractionManager = CreateDefaultSubobject<UOntoTwinWebInteractionComponent>(TEXT("WebInteractionManager"));
    AssetCatalogSync = CreateDefaultSubobject<UOntoTwinUEAssetCatalogSyncComponent>(TEXT("AssetCatalogSync"));
    UEProjectName = FApp::GetProjectName();
    UEProjectId = FString::Printf(TEXT("ueproj_%s"), *UEProjectName);
}

// ── BeginPlay ────────────────────────────────────────────────────────────────

void ATwinSceneManager::BeginPlay()
{
    // Customer releases can override serialized level defaults without rebuilding the map.
    // Resolve the backend before Super::BeginPlay(), because that starts actor components and
    // SceneInteractionManager immediately performs its first runtime projection request.
    FString BackendBaseUrlOverride;
    const bool bHasBackendBaseUrlOverride = FParse::Value(
            FCommandLine::Get(),
            TEXT("OntoTwinBackendBaseUrl="),
            BackendBaseUrlOverride);
    if (bHasBackendBaseUrlOverride)
    {
        BackendBaseUrlOverride.TrimStartAndEndInline();
        BackendBaseUrlOverride.RemoveFromEnd(TEXT("/"));
        if (!BackendBaseUrlOverride.IsEmpty())
        {
            BackendBaseUrl = BackendBaseUrlOverride;
        }
    }
    else if (BackendBaseUrl.Equals(TEXT("http://127.0.0.1:5000"), ESearchCase::IgnoreCase))
    {
        // Docker Desktop can leave an installed-release portproxy on IPv4 loopback.
        // localhost prefers Docker's working IPv6 loopback in PIE and packaged builds.
        BackendBaseUrl = TEXT("http://localhost:5000");
    }

    Super::BeginPlay();
    SetActorTickEnabled(true);
    EnsureModelLoadingWidget();

    FString RealtimeEnabledOverride;
    if (FParse::Value(
            FCommandLine::Get(),
            TEXT("OntoTwinRealtimeWebSocket="),
            RealtimeEnabledOverride))
    {
        RealtimeEnabledOverride.TrimStartAndEndInline();
        bEnableRealtimeWebSocket = RealtimeEnabledOverride.Equals(TEXT("true"), ESearchCase::IgnoreCase)
            || RealtimeEnabledOverride == TEXT("1")
            || RealtimeEnabledOverride.Equals(TEXT("yes"), ESearchCase::IgnoreCase);
    }

    FString RealtimeUrlOverride;
    if (FParse::Value(
            FCommandLine::Get(),
            TEXT("OntoTwinRealtimeWebSocketUrl="),
            RealtimeUrlOverride))
    {
        RealtimeWebSocketUrl = RealtimeUrlOverride;
    }

    float PollIntervalOverride = 0.0f;
    if (FParse::Value(FCommandLine::Get(), TEXT("OntoTwinPollInterval="), PollIntervalOverride))
    {
        PollInterval = FMath::Clamp(PollIntervalOverride, 0.1f, 10.0f);
    }

    FString IncrementalSnapshotsOverride;
    if (FParse::Value(
            FCommandLine::Get(),
            TEXT("OntoTwinIncrementalSnapshots="),
            IncrementalSnapshotsOverride))
    {
        IncrementalSnapshotsOverride.TrimStartAndEndInline();
        bEnableIncrementalSnapshots = IncrementalSnapshotsOverride.Equals(TEXT("true"), ESearchCase::IgnoreCase)
            || IncrementalSnapshotsOverride == TEXT("1")
            || IncrementalSnapshotsOverride.Equals(TEXT("yes"), ESearchCase::IgnoreCase);
    }

    // 启动定时轮询（FR-4：孪生实例全部由数据库驱动动态 spawn，不再接管关卡预置 Actor）
    SetPollTimerInterval(PollInterval, 1.0f);

    bRealtimeClosing = false;
    if (bEnableRealtimeWebSocket)
    {
        // MetaverseClient 在 World 初始化回调中创建 Provider，其 BeginPlay
        // 可能晚于本 Actor。先给同 World Provider 一个短暂注册窗口，
        // 避免 TwinSceneManager 抢先建立第二条兼容 socket。
        bRealtimeProviderProbePending = true;
        GetWorldTimerManager().SetTimer(
            RealtimeProviderProbeTimerHandle,
            FTimerDelegate::CreateWeakLambda(this, [this]()
            {
                ProbeRealtimeProviderAndConnect();
            }),
            RealtimeProviderStartupGraceSeconds,
            false);
    }
    else
    {
        RealtimeConnectionState = TEXT("disabled");
    }

    if (bEnableOverlays && !bEnableRuntimeEditor)
    {
        if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
        {
            EnableInput(PC);
        }
    }

    if (bEnableRuntimeEditor)
    {
        bRuntimeEditSelfTestRequested = FParse::Param(
            FCommandLine::Get(), TEXT("OntoTwinRuntimeEditSelfTest"));
        bRuntimeEditSelfTestQuit = FParse::Param(
            FCommandLine::Get(), TEXT("OntoTwinRuntimeEditSelfTestQuit"));
        FParse::Value(
            FCommandLine::Get(),
            TEXT("OntoTwinRuntimeEditSelfTestExpectedInstances="),
            RuntimeEditSelfTestExpectedInstances);
        FParse::Value(
            FCommandLine::Get(),
            TEXT("OntoTwinRuntimeEditSelfTestExpectedTypes="),
            RuntimeEditSelfTestExpectedTypes);
        RuntimeEditSelfTestStartSeconds = FPlatformTime::Seconds();
        APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
        if (PC)
        {
            EnableInput(PC);
            if (InputComponent)
            {
                InputComponent->BindKey(
                    RuntimeEditorToggleKey,
                    IE_Pressed,
                    this,
                    &ATwinSceneManager::RequestRuntimeEditToggle);
            }
        }

        UE_LOG(LogTemp, Log,
            TEXT("[RuntimeEditor] 已启用 | 场景编辑快捷键=%s"),
            *RuntimeEditorToggleKey.ToString());
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 6.0f, FColor::Cyan,
                FString::Printf(TEXT("Runtime Editor: %s"),
                    *RuntimeEditorToggleKey.ToString()));
        }
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("[RuntimeEditor] 已关闭 (bEnableRuntimeEditor=false)"));
    }

    UE_LOG(LogTemp, Log,
           TEXT("[孪生管理器] 启动完毕 | 后端=%s | 轮询间隔=%.2fs | 预置实例=%d"),
           *BackendBaseUrl, PollInterval, InstanceRegistry.Num());
}

// ── EndPlay ──────────────────────────────────────────────────────────────────

void ATwinSceneManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    GetWorldTimerManager().ClearTimer(PollTimerHandle);
    GetWorldTimerManager().ClearTimer(ModelLoadingDismissTimer);
    GetWorldTimerManager().ClearTimer(RealtimeReconnectTimerHandle);
    GetWorldTimerManager().ClearTimer(RealtimeProviderProbeTimerHandle);
    GetWorldTimerManager().ClearTimer(OverlayMediaRetryTimer);
    bRealtimeClosing = true;
    RealtimeConnectionState = TEXT("disabled");
    ++RealtimeConnectionGeneration;
    if (RealtimeSocket.IsValid() && RealtimeSocket->IsConnected())
    {
        RealtimeSocket->Close();
    }
    RealtimeSocket.Reset();
    bRealtimeProviderProbePending = false;
    bRealtimeUsingExternalProvider = false;
    bRuntimeEditDirty = false;
    bRuntimeEnterRoamingAfterExit = false;
    ExitRuntimeEditMode();
    if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
    {
        UpdateOverlayPointerInput(PC, false);
    }
    ClearOverlaySelection();
    DismissModelLoadingWidget();

    if (RuntimeGizmo && IsValid(RuntimeGizmo))
    {
        RuntimeGizmo->Destroy();
    }
    RuntimeGizmo = nullptr;

    // 清理全部运行时 spawn 的孪生体（FR-4：不再有编辑器固化 Actor 需要保留）
    for (auto& Pair : InstanceRegistry)
    {
        if (Pair.Value && IsValid(Pair.Value))
        {
            Pair.Value->Destroy();
        }
    }
    InstanceRegistry.Empty();

    Super::EndPlay(EndPlayReason);
    UE_LOG(LogTemp, Log, TEXT("[孪生管理器] 已清理运行时孪生体并停止轮询"));
}

void ATwinSceneManager::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    ReconcileRealtimeProviderOwnership();
    TickRuntimeEditor(DeltaTime);
    if (bRuntimeEditSelfTestRequested && !bRuntimeEditSelfTestComplete)
    {
        RunRuntimeEditSelfTest();
    }
    TickOverlays();
}

void ATwinSceneManager::TickOverlays()
{
    UpdateOverlayMediaPlaybackClock();
    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    if (!PC)
    {
        if (!bEnableOverlays) ClearOverlaySelection();
        return;
    }

    const bool bPointerOverRuntimePanel =
        RuntimeEditorPanel && IsValid(RuntimeEditorPanel) && RuntimeEditorPanel->IsPointerOverPanel();
    const bool bInteractionOwnsSelection =
        InteractionManager && InteractionManager->OwnsPointerSelection();
    const bool bPointerOverWebInteraction =
        WebInteractionManager && WebInteractionManager->IsPointerOverInteractiveWeb();
    const bool bPointerOverOverlay = SelectedOverlayWidget
        && IsValid(SelectedOverlayWidget) && SelectedOverlayWidget->IsPointerOverPanel();
    const bool bGlobalSelectionClick = !bInteractionOwnsSelection
        && !bRuntimeEditMode
        && !bPointerOverRuntimePanel
        && !bPointerOverOverlay
        && !bPointerOverWebInteraction
        && PC->WasInputKeyJustPressed(EKeys::LeftMouseButton);
    FHitResult GlobalSelectionHit;
    const bool bHasGlobalSelectionHit = bGlobalSelectionClick
        && TraceRuntimeCursor(GlobalSelectionHit);
    if (bGlobalSelectionClick && InteractionManager)
    {
        InteractionManager->HandleGlobalPointerSelection(
            bHasGlobalSelectionHit ? &GlobalSelectionHit : nullptr);
    }

    bool bHasSelectedOverlay = false;
    if (bEnableOverlays)
    {
        for (const auto& Pair : InstanceRegistry)
        {
            if (Pair.Value && IsValid(Pair.Value) && Pair.Value->HasSelectedOverlay())
            {
                bHasSelectedOverlay = true;
                break;
            }
        }
    }

    const bool bInteractiveMediaSelected = OverlaySelectedInstance && SelectedOverlayWidget
        && SelectedOverlayWidget->HasPlayableMedia();
    UpdateOverlayPointerInput(
        PC,
        bEnableOverlays && (bHasSelectedOverlay || bInteractiveMediaSelected)
            && !bInteractionOwnsSelection && !bRuntimeEditMode && !bPointerOverWebInteraction);

    if (!bEnableOverlays)
    {
        ClearOverlaySelection();
        return;
    }

    if (bGlobalSelectionClick)
    {
        ATwinInstance* HitInstance = bHasGlobalSelectionHit
            ? ResolveTwinInstanceActor(GlobalSelectionHit.GetActor())
            : nullptr;
        if (HitInstance && HitInstance->HasSelectedOverlay())
        {
            SelectOverlayInstance(HitInstance);
        }
        else
        {
            ClearOverlaySelection();
        }
    }
    else if (bRuntimeEditMode && RuntimeSelectedInstance && IsValid(RuntimeSelectedInstance)
        && RuntimeSelectedInstance->HasSelectedOverlay())
    {
        SelectOverlayInstance(RuntimeSelectedInstance);
    }

    if (!bInteractionOwnsSelection && !bRuntimeEditMode
        && PC->WasInputKeyJustPressed(EKeys::Escape))
    {
        ClearOverlaySelection();
    }

    if (OverlaySelectedInstance &&
        (!IsValid(OverlaySelectedInstance)
            || !OverlaySelectedInstance->HasSelectedOverlay()))
    {
        ClearOverlaySelection();
    }

    if (OverlaySelectedInstance && SelectedOverlayWidget)
    {
        const uint64 PayloadSerial = OverlaySelectedInstance->GetOverlayPayloadSerial();
        if (PayloadSerial != SelectedOverlayPayloadSerial)
        {
            SelectedOverlayWidget->ApplyOverlayData(OverlaySelectedInstance->GetOverlayData());
            SelectedOverlayPayloadSerial = PayloadSerial;
            RefreshOverlayMediaForSelection();
        }

        const ESlateVisibility InteractiveVisibility = SelectedOverlayWidget->HasPlayableMedia()
            ? ESlateVisibility::Visible : ESlateVisibility::HitTestInvisible;
        if (SelectedOverlayWidget->IsMediaExpanded())
        {
            SelectedOverlayWidget->SetAlignmentInViewport(FVector2D(0.5f, 0.5f));
            SelectedOverlayWidget->SetPositionInViewport(FVector2D::ZeroVector, false);
            SelectedOverlayWidget->SetAnchorsInViewport(FAnchors(0.5f, 0.5f));
            SelectedOverlayWidget->SetVisibility(InteractiveVisibility);
        }
        else
        {
            // UMG 使用经过 DPI 缩放的逻辑坐标，不能直接使用 PlayerController 返回的物理像素。
            // 所有视角统一跟随模型锚点；锚点出屏时把面板约束在最近的屏幕边缘。
            FVector2D WidgetPosition;
            const bool bProjected = UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
                PC,
                OverlaySelectedInstance->GetOverlayAnchorWorldLocation(),
                WidgetPosition,
                true);
            SelectedOverlayWidget->SetAlignmentInViewport(FVector2D(0.5f, 1.0f));
            SelectedOverlayWidget->SetVisibility(
                bProjected ? InteractiveVisibility : ESlateVisibility::Collapsed);
            if (bProjected)
            {
                constexpr double EdgeMargin = 24.0;
                constexpr double AnchorGap = 16.0;
                const float ViewportScale = FMath::Max(
                    KINDA_SMALL_NUMBER,
                    UWidgetLayoutLibrary::GetViewportScale(this));
                const FVector2D ViewportSize =
                    UWidgetLayoutLibrary::GetViewportSize(this) / ViewportScale;
                const FVector2D PanelSize = SelectedOverlayWidget->GetDesiredRenderSize();

                WidgetPosition.Y -= AnchorGap;
                const double MinX = FMath::Min(
                    EdgeMargin + PanelSize.X * 0.5,
                    FMath::Max(EdgeMargin, ViewportSize.X - EdgeMargin));
                const double MaxX = FMath::Max(
                    MinX,
                    ViewportSize.X - EdgeMargin - PanelSize.X * 0.5);
                const double MaxY = FMath::Max(EdgeMargin, ViewportSize.Y - EdgeMargin);
                const double MinY = FMath::Min(EdgeMargin + PanelSize.Y, MaxY);
                WidgetPosition.X = FMath::Clamp(WidgetPosition.X, MinX, MaxX);
                WidgetPosition.Y = FMath::Clamp(WidgetPosition.Y, MinY, MaxY);
                SelectedOverlayWidget->SetPositionInViewport(WidgetPosition, false);
            }
        }
    }

    UpdateAlwaysOverlays(PC);
}

void ATwinSceneManager::UpdateOverlayPointerInput(
    APlayerController* PlayerController,
    bool bShouldOwnPointer)
{
    if (!PlayerController) return;

    if (bShouldOwnPointer)
    {
        if (!bOverlayPointerInputActive)
        {
            bOverlayPreviousMouseCursor = PlayerController->bShowMouseCursor;
            bOverlayPointerInputActive = true;

            FInputModeGameAndUI InputMode;
            InputMode.SetHideCursorDuringCapture(false);
            InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
            PlayerController->SetInputMode(InputMode);
        }
        PlayerController->bShowMouseCursor = true;
        return;
    }

    if (!bOverlayPointerInputActive) return;
    bOverlayPointerInputActive = false;

    const bool bAnotherSystemOwnsInput = bRuntimeEditMode
        || (InteractionManager && InteractionManager->IsRoamingActive());
    if (bAnotherSystemOwnsInput) return;

    PlayerController->bShowMouseCursor = bOverlayPreviousMouseCursor;
    if (!bOverlayPreviousMouseCursor)
    {
        PlayerController->SetInputMode(FInputModeGameOnly());
    }
}

void ATwinSceneManager::SelectOverlayInstance(ATwinInstance* Instance)
{
    if (!Instance || !IsValid(Instance) || !Instance->HasSelectedOverlay())
    {
        return;
    }
    if (OverlaySelectedInstance != Instance)
    {
        StopOverlayMedia();
        OverlaySelectedInstance = Instance;
        SelectedOverlayPayloadSerial = 0;
    }
    if (!SelectedOverlayWidget)
    {
        APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
        if (!PC) return;
        UClass* WidgetClass = OverlayWidgetClass
            ? OverlayWidgetClass.Get() : UOntoTwinOverlayWidget::StaticClass();
        SelectedOverlayWidget = CreateWidget<UOntoTwinOverlayWidget>(PC, WidgetClass);
        if (!SelectedOverlayWidget) return;
        TWeakObjectPtr<ATwinSceneManager> WeakThis(this);
        SelectedOverlayWidget->SetMediaActionHandler(
            [WeakThis](EOntoTwinOverlayMediaAction Action)
            {
                if (WeakThis.IsValid())
                {
                    WeakThis->HandleOverlayMediaAction(Action);
                }
            });
        SelectedOverlayWidget->SetAlignmentInViewport(FVector2D(0.5f, 1.0f));
        SelectedOverlayWidget->AddToViewport(900);
    }

    UE_LOG(LogTemp, Log,
        TEXT("OntoTwin overlay selected instance=%s roaming=%s widget=%s"),
        *Instance->GetInstanceId(),
        InteractionManager && InteractionManager->IsRoamingActive() ? TEXT("true") : TEXT("false"),
        SelectedOverlayWidget ? TEXT("ready") : TEXT("missing"));
}

void ATwinSceneManager::ClearOverlaySelection()
{
    StopOverlayMedia();
    OverlaySelectedInstance = nullptr;
    SelectedOverlayPayloadSerial = 0;
    UOntoTwinOverlayWidget* WidgetToClose = SelectedOverlayWidget;
    SelectedOverlayWidget = nullptr;
    if (WidgetToClose && IsValid(WidgetToClose))
    {
        const TWeakObjectPtr<UOntoTwinOverlayWidget> WeakWidget(WidgetToClose);
        WidgetToClose->PlayCloseAnimation(
            [WeakWidget]()
            {
                if (WeakWidget.IsValid())
                {
                    WeakWidget->RemoveFromParent();
                }
            });
    }
}

void ATwinSceneManager::RefreshOverlayMediaForSelection(bool bForceResolve)
{
    if (!OverlaySelectedInstance || !IsValid(OverlaySelectedInstance)
        || !SelectedOverlayWidget || !IsValid(SelectedOverlayWidget)
        || !SelectedOverlayWidget->HasPlayableMedia())
    {
        StopOverlayMedia();
        return;
    }

    const FString InstanceId = OverlaySelectedInstance->GetInstanceId();
    const FString SourceRevision = SelectedOverlayWidget->GetMediaSourceRevision();
    const bool bSameSource = OverlayMediaInstanceId == InstanceId
        && OverlayMediaSourceRevision == SourceRevision;
    if (bSameSource && (OverlayMediaResolveRequest.IsValid()
        || bOverlayMediaOpening
        || GetWorldTimerManager().IsTimerActive(OverlayMediaRetryTimer)
        || (OverlayMediaPlayer && OverlayMediaPlayer->IsReady())))
    {
        return;
    }
    if (bSameSource && bOverlayMediaManualRetryRequired && !bForceResolve)
    {
        return;
    }

    if (!bSameSource)
    {
        StopOverlayMedia();
        OverlayMediaInstanceId = InstanceId;
        OverlayMediaSourceRevision = SourceRevision;
        bOverlayMediaAutoplay = SelectedOverlayWidget->ShouldAutoplayMedia();
        bOverlayMediaPlayWhenOpened = false;
        OverlayMediaRetryIndex = 0;
    }

    if (bForceResolve || bOverlayMediaAutoplay)
    {
        bOverlayMediaPlayWhenOpened = bForceResolve || bOverlayMediaAutoplay;
        RequestOverlayMediaResolve(false);
    }
    else
    {
        SelectedOverlayWidget->SetMediaPlaybackState(
            false, bOverlayMediaMuted, TEXT("Select Play to start"));
    }
}

void ATwinSceneManager::RequestOverlayMediaResolve(bool bResetRetry)
{
    if (!OverlaySelectedInstance || !IsValid(OverlaySelectedInstance)
        || !SelectedOverlayWidget || !IsValid(SelectedOverlayWidget)
        || !SelectedOverlayWidget->HasPlayableMedia())
    {
        return;
    }
    if (OverlayMediaResolveRequest.IsValid() || bOverlayMediaOpening)
    {
        return;
    }

    if (bResetRetry)
    {
        GetWorldTimerManager().ClearTimer(OverlayMediaRetryTimer);
        OverlayMediaRetryIndex = 0;
        bOverlayMediaManualRetryRequired = false;
    }

    OverlayMediaInstanceId = OverlaySelectedInstance->GetInstanceId();
    OverlayMediaSourceRevision = SelectedOverlayWidget->GetMediaSourceRevision();
    SelectedOverlayWidget->SetMediaPlaybackState(
        false, bOverlayMediaMuted, TEXT("Loading video..."));

    TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
    Body->SetStringField(TEXT("instance_id"), OverlayMediaInstanceId);
    FString BodyString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyString);
    FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(FString::Printf(
        TEXT("%s/api/v2/overlays/media/resolve"), *BackendBaseUrl));
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
    AddUEProjectHeaders(Request);
    Request->SetContentAsString(BodyString);
    Request->OnProcessRequestComplete().BindUObject(
        this, &ATwinSceneManager::OnOverlayMediaResolveResponse);
    OverlayMediaResolveRequest = Request;
    if (!Request->ProcessRequest())
    {
        OverlayMediaResolveRequest.Reset();
        ScheduleOverlayMediaRetry(TEXT("Video source request failed"));
    }
}

void ATwinSceneManager::OnOverlayMediaResolveResponse(
    FHttpRequestPtr HttpRequest,
    FHttpResponsePtr Response,
    bool bWasSuccessful)
{
    if (OverlayMediaResolveRequest != HttpRequest)
    {
        return;
    }
    OverlayMediaResolveRequest.Reset();
    if (!OverlaySelectedInstance || !IsValid(OverlaySelectedInstance)
        || OverlaySelectedInstance->GetInstanceId() != OverlayMediaInstanceId)
    {
        return;
    }

    const int32 ResponseCode = Response.IsValid() ? Response->GetResponseCode() : -1;
    if (!bWasSuccessful || !Response.IsValid() || ResponseCode != 200)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("OntoTwin media resolve failed successful=%s response_valid=%s code=%d retry_index=%d"),
            bWasSuccessful ? TEXT("true") : TEXT("false"),
            Response.IsValid() ? TEXT("true") : TEXT("false"),
            ResponseCode,
            OverlayMediaRetryIndex);
        if (ResponseCode < 0 || ResponseCode >= 500)
        {
            ScheduleOverlayMediaRetry(TEXT("Video source unavailable"));
        }
        else if (SelectedOverlayWidget && IsValid(SelectedOverlayWidget))
        {
            bOverlayMediaManualRetryRequired = true;
            SelectedOverlayWidget->SetMediaPlaybackState(
                false,
                bOverlayMediaMuted,
                TEXT("Video source blocked or invalid"),
                true);
        }
        return;
    }

    TSharedPtr<FJsonObject> Payload;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(
        Response->GetContentAsString());
    if (!FJsonSerializer::Deserialize(Reader, Payload) || !Payload.IsValid())
    {
        ScheduleOverlayMediaRetry(TEXT("Invalid video source response"));
        return;
    }

    FString InstanceId;
    FString SourceRevision;
    FString MediaUrl;
    Payload->TryGetStringField(TEXT("instance_id"), InstanceId);
    Payload->TryGetStringField(TEXT("source_revision"), SourceRevision);
    Payload->TryGetStringField(TEXT("kind"), OverlayMediaKind);
    Payload->TryGetStringField(TEXT("url"), MediaUrl);
    if (InstanceId != OverlayMediaInstanceId || MediaUrl.IsEmpty())
    {
        return;
    }
    if (!OverlayMediaSourceRevision.IsEmpty() && !SourceRevision.IsEmpty()
        && SourceRevision != OverlayMediaSourceRevision)
    {
        // The snapshot changed while the signed URL was being resolved.
        RefreshOverlayMediaForSelection(true);
        return;
    }

    const TSharedPtr<FJsonObject>* Playback = nullptr;
    if (Payload->TryGetObjectField(TEXT("playback"), Playback)
        && Playback && Playback->IsValid())
    {
        (*Playback)->TryGetBoolField(TEXT("autoplay"), bOverlayMediaAutoplay);
        (*Playback)->TryGetBoolField(TEXT("muted"), bOverlayMediaMuted);
        (*Playback)->TryGetBoolField(TEXT("loop"), bOverlayMediaLoop);
    }
    if (OverlayMediaKind == TEXT("hls"))
    {
        bOverlayMediaLoop = false;
    }
    bOverlayMediaPlayWhenOpened = bOverlayMediaPlayWhenOpened || bOverlayMediaAutoplay;

    EnsureOverlayMediaPlayer();
    if (!OverlayMediaPlayer)
    {
        ScheduleOverlayMediaRetry(TEXT("Video player unavailable"));
        return;
    }
#if PLATFORM_WINDOWS
    const FName DesiredPlayerName = OverlayMediaKind.Equals(
        TEXT("mp4"), ESearchCase::IgnoreCase)
        ? FName(TEXT("WmfMedia"))
        : FName(TEXT("ElectraPlayer"));
#else
    const FName DesiredPlayerName(TEXT("ElectraPlayer"));
#endif
    OverlayMediaPlayer->SetDesiredPlayerName(DesiredPlayerName);
    OverlayMediaPlayer->SetLooping(bOverlayMediaLoop);
    if (OverlayMediaSound)
    {
        OverlayMediaSound->SetVolumeMultiplier(bOverlayMediaMuted ? 0.0f : 1.0f);
    }
    bOverlayMediaOpening = true;
    UE_LOG(LogTemp, Log,
        TEXT("OntoTwin media open started instance=%s kind=%s desired_player=%s"),
        *OverlayMediaInstanceId,
        *OverlayMediaKind,
        *DesiredPlayerName.ToString());
    if (!OverlayMediaPlayer->OpenUrl(MediaUrl))
    {
        bOverlayMediaOpening = false;
        ScheduleOverlayMediaRetry(TEXT("Unable to open video"));
    }
}

void ATwinSceneManager::EnsureOverlayMediaPlayer()
{
    if (!OverlayMediaPlayer)
    {
        OverlayMediaPlayer = NewObject<UMediaPlayer>(this, TEXT("OverlayMediaPlayer"));
        if (OverlayMediaPlayer)
        {
            OverlayMediaPlayer->PlayOnOpen = false;
            OverlayMediaPlayer->OnMediaOpened.AddDynamic(
                this, &ATwinSceneManager::OnOverlayMediaOpened);
            OverlayMediaPlayer->OnTracksChanged.AddDynamic(
                this, &ATwinSceneManager::OnOverlayMediaTracksChanged);
            OverlayMediaPlayer->OnMediaOpenFailed.AddDynamic(
                this, &ATwinSceneManager::OnOverlayMediaOpenFailed);
            OverlayMediaPlayer->OnEndReached.AddDynamic(
                this, &ATwinSceneManager::OnOverlayMediaEndReached);
            OverlayMediaPlayer->OnPlaybackResumed.AddDynamic(
                this, &ATwinSceneManager::OnOverlayMediaPlaybackResumed);
            OverlayMediaPlayer->OnPlaybackSuspended.AddDynamic(
                this, &ATwinSceneManager::OnOverlayMediaPlaybackSuspended);
        }
    }
    if (!OverlayMediaTexture)
    {
        OverlayMediaTexture = NewObject<UMediaTexture>(this, TEXT("OverlayMediaTexture"));
        if (OverlayMediaTexture)
        {
            OverlayMediaTexture->AutoClear = false;
            OverlayMediaTexture->NewStyleOutput = true;
            OverlayMediaTexture->SetRenderMode(UMediaTexture::ERenderMode::Default);
            OverlayMediaTexture->SetMediaPlayer(OverlayMediaPlayer);
            OverlayMediaTexture->UpdateResource();
        }
    }
    if (OverlayMediaSound)
    {
        if (OverlayMediaSound->GetMediaPlayer() != OverlayMediaPlayer)
        {
            // MediaSound must not create its generator before it has a player.
            // Otherwise UE 5.6 can register an unconsumed audio sink and stall V2 video timing.
            OverlayMediaSound->SetActive(false, true);
            OverlayMediaSound->SetMediaPlayer(OverlayMediaPlayer);
            OverlayMediaSound->SetActive(true, true);
        }
        OverlayMediaSound->SetVolumeMultiplier(bOverlayMediaMuted ? 0.0f : 1.0f);
        UE_LOG(LogTemp, Log,
            TEXT("OntoTwin media sound active=%s bound=%s muted=%s"),
            OverlayMediaSound->IsActive() ? TEXT("true") : TEXT("false"),
            OverlayMediaSound->GetMediaPlayer() == OverlayMediaPlayer
                ? TEXT("true") : TEXT("false"),
            bOverlayMediaMuted ? TEXT("true") : TEXT("false"));
    }
}

void ATwinSceneManager::UpdateOverlayMediaPlaybackClock()
{
    if (!OverlayMediaPlayer || bOverlayMediaLoop || bOverlayMediaReachedEnd
        || bOverlayMediaOpening || !OverlayMediaPlayer->IsReady())
    {
        return;
    }

    if (OverlayMediaDurationSeconds <= 0.0)
    {
        OverlayMediaDurationSeconds = OverlayMediaPlayer->GetDuration().GetTotalSeconds();
    }
    if (OverlayMediaDurationSeconds <= 0.0
        || OverlayMediaPlaybackStartedAtSeconds <= 0.0
        || !OverlayMediaPlayer->IsPlaying())
    {
        return;
    }

    const double NowSeconds = FPlatformTime::Seconds();
    const double ElapsedSeconds = OverlayMediaPlayedSeconds
        + FMath::Max(0.0, NowSeconds - OverlayMediaPlaybackStartedAtSeconds);
    if (!bOverlayMediaTextureSampleLogged && ElapsedSeconds >= 1.0
        && OverlayMediaTexture)
    {
        bOverlayMediaTextureSampleLogged = true;
        const UWorld* World = GetWorld();
        UE_LOG(LogTemp, Log,
            TEXT("OntoTwin media texture size=%dx%d aspect=%.3f samples=%d new_style=%s bound=%s player=%s rate=%.3f media_time=%.3fs next_sample=%.3fs world_paused=%s world_delta=%.6f"),
            OverlayMediaTexture->GetWidth(),
            OverlayMediaTexture->GetHeight(),
            OverlayMediaTexture->GetCurrentAspectRatio(),
            OverlayMediaTexture->GetAvailableSampleCount(),
            OverlayMediaTexture->NewStyleOutput ? TEXT("true") : TEXT("false"),
            OverlayMediaTexture->GetMediaPlayer() == OverlayMediaPlayer
                ? TEXT("true") : TEXT("false"),
            *OverlayMediaPlayer->GetPlayerName().ToString(),
            OverlayMediaPlayer->GetRate(),
            OverlayMediaPlayer->GetTime().GetTotalSeconds(),
            OverlayMediaTexture->GetNextSampleTime().GetTotalSeconds(),
            World && World->IsPaused() ? TEXT("true") : TEXT("false"),
            World ? World->GetDeltaSeconds() : 0.0f);
    }
    if (ElapsedSeconds < OverlayMediaDurationSeconds + 0.15)
    {
        return;
    }

    OverlayMediaPlayedSeconds = OverlayMediaDurationSeconds;
    OverlayMediaPlaybackStartedAtSeconds = 0.0;
    UE_LOG(LogTemp, Log,
        TEXT("OntoTwin media end fallback elapsed=%.3fs duration=%.3fs"),
        ElapsedSeconds,
        OverlayMediaDurationSeconds);
    OnOverlayMediaEndReached();
}

void ATwinSceneManager::HandleOverlayMediaAction(EOntoTwinOverlayMediaAction Action)
{
    if (!SelectedOverlayWidget || !IsValid(SelectedOverlayWidget)) return;

    if (Action == EOntoTwinOverlayMediaAction::PlayPause)
    {
        UE_LOG(LogTemp, Log,
            TEXT("OntoTwin media play/pause ready=%s playing=%s ended=%s time=%.3fs"),
            OverlayMediaPlayer && OverlayMediaPlayer->IsReady() ? TEXT("true") : TEXT("false"),
            OverlayMediaPlayer && OverlayMediaPlayer->IsPlaying() ? TEXT("true") : TEXT("false"),
            bOverlayMediaReachedEnd ? TEXT("true") : TEXT("false"),
            OverlayMediaPlayer ? OverlayMediaPlayer->GetTime().GetTotalSeconds() : 0.0);
    }

    switch (Action)
    {
    case EOntoTwinOverlayMediaAction::PlayPause:
        if (OverlayMediaPlayer && bOverlayMediaReachedEnd)
        {
            bOverlayMediaReachedEnd = false;
            bOverlayMediaPlayWhenOpened = true;
            OverlayMediaPlayedSeconds = 0.0;
            OverlayMediaPlaybackStartedAtSeconds = 0.0;
            RequestOverlayMediaResolve(true);
        }
        else if (OverlayMediaPlayer && OverlayMediaPlayer->IsPlaying())
        {
            OverlayMediaPlayer->Pause();
            SelectedOverlayWidget->SetMediaPlaybackState(
                false, bOverlayMediaMuted, TEXT("Paused"));
        }
        else if (OverlayMediaPlayer && OverlayMediaPlayer->IsReady())
        {
            if (OverlayMediaPlayer->Play())
            {
                SelectedOverlayWidget->SetMediaPlaybackState(true, bOverlayMediaMuted);
            }
            else
            {
                bOverlayMediaPlayWhenOpened = true;
                RequestOverlayMediaResolve(true);
            }
        }
        else
        {
            bOverlayMediaPlayWhenOpened = true;
            RequestOverlayMediaResolve(true);
        }
        break;
    case EOntoTwinOverlayMediaAction::ToggleMute:
        bOverlayMediaMuted = !bOverlayMediaMuted;
        if (OverlayMediaSound)
        {
            OverlayMediaSound->SetVolumeMultiplier(bOverlayMediaMuted ? 0.0f : 1.0f);
        }
        SelectedOverlayWidget->SetMediaPlaybackState(
            OverlayMediaPlayer && OverlayMediaPlayer->IsPlaying(), bOverlayMediaMuted);
        break;
    case EOntoTwinOverlayMediaAction::ToggleExpanded:
        SelectedOverlayWidget->SetMediaExpanded(!SelectedOverlayWidget->IsMediaExpanded());
        break;
    case EOntoTwinOverlayMediaAction::Close:
        ClearOverlaySelection();
        break;
    case EOntoTwinOverlayMediaAction::Retry:
        bOverlayMediaPlayWhenOpened = true;
        RequestOverlayMediaResolve(true);
        break;
    default:
        break;
    }
}

void ATwinSceneManager::ScheduleOverlayMediaRetry(const FString& StatusMessage)
{
    bOverlayMediaOpening = false;
    static const float RetryDelays[] = {2.0f, 5.0f, 15.0f};
    if (!SelectedOverlayWidget || !IsValid(SelectedOverlayWidget)) return;

    UE_LOG(LogTemp, Warning,
        TEXT("OntoTwin media retry status=%s retry_index=%d"),
        *StatusMessage,
        OverlayMediaRetryIndex);

    if (OverlayMediaRetryIndex >= UE_ARRAY_COUNT(RetryDelays))
    {
        bOverlayMediaManualRetryRequired = true;
        SelectedOverlayWidget->SetMediaPlaybackState(
            false,
            bOverlayMediaMuted,
            StatusMessage + TEXT(" - select Retry"),
            true);
        return;
    }

    const float Delay = RetryDelays[OverlayMediaRetryIndex++];
    SelectedOverlayWidget->SetMediaPlaybackState(
        false,
        bOverlayMediaMuted,
        FString::Printf(TEXT("%s - retrying in %.0fs"), *StatusMessage, Delay));
    TWeakObjectPtr<ATwinSceneManager> WeakThis(this);
    GetWorldTimerManager().SetTimer(
        OverlayMediaRetryTimer,
        FTimerDelegate::CreateLambda([WeakThis]()
        {
            if (WeakThis.IsValid())
            {
                WeakThis->RequestOverlayMediaResolve(false);
            }
        }),
        Delay,
        false);
}

void ATwinSceneManager::StopOverlayMedia(bool bResetWidget)
{
    GetWorldTimerManager().ClearTimer(OverlayMediaRetryTimer);
    bOverlayMediaOpening = false;
    bOverlayMediaReachedEnd = false;
    bOverlayMediaTextureSampleLogged = false;
    OverlayMediaDurationSeconds = 0.0;
    OverlayMediaPlayedSeconds = 0.0;
    OverlayMediaPlaybackStartedAtSeconds = 0.0;
    if (OverlayMediaResolveRequest.IsValid())
    {
        OverlayMediaResolveRequest->CancelRequest();
        OverlayMediaResolveRequest.Reset();
    }
    OverlayMediaInstanceId.Empty();
    OverlayMediaSourceRevision.Empty();
    OverlayMediaKind.Empty();
    OverlayMediaRetryIndex = 0;
    bOverlayMediaPlayWhenOpened = false;
    bOverlayMediaManualRetryRequired = false;
    if (OverlayMediaPlayer)
    {
        OverlayMediaPlayer->Close();
    }
    if (bResetWidget && SelectedOverlayWidget && IsValid(SelectedOverlayWidget))
    {
        SelectedOverlayWidget->SetMediaTexture(nullptr);
        SelectedOverlayWidget->SetMediaPlaybackState(false, bOverlayMediaMuted);
        SelectedOverlayWidget->SetMediaExpanded(false);
    }
}

void ATwinSceneManager::OnOverlayMediaOpened(FString OpenedUrl)
{
    bOverlayMediaOpening = false;
    bOverlayMediaReachedEnd = false;
    bOverlayMediaTextureSampleLogged = false;
    OverlayMediaDurationSeconds = 0.0;
    OverlayMediaPlayedSeconds = 0.0;
    OverlayMediaPlaybackStartedAtSeconds = 0.0;
    GetWorldTimerManager().ClearTimer(OverlayMediaRetryTimer);
    UE_LOG(LogTemp, Log, TEXT("OntoTwin media opened instance=%s"),
        *OverlayMediaInstanceId);
    if (OverlayMediaInstanceId.IsEmpty() || !SelectedOverlayWidget
        || !IsValid(SelectedOverlayWidget) || !OverlayMediaPlayer)
    {
        return;
    }
    OverlayMediaDurationSeconds = OverlayMediaPlayer->GetDuration().GetTotalSeconds();
    UE_LOG(LogTemp, Log, TEXT("OntoTwin media duration=%.3fs"),
        OverlayMediaDurationSeconds);
    OverlayMediaRetryIndex = 0;
    SelectedOverlayWidget->SetMediaTexture(OverlayMediaTexture);
    if (bOverlayMediaPlayWhenOpened)
    {
        OverlayMediaPlayer->Play();
    }
    SelectedOverlayWidget->SetMediaPlaybackState(
        OverlayMediaPlayer->IsPlaying(),
        bOverlayMediaMuted,
        OverlayMediaPlayer->IsPlaying() ? FString() : TEXT("Ready"));
}

void ATwinSceneManager::OnOverlayMediaOpenFailed(FString FailedUrl)
{
    if (OverlayMediaInstanceId.IsEmpty()) return;
    bOverlayMediaOpening = false;
    UE_LOG(LogTemp, Warning, TEXT("OntoTwin media open failed instance=%s kind=%s"),
        *OverlayMediaInstanceId,
        *OverlayMediaKind);
    ScheduleOverlayMediaRetry(TEXT("Unable to play video"));
}

void ATwinSceneManager::OnOverlayMediaTracksChanged()
{
    if (!OverlayMediaPlayer)
    {
        return;
    }

    const int32 VideoTrackCount = OverlayMediaPlayer->GetNumTracks(
        EMediaPlayerTrack::Video);
    int32 SelectedVideoTrack = OverlayMediaPlayer->GetSelectedTrack(
        EMediaPlayerTrack::Video);
    bool bSelectedFallbackTrack = false;
    if (SelectedVideoTrack == INDEX_NONE && VideoTrackCount > 0)
    {
        bSelectedFallbackTrack = OverlayMediaPlayer->SelectTrack(
            EMediaPlayerTrack::Video, 0);
        SelectedVideoTrack = OverlayMediaPlayer->GetSelectedTrack(
            EMediaPlayerTrack::Video);
    }

    const int32 FormatIndex = SelectedVideoTrack == INDEX_NONE
        ? INDEX_NONE
        : OverlayMediaPlayer->GetTrackFormat(
            EMediaPlayerTrack::Video, SelectedVideoTrack);
    const FIntPoint Dimensions = FormatIndex == INDEX_NONE
        ? FIntPoint::ZeroValue
        : OverlayMediaPlayer->GetVideoTrackDimensions(
            SelectedVideoTrack, FormatIndex);
    const float FrameRate = FormatIndex == INDEX_NONE
        ? 0.0f
        : OverlayMediaPlayer->GetVideoTrackFrameRate(
            SelectedVideoTrack, FormatIndex);
    const FString Codec = FormatIndex == INDEX_NONE
        ? FString()
        : OverlayMediaPlayer->GetVideoTrackType(
            SelectedVideoTrack, FormatIndex);

    UE_LOG(LogTemp, Log,
        TEXT("OntoTwin media tracks player=%s video_tracks=%d selected=%d fallback_selected=%s format=%d size=%dx%d fps=%.3f codec=%s"),
        *OverlayMediaPlayer->GetPlayerName().ToString(),
        VideoTrackCount,
        SelectedVideoTrack,
        bSelectedFallbackTrack ? TEXT("true") : TEXT("false"),
        FormatIndex,
        Dimensions.X,
        Dimensions.Y,
        FrameRate,
        Codec.IsEmpty() ? TEXT("unknown") : *Codec);
}

void ATwinSceneManager::OnOverlayMediaEndReached()
{
    UE_LOG(LogTemp, Log,
        TEXT("OntoTwin media end reached player=%s time=%.3fs rate=%.3f"),
        OverlayMediaPlayer ? *OverlayMediaPlayer->GetPlayerName().ToString() : TEXT("none"),
        OverlayMediaPlayer ? OverlayMediaPlayer->GetTime().GetTotalSeconds() : 0.0,
        OverlayMediaPlayer ? OverlayMediaPlayer->GetRate() : 0.0f);
    if (bOverlayMediaLoop)
    {
        bOverlayMediaReachedEnd = false;
        OverlayMediaPlayedSeconds = 0.0;
        OverlayMediaPlaybackStartedAtSeconds = FPlatformTime::Seconds();
        return;
    }
    bOverlayMediaReachedEnd = true;
    OverlayMediaPlaybackStartedAtSeconds = 0.0;
    if (OverlayMediaDurationSeconds > 0.0)
    {
        OverlayMediaPlayedSeconds = OverlayMediaDurationSeconds;
    }
    if (SelectedOverlayWidget && IsValid(SelectedOverlayWidget))
    {
        SelectedOverlayWidget->SetMediaPlaybackState(
            false, bOverlayMediaMuted, TEXT("Finished - select Play to replay"));
    }
}

void ATwinSceneManager::OnOverlayMediaPlaybackResumed()
{
    UE_LOG(LogTemp, Log,
        TEXT("OntoTwin media playback resumed player=%s time=%.3fs rate=%.3f"),
        OverlayMediaPlayer ? *OverlayMediaPlayer->GetPlayerName().ToString() : TEXT("none"),
        OverlayMediaPlayer ? OverlayMediaPlayer->GetTime().GetTotalSeconds() : 0.0,
        OverlayMediaPlayer ? OverlayMediaPlayer->GetRate() : 0.0f);
    bOverlayMediaReachedEnd = false;
    if (OverlayMediaPlayer && OverlayMediaDurationSeconds <= 0.0)
    {
        OverlayMediaDurationSeconds = OverlayMediaPlayer->GetDuration().GetTotalSeconds();
    }
    OverlayMediaPlaybackStartedAtSeconds = FPlatformTime::Seconds();
    if (SelectedOverlayWidget && IsValid(SelectedOverlayWidget))
    {
        SelectedOverlayWidget->SetMediaPlaybackState(true, bOverlayMediaMuted);
    }
}

void ATwinSceneManager::OnOverlayMediaPlaybackSuspended()
{
    UE_LOG(LogTemp, Log,
        TEXT("OntoTwin media playback suspended player=%s time=%.3fs rate=%.3f"),
        OverlayMediaPlayer ? *OverlayMediaPlayer->GetPlayerName().ToString() : TEXT("none"),
        OverlayMediaPlayer ? OverlayMediaPlayer->GetTime().GetTotalSeconds() : 0.0,
        OverlayMediaPlayer ? OverlayMediaPlayer->GetRate() : 0.0f);
    if (OverlayMediaPlaybackStartedAtSeconds > 0.0)
    {
        OverlayMediaPlayedSeconds += FMath::Max(
            0.0,
            FPlatformTime::Seconds() - OverlayMediaPlaybackStartedAtSeconds);
        OverlayMediaPlaybackStartedAtSeconds = 0.0;
        if (OverlayMediaDurationSeconds > 0.0)
        {
            OverlayMediaPlayedSeconds = FMath::Min(
                OverlayMediaPlayedSeconds, OverlayMediaDurationSeconds);
        }
    }
    if (!bOverlayMediaReachedEnd && SelectedOverlayWidget
        && IsValid(SelectedOverlayWidget))
    {
        SelectedOverlayWidget->SetMediaPlaybackState(
            false, bOverlayMediaMuted, TEXT("Paused"));
    }
}

void ATwinSceneManager::SelectOverlayFromSceneInteraction(ATwinInstance* Instance)
{
    // always 的世界空间面板已经是最终呈现，点击时不能再创建一份 Screen Space 面板。
    // selected 才进入点按选择链路；两个显示模式在单个实例上严格互斥。
    if (!Instance || !IsValid(Instance) || !Instance->HasSelectedOverlay())
    {
        ClearOverlaySelection();
        return;
    }
    SelectOverlayInstance(Instance);
}

void ATwinSceneManager::ClearOverlayFromSceneInteraction()
{
    ClearOverlaySelection();
}

ATwinInstance* ATwinSceneManager::FindManagedInstance(const FString& InstanceId) const
{
    ATwinInstance* const* Found = InstanceRegistry.Find(InstanceId);
    return Found && IsValid(*Found) ? *Found : nullptr;
}

void ATwinSceneManager::GetManagedInstances(TArray<ATwinInstance*>& OutInstances) const
{
    OutInstances.Reset();
    for (const TPair<FString, ATwinInstance*>& Pair : InstanceRegistry)
    {
        if (Pair.Value && IsValid(Pair.Value)) OutInstances.Add(Pair.Value);
    }
}

void ATwinSceneManager::FocusManagedInstance(ATwinInstance* Instance) const
{
    if (!Instance || !IsValid(Instance)) return;
    FBox Bounds = Instance->GetRepresentationWorldBounds(true);
    if (!Bounds.IsValid) Bounds += Instance->GetActorLocation();
    FocusManagedBounds(Bounds, Instance->GetInstanceId(), 1);
}

void ATwinSceneManager::FocusManagedInstances(const TSet<FString>& InstanceIds) const
{
    if (InstanceIds.Num() == 0) return;

    FBox CombinedBounds(EForceInit::ForceInit);
    int32 IncludedCount = 0;
    for (const TPair<FString, ATwinInstance*>& Pair : InstanceRegistry)
    {
        ATwinInstance* Instance = Pair.Value;
        if (!InstanceIds.Contains(Pair.Key) || !Instance || !IsValid(Instance)
            || Instance->IsHidden())
        {
            continue;
        }
        const FBox InstanceBounds = Instance->GetRepresentationWorldBounds(true);
        if (InstanceBounds.IsValid) CombinedBounds += InstanceBounds;
        else CombinedBounds += Instance->GetActorLocation();
        ++IncludedCount;
    }
    if (!CombinedBounds.IsValid || IncludedCount == 0)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("OntoTwin scope focus skipped: no visible managed instances"));
        return;
    }
    FocusManagedBounds(CombinedBounds, TEXT("scope"), IncludedCount);
}

void ATwinSceneManager::FocusManagedBounds(
    const FBox& Bounds,
    const FString& FocusLabel,
    int32 InstanceCount) const
{
    if (!Bounds.IsValid || InstanceCount <= 0) return;
    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    if (!PC) return;

    const FVector FocusCenter = Bounds.GetCenter();
    const float BoundsRadius = FMath::Max(50.0f, Bounds.GetExtent().Size());
    const float FovDegrees = PC->PlayerCameraManager
        ? FMath::Clamp(PC->PlayerCameraManager->GetFOVAngle(), 20.0f, 120.0f)
        : 90.0f;
    const float HalfFovRadians = FMath::DegreesToRadians(FovDegrees * 0.5f);
    const float FocusDistance = FMath::Clamp(
        BoundsRadius * 1.35f / FMath::Max(FMath::Tan(HalfFovRadians), 0.1f),
        250.0f,
        50000.0f);

    const FRotator CurrentRotation = PC->PlayerCameraManager
        ? PC->PlayerCameraManager->GetCameraRotation()
        : PC->GetControlRotation();
    const FRotator FocusRotation(
        FMath::Clamp(CurrentRotation.Pitch, -55.0f, -12.0f),
        CurrentRotation.Yaw,
        0.0f);
    const FVector FocusLocation = FocusCenter - FocusRotation.Vector() * FocusDistance;
    const FTransform FocusTransform(FocusRotation, FocusLocation);

    FString FocusError;
    if (InteractionManager && InteractionManager->IsRoamingActive()
        && InteractionManager->FocusInstanceCamera(
            FocusTransform, FovDegrees, FocusError))
    {
        UE_LOG(LogTemp, Log,
            TEXT("OntoTwin focus applied in roaming: target=%s instances=%d distance=%.1f radius=%.1f"),
            *FocusLabel, InstanceCount, FocusDistance, BoundsRadius);
        return;
    }

    AActor* CameraCarrier = PC->GetViewTarget();
    if (!CameraCarrier) CameraCarrier = PC->GetPawn();
    if (!CameraCarrier)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("OntoTwin focus failed: no active camera carrier; target=%s instances=%d roaming_error=%s"),
            *FocusLabel, InstanceCount, *FocusError);
        return;
    }
    CameraCarrier->SetActorLocationAndRotation(
        FocusLocation,
        FocusRotation,
        false,
        nullptr,
        ETeleportType::TeleportPhysics);
    if (UCameraComponent* Camera = CameraCarrier->FindComponentByClass<UCameraComponent>())
    {
        Camera->SetFieldOfView(FovDegrees);
    }
    if (PC->GetPawn() == CameraCarrier)
    {
        PC->SetControlRotation(FocusRotation);
    }
    UE_LOG(LogTemp, Log,
        TEXT("OntoTwin focus applied: target=%s instances=%d carrier=%s distance=%.1f radius=%.1f"),
        *FocusLabel, InstanceCount, *CameraCarrier->GetName(), FocusDistance, BoundsRadius);
}

ATwinInstance* ATwinSceneManager::FindAlwaysOverlayAtScreenPosition(
    APlayerController* PlayerController,
    const FVector2D& ScreenPosition) const
{
    if (!PlayerController) return nullptr;

    ATwinInstance* BestInstance = nullptr;
    float BestDistanceSquared = FLT_MAX;
    const FVector CameraLocation = PlayerController->PlayerCameraManager
        ? PlayerController->PlayerCameraManager->GetCameraLocation()
        : FVector::ZeroVector;

    for (const TPair<FString, ATwinInstance*>& Pair : InstanceRegistry)
    {
        ATwinInstance* Instance = Pair.Value;
        if (!Instance || !IsValid(Instance)
            || !Instance->IsScreenPointOverAlwaysOverlay(PlayerController, ScreenPosition))
        {
            continue;
        }

        const float DistanceSquared = FVector::DistSquared(
            CameraLocation, Instance->GetOverlayAnchorWorldLocation());
        if (!BestInstance || DistanceSquared < BestDistanceSquared)
        {
            BestInstance = Instance;
            BestDistanceSquared = DistanceSquared;
        }
    }
    return BestInstance;
}

bool ATwinSceneManager::SelectOverlayAtScreenPosition(const FVector2D& ScreenPosition)
{
    APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
    ATwinInstance* Instance = FindAlwaysOverlayAtScreenPosition(PlayerController, ScreenPosition);
    if (!Instance) return false;
    SelectOverlayFromSceneInteraction(Instance);
    return true;
}

ATwinInstance* ATwinSceneManager::FindOverlayInstanceNearHit(
    const FHitResult& Hit,
    float MaxDistanceCm) const
{
    const FVector HitLocation = Hit.bBlockingHit ? Hit.ImpactPoint : Hit.Location;
    const float MaxDistanceSquared = FMath::Square(FMath::Max(1.0f, MaxDistanceCm));
    ATwinInstance* BestInstance = nullptr;
    float BestDistanceSquared = MaxDistanceSquared;

    for (const TPair<FString, ATwinInstance*>& Pair : InstanceRegistry)
    {
        ATwinInstance* Instance = Pair.Value;
        if (!Instance || !IsValid(Instance) || !Instance->HasOverlay()) continue;

        const FBox Bounds = Instance->GetRepresentationWorldBounds(true);
        const FVector Closest = Bounds.IsValid
            ? Bounds.GetClosestPointTo(HitLocation)
            : Instance->GetActorLocation();
        const float DistanceSquared = FVector::DistSquared(HitLocation, Closest);
        if (DistanceSquared <= BestDistanceSquared)
        {
            BestInstance = Instance;
            BestDistanceSquared = DistanceSquared;
        }
    }
    return BestInstance;
}

void ATwinSceneManager::UpdateAlwaysOverlays(APlayerController* PlayerController)
{
    if (!PlayerController || !PlayerController->PlayerCameraManager) return;
    const FVector CameraLocation = PlayerController->PlayerCameraManager->GetCameraLocation();
    const float HorizontalFovRadians = FMath::DegreesToRadians(FMath::Clamp(
        PlayerController->PlayerCameraManager->GetFOVAngle(), 5.0f, 170.0f));
    const float MaxDistanceSquared = FMath::Square(FMath::Max(100.0f, OverlayCullDistanceCm));
    int32 ViewportX = 0;
    int32 ViewportY = 0;
    PlayerController->GetViewportSize(ViewportX, ViewportY);

    TArray<TPair<float, ATwinInstance*>> Candidates;
    for (const TPair<FString, ATwinInstance*>& Pair : InstanceRegistry)
    {
        ATwinInstance* Instance = Pair.Value;
        if (!Instance || !IsValid(Instance) || !Instance->HasAlwaysOverlay())
        {
            if (Instance && IsValid(Instance)) Instance->RefreshAlwaysOverlay(CameraLocation, false);
            continue;
        }
        const FVector Anchor = Instance->GetOverlayAnchorWorldLocation();
        const float DistanceSquared = FVector::DistSquared(CameraLocation, Anchor);
        FVector2D ScreenPosition;
        const bool bProjected = PlayerController->ProjectWorldLocationToScreen(Anchor, ScreenPosition, true);
        const bool bOnScreen = bProjected && ScreenPosition.X >= 0.0f && ScreenPosition.Y >= 0.0f
            && ScreenPosition.X <= ViewportX && ScreenPosition.Y <= ViewportY;
        if (DistanceSquared <= MaxDistanceSquared && bOnScreen)
        {
            Candidates.Emplace(DistanceSquared, Instance);
        }
        else
        {
            Instance->RefreshAlwaysOverlay(CameraLocation, false);
        }
    }

    Candidates.Sort([](const TPair<float, ATwinInstance*>& A, const TPair<float, ATwinInstance*>& B)
    {
        return A.Key < B.Key;
    });
    const int32 VisibleCount = FMath::Min(MaxVisibleAlwaysOverlays, Candidates.Num());
    for (int32 Index = 0; Index < Candidates.Num(); ++Index)
    {
        ATwinInstance* Instance = Candidates[Index].Value;
        if (Index >= VisibleCount)
        {
            Instance->RefreshAlwaysOverlay(CameraLocation, false);
            continue;
        }

        const float DistanceCm = FMath::Sqrt(Candidates[Index].Key);
        const float WorldViewportWidthCm = 2.0f * DistanceCm
            * FMath::Tan(HorizontalFovRadians * 0.5f);
        const float DesiredWorldWidthCm = WorldViewportWidthCm
            * (FMath::Max(1.0f, AlwaysOverlayTargetScreenWidthPx) / FMath::Max(1, ViewportX));
        const float RenderWidthPx = FMath::Max(1.0f, Instance->GetOverlayRenderWidthPixels());
        const float MinScale = FMath::Min(AlwaysOverlayMinWorldScale, AlwaysOverlayMaxWorldScale);
        const float MaxScale = FMath::Max(AlwaysOverlayMinWorldScale, AlwaysOverlayMaxWorldScale);
        const float WorldScale = FMath::Clamp(DesiredWorldWidthCm / RenderWidthPx, MinScale, MaxScale);
        Instance->RefreshAlwaysOverlay(CameraLocation, true, WorldScale);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 快照接口 URL
// ═══════════════════════════════════════════════════════════════════════════

FString ATwinSceneManager::BuildSnapshotsUrl() const
{
    FString Url = FString::Printf(TEXT("%s/api/v2/state/snapshots"), *BackendBaseUrl);
    // SceneId 非空时锁定场景；留空则由后端用当前激活数据集
    if (!SceneId.IsEmpty())
    {
        Url += FString::Printf(TEXT("?scene=%s"), *SceneId);
    }
    return Url;
}

FString ATwinSceneManager::BuildSnapshotChangesUrl() const
{
    FString Url = FString::Printf(TEXT("%s/api/v2/state/snapshot_changes"), *BackendBaseUrl);
    FString Separator = TEXT("?");
    if (!SceneId.IsEmpty())
    {
        Url += FString::Printf(
            TEXT("%sscene=%s"),
            *Separator,
            *FGenericPlatformHttp::UrlEncode(SceneId));
        Separator = TEXT("&");
    }
    if (!IncrementalSnapshotCursor.IsEmpty())
    {
        Url += FString::Printf(
            TEXT("%scursor=%s"),
            *Separator,
            *FGenericPlatformHttp::UrlEncode(IncrementalSnapshotCursor));
    }
    return Url;
}

void ATwinSceneManager::AddUEProjectHeaders(TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest) const
{
    HttpRequest->SetHeader(TEXT("X-OntoTwin-UE-Project-Id"), UEProjectId);
    HttpRequest->SetHeader(TEXT("X-OntoTwin-UE-Project-Name"), UEProjectName);
}

void ATwinSceneManager::SetPollTimerInterval(float IntervalSeconds, float FirstDelaySeconds)
{
    UWorld* World = GetWorld();
    if (!World) return;

    const float SafeInterval = FMath::Max(0.1f, IntervalSeconds);
    World->GetTimerManager().ClearTimer(PollTimerHandle);
    World->GetTimerManager().SetTimer(
        PollTimerHandle,
        this,
        &ATwinSceneManager::PollBackend,
        SafeInterval,
        true,
        FMath::Max(0.0f, FirstDelaySeconds));
}

void ATwinSceneManager::EnsureModelLoadingWidget()
{
    if (bInitialModelBaselineReady || ModelLoadingWidget)
    {
        return;
    }

    APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
    if (!PlayerController)
    {
        return;
    }

    ModelLoadingWidget = CreateWidget<UOntoTwinModelLoadingWidget>(
        PlayerController, UOntoTwinModelLoadingWidget::StaticClass());
    if (!ModelLoadingWidget)
    {
        UE_LOG(LogTemp, Warning, TEXT("[孪生管理器] 无法创建模型加载界面"));
        return;
    }

    ModelLoadingWidget->AddToViewport(2000);
    ModelLoadingWidget->ShowWaiting(TEXT("正在连接 OntoTwin 并等待当前项目的模型数据。"));
    UE_LOG(LogTemp, Log, TEXT("[孪生管理器] 模型加载界面已显示"));
}

void ATwinSceneManager::UpdateInitialModelLoadingProgress(const int32 Completed, const int32 Total)
{
    if (bInitialModelBaselineReady)
    {
        return;
    }
    EnsureModelLoadingWidget();
    if (ModelLoadingWidget)
    {
        ModelLoadingWidget->ShowProgress(Completed, Total);
    }
}

void ATwinSceneManager::CompleteInitialModelBaseline(const int32 Total)
{
    if (bInitialModelBaselineReady)
    {
        return;
    }

    bInitialModelBaselineReady = true;
    if (ModelLoadingWidget)
    {
        ModelLoadingWidget->ShowComplete(Total);
    }

    UE_LOG(LogTemp, Log,
        TEXT("[孪生管理器] 首轮模型基线已完成 | instances=%d"), Total);

    if (InteractionManager)
    {
        InteractionManager->NotifySceneBaselineReady();
    }

    GetWorldTimerManager().SetTimer(
        ModelLoadingDismissTimer,
        this,
        &ATwinSceneManager::DismissModelLoadingWidget,
        1.65f,
        false);
}

void ATwinSceneManager::FailInitialModelBaseline(const FString& Detail)
{
    if (bInitialModelBaselineReady)
    {
        return;
    }
    EnsureModelLoadingWidget();
    if (ModelLoadingWidget)
    {
        ModelLoadingWidget->ShowFailure(Detail);
    }
}

void ATwinSceneManager::DismissModelLoadingWidget()
{
    if (ModelLoadingWidget)
    {
        ModelLoadingWidget->RemoveFromParent();
        ModelLoadingWidget = nullptr;
    }
}

void ATwinSceneManager::BindCurrentUEProjectToActiveDataset()
{
#if WITH_EDITOR
    TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
    Body->SetStringField(TEXT("ue_project_id"), UEProjectId);
    Body->SetStringField(TEXT("ue_project_name"), UEProjectName);

    FString BodyStr;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyStr);
    FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
    HttpRequest->SetURL(FString::Printf(TEXT("%s/api/v2/ue/bind_active_project"), *BackendBaseUrl));
    HttpRequest->SetVerb(TEXT("POST"));
    HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    AddUEProjectHeaders(HttpRequest);
    HttpRequest->SetContentAsString(BodyStr);
    const TWeakObjectPtr<ATwinSceneManager> WeakThis(this);
    HttpRequest->OnProcessRequestComplete().BindLambda([WeakThis](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bOk)
    {
        const int32 Code = Resp.IsValid() ? Resp->GetResponseCode() : -1;
        const bool bSuccess = bOk && Resp.IsValid() && Code == 200;
        if (bSuccess)
        {
            UE_LOG(LogTemp, Log, TEXT("[UE绑定] 绑定当前 UE 工程到激活数据集成功 (code=%d)"), Code);
            if (ATwinSceneManager* SceneManager = WeakThis.Get())
            {
                SceneManager->SyncUEAssetCatalog();
            }
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("[UE绑定] 绑定当前 UE 工程到激活数据集失败 (code=%d)"), Code);
        }
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 8.0f, bSuccess ? FColor::Green : FColor::Red,
                bSuccess ? TEXT("已绑定当前 UE 工程到激活数据集") : TEXT("UE 工程绑定失败，请查看 Output Log"));
        }
    });
    HttpRequest->ProcessRequest();
#else
    UE_LOG(LogTemp, Warning, TEXT("[UE绑定] 仅在编辑器模式下可用"));
#endif
}

// ═══════════════════════════════════════════════════════════════════════════
// 轮询逻辑
// ═══════════════════════════════════════════════════════════════════════════

void ATwinSceneManager::PollBackend()
{
    if (bRequestInFlight) return;

    if (!bInitialModelBaselineReady)
    {
        EnsureModelLoadingWidget();
    }

    const bool bUseIncremental = bEnableIncrementalSnapshots && !bIncrementalSnapshotsFellBackToFull;
    if (!bUseIncremental)
    {
        UE_LOG(LogTemp, Log,
               TEXT("[孪生管理器] 全量轮询中... 场景=%s | 现有实例数=%d"),
               SceneId.IsEmpty() ? TEXT("(跟随后端)") : *SceneId, InstanceRegistry.Num());
    }

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest =
        FHttpModule::Get().CreateRequest();
    // The first project baseline can contain hundreds of instances and tens
    // of thousands of render parts. Customer workstations may need more than
    // UE's 30-second default activity window to produce and transfer it.
    HttpRequest->SetTimeout(300.0f);
    HttpRequest->SetActivityTimeout(180.0f);

    const FString Url = bUseIncremental ? BuildSnapshotChangesUrl() : BuildSnapshotsUrl();
    HttpRequest->SetURL(Url);
    HttpRequest->SetVerb(TEXT("GET"));
    HttpRequest->SetHeader(TEXT("Accept"), TEXT("application/json"));
    AddUEProjectHeaders(HttpRequest);

    if (bUseIncremental)
    {
        HttpRequest->OnProcessRequestComplete().BindUObject(
            this, &ATwinSceneManager::OnIncrementalPollResponse);
    }
    else
    {
        HttpRequest->OnProcessRequestComplete().BindUObject(
            this, &ATwinSceneManager::OnPollResponse);
    }

    bRequestInFlight = true;
    HttpRequest->ProcessRequest();
}

void ATwinSceneManager::OnPollResponse(
    FHttpRequestPtr HttpRequest,
    FHttpResponsePtr Response,
    bool bWasSuccessful)
{
    bRequestInFlight = false;

    int32 ResponseCode = Response.IsValid() ? Response->GetResponseCode() : -1;
    if (!bWasSuccessful || !Response.IsValid() || ResponseCode != 200)
    {
        UE_LOG(LogTemp, Warning,
               TEXT("[孪生管理器] ❌ 请求失败 | Code=%d | bSuccessful=%s"),
               ResponseCode, bWasSuccessful ? TEXT("true") : TEXT("false"));
        ConsecutiveFailures++;
        if (ConsecutiveFailures >= OfflineThreshold)
        {
            UE_LOG(LogTemp, Warning,
                   TEXT("[孪生管理器] 后端连续 %d 次无响应，标记全部实例离线"),
                   ConsecutiveFailures);
        }
        FailInitialModelBaseline(FString::Printf(
            TEXT("模型数据请求失败（HTTP %d），系统将自动重试。"), ResponseCode));
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("[孪生管理器] ✅ 收到快照响应 Code=200, Body长度=%d"), Response->GetContentLength());

    ConsecutiveFailures = 0;

    // ── 解析 JSON 数组 ───────────────────────────────────────────────────
    FString Body = Response->GetContentAsString();
    TArray<TSharedPtr<FJsonValue>> SnapshotArray;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);

    if (!FJsonSerializer::Deserialize(Reader, SnapshotArray))
    {
        UE_LOG(LogTemp, Error, TEXT("[孪生管理器] ❌ 快照 JSON 数组解析失败:\n%s"), *Body);
        FailInitialModelBaseline(TEXT("模型数据格式无法解析，系统将自动重试。"));
        return;
    }

    if (SnapshotArray.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[孪生管理器] ⚠️ 收到 200 OK，但快照数组为空 (当前无实例)"));
    }

    // ── 收集后端当前存在的实例 ID ─────────────────────────────────────────
    TSet<FString> BackendInstanceIds;

    bool bAllApplied = true;
    int32 Completed = 0;
    for (const auto& Val : SnapshotArray)
    {
        const TSharedPtr<FJsonObject>* SnapObj;
        if (!Val->TryGetObject(SnapObj))
        {
            bAllApplied = false;
            UpdateInitialModelLoadingProgress(++Completed, SnapshotArray.Num());
            continue;
        }

        FString InstanceId;
        if (!(*SnapObj)->TryGetStringField(TEXT("instanceId"), InstanceId))
        {
            bAllApplied = false;
            UpdateInitialModelLoadingProgress(++Completed, SnapshotArray.Num());
            continue;
        }

        BackendInstanceIds.Add(InstanceId);
        bAllApplied = ProcessSnapshot(*SnapObj) && bAllApplied;
        UpdateInitialModelLoadingProgress(++Completed, SnapshotArray.Num());
    }

    // ── 检测后端已删除的实例 → 在场景中销毁（FR-4：全部动态 spawn，一律销毁）─────
    TArray<FString> ToRemove;
    for (auto& Pair : InstanceRegistry)
    {
        if (!BackendInstanceIds.Contains(Pair.Key))
        {
            ToRemove.Add(Pair.Key);
        }
    }
    for (const FString& Id : ToRemove)
    {
        DestroyTwinInstance(Id);
    }

    if (bAllApplied)
    {
        CompleteInitialModelBaseline(SnapshotArray.Num());
    }
    else
    {
        FailInitialModelBaseline(TEXT("部分模型未能加载，系统将自动重试。"));
    }
}

void ATwinSceneManager::SyncUEAssetCatalog()
{
    if (AssetCatalogSync)
    {
        AssetCatalogSync->SyncCatalog();
    }
}

void ATwinSceneManager::FallBackToFullSnapshots(const FString& Reason)
{
    if (bIncrementalSnapshotsFellBackToFull)
    {
        return;
    }
    bIncrementalSnapshotsFellBackToFull = true;
    IncrementalSnapshotCursor.Empty();
    UE_LOG(LogTemp, Warning,
           TEXT("[孪生管理器] 增量快照不可用，当前会话回退全量接口: %s"), *Reason);
}

void ATwinSceneManager::OnIncrementalPollResponse(
    FHttpRequestPtr HttpRequest,
    FHttpResponsePtr Response,
    bool bWasSuccessful)
{
    bRequestInFlight = false;

    const int32 ResponseCode = Response.IsValid() ? Response->GetResponseCode() : -1;
    if (!bWasSuccessful || !Response.IsValid() || ResponseCode != 200)
    {
        if (Response.IsValid() && (ResponseCode == 400 || ResponseCode == 404 || ResponseCode == 501))
        {
            ConsecutiveFailures = 0;
            FallBackToFullSnapshots(FString::Printf(TEXT("HTTP %d"), ResponseCode));
            PollBackend();
            return;
        }

        UE_LOG(LogTemp, Warning,
               TEXT("[孪生管理器] 增量快照请求失败 | Code=%d | bSuccessful=%s"),
               ResponseCode, bWasSuccessful ? TEXT("true") : TEXT("false"));
        ConsecutiveFailures++;
        FailInitialModelBaseline(FString::Printf(
            TEXT("模型数据请求失败（HTTP %d），系统将自动重试。"), ResponseCode));
        return;
    }

    const FString Body = Response->GetContentAsString();
    TSharedPtr<FJsonObject> Envelope;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, Envelope) || !Envelope.IsValid())
    {
        FallBackToFullSnapshots(TEXT("响应不是 JSON 对象"));
        PollBackend();
        return;
    }

    FString SchemaVersion;
    FString Mode;
    FString NewCursor;
    const TArray<TSharedPtr<FJsonValue>>* Upserts = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* DeletedIds = nullptr;
    const bool bEnvelopeValid =
        Envelope->TryGetStringField(TEXT("schemaVersion"), SchemaVersion)
        && SchemaVersion == TEXT("snapshot_delta_v1")
        && Envelope->TryGetStringField(TEXT("mode"), Mode)
        && (Mode == TEXT("reset") || Mode == TEXT("delta"))
        && Envelope->TryGetStringField(TEXT("cursor"), NewCursor)
        && !NewCursor.IsEmpty()
        && Envelope->TryGetArrayField(TEXT("upserts"), Upserts)
        && Envelope->TryGetArrayField(TEXT("deletedIds"), DeletedIds);
    if (!bEnvelopeValid)
    {
        FallBackToFullSnapshots(TEXT("schemaVersion 或核心字段不兼容"));
        PollBackend();
        return;
    }

    ConsecutiveFailures = 0;
    double RevisionNumber = 0.0;
    Envelope->TryGetNumberField(TEXT("revision"), RevisionNumber);

    if (Mode == TEXT("reset"))
    {
        TSet<FString> BackendInstanceIds;
        bool bAllApplied = true;
        int32 Completed = 0;
        for (const TSharedPtr<FJsonValue>& Value : *Upserts)
        {
            const TSharedPtr<FJsonObject>* Snapshot = nullptr;
            FString InstanceId;
            if (!Value.IsValid()
                || !Value->TryGetObject(Snapshot)
                || !Snapshot
                || !Snapshot->IsValid()
                || !(*Snapshot)->TryGetStringField(TEXT("instanceId"), InstanceId)
                || InstanceId.IsEmpty())
            {
                bAllApplied = false;
                UpdateInitialModelLoadingProgress(++Completed, Upserts->Num());
                continue;
            }
            BackendInstanceIds.Add(InstanceId);
            bAllApplied = ProcessSnapshot(*Snapshot, false) && bAllApplied;
            UpdateInitialModelLoadingProgress(++Completed, Upserts->Num());
        }

        if (!bAllApplied)
        {
            IncrementalSnapshotCursor.Empty();
            UE_LOG(LogTemp, Error, TEXT("[孪生管理器] reset 基线应用不完整，下轮重新请求基线"));
            FailInitialModelBaseline(TEXT("部分模型未能加载，系统将自动重试。"));
            return;
        }

        TArray<FString> ToRemove;
        for (const auto& Pair : InstanceRegistry)
        {
            if (!BackendInstanceIds.Contains(Pair.Key))
            {
                ToRemove.Add(Pair.Key);
            }
        }
        for (const FString& InstanceId : ToRemove)
        {
            DestroyTwinInstance(InstanceId);
        }

        IncrementalSnapshotCursor = NewCursor;
        UE_LOG(LogTemp, Log,
               TEXT("[孪生管理器] 增量基线已恢复 | revision=%lld | instances=%d | removed=%d | bytes=%d"),
               static_cast<int64>(RevisionNumber), Upserts->Num(), ToRemove.Num(), Response->GetContentLength());
        CompleteInitialModelBaseline(Upserts->Num());
        return;
    }

    for (const TSharedPtr<FJsonValue>& Value : *DeletedIds)
    {
        FString InstanceId;
        if (Value.IsValid() && Value->TryGetString(InstanceId) && !InstanceId.IsEmpty())
        {
            DestroyTwinInstance(InstanceId);
        }
    }

    bool bAllApplied = true;
    for (const TSharedPtr<FJsonValue>& Value : *Upserts)
    {
        const TSharedPtr<FJsonObject>* Snapshot = nullptr;
        if (!Value.IsValid() || !Value->TryGetObject(Snapshot) || !Snapshot || !Snapshot->IsValid())
        {
            bAllApplied = false;
            continue;
        }
        bAllApplied = ProcessSnapshot(*Snapshot, true) && bAllApplied;
    }

    if (!bAllApplied)
    {
        UE_LOG(LogTemp, Error,
               TEXT("[孪生管理器] delta 应用不完整，保留旧 cursor 以便幂等重试"));
        return;
    }

    IncrementalSnapshotCursor = NewCursor;
    if (!bInitialModelBaselineReady)
    {
        CompleteInitialModelBaseline(InstanceRegistry.Num());
    }
    if (Upserts->Num() > 0 || DeletedIds->Num() > 0)
    {
        UE_LOG(LogTemp, Log,
               TEXT("[孪生管理器] 增量已应用 | revision=%lld | upserts=%d | deleted=%d | bytes=%d"),
               static_cast<int64>(RevisionNumber), Upserts->Num(), DeletedIds->Num(), Response->GetContentLength());
    }
}

IOntoTwinRealtimeStreamProvider* ATwinSceneManager::FindRealtimeProviderForWorld(
    bool bLogDuplicates) const
{
    IOntoTwinRealtimeStreamProvider* FirstProvider = nullptr;
    int32 ProviderCount = 0;
    const TArray<IOntoTwinRealtimeStreamProvider*> Providers =
        IModularFeatures::Get().GetModularFeatureImplementations<IOntoTwinRealtimeStreamProvider>(
            IOntoTwinRealtimeStreamProvider::GetModularFeatureName());
    for (IOntoTwinRealtimeStreamProvider* Provider : Providers)
    {
        if (Provider && Provider->GetRealtimeStreamWorld() == GetWorld())
        {
            if (FirstProvider == nullptr)
            {
                FirstProvider = Provider;
            }
            ++ProviderCount;
        }
    }
    if (bLogDuplicates && ProviderCount > 1)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[OntoTwinWS] 同一 World 注册了 %d 个外部 Provider；仅使用第一个，拒绝兼容回退抢占"),
            ProviderCount);
    }
    return FirstProvider;
}

void ATwinSceneManager::StopLegacyRealtimeSocketForExternalProvider()
{
    GetWorldTimerManager().ClearTimer(RealtimeReconnectTimerHandle);
    GetWorldTimerManager().ClearTimer(RealtimeProviderProbeTimerHandle);
    bRealtimeReconnectScheduled = false;
    bRealtimeProviderProbePending = false;
    ++RealtimeConnectionGeneration;
    if (RealtimeSocket.IsValid())
    {
        if (RealtimeSocket->IsConnected())
        {
            RealtimeSocket->Close();
        }
        RealtimeSocket.Reset();
    }

    // 兼容回退 socket 产生的状态不能泄漏到外部 Provider 的健康视图。
    RealtimeTargetStates.Empty();
    RealtimeAppliedInstanceIds.Empty();
    RealtimeCategoryHealth.Empty();
    RealtimeLastTargetCount = 0;
    RealtimeLastAppliedCount = 0;
    RealtimeLastFramePlatformSeconds = -1.0;
    RealtimeLastSourceTimestampMs = 0;
    RealtimeConnectionState = bEnableRealtimeWebSocket ? TEXT("provider") : TEXT("disabled");
    RealtimeLastError.Empty();
}

void ATwinSceneManager::ProbeRealtimeProviderAndConnect()
{
    bRealtimeProviderProbePending = false;
    if (bRealtimeClosing || !bEnableRealtimeWebSocket)
    {
        return;
    }

    if (FindRealtimeProviderForWorld(true) != nullptr)
    {
        bRealtimeUsingExternalProvider = true;
        StopLegacyRealtimeSocketForExternalProvider();
        UE_LOG(LogTemp, Log,
            TEXT("[OntoTwinWS] 检测到同 World 外部 Provider，跳过 OntoTwinSync 兼容 socket"));
        return;
    }

    bRealtimeUsingExternalProvider = false;
    if (!FModuleManager::Get().IsModuleLoaded(TEXT("WebSockets")))
    {
        FModuleManager::Get().LoadModule(TEXT("WebSockets"));
    }
    if (!FHttpModule::Get().GetProxyAddress().IsEmpty())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[OntoTwinWS] UE HTTP 代理仍处于启用状态，内网 WebSocket 可能被代理拦截: %s"),
            *FHttpModule::Get().GetProxyAddress());
    }
    ConnectRealtimeWebSocket();
}

void ATwinSceneManager::ReconcileRealtimeProviderOwnership()
{
    if (bRealtimeClosing || !bEnableRealtimeWebSocket)
    {
        return;
    }

    IOntoTwinRealtimeStreamProvider* Provider = FindRealtimeProviderForWorld(false);
    if (Provider != nullptr)
    {
        if (!bRealtimeUsingExternalProvider || RealtimeSocket.IsValid()
            || bRealtimeReconnectScheduled || bRealtimeProviderProbePending)
        {
            bRealtimeUsingExternalProvider = true;
            StopLegacyRealtimeSocketForExternalProvider();
        }
        return;
    }

    if (bRealtimeUsingExternalProvider)
    {
        bRealtimeUsingExternalProvider = false;
        if (!bRealtimeProviderProbePending)
        {
            bRealtimeProviderProbePending = true;
            GetWorldTimerManager().SetTimer(
                RealtimeProviderProbeTimerHandle,
                FTimerDelegate::CreateWeakLambda(this, [this]()
                {
                    ProbeRealtimeProviderAndConnect();
                }),
                RealtimeProviderStartupGraceSeconds,
                false);
        }
    }
}

void ATwinSceneManager::ApplyRealtimeWebSocketEnabled(bool bEnabled)
{
    if (bRealtimeClosing)
    {
        return;
    }

    // 期望开关由 Manager 记住，即使 Provider 负责实际 socket，后续 Provider
    // 注销后仍能按同一意图进入兼容回退。
    bEnableRealtimeWebSocket = bEnabled;
    IOntoTwinRealtimeStreamProvider* Provider = FindRealtimeProviderForWorld(true);
    if (Provider != nullptr)
    {
        bRealtimeUsingExternalProvider = true;
        StopLegacyRealtimeSocketForExternalProvider();
        Provider->ApplyRealtimeStreamEnabled(bEnabled);
        return;
    }

    GetWorldTimerManager().ClearTimer(RealtimeReconnectTimerHandle);
    bRealtimeReconnectScheduled = false;
    ++RealtimeConnectionGeneration;

    if (!bEnabled)
    {
        GetWorldTimerManager().ClearTimer(RealtimeProviderProbeTimerHandle);
        bRealtimeProviderProbePending = false;
        bRealtimeUsingExternalProvider = false;
        if (RealtimeSocket.IsValid())
        {
            if (RealtimeSocket->IsConnected())
            {
                RealtimeSocket->Close();
            }
            RealtimeSocket.Reset();
        }
        RealtimeTargetStates.Empty();
        RealtimeAppliedInstanceIds.Empty();
        RealtimeCategoryHealth.Empty();
        RealtimeLastTargetCount = 0;
        RealtimeLastAppliedCount = 0;
        RealtimeLastFramePlatformSeconds = -1.0;
        RealtimeConnectionState = TEXT("disabled");
        RealtimeLastError.Empty();
        UE_LOG(LogTemp, Log,
            TEXT("[OntoTwinWS] 外部数据监控台已关闭实时流；HTTP 快照继续运行"));
        return;
    }

    // 开启时同样走探测窗口；Provider 已在路上时不创建第二个 socket。
    bRealtimeUsingExternalProvider = false;
    if (!bRealtimeProviderProbePending)
    {
        bRealtimeProviderProbePending = true;
        GetWorldTimerManager().SetTimer(
            RealtimeProviderProbeTimerHandle,
            FTimerDelegate::CreateWeakLambda(this, [this]()
            {
                ProbeRealtimeProviderAndConnect();
            }),
            RealtimeProviderStartupGraceSeconds,
            false);
    }
    UE_LOG(LogTemp, Log, TEXT("[OntoTwinWS] 外部数据监控台已开启实时流，等待 Provider 所有权确认"));
}

void ATwinSceneManager::ConnectRealtimeWebSocket()
{
    if (bRealtimeClosing)
    {
        return;
    }
    if (!bEnableRealtimeWebSocket)
    {
        RealtimeConnectionState = TEXT("disabled");
        return;
    }
    if (IOntoTwinRealtimeStreamProvider* Provider = FindRealtimeProviderForWorld(true))
    {
        bRealtimeUsingExternalProvider = true;
        StopLegacyRealtimeSocketForExternalProvider();
        UE_LOG(LogTemp, Log,
            TEXT("[OntoTwinWS] Provider 已拥有当前 World，拒绝创建兼容 socket (%s)"),
            *Provider->GetRealtimeStreamId());
        return;
    }
    bRealtimeUsingExternalProvider = false;
    if (RealtimeWebSocketUrl.IsEmpty())
    {
        RealtimeConnectionState = TEXT("error");
        RealtimeLastError = TEXT("WebSocket URL is empty");
        return;
    }

    bRealtimeReconnectScheduled = false;
    RealtimeConnectionState = TEXT("connecting");
    const int32 ThisGeneration = ++RealtimeConnectionGeneration;

    if (RealtimeSocket.IsValid())
    {
        if (RealtimeSocket->IsConnected())
        {
            RealtimeSocket->Close();
        }
        RealtimeSocket.Reset();
    }

    // 与现场 MetaverseClient 保持一致。部分 libwebsockets 构建需要一个客户端协议名。
    RealtimeSocket = FWebSocketsModule::Get().CreateWebSocket(RealtimeWebSocketUrl, TEXT("ws"));

    RealtimeSocket->OnConnected().AddWeakLambda(this, [this, ThisGeneration]()
    {
        if (ThisGeneration != RealtimeConnectionGeneration) return;
        RealtimeConnectionState = TEXT("connected");
        RealtimeLastError.Empty();
        UE_LOG(LogTemp, Log, TEXT("[OntoTwinWS] 已连接 %s"), *RealtimeWebSocketUrl);
    });

    RealtimeSocket->OnConnectionError().AddWeakLambda(
        this, [this, ThisGeneration](const FString& Error)
    {
        if (ThisGeneration != RealtimeConnectionGeneration) return;
        RealtimeConnectionState = TEXT("error");
        RealtimeLastError = Error;
        UE_LOG(LogTemp, Warning, TEXT("[OntoTwinWS] 连接错误: %s"), *Error);
        ScheduleRealtimeReconnect();
    });

    RealtimeSocket->OnClosed().AddWeakLambda(
        this, [this, ThisGeneration](int32 StatusCode, const FString& Reason, bool bWasClean)
    {
        if (ThisGeneration != RealtimeConnectionGeneration) return;
        RealtimeConnectionState = TEXT("disconnected");
        RealtimeLastError = Reason;
        UE_LOG(LogTemp, Warning,
            TEXT("[OntoTwinWS] 已断开 code=%d clean=%s reason=%s"),
            StatusCode, bWasClean ? TEXT("true") : TEXT("false"), *Reason);
        if (!bRealtimeClosing)
        {
            ScheduleRealtimeReconnect();
        }
    });

    RealtimeSocket->OnMessage().AddWeakLambda(
        this, [this, ThisGeneration](const FString& Message)
    {
        if (ThisGeneration != RealtimeConnectionGeneration) return;
        HandleRealtimeMessage(Message);
    });

    RealtimeSocket->Connect();
}

void ATwinSceneManager::ScheduleRealtimeReconnect()
{
    if (bRealtimeClosing || bRealtimeReconnectScheduled || !GetWorld()
        || FindRealtimeProviderForWorld(false) != nullptr)
    {
        return;
    }

    bRealtimeReconnectScheduled = true;
    RealtimeConnectionState = TEXT("reconnecting");
    GetWorldTimerManager().SetTimer(
        RealtimeReconnectTimerHandle,
        FTimerDelegate::CreateWeakLambda(this, [this]()
        {
            bRealtimeReconnectScheduled = false;
            ConnectRealtimeWebSocket();
        }),
        FMath::Max(0.5f, RealtimeReconnectSeconds),
        false);
}

void ATwinSceneManager::HandleRealtimeMessage(const FString& Message)
{
    TSharedPtr<FJsonObject> Frame;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);
    if (!FJsonSerializer::Deserialize(Reader, Frame) || !Frame.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("[OntoTwinWS] 无法解析状态帧"));
        return;
    }

    const TArray<TSharedPtr<FJsonValue>>* Targets = nullptr;
    if (!Frame->TryGetArrayField(TEXT("targets"), Targets))
    {
        UE_LOG(LogTemp, Warning, TEXT("[OntoTwinWS] 状态帧缺少 targets"));
        return;
    }

    int32 AppliedCount = 0;
    TMap<FString, FString> FrameTargetStates;
    TSet<FString> FrameAppliedInstanceIds;
    TMap<FString, FRealtimeCategoryHealth> FrameCategoryHealth;

    auto NormalizeCategory = [](FString Category)
    {
        Category.TrimStartAndEndInline();
        Category.ToLowerInline();
        return Category.IsEmpty() ? FString(TEXT("unknown")) : Category;
    };
    auto AddReason = [](FRealtimeCategoryHealth& Stats, const TCHAR* Reason)
    {
        Stats.UnappliedReasons.FindOrAdd(Reason)++;
    };

    for (const TSharedPtr<FJsonValue>& Value : *Targets)
    {
        const TSharedPtr<FJsonObject>* Target = nullptr;
        if (!Value.IsValid() || !Value->TryGetObject(Target) || !Target || !Target->IsValid())
        {
            FRealtimeCategoryHealth& Stats = FrameCategoryHealth.FindOrAdd(TEXT("unknown"));
            ++Stats.TargetCount;
            AddReason(Stats, TEXT("invalid"));
            continue;
        }

        FString Category;
        (*Target)->TryGetStringField(TEXT("clazz"), Category);
        Category = NormalizeCategory(MoveTemp(Category));
        FString ObjectTypeRid;
        (*Target)->TryGetStringField(TEXT("objectTypeRid"), ObjectTypeRid);
        FRealtimeCategoryHealth& CategoryStats = FrameCategoryHealth.FindOrAdd(Category);
        ++CategoryStats.TargetCount;

        FString InstanceId;
        FString State;
        if (!(*Target)->TryGetStringField(TEXT("key"), InstanceId) || InstanceId.IsEmpty())
        {
            AddReason(CategoryStats, TEXT("invalid"));
            continue;
        }

        (*Target)->TryGetStringField(TEXT("state"), State);
        if (State.IsEmpty())
        {
            State = TEXT("active");
        }
        FrameTargetStates.Add(InstanceId, State.ToLower());
        if (State.Equals(TEXT("lost"), ESearchCase::IgnoreCase))
        {
            AddReason(CategoryStats, TEXT("lost"));
            continue;
        }

        double X = 0.0;
        double Y = 0.0;
        double Heading = 0.0;
        if (!(*Target)->TryGetNumberField(TEXT("x"), X)
            || !(*Target)->TryGetNumberField(TEXT("y"), Y)
            || !(*Target)->TryGetNumberField(TEXT("heading"), Heading))
        {
            AddReason(CategoryStats, TEXT("invalid"));
            continue;
        }

        ATwinInstance** Found = InstanceRegistry.Find(InstanceId);
        if (Found && *Found && IsValid(*Found))
        {
            (*Found)->ApplyRealtimeSpatial(X, Y, Heading, RealtimeSpatialHoldSeconds);
            RealtimeMissingInstanceWarnings.Remove(InstanceId);
            FrameAppliedInstanceIds.Add(InstanceId);
            ++CategoryStats.AppliedCount;
            ++AppliedCount;
        }
        else
        {
            AddReason(CategoryStats,
                (!ObjectTypeRid.IsEmpty() || Category != TEXT("unknown"))
                    ? TEXT("instance_missing")
                    : TEXT("unmapped"));
            if (!RealtimeMissingInstanceWarnings.Contains(InstanceId))
            {
                RealtimeMissingInstanceWarnings.Add(InstanceId);
                UE_LOG(LogTemp, Warning,
                    TEXT("[OntoTwinWS] 收到 %s，但 HTTP 档案尚未创建对应 TwinInstance"),
                    *InstanceId);
            }
        }
    }

    double SourceTimestampMs = 0.0;
    if (Frame->TryGetNumberField(TEXT("source_timestamp_ms"), SourceTimestampMs)
        || Frame->TryGetNumberField(TEXT("timestamp_ms"), SourceTimestampMs))
    {
        RealtimeLastSourceTimestampMs = FMath::Max<int64>(0, static_cast<int64>(SourceTimestampMs));
    }
    RealtimeTargetStates = MoveTemp(FrameTargetStates);
    RealtimeAppliedInstanceIds = MoveTemp(FrameAppliedInstanceIds);
    RealtimeCategoryHealth = MoveTemp(FrameCategoryHealth);
    RealtimeLastTargetCount = Targets->Num();
    RealtimeLastAppliedCount = AppliedCount;
    RealtimeLastFramePlatformSeconds = FPlatformTime::Seconds();
    ++RealtimeFrameCount;
    if (RealtimeFrameCount == 1 || RealtimeFrameCount % 300 == 0)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[OntoTwinWS] 状态帧=%lld targets=%d applied=%d"),
            RealtimeFrameCount, Targets->Num(), AppliedCount);
    }
}

TSharedRef<FJsonObject> ATwinSceneManager::BuildRealtimeChannelHealth() const
{
    if (IOntoTwinRealtimeStreamProvider* Provider = FindRealtimeProviderForWorld(false))
    {
        return Provider->BuildRealtimeStreamHealth();
    }

    const TSharedRef<FJsonObject> Health = MakeShared<FJsonObject>();
    Health->SetBoolField(TEXT("enabled"), bEnableRealtimeWebSocket);
    Health->SetBoolField(TEXT("reported_enabled"), bEnableRealtimeWebSocket);
    Health->SetBoolField(TEXT("command_configured"), true);
    Health->SetBoolField(TEXT("in_sync"), true);
    Health->SetStringField(TEXT("stream_id"), TEXT("ontotwin.instances.primary"));
    Health->SetStringField(TEXT("owner"), TEXT("OntoTwinSync"));
    Health->SetStringField(TEXT("owner_state"), TEXT("compatibility_fallback"));
    Health->SetBoolField(TEXT("compatibility_fallback"), true);
    Health->SetStringField(TEXT("url"), RealtimeWebSocketUrl);
    Health->SetStringField(TEXT("connection_state"),
        bEnableRealtimeWebSocket ? RealtimeConnectionState : TEXT("disabled"));

    const double LastFrameAgeMs = RealtimeLastFramePlatformSeconds >= 0.0
        ? FMath::Max(0.0, (FPlatformTime::Seconds() - RealtimeLastFramePlatformSeconds) * 1000.0)
        : -1.0;
    const bool bFrameFresh = bEnableRealtimeWebSocket
        && RealtimeConnectionState == TEXT("connected")
        && LastFrameAgeMs >= 0.0
        && LastFrameAgeMs <= RealtimeFrameFreshnessSeconds * 1000.0;

    Health->SetStringField(TEXT("active_source"), bFrameFresh ? TEXT("websocket") : TEXT("none"));
    if (LastFrameAgeMs >= 0.0)
    {
        Health->SetNumberField(TEXT("last_frame_age_ms"), LastFrameAgeMs);
    }
    else
    {
        Health->SetField(TEXT("last_frame_age_ms"), MakeShared<FJsonValueNull>());
    }
    Health->SetNumberField(TEXT("frame_count"), static_cast<double>(RealtimeFrameCount));
    Health->SetNumberField(TEXT("source_timestamp_ms"), static_cast<double>(RealtimeLastSourceTimestampMs));
    Health->SetNumberField(TEXT("target_count"), RealtimeLastTargetCount);
    Health->SetNumberField(TEXT("applied_target_count"), RealtimeLastAppliedCount);
    Health->SetNumberField(
        TEXT("unapplied_target_count"),
        FMath::Max(0, RealtimeLastTargetCount - RealtimeLastAppliedCount));

    FString ExecutionState = TEXT("waiting");
    if (!bEnableRealtimeWebSocket)
    {
        ExecutionState = TEXT("waiting");
    }
    else if (RealtimeLastTargetCount > 0 && RealtimeLastAppliedCount == 0)
    {
        ExecutionState = TEXT("none_applied");
    }
    else if (RealtimeLastTargetCount > 0
        && RealtimeLastAppliedCount < RealtimeLastTargetCount)
    {
        ExecutionState = TEXT("partial");
    }
    else if (RealtimeLastTargetCount > 0
        && RealtimeLastAppliedCount == RealtimeLastTargetCount)
    {
        ExecutionState = TEXT("ready");
    }
    Health->SetStringField(TEXT("execution_state"), ExecutionState);

    TArray<FString> CategoryIds;
    RealtimeCategoryHealth.GetKeys(CategoryIds);
    CategoryIds.Sort();
    TArray<TSharedPtr<FJsonValue>> Categories;
    TMap<FString, int32> AggregateReasons;
    for (const FString& CategoryId : CategoryIds)
    {
        const FRealtimeCategoryHealth* Stats = RealtimeCategoryHealth.Find(CategoryId);
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
        }
        else if (CategoryId == TEXT("agv"))
        {
            Category->SetStringField(TEXT("label"), TEXT("AGV"));
            Category->SetStringField(TEXT("lifecycle"), TEXT("formal_instance"));
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
            if (ReasonId == TEXT("instance_missing"))
            {
                Reason->SetStringField(TEXT("message"), TEXT("尚未找到对应的正式实例"));
            }
            else if (ReasonId == TEXT("handler_unavailable"))
            {
                Reason->SetStringField(TEXT("message"), TEXT("没有可用的场景处理器"));
            }
            else if (ReasonId == TEXT("lost"))
            {
                Reason->SetStringField(TEXT("message"), TEXT("目标已标记为离场"));
            }
            else
            {
                Reason->SetStringField(TEXT("message"), TEXT("目标数据无效"));
            }
            Reasons.Add(MakeShared<FJsonValueObject>(Reason));
        }
        Category->SetArrayField(TEXT("reasons"), Reasons);
        Categories.Add(MakeShared<FJsonValueObject>(Category));
    }
    Health->SetArrayField(TEXT("categories"), Categories);

    TArray<FString> AggregateReasonIds;
    AggregateReasons.GetKeys(AggregateReasonIds);
    AggregateReasonIds.Sort();
    TArray<TSharedPtr<FJsonValue>> UnappliedReasons;
    for (const FString& ReasonId : AggregateReasonIds)
    {
        const TSharedRef<FJsonObject> Reason = MakeShared<FJsonObject>();
        Reason->SetStringField(TEXT("code"), ReasonId);
        Reason->SetNumberField(TEXT("count"), AggregateReasons.FindRef(ReasonId));
        UnappliedReasons.Add(MakeShared<FJsonValueObject>(Reason));
    }
    Health->SetArrayField(TEXT("unapplied_reasons"), UnappliedReasons);
    Health->SetStringField(TEXT("error"), RealtimeLastError);

    TArray<FString> InstanceIds;
    RealtimeTargetStates.GetKeys(InstanceIds);
    InstanceIds.Sort();
    TArray<TSharedPtr<FJsonValue>> Targets;
    Targets.Reserve(InstanceIds.Num());
    for (const FString& InstanceId : InstanceIds)
    {
        const TSharedRef<FJsonObject> Target = MakeShared<FJsonObject>();
        Target->SetStringField(TEXT("instance_id"), InstanceId);
        Target->SetStringField(TEXT("state"), RealtimeTargetStates.FindRef(InstanceId));
        Target->SetBoolField(TEXT("applied"), RealtimeAppliedInstanceIds.Contains(InstanceId));
        Targets.Add(MakeShared<FJsonValueObject>(Target));
    }
    Health->SetArrayField(TEXT("targets"), Targets);
    return Health;
}

// ═══════════════════════════════════════════════════════════════════════════
// 实例处理
// ═══════════════════════════════════════════════════════════════════════════

bool ATwinSceneManager::ProcessSnapshot(const TSharedPtr<FJsonObject>& Snapshot, bool bIsDelta)
{
    if (!Snapshot.IsValid())
    {
        return false;
    }

    FString InstanceId;
    if (!Snapshot->TryGetStringField(TEXT("instanceId"), InstanceId) || InstanceId.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[孪生管理器] 忽略缺少 instanceId 的快照"));
        return false;
    }

    // ── 已存在：更新状态 ─────────────────────────────────────────────────
    ATwinInstance** Found = InstanceRegistry.Find(InstanceId);
    if (Found && *Found && IsValid(*Found))
    {
        (*Found)->ApplySnapshot(Snapshot, bIsDelta);
        return true;
    }

    // ── 不存在：创建新实例 ───────────────────────────────────────────────
    ATwinInstance* NewInst = SpawnTwinInstance(InstanceId, Snapshot);
    if (NewInst)
    {
        InstanceRegistry.Add(InstanceId, NewInst);
        UE_LOG(LogTemp, Log, TEXT("[孪生管理器] 新增实例: %s"), *InstanceId);
        return true;
    }
    return false;
}

ATwinInstance* ATwinSceneManager::SpawnTwinInstance(
    const FString& InstanceId,
    const TSharedPtr<FJsonObject>& Snapshot)
{
    UWorld* World = GetWorld();
    if (!World) return nullptr;

    // ── 解析 asset_id（UE 内容路径） ─────────────────────────────────────
    FString AssetPathStr;
    FString ContainerBlueprintPath;
    FString ContainerSlot = TEXT("primary");
    const TSharedPtr<FJsonObject>* InterfacesObj;
    if (Snapshot->TryGetObjectField(TEXT("interfaces"), InterfacesObj))
    {
        const TSharedPtr<FJsonObject>* RepObj;
        if ((*InterfacesObj)->TryGetObjectField(TEXT("I3D_Representable"), RepObj))
        {
            (*RepObj)->TryGetStringField(TEXT("asset_id"), AssetPathStr);
            (*RepObj)->TryGetStringField(
                TEXT("container_blueprint_id"),
                ContainerBlueprintPath);
            (*RepObj)->TryGetStringField(TEXT("container_slot"), ContainerSlot);
            if (ContainerSlot.IsEmpty())
            {
                ContainerSlot = TEXT("primary");
            }
        }
    }

    // ── 解析初始位置 ─────────────────────────────────────────────────────
    FVector SpawnLocation = FVector::ZeroVector;
    if (Snapshot->TryGetObjectField(TEXT("interfaces"), InterfacesObj))
    {
        const TSharedPtr<FJsonObject>* SpatialObj;
        if ((*InterfacesObj)->TryGetObjectField(TEXT("I3D_Spatial"), SpatialObj))
        {
            double tx = 0, ty = 0, tz = 0;
            (*SpatialObj)->TryGetNumberField(TEXT("translation_x"), tx);
            (*SpatialObj)->TryGetNumberField(TEXT("translation_y"), ty);
            (*SpatialObj)->TryGetNumberField(TEXT("translation_z"), tz);
            SpawnLocation = FVector(tx, ty, tz);
        }
    }

    // ── Spawn ATwinInstance ──────────────────────────────────────────────
    FActorSpawnParameters SpawnParams;
    SpawnParams.Name = FName(*FString::Printf(TEXT("Twin_%s"), *InstanceId));

    UClass* SpawnClass = InstanceClass ? InstanceClass.Get() : ATwinInstance::StaticClass();

    ATwinInstance* Inst = World->SpawnActor<ATwinInstance>(
        SpawnClass,
        SpawnLocation,
        FRotator::ZeroRotator,
        SpawnParams
    );

    if (!Inst)
    {
        UE_LOG(LogTemp, Error, TEXT("[孪生管理器] Spawn 失败: %s"), *InstanceId);
        return nullptr;
    }

    // ── 设置标签和名称 ───────────────────────────────────────────────────
    FString DisplayName;
    if (!Snapshot->TryGetStringField(TEXT("displayName"), DisplayName) || DisplayName.IsEmpty())
    {
        DisplayName = InstanceId;
    }
#if WITH_EDITOR
    Inst->SetActorLabel(DisplayName);
#endif
    Inst->Tags.Add(FName(*InstanceId));
    Inst->TwinDisplayName = DisplayName;

    // ── 放入世界大纲特定文件夹下 ─────────────────────────────────────────
#if WITH_EDITOR
    Inst->SetFolderPath(BuildTwinFolderPath(Snapshot, TEXT("TwinInstances")));
#endif

    // ── 初始化孪生实例 ───────────────────────────────────────────────────
    Inst->InitializeTwin(
        InstanceId,
        AssetPathStr,
        BackendBaseUrl,
        ContainerBlueprintPath,
        ContainerSlot);

    // ── 首次应用快照 ─────────────────────────────────────────────────────
    Inst->ApplySnapshot(Snapshot);

    return Inst;
}

void ATwinSceneManager::DestroyTwinInstance(const FString& InstanceId)
{
    ATwinInstance** Found = InstanceRegistry.Find(InstanceId);
    if (Found && *Found && IsValid(*Found))
    {
        if (OverlaySelectedInstance == *Found)
        {
            ClearOverlaySelection();
        }
        if (RuntimeSelectedInstances.Contains(*Found))
        {
            RuntimeSelectedInstances.Remove(*Found);
            RuntimeSelectedInstance = RuntimeSelectedInstances.Num() > 0 ? RuntimeSelectedInstances[0] : nullptr;
            ReleaseRuntimeEditState(InstanceId, false);
            UpdateRuntimeSelectionGeometry();
            RefreshRuntimeEditDirtyState();
        }

        UE_LOG(LogTemp, Log, TEXT("[孪生管理器] 销毁实例: %s"), *InstanceId);
        (*Found)->Destroy();
    }
    InstanceRegistry.Remove(InstanceId);
}

// ═══════════════════════════════════════════════════════════════════════════
// Runtime Editor（打包 exe 内轻量编辑模式）
// ═══════════════════════════════════════════════════════════════════════════

void ATwinSceneManager::ToggleRuntimeEditMode()
{
    if (!bEnableRuntimeEditor)
    {
        RuntimeStatusMessage = TEXT("当前管理器未启用运行时编辑器");
        UpdateRuntimeEditorPanel();
        return;
    }

    if (!bRuntimeEditMode && InteractionManager && InteractionManager->IsRoamingActive())
    {
        InteractionManager->ExitRoaming();
    }

    if (bRuntimeEditMode)
    {
        ExitRuntimeEditMode();
    }
    else
    {
        EnterRuntimeEditMode();
    }
}

void ATwinSceneManager::RequestRuntimeEditToggle()
{
    if (bRuntimeEditMode && RuntimeEditorPanel && IsValid(RuntimeEditorPanel)
        && RuntimeEditorPanel->IsConfirmationOpen())
    {
        return;
    }
    UWorld* World = GetWorld();
    const float Now = World ? World->GetTimeSeconds() : 0.0f;
    if (Now - RuntimeLastToggleInputTime < 0.15f)
    {
        return;
    }

    RuntimeLastToggleInputTime = Now;
    ToggleRuntimeEditMode();
}

void ATwinSceneManager::RequestRoamingAfterRuntimeEdit()
{
    if (!bRuntimeEditMode)
    {
        if (InteractionManager && !InteractionManager->IsRoamingActive())
        {
            InteractionManager->ToggleRoaming();
        }
        return;
    }
    if (bRuntimeEnterRoamingAfterExit
        || (RuntimeEditorPanel && IsValid(RuntimeEditorPanel)
            && RuntimeEditorPanel->IsConfirmationOpen()))
    {
        return;
    }

    bRuntimeEnterRoamingAfterExit = true;
    ExitRuntimeEditMode();
}

void ATwinSceneManager::CancelRuntimeEditExitRequest()
{
    bRuntimeExitAfterSave = false;
    bRuntimeEnterRoamingAfterExit = false;
}

void ATwinSceneManager::EnterRuntimeEditMode()
{
    if (bRuntimeEditMode || !bEnableRuntimeEditor)
    {
        return;
    }

    if (InteractionManager) InteractionManager->SetRuntimeEditorSuppressed(true);
    if (WebInteractionManager) WebInteractionManager->SetRuntimeEditorSuppressed(true);
    RuntimeBusinessConfig.Reset();
    RuntimeBusinessBaseline.Reset();
    RuntimeBusinessRevision = -1;
    bRuntimeBusinessDirty = false;
    if (WebInteractionManager)
    {
        TSharedPtr<FJsonObject> Snapshot;
        int32 SnapshotRevision = -1;
        if (WebInteractionManager->GetRuntimeBusinessEditSnapshot(
            Snapshot, SnapshotRevision))
        {
            RuntimeBusinessConfig = Snapshot;
            RuntimeBusinessBaseline = CloneRuntimeBusinessConfig(Snapshot);
            RuntimeBusinessRevision = SnapshotRevision;
        }
    }
    bRuntimeEditMode = true;
    RuntimeStatusMessage = TEXT("运行时编辑已开启");
    SetPollTimerInterval(EditModePollInterval, EditModePollInterval);

    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    if (PC)
    {
        bRuntimePreviousMouseCursor = PC->bShowMouseCursor;
        if (bEnableRuntimeEditorFreeCamera && !StartRuntimeEditorCamera(PC))
        {
            RuntimeStatusMessage = TEXT("运行时编辑已开启，自由相机不可用");
        }
        PC->bShowMouseCursor = true;

        FInputModeGameAndUI InputMode;
        InputMode.SetHideCursorDuringCapture(false);
        InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        PC->SetInputMode(InputMode);
    }

    EnsureRuntimeGizmo();
    ShowRuntimeEditorPanel();
    CheckRuntimeBindingStatus();
    UpdateRuntimeEditorPanel();
}

void ATwinSceneManager::ExitRuntimeEditMode()
{
    if (!bRuntimeEditMode)
    {
        return;
    }

    if (bRuntimeEditDirty)
    {
        RuntimeStatusMessage = TEXT("存在待保存修改");
        if (RuntimeEditorPanel && IsValid(RuntimeEditorPanel))
        {
            RuntimeEditorPanel->ShowExitConfirmation();
        }
        UpdateRuntimeEditorPanel();
        return;
    }

    FinishExitRuntimeEditMode();
}

void ATwinSceneManager::FinishExitRuntimeEditMode()
{
    if (!bRuntimeEditMode) return;

    const bool bEnterRoamingAfterExit = bRuntimeEnterRoamingAfterExit;
    bRuntimeEnterRoamingAfterExit = false;

    ClearRuntimeSelection(false);
    ReleaseCleanUnselectedRuntimeStates();
    HideRuntimeEditorPanel();

    if (RuntimeGizmo && IsValid(RuntimeGizmo))
    {
        RuntimeGizmo->SetGizmoEnabled(false);
    }

    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    if (PC)
    {
        StopRuntimeEditorCamera(PC);
        PC->bShowMouseCursor = bRuntimePreviousMouseCursor;
        FInputModeGameOnly InputMode;
        PC->SetInputMode(InputMode);
    }
    if (InteractionManager)
    {
        InteractionManager->RestoreStartupView();
    }

    bRuntimeEditMode = false;
    RuntimeBusinessConfig.Reset();
    RuntimeBusinessBaseline.Reset();
    RuntimeBusinessRevision = -1;
    bRuntimeBusinessDirty = false;
    bRuntimeExitAfterSave = false;
    RuntimeStatusMessage = TEXT("运行时编辑已关闭");
    SetPollTimerInterval(PollInterval, PollInterval);
    if (InteractionManager) InteractionManager->SetRuntimeEditorSuppressed(false);
    if (WebInteractionManager) WebInteractionManager->SetRuntimeEditorSuppressed(false);

    if (bEnterRoamingAfterExit && InteractionManager && GetWorld())
    {
        TWeakObjectPtr<UTwinInteractionManagerComponent> WeakInteractionManager(
            InteractionManager);
        GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda(
            [WeakInteractionManager]()
            {
                UTwinInteractionManagerComponent* Manager = WeakInteractionManager.Get();
                if (Manager && !Manager->IsRoamingActive())
                {
                    Manager->ToggleRoaming();
                }
            }));
    }
}

void ATwinSceneManager::SaveAndExitRuntimeEdit()
{
    if (!bRuntimeEditDirty)
    {
        FinishExitRuntimeEditMode();
        return;
    }
    bRuntimeExitAfterSave = true;
    SaveRuntimeEdit();
}

void ATwinSceneManager::DiscardAndExitRuntimeEdit()
{
    CancelRuntimeEdit();
    FinishExitRuntimeEditMode();
}

void ATwinSceneManager::TickRuntimeEditor(float DeltaTime)
{
    if (!bEnableRuntimeEditor)
    {
        return;
    }

    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    if (!PC)
    {
        return;
    }

    if (PC->WasInputKeyJustPressed(RuntimeEditorToggleKey))
    {
        RequestRuntimeEditToggle();
        return;
    }

    if (!bRuntimeEditMode)
    {
        return;
    }

    // Web 投影与 F10 可能同时启动。只要用户尚未编辑业务，就持续接收更新版本，
    // 避免进入 F10 时的一次性空快照把业务下拉永久锁死。
    RefreshRuntimeBusinessSnapshot();

    if (RuntimeEditorPanel && IsValid(RuntimeEditorPanel) && RuntimeEditorPanel->IsConfirmationOpen())
    {
        TickRuntimeEditorCamera(PC, DeltaTime, true);
        return;
    }

    const bool bCtrlDown = PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl);
    const bool bShiftDown = PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift);
    if (bCtrlDown && PC->WasInputKeyJustPressed(SaveKey))
    {
        SaveRuntimeEdit();
    }
    if (bCtrlDown && PC->WasInputKeyJustPressed(EKeys::Z))
    {
        UndoRuntimeEdit();
    }
    if (bCtrlDown && PC->WasInputKeyJustPressed(EKeys::Y))
    {
        RedoRuntimeEdit();
    }
    if (PC->WasInputKeyJustPressed(EKeys::Delete))
    {
        RemoveRuntimeSelectionFromScene();
    }
    if (PC->WasInputKeyJustPressed(CancelKey) &&
        !(RuntimeEditorPanel && IsValid(RuntimeEditorPanel) && RuntimeEditorPanel->IsConfirmationOpen()))
    {
        if (bRuntimeDragging)
        {
            EndRuntimeGizmoDrag(false);
        }
        else
        {
            ClearRuntimeSelection(false);
            RuntimeStatusMessage = TEXT("已清空当前选择");
        }
    }

    const bool bPointerOverRuntimePanel =
        RuntimeEditorPanel && IsValid(RuntimeEditorPanel) && RuntimeEditorPanel->IsPointerOverPanel();

    TickRuntimeEditorCamera(PC, DeltaTime, bPointerOverRuntimePanel);

    RuntimeHoverPart = ERuntimeDragPart::None;
    if (!bRuntimeDragging && !bPointerOverRuntimePanel && RuntimeSelectedInstances.Num() > 0)
    {
        FHitResult HoverHit;
        if (TraceRuntimeCursor(HoverHit) && RuntimeGizmo && IsValid(RuntimeGizmo))
        {
            const EOntoTwinRuntimeGizmoPart HoverGizmoPart = RuntimeGizmo->GetPartForComponent(HoverHit.GetComponent());
            if (HoverGizmoPart == EOntoTwinRuntimeGizmoPart::MoveX)
            {
                RuntimeHoverPart = ERuntimeDragPart::MoveX;
            }
            else if (HoverGizmoPart == EOntoTwinRuntimeGizmoPart::MoveY)
            {
                RuntimeHoverPart = ERuntimeDragPart::MoveY;
            }
            else if (HoverGizmoPart == EOntoTwinRuntimeGizmoPart::MoveXY)
            {
                RuntimeHoverPart = ERuntimeDragPart::MoveXY;
            }
            else if (HoverGizmoPart == EOntoTwinRuntimeGizmoPart::MoveZ)
            {
                RuntimeHoverPart = ERuntimeDragPart::MoveZ;
            }
            else if (HoverGizmoPart == EOntoTwinRuntimeGizmoPart::RotateYaw)
            {
                RuntimeHoverPart = ERuntimeDragPart::RotateYaw;
            }
        }
    }

    if (!bPointerOverRuntimePanel && PC->WasInputKeyJustPressed(EKeys::LeftMouseButton))
    {
        FHitResult Hit;
        if (TraceRuntimeCursor(Hit))
        {
            UPrimitiveComponent* HitComponent = Hit.GetComponent();
            if (RuntimeGizmo && IsValid(RuntimeGizmo))
            {
                const EOntoTwinRuntimeGizmoPart GizmoPart = RuntimeGizmo->GetPartForComponent(HitComponent);
                if (GizmoPart == EOntoTwinRuntimeGizmoPart::MoveX)
                {
                    BeginRuntimeGizmoDrag(ERuntimeDragPart::MoveX);
                }
                else if (GizmoPart == EOntoTwinRuntimeGizmoPart::MoveY)
                {
                    BeginRuntimeGizmoDrag(ERuntimeDragPart::MoveY);
                }
                else if (GizmoPart == EOntoTwinRuntimeGizmoPart::MoveXY)
                {
                    BeginRuntimeGizmoDrag(ERuntimeDragPart::MoveXY);
                }
                else if (GizmoPart == EOntoTwinRuntimeGizmoPart::MoveZ)
                {
                    BeginRuntimeGizmoDrag(ERuntimeDragPart::MoveZ);
                }
                else if (GizmoPart == EOntoTwinRuntimeGizmoPart::RotateYaw)
                {
                    BeginRuntimeGizmoDrag(ERuntimeDragPart::RotateYaw);
                }
            }

            if (!bRuntimeDragging)
            {
                if (ATwinInstance* HitInstance = ResolveTwinInstanceActor(Hit.GetActor()))
                {
                    SelectRuntimeInstance(HitInstance, bShiftDown);
                }
                else if (!bShiftDown && (!RuntimeGizmo || Hit.GetActor() != RuntimeGizmo))
                {
                    ClearRuntimeSelection(false);
                }
            }
        }
        else if (!bShiftDown)
        {
            ClearRuntimeSelection(false);
        }
    }

    if (bRuntimeDragging)
    {
        if (PC->IsInputKeyDown(EKeys::LeftMouseButton))
        {
            UpdateRuntimeGizmoDrag();
        }
        else
        {
            EndRuntimeGizmoDrag();
        }
    }

    if (RuntimeSelectedInstances.Num() > 0)
    {
        if (RuntimeGizmo && IsValid(RuntimeGizmo))
        {
            UpdateRuntimeSelectionGeometry(bRuntimeDragging);

            const auto ToGizmoPart = [](ERuntimeDragPart Part)
            {
                if (Part == ERuntimeDragPart::MoveX) return EOntoTwinRuntimeGizmoPart::MoveX;
                if (Part == ERuntimeDragPart::MoveY) return EOntoTwinRuntimeGizmoPart::MoveY;
                if (Part == ERuntimeDragPart::MoveXY) return EOntoTwinRuntimeGizmoPart::MoveXY;
                if (Part == ERuntimeDragPart::MoveZ) return EOntoTwinRuntimeGizmoPart::MoveZ;
                if (Part == ERuntimeDragPart::RotateYaw) return EOntoTwinRuntimeGizmoPart::RotateYaw;
                return EOntoTwinRuntimeGizmoPart::None;
            };
            RuntimeGizmo->SetInteractionState(ToGizmoPart(RuntimeHoverPart), ToGizmoPart(RuntimeDragPart));

            EOntoTwinRuntimeSnapState SnapState = EOntoTwinRuntimeSnapState::None;
            if (RuntimeSnapFeedback == ERuntimeSnapFeedback::Grid)
            {
                SnapState = EOntoTwinRuntimeSnapState::Grid;
            }
            else if (RuntimeSnapFeedback == ERuntimeSnapFeedback::Wall)
            {
                SnapState = EOntoTwinRuntimeSnapState::Wall;
            }
            RuntimeGizmo->SetSnapFeedback(SnapState, RuntimeSnapFeedbackPoint);
        }
    }

    UpdateRuntimeEditorPanel();
}

bool ATwinSceneManager::StartRuntimeEditorCamera(APlayerController* PlayerController)
{
    if (!PlayerController || !GetWorld()) return false;
    if (RuntimeEditorCameraPawn && IsValid(RuntimeEditorCameraPawn)) return true;

    RuntimeEditorOriginalPawn = PlayerController->GetPawn();
    const FTransform CameraTransform = BuildRuntimeEditorCameraTransform(PlayerController);

    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = this;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    RuntimeEditorCameraPawn = GetWorld()->SpawnActor<ATwinRuntimeEditorCameraPawn>(
        ATwinRuntimeEditorCameraPawn::StaticClass(), CameraTransform, SpawnParams);
    if (!RuntimeEditorCameraPawn)
    {
        RuntimeEditorOriginalPawn = nullptr;
        UE_LOG(LogTemp, Error, TEXT("[RuntimeEditor] Failed to spawn free camera pawn"));
        return false;
    }

    float LookSensitivity = RuntimeEditorCameraLookSensitivity;
    const bool bUsesGodViewLookSensitivity = InteractionManager
        && InteractionManager->GetGodViewLookSensitivity(LookSensitivity);
    RuntimeEditorCameraPawn->Configure(RuntimeEditorCameraMoveSpeedCmS, LookSensitivity);
    PlayerController->Possess(RuntimeEditorCameraPawn);
    PlayerController->SetControlRotation(CameraTransform.Rotator());

    if (PlayerController->GetPawn() != RuntimeEditorCameraPawn)
    {
        RuntimeEditorCameraPawn->Destroy();
        RuntimeEditorCameraPawn = nullptr;
        RuntimeEditorOriginalPawn = nullptr;
        UE_LOG(LogTemp, Error, TEXT("[RuntimeEditor] Failed to possess free camera pawn"));
        return false;
    }

    const FVector Location = CameraTransform.GetLocation();
    const FRotator Rotation = CameraTransform.Rotator();
    UE_LOG(LogTemp, Log,
        TEXT("[RuntimeEditor] Free camera ready at (%.0f, %.0f, %.0f), pitch=%.1f yaw=%.1f, look=%.2f (%s)"),
        Location.X, Location.Y, Location.Z, Rotation.Pitch, Rotation.Yaw,
        LookSensitivity,
        bUsesGodViewLookSensitivity ? TEXT("F7 god view") : TEXT("local fallback"));
    return true;
}

void ATwinSceneManager::StopRuntimeEditorCamera(APlayerController* PlayerController)
{
    if (!PlayerController) return;
    EndRuntimeEditorCameraLook(PlayerController);

    ATwinRuntimeEditorCameraPawn* CameraPawn = RuntimeEditorCameraPawn;
    if (CameraPawn && IsValid(CameraPawn) && PlayerController->GetPawn() == CameraPawn)
    {
        if (RuntimeEditorOriginalPawn && IsValid(RuntimeEditorOriginalPawn))
        {
            PlayerController->Possess(RuntimeEditorOriginalPawn);
        }
        else
        {
            PlayerController->UnPossess();
        }
    }

    if (CameraPawn && IsValid(CameraPawn))
    {
        CameraPawn->Destroy();
    }
    RuntimeEditorCameraPawn = nullptr;
    RuntimeEditorOriginalPawn = nullptr;
}

void ATwinSceneManager::TickRuntimeEditorCamera(
    APlayerController* PlayerController,
    float DeltaTime,
    bool bPointerOverRuntimePanel)
{
    (void)DeltaTime;
    if (!PlayerController || !RuntimeEditorCameraPawn || !IsValid(RuntimeEditorCameraPawn)
        || PlayerController->GetPawn() != RuntimeEditorCameraPawn)
    {
        return;
    }

    if (bRuntimeDragging && bRuntimeCameraRotating)
    {
        EndRuntimeEditorCameraLook(PlayerController);
    }
    if (!bRuntimeDragging && !bRuntimeCameraRotating && !bPointerOverRuntimePanel
        && PlayerController->WasInputKeyJustPressed(EKeys::RightMouseButton))
    {
        BeginRuntimeEditorCameraLook(PlayerController);
    }
    if (bRuntimeCameraRotating)
    {
        if (PlayerController->IsInputKeyDown(EKeys::RightMouseButton))
        {
            float MouseX = 0.0f;
            float MouseY = 0.0f;
            PlayerController->GetInputMouseDelta(MouseX, MouseY);
            RuntimeEditorCameraPawn->Look(FVector2D(MouseX, MouseY));
        }
        else
        {
            EndRuntimeEditorCameraLook(PlayerController);
        }
    }

    if (bRuntimeDragging) return;

    FVector2D MoveInput(
        (PlayerController->IsInputKeyDown(EKeys::D) ? 1.0f : 0.0f)
            - (PlayerController->IsInputKeyDown(EKeys::A) ? 1.0f : 0.0f),
        (PlayerController->IsInputKeyDown(EKeys::W) ? 1.0f : 0.0f)
            - (PlayerController->IsInputKeyDown(EKeys::S) ? 1.0f : 0.0f));
    MoveInput = MoveInput.GetClampedToMaxSize(1.0f);
    if (!MoveInput.IsNearlyZero())
    {
        RuntimeEditorCameraPawn->MovePlanar(MoveInput);
    }

    const float VerticalInput =
        (PlayerController->IsInputKeyDown(EKeys::E) ? 1.0f : 0.0f)
        - (PlayerController->IsInputKeyDown(EKeys::Q) ? 1.0f : 0.0f);
    if (!FMath::IsNearlyZero(VerticalInput))
    {
        RuntimeEditorCameraPawn->MoveVertical(VerticalInput);
    }

    RuntimeEditorCameraPawn->AdjustSpeed(
        PlayerController->GetInputAnalogKeyState(EKeys::MouseWheelAxis));
}

void ATwinSceneManager::BeginRuntimeEditorCameraLook(APlayerController* PlayerController)
{
    if (!PlayerController || bRuntimeCameraRotating) return;
    float MouseX = 0.0f;
    float MouseY = 0.0f;
    if (PlayerController->GetMousePosition(MouseX, MouseY))
    {
        RuntimeCameraCursorRestorePosition = FVector2D(MouseX, MouseY);
    }
    bRuntimeCameraRotating = true;
    PlayerController->bShowMouseCursor = false;
    PlayerController->SetInputMode(FInputModeGameOnly());
}

void ATwinSceneManager::EndRuntimeEditorCameraLook(APlayerController* PlayerController)
{
    if (!PlayerController || !bRuntimeCameraRotating) return;
    bRuntimeCameraRotating = false;
    PlayerController->bShowMouseCursor = true;

    FInputModeGameAndUI InputMode;
    InputMode.SetHideCursorDuringCapture(false);
    InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    PlayerController->SetInputMode(InputMode);
    PlayerController->SetMouseLocation(
        FMath::RoundToInt(RuntimeCameraCursorRestorePosition.X),
        FMath::RoundToInt(RuntimeCameraCursorRestorePosition.Y));
}

FTransform ATwinSceneManager::BuildRuntimeEditorCameraTransform(
    APlayerController* PlayerController) const
{
    FTransform GodViewTransform;
    if (InteractionManager && InteractionManager->GetGodViewTransform(GodViewTransform))
    {
        return GodViewTransform;
    }

    FVector ViewLocation = FVector::ZeroVector;
    FRotator ViewRotation = FRotator::ZeroRotator;
    if (PlayerController)
    {
        PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
        return FTransform(ViewRotation, ViewLocation);
    }

    return RuntimeEditorOriginalPawn && IsValid(RuntimeEditorOriginalPawn)
        ? RuntimeEditorOriginalPawn->GetActorTransform()
        : GetActorTransform();
}

void ATwinSceneManager::ShowRuntimeEditorPanel()
{
    if (RuntimeEditorPanel && IsValid(RuntimeEditorPanel))
    {
        RuntimeEditorPanel->SetVisibility(ESlateVisibility::Visible);
        return;
    }

    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    if (!PC)
    {
        return;
    }

    UClass* PanelClass = RuntimeEditorPanelClass ? RuntimeEditorPanelClass.Get() : UOntoTwinRuntimeEditorPanel::StaticClass();
    RuntimeEditorPanel = CreateWidget<UOntoTwinRuntimeEditorPanel>(PC, PanelClass);
    if (!RuntimeEditorPanel)
    {
        RuntimeStatusMessage = TEXT("无法创建运行时编辑器面板");
        return;
    }

    RuntimeEditorPanel->SetSceneManager(this);
    RuntimeEditorPanel->AddToViewport(1000);
    UE_LOG(LogTemp, Log, TEXT("[RuntimeEditor] 面板已创建并添加到视口"));
    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 4.0f, FColor::Cyan, TEXT("Runtime Editor panel opened"));
    }
}

void ATwinSceneManager::HideRuntimeEditorPanel()
{
    if (RuntimeEditorPanel && IsValid(RuntimeEditorPanel))
    {
        RuntimeEditorPanel->RemoveFromParent();
    }
    RuntimeEditorPanel = nullptr;
}

void ATwinSceneManager::EnsureRuntimeGizmo()
{
    if (RuntimeGizmo && IsValid(RuntimeGizmo))
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    UClass* GizmoClass = RuntimeGizmoClass ? RuntimeGizmoClass.Get() : AOntoTwinRuntimeGizmo::StaticClass();

    FActorSpawnParameters SpawnParams;
    SpawnParams.Name = TEXT("OntoTwinRuntimeGizmo");
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    RuntimeGizmo = World->SpawnActor<AOntoTwinRuntimeGizmo>(GizmoClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    if (RuntimeGizmo)
    {
        RuntimeGizmo->SetGizmoEnabled(false);
    }
}

void ATwinSceneManager::UpdateRuntimeEditorPanel()
{
    if (RuntimeEditorPanel && IsValid(RuntimeEditorPanel))
    {
        RuntimeEditorPanel->RefreshFromManager();
    }
}

void ATwinSceneManager::CheckRuntimeBindingStatus()
{
    if (bRuntimeBindingRequestInFlight)
    {
        return;
    }

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
    HttpRequest->SetURL(FString::Printf(TEXT("%s/api/v2/ue/binding_status"), *BackendBaseUrl));
    HttpRequest->SetVerb(TEXT("GET"));
    HttpRequest->SetHeader(TEXT("Accept"), TEXT("application/json"));
    AddUEProjectHeaders(HttpRequest);

    bRuntimeBindingRequestInFlight = true;
    RuntimeStatusMessage = TEXT("正在检查 UE 工程绑定...");
    UpdateRuntimeEditorPanel();

    TWeakObjectPtr<ATwinSceneManager> WeakThis(this);
    HttpRequest->OnProcessRequestComplete().BindLambda(
        [WeakThis](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bOk)
        {
            ATwinSceneManager* Self = WeakThis.Get();
            if (!Self) return;

            Self->bRuntimeBindingRequestInFlight = false;
            Self->bRuntimeCanSave = false;
            Self->RuntimeBindingMode = TEXT("unknown");

            const int32 Code = Resp.IsValid() ? Resp->GetResponseCode() : -1;
            TSharedPtr<FJsonObject> Root;
            if (Resp.IsValid())
            {
                TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Resp->GetContentAsString());
                FJsonSerializer::Deserialize(Reader, Root);
            }

            FString Mode;
            FString Error;
            if (Root.IsValid())
            {
                Root->TryGetStringField(TEXT("mode"), Mode);
                Root->TryGetStringField(TEXT("error"), Error);
            }

            if (bOk && Resp.IsValid() && Code == 200)
            {
                if (Mode == TEXT("matched"))
                {
                    Self->RuntimeBindingMode = TEXT("matched");
                    Self->bRuntimeCanSave = true;
                    Self->RuntimeStatusMessage = TEXT("UE 工程绑定匹配");
                }
                else if (Mode == TEXT("unbound"))
                {
                    Self->RuntimeBindingMode = TEXT("unbound");
                    Self->RuntimeStatusMessage = TEXT("当前数据集尚未绑定，绑定后才能保存");
                }
                else
                {
                    Self->RuntimeBindingMode = Mode.IsEmpty() ? TEXT("unknown") : Mode;
                    Self->RuntimeStatusMessage = TEXT("当前绑定状态不允许保存");
                }
            }
            else
            {
                Self->RuntimeBindingMode = Error.IsEmpty() ? TEXT("error") : Error;
                if (Error == TEXT("ue_project_mismatch"))
                {
                    Self->RuntimeStatusMessage = TEXT("UE 工程不匹配，保存已禁用");
                }
                else
                {
                    Self->RuntimeStatusMessage = FString::Printf(TEXT("Binding check failed (code=%d)"), Code);
                    if (Self->RuntimeEditorPanel && IsValid(Self->RuntimeEditorPanel))
                    {
                        Self->RuntimeEditorPanel->ShowToast(
                            TEXT("Access check failed"),
                            EOntoTwinRuntimeToastType::Error);
                    }
                }
            }

            Self->UpdateRuntimeEditorPanel();
        });

    HttpRequest->ProcessRequest();
}

void ATwinSceneManager::RetryRuntimeBindingStatus()
{
    if (!bRuntimeEditMode || bRuntimeBindingRequestInFlight)
    {
        return;
    }

    CheckRuntimeBindingStatus();
}

void ATwinSceneManager::BindCurrentRuntimeProject()
{
    if (!bRuntimeEditMode || bRuntimeBindingRequestInFlight)
    {
        return;
    }

    TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
    Body->SetStringField(TEXT("ue_project_id"), UEProjectId);
    Body->SetStringField(TEXT("ue_project_name"), UEProjectName);

    FString BodyStr;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyStr);
    FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
    HttpRequest->SetURL(FString::Printf(TEXT("%s/api/v2/ue/bind_active_project"), *BackendBaseUrl));
    HttpRequest->SetVerb(TEXT("POST"));
    HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    AddUEProjectHeaders(HttpRequest);
    HttpRequest->SetContentAsString(BodyStr);

    bRuntimeBindingRequestInFlight = true;
    RuntimeStatusMessage = TEXT("正在将当前数据集绑定到此 UE 工程...");
    UpdateRuntimeEditorPanel();

    TWeakObjectPtr<ATwinSceneManager> WeakThis(this);
    HttpRequest->OnProcessRequestComplete().BindLambda(
        [WeakThis](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bOk)
        {
            ATwinSceneManager* Self = WeakThis.Get();
            if (!Self) return;

            Self->bRuntimeBindingRequestInFlight = false;
            const int32 Code = Resp.IsValid() ? Resp->GetResponseCode() : -1;
            if (bOk && Resp.IsValid() && Code == 200)
            {
                Self->RuntimeBindingMode = TEXT("matched");
                Self->bRuntimeCanSave = true;
                Self->RuntimeStatusMessage = TEXT("当前数据集已绑定到此 UE 工程");
                if (Self->RuntimeEditorPanel && IsValid(Self->RuntimeEditorPanel))
                {
                    Self->RuntimeEditorPanel->ShowToast(
                        TEXT("Active dataset bound"),
                        EOntoTwinRuntimeToastType::Success);
                }
            }
            else
            {
                Self->bRuntimeCanSave = false;
                Self->RuntimeBindingMode = TEXT("error");
                Self->RuntimeStatusMessage = FString::Printf(TEXT("Bind failed (code=%d)"), Code);
                if (Self->RuntimeEditorPanel && IsValid(Self->RuntimeEditorPanel))
                {
                    Self->RuntimeEditorPanel->ShowToast(
                        TEXT("Dataset binding failed"),
                        EOntoTwinRuntimeToastType::Error);
                }
            }
            Self->UpdateRuntimeEditorPanel();
        });

    HttpRequest->ProcessRequest();
}

bool ATwinSceneManager::CalculateRuntimeEditLocalBounds(AActor* Actor, FBox& OutLocalBounds) const
{
    OutLocalBounds = FBox(ForceInit);
    if (!Actor || !IsValid(Actor))
    {
        return false;
    }

    if (const ATwinInstance* TwinInstance = Cast<ATwinInstance>(Actor))
    {
        return TwinInstance->GetRepresentationLocalBounds(OutLocalBounds);
    }

    const FTransform WorldToActor = Actor->GetActorTransform().Inverse();
    TInlineComponentArray<UMeshComponent*> MeshComponents(Actor);
    for (UMeshComponent* MeshComponent : MeshComponents)
    {
        if (!MeshComponent ||
            !MeshComponent->IsRegistered() ||
            !MeshComponent->IsVisible())
        {
            continue;
        }

        const FTransform ComponentToActor = MeshComponent->GetComponentTransform() * WorldToActor;
        OutLocalBounds += MeshComponent->CalcBounds(ComponentToActor).GetBox();
    }
    if (OutLocalBounds.IsValid)
    {
        return true;
    }

    const FBox WorldBounds = Actor->GetComponentsBoundingBox(false, true);
    if (!WorldBounds.IsValid)
    {
        return false;
    }

    const FTransform ActorTransform = Actor->GetActorTransform();
    const FVector Min = WorldBounds.Min;
    const FVector Max = WorldBounds.Max;
    for (int32 XIndex = 0; XIndex < 2; ++XIndex)
    {
        for (int32 YIndex = 0; YIndex < 2; ++YIndex)
        {
            for (int32 ZIndex = 0; ZIndex < 2; ++ZIndex)
            {
                const FVector WorldCorner(
                    XIndex == 0 ? Min.X : Max.X,
                    YIndex == 0 ? Min.Y : Max.Y,
                    ZIndex == 0 ? Min.Z : Max.Z);
                OutLocalBounds += ActorTransform.InverseTransformPosition(WorldCorner);
            }
        }
    }
    return OutLocalBounds.IsValid != 0;
}

FVector ATwinSceneManager::CalculateRuntimeActorLocationForPivot(
    const FVector& PivotWorld,
    const FRotator& Rotation,
    const FVector& Scale) const
{
    const FTransform PivotOffsetTransform(Rotation, FVector::ZeroVector, Scale);
    return PivotWorld - PivotOffsetTransform.TransformVector(RuntimeEditPivotLocal);
}

float ATwinSceneManager::CalculateRuntimeBoundsRadiusAlongNormal(
    const FVector& WorldNormal,
    const FRotator& Rotation,
    const FVector& Scale) const
{
    if (!RuntimeEditLocalBounds.IsValid) return 2.0f;

    const FVector Normal = WorldNormal.GetSafeNormal();
    const FVector ScaledExtent = RuntimeEditLocalBounds.GetExtent() * Scale.GetAbs();
    const FQuat RotationQuat = Rotation.Quaternion();
    return
        FMath::Abs(FVector::DotProduct(Normal, RotationQuat.GetAxisX())) * ScaledExtent.X +
        FMath::Abs(FVector::DotProduct(Normal, RotationQuat.GetAxisY())) * ScaledExtent.Y +
        FMath::Abs(FVector::DotProduct(Normal, RotationQuat.GetAxisZ())) * ScaledExtent.Z +
        2.0f;
}

ATwinSceneManager::FRuntimeEditInstanceState& ATwinSceneManager::EnsureRuntimeEditState(
    ATwinInstance* Instance)
{
    const FString InstanceId = Instance->GetInstanceId();
    if (FRuntimeEditInstanceState* Existing = RuntimeEditStates.Find(InstanceId))
    {
        return *Existing;
    }

    FRuntimeEditInstanceState State;
    State.Instance = Instance;
    State.BaselineTransform = Instance->GetActorTransform();
    State.bBaselineLoaded = Instance->IsRuntimeLoaded();
    State.BaselineStateHash = Instance->GetRuntimeEditStateHash();
    State.PreviousAnimState = Instance->PauseRuntimeEditorAnimation(State.bPreviousAnimRunning);
    Instance->bLocalOverrideLock = true;
    return RuntimeEditStates.Add(InstanceId, MoveTemp(State));
}

void ATwinSceneManager::SelectRuntimeInstance(ATwinInstance* Instance, bool bToggleSelection)
{
    if (!Instance || !IsValid(Instance))
    {
        return;
    }

    if (!Instance->IsRuntimeSpatialEditable())
    {
        RuntimeStatusMessage = TEXT("该实例由实时位置源驱动");
        if (RuntimeEditorPanel && IsValid(RuntimeEditorPanel))
        {
            RuntimeEditorPanel->ShowToast(
                TEXT("该实例由实时位置源驱动，不能在 F10 中修改"),
                EOntoTwinRuntimeToastType::Warning);
        }
        UpdateRuntimeEditorPanel();
        return;
    }

    const int32 ExistingIndex = RuntimeSelectedInstances.IndexOfByKey(Instance);
    if (bToggleSelection && ExistingIndex != INDEX_NONE)
    {
        RuntimeSelectedInstances.RemoveAt(ExistingIndex);
        RuntimeSelectedInstance = RuntimeSelectedInstances.Num() > 0 ? RuntimeSelectedInstances[0] : nullptr;
        RuntimeSelectionYawDelta = 0.0f;
        ReleaseCleanUnselectedRuntimeStates();
        UpdateRuntimeSelectionGeometry();
        RuntimeStatusMessage = RuntimeSelectedInstances.Num() > 0
            ? FString::Printf(TEXT("已选择 %d 个实例"), RuntimeSelectedInstances.Num())
            : TEXT("未选择实例");
        UpdateRuntimeEditorPanel();
        return;
    }

    if (!bToggleSelection)
    {
        ClearRuntimeSelection(false);
    }
    else if (RuntimeSelectedInstances.Num() >= RuntimeEditorMaxSelection)
    {
        RuntimeStatusMessage = FString::Printf(TEXT("单次最多选择 %d 个实例"), RuntimeEditorMaxSelection);
        if (RuntimeEditorPanel && IsValid(RuntimeEditorPanel))
        {
            RuntimeEditorPanel->ShowToast(RuntimeStatusMessage, EOntoTwinRuntimeToastType::Warning);
        }
        UpdateRuntimeEditorPanel();
        return;
    }

    if (!RuntimeSelectedInstances.Contains(Instance))
    {
        RuntimeSelectedInstances.Add(Instance);
    }
    RuntimeSelectedInstance = RuntimeSelectedInstances.Num() > 0 ? RuntimeSelectedInstances[0] : nullptr;
    EnsureRuntimeEditState(Instance);
    RuntimeSelectionYawDelta = 0.0f;
    UpdateRuntimeSelectionGeometry();

    RuntimeStatusMessage = RuntimeSelectedInstances.Num() > 1
        ? FString::Printf(TEXT("已选择 %d 个实例"), RuntimeSelectedInstances.Num())
        : FString::Printf(TEXT("已选择 %s"), *Instance->GetTwinDisplayName());
    UpdateRuntimeEditorPanel();
}

void ATwinSceneManager::ReleaseRuntimeEditState(
    const FString& InstanceId,
    bool bRestoreBaseline)
{
    FRuntimeEditInstanceState* State = RuntimeEditStates.Find(InstanceId);
    if (!State) return;

    if (ATwinInstance* Instance = State->Instance.Get())
    {
        if (bRestoreBaseline)
        {
            Instance->SetActorTransform(State->BaselineTransform);
            Instance->SetRuntimeEditorLoadedOverride(State->bBaselineLoaded);
        }
        Instance->ClearRuntimeEditorLoadedOverride();
        Instance->bLocalOverrideLock = false;
        Instance->ResumeRuntimeEditorAnimation(State->PreviousAnimState, State->bPreviousAnimRunning);
    }
    RuntimeEditStates.Remove(InstanceId);
}

void ATwinSceneManager::ReleaseCleanUnselectedRuntimeStates()
{
    TArray<FString> ReleaseIds;
    for (const TPair<FString, FRuntimeEditInstanceState>& Pair : RuntimeEditStates)
    {
        ATwinInstance* Instance = Pair.Value.Instance.Get();
        if (!Instance || !IsValid(Instance))
        {
            ReleaseIds.Add(Pair.Key);
            continue;
        }
        const bool bSelected = RuntimeSelectedInstances.Contains(Instance);
        if (!bSelected && !Pair.Value.bTransformDirty && !Pair.Value.bLoadedDirty)
        {
            ReleaseIds.Add(Pair.Key);
        }
    }
    for (const FString& InstanceId : ReleaseIds)
    {
        ReleaseRuntimeEditState(InstanceId, false);
    }
}

void ATwinSceneManager::RefreshRuntimeEditDirtyState()
{
    int32 DirtyCount = 0;
    for (TPair<FString, FRuntimeEditInstanceState>& Pair : RuntimeEditStates)
    {
        FRuntimeEditInstanceState& State = Pair.Value;
        ATwinInstance* Instance = State.Instance.Get();
        if (!Instance || !IsValid(Instance)) continue;
        State.bTransformDirty = !Instance->GetActorTransform().Equals(State.BaselineTransform, 0.01f);
        State.bLoadedDirty = Instance->IsRuntimeLoaded() != State.bBaselineLoaded;
        if (State.bTransformDirty || State.bLoadedDirty) ++DirtyCount;
    }
    bRuntimeEditDirty = DirtyCount > 0 || bRuntimeBusinessDirty;
    RuntimeStatusMessage = bRuntimeEditDirty
        ? FString::Printf(TEXT("待保存 %d 项%s"), DirtyCount,
            bRuntimeBusinessDirty ? TEXT(" + 业务修改") : TEXT(""))
        : TEXT("没有待保存修改");
}

void ATwinSceneManager::RefreshRuntimeBusinessDirtyState()
{
    FString BaselineJson;
    FString CurrentJson;
    if (RuntimeBusinessBaseline.IsValid())
    {
        const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BaselineJson);
        FJsonSerializer::Serialize(RuntimeBusinessBaseline.ToSharedRef(), Writer);
    }
    if (RuntimeBusinessConfig.IsValid())
    {
        const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&CurrentJson);
        FJsonSerializer::Serialize(RuntimeBusinessConfig.ToSharedRef(), Writer);
    }
    bRuntimeBusinessDirty = BaselineJson != CurrentJson;
}

void ATwinSceneManager::UpdateRuntimeSelectionGeometry(bool bKeepDragPivot)
{
    RuntimeSelectedInstances.RemoveAll([](ATwinInstance* Instance)
    {
        return !Instance || !IsValid(Instance) || !Instance->IsRuntimeLoaded();
    });
    RuntimeSelectedInstance = RuntimeSelectedInstances.Num() > 0 ? RuntimeSelectedInstances[0] : nullptr;

    RuntimeSelectionLocalBounds.Reset();
    RuntimeSelectionWorldBounds = FBox(ForceInit);
    TArray<AActor*> Targets;
    for (ATwinInstance* Instance : RuntimeSelectedInstances)
    {
        FBox LocalBounds(ForceInit);
        if (!CalculateRuntimeEditLocalBounds(Instance, LocalBounds))
        {
            LocalBounds = FBox(FVector(-25.0f), FVector(25.0f));
        }
        RuntimeSelectionLocalBounds.Add(LocalBounds);
        RuntimeSelectionWorldBounds += LocalBounds.TransformBy(Instance->GetActorTransform());
        Targets.Add(Instance);
    }

    if (RuntimeSelectedInstances.Num() == 0 || !RuntimeSelectionWorldBounds.IsValid)
    {
        RuntimeSelectionPivotWorld = FVector::ZeroVector;
        if (RuntimeGizmo && IsValid(RuntimeGizmo)) RuntimeGizmo->SetGizmoEnabled(false);
        return;
    }

    if (!bKeepDragPivot)
    {
        RuntimeSelectionPivotWorld = RuntimeSelectionWorldBounds.GetCenter();
        RuntimeDragPivotWorld = RuntimeSelectionPivotWorld;
    }
    RuntimeEditPlaneZ = RuntimeSelectionPivotWorld.Z;

    if (RuntimeSelectedInstances.Num() == 1)
    {
        RuntimeEditLocalBounds = RuntimeSelectionLocalBounds[0];
        RuntimeEditPivotLocal = RuntimeEditLocalBounds.GetCenter();
    }

    EnsureRuntimeGizmo();
    if (RuntimeGizmo && IsValid(RuntimeGizmo))
    {
        RuntimeGizmo->UpdateForSelection(
            Targets,
            RuntimeSelectionLocalBounds,
            RuntimeSelectionWorldBounds,
            bKeepDragPivot ? RuntimeDragPivotWorld : RuntimeSelectionPivotWorld);
    }
}

bool ATwinSceneManager::CanStageRuntimeSelection() const
{
    TSet<FString> PendingIds;
    for (const TPair<FString, FRuntimeEditInstanceState>& Pair : RuntimeEditStates)
    {
        if (Pair.Value.bTransformDirty || Pair.Value.bLoadedDirty) PendingIds.Add(Pair.Key);
    }
    for (ATwinInstance* Instance : RuntimeSelectedInstances)
    {
        if (Instance && IsValid(Instance)) PendingIds.Add(Instance->GetInstanceId());
    }
    return PendingIds.Num() <= RuntimeEditorMaxPending;
}

void ATwinSceneManager::ClearRuntimeSelection(bool bRestoreBaseline)
{
    const TArray<ATwinInstance*> PreviousSelection = RuntimeSelectedInstances;
    RuntimeSelectedInstances.Reset();
    RuntimeSelectedInstance = nullptr;
    RuntimeSelectionLocalBounds.Reset();
    RuntimeSelectionWorldBounds = FBox(ForceInit);
    RuntimeSelectionPivotWorld = FVector::ZeroVector;
    RuntimeSelectionYawDelta = 0.0f;

    if (bRestoreBaseline)
    {
        for (ATwinInstance* Instance : PreviousSelection)
        {
            if (Instance && IsValid(Instance))
            {
                ReleaseRuntimeEditState(Instance->GetInstanceId(), true);
            }
        }
    }
    else
    {
        ReleaseCleanUnselectedRuntimeStates();
    }

    RuntimeEditLocalBounds = FBox(ForceInit);
    RuntimeEditPivotLocal = FVector::ZeroVector;
    RuntimeDragPivotWorld = FVector::ZeroVector;
    SetRuntimeCameraLookSuppressed(false);
    bRuntimeDragging = false;
    RuntimeHoverPart = ERuntimeDragPart::None;
    RuntimeDragPart = ERuntimeDragPart::None;
    RuntimeSnapFeedback = ERuntimeSnapFeedback::None;
    RuntimeSnapFeedbackPoint = FVector::ZeroVector;

    if (RuntimeGizmo && IsValid(RuntimeGizmo))
    {
        RuntimeGizmo->SetGizmoEnabled(false);
    }

    RefreshRuntimeEditDirtyState();
    UpdateRuntimeEditorPanel();
}

bool ATwinSceneManager::TraceRuntimeCursor(FHitResult& OutHit) const
{
    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    UWorld* World = GetWorld();
    if (!PC || !World)
    {
        return false;
    }

    FVector RayOrigin = FVector::ZeroVector;
    FVector RayDirection = FVector::ForwardVector;
    const bool bHasCursorRay = PC->DeprojectMousePositionToWorld(RayOrigin, RayDirection);
    if (bHasCursorRay &&
        RuntimeSelectedInstance && IsValid(RuntimeSelectedInstance) &&
        RuntimeGizmo && IsValid(RuntimeGizmo))
    {
        FCollisionQueryParams GizmoQueryParams(SCENE_QUERY_STAT(OntoTwinRuntimeGizmoTrace), true);
        for (ATwinInstance* Selected : RuntimeSelectedInstances)
        {
            if (Selected && IsValid(Selected)) GizmoQueryParams.AddIgnoredActor(Selected);
        }
        GizmoQueryParams.AddIgnoredActor(this);

        FHitResult GizmoHit;
        if (World->LineTraceSingleByChannel(
            GizmoHit,
            RayOrigin,
            RayOrigin + RayDirection * 1000000.0f,
            ECC_Visibility,
            GizmoQueryParams) &&
            GizmoHit.GetActor() == RuntimeGizmo &&
            RuntimeGizmo->GetPartForComponent(GizmoHit.GetComponent()) != EOntoTwinRuntimeGizmoPart::None)
        {
            OutHit = GizmoHit;
            return true;
        }
    }

    if (!PC->GetHitResultUnderCursor(ECC_Visibility, false, OutHit))
    {
        return false;
    }

    // A migration preview can temporarily contain a legacy source actor at
    // exactly the same transform as its database-driven TwinInstance. The
    // legacy component may win the single Visibility trace, which used to
    // clear the F10 selection even though the Twin was directly underneath it.
    // Look through only a very shallow stack of identical meshes. The strict
    // distance and mesh checks prevent selecting an unrelated Twin behind a
    // normal scene occluder.
    if (!bHasCursorRay
        || ResolveTwinInstanceActor(OutHit.GetActor())
        || (RuntimeGizmo && OutHit.GetActor() == RuntimeGizmo))
    {
        return true;
    }

    const UStaticMeshComponent* PrimaryMeshComponent =
        Cast<UStaticMeshComponent>(OutHit.GetComponent());
    UStaticMesh* PrimaryMesh = PrimaryMeshComponent
        ? PrimaryMeshComponent->GetStaticMesh()
        : nullptr;
    AActor* PrimaryActor = OutHit.GetActor();
    if (!PrimaryActor || !PrimaryMesh)
    {
        return true;
    }

    constexpr float CoincidentTwinToleranceCm = 5.0f;
    FCollisionQueryParams FallbackParams(
        SCENE_QUERY_STAT(OntoTwinRuntimeCoincidentSourceFallback), true);
    FallbackParams.AddIgnoredActor(this);
    FallbackParams.AddIgnoredActor(PrimaryActor);
    if (RuntimeGizmo && IsValid(RuntimeGizmo))
    {
        FallbackParams.AddIgnoredActor(RuntimeGizmo);
    }

    const float PrimaryDistance = OutHit.Distance;
    for (int32 Depth = 0; Depth < 8; ++Depth)
    {
        FHitResult FallbackHit;
        if (!World->LineTraceSingleByChannel(
            FallbackHit,
            RayOrigin,
            RayOrigin + RayDirection * 1000000.0f,
            ECC_Visibility,
            FallbackParams))
        {
            break;
        }

        if (FallbackHit.Distance - PrimaryDistance > CoincidentTwinToleranceCm)
        {
            break;
        }

        if (ATwinInstance* FallbackInstance =
            ResolveTwinInstanceActor(FallbackHit.GetActor()))
        {
            const UStaticMeshComponent* FallbackMeshComponent =
                Cast<UStaticMeshComponent>(FallbackHit.GetComponent());
            if (FallbackMeshComponent
                && FallbackMeshComponent->GetStaticMesh() == PrimaryMesh)
            {
                OutHit = FallbackHit;
                return true;
            }
        }

        AActor* FallbackActor = FallbackHit.GetActor();
        if (!FallbackActor)
        {
            break;
        }
        FallbackParams.AddIgnoredActor(FallbackActor);
    }
    return true;
}

void ATwinSceneManager::RunRuntimeEditSelfTest()
{
    if (!bInitialModelBaselineReady)
    {
        return;
    }

    const double ElapsedSeconds = FPlatformTime::Seconds() - RuntimeEditSelfTestStartSeconds;
    const int32 RegistryCount = InstanceRegistry.Num();
    if (RuntimeEditSelfTestExpectedInstances >= 0
        && RegistryCount != RuntimeEditSelfTestExpectedInstances)
    {
        if (ElapsedSeconds < 12.0)
        {
            return;
        }
        bRuntimeEditSelfTestComplete = true;
        UE_LOG(LogTemp, Error,
            TEXT("ONTOTWIN_RUNTIME_EDIT_SELFTEST success=false reason=instance_count expected=%d actual=%d"),
            RuntimeEditSelfTestExpectedInstances,
            RegistryCount);
        if (bRuntimeEditSelfTestQuit) FGenericPlatformMisc::RequestExit(false);
        return;
    }

    TMap<FString, ATwinInstance*> SampleByType;
    int32 InvalidInstanceCount = 0;
    for (const TPair<FString, ATwinInstance*>& Pair : InstanceRegistry)
    {
        ATwinInstance* Instance = Pair.Value;
        if (!Instance || !IsValid(Instance) || !Instance->IsRuntimeLoaded()
            || Instance->GetRenderPartComponentCount() != 1
            || !Instance->IsAssemblyRenderActive())
        {
            ++InvalidInstanceCount;
            continue;
        }
        const FString TypeName = Instance->GetTwinObjectTypeName();
        if (!TypeName.IsEmpty() && !SampleByType.Contains(TypeName))
        {
            SampleByType.Add(TypeName, Instance);
        }
    }

    bool bSuccess = InvalidInstanceCount == 0;
    if (RuntimeEditSelfTestExpectedTypes >= 0)
    {
        bSuccess = bSuccess && SampleByType.Num() == RuntimeEditSelfTestExpectedTypes;
    }
    if (SampleByType.Num() == 0)
    {
        bSuccess = false;
    }

    EnterRuntimeEditMode();
    bSuccess = bSuccess && bRuntimeEditMode;
    int32 TestedTypes = 0;
    float MaxPivotErrorCm = 0.0f;
    for (const TPair<FString, ATwinInstance*>& Sample : SampleByType)
    {
        ATwinInstance* Instance = Sample.Value;
        if (!Instance || !IsValid(Instance))
        {
            bSuccess = false;
            continue;
        }

        ATwinInstance* Neighbor = nullptr;
        for (const TPair<FString, ATwinInstance*>& Candidate : InstanceRegistry)
        {
            if (Candidate.Value && Candidate.Value != Instance && IsValid(Candidate.Value))
            {
                Neighbor = Candidate.Value;
                break;
            }
        }
        if (!Neighbor)
        {
            bSuccess = false;
            continue;
        }

        const FTransform Baseline = Instance->GetActorTransform();
        const FTransform NeighborBaseline = Neighbor->GetActorTransform();
        const FBox RepresentationBounds = Instance->GetRepresentationWorldBounds(true);
        SelectRuntimeInstance(Instance, false);
        const bool bSelected = RuntimeSelectedInstances.Num() == 1
            && RuntimeSelectedInstance == Instance;
        const bool bGizmoReady = RuntimeGizmo && IsValid(RuntimeGizmo)
            && RuntimeGizmo->IsGizmoEnabled();
        const float PivotErrorCm = RepresentationBounds.IsValid && bGizmoReady
            ? FVector::Distance(RuntimeGizmo->GetActorLocation(), RepresentationBounds.GetCenter())
            : TNumericLimits<float>::Max();
        MaxPivotErrorCm = FMath::Max(MaxPivotErrorCm, PivotErrorCm);

        const FVector TestDelta(10.0f, 0.0f, 0.0f);
        Instance->SetActorLocation(Baseline.GetLocation() + TestDelta);
        UpdateRuntimeSelectionGeometry();
        const bool bSelectedMoved = Instance->GetActorLocation().Equals(
            Baseline.GetLocation() + TestDelta, 0.01f);
        const bool bNeighborStayed = Neighbor->GetActorTransform().Equals(
            NeighborBaseline, 0.01f);
        ClearRuntimeSelection(true);
        const bool bRestored = Instance->GetActorTransform().Equals(Baseline, 0.01f);

        bSuccess = bSuccess && bSelected && bGizmoReady
            && PivotErrorCm <= 1.0f && bSelectedMoved && bNeighborStayed && bRestored;
        ++TestedTypes;
    }

    if (bRuntimeEditMode)
    {
        FinishExitRuntimeEditMode();
    }
    bRuntimeEditSelfTestComplete = true;
    UE_LOG(LogTemp, Display,
        TEXT("ONTOTWIN_RUNTIME_EDIT_SELFTEST success=%s instances=%d types=%d tested_types=%d invalid_instances=%d max_pivot_error_cm=%.3f"),
        bSuccess ? TEXT("true") : TEXT("false"),
        RegistryCount,
        SampleByType.Num(),
        TestedTypes,
        InvalidInstanceCount,
        MaxPivotErrorCm);
    if (bRuntimeEditSelfTestQuit)
    {
        FGenericPlatformMisc::RequestExit(false);
    }
}

bool ATwinSceneManager::GetRuntimeCursorPointOnPlane(const FPlane& Plane, FVector& OutPoint) const
{
    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    if (!PC)
    {
        return false;
    }

    float MouseX = 0.0f;
    float MouseY = 0.0f;
    if (!PC->GetMousePosition(MouseX, MouseY))
    {
        return false;
    }

    FVector RayOrigin = FVector::ZeroVector;
    FVector RayDirection = FVector::ForwardVector;
    if (!PC->DeprojectScreenPositionToWorld(MouseX, MouseY, RayOrigin, RayDirection))
    {
        return false;
    }

    const FVector PlaneNormal(Plane.X, Plane.Y, Plane.Z);
    if (FMath::IsNearlyZero(FVector::DotProduct(RayDirection, PlaneNormal)))
    {
        return false;
    }

    OutPoint = FMath::LinePlaneIntersection(
        RayOrigin,
        RayOrigin + RayDirection * 1000000.0f,
        Plane);
    return true;
}

bool ATwinSceneManager::GetRuntimeCursorPlanePoint(FVector& OutPoint) const
{
    return GetRuntimeCursorPointOnPlane(
        FPlane(FVector(0.0f, 0.0f, RuntimeDragPlaneZ), FVector::UpVector),
        OutPoint);
}

void ATwinSceneManager::BeginRuntimeGizmoDrag(ERuntimeDragPart Part)
{
    if (RuntimeSelectedInstances.Num() == 0 || Part == ERuntimeDragPart::None)
    {
        return;
    }

    if (!CanStageRuntimeSelection())
    {
        RuntimeStatusMessage = FString::Printf(TEXT("会话最多保存 %d 个实例"), RuntimeEditorMaxPending);
        if (RuntimeEditorPanel && IsValid(RuntimeEditorPanel))
        {
            RuntimeEditorPanel->ShowToast(RuntimeStatusMessage, EOntoTwinRuntimeToastType::Warning);
        }
        return;
    }

    UpdateRuntimeSelectionGeometry();
    RuntimeDragStartTransforms.Reset();
    RuntimeDragBeforeValues.Reset();
    for (ATwinInstance* Instance : RuntimeSelectedInstances)
    {
        if (!Instance || !IsValid(Instance)) continue;
        EnsureRuntimeEditState(Instance);
        RuntimeDragStartTransforms.Add(Instance->GetInstanceId(), Instance->GetActorTransform());
        RuntimeDragBeforeValues.Add(CaptureRuntimeValue(Instance));
    }

    RuntimeDragPart = Part;
    RuntimeHoverPart = Part;
    RuntimeSnapFeedback = ERuntimeSnapFeedback::None;
    RuntimeSnapFeedbackPoint = FVector::ZeroVector;
    RuntimeDragStartPivotWorld = RuntimeSelectionPivotWorld;
    RuntimeDragPivotWorld = RuntimeDragStartPivotWorld;
    RuntimeDragStartGroupYawDelta = RuntimeSelectionYawDelta;
    RuntimeDragPlaneZ = RuntimeDragStartPivotWorld.Z;
    if (RuntimeGizmo && IsValid(RuntimeGizmo))
    {
        if (Part == ERuntimeDragPart::MoveX ||
            Part == ERuntimeDragPart::MoveY ||
            Part == ERuntimeDragPart::MoveXY)
        {
            RuntimeDragPlaneZ = RuntimeGizmo->GetMoveInteractionPlaneZ();
        }
        else if (Part == ERuntimeDragPart::RotateYaw)
        {
            RuntimeDragPlaneZ = RuntimeGizmo->GetRotateInteractionPlaneZ();
        }
    }
    bRuntimeDragging = true;
    SetRuntimeCameraLookSuppressed(true);

    if (Part == ERuntimeDragPart::MoveZ)
    {
        APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
        FVector CameraLocation = FVector::ZeroVector;
        FRotator CameraRotation = FRotator::ZeroRotator;
        if (PC)
        {
            PC->GetPlayerViewPoint(CameraLocation, CameraRotation);
        }

        FVector PlaneNormal = CameraLocation - RuntimeDragStartPivotWorld;
        PlaneNormal.Z = 0.0f;
        if (!PlaneNormal.Normalize())
        {
            PlaneNormal = -CameraRotation.Vector();
            PlaneNormal.Z = 0.0f;
            if (!PlaneNormal.Normalize())
            {
                PlaneNormal = FVector::ForwardVector;
            }
        }

        RuntimeZDragPlane = FPlane(RuntimeDragStartPivotWorld, PlaneNormal);
        if (!GetRuntimeCursorPointOnPlane(RuntimeZDragPlane, RuntimeZDragStartPoint))
        {
            RuntimeZDragStartPoint = RuntimeDragStartPivotWorld;
        }
        return;
    }

    FVector PlanePoint = FVector::ZeroVector;
    if (GetRuntimeCursorPlanePoint(PlanePoint))
    {
        RuntimeDragStartPoint = PlanePoint;
        const FVector ToCursor = PlanePoint - RuntimeDragStartPivotWorld;
        RuntimeDragStartAngleDeg = FMath::RadiansToDegrees(FMath::Atan2(ToCursor.Y, ToCursor.X));
    }
    else
    {
        RuntimeDragStartPoint = RuntimeDragStartPivotWorld;
        RuntimeDragStartAngleDeg = 0.0f;
    }
}

void ATwinSceneManager::UpdateRuntimeGizmoDrag()
{
    if (!bRuntimeDragging || RuntimeSelectedInstances.Num() == 0)
    {
        return;
    }

    FVector NewPivot = RuntimeDragStartPivotWorld;
    float OperationYawDelta = 0.0f;

    if (RuntimeDragPart == ERuntimeDragPart::MoveZ)
    {
        FVector ZPlanePoint = FVector::ZeroVector;
        if (!GetRuntimeCursorPointOnPlane(RuntimeZDragPlane, ZPlanePoint))
        {
            return;
        }

        NewPivot.Z += ZPlanePoint.Z - RuntimeZDragStartPoint.Z;
        RuntimeSnapFeedback = ERuntimeSnapFeedback::None;
        RuntimeSnapFeedbackPoint = FVector::ZeroVector;
    }
    else
    {
        FVector PlanePoint = FVector::ZeroVector;
        if (!GetRuntimeCursorPlanePoint(PlanePoint))
        {
            return;
        }

        if (RuntimeDragPart == ERuntimeDragPart::MoveX ||
            RuntimeDragPart == ERuntimeDragPart::MoveY ||
            RuntimeDragPart == ERuntimeDragPart::MoveXY)
        {
            const FVector Delta = PlanePoint - RuntimeDragStartPoint;
            if (RuntimeDragPart == ERuntimeDragPart::MoveX || RuntimeDragPart == ERuntimeDragPart::MoveXY)
            {
                NewPivot.X += Delta.X;
            }
            if (RuntimeDragPart == ERuntimeDragPart::MoveY || RuntimeDragPart == ERuntimeDragPart::MoveXY)
            {
                NewPivot.Y += Delta.Y;
            }
        }
        else if (RuntimeDragPart == ERuntimeDragPart::RotateYaw)
        {
            const FVector ToCursor = PlanePoint - RuntimeDragStartPivotWorld;
            if (!ToCursor.IsNearlyZero())
            {
                const float CurrentAngleDeg = FMath::RadiansToDegrees(FMath::Atan2(ToCursor.Y, ToCursor.X));
                OperationYawDelta = FMath::FindDeltaAngleDegrees(RuntimeDragStartAngleDeg, CurrentAngleDeg);
            }
        }

        ApplyRuntimeGroupSnaps(NewPivot, OperationYawDelta);
    }

    ApplyRuntimeGroupTransform(NewPivot, OperationYawDelta);
    RuntimeSelectionYawDelta = RuntimeDragStartGroupYawDelta + OperationYawDelta;
    RuntimeSelectionPivotWorld = NewPivot;
    RuntimeDragPivotWorld = NewPivot;
    RefreshRuntimeEditDirtyState();
    UpdateRuntimeSelectionGeometry(true);

    UpdateRuntimeEditorPanel();
}

void ATwinSceneManager::EndRuntimeGizmoDrag(bool bCommit)
{
    if (!bCommit)
    {
        for (const FRuntimeEditValue& Value : RuntimeDragBeforeValues)
        {
            ApplyRuntimeValue(Value);
        }
        RuntimeSelectionYawDelta = RuntimeDragStartGroupYawDelta;
        RefreshRuntimeEditDirtyState();
    }
    else
    {
        FRuntimeEditCommand Command;
        Command.Label = RuntimeDragPart == ERuntimeDragPart::RotateYaw
            ? TEXT("旋转临时组") : TEXT("移动临时组");
        Command.Before = RuntimeDragBeforeValues;
        for (ATwinInstance* Instance : RuntimeSelectedInstances)
        {
            if (Instance && IsValid(Instance)) Command.After.Add(CaptureRuntimeValue(Instance));
        }

        bool bChanged = false;
        for (int32 Index = 0; Index < Command.Before.Num() && Index < Command.After.Num(); ++Index)
        {
            if (!Command.Before[Index].Transform.Equals(Command.After[Index].Transform, 0.01f) ||
                Command.Before[Index].bLoaded != Command.After[Index].bLoaded)
            {
                bChanged = true;
                break;
            }
        }
        if (bChanged) RecordRuntimeCommand(MoveTemp(Command));
    }

    SetRuntimeCameraLookSuppressed(false);
    bRuntimeDragging = false;
    RuntimeHoverPart = ERuntimeDragPart::None;
    RuntimeDragPart = ERuntimeDragPart::None;
    RuntimeSnapFeedback = ERuntimeSnapFeedback::None;
    RuntimeSnapFeedbackPoint = FVector::ZeroVector;
    RuntimeDragStartTransforms.Reset();
    RuntimeDragBeforeValues.Reset();
    UpdateRuntimeSelectionGeometry();
    RefreshRuntimeEditDirtyState();
    UpdateRuntimeEditorPanel();
}

ATwinSceneManager::FRuntimeEditValue ATwinSceneManager::CaptureRuntimeValue(
    ATwinInstance* Instance) const
{
    FRuntimeEditValue Value;
    Value.Instance = Instance;
    if (Instance && IsValid(Instance))
    {
        Value.Transform = Instance->GetActorTransform();
        Value.bLoaded = Instance->IsRuntimeLoaded();
    }
    return Value;
}

TSharedPtr<FJsonObject> ATwinSceneManager::CloneRuntimeBusinessConfig(
    const TSharedPtr<FJsonObject>& Source) const
{
    if (!Source.IsValid()) return nullptr;
    FString Json;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
    if (!FJsonSerializer::Serialize(Source.ToSharedRef(), Writer)) return nullptr;
    TSharedPtr<FJsonObject> Result;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
    return FJsonSerializer::Deserialize(Reader, Result) ? Result : nullptr;
}

bool ATwinSceneManager::RefreshRuntimeBusinessSnapshot()
{
    if (!bRuntimeEditMode || bRuntimeEditSaving || bRuntimeBusinessDirty
        || !WebInteractionManager)
    {
        return false;
    }

    const bool bHasBusinessHistory = RuntimeUndoStack.ContainsByPredicate(
        [](const FRuntimeEditCommand& Command) { return Command.bBusinessCommand; })
        || RuntimeRedoStack.ContainsByPredicate(
            [](const FRuntimeEditCommand& Command) { return Command.bBusinessCommand; });
    if (bHasBusinessHistory)
    {
        return false;
    }

    const int32 AvailableRevision = WebInteractionManager->GetRuntimeBusinessEditRevision();
    if (AvailableRevision < 0
        || (RuntimeBusinessConfig.IsValid() && AvailableRevision <= RuntimeBusinessRevision))
    {
        return false;
    }

    TSharedPtr<FJsonObject> Snapshot;
    int32 SnapshotRevision = -1;
    if (!WebInteractionManager->GetRuntimeBusinessEditSnapshot(Snapshot, SnapshotRevision))
    {
        return false;
    }

    RuntimeBusinessConfig = Snapshot;
    RuntimeBusinessBaseline = CloneRuntimeBusinessConfig(Snapshot);
    RuntimeBusinessRevision = SnapshotRevision;
    return true;
}

void ATwinSceneManager::GetRuntimeSelectedInstanceIds(
    TArray<FString>& OutInstanceIds) const
{
    OutInstanceIds.Reset();
    for (const ATwinInstance* Instance : RuntimeSelectedInstances)
    {
        if (Instance && IsValid(Instance)) OutInstanceIds.Add(Instance->GetInstanceId());
    }
}

void ATwinSceneManager::GetRuntimeSelectedInstanceDisplayNames(
    TArray<FString>& OutNames) const
{
    OutNames.Reset();
    for (const ATwinInstance* Instance : RuntimeSelectedInstances)
    {
        if (!Instance || !IsValid(Instance)) continue;
        const FString DisplayName = Instance->GetTwinDisplayName();
        OutNames.Add(DisplayName.IsEmpty() ? Instance->GetInstanceId() : DisplayName);
    }
}

void ATwinSceneManager::GetRuntimeBusinessMembershipRows(
    TArray<FString>& OutBusinessIds,
    TArray<FString>& OutNames,
    TArray<uint8>& OutStates) const
{
    OutBusinessIds.Reset();
    OutNames.Reset();
    OutStates.Reset();
    if (!WebInteractionManager || !RuntimeBusinessConfig.IsValid()) return;
    TArray<FString> SelectedIds;
    GetRuntimeSelectedInstanceIds(SelectedIds);
    TArray<EOntoTwinBusinessMembershipState> States;
    WebInteractionManager->GetRuntimeBusinessMembershipStates(
        RuntimeBusinessConfig, SelectedIds, OutBusinessIds, OutNames, States);
    for (const EOntoTwinBusinessMembershipState State : States)
    {
        OutStates.Add(static_cast<uint8>(State));
    }
}

bool ATwinSceneManager::ToggleRuntimeBusinessMembership(const FString& BusinessId)
{
    if (!bRuntimeEditMode || bRuntimeEditSaving || !WebInteractionManager
        || !RuntimeBusinessConfig.IsValid()) return false;
    TArray<FString> SelectedIds;
    GetRuntimeSelectedInstanceIds(SelectedIds);
    if (SelectedIds.Num() == 0)
    {
        RuntimeStatusMessage = TEXT("请先在场景中选择对象");
        UpdateRuntimeEditorPanel();
        return false;
    }
    TArray<FString> Ids;
    TArray<FString> Names;
    TArray<EOntoTwinBusinessMembershipState> States;
    WebInteractionManager->GetRuntimeBusinessMembershipStates(
        RuntimeBusinessConfig, SelectedIds, Ids, Names, States);
    const int32 Index = Ids.IndexOfByKey(BusinessId);
    if (!States.IsValidIndex(Index)) return false;

    FRuntimeEditCommand Command;
    Command.Label = States[Index] == EOntoTwinBusinessMembershipState::All
        ? TEXT("移出业务") : TEXT("加入业务");
    Command.bBusinessCommand = true;
    Command.BusinessBefore = CloneRuntimeBusinessConfig(RuntimeBusinessConfig);
    FString Error;
    const bool bAdd = States[Index] != EOntoTwinBusinessMembershipState::All;
    if (!WebInteractionManager->SetRuntimeBusinessMembership(
        RuntimeBusinessConfig, BusinessId, SelectedIds, bAdd, Error))
    {
        RuntimeStatusMessage = Error;
        UpdateRuntimeEditorPanel();
        return false;
    }
    Command.BusinessAfter = CloneRuntimeBusinessConfig(RuntimeBusinessConfig);
    RecordRuntimeCommand(MoveTemp(Command));
    RefreshRuntimeBusinessDirtyState();
    RefreshRuntimeEditDirtyState();
    RuntimeStatusMessage = bAdd ? TEXT("已加入业务，等待保存") : TEXT("已移出业务，等待保存");
    UpdateRuntimeEditorPanel();
    return true;
}

bool ATwinSceneManager::CreateRuntimeBusiness(const FString& Name)
{
    if (!bRuntimeEditMode || bRuntimeEditSaving || !WebInteractionManager
        || !RuntimeBusinessConfig.IsValid()) return false;
    TArray<FString> SelectedIds;
    GetRuntimeSelectedInstanceIds(SelectedIds);
    if (SelectedIds.Num() == 0)
    {
        RuntimeStatusMessage = TEXT("请先在场景中选择对象");
        UpdateRuntimeEditorPanel();
        return false;
    }
    FRuntimeEditCommand Command;
    Command.Label = TEXT("新建业务");
    Command.bBusinessCommand = true;
    Command.BusinessBefore = CloneRuntimeBusinessConfig(RuntimeBusinessConfig);
    FString BusinessId;
    FString Error;
    if (!WebInteractionManager->CreateRuntimeBusiness(
        RuntimeBusinessConfig, Name, SelectedIds, BusinessId, Error))
    {
        RuntimeStatusMessage = Error;
        UpdateRuntimeEditorPanel();
        return false;
    }
    Command.BusinessAfter = CloneRuntimeBusinessConfig(RuntimeBusinessConfig);
    RecordRuntimeCommand(MoveTemp(Command));
    RefreshRuntimeBusinessDirtyState();
    RefreshRuntimeEditDirtyState();
    RuntimeStatusMessage = TEXT("业务已创建，等待保存");
    UpdateRuntimeEditorPanel();
    return true;
}

void ATwinSceneManager::ApplyRuntimeValue(const FRuntimeEditValue& Value)
{
    ATwinInstance* Instance = Value.Instance.Get();
    if (!Instance || !IsValid(Instance)) return;
    EnsureRuntimeEditState(Instance);
    Instance->SetActorTransform(Value.Transform);
    Instance->SetRuntimeEditorLoadedOverride(Value.bLoaded);
}

void ATwinSceneManager::RecordRuntimeCommand(FRuntimeEditCommand&& Command)
{
    RuntimeUndoStack.Add(MoveTemp(Command));
    while (RuntimeUndoStack.Num() > RuntimeEditorMaxHistory)
    {
        RuntimeUndoStack.RemoveAt(0);
    }
    RuntimeRedoStack.Reset();
}

void ATwinSceneManager::ApplyRuntimeCommand(
    const FRuntimeEditCommand& Command,
    bool bUseAfter)
{
    if (Command.bBusinessCommand)
    {
        RuntimeBusinessConfig = CloneRuntimeBusinessConfig(
            bUseAfter ? Command.BusinessAfter : Command.BusinessBefore);
        RefreshRuntimeBusinessDirtyState();
        RefreshRuntimeEditDirtyState();
        return;
    }
    const TArray<FRuntimeEditValue>& Values = bUseAfter ? Command.After : Command.Before;
    for (const FRuntimeEditValue& Value : Values)
    {
        ApplyRuntimeValue(Value);
    }
    RefreshRuntimeEditDirtyState();
    ReleaseCleanUnselectedRuntimeStates();
    UpdateRuntimeSelectionGeometry();
}

void ATwinSceneManager::UndoRuntimeEdit()
{
    if (!CanUndoRuntimeEdit()) return;
    FRuntimeEditCommand Command = MoveTemp(RuntimeUndoStack.Last());
    RuntimeUndoStack.Pop();
    ApplyRuntimeCommand(Command, false);
    RuntimeRedoStack.Add(MoveTemp(Command));
    RuntimeStatusMessage = TEXT("已撤销上一步");
    if (RuntimeEditorPanel && IsValid(RuntimeEditorPanel))
    {
        RuntimeEditorPanel->ShowToast(RuntimeStatusMessage, EOntoTwinRuntimeToastType::Info);
    }
    UpdateRuntimeEditorPanel();
}

void ATwinSceneManager::RedoRuntimeEdit()
{
    if (!CanRedoRuntimeEdit()) return;
    FRuntimeEditCommand Command = MoveTemp(RuntimeRedoStack.Last());
    RuntimeRedoStack.Pop();
    ApplyRuntimeCommand(Command, true);
    RuntimeUndoStack.Add(MoveTemp(Command));
    RuntimeStatusMessage = TEXT("已重做上一步");
    if (RuntimeEditorPanel && IsValid(RuntimeEditorPanel))
    {
        RuntimeEditorPanel->ShowToast(RuntimeStatusMessage, EOntoTwinRuntimeToastType::Info);
    }
    UpdateRuntimeEditorPanel();
}

void ATwinSceneManager::RemoveRuntimeSelectionFromScene()
{
    if (!CanRemoveRuntimeSelection()) return;
    if (!CanStageRuntimeSelection())
    {
        RuntimeStatusMessage = FString::Printf(TEXT("会话最多保存 %d 个实例"), RuntimeEditorMaxPending);
        if (RuntimeEditorPanel && IsValid(RuntimeEditorPanel))
        {
            RuntimeEditorPanel->ShowToast(RuntimeStatusMessage, EOntoTwinRuntimeToastType::Warning);
        }
        return;
    }

    FRuntimeEditCommand Command;
    Command.Label = TEXT("从场景移除");
    const TArray<ATwinInstance*> Removing = RuntimeSelectedInstances;
    for (ATwinInstance* Instance : Removing)
    {
        if (!Instance || !IsValid(Instance)) continue;
        EnsureRuntimeEditState(Instance);
        Command.Before.Add(CaptureRuntimeValue(Instance));
        Instance->SetRuntimeEditorLoadedOverride(false);
        Command.After.Add(CaptureRuntimeValue(Instance));
    }
    RecordRuntimeCommand(MoveTemp(Command));

    RuntimeSelectedInstances.Reset();
    RuntimeSelectedInstance = nullptr;
    RuntimeSelectionLocalBounds.Reset();
    if (RuntimeGizmo && IsValid(RuntimeGizmo)) RuntimeGizmo->SetGizmoEnabled(false);
    RefreshRuntimeEditDirtyState();
    RuntimeStatusMessage = FString::Printf(TEXT("已标记从场景移除 %d 个实例"), Removing.Num());
    if (RuntimeEditorPanel && IsValid(RuntimeEditorPanel))
    {
        RuntimeEditorPanel->ShowToast(
            TEXT("已从视口隐藏，保存后生效；可按 Ctrl+Z 撤销"),
            EOntoTwinRuntimeToastType::Warning);
    }
    UpdateRuntimeEditorPanel();
}

void ATwinSceneManager::ApplyRuntimeGroupTransform(
    const FVector& NewPivot,
    float YawDeltaDegrees)
{
    const FQuat DeltaRotation = FRotator(0.0f, YawDeltaDegrees, 0.0f).Quaternion();
    for (ATwinInstance* Instance : RuntimeSelectedInstances)
    {
        if (!Instance || !IsValid(Instance)) continue;
        const FTransform* Start = RuntimeDragStartTransforms.Find(Instance->GetInstanceId());
        if (!Start) continue;

        const FVector StartOffset = Start->GetLocation() - RuntimeDragStartPivotWorld;
        const FVector NewLocation = NewPivot + DeltaRotation.RotateVector(StartOffset);
        FRotator NewRotation = Start->Rotator();
        NewRotation.Yaw += YawDeltaDegrees;
        Instance->SetActorLocationAndRotation(NewLocation, NewRotation);
    }
}

float ATwinSceneManager::CalculateRuntimeSelectionRadiusAlongNormal(
    const FVector& WorldNormal,
    float YawDeltaDegrees) const
{
    const FVector Normal = WorldNormal.GetSafeNormal();
    const FQuat DeltaRotation = FRotator(0.0f, YawDeltaDegrees, 0.0f).Quaternion();
    float Radius = 2.0f;
    for (int32 Index = 0; Index < RuntimeSelectedInstances.Num(); ++Index)
    {
        ATwinInstance* Instance = RuntimeSelectedInstances[Index];
        if (!Instance || !IsValid(Instance) || !RuntimeSelectionLocalBounds.IsValidIndex(Index)) continue;
        const FTransform* Start = RuntimeDragStartTransforms.Find(Instance->GetInstanceId());
        if (!Start) continue;

        const FBox& Bounds = RuntimeSelectionLocalBounds[Index];
        const FVector Min = Bounds.Min;
        const FVector Max = Bounds.Max;
        const FVector Corners[8] =
        {
            FVector(Min.X, Min.Y, Min.Z), FVector(Max.X, Min.Y, Min.Z),
            FVector(Max.X, Max.Y, Min.Z), FVector(Min.X, Max.Y, Min.Z),
            FVector(Min.X, Min.Y, Max.Z), FVector(Max.X, Min.Y, Max.Z),
            FVector(Max.X, Max.Y, Max.Z), FVector(Min.X, Max.Y, Max.Z)
        };
        for (const FVector& Corner : Corners)
        {
            const FVector StartWorld = Start->TransformPosition(Corner);
            const FVector RotatedOffset = DeltaRotation.RotateVector(
                StartWorld - RuntimeDragStartPivotWorld);
            Radius = FMath::Max(Radius, FMath::Abs(FVector::DotProduct(RotatedOffset, Normal)));
        }
    }
    return Radius + 2.0f;
}

void ATwinSceneManager::ApplyRuntimeGroupSnaps(
    FVector& InOutPivot,
    float& InOutYawDelta)
{
    RuntimeSnapFeedback = ERuntimeSnapFeedback::None;
    RuntimeSnapFeedbackPoint = InOutPivot;

    if (bEnableGridSnap && GridSnapSizeCm > 0.0f)
    {
        const FVector Before = InOutPivot;
        if (RuntimeDragPart == ERuntimeDragPart::MoveX || RuntimeDragPart == ERuntimeDragPart::MoveXY)
        {
            InOutPivot.X = FMath::GridSnap(InOutPivot.X, GridSnapSizeCm);
        }
        if (RuntimeDragPart == ERuntimeDragPart::MoveY || RuntimeDragPart == ERuntimeDragPart::MoveXY)
        {
            InOutPivot.Y = FMath::GridSnap(InOutPivot.Y, GridSnapSizeCm);
        }
        if (!Before.Equals(InOutPivot, 0.01f))
        {
            RuntimeSnapFeedback = ERuntimeSnapFeedback::Grid;
            RuntimeSnapFeedbackPoint = InOutPivot;
        }
    }

    if (RuntimeDragPart != ERuntimeDragPart::MoveXY || !bEnableWallSnap ||
        WallTag.IsNone() || RuntimeSelectedInstances.Num() == 0 || !GetWorld())
    {
        return;
    }

    TArray<AActor*> Walls;
    UGameplayStatics::GetAllActorsWithTag(GetWorld(), WallTag, Walls);
    AActor* BestWall = nullptr;
    FVector BestClosest = FVector::ZeroVector;
    FVector BestNormal = FVector::ForwardVector;
    float BestGap = WallSnapDistanceCm;

    for (AActor* Wall : Walls)
    {
        if (!Wall || !IsValid(Wall)) continue;
        const FBox WallBounds = Wall->GetComponentsBoundingBox(true);
        if (!WallBounds.IsValid) continue;
        FVector Closest = WallBounds.GetClosestPointTo(InOutPivot);
        Closest.Z = InOutPivot.Z;
        FVector Normal = InOutPivot - Closest;
        Normal.Z = 0.0f;
        const float CenterDistance = Normal.Size();
        if (!Normal.Normalize())
        {
            Normal = InOutPivot - WallBounds.GetCenter();
            Normal.Z = 0.0f;
            if (!Normal.Normalize()) Normal = FVector::ForwardVector;
        }
        const float Radius = CalculateRuntimeSelectionRadiusAlongNormal(Normal, InOutYawDelta);
        const float Gap = FMath::Max(0.0f, CenterDistance - Radius);
        if (Gap <= BestGap)
        {
            BestGap = Gap;
            BestWall = Wall;
            BestClosest = Closest;
            BestNormal = Normal;
        }
    }

    if (!BestWall) return;
    const FVector Tangent(-BestNormal.Y, BestNormal.X, 0.0f);
    const float TargetYaw = Tangent.Rotation().Yaw;
    if (RuntimeSelectedInstances.Num() == 1)
    {
        const FTransform* Start = RuntimeDragStartTransforms.Find(
            RuntimeSelectedInstances[0]->GetInstanceId());
        if (Start)
        {
            InOutYawDelta = FMath::FindDeltaAngleDegrees(Start->Rotator().Yaw, TargetYaw);
        }
    }
    else
    {
        InOutYawDelta = FMath::FindDeltaAngleDegrees(RuntimeDragStartGroupYawDelta, TargetYaw);
    }
    const float Radius = CalculateRuntimeSelectionRadiusAlongNormal(BestNormal, InOutYawDelta);
    InOutPivot.X = BestClosest.X + BestNormal.X * Radius;
    InOutPivot.Y = BestClosest.Y + BestNormal.Y * Radius;
    RuntimeSnapFeedback = ERuntimeSnapFeedback::Wall;
    RuntimeSnapFeedbackPoint = BestClosest;
}

void ATwinSceneManager::SetRuntimeCameraLookSuppressed(bool bSuppress)
{
    if (bRuntimeCameraLookSuppressed == bSuppress)
    {
        return;
    }

    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    if (!PC)
    {
        bRuntimeCameraLookSuppressed = false;
        bRuntimeLookInputWasAlreadyIgnored = false;
        return;
    }

    if (bSuppress)
    {
        bRuntimeLookInputWasAlreadyIgnored = PC->IsLookInputIgnored();
        if (!bRuntimeLookInputWasAlreadyIgnored)
        {
            PC->SetIgnoreLookInput(true);
        }
        bRuntimeCameraLookSuppressed = true;
        return;
    }

    if (!bRuntimeLookInputWasAlreadyIgnored)
    {
        PC->SetIgnoreLookInput(false);
    }
    bRuntimeCameraLookSuppressed = false;
    bRuntimeLookInputWasAlreadyIgnored = false;
}

void ATwinSceneManager::ApplyRuntimeSnaps(FVector& InOutLocation, FRotator& InOutRotation)
{
    RuntimeSnapFeedback = ERuntimeSnapFeedback::None;
    InOutLocation.Z = RuntimeEditPlaneZ;

    const FVector ActorScale = RuntimeDragStartTransform.GetScale3D();
    FTransform CandidateTransform(InOutRotation, InOutLocation, ActorScale);
    FVector CandidatePivot = CandidateTransform.TransformPosition(RuntimeEditPivotLocal);
    RuntimeSnapFeedbackPoint = CandidatePivot;

    if (bEnableGridSnap && GridSnapSizeCm > 0.0f)
    {
        const FVector BeforeGridSnap = CandidatePivot;
        if (RuntimeDragPart == ERuntimeDragPart::MoveX || RuntimeDragPart == ERuntimeDragPart::MoveXY)
        {
            CandidatePivot.X = FMath::GridSnap(CandidatePivot.X, GridSnapSizeCm);
        }
        if (RuntimeDragPart == ERuntimeDragPart::MoveY || RuntimeDragPart == ERuntimeDragPart::MoveXY)
        {
            CandidatePivot.Y = FMath::GridSnap(CandidatePivot.Y, GridSnapSizeCm);
        }

        const FVector GridDelta = CandidatePivot - BeforeGridSnap;
        InOutLocation += GridDelta;
        if (!GridDelta.IsNearlyZero(0.01f))
        {
            RuntimeSnapFeedback = ERuntimeSnapFeedback::Grid;
            RuntimeSnapFeedbackPoint = CandidatePivot;
        }
    }

    if (RuntimeDragPart != ERuntimeDragPart::MoveXY ||
        !bEnableWallSnap ||
        WallTag.IsNone() ||
        !RuntimeSelectedInstance ||
        !GetWorld())
    {
        return;
    }

    TArray<AActor*> Walls;
    UGameplayStatics::GetAllActorsWithTag(GetWorld(), WallTag, Walls);
    if (Walls.Num() == 0)
    {
        return;
    }

    AActor* BestWall = nullptr;
    FVector BestClosest = FVector::ZeroVector;
    float BestSurfaceGap = WallSnapDistanceCm;

    for (AActor* Wall : Walls)
    {
        if (!Wall || !IsValid(Wall))
        {
            continue;
        }

        const FBox WallBounds = Wall->GetComponentsBoundingBox(true);
        if (!WallBounds.IsValid)
        {
            continue;
        }

        FVector Closest = WallBounds.GetClosestPointTo(CandidatePivot);
        Closest.Z = CandidatePivot.Z;

        const FVector2D Delta2D(CandidatePivot.X - Closest.X, CandidatePivot.Y - Closest.Y);
        FVector CandidateNormal(Delta2D.X, Delta2D.Y, 0.0f);
        const float CenterDistance = CandidateNormal.Size();
        if (!CandidateNormal.Normalize())
        {
            CandidateNormal = CandidatePivot - WallBounds.GetCenter();
            CandidateNormal.Z = 0.0f;
            if (!CandidateNormal.Normalize()) CandidateNormal = FVector::ForwardVector;
        }

        const float CandidateRadius = CalculateRuntimeBoundsRadiusAlongNormal(
            CandidateNormal,
            InOutRotation,
            ActorScale);
        const float SurfaceGap = FMath::Max(0.0f, CenterDistance - CandidateRadius);
        if (SurfaceGap <= BestSurfaceGap)
        {
            BestSurfaceGap = SurfaceGap;
            BestWall = Wall;
            BestClosest = Closest;
        }
    }

    if (!BestWall)
    {
        return;
    }

    const FBox WallBounds = BestWall->GetComponentsBoundingBox(true);
    FVector Normal = CandidatePivot - BestClosest;
    Normal.Z = 0.0f;
    if (!Normal.Normalize())
    {
        const FVector Center = WallBounds.GetCenter();
        Normal = CandidatePivot - Center;
        Normal.Z = 0.0f;
        if (!Normal.Normalize())
        {
            Normal = FVector::ForwardVector;
        }
    }

    const FVector Tangent(-Normal.Y, Normal.X, 0.0f);
    InOutRotation.Yaw = Tangent.Rotation().Yaw;
    const float TargetRadiusAlongNormal = CalculateRuntimeBoundsRadiusAlongNormal(
        Normal,
        InOutRotation,
        ActorScale);
    const FVector SnappedPivot = BestClosest + Normal * TargetRadiusAlongNormal;
    InOutLocation = CalculateRuntimeActorLocationForPivot(
        SnappedPivot,
        InOutRotation,
        ActorScale);
    InOutLocation.Z = RuntimeEditPlaneZ;
    RuntimeSnapFeedback = ERuntimeSnapFeedback::Wall;
    RuntimeSnapFeedbackPoint = BestClosest;
}

void ATwinSceneManager::MarkRuntimeDirtyFromTransform()
{
    if (!RuntimeSelectedInstance || !IsValid(RuntimeSelectedInstance))
    {
        bRuntimeEditDirty = false;
        return;
    }

    bRuntimeEditDirty = !RuntimeSelectedInstance->GetActorTransform().Equals(RuntimeEditBaseline, 0.01f);
    RuntimeStatusMessage = bRuntimeEditDirty ? TEXT("Dirty edit; save or cancel") : TEXT("Transform matches baseline");
}

void ATwinSceneManager::SaveRuntimeEdit()
{
    if (!bRuntimeEditDirty)
    {
        RuntimeStatusMessage = TEXT("没有待保存修改");
        UpdateRuntimeEditorPanel();
        return;
    }
    if (!bRuntimeCanSave)
    {
        CancelRuntimeEditExitRequest();
        RuntimeStatusMessage = TEXT("当前数据集访问未就绪");
        if (RuntimeEditorPanel && IsValid(RuntimeEditorPanel))
        {
            RuntimeEditorPanel->ShowToast(
                TEXT("当前数据集未正确绑定，无法保存"),
                EOntoTwinRuntimeToastType::Warning);
        }
        UpdateRuntimeEditorPanel();
        return;
    }
    if (bRuntimeEditSaving)
    {
        return;
    }

    if (bRuntimeBusinessDirty)
    {
        if (!WebInteractionManager || !RuntimeBusinessConfig.IsValid()
            || RuntimeBusinessRevision < 0)
        {
            CancelRuntimeEditExitRequest();
            RuntimeStatusMessage = TEXT("业务配置尚未就绪，本地修改仍保留");
            UpdateRuntimeEditorPanel();
            return;
        }
        bRuntimeEditSaving = true;
        RuntimeStatusMessage = TEXT("正在保存业务修改...");
        UpdateRuntimeEditorPanel();
        TWeakObjectPtr<ATwinSceneManager> WeakThis(this);
        WebInteractionManager->ApplyRuntimeBusinessEdit(
            RuntimeBusinessConfig,
            RuntimeBusinessRevision,
            [WeakThis](bool bSuccess, int32 NewRevision, const FString& Error)
            {
                ATwinSceneManager* Self = WeakThis.Get();
                if (!Self) return;
                Self->bRuntimeEditSaving = false;
                if (!bSuccess)
                {
                    Self->CancelRuntimeEditExitRequest();
                    Self->RuntimeStatusMessage = Error;
                    if (Self->RuntimeEditorPanel && IsValid(Self->RuntimeEditorPanel))
                    {
                        Self->RuntimeEditorPanel->ShowToast(
                            Error,
                            EOntoTwinRuntimeToastType::Error);
                    }
                    Self->UpdateRuntimeEditorPanel();
                    return;
                }

                Self->RuntimeBusinessRevision = NewRevision;
                Self->RuntimeBusinessBaseline = Self->CloneRuntimeBusinessConfig(
                    Self->RuntimeBusinessConfig);
                Self->bRuntimeBusinessDirty = false;
                Self->RefreshRuntimeEditDirtyState();
                if (Self->bRuntimeEditDirty)
                {
                    Self->RuntimeStatusMessage = TEXT("业务修改已保存，继续保存场景修改...");
                    Self->SaveRuntimeEdit();
                    return;
                }

                Self->RuntimeUndoStack.Reset();
                Self->RuntimeRedoStack.Reset();
                Self->RuntimeStatusMessage = TEXT("业务修改已保存并应用");
                if (Self->RuntimeEditorPanel && IsValid(Self->RuntimeEditorPanel))
                {
                    Self->RuntimeEditorPanel->ShowToast(
                        TEXT("全部修改已保存"),
                        EOntoTwinRuntimeToastType::Success);
                }
                if (Self->bRuntimeExitAfterSave)
                {
                    Self->FinishExitRuntimeEditMode();
                    return;
                }
                Self->UpdateRuntimeEditorPanel();
            });
        return;
    }

    TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Changes;
    for (const TPair<FString, FRuntimeEditInstanceState>& Pair : RuntimeEditStates)
    {
        const FRuntimeEditInstanceState& State = Pair.Value;
        ATwinInstance* Instance = State.Instance.Get();
        if (!Instance || !IsValid(Instance) || (!State.bTransformDirty && !State.bLoadedDirty)) continue;

        const FTransform Cur = Instance->GetActorTransform();
        const FVector L = Cur.GetLocation();
        const FRotator R = Cur.Rotator();
        const FVector S = Cur.GetScale3D();

        TSharedPtr<FJsonObject> Change = MakeShared<FJsonObject>();
        Change->SetStringField(TEXT("instance_id"), Pair.Key);
        Change->SetStringField(TEXT("expected_state_hash"), State.BaselineStateHash);
        Change->SetBoolField(TEXT("is_loaded"), Instance->IsRuntimeLoaded());

        TSharedPtr<FJsonObject> TF = MakeShared<FJsonObject>();
        TF->SetNumberField(TEXT("tx"), L.X);
        TF->SetNumberField(TEXT("ty"), L.Y);
        TF->SetNumberField(TEXT("tz"), L.Z);
        TF->SetNumberField(TEXT("rx"), R.Roll);
        TF->SetNumberField(TEXT("ry"), R.Pitch);
        TF->SetNumberField(TEXT("rz"), R.Yaw);
        TF->SetNumberField(TEXT("sx"), S.X);
        TF->SetNumberField(TEXT("sy"), S.Y);
        TF->SetNumberField(TEXT("sz"), S.Z);
        Change->SetObjectField(TEXT("transform"), TF);
        Changes.Add(MakeShared<FJsonValueObject>(Change));
    }
    Body->SetArrayField(TEXT("changes"), Changes);

    FString BodyStr;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyStr);
    FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
    HttpRequest->SetURL(FString::Printf(TEXT("%s/api/v2/state/writeback/batch"), *BackendBaseUrl));
    HttpRequest->SetVerb(TEXT("POST"));
    HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    AddUEProjectHeaders(HttpRequest);
    HttpRequest->SetContentAsString(BodyStr);

    bRuntimeEditSaving = true;
    RuntimeStatusMessage = FString::Printf(TEXT("正在保存 %d 项修改..."), Changes.Num());
    UpdateRuntimeEditorPanel();

    TWeakObjectPtr<ATwinSceneManager> WeakThis(this);
    HttpRequest->OnProcessRequestComplete().BindLambda(
        [WeakThis](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bOk)
        {
            ATwinSceneManager* Self = WeakThis.Get();
            if (!Self) return;

            Self->bRuntimeEditSaving = false;

            const int32 Code = Resp.IsValid() ? Resp->GetResponseCode() : -1;
            TSharedPtr<FJsonObject> Root;
            if (Resp.IsValid())
            {
                TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Resp->GetContentAsString());
                FJsonSerializer::Deserialize(Reader, Root);
            }

            if (bOk && Resp.IsValid() && Code == 200)
            {
                TMap<FString, TSharedPtr<FJsonObject>> SnapshotsById;
                const TArray<TSharedPtr<FJsonValue>>* Snapshots = nullptr;
                if (Root.IsValid() && Root->TryGetArrayField(TEXT("snapshots"), Snapshots) && Snapshots)
                {
                    for (const TSharedPtr<FJsonValue>& Value : *Snapshots)
                    {
                        const TSharedPtr<FJsonObject> Snapshot = Value.IsValid() ? Value->AsObject() : nullptr;
                        FString InstanceId;
                        if (Snapshot.IsValid() && Snapshot->TryGetStringField(TEXT("instanceId"), InstanceId))
                        {
                            SnapshotsById.Add(InstanceId, Snapshot);
                        }
                    }
                }

                for (TPair<FString, FRuntimeEditInstanceState>& Pair : Self->RuntimeEditStates)
                {
                    FRuntimeEditInstanceState& State = Pair.Value;
                    ATwinInstance* Instance = State.Instance.Get();
                    if (!Instance || !IsValid(Instance) || (!State.bTransformDirty && !State.bLoadedDirty)) continue;
                    Instance->ClearRuntimeEditorLoadedOverride();
                    Instance->bLocalOverrideLock = false;
                    if (const TSharedPtr<FJsonObject>* Snapshot = SnapshotsById.Find(Pair.Key))
                    {
                        Instance->ApplySnapshot(*Snapshot);
                    }
                    State.BaselineTransform = Instance->GetActorTransform();
                    State.bBaselineLoaded = Instance->IsRuntimeLoaded();
                    State.BaselineStateHash = Instance->GetRuntimeEditStateHash();
                    State.bTransformDirty = false;
                    State.bLoadedDirty = false;
                    State.bConflict = false;
                    if (Self->RuntimeSelectedInstances.Contains(Instance))
                    {
                        Instance->bLocalOverrideLock = true;
                    }
                }

                Self->RuntimeUndoStack.Reset();
                Self->RuntimeRedoStack.Reset();
                Self->RefreshRuntimeEditDirtyState();
                Self->ReleaseCleanUnselectedRuntimeStates();
                Self->RuntimeStatusMessage = TEXT("修改已保存");
                if (Self->RuntimeEditorPanel && IsValid(Self->RuntimeEditorPanel))
                {
                    Self->RuntimeEditorPanel->ShowToast(
                        TEXT("全部修改已保存"),
                        EOntoTwinRuntimeToastType::Success);
                }
                if (Self->bRuntimeExitAfterSave)
                {
                    Self->FinishExitRuntimeEditMode();
                    return;
                }
            }
            else
            {
                FString Error;
                if (Root.IsValid())
                {
                    Root->TryGetStringField(TEXT("error"), Error);
                    if (Code == 409)
                    {
                        const TArray<TSharedPtr<FJsonValue>>* Conflicts = nullptr;
                        if (Root->TryGetArrayField(TEXT("conflicts"), Conflicts) && Conflicts)
                        {
                            for (const TSharedPtr<FJsonValue>& Value : *Conflicts)
                            {
                                const TSharedPtr<FJsonObject> Conflict = Value.IsValid() ? Value->AsObject() : nullptr;
                                FString InstanceId;
                                if (Conflict.IsValid() && Conflict->TryGetStringField(TEXT("instance_id"), InstanceId))
                                {
                                    if (FRuntimeEditInstanceState* State = Self->RuntimeEditStates.Find(InstanceId))
                                    {
                                        State->bConflict = true;
                                    }
                                }
                            }
                        }
                    }
                }
                Self->CancelRuntimeEditExitRequest();
                Self->RuntimeStatusMessage = Code == 409
                    ? TEXT("保存冲突：后端状态已变化")
                    : FString::Printf(TEXT("保存失败（%d）"), Code);
                if (Self->RuntimeEditorPanel && IsValid(Self->RuntimeEditorPanel))
                {
                    Self->RuntimeEditorPanel->ShowToast(
                        Code == 409
                            ? TEXT("部分实例已被其他客户端修改，本地修改仍保留")
                            : TEXT("保存失败，本地修改仍保留"),
                        EOntoTwinRuntimeToastType::Error);
                }
            }

            Self->UpdateRuntimeEditorPanel();
        });

    HttpRequest->ProcessRequest();
}

void ATwinSceneManager::ApplyRuntimeSnapshotIfPresent(const TSharedPtr<FJsonObject>& ResponseObj)
{
    if (!RuntimeSelectedInstance || !IsValid(RuntimeSelectedInstance) || !ResponseObj.IsValid())
    {
        return;
    }

    const TSharedPtr<FJsonObject>* SnapshotObj = nullptr;
    if (ResponseObj->TryGetObjectField(TEXT("snapshot"), SnapshotObj) && SnapshotObj && SnapshotObj->IsValid())
    {
        RuntimeSelectedInstance->bLocalOverrideLock = false;
        RuntimeSelectedInstance->ApplySnapshot(*SnapshotObj);
    }
}

void ATwinSceneManager::CancelRuntimeEdit()
{
    if (!bRuntimeEditDirty)
    {
        RuntimeStatusMessage = TEXT("没有待取消的修改");
        UpdateRuntimeEditorPanel();
        return;
    }

    TArray<FString> StateIds;
    RuntimeEditStates.GetKeys(StateIds);
    RuntimeSelectedInstances.Reset();
    RuntimeSelectedInstance = nullptr;
    for (const FString& InstanceId : StateIds)
    {
        ReleaseRuntimeEditState(InstanceId, true);
    }
    RuntimeUndoStack.Reset();
    RuntimeRedoStack.Reset();
    RuntimeBusinessConfig = CloneRuntimeBusinessConfig(RuntimeBusinessBaseline);
    bRuntimeBusinessDirty = false;
    bRuntimeEditDirty = false;
    bRuntimeEditSaving = false;
    bRuntimeExitAfterSave = false;
    RuntimeSelectionYawDelta = 0.0f;
    if (RuntimeGizmo && IsValid(RuntimeGizmo)) RuntimeGizmo->SetGizmoEnabled(false);
    RuntimeStatusMessage = TEXT("已取消全部修改");
    if (RuntimeEditorPanel && IsValid(RuntimeEditorPanel))
    {
        RuntimeEditorPanel->ShowToast(
            TEXT("全部修改已恢复"),
            EOntoTwinRuntimeToastType::Info);
    }
    UpdateRuntimeEditorPanel();
}

void ATwinSceneManager::SetRuntimeWallSnapEnabled(bool bEnabled)
{
    bEnableWallSnap = bEnabled;
    RuntimeStatusMessage = bEnabled ? TEXT("靠墙吸附已开启") : TEXT("靠墙吸附已关闭");
    UpdateRuntimeEditorPanel();
}

void ATwinSceneManager::SetRuntimeGridSnapEnabled(bool bEnabled)
{
    bEnableGridSnap = bEnabled;
    RuntimeStatusMessage = bEnabled ? TEXT("网格吸附已开启") : TEXT("网格吸附已关闭");
    UpdateRuntimeEditorPanel();
}

FString ATwinSceneManager::GetRuntimeEditorModeText() const
{
    return bRuntimeEditMode ? TEXT("模式：运行时编辑") : TEXT("模式：运行时查看");
}

FString ATwinSceneManager::GetRuntimeEditorBindingText() const
{
    switch (GetRuntimeEditorAccessState())
    {
    case EOntoTwinRuntimeAccessState::Checking:
        return TEXT("数据集访问：正在检查");
    case EOntoTwinRuntimeAccessState::Ready:
        return TEXT("数据集访问：可用");
    case EOntoTwinRuntimeAccessState::Unbound:
        return TEXT("数据集访问：尚未绑定");
    case EOntoTwinRuntimeAccessState::Mismatch:
        return TEXT("数据集访问：已绑定其他 UE 工程");
    case EOntoTwinRuntimeAccessState::Error:
    default:
        return TEXT("数据集访问：无法验证");
    }
}

FString ATwinSceneManager::GetRuntimeEditorSelectionText() const
{
    const int32 SelectionCount = GetRuntimeEditSelectionCount();
    if (SelectionCount == 0)
    {
        return TEXT("选择：无");
    }
    return FString::Printf(TEXT("选择：%d 个实例%s"),
        SelectionCount,
        bRuntimeEditDirty ? TEXT(" *") : TEXT(""));
}

FString ATwinSceneManager::GetRuntimeEditorTransformText() const
{
    FVector Location = FVector::ZeroVector;
    float Yaw = 0.0f;
    if (!GetRuntimeEditorTransform(Location, Yaw))
    {
        return TEXT("变换：-");
    }
    return FString::Printf(TEXT("变换：X %.1f | Y %.1f | Z %.1f | %s %.1f"),
        Location.X, Location.Y, Location.Z,
        IsRuntimeSelectionMultiple() ? TEXT("偏航增量") : TEXT("偏航"), Yaw);
}

FString ATwinSceneManager::GetRuntimeEditorStatusText() const
{
    return FString::Printf(TEXT("状态：%s"), *RuntimeStatusMessage);
}

FString ATwinSceneManager::GetRuntimeEditorHeaderStateText() const
{
    if (bRuntimeEditSaving)
    {
        return TEXT("正在保存");
    }
    if (bRuntimeDragging && RuntimeSnapFeedback == ERuntimeSnapFeedback::Wall)
    {
        return TEXT("已吸附墙面");
    }
    if (bRuntimeDragging && RuntimeSnapFeedback == ERuntimeSnapFeedback::Grid)
    {
        return TEXT("已吸附网格");
    }
    if (bRuntimeDragging && RuntimeDragPart == ERuntimeDragPart::MoveXY)
    {
        return TEXT("正在移动 XY");
    }
    if (bRuntimeDragging && RuntimeDragPart == ERuntimeDragPart::MoveX)
    {
        return TEXT("正在移动 X");
    }
    if (bRuntimeDragging && RuntimeDragPart == ERuntimeDragPart::MoveY)
    {
        return TEXT("正在移动 Y");
    }
    if (bRuntimeDragging && RuntimeDragPart == ERuntimeDragPart::MoveZ)
    {
        return TEXT("正在移动 Z");
    }
    if (bRuntimeDragging && RuntimeDragPart == ERuntimeDragPart::RotateYaw)
    {
        return TEXT("正在旋转");
    }
    if (bRuntimeEditDirty)
    {
        return FString::Printf(TEXT("%d 项待保存"), GetRuntimeEditPendingCount());
    }
    if (RuntimeHoverPart == ERuntimeDragPart::MoveXY)
    {
        return TEXT("移动 XY");
    }
    if (RuntimeHoverPart == ERuntimeDragPart::MoveX)
    {
        return TEXT("移动 X");
    }
    if (RuntimeHoverPart == ERuntimeDragPart::MoveY)
    {
        return TEXT("移动 Y");
    }
    if (RuntimeHoverPart == ERuntimeDragPart::MoveZ)
    {
        return TEXT("移动 Z");
    }
    if (RuntimeHoverPart == ERuntimeDragPart::RotateYaw)
    {
        return TEXT("旋转偏航角");
    }
    return TEXT("编辑中");
}

FString ATwinSceneManager::GetRuntimeEditorDisplayName() const
{
    const int32 SelectionCount = GetRuntimeEditSelectionCount();
    if (SelectionCount == 0)
    {
        return TEXT("未选择实例");
    }
    if (SelectionCount > 1)
    {
        return FString::Printf(TEXT("已选择 %d 个实例"), SelectionCount);
    }

    const FString DisplayName = RuntimeSelectedInstance->GetTwinDisplayName();
    return TruncateRuntimeLabel(DisplayName.IsEmpty() ? TEXT("孪生实例") : DisplayName, 34);
}

FString ATwinSceneManager::GetRuntimeEditorDisplayNameRaw() const
{
    return GetRuntimeEditSelectionCount() == 1 && RuntimeSelectedInstance
        ? RuntimeSelectedInstance->GetTwinDisplayName()
        : FString();
}

FString ATwinSceneManager::GetRuntimeEditorTypeName() const
{
    const int32 SelectionCount = GetRuntimeEditSelectionCount();
    if (SelectionCount == 0)
    {
        return TEXT("-");
    }
    if (SelectionCount > 1)
    {
        FString CommonTypeName;
        bool bHasCommonTypeName = false;
        for (const ATwinInstance* Instance : RuntimeSelectedInstances)
        {
            if (!Instance || !IsValid(Instance)) continue;
            const FString& TypeName = Instance->GetTwinObjectTypeName();
            if (!bHasCommonTypeName)
            {
                CommonTypeName = TypeName;
                bHasCommonTypeName = true;
            }
            else if (!CommonTypeName.Equals(TypeName, ESearchCase::CaseSensitive))
            {
                return TEXT("多个类型");
            }
        }
        return CommonTypeName.IsEmpty()
            ? TEXT("类型未知")
            : TruncateRuntimeLabel(CommonTypeName, 34);
    }

    const FString& TypeName = RuntimeSelectedInstance->GetTwinObjectTypeName();
    return TruncateRuntimeLabel(TypeName.IsEmpty() ? TEXT("类型未知") : TypeName, 34);
}

FString ATwinSceneManager::GetRuntimeEditorTypeNameRaw() const
{
    return GetRuntimeEditSelectionCount() == 1 && RuntimeSelectedInstance
        ? RuntimeSelectedInstance->GetTwinObjectTypeName()
        : FString();
}

FString ATwinSceneManager::GetRuntimeEditorInstanceIdText() const
{
    const int32 SelectionCount = GetRuntimeEditSelectionCount();
    if (SelectionCount == 0)
    {
        return TEXT("-");
    }
    if (SelectionCount > 1)
    {
        return TEXT("多个实例");
    }

    const FString InstanceId = RuntimeSelectedInstance->GetInstanceId();
    if (InstanceId.Len() <= 18)
    {
        return InstanceId;
    }
    return InstanceId.Left(8) + TEXT("...") + InstanceId.Right(5);
}

bool ATwinSceneManager::GetRuntimeEditorTransform(FVector& OutLocation, float& OutYaw) const
{
    if (GetRuntimeEditSelectionCount() == 0)
    {
        OutLocation = FVector::ZeroVector;
        OutYaw = 0.0f;
        return false;
    }

    OutLocation = RuntimeSelectionPivotWorld;
    OutYaw = IsRuntimeSelectionMultiple()
        ? RuntimeSelectionYawDelta
        : RuntimeSelectedInstance->GetActorRotation().Yaw;
    return true;
}

EOntoTwinRuntimeAccessState ATwinSceneManager::GetRuntimeEditorAccessState() const
{
    if (bRuntimeBindingRequestInFlight)
    {
        return EOntoTwinRuntimeAccessState::Checking;
    }
    if (RuntimeBindingMode == TEXT("matched"))
    {
        return EOntoTwinRuntimeAccessState::Ready;
    }
    if (RuntimeBindingMode == TEXT("unbound"))
    {
        return EOntoTwinRuntimeAccessState::Unbound;
    }
    if (RuntimeBindingMode == TEXT("ue_project_mismatch"))
    {
        return EOntoTwinRuntimeAccessState::Mismatch;
    }
    return EOntoTwinRuntimeAccessState::Error;
}

bool ATwinSceneManager::CanBindRuntimeProject() const
{
    return bRuntimeEditMode && !bRuntimeBindingRequestInFlight && RuntimeBindingMode == TEXT("unbound");
}

bool ATwinSceneManager::CanRetryRuntimeBindingStatus() const
{
    return bRuntimeEditMode && !bRuntimeBindingRequestInFlight &&
        GetRuntimeEditorAccessState() == EOntoTwinRuntimeAccessState::Error;
}

bool ATwinSceneManager::CanSaveRuntimeEdit() const
{
    return bRuntimeEditMode &&
        bRuntimeEditDirty &&
        !bRuntimeEditSaving &&
        bRuntimeCanSave;
}

bool ATwinSceneManager::CanCancelRuntimeEdit() const
{
    return bRuntimeEditMode &&
        bRuntimeEditDirty &&
        !bRuntimeEditSaving;
}

bool ATwinSceneManager::CanUndoRuntimeEdit() const
{
    return bRuntimeEditMode && !bRuntimeEditSaving && RuntimeUndoStack.Num() > 0;
}

bool ATwinSceneManager::CanRedoRuntimeEdit() const
{
    return bRuntimeEditMode && !bRuntimeEditSaving && RuntimeRedoStack.Num() > 0;
}

bool ATwinSceneManager::CanRemoveRuntimeSelection() const
{
    return bRuntimeEditMode && !bRuntimeEditSaving && GetRuntimeEditSelectionCount() > 0;
}

bool ATwinSceneManager::HasRuntimeEditSelection() const
{
    return GetRuntimeEditSelectionCount() > 0;
}

int32 ATwinSceneManager::GetRuntimeEditSelectionCount() const
{
    int32 Count = 0;
    for (const ATwinInstance* Instance : RuntimeSelectedInstances)
    {
        if (IsValid(Instance))
        {
            ++Count;
        }
    }
    return Count;
}

int32 ATwinSceneManager::GetRuntimeEditPendingCount() const
{
    int32 Count = 0;
    for (const TPair<FString, FRuntimeEditInstanceState>& Pair : RuntimeEditStates)
    {
        if (Pair.Value.bTransformDirty || Pair.Value.bLoadedDirty)
        {
            ++Count;
        }
    }
    return Count + (bRuntimeBusinessDirty ? 1 : 0);
}

void ATwinSceneManager::GetRuntimeEditPendingLines(TArray<FString>& OutLines) const
{
    OutLines.Reset();
    for (const TPair<FString, FRuntimeEditInstanceState>& Pair : RuntimeEditStates)
    {
        const FRuntimeEditInstanceState& State = Pair.Value;
        if (!State.bTransformDirty && !State.bLoadedDirty)
        {
            continue;
        }

        FString Name = Pair.Key;
        if (const ATwinInstance* Instance = State.Instance.Get())
        {
            const FString DisplayName = Instance->GetTwinDisplayName();
            if (!DisplayName.IsEmpty())
            {
                Name = DisplayName;
            }
        }

        FString ChangeText;
        if (State.bTransformDirty && State.bLoadedDirty)
        {
            ChangeText = TEXT("位置、从场景移除");
        }
        else if (State.bLoadedDirty)
        {
            ChangeText = TEXT("从场景移除");
        }
        else
        {
            ChangeText = TEXT("空间位置");
        }
        if (State.bConflict)
        {
            ChangeText += TEXT("（存在版本冲突）");
        }
        OutLines.Add(FString::Printf(TEXT("%s · %s"), *TruncateRuntimeLabel(Name, 24), *ChangeText));
    }
    if (bRuntimeBusinessDirty)
    {
        OutLines.Add(TEXT("业务归属 · 已修改"));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// FR-6 历史 actor 迁移工具（编辑器一次性）
// ═══════════════════════════════════════════════════════════════════════════

FString ATwinSceneManager::MigrationExportPath() const
{
    return FPaths::ConvertRelativePathToFull(FPaths::Combine(
        FPaths::ProjectSavedDir(), TEXT("OntoTwinMigration"), TEXT("ue_actors_export.json")));
}

FString ATwinSceneManager::MigrationResultPath() const
{
    return FPaths::ConvertRelativePathToFull(FPaths::Combine(
        FPaths::ProjectSavedDir(), TEXT("OntoTwinMigration"), TEXT("ue_migration_result.json")));
}

FString ATwinSceneManager::MigrationPreviewSnapshotPath() const
{
    return FPaths::ConvertRelativePathToFull(FPaths::Combine(
        FPaths::ProjectSavedDir(), TEXT("OntoTwinMigration"), TEXT("ue_snapshots.json")));
}

FString ATwinSceneManager::MigrationPreviewAuditPath() const
{
    return FPaths::ConvertRelativePathToFull(FPaths::Combine(
        FPaths::ProjectSavedDir(), TEXT("OntoTwinMigration"), TEXT("ue_preview_audit.json")));
}

void ATwinSceneManager::ExportSelectedActorsForMigration()
{
#if WITH_EDITOR
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : GetWorld();
    if (!World)
    {
        return;
    }

    FString TargetFolder = MigrationFolderName;
    TargetFolder.TrimStartAndEndInline();
    TSet<AActor*> CandidateActors;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* A = *It;
        if (!A || A == this) continue;
        if (A->IsA(ATwinInstance::StaticClass())) continue;
        if (IsInFolderOrChild(A->GetFolderPath(), TargetFolder))
        {
            CandidateActors.Add(A);
        }
    }

    // 文件夹里只需要放母 Actor。若一个候选又附着在另一个候选下面，只导出最上层母 Actor，
    // 防止同一批后代既作为部件又被重复建实例。
    TArray<AActor*> RootActors;
    for (AActor* Candidate : CandidateActors)
    {
        bool bHasCandidateAncestor = false;
        for (AActor* Parent = Candidate ? Candidate->GetAttachParentActor() : nullptr;
             Parent;
             Parent = Parent->GetAttachParentActor())
        {
            if (CandidateActors.Contains(Parent))
            {
                bHasCandidateAncestor = true;
                break;
            }
        }
        if (!bHasCandidateAncestor)
        {
            RootActors.Add(Candidate);
        }
    }
    RootActors.Sort([](const AActor& Lhs, const AActor& Rhs)
    {
        return Lhs.GetActorGuid().ToString(EGuidFormats::DigitsWithHyphens)
            < Rhs.GetActorGuid().ToString(EGuidFormats::DigitsWithHyphens);
    });

    auto ActorGuidString = [](const AActor* A)
    {
        return A
            ? A->GetActorGuid().ToString(EGuidFormats::DigitsWithHyphens)
            : FString();
    };
    auto TransformToJson = [](const FTransform& Transform)
    {
        const FVector Loc = Transform.GetLocation();
        const FRotator Rot = Transform.Rotator();
        const FVector Scale = Transform.GetScale3D();
        TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
        Json->SetNumberField(TEXT("tx"), Loc.X);
        Json->SetNumberField(TEXT("ty"), Loc.Y);
        Json->SetNumberField(TEXT("tz"), Loc.Z);
        // 与 ApplySpatialFromSnapshot 约定一致：rx=Roll, ry=Pitch, rz=Yaw。
        Json->SetNumberField(TEXT("rx"), Rot.Roll);
        Json->SetNumberField(TEXT("ry"), Rot.Pitch);
        Json->SetNumberField(TEXT("rz"), Rot.Yaw);
        Json->SetNumberField(TEXT("sx"), Scale.X);
        Json->SetNumberField(TEXT("sy"), Scale.Y);
        Json->SetNumberField(TEXT("sz"), Scale.Z);
        return Json;
    };
    auto UnsupportedComponentKind = [](const UActorComponent* Component)
    {
        if (!Component || !Component->GetClass()) return FString();
        const FString ClassName = Component->GetClass()->GetName();
        if (Component->IsA<USkeletalMeshComponent>()) return FString(TEXT("skeletal_mesh"));
        if (Component->IsA<UInstancedStaticMeshComponent>())
        {
            return ClassName.Contains(TEXT("Hierarchical"), ESearchCase::IgnoreCase)
                ? FString(TEXT("hierarchical_instanced_static_mesh"))
                : FString(TEXT("instanced_static_mesh"));
        }
        if (ClassName.Contains(TEXT("Spline"), ESearchCase::IgnoreCase))
            return FString(TEXT("spline"));
        if (ClassName.Contains(TEXT("ProceduralMesh"), ESearchCase::IgnoreCase))
            return FString(TEXT("procedural_mesh"));
        if (ClassName.Contains(TEXT("GeometryCollection"), ESearchCase::IgnoreCase))
            return FString(TEXT("geometry_collection"));
        if (ClassName.Contains(TEXT("Niagara"), ESearchCase::IgnoreCase))
            return FString(TEXT("niagara"));
        if (ClassName.Contains(TEXT("Decal"), ESearchCase::IgnoreCase))
            return FString(TEXT("decal"));
        if (ClassName.Contains(TEXT("ChildActor"), ESearchCase::IgnoreCase))
            return FString(TEXT("child_actor"));
        if (ClassName.Contains(TEXT("GeometryCache"), ESearchCase::IgnoreCase))
            return FString(TEXT("geometry_cache"));
        if (ClassName.Contains(TEXT("Groom"), ESearchCase::IgnoreCase))
            return FString(TEXT("groom"));
        if (ClassName.Contains(TEXT("ParticleSystem"), ESearchCase::IgnoreCase))
            return FString(TEXT("particle_system"));
        return FString();
    };

    TArray<TSharedPtr<FJsonValue>> ActorsJson;
    int32 TotalSourceActors = 0;
    int32 TotalRenderParts = 0;
    int32 TotalUnsupported = 0;
    for (AActor* RootActor : RootActors)
    {
        if (!RootActor) continue;

        TArray<AActor*> AssemblyActors;
        TSet<AActor*> VisitedActors;
        TFunction<void(AActor*)> CollectAttachedTree = [&](AActor* Current)
        {
            if (!Current || VisitedActors.Contains(Current)) return;
            VisitedActors.Add(Current);
            AssemblyActors.Add(Current);

            TArray<AActor*> DirectChildren;
            Current->GetAttachedActors(DirectChildren, /*bResetArray=*/true, /*bRecursivelyIncludeAttachedActors=*/false);
            DirectChildren.Sort([&ActorGuidString](const AActor& Lhs, const AActor& Rhs)
            {
                return ActorGuidString(&Lhs) < ActorGuidString(&Rhs);
            });
            for (AActor* Child : DirectChildren)
            {
                CollectAttachedTree(Child);
            }
        };
        CollectAttachedTree(RootActor);

        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        const FString RootGuid = ActorGuidString(RootActor);
        Obj->SetStringField(TEXT("ext_guid"), RootGuid);
        Obj->SetStringField(TEXT("name"), RootActor->GetActorLabel());
        Obj->SetStringField(TEXT("actor_label"), RootActor->GetActorLabel());
        Obj->SetStringField(TEXT("actor_name"), RootActor->GetName());
        Obj->SetStringField(TEXT("source_folder_path"), RootActor->GetFolderPath().ToString());
        if (UClass* ActorClass = RootActor->GetClass())
        {
            Obj->SetStringField(TEXT("actor_class"), ActorClass->GetName());
            Obj->SetStringField(TEXT("actor_class_path"), ActorClass->GetPathName());
            if (ActorClass->ClassGeneratedBy)
            {
                Obj->SetStringField(TEXT("blueprint_class_path"), ActorClass->ClassGeneratedBy->GetPathName());
            }
        }
        Obj->SetObjectField(TEXT("transform"), TransformToJson(RootActor->GetActorTransform()));

        TArray<TSharedPtr<FJsonValue>> SourceActorGuids;
        TArray<TSharedPtr<FJsonValue>> SourceActors;
        TArray<TSharedPtr<FJsonValue>> RenderParts;
        TArray<TSharedPtr<FJsonValue>> StaticMeshAssets;
        TArray<TSharedPtr<FJsonValue>> SkeletalMeshAssets;
        TArray<TSharedPtr<FJsonValue>> UnsupportedComponents;
        TArray<TSharedPtr<FJsonValue>> MigrationWarnings;
        TMap<FString, int32> ActorClassCounts;
        TMap<FString, int32> ComponentClassCounts;
        TArray<FString> SignatureParts;
        FString FirstMeshAsset;
        FString FirstSkeletalMeshAsset;

        const FTransform RootWorldTransform = RootActor->GetActorTransform();
        for (AActor* SourceActor : AssemblyActors)
        {
            if (!SourceActor) continue;
            const FString SourceGuid = ActorGuidString(SourceActor);
            SourceActorGuids.Add(MakeShared<FJsonValueString>(SourceGuid));

            TSharedPtr<FJsonObject> SourceActorJson = MakeShared<FJsonObject>();
            SourceActorJson->SetStringField(TEXT("guid"), SourceGuid);
            SourceActorJson->SetStringField(TEXT("actor_label"), SourceActor->GetActorLabel());
            SourceActorJson->SetStringField(TEXT("actor_name"), SourceActor->GetName());
            SourceActorJson->SetStringField(TEXT("folder_path"), SourceActor->GetFolderPath().ToString());
            SourceActorJson->SetStringField(TEXT("parent_guid"), ActorGuidString(SourceActor->GetAttachParentActor()));
            if (UClass* SourceClass = SourceActor->GetClass())
            {
                SourceActorJson->SetStringField(TEXT("actor_class"), SourceClass->GetName());
                SourceActorJson->SetStringField(TEXT("actor_class_path"), SourceClass->GetPathName());
                ActorClassCounts.FindOrAdd(SourceClass->GetPathName())++;
                if (SourceClass->ClassGeneratedBy)
                {
                    SourceActorJson->SetStringField(
                        TEXT("blueprint_class_path"), SourceClass->ClassGeneratedBy->GetPathName());
                }
            }
            SourceActors.Add(MakeShared<FJsonValueObject>(SourceActorJson));

            // Datasmith 大量使用负缩放做镜像。assembly_v1 会在部件相对 FTransform 中
            // 原样保留它；这里只把事实写入审计，不把正常镜像误判为禁止迁移。
            const FVector SourceActorScale = SourceActor->GetActorScale3D();
            SourceActorJson->SetBoolField(TEXT("has_mirrored_scale"),
                SourceActorScale.X < 0.0 || SourceActorScale.Y < 0.0 || SourceActorScale.Z < 0.0);

            TInlineComponentArray<UActorComponent*> Components(SourceActor);
            Components.Sort([](const UActorComponent& Lhs, const UActorComponent& Rhs)
            {
                return Lhs.GetName() < Rhs.GetName();
            });
            for (UActorComponent* Component : Components)
            {
                if (!Component || !Component->GetClass()) continue;
                const FString ComponentClassPath = Component->GetClass()->GetPathName();
                ComponentClassCounts.FindOrAdd(ComponentClassPath)++;

                const FString UnsupportedKind = UnsupportedComponentKind(Component);
                if (!UnsupportedKind.IsEmpty())
                {
                    TSharedPtr<FJsonObject> Unsupported = MakeShared<FJsonObject>();
                    Unsupported->SetStringField(TEXT("kind"), UnsupportedKind);
                    Unsupported->SetStringField(TEXT("source_actor_guid"), SourceGuid);
                    Unsupported->SetStringField(TEXT("source_actor_label"), SourceActor->GetActorLabel());
                    Unsupported->SetStringField(TEXT("component_name"), Component->GetName());
                    Unsupported->SetStringField(TEXT("component_class"), ComponentClassPath);
                    UnsupportedComponents.Add(MakeShared<FJsonValueObject>(Unsupported));

                    if (USkeletalMeshComponent* Skeletal = Cast<USkeletalMeshComponent>(Component))
                    {
                        if (USkeletalMesh* SkeletalAsset = Skeletal->GetSkeletalMeshAsset())
                        {
                            const FString AssetPath = SkeletalAsset->GetPathName();
                            if (FirstSkeletalMeshAsset.IsEmpty()) FirstSkeletalMeshAsset = AssetPath;
                            SkeletalMeshAssets.Add(MakeShared<FJsonValueString>(AssetPath));
                        }
                    }
                    continue;
                }

                UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component);
                if (!StaticMeshComponent || !StaticMeshComponent->GetStaticMesh()) continue;

                const FString AssetPath = StaticMeshComponent->GetStaticMesh()->GetPathName();
                const FTransform RelativeTransform =
                    StaticMeshComponent->GetComponentTransform().GetRelativeTransform(RootWorldTransform);
                if (FirstMeshAsset.IsEmpty()) FirstMeshAsset = AssetPath;
                StaticMeshAssets.Add(MakeShared<FJsonValueString>(AssetPath));

                TSharedPtr<FJsonObject> Part = MakeShared<FJsonObject>();
                Part->SetStringField(TEXT("asset_path"), AssetPath);
                Part->SetStringField(TEXT("source_actor_guid"), SourceGuid);
                Part->SetStringField(TEXT("source_actor_label"), SourceActor->GetActorLabel());
                Part->SetStringField(TEXT("source_component_name"), StaticMeshComponent->GetName());
                Part->SetStringField(TEXT("source_component_class"), ComponentClassPath);
                Part->SetObjectField(TEXT("relative_transform"), TransformToJson(RelativeTransform));

                TArray<TSharedPtr<FJsonValue>> MaterialPaths;
                TArray<FString> MaterialSignaturePaths;
                const int32 MaterialSlotCount = StaticMeshComponent->GetNumMaterials();
                MaterialPaths.Reserve(MaterialSlotCount);
                MaterialSignaturePaths.Reserve(MaterialSlotCount);
                for (int32 MaterialIndex = 0; MaterialIndex < MaterialSlotCount; ++MaterialIndex)
                {
                    UMaterialInterface* EffectiveMaterial = StaticMeshComponent->GetMaterial(MaterialIndex);
                    const FString MaterialPath = EffectiveMaterial
                        ? EffectiveMaterial->GetPathName()
                        : FString();
                    MaterialPaths.Add(MakeShared<FJsonValueString>(MaterialPath));
                    MaterialSignaturePaths.Add(MaterialPath);
                }
                // visible 与 hidden_in_game 分别保存原始标志，避免 IsVisible() 的上下文合成结果丢信息。
                const bool bPartVisible = StaticMeshComponent->GetVisibleFlag();
                const bool bPartHiddenInGame = StaticMeshComponent->bHiddenInGame;
                const bool bPartCastShadow = StaticMeshComponent->CastShadow;
                const FString CollisionEnabled = CollisionEnabledToMigrationString(
                    StaticMeshComponent->GetCollisionEnabled());
                Part->SetArrayField(TEXT("material_paths"), MaterialPaths);
                Part->SetBoolField(TEXT("visible"), bPartVisible);
                Part->SetBoolField(TEXT("hidden_in_game"), bPartHiddenInGame);
                Part->SetBoolField(TEXT("cast_shadow"), bPartCastShadow);
                Part->SetStringField(TEXT("collision_enabled"), CollisionEnabled);
                RenderParts.Add(MakeShared<FJsonValueObject>(Part));

                const FVector RelativeLocation = RelativeTransform.GetLocation();
                const FRotator RelativeRotation = RelativeTransform.Rotator();
                const FVector RelativeScale = RelativeTransform.GetScale3D();
                SignatureParts.Add(FString::Printf(
                    TEXT("%s|%.3f,%.3f,%.3f|%.3f,%.3f,%.3f|%.5f,%.5f,%.5f|%s|%d|%d|%d|%s"),
                    *AssetPath,
                    RelativeLocation.X, RelativeLocation.Y, RelativeLocation.Z,
                    RelativeRotation.Roll, RelativeRotation.Pitch, RelativeRotation.Yaw,
                    RelativeScale.X, RelativeScale.Y, RelativeScale.Z,
                    *FString::Join(MaterialSignaturePaths, TEXT(",")),
                    bPartVisible ? 1 : 0,
                    bPartHiddenInGame ? 1 : 0,
                    bPartCastShadow ? 1 : 0,
                    *CollisionEnabled));
            }
        }

        if (RenderParts.Num() == 0)
        {
            MigrationWarnings.Add(MakeShared<FJsonValueString>(TEXT("no_supported_static_mesh_parts")));
        }
        if (UnsupportedComponents.Num() > 0)
        {
            MigrationWarnings.Add(MakeShared<FJsonValueString>(TEXT("contains_unsupported_components")));
        }

        SignatureParts.Sort();
        const FString SignatureSource = FString::Join(SignatureParts, TEXT("\n"));
        Obj->SetStringField(TEXT("assembly_signature"), FMD5::HashAnsiString(*SignatureSource));
        Obj->SetArrayField(TEXT("source_actor_guids"), SourceActorGuids);
        Obj->SetArrayField(TEXT("source_actors"), SourceActors);
        Obj->SetArrayField(TEXT("render_parts"), RenderParts);
        Obj->SetArrayField(TEXT("unsupported_components"), UnsupportedComponents);
        Obj->SetArrayField(TEXT("migration_warnings"), MigrationWarnings);

        // 旧版字段保留，便于旧工具读取与分类脚本逐步升级。
        Obj->SetStringField(TEXT("mesh_asset"), FirstMeshAsset);
        Obj->SetStringField(TEXT("static_mesh_asset"), FirstMeshAsset);
        Obj->SetArrayField(TEXT("static_mesh_assets"), StaticMeshAssets);
        Obj->SetStringField(TEXT("skeletal_mesh_asset"), FirstSkeletalMeshAsset);
        Obj->SetArrayField(TEXT("skeletal_mesh_assets"), SkeletalMeshAssets);

        TSharedPtr<FJsonObject> ActorClassesJson = MakeShared<FJsonObject>();
        for (const TPair<FString, int32>& Pair : ActorClassCounts)
        {
            ActorClassesJson->SetNumberField(Pair.Key, Pair.Value);
        }
        TSharedPtr<FJsonObject> ComponentClassesJson = MakeShared<FJsonObject>();
        for (const TPair<FString, int32>& Pair : ComponentClassCounts)
        {
            ComponentClassesJson->SetNumberField(Pair.Key, Pair.Value);
        }
        TSharedPtr<FJsonObject> ComponentAudit = MakeShared<FJsonObject>();
        ComponentAudit->SetNumberField(TEXT("source_actor_count"), AssemblyActors.Num());
        ComponentAudit->SetNumberField(TEXT("descendant_actor_count"), FMath::Max(0, AssemblyActors.Num() - 1));
        ComponentAudit->SetNumberField(TEXT("render_part_count"), RenderParts.Num());
        ComponentAudit->SetNumberField(TEXT("unsupported_component_count"), UnsupportedComponents.Num());
        // 兼容旧版审计字段；ISM/HISM 不会计入 static_mesh_components。
        ComponentAudit->SetNumberField(TEXT("static_mesh_components"), RenderParts.Num());
        ComponentAudit->SetNumberField(TEXT("skeletal_mesh_components"), SkeletalMeshAssets.Num());
        ComponentAudit->SetObjectField(TEXT("actor_classes"), ActorClassesJson);
        ComponentAudit->SetObjectField(TEXT("component_classes"), ComponentClassesJson);
        Obj->SetObjectField(TEXT("component_audit"), ComponentAudit);
        Obj->SetObjectField(TEXT("component_summary"), ComponentAudit);

        ActorsJson.Add(MakeShared<FJsonValueObject>(Obj));
        TotalSourceActors += AssemblyActors.Num();
        TotalRenderParts += RenderParts.Num();
        TotalUnsupported += UnsupportedComponents.Num();
    }

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("schema_version"), TEXT("assembly_v1"));
    Root->SetStringField(TEXT("ue_project_id"), UEProjectId);
    Root->SetStringField(TEXT("ue_project_name"), UEProjectName);
    // project_id 留空→后端用当前激活项目；zone_id 用本 Manager 的 SceneId（=分区）
    if (!SceneId.IsEmpty())
    {
        Root->SetStringField(TEXT("zone_id"), SceneId);
    }
    Root->SetArrayField(TEXT("actors"), ActorsJson);

    FString OutStr;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutStr);
    FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

    const FString Path = MigrationExportPath();
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree=*/true);
    // 后端工具按 UTF-8 读取；Windows 默认编码会把含中文标签的 FString 写成 UTF-16LE。
    const bool bOk = FFileHelper::SaveStringToFile(
        OutStr,
        *Path,
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

    UE_LOG(LogTemp, Log,
        TEXT("[迁移] assembly_v1 导出 %d 个母 Actor / %d 个源 Actor / %d 个渲染部件 / %d 个不支持组件 → %s (%s)"),
        RootActors.Num(), TotalSourceActors, TotalRenderParts, TotalUnsupported,
        *Path, bOk ? TEXT("成功") : TEXT("写入失败"));
    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 8.0f,
            bOk ? FColor::Green : FColor::Red,
            RootActors.Num() > 0
                ? FString::Printf(
                    TEXT("导出 %d 个母 Actor（%d 个部件，%d 个不支持组件）→ %s\n把它交给后端 migrate_ue_actors.py"),
                    RootActors.Num(), TotalRenderParts, TotalUnsupported, *Path)
                : FString::Printf(
                    TEXT("文件夹「%s」及其子文件夹下没有 actor\n请先框选历史 actor，右键→移动到文件夹→填「%s」"),
                    *MigrationFolderName, *MigrationFolderName));
    }
#else
    UE_LOG(LogTemp, Warning, TEXT("[迁移] 导出仅在编辑器模式下可用"));
#endif
}

void ATwinSceneManager::RemoveMigratedActors()
{
#if WITH_EDITOR
    const FString Path = MigrationResultPath();
    FString Content;
    if (!FFileHelper::LoadFileToString(Content, *Path))
    {
        UE_LOG(LogTemp, Error, TEXT("[迁移] 读不到结果文件: %s"), *Path);
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 8.0f, FColor::Red,
                FString::Printf(TEXT("读不到 %s\n请先跑后端迁移脚本产出结果"), *Path));
        }
        return;
    }

    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("[迁移] 结果 JSON 解析失败"));
        return;
    }

    TSet<FString> MigratedGuids;
    FString ResultFormat = TEXT("legacy_flat_mapping");

    // assembly_v1 的 canonical 删除清单最精确：它包含母 Actor 与全部已收编后代。
    const TArray<TSharedPtr<FJsonValue>>* DeleteActorGuids = nullptr;
    if (Root->TryGetArrayField(TEXT("delete_actor_guids"), DeleteActorGuids) && DeleteActorGuids)
    {
        ResultFormat = TEXT("assembly_v1.delete_actor_guids");
        for (const TSharedPtr<FJsonValue>& Value : *DeleteActorGuids)
        {
            FString Guid;
            if (Value.IsValid() && Value->TryGetString(Guid) && !Guid.IsEmpty())
            {
                MigratedGuids.Add(Guid);
            }
        }
    }
    else
    {
        // 新结果缺删除清单时只删除 canonical instances 的 key（母 Actor），不误读保留字段。
        const TSharedPtr<FJsonObject>* Instances = nullptr;
        if (Root->TryGetObjectField(TEXT("instances"), Instances) && Instances && Instances->IsValid())
        {
            ResultFormat = TEXT("assembly_v1.instances_fallback");
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Instances)->Values)
            {
                FGuid ParsedGuid;
                if (FGuid::Parse(Pair.Key, ParsedGuid))
                {
                    MigratedGuids.Add(Pair.Key);
                }
            }
        }
        else
        {
            // 兼容旧版根对象 {ext_guid: instance_id}。
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Root->Values)
            {
                FGuid ParsedGuid;
                FString InstanceId;
                if (FGuid::Parse(Pair.Key, ParsedGuid)
                    && Pair.Value.IsValid()
                    && Pair.Value->TryGetString(InstanceId))
                {
                    MigratedGuids.Add(Pair.Key);
                }
            }
        }
    }

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : GetWorld();
    if (!World) return;

    TArray<AActor*> ToDelete;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* A = *It;
        if (!A || A == this) continue;
        if (A->IsA(ATwinInstance::StaticClass())) continue;
        const FString Guid = A->GetActorGuid().ToString(EGuidFormats::DigitsWithHyphens);
        if (MigratedGuids.Contains(Guid))
        {
            ToDelete.Add(A);
        }
    }

    int32 Deleted = 0;
    for (AActor* A : ToDelete)
    {
        if (A && IsValid(A) && !A->IsActorBeingDestroyed()
            && World->EditorDestroyActor(A, /*bShouldModifyLevel=*/true))
        {
            Deleted++;
        }
    }

    UE_LOG(LogTemp, Log,
        TEXT("[迁移] 按 %s 已删除 %d 个已收编的原 actor（结果含 %d GUID，共匹配 %d）"),
        *ResultFormat, Deleted, MigratedGuids.Num(), ToDelete.Num());
    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 8.0f, FColor::Green,
            FString::Printf(TEXT("已清除 %d 个已迁移原 actor\n记得保存关卡(Ctrl+S)"), Deleted));
    }
#else
    UE_LOG(LogTemp, Warning, TEXT("[迁移] 清除仅在编辑器模式下可用"));
#endif
}

// ═══════════════════════════════════════════════════════════════════════════
// FR-4 编辑器预览（从数据库临时 spawn 供查看/微调，绝不写入 .umap）
// ═══════════════════════════════════════════════════════════════════════════

void ATwinSceneManager::PullPreviewFromDB()
{
#if WITH_EDITOR
    ClearPreview();  // 先清掉上一批，避免重叠

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest =
        FHttpModule::Get().CreateRequest();
    HttpRequest->SetURL(BuildSnapshotsUrl());
    HttpRequest->SetVerb(TEXT("GET"));
    HttpRequest->SetHeader(TEXT("Accept"), TEXT("application/json"));
    AddUEProjectHeaders(HttpRequest);
    HttpRequest->OnProcessRequestComplete().BindUObject(
        this, &ATwinSceneManager::OnPreviewResponse);
    HttpRequest->ProcessRequest();

    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Cyan,
            TEXT("正在从数据库拉取预览..."));
    }
#else
    UE_LOG(LogTemp, Warning, TEXT("[孪生管理器] 预览仅在编辑器模式下可用"));
#endif
}

void ATwinSceneManager::PreviewMigratedActorsFromSnapshotFile()
{
#if WITH_EDITOR
    ClearPreview();
    IFileManager::Get().Delete(*MigrationPreviewAuditPath(), false, true, true);
    const FString SnapshotPath = MigrationPreviewSnapshotPath();
    FString JsonPayload;
    if (!FFileHelper::LoadFileToString(JsonPayload, *SnapshotPath))
    {
        UE_LOG(LogTemp, Error, TEXT("[迁移预览] 无法读取快照文件: %s"), *SnapshotPath);
        return;
    }

    const int32 Count = SpawnPreviewActorsFromJson(JsonPayload, TEXT("migration_snapshot_file"));
    UE_LOG(LogTemp, Log,
        TEXT("[迁移预览] 从 %s 生成 %d 个 transient Actor；关卡未保存"),
        *SnapshotPath, Count);
#else
    UE_LOG(LogTemp, Warning, TEXT("[迁移预览] 仅在编辑器模式下可用"));
#endif
}

void ATwinSceneManager::ClearPreview()
{
#if WITH_EDITOR
    int32 Cleared = 0;
    for (ATwinInstance* A : PreviewActors)
    {
        if (A && IsValid(A))
        {
            A->Destroy();
            Cleared++;
        }
    }
    PreviewActors.Empty();
    PreviewBaseline.Empty();
    PreviewMaterialBaseline.Empty();
    UE_LOG(LogTemp, Log, TEXT("[孪生管理器] 已清除 %d 个预览 Actor"), Cleared);
#endif
}

bool ATwinSceneManager::CapturePreviewMaterialBaseline(
    ATwinInstance* Instance,
    const TSharedPtr<FJsonObject>& Snapshot,
    FString* OutError)
{
    auto Fail = [OutError](const FString& Message)
    {
        if (OutError)
        {
            *OutError = Message;
        }
        return false;
    };

    if (!Instance || !IsValid(Instance) || !Snapshot.IsValid())
    {
        return Fail(TEXT("预览实例或快照无效"));
    }

    const TSharedPtr<FJsonObject>* Interfaces = nullptr;
    const TSharedPtr<FJsonObject>* Representable = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* RenderParts = nullptr;
    if (!Snapshot->TryGetObjectField(TEXT("interfaces"), Interfaces)
        || !Interfaces || !Interfaces->IsValid()
        || !(*Interfaces)->TryGetObjectField(TEXT("I3D_Representable"), Representable)
        || !Representable || !Representable->IsValid()
        || !(*Representable)->TryGetArrayField(TEXT("render_parts"), RenderParts)
        || !RenderParts || RenderParts->Num() == 0)
    {
        return Fail(TEXT("快照不含 assembly_v1 render_parts"));
    }

    FString AssemblySignature;
    (*Representable)->TryGetStringField(TEXT("assembly_signature"), AssemblySignature);
    if (AssemblySignature.IsEmpty()
        || Instance->GetCurrentAssemblySignature() != AssemblySignature
        || Instance->GetRenderPartComponentCount() != RenderParts->Num())
    {
        return Fail(TEXT("装配未完整加载，不能建立材质回写基线"));
    }

    FPreviewMaterialInstanceBaseline InstanceBaseline;
    InstanceBaseline.AssemblySignature = AssemblySignature;
    InstanceBaseline.Parts.Reserve(RenderParts->Num());

    for (int32 PartIndex = 0; PartIndex < RenderParts->Num(); ++PartIndex)
    {
        const TSharedPtr<FJsonObject>* PartObject = nullptr;
        if (!(*RenderParts)[PartIndex].IsValid()
            || !(*RenderParts)[PartIndex]->TryGetObject(PartObject)
            || !PartObject || !PartObject->IsValid())
        {
            return Fail(FString::Printf(TEXT("render_parts[%d] 不是对象"), PartIndex));
        }

        FPreviewMaterialPartBaseline PartBaseline;
        PartBaseline.PartIndex = PartIndex;
        (*PartObject)->TryGetStringField(TEXT("asset_path"), PartBaseline.AssetPath);
        (*PartObject)->TryGetStringField(
            TEXT("source_actor_guid"), PartBaseline.SourceActorGuid);
        (*PartObject)->TryGetStringField(
            TEXT("source_component_name"), PartBaseline.SourceComponentName);
        if (PartBaseline.AssetPath.IsEmpty())
        {
            return Fail(FString::Printf(TEXT("render_parts[%d] 缺少 asset_path"), PartIndex));
        }

        const TArray<TSharedPtr<FJsonValue>>* MaterialValues = nullptr;
        if (!(*PartObject)->TryGetArrayField(TEXT("material_paths"), MaterialValues)
            || !MaterialValues)
        {
            return Fail(FString::Printf(TEXT("render_parts[%d] 缺少 material_paths"), PartIndex));
        }
        for (const TSharedPtr<FJsonValue>& MaterialValue : *MaterialValues)
        {
            FString MaterialPath;
            if (!MaterialValue.IsValid() || !MaterialValue->TryGetString(MaterialPath))
            {
                return Fail(FString::Printf(
                    TEXT("render_parts[%d] 的 material_paths 含非字符串"), PartIndex));
            }
            PartBaseline.DatabaseMaterialPaths.Add(MaterialPath);
        }

        if (!Instance->GetRenderPartMaterialPaths(
                PartIndex, PartBaseline.ComponentMaterialPaths)
            || !Instance->GetSavedRenderPartMaterialPaths(
                PartIndex, PartBaseline.SavedMeshMaterialPaths))
        {
            return Fail(FString::Printf(TEXT("无法读取 render_parts[%d] 材质槽"), PartIndex));
        }
        InstanceBaseline.Parts.Add(MoveTemp(PartBaseline));
    }

    PreviewMaterialBaseline.Add(Instance->GetInstanceId(), MoveTemp(InstanceBaseline));
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// FR-5 空间回写：预览 Actor 手动调整后提交回后端真源
// ═══════════════════════════════════════════════════════════════════════════

void ATwinSceneManager::CommitPreviewChanges()
{
#if WITH_EDITOR
    // ── 1. diff：找出被手动挪动过的预览 Actor ────────────────────────────
    struct FChange { FString Id; FTransform Cur; };
    TArray<FChange> Changes;

    for (ATwinInstance* A : PreviewActors)
    {
        if (!A || !IsValid(A)) continue;
        const FString Id = A->GetInstanceId();
        const FTransform* Base = PreviewBaseline.Find(Id);
        if (!Base) continue;

        const FTransform Cur = A->GetActorTransform();
        if (Cur.Equals(*Base, 0.01f)) continue;   // 没动过（容差 0.01）

        Changes.Add({Id, Cur});
    }

    if (Changes.Num() == 0)
    {
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Yellow,
                TEXT("没有检测到位置变更（先拉取预览、挪动后再提交）"));
        }
        return;
    }

    // ── 2. 逐条 POST /api/v2/state/writeback ────────────────────────────
    PendingWritebacks = Changes.Num();
    SucceededWritebacks = 0;

    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Cyan,
            FString::Printf(TEXT("正在提交 %d 处空间变更..."), Changes.Num()));
    }

    for (const FChange& C : Changes)
    {
        TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
        Body->SetStringField(TEXT("instance_id"), C.Id);

        const FVector L = C.Cur.GetLocation();
        const FRotator R = C.Cur.Rotator();
        const FVector S = C.Cur.GetScale3D();
        TSharedPtr<FJsonObject> TF = MakeShared<FJsonObject>();
        TF->SetNumberField(TEXT("tx"), L.X);
        TF->SetNumberField(TEXT("ty"), L.Y);
        TF->SetNumberField(TEXT("tz"), L.Z);
        // 与后端 writeback.py / ApplySpatialFromSnapshot 约定一致：rx=Roll, ry=Pitch, rz=Yaw
        TF->SetNumberField(TEXT("rx"), R.Roll);
        TF->SetNumberField(TEXT("ry"), R.Pitch);
        TF->SetNumberField(TEXT("rz"), R.Yaw);
        TF->SetNumberField(TEXT("sx"), S.X);
        TF->SetNumberField(TEXT("sy"), S.Y);
        TF->SetNumberField(TEXT("sz"), S.Z);
        Body->SetObjectField(TEXT("transform"), TF);

        FString BodyStr;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyStr);
        FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);

        TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest =
            FHttpModule::Get().CreateRequest();
        HttpRequest->SetURL(FString::Printf(TEXT("%s/api/v2/state/writeback"), *BackendBaseUrl));
        HttpRequest->SetVerb(TEXT("POST"));
        HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
        AddUEProjectHeaders(HttpRequest);
        HttpRequest->SetContentAsString(BodyStr);

        TWeakObjectPtr<ATwinSceneManager> WeakThis(this);
        const FString Id = C.Id;
        const FTransform Committed = C.Cur;
        HttpRequest->OnProcessRequestComplete().BindLambda(
            [WeakThis, Id, Committed](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bOk)
            {
                ATwinSceneManager* Self = WeakThis.Get();
                if (!Self) return;
                Self->PendingWritebacks--;

                const bool bSuccess = bOk && Resp.IsValid() && Resp->GetResponseCode() == 200;
                if (bSuccess)
                {
                    Self->SucceededWritebacks++;
                    // 基线推进到已提交值：重复点提交不会再发同一条
                    Self->PreviewBaseline.Add(Id, Committed);
                    UE_LOG(LogTemp, Log, TEXT("[回写] %s 提交成功"), *Id);
                }
                else
                {
                    UE_LOG(LogTemp, Error, TEXT("[回写] %s 提交失败 (code=%d)"),
                           *Id, Resp.IsValid() ? Resp->GetResponseCode() : -1);
                    if (GEngine)
                    {
                        GEngine->AddOnScreenDebugMessage(-1, 8.0f, FColor::Red,
                            FString::Printf(TEXT("回写失败: %s（详见 Output Log）"), *Id));
                    }
                }

                // 全部返回后给一条汇总
                if (Self->PendingWritebacks <= 0 && GEngine)
                {
                    GEngine->AddOnScreenDebugMessage(-1, 8.0f,
                        Self->SucceededWritebacks > 0 ? FColor::Green : FColor::Red,
                        FString::Printf(TEXT("回写完成：成功 %d 处"), Self->SucceededWritebacks));
                }
            });

        HttpRequest->ProcessRequest();
    }
#else
    UE_LOG(LogTemp, Warning, TEXT("[孪生管理器] 回写提交仅在编辑器模式下可用"));
#endif
}

void ATwinSceneManager::CommitPreviewMaterialChanges()
{
#if WITH_EDITOR
    if (bMaterialWritebackInFlight)
    {
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(
                -1, 5.0f, FColor::Yellow, TEXT("材质回写正在进行，请等待本次请求完成"));
        }
        return;
    }

    TArray<TSharedPtr<FJsonValue>> InstanceChanges;
    int32 ChangedPartCount = 0;
    FString ValidationError;

    auto PathsToJson = [](const TArray<FString>& Paths)
    {
        TArray<TSharedPtr<FJsonValue>> Values;
        Values.Reserve(Paths.Num());
        for (const FString& Path : Paths)
        {
            Values.Add(MakeShared<FJsonValueString>(Path));
        }
        return Values;
    };

    for (ATwinInstance* Instance : PreviewActors)
    {
        if (!Instance || !IsValid(Instance) || !Instance->IsAssemblyRenderActive())
        {
            continue;
        }

        const FString InstanceId = Instance->GetInstanceId();
        const FPreviewMaterialInstanceBaseline* InstanceBaseline =
            PreviewMaterialBaseline.Find(InstanceId);
        if (!InstanceBaseline)
        {
            ValidationError = FString::Printf(
                TEXT("%s 没有材质基线，请重新“从数据库拉取预览”"), *InstanceId);
            break;
        }
        if (Instance->GetCurrentAssemblySignature()
                != InstanceBaseline->AssemblySignature
            || Instance->GetRenderPartComponentCount()
                != InstanceBaseline->Parts.Num())
        {
            ValidationError = FString::Printf(
                TEXT("%s 的装配已变化，请重新拉取预览后再提交"), *InstanceId);
            break;
        }

        TArray<TSharedPtr<FJsonValue>> PartChanges;
        for (const FPreviewMaterialPartBaseline& PartBaseline : InstanceBaseline->Parts)
        {
            TArray<FString> CurrentComponentPaths;
            TArray<FString> CurrentSavedMeshPaths;
            if (!Instance->GetRenderPartMaterialPaths(
                    PartBaseline.PartIndex, CurrentComponentPaths)
                || !Instance->GetSavedRenderPartMaterialPaths(
                    PartBaseline.PartIndex, CurrentSavedMeshPaths))
            {
                ValidationError = FString::Printf(
                    TEXT("%s 零件 %d 的材质槽读取失败"),
                    *InstanceId, PartBaseline.PartIndex);
                break;
            }

            const bool bComponentChanged =
                CurrentComponentPaths != PartBaseline.ComponentMaterialPaths;
            const bool bSavedMeshChanged =
                CurrentSavedMeshPaths != PartBaseline.SavedMeshMaterialPaths;
            if (!bComponentChanged && !bSavedMeshChanged)
            {
                continue;
            }

            if (bComponentChanged && bSavedMeshChanged
                && CurrentComponentPaths != CurrentSavedMeshPaths)
            {
                ValidationError = FString::Printf(
                    TEXT("%s 零件 %d：组件覆写与 StaticMesh 默认槽改成了不同材质，请统一后再提交"),
                    *InstanceId, PartBaseline.PartIndex);
                break;
            }

            const TArray<FString>& DesiredPaths =
                bComponentChanged ? CurrentComponentPaths : CurrentSavedMeshPaths;
            if (DesiredPaths == PartBaseline.DatabaseMaterialPaths)
            {
                continue;
            }
            if (DesiredPaths.Num() == 0)
            {
                ValidationError = FString::Printf(
                    TEXT("%s 零件 %d 的材质槽为空，拒绝回写"),
                    *InstanceId, PartBaseline.PartIndex);
                break;
            }

            TSharedPtr<FJsonObject> PartChange = MakeShared<FJsonObject>();
            PartChange->SetNumberField(TEXT("part_index"), PartBaseline.PartIndex);
            PartChange->SetStringField(TEXT("asset_path"), PartBaseline.AssetPath);
            PartChange->SetStringField(
                TEXT("source_actor_guid"), PartBaseline.SourceActorGuid);
            PartChange->SetStringField(
                TEXT("source_component_name"), PartBaseline.SourceComponentName);
            PartChange->SetArrayField(
                TEXT("expected_material_paths"),
                PathsToJson(PartBaseline.DatabaseMaterialPaths));
            PartChange->SetArrayField(
                TEXT("material_paths"), PathsToJson(DesiredPaths));
            PartChanges.Add(MakeShared<FJsonValueObject>(PartChange));
            ++ChangedPartCount;
        }

        if (!ValidationError.IsEmpty())
        {
            break;
        }
        if (PartChanges.Num() > 0)
        {
            TSharedPtr<FJsonObject> InstanceChange = MakeShared<FJsonObject>();
            InstanceChange->SetStringField(TEXT("instance_id"), InstanceId);
            InstanceChange->SetStringField(
                TEXT("expected_assembly_signature"),
                InstanceBaseline->AssemblySignature);
            InstanceChange->SetArrayField(TEXT("parts"), PartChanges);
            InstanceChanges.Add(MakeShared<FJsonValueObject>(InstanceChange));
        }
    }

    if (!ValidationError.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("[材质回写] %s"), *ValidationError);
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Red, ValidationError);
        }
        return;
    }
    if (InstanceChanges.Num() == 0)
    {
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(
                -1, 6.0f, FColor::Yellow,
                TEXT("没有检测到材质路径变更（请先拉取预览，再修改并保存材质槽）"));
        }
        return;
    }

    TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
    Body->SetArrayField(TEXT("changes"), InstanceChanges);
    FString BodyString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyString);
    FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest =
        FHttpModule::Get().CreateRequest();
    HttpRequest->SetURL(FString::Printf(
        TEXT("%s/api/v2/state/material-writeback"), *BackendBaseUrl));
    HttpRequest->SetVerb(TEXT("POST"));
    HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    AddUEProjectHeaders(HttpRequest);
    HttpRequest->SetTimeout(300.0f);
    HttpRequest->SetActivityTimeout(180.0f);
    HttpRequest->SetContentAsString(BodyString);

    bMaterialWritebackInFlight = true;
    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(
            -1, 6.0f, FColor::Cyan,
            FString::Printf(
                TEXT("正在原子提交 %d 个实例、%d 个零件的材质变更..."),
                InstanceChanges.Num(), ChangedPartCount));
    }

    TWeakObjectPtr<ATwinSceneManager> WeakThis(this);
    HttpRequest->OnProcessRequestComplete().BindLambda(
        [WeakThis, ChangedPartCount](
            FHttpRequestPtr Request,
            FHttpResponsePtr Response,
            bool bWasSuccessful)
        {
            ATwinSceneManager* Self = WeakThis.Get();
            if (!Self)
            {
                return;
            }
            Self->bMaterialWritebackInFlight = false;

            const int32 ResponseCode = Response.IsValid()
                ? Response->GetResponseCode() : -1;
            const FString ResponseBody = Response.IsValid()
                ? Response->GetContentAsString() : FString();
            if (!bWasSuccessful || ResponseCode != 200)
            {
                UE_LOG(LogTemp, Error,
                    TEXT("[材质回写] 请求失败 code=%d body=%s"),
                    ResponseCode, *ResponseBody);
                if (GEngine)
                {
                    GEngine->AddOnScreenDebugMessage(
                        -1, 12.0f, FColor::Red,
                        FString::Printf(
                            TEXT("材质回写失败（HTTP %d）；数据库未发生部分写入，详见 Output Log"),
                            ResponseCode));
                }
                return;
            }

            TSharedPtr<FJsonObject> RootObject;
            TSharedRef<TJsonReader<>> Reader =
                TJsonReaderFactory<>::Create(ResponseBody);
            const TArray<TSharedPtr<FJsonValue>>* Snapshots = nullptr;
            if (!FJsonSerializer::Deserialize(Reader, RootObject)
                || !RootObject.IsValid()
                || !RootObject->TryGetArrayField(TEXT("snapshots"), Snapshots)
                || !Snapshots)
            {
                UE_LOG(LogTemp, Error,
                    TEXT("[材质回写] 数据库已提交，但响应快照解析失败: %s"),
                    *ResponseBody);
                if (GEngine)
                {
                    GEngine->AddOnScreenDebugMessage(
                        -1, 12.0f, FColor::Yellow,
                        TEXT("材质已回写，但本地基线刷新失败；请重新从数据库拉取预览"));
                }
                return;
            }

            int32 RefreshedInstances = 0;
            for (const TSharedPtr<FJsonValue>& SnapshotValue : *Snapshots)
            {
                const TSharedPtr<FJsonObject>* SnapshotObject = nullptr;
                if (!SnapshotValue.IsValid()
                    || !SnapshotValue->TryGetObject(SnapshotObject)
                    || !SnapshotObject || !SnapshotObject->IsValid())
                {
                    continue;
                }
                FString InstanceId;
                if (!(*SnapshotObject)->TryGetStringField(TEXT("instanceId"), InstanceId))
                {
                    continue;
                }
                ATwinInstance* PreviewInstance = nullptr;
                for (ATwinInstance* Candidate : Self->PreviewActors)
                {
                    if (Candidate && IsValid(Candidate)
                        && Candidate->GetInstanceId() == InstanceId)
                    {
                        PreviewInstance = Candidate;
                        break;
                    }
                }
                if (!PreviewInstance)
                {
                    continue;
                }
                PreviewInstance->ApplySnapshot(*SnapshotObject);
                FString BaselineError;
                if (Self->CapturePreviewMaterialBaseline(
                        PreviewInstance, *SnapshotObject, &BaselineError))
                {
                    ++RefreshedInstances;
                }
                else
                {
                    UE_LOG(LogTemp, Warning,
                        TEXT("[材质回写] %s 新基线捕获失败: %s"),
                        *InstanceId, *BaselineError);
                }
            }

            UE_LOG(LogTemp, Log,
                TEXT("[材质回写] 成功提交 %d 个零件，刷新 %d 个实例"),
                ChangedPartCount, RefreshedInstances);
            if (GEngine)
            {
                GEngine->AddOnScreenDebugMessage(
                    -1, 10.0f, FColor::Green,
                    FString::Printf(
                        TEXT("材质回写完成：成功 %d 个零件；PIE/EXE 后续快照以数据库新材质为准"),
                        ChangedPartCount));
            }
        });

    if (!HttpRequest->ProcessRequest())
    {
        bMaterialWritebackInFlight = false;
        UE_LOG(LogTemp, Error, TEXT("[材质回写] HTTP 请求未能启动"));
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(
                -1, 8.0f, FColor::Red, TEXT("材质回写请求未能启动"));
        }
    }
#else
    UE_LOG(LogTemp, Warning, TEXT("[孪生管理器] 材质回写仅在编辑器模式下可用"));
#endif
}

void ATwinSceneManager::OnPreviewResponse(
    FHttpRequestPtr HttpRequest,
    FHttpResponsePtr Response,
    bool bWasSuccessful)
{
#if WITH_EDITOR
    if (!bWasSuccessful || !Response.IsValid() || Response->GetResponseCode() != 200)
    {
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Red,
                TEXT("预览拉取失败，请确认后端在运行"));
        }
        return;
    }

    const int32 Count = SpawnPreviewActorsFromJson(
        Response->GetContentAsString(), TEXT("backend_http"));
    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Green,
            FString::Printf(TEXT("预览已生成 %d 个（transient，不会存进关卡）"), Count));
    }
#endif
}

int32 ATwinSceneManager::SpawnPreviewActorsFromJson(
    const FString& JsonPayload,
    const FString& SourceLabel)
{
#if WITH_EDITOR
    const FString AuditPath = MigrationPreviewAuditPath();
    IFileManager::Get().Delete(*AuditPath, false, true, true);
    TArray<TSharedPtr<FJsonValue>> Arr;
    TSharedRef<TJsonReader<>> Reader =
        TJsonReaderFactory<>::Create(JsonPayload);
    if (!FJsonSerializer::Deserialize(Reader, Arr))
    {
        UE_LOG(LogTemp, Error, TEXT("[孪生管理器] 预览 JSON 解析失败"));
        return 0;
    }

    UWorld* World = GetWorld();
    if (!World && GEditor)
    {
        World = GEditor->GetEditorWorldContext().World();
    }
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("[孪生管理器] 预览失败：编辑器世界不可用"));
        return 0;
    }

    UClass* SpawnClass = InstanceClass ? InstanceClass.Get() : ATwinInstance::StaticClass();
    int32 Count = 0;
    int32 ExpectedRenderParts = 0;
    int32 LoadedRenderParts = 0;
    int32 AssemblyInstances = 0;
    int32 CompleteAssemblies = 0;
    int32 PartStateValidAssemblies = 0;
    int32 SpatialSnapshots = 0;
    int32 SpatialSchemaErrors = 0;
    int32 MalformedSnapshots = 0;
    int32 TransientActors = 0;
    TArray<TSharedPtr<FJsonValue>> IncompleteAssemblies;
    TArray<TSharedPtr<FJsonValue>> PartStateFailures;
    TArray<TSharedPtr<FJsonValue>> SpatialMismatches;
    TArray<TSharedPtr<FJsonValue>> SpatialSchemaErrorInstances;
    TArray<TSharedPtr<FJsonValue>> NonTransientInstances;

    for (const auto& Val : Arr)
    {
        const TSharedPtr<FJsonObject>* SnapObj = nullptr;
        if (!Val.IsValid() || !Val->TryGetObject(SnapObj) || !SnapObj || !SnapObj->IsValid())
        {
            ++MalformedSnapshots;
            continue;
        }

        FString InstId;
        if (!(*SnapObj)->TryGetStringField(TEXT("instanceId"), InstId) || InstId.IsEmpty())
        {
            ++MalformedSnapshots;
            continue;
        }

        FString AssetPathStr;
        FString ContainerBlueprintPath;
        FString ContainerSlot = TEXT("primary");
        FString ExpectedAssemblySignature;
        int32 ExpectedPartCount = 0;
        bool bExpectedOverallVisible = true;
        const TArray<TSharedPtr<FJsonValue>>* ExpectedParts = nullptr;
        const TSharedPtr<FJsonObject>* InterfacesObj = nullptr;
        if ((*SnapObj)->TryGetObjectField(TEXT("interfaces"), InterfacesObj))
        {
            const TSharedPtr<FJsonObject>* RepObj;
            if ((*InterfacesObj)->TryGetObjectField(TEXT("I3D_Representable"), RepObj))
            {
                (*RepObj)->TryGetStringField(TEXT("asset_id"), AssetPathStr);
                (*RepObj)->TryGetStringField(
                    TEXT("container_blueprint_id"),
                    ContainerBlueprintPath);
                (*RepObj)->TryGetStringField(TEXT("container_slot"), ContainerSlot);
                if (ContainerSlot.IsEmpty())
                {
                    ContainerSlot = TEXT("primary");
                }
                (*RepObj)->TryGetStringField(
                    TEXT("assembly_signature"), ExpectedAssemblySignature);
                (*RepObj)->TryGetBoolField(
                    TEXT("is_visible"), bExpectedOverallVisible);
                if ((*RepObj)->TryGetArrayField(TEXT("render_parts"), ExpectedParts)
                    && ExpectedParts)
                {
                    ExpectedPartCount = ExpectedParts->Num();
                }
            }
        }

        // 关键：RF_Transient → 保存关卡时绝不写入 .umap（杜绝“误固化”）
        FActorSpawnParameters SpawnParams;
        SpawnParams.ObjectFlags |= RF_Transient;

        ATwinInstance* Inst = World->SpawnActor<ATwinInstance>(
            SpawnClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
        if (!Inst) continue;
        if (Inst->HasAnyFlags(RF_Transient))
        {
            ++TransientActors;
        }
        else
        {
            NonTransientInstances.Add(MakeShared<FJsonValueString>(InstId));
        }

        FString DisplayName;
        if (!(*SnapObj)->TryGetStringField(TEXT("displayName"), DisplayName) || DisplayName.IsEmpty())
        {
            DisplayName = InstId;
        }
        Inst->SetActorLabel(FString::Printf(TEXT("[预览] %s"), *DisplayName));
        Inst->TwinDisplayName = DisplayName;
        Inst->SetFolderPath(BuildTwinFolderPath(*SnapObj, TEXT("TwinPreview")));
        Inst->InitializeTwin(
            InstId,
            AssetPathStr,
            BackendBaseUrl,
            ContainerBlueprintPath,
            ContainerSlot);
        Inst->ApplySnapshot(*SnapObj);   // 应用位置/材质等，摆到正确位置

        const int32 ActualPartCount = Inst->GetRenderPartComponentCount();
        ExpectedRenderParts += ExpectedPartCount;
        LoadedRenderParts += ActualPartCount;
        if (ExpectedPartCount > 0)
        {
            ++AssemblyInstances;
            const bool bAssemblyComplete = Inst->IsAssemblyRenderActive()
                && ActualPartCount == ExpectedPartCount
                && !ExpectedAssemblySignature.IsEmpty()
                && Inst->GetCurrentAssemblySignature() == ExpectedAssemblySignature;
            if (bAssemblyComplete)
            {
                ++CompleteAssemblies;
            }
            else
            {
                TSharedPtr<FJsonObject> Failure = MakeShared<FJsonObject>();
                Failure->SetStringField(TEXT("instance_id"), InstId);
                Failure->SetNumberField(TEXT("expected_render_parts"), ExpectedPartCount);
                Failure->SetNumberField(TEXT("loaded_render_parts"), ActualPartCount);
                Failure->SetStringField(
                    TEXT("expected_assembly_signature"), ExpectedAssemblySignature);
                Failure->SetStringField(
                    TEXT("loaded_assembly_signature"), Inst->GetCurrentAssemblySignature());
                IncompleteAssemblies.Add(MakeShared<FJsonValueObject>(Failure));
            }

            TArray<TSharedPtr<FJsonValue>> InstancePartFailures;
            if (ExpectedParts
                && Inst->ValidateRenderPartsAgainstSnapshot(
                    *ExpectedParts,
                    bExpectedOverallVisible,
                    InstancePartFailures))
            {
                ++PartStateValidAssemblies;
            }
            else
            {
                TSharedPtr<FJsonObject> Failure = MakeShared<FJsonObject>();
                Failure->SetStringField(TEXT("instance_id"), InstId);
                Failure->SetArrayField(
                    TEXT("part_failures"), InstancePartFailures);
                PartStateFailures.Add(MakeShared<FJsonValueObject>(Failure));
            }
        }

        const TSharedPtr<FJsonObject>* SpatialObj = nullptr;
        if (InterfacesObj
            && (*InterfacesObj)->TryGetObjectField(TEXT("I3D_Spatial"), SpatialObj)
            && SpatialObj && SpatialObj->IsValid())
        {
            bool bSpatialSchemaValid = true;
            auto ReadNumber = [SpatialObj, &bSpatialSchemaValid](
                const TCHAR* Field, double DefaultValue)
            {
                double Value = DefaultValue;
                if (!(*SpatialObj)->TryGetNumberField(Field, Value) || !FMath::IsFinite(Value))
                {
                    bSpatialSchemaValid = false;
                    return DefaultValue;
                }
                return Value;
            };
            const FVector ExpectedLocation(
                ReadNumber(TEXT("translation_x"), 0.0),
                ReadNumber(TEXT("translation_y"), 0.0),
                ReadNumber(TEXT("translation_z"), 0.0));
            const FRotator ExpectedRotation(
                ReadNumber(TEXT("rotation_y"), 0.0),
                ReadNumber(TEXT("rotation_z"), 0.0),
                ReadNumber(TEXT("rotation_x"), 0.0));
            const FVector ExpectedScale(
                ReadNumber(TEXT("scale_x"), 1.0),
                ReadNumber(TEXT("scale_y"), 1.0),
                ReadNumber(TEXT("scale_z"), 1.0));
            if (bSpatialSchemaValid)
            {
                ++SpatialSnapshots;
                constexpr double LocationToleranceCm = 0.01;
                constexpr double ScaleTolerance = 1.0e-5;
                constexpr double RotationToleranceDegrees = 0.01;
                const FVector ActualLocation = Inst->GetActorLocation();
                const FVector ActualScale = Inst->GetActorScale3D();
                const FQuat ExpectedQuat = ExpectedRotation.Quaternion();
                const FQuat ActualQuat = Inst->GetActorQuat();
                const double RotationErrorDegrees = FMath::RadiansToDegrees(
                    ActualQuat.AngularDistance(ExpectedQuat));
                const bool bLocationMatches = ActualLocation.Equals(
                    ExpectedLocation, LocationToleranceCm);
                const bool bScaleMatches = ActualScale.Equals(
                    ExpectedScale, ScaleTolerance);
                const bool bRotationMatches =
                    RotationErrorDegrees <= RotationToleranceDegrees;

                if (!bLocationMatches || !bScaleMatches || !bRotationMatches)
                {
                    auto VectorToJson = [](const FVector& Value)
                    {
                        TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
                        Object->SetNumberField(TEXT("x"), Value.X);
                        Object->SetNumberField(TEXT("y"), Value.Y);
                        Object->SetNumberField(TEXT("z"), Value.Z);
                        return Object;
                    };
                    TSharedPtr<FJsonObject> Mismatch = MakeShared<FJsonObject>();
                    Mismatch->SetStringField(TEXT("instance_id"), InstId);
                    Mismatch->SetObjectField(
                        TEXT("expected_location_cm"), VectorToJson(ExpectedLocation));
                    Mismatch->SetObjectField(
                        TEXT("actual_location_cm"), VectorToJson(ActualLocation));
                    Mismatch->SetObjectField(
                        TEXT("expected_scale"), VectorToJson(ExpectedScale));
                    Mismatch->SetObjectField(
                        TEXT("actual_scale"), VectorToJson(ActualScale));
                    Mismatch->SetNumberField(
                        TEXT("rotation_error_degrees"), RotationErrorDegrees);
                    Mismatch->SetBoolField(
                        TEXT("location_matches"), bLocationMatches);
                    Mismatch->SetBoolField(TEXT("scale_matches"), bScaleMatches);
                    Mismatch->SetBoolField(
                        TEXT("rotation_matches"), bRotationMatches);
                    SpatialMismatches.Add(MakeShared<FJsonValueObject>(Mismatch));
                }
            }
            else
            {
                ++SpatialSchemaErrors;
                SpatialSchemaErrorInstances.Add(MakeShared<FJsonValueString>(InstId));
            }
        }
        else
        {
            ++SpatialSchemaErrors;
            SpatialSchemaErrorInstances.Add(MakeShared<FJsonValueString>(InstId));
        }

        // FR-5：记录回写基线 = 数据库此刻给的 transform（提交时 diff 用）
        PreviewBaseline.Add(InstId, Inst->GetActorTransform());

        if (ExpectedPartCount > 0)
        {
            FString MaterialBaselineError;
            if (!CapturePreviewMaterialBaseline(
                    Inst, *SnapObj, &MaterialBaselineError))
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[材质回写] %s 未建立基线: %s"),
                    *InstId, *MaterialBaselineError);
            }
        }

        PreviewActors.Add(Inst);
        Count++;
    }

    TSharedPtr<FJsonObject> Audit = MakeShared<FJsonObject>();
    FTCHARToUTF8 SnapshotUtf8(*JsonPayload);
    Audit->SetStringField(TEXT("schema_version"), TEXT("zhhz_ue_preview_audit_v1"));
    Audit->SetStringField(TEXT("source"), SourceLabel);
    Audit->SetStringField(
        TEXT("snapshot_md5"),
        FMD5::HashBytes(
            reinterpret_cast<const uint8*>(SnapshotUtf8.Get()),
            static_cast<uint64>(SnapshotUtf8.Length())));
    Audit->SetBoolField(TEXT("transient"), Count > 0 && TransientActors == Count);
    Audit->SetBoolField(TEXT("level_save_requested"), false);
    Audit->SetNumberField(TEXT("requested_instances"), Arr.Num());
    Audit->SetNumberField(TEXT("spawned_instances"), Count);
    Audit->SetNumberField(TEXT("expected_render_parts"), ExpectedRenderParts);
    Audit->SetNumberField(TEXT("loaded_render_parts"), LoadedRenderParts);
    Audit->SetNumberField(TEXT("assembly_instances"), AssemblyInstances);
    Audit->SetNumberField(TEXT("complete_assemblies"), CompleteAssemblies);
    Audit->SetNumberField(
        TEXT("part_state_valid_assemblies"), PartStateValidAssemblies);
    Audit->SetNumberField(TEXT("spatial_snapshots"), SpatialSnapshots);
    Audit->SetNumberField(TEXT("spatial_schema_errors"), SpatialSchemaErrors);
    Audit->SetNumberField(TEXT("malformed_snapshots"), MalformedSnapshots);
    Audit->SetNumberField(TEXT("transient_actors"), TransientActors);
    Audit->SetArrayField(TEXT("incomplete_assemblies"), IncompleteAssemblies);
    Audit->SetArrayField(TEXT("part_state_failures"), PartStateFailures);
    Audit->SetArrayField(TEXT("spatial_mismatch_instances"), SpatialMismatches);
    Audit->SetArrayField(
        TEXT("spatial_schema_error_instances"), SpatialSchemaErrorInstances);
    Audit->SetArrayField(TEXT("non_transient_instances"), NonTransientInstances);
    const bool bRequireAssemblies = SourceLabel == TEXT("migration_snapshot_file");
    const bool bAuditPassed =
        Arr.Num() > 0
            && Count > 0
            && Count == Arr.Num()
            && MalformedSnapshots == 0
            && SpatialSnapshots == Count
            && SpatialSchemaErrors == 0
            && TransientActors == Count
            && ExpectedRenderParts == LoadedRenderParts
            && IncompleteAssemblies.Num() == 0
            && PartStateFailures.Num() == 0
            && SpatialMismatches.Num() == 0
            && (!bRequireAssemblies
                || (AssemblyInstances == Count
                    && CompleteAssemblies == Count
                    && PartStateValidAssemblies == Count
                    && ExpectedRenderParts > 0));
    Audit->SetBoolField(TEXT("passed"), bAuditPassed);

    FString AuditJson;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&AuditJson);
    FJsonSerializer::Serialize(Audit.ToSharedRef(), Writer);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(AuditPath), true);
    const bool bAuditSaved = FFileHelper::SaveStringToFile(
        AuditJson,
        *AuditPath,
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    if (bAuditSaved)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[孪生管理器] 预览审计 %s：%d 个 transient Actor；部件 %d/%d；完整装配 %d/%d；逐部件状态 %d/%d；审计=%s"),
            bAuditPassed ? TEXT("PASS") : TEXT("FAIL"),
            Count, LoadedRenderParts, ExpectedRenderParts,
            CompleteAssemblies, AssemblyInstances,
            PartStateValidAssemblies, AssemblyInstances, *AuditPath);
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(
                -1,
                12.0f,
                bAuditPassed ? FColor::Green : FColor::Red,
                FString::Printf(
                    TEXT("迁移预览审计 %s：实例 %d，渲染部件 %d/%d"),
                    bAuditPassed ? TEXT("PASS") : TEXT("FAIL"),
                    Count,
                    LoadedRenderParts,
                    ExpectedRenderParts));
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[孪生管理器] 无法写入预览审计"));
    }
    return Count;
#else
    return 0;
#endif
}
