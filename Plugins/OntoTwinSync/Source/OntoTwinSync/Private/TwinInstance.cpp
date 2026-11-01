// ============================================================================
// TwinInstance.cpp  (修复版)
//
// 修复点：
//   1. 构造函数移除 SetVisibility(false)，改由 SetActorHiddenInGame 控制
//   2. LoadMeshFromPath 失败时也正确显示占位立方体
//   3. 增加全链路诊断日志，便于 Output Log 排查
// ============================================================================

#include "TwinInstance.h"
#include "TwinSceneManager.h"
#include "EngineUtils.h"
#include "DigitalTwinSyncComponent.h"
#include "OntoTwinOverlayWidget.h"
#include "Representation/TwinRepresentationHostComponent.h"
#include "Presentation/OntoTwinPresentationExecutor.h"
#include "Components/MeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/LevelScriptActor.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/Engine.h"
#include "Kismet/KismetMathLibrary.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "Misc/ScopeLock.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "HAL/CriticalSection.h"
#include "Containers/Ticker.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
// HTTP —— ArtStudio glb 经后端代理流式下载（3.3）
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
// glTFRuntime —— 运行时加载磁盘上的 glb/gltf（B2 方案，模型不参与打包）
#include "glTFRuntimeFunctionLibrary.h"
#include "glTFRuntimeAsset.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"

namespace
{
FCriticalSection GArtStudioDownloadGuard;
TSet<FString> GArtStudioDownloadsInFlight;
TMap<FString, TArray<TWeakObjectPtr<ATwinInstance>>> GArtStudioDownloadWaiters;

bool IsValidGlbPayload(const TArray<uint8>& Content)
{
    if (Content.Num() < 12)
    {
        return false;
    }

    const bool bHasMagic = Content[0] == 'g' && Content[1] == 'l'
        && Content[2] == 'T' && Content[3] == 'F';
    const uint32 Version = static_cast<uint32>(Content[4])
        | (static_cast<uint32>(Content[5]) << 8)
        | (static_cast<uint32>(Content[6]) << 16)
        | (static_cast<uint32>(Content[7]) << 24);
    const uint32 DeclaredLength = static_cast<uint32>(Content[8])
        | (static_cast<uint32>(Content[9]) << 8)
        | (static_cast<uint32>(Content[10]) << 16)
        | (static_cast<uint32>(Content[11]) << 24);
    return bHasMagic && Version == 2 && DeclaredLength == static_cast<uint32>(Content.Num());
}

bool TryParseCollisionEnabled(
    const TSharedPtr<FJsonObject>& PartObj,
    ECollisionEnabled::Type& OutCollisionEnabled)
{
    if (!PartObj.IsValid()) return false;

    FString Value;
    if (PartObj->TryGetStringField(TEXT("collision_enabled"), Value))
    {
        Value.TrimStartAndEndInline();
        if (Value.Equals(TEXT("NoCollision"), ESearchCase::IgnoreCase))
            OutCollisionEnabled = ECollisionEnabled::NoCollision;
        else if (Value.Equals(TEXT("QueryOnly"), ESearchCase::IgnoreCase))
            OutCollisionEnabled = ECollisionEnabled::QueryOnly;
        else if (Value.Equals(TEXT("PhysicsOnly"), ESearchCase::IgnoreCase))
            OutCollisionEnabled = ECollisionEnabled::PhysicsOnly;
        else if (Value.Equals(TEXT("QueryAndPhysics"), ESearchCase::IgnoreCase))
            OutCollisionEnabled = ECollisionEnabled::QueryAndPhysics;
        else if (Value.Equals(TEXT("ProbeOnly"), ESearchCase::IgnoreCase))
            OutCollisionEnabled = ECollisionEnabled::ProbeOnly;
        else if (Value.Equals(TEXT("QueryAndProbe"), ESearchCase::IgnoreCase))
            OutCollisionEnabled = ECollisionEnabled::QueryAndProbe;
        else
            return false;
        return true;
    }

    // 兼容早期或人工构造的数值 payload。
    double NumericValue = 0.0;
    if (PartObj->TryGetNumberField(TEXT("collision_enabled"), NumericValue)
        && NumericValue >= static_cast<int32>(ECollisionEnabled::NoCollision)
        && NumericValue <= static_cast<int32>(ECollisionEnabled::QueryAndProbe))
    {
        OutCollisionEnabled = static_cast<ECollisionEnabled::Type>(FMath::RoundToInt(NumericValue));
        return true;
    }
    return false;
}

FString CollisionEnabledToString(ECollisionEnabled::Type Value)
{
    switch (Value)
    {
    case ECollisionEnabled::NoCollision: return TEXT("NoCollision");
    case ECollisionEnabled::QueryOnly: return TEXT("QueryOnly");
    case ECollisionEnabled::PhysicsOnly: return TEXT("PhysicsOnly");
    case ECollisionEnabled::QueryAndPhysics: return TEXT("QueryAndPhysics");
    case ECollisionEnabled::ProbeOnly: return TEXT("ProbeOnly");
    case ECollisionEnabled::QueryAndProbe: return TEXT("QueryAndProbe");
    default: return FString::Printf(TEXT("Unknown(%d)"), static_cast<int32>(Value));
    }
}

TSharedPtr<FJsonValue> JsonVectorValue(const FVector& Value)
{
    TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
    Object->SetNumberField(TEXT("x"), Value.X);
    Object->SetNumberField(TEXT("y"), Value.Y);
    Object->SetNumberField(TEXT("z"), Value.Z);
    return MakeShared<FJsonValueObject>(Object);
}

TSharedPtr<FJsonValue> JsonRotatorValue(const FRotator& Value)
{
    TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
    Object->SetNumberField(TEXT("roll"), Value.Roll);
    Object->SetNumberField(TEXT("pitch"), Value.Pitch);
    Object->SetNumberField(TEXT("yaw"), Value.Yaw);
    return MakeShared<FJsonValueObject>(Object);
}

void AddAssemblyAuditFailure(
    TArray<TSharedPtr<FJsonValue>>& OutFailures,
    int32 PartIndex,
    const TCHAR* Field,
    const TSharedPtr<FJsonValue>& Expected,
    const TSharedPtr<FJsonValue>& Actual,
    double Tolerance = -1.0,
    int32 MaterialSlot = INDEX_NONE)
{
    TSharedPtr<FJsonObject> Failure = MakeShared<FJsonObject>();
    if (PartIndex != INDEX_NONE)
    {
        Failure->SetNumberField(TEXT("part_index"), PartIndex);
    }
    if (MaterialSlot != INDEX_NONE)
    {
        Failure->SetNumberField(TEXT("material_slot"), MaterialSlot);
    }
    Failure->SetStringField(TEXT("field"), Field);
    Failure->SetField(TEXT("expected"), Expected);
    Failure->SetField(TEXT("actual"), Actual);
    if (Tolerance >= 0.0)
    {
        Failure->SetNumberField(TEXT("tolerance"), Tolerance);
    }
    OutFailures.Add(MakeShared<FJsonValueObject>(Failure));
}

void AppendVisibleMeshBounds(const AActor* Actor, FBox& OutBounds)
{
    if (!Actor || !IsValid(Actor) || Actor->IsHidden())
    {
        return;
    }

    TInlineComponentArray<UMeshComponent*> MeshComponents(Actor);
    for (const UMeshComponent* Component : MeshComponents)
    {
        // World-space UI is presentation, not part of the represented model.
        // Including TwinOverlay here creates a feedback loop: the overlay moves
        // to BoundsTop, then its own bounds raise BoundsTop again on the next
        // frame until the panel is projected off-screen and culled.
        if (Component
            && !Component->IsA<UWidgetComponent>()
            && Component->IsRegistered()
            && Component->IsVisible())
        {
            OutBounds += Component->Bounds.GetBox();
        }
    }
}
}

FOnTwinRepresentationVisualReady ATwinInstance::OnRepresentationVisualReady;

// ── 构造函数 ─────────────────────────────────────────────────────────────────

ATwinInstance::ATwinInstance()
{
    // Tick 默认关闭，只有动画进行时才开启，节省性能
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;

    // 创建默认的 StaticMeshComponent 作为根组件
    MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TwinMesh"));
    RootComponent = MeshComponent;
    // TwinInstance 是 OntoTwin 动态生成的语义/显示代理，不负责定义宿主关卡的可行走碰撞。
    // 保留 Visibility 响应供射线选择，Pawn 碰撞由 UE 关卡中的真实地面、墙体和设备承担。
    MeshComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);

    SkeletalMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("TwinSkeletalMesh"));
    SkeletalMeshComponent->SetupAttachment(MeshComponent);
    SkeletalMeshComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
    SkeletalMeshComponent->SetVisibility(false, false);

    BlueprintActorComponent = CreateDefaultSubobject<UChildActorComponent>(TEXT("TwinBlueprintActor"));
    BlueprintActorComponent->SetupAttachment(MeshComponent);
    BlueprintActorComponent->SetRelativeTransform(FTransform::Identity);
    BlueprintActorComponent->SetVisibility(false, false);

    // 创建 3D 文字标签组件，默认隐藏
    LabelComponent = CreateDefaultSubobject<UTextRenderComponent>(TEXT("TwinLabel"));
    LabelComponent->SetupAttachment(MeshComponent);
    LabelComponent->SetRelativeLocation(FVector(0.f, 0.f, LabelZOffset)); // 默认 20cm 高
    // 恢复默认朝向 (之前为了测试曾改过 180)
    LabelComponent->SetRelativeRotation(FRotator(0.f, 0.f, 0.f));
    LabelComponent->SetHorizontalAlignment(EHTA_Center);                   // 水平居中

    LabelComponent->SetVerticalAlignment(EVRTA_TextCenter);                // 垂直居中
    LabelComponent->SetWorldSize(LabelWorldSize);                          // 字体大小
    LabelComponent->SetTextRenderColor(LabelColor);                        // 文字颜色
    LabelComponent->SetVisibility(false);                                  // 初始隐藏
    LabelComponent->SetText(FText::GetEmpty());

    OverlayWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("TwinOverlay"));
    OverlayWidgetComponent->SetupAttachment(MeshComponent);
    OverlayWidgetComponent->SetAbsolute(false, false, true);
    OverlayWidgetComponent->SetWidgetSpace(EWidgetSpace::World);
    OverlayWidgetComponent->SetDrawSize(FVector2D(720.0f, 160.0f));
    OverlayWidgetComponent->SetDrawAtDesiredSize(false);
    OverlayWidgetComponent->SetPivot(FVector2D(0.5f, 1.0f));
    OverlayWidgetComponent->SetBlendMode(EWidgetBlendMode::Transparent);
    OverlayWidgetComponent->SetTwoSided(true);
    OverlayWidgetComponent->SetTickWhenOffscreen(false);
    OverlayWidgetComponent->SetManuallyRedraw(true);
    OverlayWidgetComponent->SetRedrawTime(0.0f);
    OverlayWidgetComponent->SetWindowFocusable(false);
    OverlayWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    OverlayWidgetComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
    OverlayWidgetComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    OverlayWidgetComponent->SetGenerateOverlapEvents(false);
    OverlayWidgetComponent->SetVisibility(false);
    OverlayWidgetComponent->SetWorldScale3D(FVector(0.05f));
}

// ── BeginPlay ────────────────────────────────────────────────────────────────

void ATwinInstance::BeginPlay()
{
    Super::BeginPlay();
    InitAnimLibrary();
    if (OverlayWidgetComponent)
    {
        OverlayWidgetComponent->SetWidgetClass(UOntoTwinOverlayWidget::StaticClass());
        OverlayWidgetComponent->InitWidget();
        WorldOverlayWidget = Cast<UOntoTwinOverlayWidget>(OverlayWidgetComponent->GetUserWidgetObject());
        if (WorldOverlayWidget)
        {
            WorldOverlayWidget->SetWorldSpacePresentation(true);
            UpdateWorldOverlayRenderTarget();
        }
    }
}

void ATwinInstance::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // ── 3D文字始终朝向相机 (Billboarding) ──
    if (LabelComponent && LabelComponent->IsVisible())
    {
        UWorld* World = GetWorld();
        APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
        if (APlayerCameraManager* CamManager = PlayerController
            ? PlayerController->PlayerCameraManager
            : nullptr)
        {
            FVector CamLoc = CamManager->GetCameraLocation();
            FVector TextLoc = LabelComponent->GetComponentLocation();

            // 计算 LookAt 旋转
            FRotator LookAtRot = UKismetMathLibrary::FindLookAtRotation(TextLoc, CamLoc);

            // 只需要水平环绕相机（Yaw），强行把 Pitch 和 Roll 锁定为 0，防止文字趴在地上或者竖直歪曲
            FRotator BillboardRot(0.f, LookAtRot.Yaw, 0.f);

            // 如果你发现文字刚好是左右镜像反的，可以改成 BillboardRot.Yaw += 180.f; 但纯 LookAt 一般是正的！
            LabelComponent->SetWorldRotation(BillboardRot);
        }
    }

    if (HasAlwaysOverlay() && OverlayWidgetComponent && OverlayWidgetComponent->IsVisible())
    {
        if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
        {
            if (APlayerCameraManager* CamManager = PC->PlayerCameraManager)
            {
                RefreshAlwaysOverlay(CamManager->GetCameraLocation(), true);
            }
        }
    }

    if (!bAnimRunning) return;

    AnimTimer += DeltaTime;
    float Duration = ActiveRecipe.Duration;
    if (Duration <= 0.f) return;

    // 计算动画进度 Alpha（0.0−1.0）
    float RawAlpha = FMath::Fmod(AnimTimer, Duration) / Duration;

    // PingPong：偶数循环就反过来
    float Alpha = RawAlpha;
    if (ActiveRecipe.bPingPong)
    {
        int32 CycleIndex = FMath::FloorToInt(AnimTimer / Duration);
        if (CycleIndex % 2 == 1) Alpha = 1.0f - RawAlpha;
    }

    // 平滑曲线（SmoothStep）让动画两端更自然
    float SmoothAlpha = FMath::SmoothStep(0.f, 1.f, Alpha);

    // 应用位移
    if (!ActiveRecipe.TranslationDelta.IsNearlyZero())
    {
        FVector NewLoc = AnimBaseLocation + ActiveRecipe.TranslationDelta * SmoothAlpha;
        SetActorLocation(NewLoc);
    }

    // 应用旋转
    if (!ActiveRecipe.RotationDelta.IsNearlyZero())
    {
        FRotator Delta = ActiveRecipe.RotationDelta * SmoothAlpha;
        FRotator NewRot = AnimBaseRotation + Delta;
        SetActorRotation(NewRot);
    }

    // 如果不循环且时间到达，停止
    if (!ActiveRecipe.bLoop && AnimTimer >= Duration)
    {
        bAnimRunning = false;
        SetActorEnableCollision(true);
        // 如果文字没显示，才真正关闭 Tick
        if ((!LabelComponent || !LabelComponent->IsVisible()) && !HasAlwaysOverlay())
        {
            SetActorTickEnabled(false);
        }
    }
}

FString ATwinInstance::PauseRuntimeEditorAnimation(bool& bOutWasRunning)
{
    bOutWasRunning = bAnimRunning;
    const FString PreviousState = CurrentAnimState;
    bAnimRunning = false;
    AnimTimer = 0.0f;
    return PreviousState;
}

void ATwinInstance::ResumeRuntimeEditorAnimation(const FString& PreviousState, bool bWasRunning)
{
    if (!bWasRunning || PreviousState.IsEmpty())
    {
        return;
    }

    PlayAnimationState(PreviousState);
}

// 初始化动画配方字典
void ATwinInstance::InitAnimLibrary()
{
    AnimLibrary.Empty();

    // idle: 停止，无动画
    AnimLibrary.Add(TEXT("idle"),
        FAnimRecipe(FVector::ZeroVector, FRotator::ZeroRotator, 0.f, false, false));

    // translate: X轴平移 100cm，循环往返，3秒一霿
    AnimLibrary.Add(TEXT("translate"),
        FAnimRecipe(FVector(100.f, 0.f, 0.f), FRotator::ZeroRotator, 3.0f, true, true));

    // jump: Z轴上弹 15cm，循环往返，1秒一霿
    AnimLibrary.Add(TEXT("jump"),
        FAnimRecipe(FVector(0.f, 0.f, 15.f), FRotator::ZeroRotator, 1.0f, true, true));

    // flip: Y轴封转 180°，循环往返，1.5秒一霿
    AnimLibrary.Add(TEXT("flip"),
        FAnimRecipe(FVector::ZeroVector, FRotator(180.f, 0.f, 0.f), 1.5f, true, true));
}

// 立即切换并播放动画状态
void ATwinInstance::PlayAnimationState(const FString& StateName)
{
    const FAnimRecipe* Found = AnimLibrary.Find(StateName);
    if (!Found)
    {
        UE_LOG(LogTemp, Warning, TEXT("[孪生体] 未知动画状态: %s"), *StateName);
        return;
    }

    // idle 返回初始位置并关闭 Tick
    if (StateName == TEXT("idle"))
    {
        bAnimRunning = false;
        if ((!LabelComponent || !LabelComponent->IsVisible()) && !HasAlwaysOverlay())
        {
            SetActorTickEnabled(false);
        }
        // 归位
        SetActorLocation(AnimBaseLocation);
        SetActorRotation(AnimBaseRotation);
        UE_LOG(LogTemp, Log, TEXT("[孪生体] 动画归位: %s"), *InstanceId);
        return;
    }

    // 记录当前状态作为基准点
    AnimBaseLocation = GetActorLocation();
    AnimBaseRotation = GetActorRotation();
    AnimTimer        = 0.0f;
    ActiveRecipe     = *Found;
    bAnimRunning     = true;

    // 开启 Tick
    SetActorTickEnabled(true);

    UE_LOG(LogTemp, Log, TEXT("[孪生体] 动画切换: %s → %s"), *InstanceId, *StateName);
}

// ═══════════════════════════════════════════════════════════════════════════
// 公开接口
// ═══════════════════════════════════════════════════════════════════════════

void ATwinInstance::InitializeTwin(
    const FString& InInstanceId,
    const FString& InAssetPath,
    const FString& InBackendBaseUrl,
    const FString& InContainerBlueprintPath,
    const FString& InContainerSlot)
{
    InstanceId     = InInstanceId;
    if (TwinDisplayName.IsEmpty())
    {
        TwinDisplayName = InInstanceId;
    }
    AssetPath      = InAssetPath;
    BackendBaseUrl = InBackendBaseUrl;
    ContainerBlueprintPath = InContainerBlueprintPath;
    ContainerSlot = InContainerSlot.IsEmpty() ? TEXT("primary") : InContainerSlot;

    UE_LOG(LogTemp, Log,
        TEXT("[孪生体] ████ 初始化开始 | ID=%s | 资产路径=%s | 容器=%s | 槽位=%s"),
        *InstanceId,
        *AssetPath,
        *ContainerBlueprintPath,
        *ContainerSlot);

    // ── 1. 加载主表现 ────────────────────────────────────────────────────
    if (AssetPath.IsEmpty())
    {
        UE_LOG(LogTemp, Warning,
               TEXT("[孪生体] ⚠️  资产路径为空 (ID=%s)，使用默认立方体"), *InstanceId);
        SetPlaceholderCube();
    }
    else if (!ContainerBlueprintPath.IsEmpty())
    {
        LoadContainerBlueprintRepresentation(
            ContainerBlueprintPath,
            ContainerSlot,
            AssetPath);
    }
    else
    {
        LoadMeshFromPath(AssetPath);
    }

    // ── 2. 缓存原始材质 ──────────────────────────────────────────────────
    if (UMeshComponent* ActiveMesh = GetActiveMeshComponent();
        ActiveMesh && ActiveMesh->GetNumMaterials() > 0)
    {
        CacheOriginalMaterials();
    }

    bInitialized = true;
    UE_LOG(LogTemp, Log, TEXT("[孪生体] ████ 初始化完成 | ID=%s"), *InstanceId);
}

void ATwinInstance::ApplySnapshot(const TSharedPtr<FJsonObject>& Snapshot, bool bIsDelta)
{
    if (!Snapshot.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("[孪生体] ApplySnapshot: Snapshot 无效 (ID=%s)"), *InstanceId);
        return;
    }

    FString SnapshotDisplayName;
    if (Snapshot->TryGetStringField(TEXT("displayName"), SnapshotDisplayName) && !SnapshotDisplayName.IsEmpty())
    {
        TwinDisplayName = SnapshotDisplayName;
    }

    FString SnapshotObjectTypeName;
    if (Snapshot->TryGetStringField(TEXT("objectTypeName"), SnapshotObjectTypeName)
        && !SnapshotObjectTypeName.IsEmpty())
    {
        TwinObjectTypeName = SnapshotObjectTypeName;
    }
    else
    {
        FString SnapshotObjectTypeRid;
        if (Snapshot->TryGetStringField(TEXT("objectTypeRid"), SnapshotObjectTypeRid)
            && !SnapshotObjectTypeRid.IsEmpty())
        {
            TwinObjectTypeName = SnapshotObjectTypeRid;
        }
    }

    const TSharedPtr<FJsonObject>* RuntimeEditorObj = nullptr;
    if (Snapshot->TryGetObjectField(TEXT("runtimeEditor"), RuntimeEditorObj) && RuntimeEditorObj)
    {
        (*RuntimeEditorObj)->TryGetBoolField(TEXT("spatialEditable"), bRuntimeSpatialEditable);
        (*RuntimeEditorObj)->TryGetStringField(TEXT("stateHash"), RuntimeEditStateHash);
    }

    const TSharedPtr<FJsonObject>* InterfacesObj;
    if (!Snapshot->TryGetObjectField(TEXT("interfaces"), InterfacesObj))
    {
        if (!bIsDelta)
        {
            UE_LOG(LogTemp, Warning,
                   TEXT("[孪生体] ApplySnapshot: 快照中无 'interfaces' 字段 (ID=%s)"), *InstanceId);
        }
        return;
    }

    UE_LOG(LogTemp, Verbose, TEXT("[孪生体] 应用快照 (ID=%s)"), *InstanceId);

    // A full snapshot without the coordinator is an old/compatibility
    // payload.  Delta payloads preserve the previous authority until a new
    // coordinator object arrives.
    if (!bIsDelta && !(*InterfacesObj)->HasField(TEXT("I3D_Presentation")))
    {
        if (bPresentationAuthoritative)
        {
            ExecutePresentationRoute(TEXT("animation"), TEXT("safe.idle"), TEXT("motion"));
            ExecutePresentationRoute(TEXT("visual"), TEXT("safe.visible"), TEXT("status_indicator"));
            ExecutePresentationRoute(TEXT("fx"), TEXT("safe.none"), TEXT("alarm"));
            ExecutePresentationRoute(TEXT("label"), TEXT("safe.label"), TEXT("label"));
        }
        bPresentationAuthoritative = false;
        ActivePresentationRouteKeys.Reset();
    }

    // ── I3D_Presentation ────────────────────────────────────────────────
    const TSharedPtr<FJsonObject>* PresentationObj;
    if ((*InterfacesObj)->TryGetObjectField(TEXT("I3D_Presentation"), PresentationObj))
    {
        ApplyPresentationFromSnapshot(*PresentationObj);
    }

    // ── I3D_Representable ────────────────────────────────────────────────
    const TSharedPtr<FJsonObject>* RepObj;
    if ((*InterfacesObj)->TryGetObjectField(TEXT("I3D_Representable"), RepObj))
    {
        ApplyRepresentableFromSnapshot(*RepObj, bIsDelta);
    }

    // ── I3D_Spatial ──────────────────────────────────────────────────────
    const TSharedPtr<FJsonObject>* SpatialObj;
    if ((*InterfacesObj)->TryGetObjectField(TEXT("I3D_Spatial"), SpatialObj))
    {
        ApplySpatialFromSnapshot(*SpatialObj);
    }

    // ── I3D_Visual ───────────────────────────────────────────────────────
    const TSharedPtr<FJsonObject>* VisualObj;
    if ((*InterfacesObj)->TryGetObjectField(TEXT("I3D_Visual"), VisualObj))
    {
        ApplyVisualFromSnapshot(*VisualObj);
    }

    // ── I3D_Behavioral ──────────────────────────────────────────────────
    const TSharedPtr<FJsonObject>* BehaviorObj;
    if ((*InterfacesObj)->TryGetObjectField(TEXT("I3D_Behavioral"), BehaviorObj))
    {
        ApplyBehavioralFromSnapshot(*BehaviorObj);
    }

    if (!bIsDelta && !(*InterfacesObj)->HasField(TEXT("I3D_Visual")) && !(*InterfacesObj)->HasField(TEXT("I3D_Behavioral")))
    {
        TSharedPtr<FJsonObject> EmptyLabel = MakeShared<FJsonObject>();
        EmptyLabel->SetStringField(TEXT("ui_label_content"), TEXT(""));
        ApplyPlainLabelFromSnapshot(EmptyLabel);
    }

    const TSharedPtr<FJsonObject>* OverlayObj;
    if ((*InterfacesObj)->TryGetObjectField(TEXT("I3D_Overlay"), OverlayObj))
    {
        ApplyOverlayFromSnapshot(*OverlayObj);
    }
    else if (!bIsDelta)
    {
        ClearOverlay();
    }
}

void ATwinInstance::ApplyPresentationFromSnapshot(const TSharedPtr<FJsonObject>& PresentationObj)
{
    if (!PresentationObj.IsValid())
    {
        return;
    }

    bPresentationAuthoritative = true;
    // Presentation owns display motion. Do not let a legacy actor animation
    // keep writing an old world-space baseline on subsequent ticks.
    bAnimRunning = false;
    AnimTimer = 0.0f;
    double RevisionNumber = 0.0;
    if (PresentationObj->TryGetNumberField(TEXT("presentation_revision"), RevisionNumber))
    {
        CurrentPresentationRevision = FMath::Max(CurrentPresentationRevision, static_cast<int32>(RevisionNumber));
    }
    PresentationObj->TryGetStringField(TEXT("decision_id"), CurrentPresentationDecisionId);

    const TSharedPtr<FJsonObject>* ResolutionObj = nullptr;
    if (PresentationObj->TryGetObjectField(TEXT("resolution"), ResolutionObj) && ResolutionObj)
    {
        const TSharedPtr<FJsonObject>* ChannelsObj = nullptr;
        if ((*ResolutionObj)->TryGetObjectField(TEXT("channels"), ChannelsObj) && ChannelsObj)
        {
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*ChannelsObj)->Values)
            {
                const TSharedPtr<FJsonObject>* ChannelObj = nullptr;
                if (!Pair.Value.IsValid()
                    || Pair.Value->Type != EJson::Object
                    || !Pair.Value->TryGetObject(ChannelObj)
                    || !ChannelObj || !ChannelObj->IsValid())
                {
                    continue;
                }
                FString BehaviorId;
                FString Slot;
                if (!(*ChannelObj)->TryGetStringField(TEXT("behavior_id"), BehaviorId)
                    || BehaviorId.IsEmpty())
                {
                    continue;
                }
                (*ChannelObj)->TryGetStringField(TEXT("slot"), Slot);
                const TSharedPtr<FJsonObject>* ParamsObj = nullptr;
                (*ChannelObj)->TryGetObjectField(TEXT("params"), ParamsObj);
                FString Source;
                (*ChannelObj)->TryGetStringField(TEXT("source"), Source);
                if (Source.IsEmpty())
                {
                    Source = TEXT("platform");
                }
                FString ParamsKey;
                if (ParamsObj && ParamsObj->IsValid())
                {
                    auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&ParamsKey);
                    FJsonSerializer::Serialize(ParamsObj->ToSharedRef(), Writer);
                }
                const FString RouteKey = Source + TEXT("|") + BehaviorId + TEXT("|") + Slot + TEXT("|") + ParamsKey;
                if (ActivePresentationRouteKeys.FindRef(Pair.Key) == RouteKey)
                {
                    continue;
                }
                if (ExecutePresentationRoute(Pair.Key, BehaviorId, Slot, Source,
                    ParamsObj && *ParamsObj ? *ParamsObj : MakeShared<FJsonObject>()))
                    ActivePresentationRouteKeys.Add(Pair.Key, RouteKey);
            }
        }
    }

    const TArray<TSharedPtr<FJsonValue>>* Actions = nullptr;
    if (PresentationObj->TryGetArrayField(TEXT("actions"), Actions) && Actions)
    {
        for (const TSharedPtr<FJsonValue>& ActionValue : *Actions)
        {
            const TSharedPtr<FJsonObject>* ActionObj = nullptr;
            if (!ActionValue.IsValid() || !ActionValue->TryGetObject(ActionObj)
                || !ActionObj || !ActionObj->IsValid())
            {
                continue;
            }
            FString EventId;
            FString ActionId;
            if (!(*ActionObj)->TryGetStringField(TEXT("event_id"), EventId)
                || EventId.IsEmpty()
                || !(*ActionObj)->TryGetStringField(TEXT("id"), ActionId)
                || ActionId.IsEmpty())
            {
                continue;
            }
            if (ConsumedPresentationEventIds.Contains(EventId))
            {
                continue;
            }
            const TSharedPtr<FJsonObject>* ParamsObj = nullptr;
            (*ActionObj)->TryGetObjectField(TEXT("params"), ParamsObj);
            const bool bExecuted = ExecutePresentationRoute(
                TEXT("action"), ActionId, TEXT(""), TEXT("platform"),
                ParamsObj && *ParamsObj ? *ParamsObj : MakeShared<FJsonObject>());
            // At-least-once delivery: mark as consumed only after an executor
            // accepts the event.  The set is intentionally process-local.
            if (bExecuted)
            {
                ConsumedPresentationEventIds.Add(EventId);
                if (ConsumedPresentationEventIds.Num() > 1024)
                {
                    ConsumedPresentationEventIds.Reset();
                    ConsumedPresentationEventIds.Add(EventId);
                }
            }
        }
    }
}

bool ATwinInstance::ExecutePresentationRoute(
    const FString& Channel,
    const FString& BehaviorId,
    const FString& Slot,
    const FString& Source,
    const TSharedPtr<FJsonObject>& Params)
{
    TArray<IOntoTwinPresentationExecutor*> Executors =
        IModularFeatures::Get().GetModularFeatureImplementations<IOntoTwinPresentationExecutor>(
            IOntoTwinPresentationExecutor::GetModularFeatureName());
    Executors.Sort([](const IOntoTwinPresentationExecutor& Left, const IOntoTwinPresentationExecutor& Right)
    {
        return Left.GetPresentationExecutorPriority() > Right.GetPresentationExecutorPriority();
    });

    for (IOntoTwinPresentationExecutor* Executor : Executors)
    {
        if (!Executor || !Executor->SupportsPresentationRoute(Channel, BehaviorId, Slot, Source))
        {
            continue;
        }
        if (Executor->ExecutePresentationRoute(this, Channel, BehaviorId, Slot, Source, Params))
        {
            return true;
        }
    }

    // Clear any previous platform effect when a replacement cannot execute.
    // Do not cache this fallback as success: the desired executor may load later.
    const FString SafeBehavior = Channel == TEXT("animation") ? TEXT("safe.idle")
        : Channel == TEXT("visual") ? TEXT("safe.visible")
        : Channel == TEXT("label") ? TEXT("safe.label") : TEXT("safe.none");
    for (IOntoTwinPresentationExecutor* Executor : Executors)
    {
        if (Executor && Executor->SupportsPresentationRoute(Channel, SafeBehavior, Slot, TEXT("platform")))
            Executor->ExecutePresentationRoute(this, Channel, SafeBehavior, Slot, TEXT("platform"), nullptr);
    }

    // Core-level safe fallback.  Unknown/project-specific routes never
    // execute legacy fields; they degrade to an inert safe state instead.
    if (Channel == TEXT("animation"))
    {
        // Failure must be inert, not a request to restore a legacy baseline
        // (which may be zero or predate the latest realtime spatial update).
        bAnimRunning = false;
        AnimTimer = 0.0f;
        CurrentAnimState = TEXT("idle");
        return false;
    }
    if (Channel == TEXT("visual"))
    {
        ApplyPresentationMaterialVariant(TEXT("normal"));
        ApplyVisualVisibility(true);
    }
    UE_LOG(LogTemp, Verbose, TEXT("[孪生体] 表现路由未找到执行器，安全回退: %s/%s/%s"),
        *Channel, *BehaviorId, *Slot);
    return false;
}

void ATwinInstance::PlayPresentationAnimation(const FString& StateName)
{
    CurrentAnimState = StateName;
    PlayAnimationState(StateName);
    if (ActiveContainerHost.IsValid())
    {
        ActiveContainerHost->ForwardBehaviorState(CurrentAnimState, CurrentFxTrigger);
    }
}

void ATwinInstance::ApplyPresentationMaterialVariant(const FString& VariantName)
{
    if (VariantName.IsEmpty() || VariantName == CurrentMaterialVariant)
    {
        return;
    }
    CurrentMaterialVariant = VariantName;
    if (VariantName == TEXT("normal"))
    {
        RestoreOriginalMaterials();
    }
    else if (VariantName == TEXT("gray") || VariantName == TEXT("wireframe"))
    {
        OnMaterialVariantChanged(VariantName);
    }
    else
    {
        OnMaterialVariantChanged(VariantName);
    }
    if (ActiveContainerHost.IsValid())
    {
        ActiveContainerHost->ForwardVisualState(CurrentMaterialVariant, bVisualVisible);
    }
}

void ATwinInstance::TriggerPresentationFx(const FString& FxName)
{
    if (!FxName.IsEmpty())
    {
        CurrentFxTrigger = FxName;
        OnFxTriggered(FxName);
        if (ActiveContainerHost.IsValid())
        {
            ActiveContainerHost->ForwardBehaviorState(CurrentAnimState, CurrentFxTrigger);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 资产加载
// ═══════════════════════════════════════════════════════════════════════════

bool ATwinInstance::LoadMeshFromPath(const FString& MeshPath)
{
    // asset_id 三种语义：
    //   0. "artstudio:{id}:v{n}" → ArtStudio 资产，后端代理下载（异步，3.3）
    //   1. "/Game/..." 或 "/Engine/..." → 烘焙进包的资产，走 LoadObject（向后兼容）
    //   2. 其他（如 "forklift.glb"）→ 运行时从固定目录/磁盘加载，不参与打包
    ClearSingleAssetRepresentation();

    if (MeshPath.StartsWith(TEXT("artstudio:")))
    {
        LoadRemoteGltf(MeshPath);   // 异步接管：命中缓存即时加载，否则占位 Cube + 下载
        return true;                 // 不走下方同步 Cube 兜底
    }
    if (MeshPath.StartsWith(TEXT("/Game/")) || MeshPath.StartsWith(TEXT("/Engine/")))
    {
        FString FullPath = MeshPath;
        if (!FullPath.Contains(TEXT(".")))
        {
            FString AssetName;
            MeshPath.Split(TEXT("/"), nullptr, &AssetName, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
            FullPath = FString::Printf(TEXT("%s.%s"), *MeshPath, *AssetName);
        }

        UE_LOG(LogTemp, Log, TEXT("[孪生体] 尝试加载烘焙资产: %s"), *FullPath);
        UObject* LoadedAsset = StaticLoadObject(UObject::StaticClass(), nullptr, *FullPath);
        if (!LoadedAsset && FullPath != MeshPath)
        {
            LoadedAsset = StaticLoadObject(UObject::StaticClass(), nullptr, *MeshPath);
        }
        if (UStaticMesh* Mesh = Cast<UStaticMesh>(LoadedAsset))
        {
            ActiveRepresentationKind = ERepresentationKind::StaticMesh;
            MeshComponent->SetVisibility(true, false);
            MeshComponent->SetStaticMesh(Mesh);
            CacheOriginalMaterials();
            ApplyVisualVisibility(bVisualVisible);
            UE_LOG(LogTemp, Log, TEXT("[孪生体] ✅ StaticMesh 加载成功: %s"), *MeshPath);
            return true;
        }
        if (Cast<USkeletalMesh>(LoadedAsset) && LoadSkeletalMeshAsset(FullPath))
        {
            return true;
        }
        if (LoadBlueprintActorAsset(MeshPath))
        {
            return true;
        }

        UE_LOG(LogTemp, Error,
            TEXT("[孪生体] 烘焙资产无法识别或生成类不可用: input=%s object=%s loaded_class=%s"),
            *MeshPath,
            *FullPath,
            LoadedAsset ? *LoadedAsset->GetClass()->GetPathName() : TEXT("none"));
    }
    else if (LoadRuntimeGltf(MeshPath))
    {
        return true;
    }

    // ── 加载失败：使用引擎内置立方体作为占位符 ──────────────────────────
    UE_LOG(LogTemp, Error,
           TEXT("[孪生体] ❌ 资产加载失败: %s   请检查路径/glb 文件是否存在"), *MeshPath);
    SetPlaceholderCube();
    UE_LOG(LogTemp, Warning, TEXT("[孪生体] 使用默认立方体占位 (ID=%s)"), *InstanceId);
    return false;
}

void ATwinInstance::ClearSingleAssetRepresentation()
{
    if (ActiveContainerHost.IsValid())
    {
        ActiveContainerHost->ClearRepresentation(InstanceId);
    }
    ActiveContainerHost.Reset();
    if (BlueprintActorComponent)
    {
        BlueprintActorComponent->SetChildActorClass(nullptr);
        BlueprintActorComponent->SetVisibility(false, false);
    }
    if (SkeletalMeshComponent)
    {
        SkeletalMeshComponent->SetSkeletalMesh(nullptr);
        SkeletalMeshComponent->SetVisibility(false, false);
    }
    if (MeshComponent)
    {
        MeshComponent->SetStaticMesh(nullptr);
        MeshComponent->SetVisibility(false, false);
    }
    ActiveRepresentationKind = ERepresentationKind::None;
    OriginalMaterials.Empty();
    CurrentMaterialVariant.Empty();
}

bool ATwinInstance::LoadSkeletalMeshAsset(const FString& ObjectPath)
{
    FString FullPath = ObjectPath;
    if (!FullPath.Contains(TEXT(".")))
    {
        FString AssetName;
        ObjectPath.Split(TEXT("/"), nullptr, &AssetName,
            ESearchCase::IgnoreCase, ESearchDir::FromEnd);
        FullPath = FString::Printf(TEXT("%s.%s"), *ObjectPath, *AssetName);
    }

    USkeletalMesh* SkeletalMesh = LoadObject<USkeletalMesh>(nullptr, *FullPath);
    if (!SkeletalMesh && FullPath != ObjectPath)
    {
        SkeletalMesh = LoadObject<USkeletalMesh>(nullptr, *ObjectPath);
    }
    if (!SkeletalMesh || !SkeletalMeshComponent)
    {
        return false;
    }

    ActiveRepresentationKind = ERepresentationKind::SkeletalMesh;
    SkeletalMeshComponent->SetSkeletalMesh(SkeletalMesh);
    SkeletalMeshComponent->SetVisibility(true, false);
    CacheOriginalMaterials();
    ApplyVisualVisibility(bVisualVisible);
    UE_LOG(LogTemp, Log, TEXT("[孪生体] ✅ SkeletalMesh 加载成功（参考姿势）: %s"), *FullPath);
    return true;
}

FString ATwinInstance::MakeBlueprintGeneratedClassPath(const FString& ObjectPath)
{
    FString Path = ObjectPath;
    Path.TrimStartAndEndInline();
    if (Path.StartsWith(TEXT("BlueprintGeneratedClass'")) || Path.StartsWith(TEXT("Blueprint'")))
    {
        int32 QuoteIndex = INDEX_NONE;
        if (Path.FindLastChar(TEXT('\''), QuoteIndex) && QuoteIndex == Path.Len() - 1)
        {
            const int32 FirstQuote = Path.Find(TEXT("'"));
            Path = Path.Mid(FirstQuote + 1, QuoteIndex - FirstQuote - 1);
        }
    }

    if (!Path.Contains(TEXT(".")))
    {
        FString AssetName;
        Path.Split(TEXT("/"), nullptr, &AssetName,
            ESearchCase::IgnoreCase, ESearchDir::FromEnd);
        Path = FString::Printf(TEXT("%s.%s"), *Path, *AssetName);
    }
    if (!Path.EndsWith(TEXT("_C")))
    {
        Path += TEXT("_C");
    }
    return Path;
}

bool ATwinInstance::IsSupportedBlueprintActorClass(const UClass* ActorClass, FString& OutReason)
{
    if (!ActorClass || !ActorClass->IsChildOf(AActor::StaticClass()))
    {
        OutReason = TEXT("生成类不是 AActor");
        return false;
    }
    if (ActorClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
    {
        OutReason = TEXT("生成类不可实例化或已废弃");
        return false;
    }
    if (ActorClass->IsChildOf(APawn::StaticClass())
        || ActorClass->IsChildOf(AController::StaticClass())
        || ActorClass->IsChildOf(ALevelScriptActor::StaticClass()))
    {
        OutReason = TEXT("Pawn、Controller 与 LevelScriptActor 不属于普通表现 Actor");
        return false;
    }
    OutReason = TEXT("Actor Blueprint");
    return true;
}

bool ATwinInstance::LoadBlueprintActorAsset(const FString& ObjectPath)
{
    if (!BlueprintActorComponent)
    {
        return false;
    }

    const FString GeneratedClassPath = MakeBlueprintGeneratedClassPath(ObjectPath);
    UClass* ActorClass = LoadObject<UClass>(nullptr, *GeneratedClassPath);
    FString ValidationReason;
    if (!IsSupportedBlueprintActorClass(ActorClass, ValidationReason))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[孪生体] Blueprint 生成类不可运行: input=%s generated_class=%s reason=%s"),
            *ObjectPath, *GeneratedClassPath, *ValidationReason);
        return false;
    }

    BlueprintActorComponent->SetRelativeTransform(FTransform::Identity);
    BlueprintActorComponent->SetChildActorClass(ActorClass);
    AActor* ChildActor = BlueprintActorComponent->GetChildActor();
    if (!ChildActor)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[孪生体] Blueprint 子 Actor 创建失败: %s"), *GeneratedClassPath);
        BlueprintActorComponent->SetChildActorClass(nullptr);
        return false;
    }

    ActiveRepresentationKind = ERepresentationKind::BlueprintActor;
    BlueprintActorComponent->SetVisibility(true, false);
    ApplyVisualVisibility(bVisualVisible);
    UE_LOG(LogTemp, Log,
        TEXT("[孪生体] ✅ Actor Blueprint 加载成功: %s child=%s"),
        *GeneratedClassPath, *ChildActor->GetName());
    return true;
}

UTwinRepresentationHostComponent* ATwinInstance::FindContainerHost(
    AActor* ChildActor,
    const FString& SlotName) const
{
    if (!ChildActor)
    {
        return nullptr;
    }

    const FName RequestedSlot = FName(
        SlotName.IsEmpty() ? TEXT("primary") : *SlotName);
    TInlineComponentArray<UTwinRepresentationHostComponent*> Hosts(ChildActor);
    for (UTwinRepresentationHostComponent* Host : Hosts)
    {
        if (Host && Host->SlotName == RequestedSlot)
        {
            return Host;
        }
    }
    return nullptr;
}

bool ATwinInstance::LoadContainerBlueprintRepresentation(
    const FString& BlueprintPath,
    const FString& SlotName,
    const FString& StaticMeshPath)
{
    ClearSingleAssetRepresentation();

    if (!BlueprintActorComponent
        || BlueprintPath.IsEmpty()
        || StaticMeshPath.IsEmpty())
    {
        UE_LOG(LogTemp, Error,
            TEXT("[孪生体] BP 容器参数不完整: id=%s container=%s asset=%s"),
            *InstanceId,
            *BlueprintPath,
            *StaticMeshPath);
        SetPlaceholderCube();
        return false;
    }

    if (!StaticMeshPath.StartsWith(TEXT("/Game/"))
        && !StaticMeshPath.StartsWith(TEXT("/Engine/")))
    {
        UE_LOG(LogTemp, Error,
            TEXT("[孪生体] BP 容器首版只支持 Cooked StaticMesh: %s"),
            *StaticMeshPath);
        SetPlaceholderCube();
        return false;
    }

    FString FullStaticMeshPath = StaticMeshPath;
    if (!FullStaticMeshPath.Contains(TEXT(".")))
    {
        FString AssetName;
        StaticMeshPath.Split(
            TEXT("/"),
            nullptr,
            &AssetName,
            ESearchCase::IgnoreCase,
            ESearchDir::FromEnd);
        FullStaticMeshPath = FString::Printf(
            TEXT("%s.%s"),
            *StaticMeshPath,
            *AssetName);
    }
    UStaticMesh* StaticMesh = LoadObject<UStaticMesh>(
        nullptr,
        *FullStaticMeshPath);
    if (!StaticMesh && FullStaticMeshPath != StaticMeshPath)
    {
        StaticMesh = LoadObject<UStaticMesh>(nullptr, *StaticMeshPath);
    }
    if (!StaticMesh)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[孪生体] BP 容器模型不是可用 StaticMesh: %s"),
            *StaticMeshPath);
        SetPlaceholderCube();
        return false;
    }

    const FString GeneratedClassPath = MakeBlueprintGeneratedClassPath(BlueprintPath);
    UClass* ActorClass = LoadObject<UClass>(nullptr, *GeneratedClassPath);
    FString ValidationReason;
    if (!IsSupportedBlueprintActorClass(ActorClass, ValidationReason))
    {
        UE_LOG(LogTemp, Error,
            TEXT("[孪生体] BP 容器不可运行: input=%s class=%s reason=%s"),
            *BlueprintPath,
            *GeneratedClassPath,
            *ValidationReason);
        SetPlaceholderCube();
        return false;
    }

    BlueprintActorComponent->SetRelativeTransform(FTransform::Identity);
    BlueprintActorComponent->SetChildActorClass(ActorClass);
    AActor* ChildActor = BlueprintActorComponent->GetChildActor();
    if (!ChildActor)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[孪生体] BP 容器子 Actor 创建失败: %s"),
            *GeneratedClassPath);
        BlueprintActorComponent->SetChildActorClass(nullptr);
        SetPlaceholderCube();
        return false;
    }

    UTwinRepresentationHostComponent* Host = FindContainerHost(
        ChildActor,
        SlotName);
    if (!Host)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[孪生体] BP 容器缺少匹配 HostComponent: container=%s slot=%s"),
            *BlueprintPath,
            *SlotName);
        BlueprintActorComponent->SetChildActorClass(nullptr);
        SetPlaceholderCube();
        return false;
    }

    FString ConfigureError;
    if (!Host->ConfigureStaticMesh(
        StaticMesh,
        InstanceId,
        GetTwinDisplayName(),
        StaticMeshPath,
        ConfigureError))
    {
        UE_LOG(LogTemp, Error,
            TEXT("[孪生体] BP 容器注入失败: container=%s slot=%s error=%s"),
            *BlueprintPath,
            *SlotName,
            *ConfigureError);
        BlueprintActorComponent->SetChildActorClass(nullptr);
        SetPlaceholderCube();
        return false;
    }

    ActiveContainerHost = Host;
    ActiveRepresentationKind = ERepresentationKind::ContainerBlueprint;
    BlueprintActorComponent->SetVisibility(true, false);
    Host->ForwardVisualState(CurrentMaterialVariant, bVisualVisible);
    Host->ForwardBehaviorState(CurrentAnimState, CurrentFxTrigger);
    ApplyVisualVisibility(bVisualVisible);
    UE_LOG(LogTemp, Log,
        TEXT("[孪生体] ✅ BP 容器注入成功: container=%s slot=%s asset=%s child=%s"),
        *GeneratedClassPath,
        *SlotName,
        *FullStaticMeshPath,
        *ChildActor->GetName());
    return true;
}

bool ATwinInstance::ReloadCurrentSingleRepresentation()
{
    if (AssetPath.IsEmpty())
    {
        SetPlaceholderCube();
        return false;
    }
    if (!ContainerBlueprintPath.IsEmpty())
    {
        return LoadContainerBlueprintRepresentation(
            ContainerBlueprintPath,
            ContainerSlot,
            AssetPath);
    }
    return LoadMeshFromPath(AssetPath);
}

bool ATwinInstance::IsContainerRepresentationActive() const
{
    return ActiveRepresentationKind == ERepresentationKind::ContainerBlueprint
        && ActiveContainerHost.IsValid();
}

UMeshComponent* ATwinInstance::GetActiveMeshComponent() const
{
    if (ActiveRepresentationKind == ERepresentationKind::SkeletalMesh)
    {
        return SkeletalMeshComponent;
    }
    if (ActiveRepresentationKind == ERepresentationKind::StaticMesh)
    {
        return MeshComponent;
    }
    return nullptr;
}

void ATwinInstance::ClearRenderParts()
{
    for (UStaticMeshComponent* PartComponent : RenderPartComponents)
    {
        if (PartComponent && IsValid(PartComponent))
        {
            RemoveInstanceComponent(PartComponent);
            PartComponent->DestroyComponent();
        }
    }
    RenderPartComponents.Empty();
    RenderPartSourceVisibility.Empty();
    CurrentAssemblySignature.Empty();
    CurrentRenderPartsStateHash.Empty();
    bAssemblyRenderActive = false;
}

bool ATwinInstance::GetRenderPartMaterialPaths(
    int32 PartIndex,
    TArray<FString>& OutPaths) const
{
    OutPaths.Reset();
    if (!RenderPartComponents.IsValidIndex(PartIndex))
    {
        return false;
    }

    const UStaticMeshComponent* PartComponent = RenderPartComponents[PartIndex];
    if (!PartComponent || !IsValid(PartComponent))
    {
        return false;
    }

    const int32 SlotCount = PartComponent->GetNumMaterials();
    OutPaths.Reserve(SlotCount);
    for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
    {
        const UMaterialInterface* Material = PartComponent->GetMaterial(SlotIndex);
        OutPaths.Add(Material ? Material->GetPathName() : FString());
    }
    return true;
}

bool ATwinInstance::GetSavedRenderPartMaterialPaths(
    int32 PartIndex,
    TArray<FString>& OutPaths) const
{
    OutPaths.Reset();
    if (!RenderPartComponents.IsValidIndex(PartIndex))
    {
        return false;
    }

    const UStaticMeshComponent* PartComponent = RenderPartComponents[PartIndex];
    const UStaticMesh* StaticMesh = PartComponent ? PartComponent->GetStaticMesh() : nullptr;
    if (!StaticMesh)
    {
        return false;
    }

    const TArray<FStaticMaterial>& StaticMaterials = StaticMesh->GetStaticMaterials();
    OutPaths.Reserve(StaticMaterials.Num());
    for (const FStaticMaterial& StaticMaterial : StaticMaterials)
    {
        const UMaterialInterface* Material = StaticMaterial.MaterialInterface;
        OutPaths.Add(Material ? Material->GetPathName() : FString());
    }
    return true;
}

bool ATwinInstance::GetRenderPartRelativeTransform(
    int32 PartIndex,
    FTransform& OutTransform) const
{
    if (!RenderPartComponents.IsValidIndex(PartIndex))
    {
        return false;
    }
    const UStaticMeshComponent* PartComponent = RenderPartComponents[PartIndex];
    if (!PartComponent || !IsValid(PartComponent))
    {
        return false;
    }
    OutTransform = PartComponent->GetRelativeTransform();
    return true;
}

void ATwinInstance::ApplyRenderPartsFromSnapshot(
    const TArray<TSharedPtr<FJsonValue>>& RenderParts,
    const FString& AssemblySignature)
{
    if (RenderParts.Num() == 0 || !MeshComponent)
    {
        return;
    }

    FString RenderStateJson;
    TSharedRef<TJsonWriter<>> StateWriter =
        TJsonWriterFactory<>::Create(&RenderStateJson);
    FJsonSerializer::Serialize(RenderParts, StateWriter);
    const FString EffectiveRenderStateHash = FMD5::HashAnsiString(*RenderStateJson);

    // assembly_signature 由导出器按“资产路径 + 相对母 Actor 变换”生成。
    // 对接旧的手写 payload 时没有 signature，仍构造一个稳定的轻量缓存键。
    FString EffectiveSignature = AssemblySignature;
    if (EffectiveSignature.IsEmpty())
    {
        for (const TSharedPtr<FJsonValue>& PartValue : RenderParts)
        {
            const TSharedPtr<FJsonObject>* PartObj = nullptr;
            if (!PartValue.IsValid() || !PartValue->TryGetObject(PartObj) || !PartObj || !PartObj->IsValid())
            {
                continue;
            }
            FString Asset;
            for (const TCHAR* Field : { TEXT("asset_path"), TEXT("ue_asset_path"),
                                        TEXT("static_mesh_asset"), TEXT("mesh_asset") })
            {
                if ((*PartObj)->TryGetStringField(Field, Asset) && !Asset.IsEmpty())
                {
                    break;
                }
            }
            EffectiveSignature += Asset;
            const TSharedPtr<FJsonObject>* TransformObj = nullptr;
            if ((*PartObj)->TryGetObjectField(TEXT("relative_transform"), TransformObj)
                || (*PartObj)->TryGetObjectField(TEXT("transform"), TransformObj))
            {
                FString TransformJson;
                TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&TransformJson);
                FJsonSerializer::Serialize((*TransformObj).ToSharedRef(), Writer);
                EffectiveSignature += TransformJson;
            }
            const TArray<TSharedPtr<FJsonValue>>* MaterialPaths = nullptr;
            if ((*PartObj)->TryGetArrayField(TEXT("material_paths"), MaterialPaths) && MaterialPaths)
            {
                for (const TSharedPtr<FJsonValue>& MaterialValue : *MaterialPaths)
                {
                    FString MaterialPath;
                    if (MaterialValue.IsValid() && MaterialValue->TryGetString(MaterialPath))
                    {
                        EffectiveSignature += MaterialPath;
                    }
                    EffectiveSignature += TEXT(",");
                }
            }
            for (const TCHAR* StateField : {
                TEXT("visible"), TEXT("hidden_in_game"), TEXT("cast_shadow") })
            {
                bool bState = false;
                if ((*PartObj)->TryGetBoolField(StateField, bState))
                {
                    EffectiveSignature += FString::Printf(TEXT("%s=%d;"), StateField, bState ? 1 : 0);
                }
            }
            FString CollisionEnabled;
            if ((*PartObj)->TryGetStringField(TEXT("collision_enabled"), CollisionEnabled))
            {
                EffectiveSignature += CollisionEnabled;
            }
            EffectiveSignature += TEXT("|");
        }
    }

    if (bAssemblyRenderActive
        && CurrentAssemblySignature == EffectiveSignature
        && CurrentRenderPartsStateHash == EffectiveRenderStateHash)
    {
        return;
    }

    ClearRenderParts();
    ClearSingleAssetRepresentation();
    MeshComponent->SetVisibility(true, false);
    MeshComponent->SetHiddenInGame(false, false);
    MeshComponent->SetStaticMesh(nullptr);

    int32 LoadedPartCount = 0;
    bool bHadLoadFailure = false;
    for (int32 PartIndex = 0; PartIndex < RenderParts.Num(); ++PartIndex)
    {
        const TSharedPtr<FJsonValue>& PartValue = RenderParts[PartIndex];
        const TSharedPtr<FJsonObject>* PartObj = nullptr;
        if (!PartValue.IsValid() || !PartValue->TryGetObject(PartObj) || !PartObj || !PartObj->IsValid())
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[孪生体] render_parts[%d] 不是对象 (ID=%s)"), PartIndex, *InstanceId);
            bHadLoadFailure = true;
            continue;
        }

        FString PartAssetPath;
        for (const TCHAR* Field : { TEXT("asset_path"), TEXT("ue_asset_path"),
                                    TEXT("static_mesh_asset"), TEXT("mesh_asset") })
        {
            if ((*PartObj)->TryGetStringField(Field, PartAssetPath) && !PartAssetPath.IsEmpty())
            {
                break;
            }
        }
        if (PartAssetPath.IsEmpty())
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[孪生体] render_parts[%d] 缺少 asset_path (ID=%s)"), PartIndex, *InstanceId);
            bHadLoadFailure = true;
            continue;
        }

        FString FullPath = PartAssetPath;
        if (!FullPath.Contains(TEXT(".")))
        {
            FString AssetName;
            PartAssetPath.Split(TEXT("/"), nullptr, &AssetName,
                ESearchCase::IgnoreCase, ESearchDir::FromEnd);
            FullPath = FString::Printf(TEXT("%s.%s"), *PartAssetPath, *AssetName);
        }
        UStaticMesh* PartMesh = LoadObject<UStaticMesh>(nullptr, *FullPath);
        if (!PartMesh)
        {
            PartMesh = LoadObject<UStaticMesh>(nullptr, *PartAssetPath);
        }
        if (!PartMesh)
        {
            UE_LOG(LogTemp, Error,
                TEXT("[孪生体] 复合部件资产加载失败: %s (ID=%s)"), *PartAssetPath, *InstanceId);
            bHadLoadFailure = true;
            continue;
        }

        FVector RelativeLocation = FVector::ZeroVector;
        FRotator RelativeRotation = FRotator::ZeroRotator;
        FVector RelativeScale = FVector::OneVector;
        const TSharedPtr<FJsonObject>* TransformObj = nullptr;
        if ((*PartObj)->TryGetObjectField(TEXT("relative_transform"), TransformObj)
            || (*PartObj)->TryGetObjectField(TEXT("transform"), TransformObj))
        {
            auto ReadNumber = [TransformObj](const TCHAR* ShortName, const TCHAR* LongName, double DefaultValue)
            {
                double Value = DefaultValue;
                if (!(*TransformObj)->TryGetNumberField(ShortName, Value))
                {
                    (*TransformObj)->TryGetNumberField(LongName, Value);
                }
                return Value;
            };
            RelativeLocation = FVector(
                ReadNumber(TEXT("tx"), TEXT("translation_x"), 0.0),
                ReadNumber(TEXT("ty"), TEXT("translation_y"), 0.0),
                ReadNumber(TEXT("tz"), TEXT("translation_z"), 0.0));
            const double Roll = ReadNumber(TEXT("rx"), TEXT("rotation_x"), 0.0);
            const double Pitch = ReadNumber(TEXT("ry"), TEXT("rotation_y"), 0.0);
            const double Yaw = ReadNumber(TEXT("rz"), TEXT("rotation_z"), 0.0);
            RelativeRotation = FRotator(Pitch, Yaw, Roll);
            RelativeScale = FVector(
                ReadNumber(TEXT("sx"), TEXT("scale_x"), 1.0),
                ReadNumber(TEXT("sy"), TEXT("scale_y"), 1.0),
                ReadNumber(TEXT("sz"), TEXT("scale_z"), 1.0));
        }

        const FName PartComponentName = MakeUniqueObjectName(
            this,
            UStaticMeshComponent::StaticClass(),
            FName(*FString::Printf(TEXT("TwinRenderPart_%d"), PartIndex)));
        UStaticMeshComponent* PartComponent = NewObject<UStaticMeshComponent>(
            this,
            PartComponentName,
            RF_Transient);
        if (!PartComponent)
        {
            bHadLoadFailure = true;
            continue;
        }
        AddInstanceComponent(PartComponent);
        PartComponent->SetupAttachment(MeshComponent);
        PartComponent->SetMobility(EComponentMobility::Movable);
        PartComponent->SetStaticMesh(PartMesh);
        PartComponent->SetRelativeTransform(FTransform(RelativeRotation, RelativeLocation, RelativeScale));

        const TArray<TSharedPtr<FJsonValue>>* MaterialPaths = nullptr;
        if ((*PartObj)->TryGetArrayField(TEXT("material_paths"), MaterialPaths) && MaterialPaths)
        {
            for (int32 MaterialIndex = 0; MaterialIndex < MaterialPaths->Num(); ++MaterialIndex)
            {
                FString MaterialPath;
                if (!(*MaterialPaths)[MaterialIndex].IsValid()
                    || !(*MaterialPaths)[MaterialIndex]->TryGetString(MaterialPath)
                    || MaterialPath.IsEmpty())
                {
                    continue;
                }
                UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
                if (Material)
                {
                    PartComponent->SetMaterial(MaterialIndex, Material);
                }
                else
                {
                    // A missing optional material must not collapse the whole
                    // assembly to the placeholder cube.  The mesh is still a
                    // valid renderable part and UE will use its authored
                    // material/default surface until the material is available.
                    UE_LOG(LogTemp, Warning,
                        TEXT("[孪生体] 复合部件材质加载失败: %s (ID=%s, part=%d, slot=%d)"),
                        *MaterialPath, *InstanceId, PartIndex, MaterialIndex);
                }
            }
        }

        bool bPartVisible = true;
        bool bPartHiddenInGame = false;
        bool bPartCastShadow = true;
        (*PartObj)->TryGetBoolField(TEXT("visible"), bPartVisible);
        (*PartObj)->TryGetBoolField(TEXT("hidden_in_game"), bPartHiddenInGame);
        (*PartObj)->TryGetBoolField(TEXT("cast_shadow"), bPartCastShadow);
        PartComponent->SetVisibility(bPartVisible, false);
        PartComponent->SetHiddenInGame(bPartHiddenInGame, false);
        PartComponent->SetCastShadow(bPartCastShadow);
        ECollisionEnabled::Type CollisionEnabled = PartComponent->GetCollisionEnabled();
        if (TryParseCollisionEnabled(*PartObj, CollisionEnabled))
        {
            PartComponent->SetCollisionEnabled(CollisionEnabled);
        }
        PartComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
        PartComponent->RegisterComponent();
        RenderPartComponents.Add(PartComponent);
        RenderPartSourceVisibility.Add(bPartVisible);
        ++LoadedPartCount;
    }

    bAssemblyRenderActive = true;
    if (!bHadLoadFailure && LoadedPartCount == RenderParts.Num())
    {
        CurrentAssemblySignature = EffectiveSignature;
        CurrentRenderPartsStateHash = EffectiveRenderStateHash;
        UE_LOG(LogTemp, Log,
            TEXT("[孪生体] assembly_v1 已创建 %d/%d 个渲染部件 (ID=%s, signature=%s)"),
            LoadedPartCount, RenderParts.Num(), *InstanceId, *CurrentAssemblySignature);
    }
    else
    {
        // 不缓存失败签名：下一次快照轮询会重新加载。基座 Cube 明确提示该 assembly 不完整。
        CurrentAssemblySignature.Empty();
        CurrentRenderPartsStateHash.Empty();
        SetPlaceholderCube();
        UE_LOG(LogTemp, Error,
            TEXT("[孪生体] assembly_v1 仅加载 %d/%d 个部件，保留 Cube fallback 并等待重试 (ID=%s)"),
            LoadedPartCount, RenderParts.Num(), *InstanceId);
    }
}

// ─── 运行时 glb/gltf 加载（glTFRuntime，模型不参与打包） ──────────────────────

// 固定模型目录的默认值：编辑器与打包 exe 都读这一份，永不拷贝。
// 可在 项目 Config/DefaultGame.ini 用 [OntoTwinSync] ModelsDir=... 覆盖，无需重编译。
// assembly_v1 部件状态只读审计。
bool ATwinInstance::ValidateRenderPartsAgainstSnapshot(
    const TArray<TSharedPtr<FJsonValue>>& RenderParts,
    bool bOverallVisible,
    TArray<TSharedPtr<FJsonValue>>& OutFailures) const
{
    OutFailures.Reset();

    if (RenderPartComponents.Num() != RenderParts.Num())
    {
        AddAssemblyAuditFailure(
            OutFailures,
            INDEX_NONE,
            TEXT("render_part_count"),
            MakeShared<FJsonValueNumber>(RenderParts.Num()),
            MakeShared<FJsonValueNumber>(RenderPartComponents.Num()));
        // 组件数组只保留加载成功项；一旦数量不同，下标就不再可靠对齐。
        return false;
    }

    // 全部载入成功时 RenderPartComponents 与 render_parts 严格同序。
    constexpr double LocationToleranceCm = 0.01;
    constexpr double RotationToleranceDegrees = 0.01;
    constexpr double ScaleTolerance = 0.00001;

    for (int32 PartIndex = 0; PartIndex < RenderParts.Num(); ++PartIndex)
    {
        const TSharedPtr<FJsonValue>& PartValue = RenderParts[PartIndex];
        const TSharedPtr<FJsonObject>* PartObjPtr = nullptr;
        if (!PartValue.IsValid()
            || !PartValue->TryGetObject(PartObjPtr)
            || !PartObjPtr
            || !PartObjPtr->IsValid())
        {
            AddAssemblyAuditFailure(
                OutFailures,
                PartIndex,
                TEXT("render_part"),
                MakeShared<FJsonValueString>(TEXT("json_object")),
                MakeShared<FJsonValueString>(TEXT("invalid_or_non_object")));
            continue;
        }
        const TSharedPtr<FJsonObject>& PartObj = *PartObjPtr;

        const UStaticMeshComponent* PartComponent = RenderPartComponents[PartIndex];
        if (!PartComponent || !IsValid(PartComponent))
        {
            AddAssemblyAuditFailure(
                OutFailures,
                PartIndex,
                TEXT("component"),
                MakeShared<FJsonValueString>(TEXT("valid_static_mesh_component")),
                MakeShared<FJsonValueString>(TEXT("null_or_invalid")));
            continue;
        }

        FString ExpectedMeshPath;
        for (const TCHAR* Field : { TEXT("asset_path"), TEXT("ue_asset_path"),
                                    TEXT("static_mesh_asset"), TEXT("mesh_asset") })
        {
            if (PartObj->TryGetStringField(Field, ExpectedMeshPath) && !ExpectedMeshPath.IsEmpty())
            {
                break;
            }
        }
        const FString ActualMeshPath = PartComponent->GetStaticMesh()
            ? PartComponent->GetStaticMesh()->GetPathName()
            : FString();
        if (ExpectedMeshPath != ActualMeshPath)
        {
            AddAssemblyAuditFailure(
                OutFailures,
                PartIndex,
                TEXT("mesh_asset_path"),
                MakeShared<FJsonValueString>(ExpectedMeshPath),
                MakeShared<FJsonValueString>(ActualMeshPath));
        }

        FVector ExpectedLocation = FVector::ZeroVector;
        FRotator ExpectedRotation = FRotator::ZeroRotator;
        FVector ExpectedScale = FVector::OneVector;
        const TSharedPtr<FJsonObject>* TransformObjPtr = nullptr;
        bool bTransformSchemaValid = true;
        if (PartObj->TryGetObjectField(TEXT("relative_transform"), TransformObjPtr)
            || PartObj->TryGetObjectField(TEXT("transform"), TransformObjPtr))
        {
            const TSharedPtr<FJsonObject>& TransformObj = *TransformObjPtr;
            auto ReadNumber = [&TransformObj, &bTransformSchemaValid](
                const TCHAR* ShortName,
                const TCHAR* LongName,
                double DefaultValue)
            {
                double Value = DefaultValue;
                const bool bFound = TransformObj->TryGetNumberField(ShortName, Value)
                    || TransformObj->TryGetNumberField(LongName, Value);
                if (!bFound || !FMath::IsFinite(Value))
                {
                    bTransformSchemaValid = false;
                    return DefaultValue;
                }
                return Value;
            };
            ExpectedLocation = FVector(
                ReadNumber(TEXT("tx"), TEXT("translation_x"), 0.0),
                ReadNumber(TEXT("ty"), TEXT("translation_y"), 0.0),
                ReadNumber(TEXT("tz"), TEXT("translation_z"), 0.0));
            const double Roll = ReadNumber(TEXT("rx"), TEXT("rotation_x"), 0.0);
            const double Pitch = ReadNumber(TEXT("ry"), TEXT("rotation_y"), 0.0);
            const double Yaw = ReadNumber(TEXT("rz"), TEXT("rotation_z"), 0.0);
            ExpectedRotation = FRotator(Pitch, Yaw, Roll);
            ExpectedScale = FVector(
                ReadNumber(TEXT("sx"), TEXT("scale_x"), 1.0),
                ReadNumber(TEXT("sy"), TEXT("scale_y"), 1.0),
                ReadNumber(TEXT("sz"), TEXT("scale_z"), 1.0));
        }
        else
        {
            bTransformSchemaValid = false;
        }
        if (!bTransformSchemaValid)
        {
            AddAssemblyAuditFailure(
                OutFailures,
                PartIndex,
                TEXT("relative_transform_schema"),
                MakeShared<FJsonValueString>(TEXT("nine_finite_numeric_fields")),
                MakeShared<FJsonValueString>(TEXT("missing_or_non_finite")));
        }

        const FVector ActualLocation = PartComponent->GetRelativeLocation();
        if (!ActualLocation.Equals(ExpectedLocation, LocationToleranceCm))
        {
            AddAssemblyAuditFailure(
                OutFailures,
                PartIndex,
                TEXT("relative_location_cm"),
                JsonVectorValue(ExpectedLocation),
                JsonVectorValue(ActualLocation),
                LocationToleranceCm);
        }

        const FQuat ExpectedQuat = ExpectedRotation.Quaternion().GetNormalized();
        const FQuat ActualQuat = PartComponent->GetRelativeRotation().Quaternion().GetNormalized();
        const bool bActualQuatFinite = !ActualQuat.ContainsNaN();
        double AngularErrorDegrees = -1.0;
        if (bActualQuatFinite)
        {
            const double QuaternionDot = FMath::Clamp(
                FMath::Abs(static_cast<double>(ExpectedQuat | ActualQuat)),
                0.0,
                1.0);
            AngularErrorDegrees = FMath::RadiansToDegrees(2.0 * FMath::Acos(QuaternionDot));
        }
        if (!bActualQuatFinite || AngularErrorDegrees > RotationToleranceDegrees)
        {
            AddAssemblyAuditFailure(
                OutFailures,
                PartIndex,
                TEXT("relative_rotation_degrees"),
                JsonRotatorValue(ExpectedRotation),
                bActualQuatFinite
                    ? JsonRotatorValue(PartComponent->GetRelativeRotation())
                    : MakeShared<FJsonValueString>(TEXT("non_finite")),
                RotationToleranceDegrees);
            TSharedPtr<FJsonObject> FailureObject = OutFailures.Last()->AsObject();
            if (FailureObject.IsValid() && bActualQuatFinite)
            {
                FailureObject->SetNumberField(TEXT("angular_error_degrees"), AngularErrorDegrees);
            }
        }

        const FVector ActualScale = PartComponent->GetRelativeScale3D();
        if (!ActualScale.Equals(ExpectedScale, ScaleTolerance))
        {
            AddAssemblyAuditFailure(
                OutFailures,
                PartIndex,
                TEXT("relative_scale"),
                JsonVectorValue(ExpectedScale),
                JsonVectorValue(ActualScale),
                ScaleTolerance);
        }

        const TArray<TSharedPtr<FJsonValue>>* MaterialPaths = nullptr;
        if (PartObj->TryGetArrayField(TEXT("material_paths"), MaterialPaths) && MaterialPaths)
        {
            if (PartComponent->GetNumMaterials() != MaterialPaths->Num())
            {
                AddAssemblyAuditFailure(
                    OutFailures,
                    PartIndex,
                    TEXT("material_slot_count"),
                    MakeShared<FJsonValueNumber>(MaterialPaths->Num()),
                    MakeShared<FJsonValueNumber>(PartComponent->GetNumMaterials()));
            }
            for (int32 MaterialIndex = 0; MaterialIndex < MaterialPaths->Num(); ++MaterialIndex)
            {
                FString ExpectedMaterialPath;
                if ((*MaterialPaths)[MaterialIndex].IsValid())
                {
                    (*MaterialPaths)[MaterialIndex]->TryGetString(ExpectedMaterialPath);
                }
                const UMaterialInterface* ActualMaterial = PartComponent->GetMaterial(MaterialIndex);
                const FString ActualMaterialPath = ActualMaterial ? ActualMaterial->GetPathName() : FString();
                if (ExpectedMaterialPath != ActualMaterialPath)
                {
                    AddAssemblyAuditFailure(
                        OutFailures,
                        PartIndex,
                        TEXT("material_asset_path"),
                        MakeShared<FJsonValueString>(ExpectedMaterialPath),
                        MakeShared<FJsonValueString>(ActualMaterialPath),
                        -1.0,
                        MaterialIndex);
                }
            }
        }
        else
        {
            AddAssemblyAuditFailure(
                OutFailures,
                PartIndex,
                TEXT("material_paths"),
                MakeShared<FJsonValueString>(TEXT("json_array")),
                MakeShared<FJsonValueString>(TEXT("missing_or_invalid")));
        }

        bool bPartVisible = true;
        bool bPartHiddenInGame = false;
        bool bPartCastShadow = true;
        PartObj->TryGetBoolField(TEXT("visible"), bPartVisible);
        PartObj->TryGetBoolField(TEXT("hidden_in_game"), bPartHiddenInGame);
        PartObj->TryGetBoolField(TEXT("cast_shadow"), bPartCastShadow);

        const bool bExpectedVisible = bOverallVisible && bPartVisible;
        if (PartComponent->GetVisibleFlag() != bExpectedVisible)
        {
            AddAssemblyAuditFailure(
                OutFailures,
                PartIndex,
                TEXT("visible"),
                MakeShared<FJsonValueBoolean>(bExpectedVisible),
                MakeShared<FJsonValueBoolean>(PartComponent->GetVisibleFlag()));
        }
        if (static_cast<bool>(PartComponent->bHiddenInGame) != bPartHiddenInGame)
        {
            AddAssemblyAuditFailure(
                OutFailures,
                PartIndex,
                TEXT("hidden_in_game"),
                MakeShared<FJsonValueBoolean>(bPartHiddenInGame),
                MakeShared<FJsonValueBoolean>(
                    static_cast<bool>(PartComponent->bHiddenInGame)));
        }
        if (static_cast<bool>(PartComponent->CastShadow) != bPartCastShadow)
        {
            AddAssemblyAuditFailure(
                OutFailures,
                PartIndex,
                TEXT("cast_shadow"),
                MakeShared<FJsonValueBoolean>(bPartCastShadow),
                MakeShared<FJsonValueBoolean>(
                    static_cast<bool>(PartComponent->CastShadow)));
        }

        ECollisionEnabled::Type ExpectedCollision = ECollisionEnabled::NoCollision;
        if (!TryParseCollisionEnabled(PartObj, ExpectedCollision))
        {
            AddAssemblyAuditFailure(
                OutFailures,
                PartIndex,
                TEXT("collision_enabled"),
                MakeShared<FJsonValueString>(TEXT("valid_collision_mode")),
                MakeShared<FJsonValueString>(TEXT("missing_or_invalid_snapshot_value")));
        }
        else if (PartComponent->GetCollisionEnabled() != ExpectedCollision)
        {
            AddAssemblyAuditFailure(
                OutFailures,
                PartIndex,
                TEXT("collision_enabled"),
                MakeShared<FJsonValueString>(CollisionEnabledToString(ExpectedCollision)),
                MakeShared<FJsonValueString>(CollisionEnabledToString(PartComponent->GetCollisionEnabled())));
        }
    }

    return OutFailures.Num() == 0;
}

// 从此处起为运行时 glb/gltf 加载实现。
static const TCHAR* kDefaultModelsDir = TEXT("D:/SCC/DigitalFactoryBase_SCC/Models");

static FString FindVersionedModelFile(const FString& Directory, const FString& FileName)
{
    // ArtStudio 预取文件带版本号：{id}_v{n}.glb。
    // 用户在路径直连里常会填 {id}.glb，这里在同目录下做一次兼容查找。
    if (!FileName.EndsWith(TEXT(".glb")))
    {
        return FString();
    }

    const FString Stem = FPaths::GetBaseFilename(FileName);
    if (!Stem.IsNumeric())
    {
        return FString();
    }

    TArray<FString> Found;
    IFileManager::Get().FindFiles(
        Found,
        *FPaths::Combine(Directory, FString::Printf(TEXT("%s_v*.glb"), *Stem)),
        true,
        false);

    if (Found.Num() == 0)
    {
        return FString();
    }

    Found.Sort();
    return FPaths::ConvertRelativePathToFull(FPaths::Combine(Directory, Found.Last()));
}

FString ATwinInstance::ResolveModelFilePath(const FString& AssetId) const
{
    // 在候选目录里找 <file>，返回第一个存在的；都没有则返回空串。
    // 优先级：① 固定目录（ini 可配，默认 kDefaultModelsDir）—— 编辑器/打包共用，零拷贝；
    //         ②③④ exe 相对的 Models/（兜底，兼容把模型丢在包旁的情况）。
    // 后续若改为后端 HTTP 下发，只需替换本函数（glTFRuntime 亦支持 URL 加载）。
    FString FileName = AssetId;
    if (!FileName.EndsWith(TEXT(".glb")) && !FileName.EndsWith(TEXT(".gltf")))
    {
        FileName += TEXT(".glb");
    }

    // ① 固定目录：先读 ini 配置，没配则用默认
    FString FixedDir;
    if (!GConfig || !GConfig->GetString(TEXT("OntoTwinSync"), TEXT("ModelsDir"), FixedDir, GGameIni) || FixedDir.IsEmpty())
    {
        FixedDir = kDefaultModelsDir;
    }

    const FString ProjDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    const FString ExeDir  = FPaths::ConvertRelativePathToFull(FPlatformProcess::BaseDir());

    // 候选 1：固定目录直接拼文件名（FixedDir 已是 Models 根，不再追加 Models）
    TArray<FString> Candidates;
    Candidates.Add(FPaths::Combine(FixedDir, FileName));
    // 候选 2~5：exe 相对的 Models/ 兜底
    Candidates.Add(FPaths::Combine(ProjDir, TEXT("Models"), FileName));
    Candidates.Add(FPaths::Combine(ProjDir, TEXT(".."), TEXT("Models"), FileName));
    Candidates.Add(FPaths::Combine(ExeDir, TEXT("Models"), FileName));
    Candidates.Add(FPaths::Combine(ExeDir, TEXT(".."), TEXT(".."), TEXT(".."), TEXT("Models"), FileName));

    for (const FString& Raw : Candidates)
    {
        const FString Candidate = FPaths::ConvertRelativePathToFull(Raw);
        const bool bExists = FPaths::FileExists(Candidate);
        UE_LOG(LogTemp, Log, TEXT("[孪生体] 候选模型路径 %s : %s"),
               bExists ? TEXT("✅命中") : TEXT("✗未找到"), *Candidate);
        if (bExists)
        {
            return Candidate;
        }

        const FString VersionedCandidate = FindVersionedModelFile(FPaths::GetPath(Candidate), FileName);
        if (!VersionedCandidate.IsEmpty())
        {
            UE_LOG(LogTemp, Log, TEXT("[孪生体] 版本模型路径命中: %s -> %s"), *FileName, *VersionedCandidate);
            return VersionedCandidate;
        }
    }

    return FString();  // 全部未命中
}

bool ATwinInstance::LoadRuntimeGltf(const FString& AssetId)
{
    const FString FilePath = ResolveModelFilePath(AssetId);
    if (FilePath.IsEmpty())
    {
        UE_LOG(LogTemp, Warning,
               TEXT("[孪生体] 所有候选目录都没找到模型 '%s'，请把 Models/ 放到上面日志列出的任一目录"),
               *AssetId);
        return false;
    }
    return LoadGltfFromFile(FilePath);
}

bool ATwinInstance::LoadGltfFromFile(const FString& FilePath)
{
    UE_LOG(LogTemp, Log, TEXT("[孪生体] 运行时加载 glb: %s"), *FilePath);

    // glTF 以米为单位，UE 以厘米，SceneScale=100 做单位换算
    FglTFRuntimeConfig LoaderConfig;
    LoaderConfig.TransformBaseType = EglTFRuntimeTransformBaseType::Default;
    LoaderConfig.SceneScale = 100.0f;

    UglTFRuntimeAsset* Asset =
        UglTFRuntimeFunctionLibrary::glTFLoadAssetFromFilename(FilePath, false, LoaderConfig);
    if (!Asset)
    {
        UE_LOG(LogTemp, Error, TEXT("[孪生体] glb 解析失败: %s"), *FilePath);
        return false;
    }

    // 把整个默认场景的所有静态网格递归合并成一个 StaticMesh
    FglTFRuntimeStaticMeshConfig MeshConfig;
    MeshConfig.bBuildSimpleCollision = true;
    MeshConfig.NormalsGenerationStrategy = EglTFRuntimeNormalsGenerationStrategy::IfMissing;
    MeshConfig.TangentsGenerationStrategy = EglTFRuntimeTangentsGenerationStrategy::IfMissing;

    UStaticMesh* Mesh = Asset->LoadStaticMeshRecursive(FString(), TArray<FString>(), MeshConfig);
    if (!Mesh)
    {
        UE_LOG(LogTemp, Error, TEXT("[孪生体] glb 网格生成失败: %s"), *FilePath);
        return false;
    }

    ClearSingleAssetRepresentation();
    ActiveRepresentationKind = ERepresentationKind::StaticMesh;
    MeshComponent->SetVisibility(true, false);
    MeshComponent->SetStaticMesh(Mesh);
    CacheOriginalMaterials();
    ApplyVisualVisibility(bVisualVisible);

    // ── 材质诊断（定位打包后灰白无贴图）─────────────────────────────
    // 槽0 基材若是 M_glTFRuntime* → 材质正常,问题在贴图;若是默认/BasicShape → 材质回退(shader没编)
    const int32 NumMats = MeshComponent->GetNumMaterials();
    UE_LOG(LogTemp, Warning, TEXT("[材质诊断] %s 材质槽数=%d"),
           *FPaths::GetCleanFilename(FilePath), NumMats);
    for (int32 i = 0; i < NumMats && i < 4; ++i)
    {
        UMaterialInterface* M = MeshComponent->GetMaterial(i);
        UMaterialInterface* Base = M ? M->GetBaseMaterial() : nullptr;
        UE_LOG(LogTemp, Warning, TEXT("[材质诊断]   槽%d 材质=%s 基材=%s"),
               i,
               M ? *M->GetName() : TEXT("NULL"),
               Base ? *Base->GetName() : TEXT("?"));
    }

    UE_LOG(LogTemp, Log, TEXT("[孪生体] ✅ glb 加载成功: %s"), *FilePath);
    OnRepresentationVisualReady.Broadcast(this);
    return true;
}

void ATwinInstance::SetPlaceholderCube()
{
    ClearSingleAssetRepresentation();
    UStaticMesh* DefaultMesh = LoadObject<UStaticMesh>(
        nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (DefaultMesh)
    {
        ActiveRepresentationKind = ERepresentationKind::StaticMesh;
        MeshComponent->SetVisibility(true, false);
        MeshComponent->SetStaticMesh(DefaultMesh);
        CacheOriginalMaterials();
        ApplyVisualVisibility(bVisualVisible);
    }
}

void ATwinInstance::PurgeOldCacheVersions(const FString& AssetId, const FString& KeepFile)
{
    const FString CacheDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ModelCache"));
    const FString KeepName = FPaths::GetCleanFilename(KeepFile);
    TArray<FString> Found;
    IFileManager::Get().FindFiles(Found, *FPaths::Combine(CacheDir, AssetId + TEXT("_v*.glb")), true, false);
    for (const FString& F : Found)
    {
        if (F != KeepName)
        {
            IFileManager::Get().Delete(*FPaths::Combine(CacheDir, F));
            UE_LOG(LogTemp, Log, TEXT("[孪生体] 清理旧版本缓存: %s"), *F);
        }
    }
}

// ─── ArtStudio 远程加载：命中缓存即时加载，否则占位 Cube + 异步下载（3.3）──────
void ATwinInstance::LoadRemoteGltf(const FString& StableId)
{
    // 解析 artstudio:{id}:v{n}
    FString Rest = StableId;
    Rest.RemoveFromStart(TEXT("artstudio:"));
    FString AssetIdPart, VersionPart;
    if (!Rest.Split(TEXT(":v"), &AssetIdPart, &VersionPart))
    {
        AssetIdPart = Rest;           // 容错：无版本段
        VersionPart = TEXT("0");
    }

    if (AssetIdPart.IsEmpty() || !AssetIdPart.IsNumeric()
        || VersionPart.IsEmpty() || !VersionPart.IsNumeric())
    {
        UE_LOG(LogTemp, Error, TEXT("[孪生体] 非法 ArtStudio 标识: %s"), *StableId);
        SetPlaceholderCube();
        return;
    }

    if (RemoteRetryId != StableId)
    {
        RemoteRetryId = StableId;
        RemoteRetryAttempt = 0;
    }

    // 缓存文件：Saved/ModelCache/{id}_v{n}.glb —— 版本进文件名，升版自动失效重下
    const FString CacheDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ModelCache"));
    const FString CacheFile = FPaths::Combine(
        CacheDir, FString::Printf(TEXT("%s_v%s.glb"), *AssetIdPart, *VersionPart));

    // ① 命中缓存 → 直接加载
    if (FPaths::FileExists(CacheFile))
    {
        UE_LOG(LogTemp, Log, TEXT("[孪生体] ArtStudio 缓存命中: %s"), *CacheFile);
        if (LoadGltfFromFile(CacheFile))
        {
            PendingRemoteId.Empty();
            RemoteRetryAttempt = 0;
            return;
        }

        // 旧版本可能留下半截或不可解析文件；删除后走正常下载重试。
        IFileManager::Get().Delete(*CacheFile, false, true);
        UE_LOG(LogTemp, Warning, TEXT("[孪生体] 已删除损坏的 ArtStudio 缓存: %s"), *CacheFile);
    }

    // ② 缓存缺失 → 占位 Cube + 工程进程内共享异步下载
    if (PendingRemoteId == StableId)
    {
        return;
    }
    SetPlaceholderCube();
    // Use the same configured identity as snapshots and heartbeats, never a
    // project-filename-derived identity. Older placed instances may have no owner.
    ATwinSceneManager* Manager = Cast<ATwinSceneManager>(GetOwner());
    if (!Manager && GetWorld())
    {
        for (TActorIterator<ATwinSceneManager> It(GetWorld()); It; ++It)
        {
            if (It->BackendBaseUrl != BackendBaseUrl) continue;
            if (Manager)
            {
                UE_LOG(LogTemp, Error, TEXT("[孪生体] ArtStudio 下载身份不明确：存在多个管理器"));
                ScheduleRemoteGltfRetry(StableId);
                return;
            }
            Manager = *It;
        }
    }
    if (!Manager || Manager->UEProjectId.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[孪生体] 等待管理器项目身份后下载 ArtStudio 模型"));
        ScheduleRemoteGltfRetry(StableId);
        return;
    }
    const FString UEProjectId = Manager->UEProjectId;
    const FString UEProjectName = Manager->UEProjectName;
    const FString DownloadKey = BackendBaseUrl + TEXT("|") + UEProjectId + TEXT("|") + CacheFile;
    PendingRemoteId = StableId;

    IFileManager::Get().MakeDirectory(*CacheDir, /*Tree=*/true);

    bool bShouldStartDownload = false;
    {
        FScopeLock Lock(&GArtStudioDownloadGuard);
        TArray<TWeakObjectPtr<ATwinInstance>>& Waiters = GArtStudioDownloadWaiters.FindOrAdd(DownloadKey);
        const bool bAlreadyWaiting = Waiters.ContainsByPredicate(
            [this](const TWeakObjectPtr<ATwinInstance>& Waiter)
            {
                return Waiter.Get() == this;
            });
        if (!bAlreadyWaiting)
        {
            Waiters.Add(TWeakObjectPtr<ATwinInstance>(this));
        }
        if (!GArtStudioDownloadsInFlight.Contains(DownloadKey))
        {
            GArtStudioDownloadsInFlight.Add(DownloadKey);
            bShouldStartDownload = true;
        }
    }

    if (!bShouldStartDownload)
    {
        UE_LOG(LogTemp, Verbose, TEXT("[孪生体] 复用 ArtStudio 在途下载: %s"), *StableId);
        return;
    }

    const FString Url = FString::Printf(
        TEXT("%s/api/v2/assets/download?id=%s&version=%s"),
        *BackendBaseUrl,
        *AssetIdPart,
        *VersionPart);
    UE_LOG(LogTemp, Log, TEXT("[孪生体] ArtStudio 下载: %s | 缓存: %s"), *Url, *CacheFile);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Url);
    Request->SetVerb(TEXT("GET"));
    Request->SetTimeout(180.0f);
    Request->SetHeader(TEXT("X-OntoTwin-UE-Project-Id"), UEProjectId);
    Request->SetHeader(TEXT("X-OntoTwin-UE-Project-Name"), UEProjectName);

    const FString ExpectId = StableId;
    const FString AssetIdForPurge = AssetIdPart;
    const FString TempFile = CacheFile + TEXT(".part");
    Request->OnProcessRequestComplete().BindLambda(
        [DownloadKey, CacheFile, TempFile, ExpectId, AssetIdForPurge](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bOk)
        {
            bool bCacheReady = false;
            FString FailureReason;
            const int32 ResponseCode = Resp.IsValid() ? Resp->GetResponseCode() : -1;

            if (!bOk || !Resp.IsValid() || ResponseCode != 200)
            {
                FailureReason = FString::Printf(TEXT("HTTP %d"), ResponseCode);
                if (Resp.IsValid())
                {
                    TSharedPtr<FJsonObject> ErrorBody;
                    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Resp->GetContentAsString());
                    if (FJsonSerializer::Deserialize(Reader, ErrorBody) && ErrorBody.IsValid())
                    {
                        FString ErrorCode;
                        if (ErrorBody->TryGetStringField(TEXT("error"), ErrorCode))
                        {
                            FailureReason += TEXT(" / ") + ErrorCode.Left(160);
                        }
                    }
                }
            }
            else if (!IsValidGlbPayload(Resp->GetContent()))
            {
                FailureReason = TEXT("GLB 校验失败");
            }
            else
            {
                IFileManager::Get().Delete(*TempFile, false, true);
                if (!FFileHelper::SaveArrayToFile(Resp->GetContent(), *TempFile))
                {
                    FailureReason = TEXT("临时缓存写入失败");
                }
                else
                {
                    IFileManager::Get().Delete(*CacheFile, false, true);
                    bCacheReady = IFileManager::Get().Move(
                        *CacheFile,
                        *TempFile,
                        true,
                        true);
                    if (!bCacheReady)
                    {
                        FailureReason = TEXT("缓存原子替换失败");
                        IFileManager::Get().Delete(*TempFile, false, true);
                    }
                }
            }

            TArray<TWeakObjectPtr<ATwinInstance>> Waiters;
            {
                FScopeLock Lock(&GArtStudioDownloadGuard);
                if (TArray<TWeakObjectPtr<ATwinInstance>>* Found = GArtStudioDownloadWaiters.Find(DownloadKey))
                {
                    Waiters = MoveTemp(*Found);
                }
                GArtStudioDownloadWaiters.Remove(DownloadKey);
                GArtStudioDownloadsInFlight.Remove(DownloadKey);
            }

            if (!bCacheReady)
            {
                UE_LOG(LogTemp, Error, TEXT("[孪生体] ArtStudio 下载失败: %s (%s)"), *ExpectId, *FailureReason);
            }

            for (const TWeakObjectPtr<ATwinInstance>& WeakWaiter : Waiters)
            {
                ATwinInstance* Waiter = WeakWaiter.Get();
                if (!Waiter)
                {
                    continue;
                }
                if (Waiter->PendingRemoteId == ExpectId)
                {
                    Waiter->PendingRemoteId.Empty();
                }
                if (Waiter->AssetPath != ExpectId)
                {
                    continue;
                }

                if (bCacheReady && Waiter->LoadGltfFromFile(CacheFile))
                {
                    Waiter->RemoteRetryAttempt = 0;
                    Waiter->PurgeOldCacheVersions(AssetIdForPurge, CacheFile);
                }
                else
                {
                    if (bCacheReady)
                    {
                        IFileManager::Get().Delete(*CacheFile, false, true);
                    }
                    Waiter->SetPlaceholderCube();
                    Waiter->ScheduleRemoteGltfRetry(ExpectId);
                }
            }
        });

    if (!Request->ProcessRequest())
    {
        TArray<TWeakObjectPtr<ATwinInstance>> Waiters;
        {
            FScopeLock Lock(&GArtStudioDownloadGuard);
            if (TArray<TWeakObjectPtr<ATwinInstance>>* Found = GArtStudioDownloadWaiters.Find(DownloadKey))
            {
                Waiters = MoveTemp(*Found);
            }
            GArtStudioDownloadWaiters.Remove(DownloadKey);
            GArtStudioDownloadsInFlight.Remove(DownloadKey);
        }
        for (const TWeakObjectPtr<ATwinInstance>& WeakWaiter : Waiters)
        {
            if (ATwinInstance* Waiter = WeakWaiter.Get())
            {
                if (Waiter->PendingRemoteId == ExpectId)
                {
                    Waiter->PendingRemoteId.Empty();
                }
                if (Waiter->AssetPath == ExpectId)
                {
                    Waiter->ScheduleRemoteGltfRetry(ExpectId);
                }
            }
        }
    }
}

void ATwinInstance::ScheduleRemoteGltfRetry(const FString& StableId)
{
    if (AssetPath != StableId || RemoteRetryId != StableId)
    {
        return;
    }

    constexpr int32 MaxRetryAttempts = 3;
    if (RemoteRetryAttempt >= MaxRetryAttempts)
    {
        UE_LOG(LogTemp, Error, TEXT("[孪生体] ArtStudio 下载重试已达上限: %s"), *StableId);
        return;
    }

    const float DelaySeconds = FMath::Pow(2.0f, static_cast<float>(RemoteRetryAttempt));
    ++RemoteRetryAttempt;
    TWeakObjectPtr<ATwinInstance> WeakThis(this);
    FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateLambda(
            [WeakThis, StableId](float)
            {
                ATwinInstance* Self = WeakThis.Get();
                if (Self && Self->AssetPath == StableId)
                {
                    Self->PendingRemoteId.Empty();
                    Self->LoadRemoteGltf(StableId);
                }
                return false;
            }),
        DelaySeconds);
    UE_LOG(LogTemp, Warning, TEXT("[孪生体] %.0f 秒后重试 ArtStudio 下载: %s"), DelaySeconds, *StableId);
}

// ═══════════════════════════════════════════════════════════════════════════
// I3D_Representable — 存在性与可见性
// ═══════════════════════════════════════════════════════════════════════════

void ATwinInstance::ApplyRepresentableFromSnapshot(
    const TSharedPtr<FJsonObject>& RepObj,
    bool bIsDelta)
{
    FString NewAssetId;
    const bool bHasNewAssetId = RepObj->TryGetStringField(
        TEXT("asset_id"),
        NewAssetId);
    const bool bAssetChanged = bHasNewAssetId
        && !NewAssetId.IsEmpty()
        && NewAssetId != AssetPath;
    if (bAssetChanged)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[孪生体] 资产热更换: %s → %s"),
            *AssetPath,
            *NewAssetId);
        AssetPath = NewAssetId;
    }

    FString IncomingContainerPath;
    const bool bHasContainerPath = RepObj->TryGetStringField(
        TEXT("container_blueprint_id"),
        IncomingContainerPath);
    FString EffectiveContainerPath = ContainerBlueprintPath;
    if (bHasContainerPath)
    {
        EffectiveContainerPath = IncomingContainerPath;
    }
    else if (!bIsDelta)
    {
        EffectiveContainerPath.Empty();
    }

    FString IncomingContainerSlot;
    const bool bHasContainerSlot = RepObj->TryGetStringField(
        TEXT("container_slot"),
        IncomingContainerSlot);
    FString EffectiveContainerSlot = ContainerSlot.IsEmpty()
        ? TEXT("primary")
        : ContainerSlot;
    if (bHasContainerSlot)
    {
        EffectiveContainerSlot = IncomingContainerSlot.IsEmpty()
            ? TEXT("primary")
            : IncomingContainerSlot;
    }
    else if (!bIsDelta && !EffectiveContainerPath.IsEmpty())
    {
        EffectiveContainerSlot = TEXT("primary");
    }

    const bool bContainerChanged =
        EffectiveContainerPath != ContainerBlueprintPath;
    const bool bContainerSlotChanged =
        EffectiveContainerSlot != ContainerSlot;
    if (bContainerChanged || bContainerSlotChanged)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[孪生体] BP 容器热更换: container=%s → %s slot=%s → %s"),
            *ContainerBlueprintPath,
            *EffectiveContainerPath,
            *ContainerSlot,
            *EffectiveContainerSlot);
        ContainerBlueprintPath = EffectiveContainerPath;
        ContainerSlot = EffectiveContainerSlot;
    }

    const TArray<TSharedPtr<FJsonValue>>* RenderParts = nullptr;
    const bool bHasRenderParts = RepObj->TryGetArrayField(
        TEXT("render_parts"),
        RenderParts)
        && RenderParts
        && RenderParts->Num() > 0;
    if (bHasRenderParts)
    {
        if (!ContainerBlueprintPath.IsEmpty())
        {
            UE_LOG(LogTemp, Error,
                TEXT("[孪生体] 快照同时包含 render_parts 与 BP 容器；按组合模型处理并忽略容器: %s"),
                *InstanceId);
        }
        FString AssemblySignature;
        RepObj->TryGetStringField(TEXT("assembly_signature"), AssemblySignature);
        ApplyRenderPartsFromSnapshot(*RenderParts, AssemblySignature);
    }
    else if (bAssemblyRenderActive)
    {
        // 后端撤掉 render_parts 时显式退回单资产或 BP 容器模式。
        ClearRenderParts();
        if (bInitialized && !AssetPath.IsEmpty())
        {
            ReloadCurrentSingleRepresentation();
        }
    }

    // I3D_Representable 控制场景存在性（加载/卸载资源）。
    bool bVisible = true;
    RepObj->TryGetBoolField(TEXT("is_visible"), bVisible);
    if (!bRuntimeEditorLoadedOverride)
    {
        bRuntimeLoaded = bVisible;
    }
    ApplyRuntimeLoadedVisibility(
        bRuntimeEditorLoadedOverride ? bRuntimeLoaded : bVisible);

    if (!bHasRenderParts
        && (bAssetChanged || bContainerChanged || bContainerSlotChanged)
        && bInitialized
        && bRuntimeLoaded)
    {
        ReloadCurrentSingleRepresentation();
    }
}

void ATwinInstance::ApplyRuntimeLoadedVisibility(bool bVisible)
{
    bRuntimeLoaded = bVisible;

    if (bAssemblyRenderActive)
    {
        // 复合实例保留已加载的部件资产，只切换组件显隐；再次可见时无需整组重建。
        for (int32 PartIndex = 0; PartIndex < RenderPartComponents.Num(); ++PartIndex)
        {
            UStaticMeshComponent* PartComponent = RenderPartComponents[PartIndex];
            if (PartComponent && IsValid(PartComponent))
            {
                const bool bSourceVisible = RenderPartSourceVisibility.IsValidIndex(PartIndex)
                    ? RenderPartSourceVisibility[PartIndex]
                    : true;
                PartComponent->SetVisibility(bVisible && bSourceVisible, true);
            }
        }
        // 正常 assembly 的基座没有网格；加载失败时这里控制明显的 Cube fallback。
        MeshComponent->SetVisibility(bVisible, false);
    }
    else if (!bVisible && ActiveRepresentationKind != ERepresentationKind::None)
    {
        // 从场景卸载不占内存资源
        ClearSingleAssetRepresentation();
        UE_LOG(LogTemp, Log, TEXT("[孪生体] 已卸载资产: %s"), *InstanceId);
    }
    else if (bVisible && ActiveRepresentationKind == ERepresentationKind::None && bInitialized)
    {
        // 重新加载并进入场景
        ReloadCurrentSingleRepresentation();
        UE_LOG(LogTemp, Log, TEXT("[孪生体] 重新加载进入场景: %s"), *InstanceId);
    }
    // 强制把原先在这的 SetActorHiddenInGame 移除，交由 I3D_Visual 去处理纯粹的显隐
}

void ATwinInstance::SetRuntimeEditorLoadedOverride(bool bLoaded)
{
    bRuntimeEditorLoadedOverride = true;
    ApplyRuntimeLoadedVisibility(bLoaded);
}

void ATwinInstance::ClearRuntimeEditorLoadedOverride()
{
    bRuntimeEditorLoadedOverride = false;
}

// ═══════════════════════════════════════════════════════════════════════════
// I3D_Spatial — 空间变换
// ═══════════════════════════════════════════════════════════════════════════

void ATwinInstance::ApplyRealtimeSpatial(double X, double Y, double HeadingDeg, float HoldSeconds)
{
    if (bLocalOverrideLock || !FMath::IsFinite(X) || !FMath::IsFinite(Y) || !FMath::IsFinite(HeadingDeg))
    {
        return;
    }

    const UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    FVector NewLocation = GetActorLocation();
    NewLocation.X = X;
    NewLocation.Y = Y;

    FRotator NewRotation = GetActorRotation();
    NewRotation.Yaw = FMath::Fmod(HeadingDeg, 360.0);

    SetActorLocationAndRotation(NewLocation, NewRotation);
    RealtimeSpatialValidUntilSeconds =
        static_cast<double>(World->GetTimeSeconds()) + FMath::Max(0.1f, HoldSeconds);
}

bool ATwinInstance::IsRealtimeSpatialActive() const
{
    const UWorld* World = GetWorld();
    return World
        && static_cast<double>(World->GetTimeSeconds()) <= RealtimeSpatialValidUntilSeconds;
}

void ATwinInstance::ApplySpatialFromSnapshot(const TSharedPtr<FJsonObject>& SpatialObj)
{
    // 本地手动锁定最高优先；其次是仍有心跳的 WebSocket；最后才是 HTTP 快照。
    if (bLocalOverrideLock || IsRealtimeSpatialActive())
    {
        return;
    }

    double tx = 0, ty = 0, tz = 0;
    SpatialObj->TryGetNumberField(TEXT("translation_x"), tx);
    SpatialObj->TryGetNumberField(TEXT("translation_y"), ty);
    SpatialObj->TryGetNumberField(TEXT("translation_z"), tz);

    double rx = 0, ry = 0, rz = 0;
    SpatialObj->TryGetNumberField(TEXT("rotation_x"), rx);
    SpatialObj->TryGetNumberField(TEXT("rotation_y"), ry);
    SpatialObj->TryGetNumberField(TEXT("rotation_z"), rz);

    // [PRD B.3] 严格校验与钳位：Rotation 兜底取模，防止前端脏数据浮点溢出
    rx = FMath::Fmod(rx, 360.0);
    ry = FMath::Fmod(ry, 360.0);
    rz = FMath::Fmod(rz, 360.0);

    double sx = 1, sy = 1, sz = 1;
    SpatialObj->TryGetNumberField(TEXT("scale_x"), sx);
    SpatialObj->TryGetNumberField(TEXT("scale_y"), sy);
    SpatialObj->TryGetNumberField(TEXT("scale_z"), sz);

    FVector NewLoc = FVector(tx, ty, tz);
    FRotator NewRot = FRotator(ry, rz, rx);   // Pitch=Y, Yaw=Z, Roll=X

    // Datasmith 会用负缩放表达镜像。只把接近 0 的绝对值抬到 0.001，保留符号，
    // 否则母 Actor 的镜像会在数据库重建时被错误压成正向薄片。
    auto SanitizeScale = [](double Value)
    {
        if (!FMath::IsFinite(Value)) return 1.0;
        if (FMath::Abs(Value) >= 0.001) return Value;
        return Value < 0.0 ? -0.001 : 0.001;
    };
    FVector NewScale = FVector(
        SanitizeScale(sx),
        SanitizeScale(sy),
        SanitizeScale(sz)
    );

    SetActorLocation(NewLoc);
    SetActorRotation(NewRot);
    SetActorScale3D(NewScale);
}

// ═══════════════════════════════════════════════════════════════════════════
// 视觉表达与行为表现
// ═══════════════════════════════════════════════════════════════════════════

void ATwinInstance::CacheOriginalMaterials()
{
    UMeshComponent* ActiveMesh = GetActiveMeshComponent();
    if (!ActiveMesh) return;
    OriginalMaterials.Empty();
    for (int32 i = 0; i < ActiveMesh->GetNumMaterials(); ++i)
    {
        OriginalMaterials.Add(ActiveMesh->GetMaterial(i));
    }
}

void ATwinInstance::RestoreOriginalMaterials()
{
    UMeshComponent* ActiveMesh = GetActiveMeshComponent();
    if (!ActiveMesh) return;
    for (int32 i = 0; i < OriginalMaterials.Num(); ++i)
    {
        if (i < ActiveMesh->GetNumMaterials())
        {
            ActiveMesh->SetMaterial(i, OriginalMaterials[i]);
        }
    }
}

void ATwinInstance::ApplyVisualVisibility(bool bVisible)
{
    bVisualVisible = bVisible;
    SetActorHiddenInGame(!bVisible);
    if (BlueprintActorComponent)
    {
        BlueprintActorComponent->SetVisibility(bVisible, false);
        if (AActor* ChildActor = BlueprintActorComponent->GetChildActor())
        {
            ChildActor->SetActorHiddenInGame(!bVisible);
        }
    }
    if (ActiveContainerHost.IsValid())
    {
        ActiveContainerHost->ForwardVisualState(CurrentMaterialVariant, bVisible);
    }
}

void ATwinInstance::ApplyVisualFromSnapshot(const TSharedPtr<FJsonObject>& VisualObj)
{
    ApplyPlainLabelFromSnapshot(VisualObj);
    // ── 材质变体 (material_variant) ──────────────────────────────────────
    FString MaterialVariant;
    if (!bPresentationAuthoritative
        && VisualObj->TryGetStringField(TEXT("material_variant"), MaterialVariant)
        && MaterialVariant != CurrentMaterialVariant)
    {
        CurrentMaterialVariant = MaterialVariant;

        UE_LOG(LogTemp, Log, TEXT("[孪生体] 改变视觉状态: %s → %s"), *InstanceId, *MaterialVariant);

        if (MaterialVariant == TEXT("normal"))
        {
            RestoreOriginalMaterials();
        }
        else
        {
            // 交给蓝图处理字典映射
            OnMaterialVariantChanged(MaterialVariant);
        }
    }

    // ── 可见性 (is_visible) 控制纯渲染显隐 ────────────────────────────
    bool bNewVisualVisible = true;
    if (VisualObj->TryGetBoolField(TEXT("is_visible"), bNewVisualVisible))
    {
        ApplyVisualVisibility(bNewVisualVisible);
    }
    else if (ActiveContainerHost.IsValid())
    {
        ActiveContainerHost->ForwardVisualState(
            CurrentMaterialVariant,
            bVisualVisible);
    }
}

void ATwinInstance::ApplyBehavioralFromSnapshot(const TSharedPtr<FJsonObject>& BehaviorObj)
{
    FString AnimState;
    if (!bPresentationAuthoritative
        && BehaviorObj->TryGetStringField(TEXT("animation_state"), AnimState)
        && AnimState != CurrentAnimState)
    {
        CurrentAnimState = AnimState;
        // C++ 直接驱动程序化动画，不再依赖蓝图
        PlayAnimationState(AnimState);
        UE_LOG(LogTemp, Log, TEXT("[孪生体] 动画状态: %s → %s"), *InstanceId, *AnimState);
    }

    FString FxTrigger;
    if (!bPresentationAuthoritative
        && BehaviorObj->TryGetStringField(TEXT("fx_trigger"), FxTrigger)
        && FxTrigger != CurrentFxTrigger)
    {
        CurrentFxTrigger = FxTrigger;
        OnFxTriggered(FxTrigger);  // 抛出给蓝图实现
        UE_LOG(LogTemp, Log, TEXT("[孪生体] 特效触发: %s → %s"), *InstanceId, *FxTrigger);
    }

    ApplyPlainLabelFromSnapshot(BehaviorObj);

    if (ActiveContainerHost.IsValid())
    {
        ActiveContainerHost->ForwardBehaviorState(
            CurrentAnimState,
            CurrentFxTrigger);
    }
}

void ATwinInstance::ApplyPlainLabelFromSnapshot(const TSharedPtr<FJsonObject>& LabelObj)
{
    FString LabelContent;
    if (LabelObj->TryGetStringField(TEXT("ui_label_content"), LabelContent) && LabelContent != CurrentLabelContent)
    {
        CurrentLabelContent = LabelContent;

        if (LabelComponent)
        {
            if (LabelContent.IsEmpty())
            {
                // 空内容就隐藏标签
                LabelComponent->SetVisibility(false);
                LabelComponent->SetText(FText::GetEmpty());
                if (!bAnimRunning && !HasAlwaysOverlay())
                {
                    SetActorTickEnabled(false);
                }
            }
            else
            {
                // 应用最新字体配置（用户在编辑器设置后生效）
                LabelComponent->SetRelativeLocation(FVector(0.f, 0.f, LabelZOffset));
                LabelComponent->SetWorldSize(LabelWorldSize);
                LabelComponent->SetTextRenderColor(LabelColor);

                // 如果用户指定了字体，就应用它（支持中文）
                if (LabelFont)
                {
                    LabelComponent->SetFont(LabelFont);
                }

                LabelComponent->SetText(FText::FromString(LabelContent));
                LabelComponent->SetVisibility(true);

                // 开启 Tick 以便每帧更新朝向
                SetActorTickEnabled(true);
            }
        }

        UE_LOG(LogTemp, Log, TEXT("[孪生体] UI标签更新: %s → \"%s\""), *InstanceId, *LabelContent);
    }

}

void ATwinInstance::ApplyOverlayFromSnapshot(const TSharedPtr<FJsonObject>& OverlayObj)
{
    if (!OverlayObj.IsValid())
    {
        ClearOverlay();
        return;
    }

    bool bEnabled = false;
    OverlayObj->TryGetBoolField(TEXT("enabled"), bEnabled);
    FString DisplayMode;
    OverlayObj->TryGetStringField(TEXT("display_mode"), DisplayMode);
    if (DisplayMode != TEXT("selected") && DisplayMode != TEXT("always"))
    {
        DisplayMode = TEXT("selected");
    }

    OverlayOffsetCm = FVector(0.0f, 0.0f, 20.0f);
    const TSharedPtr<FJsonObject>* Anchor = nullptr;
    const TSharedPtr<FJsonObject>* Offset = nullptr;
    if (OverlayObj->TryGetObjectField(TEXT("anchor"), Anchor) && Anchor && Anchor->IsValid()
        && (*Anchor)->TryGetObjectField(TEXT("offset_cm"), Offset) && Offset && Offset->IsValid())
    {
        double X = 0.0;
        double Y = 0.0;
        double Z = 20.0;
        (*Offset)->TryGetNumberField(TEXT("x"), X);
        (*Offset)->TryGetNumberField(TEXT("y"), Y);
        (*Offset)->TryGetNumberField(TEXT("z"), Z);
        OverlayOffsetCm = FVector(X, Y, Z);
    }

    bOverlayEnabled = bEnabled;
    OverlayDisplayMode = DisplayMode;
    CurrentOverlayData = OverlayObj;
    ++OverlayPayloadSerial;

    if (LabelComponent)
    {
        // Plain labels and information panels have independent type settings.
        if (!CurrentLabelContent.IsEmpty())
        {
            LabelComponent->SetText(FText::FromString(CurrentLabelContent));
            LabelComponent->SetVisibility(true);
        }
    }

    if (OverlayWidgetComponent)
    {
        if (!WorldOverlayWidget)
        {
            OverlayWidgetComponent->SetWidgetClass(UOntoTwinOverlayWidget::StaticClass());
            OverlayWidgetComponent->InitWidget();
            WorldOverlayWidget = Cast<UOntoTwinOverlayWidget>(OverlayWidgetComponent->GetUserWidgetObject());
            if (WorldOverlayWidget)
            {
                WorldOverlayWidget->SetWorldSpacePresentation(true);
            }
        }
        if (WorldOverlayWidget)
        {
            WorldOverlayWidget->ApplyOverlayData(OverlayObj);
            UpdateWorldOverlayRenderTarget();
        }
        OverlayWidgetComponent->SetVisibility(bOverlayEnabled && HasAlwaysOverlay());
    }

    if (HasAlwaysOverlay() || (LabelComponent && LabelComponent->IsVisible()))
    {
        SetActorTickEnabled(true);
    }
    else if (!bAnimRunning)
    {
        SetActorTickEnabled(false);
    }
}

void ATwinInstance::ClearOverlay()
{
    bOverlayEnabled = false;
    OverlayDisplayMode.Empty();
    CurrentOverlayData.Reset();
    ++OverlayPayloadSerial;
    if (OverlayWidgetComponent)
    {
        OverlayWidgetComponent->SetVisibility(false);
        OverlayWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
    if (LabelComponent && !CurrentLabelContent.IsEmpty())
    {
        LabelComponent->SetText(FText::FromString(CurrentLabelContent));
        LabelComponent->SetVisibility(true);
        SetActorTickEnabled(true);
    }
    else if (!bAnimRunning)
    {
        SetActorTickEnabled(false);
    }
}

FBox ATwinInstance::GetRepresentationWorldBounds(bool bIncludeNonColliding) const
{
    FBox Bounds(ForceInit);
    AppendVisibleMeshBounds(this, Bounds);

    if (BlueprintActorComponent)
    {
        AActor* ChildActor = BlueprintActorComponent->GetChildActor();
        AppendVisibleMeshBounds(ChildActor, Bounds);
        if (ChildActor)
        {
            TArray<AActor*> AttachedActors;
            ChildActor->GetAttachedActors(AttachedActors, true, true);
            for (const AActor* AttachedActor : AttachedActors)
            {
                AppendVisibleMeshBounds(AttachedActor, Bounds);
            }
        }
    }

    return Bounds;
}

bool ATwinInstance::GetRepresentationLocalBounds(FBox& OutLocalBounds) const
{
    OutLocalBounds = FBox(ForceInit);
    const FBox WorldBounds = GetRepresentationWorldBounds(true);
    if (!WorldBounds.IsValid)
    {
        return false;
    }

    const FTransform WorldToActor = GetActorTransform().Inverse();
    for (int32 XIndex = 0; XIndex < 2; ++XIndex)
    {
        for (int32 YIndex = 0; YIndex < 2; ++YIndex)
        {
            for (int32 ZIndex = 0; ZIndex < 2; ++ZIndex)
            {
                const FVector WorldCorner(
                    XIndex == 0 ? WorldBounds.Min.X : WorldBounds.Max.X,
                    YIndex == 0 ? WorldBounds.Min.Y : WorldBounds.Max.Y,
                    ZIndex == 0 ? WorldBounds.Min.Z : WorldBounds.Max.Z);
                OutLocalBounds += WorldToActor.TransformPosition(WorldCorner);
            }
        }
    }
    return OutLocalBounds.IsValid != 0;
}

FVector ATwinInstance::GetOverlayAnchorWorldLocation() const
{
    const FBox CombinedBounds = GetRepresentationWorldBounds(true);
    const FVector BoundsTop = CombinedBounds.IsValid
        ? FVector(CombinedBounds.GetCenter().X, CombinedBounds.GetCenter().Y, CombinedBounds.Max.Z)
        : GetActorLocation();
    const FVector RotatedOffset = GetActorTransform().TransformVectorNoScale(OverlayOffsetCm);
    return BoundsTop + RotatedOffset;
}

float ATwinInstance::GetOverlayRenderWidthPixels() const
{
    if (!OverlayWidgetComponent) return 720.0f;
    const FVector2D CurrentSize = OverlayWidgetComponent->GetCurrentDrawSize();
    if (CurrentSize.X > 1.0f) return CurrentSize.X;
    return FMath::Max(1.0f, OverlayWidgetComponent->GetDrawSize().X);
}

bool ATwinInstance::IsScreenPointOverAlwaysOverlay(
    APlayerController* PlayerController,
    const FVector2D& ScreenPoint,
    float PaddingPixels) const
{
    if (!PlayerController || !HasAlwaysOverlay() || !OverlayWidgetComponent
        || !OverlayWidgetComponent->IsVisible()
        || OverlayWidgetComponent->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
    {
        return false;
    }

    const FBox WorldBounds = OverlayWidgetComponent->Bounds.GetBox();
    FVector2D ScreenMin(FLT_MAX, FLT_MAX);
    FVector2D ScreenMax(-FLT_MAX, -FLT_MAX);
    bool bProjectedAnyCorner = false;

    for (int32 X = 0; X < 2; ++X)
    {
        for (int32 Y = 0; Y < 2; ++Y)
        {
            for (int32 Z = 0; Z < 2; ++Z)
            {
                const FVector Corner(
                    X ? WorldBounds.Max.X : WorldBounds.Min.X,
                    Y ? WorldBounds.Max.Y : WorldBounds.Min.Y,
                    Z ? WorldBounds.Max.Z : WorldBounds.Min.Z);
                FVector2D Projected;
                if (!PlayerController->ProjectWorldLocationToScreen(Corner, Projected, true))
                {
                    continue;
                }
                bProjectedAnyCorner = true;
                ScreenMin.X = FMath::Min(ScreenMin.X, Projected.X);
                ScreenMin.Y = FMath::Min(ScreenMin.Y, Projected.Y);
                ScreenMax.X = FMath::Max(ScreenMax.X, Projected.X);
                ScreenMax.Y = FMath::Max(ScreenMax.Y, Projected.Y);
            }
        }
    }

    if (!bProjectedAnyCorner) return false;
    const float Padding = FMath::Max(0.0f, PaddingPixels);
    return ScreenPoint.X >= ScreenMin.X - Padding
        && ScreenPoint.X <= ScreenMax.X + Padding
        && ScreenPoint.Y >= ScreenMin.Y - Padding
        && ScreenPoint.Y <= ScreenMax.Y + Padding;
}

void ATwinInstance::UpdateWorldOverlayRenderTarget()
{
    if (!OverlayWidgetComponent || !WorldOverlayWidget) return;

    const FVector2D DesiredSize = WorldOverlayWidget->GetDesiredRenderSize();
    const FVector2D CurrentSize = OverlayWidgetComponent->GetDrawSize();
    if (!CurrentSize.Equals(DesiredSize, 1.0f))
    {
        OverlayWidgetComponent->SetDrawSize(DesiredSize);
    }
    OverlayWidgetComponent->RequestRenderUpdate();
}

void ATwinInstance::RefreshAlwaysOverlay(
    const FVector& CameraLocation,
    bool bShouldShow,
    float WorldScale)
{
    if (!OverlayWidgetComponent) return;
    const bool bVisible = bShouldShow && HasAlwaysOverlay();
    OverlayWidgetComponent->SetVisibility(bVisible);
    const ECollisionEnabled::Type DesiredCollision = bVisible
        ? ECollisionEnabled::QueryOnly
        : ECollisionEnabled::NoCollision;
    if (OverlayWidgetComponent->GetCollisionEnabled() != DesiredCollision)
    {
        OverlayWidgetComponent->SetCollisionEnabled(DesiredCollision);
    }
    if (!bVisible) return;

    const FVector AnchorLocation = GetOverlayAnchorWorldLocation();
    OverlayWidgetComponent->SetWorldLocation(AnchorLocation);
    if (WorldScale > 0.0f)
    {
        OverlayWidgetComponent->SetWorldScale3D(FVector(WorldScale));
    }
    FRotator LookAt = UKismetMathLibrary::FindLookAtRotation(AnchorLocation, CameraLocation);
    LookAt.Roll = 0.0f;
    OverlayWidgetComponent->SetWorldRotation(LookAt);
}
