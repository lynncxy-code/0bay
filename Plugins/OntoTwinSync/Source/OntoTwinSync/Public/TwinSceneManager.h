// ============================================================================
// TwinSceneManager.h
//
// 孪生场景管理器 — 关卡级别的全局实例生命周期管理器
//
// 功能说明：
//   1. 启动后自动轮询后端 GET /api/v2/state/snapshots，获取所有实例快照
//   2. 对比本地注册表，自动 Spawn / Destroy ATwinInstance Actor
//   3. 编辑器模式下提供"📸 快照固化到关卡"按钮，一键生成持久 Actor
//   4. Play 时自动接管关卡中已有的 ATwinInstance（编辑器预置 Actor）
//
// 使用方式：
//   在关卡中放置一个 ATwinSceneManager Actor 即可，无需蓝图连接
//
// 依赖模块（.Build.cs）：
//   "Http", "Json", "JsonUtilities"
// ============================================================================

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HttpModule.h"
#include "InputCoreTypes.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "TwinSceneManager.generated.h"

class ATwinInstance;
class AOntoTwinRuntimeGizmo;
class APlayerController;
class APawn;
class ATwinRuntimeEditorCameraPawn;
class IWebSocket;
class UMediaPlayer;
class UMediaSoundComponent;
class UMediaTexture;
class UOntoTwinRuntimeEditorPanel;
class UOntoTwinOverlayWidget;
class UOntoTwinModelLoadingWidget;
class UTwinInteractionManagerComponent;
class UOntoTwinWebInteractionComponent;
class UOntoTwinUEAssetCatalogSyncComponent;
class IOntoTwinRealtimeStreamProvider;
enum class EOntoTwinOverlayMediaAction : uint8;

UENUM(BlueprintType)
enum class EOntoTwinRuntimeAccessState : uint8
{
    Checking,
    Ready,
    Unbound,
    Mismatch,
    Error
};

struct FPreviewMaterialPartBaseline
{
    int32 PartIndex = INDEX_NONE;
    FString AssetPath;
    FString SourceActorGuid;
    FString SourceComponentName;
    TArray<FString> DatabaseMaterialPaths;
    TArray<FString> ComponentMaterialPaths;
    TArray<FString> SavedMeshMaterialPaths;
};

struct FPreviewMaterialInstanceBaseline
{
    FString AssemblySignature;
    TArray<FPreviewMaterialPartBaseline> Parts;
};

/**
 * ATwinSceneManager
 *
 * 场景中放置 1 个即可。自动轮询后端、管理所有孪生体 Actor 的生命周期。
 */
UCLASS(ClassGroup=(DigitalTwin), meta=(DisplayName="Twin Scene Manager"))
class ONTOTWINSYNC_API ATwinSceneManager : public AActor
{
    GENERATED_BODY()

public:
    ATwinSceneManager();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaTime) override;

public:
    /** Office VR keeps the HMD pawn; skip OntoTwin backend overlay and god camera. */
    bool ShouldYieldToHostVR() const;

    // ═══════════════════════════════════════════════════════════════════════
    // 编辑器可配置属性
    // ═══════════════════════════════════════════════════════════════════════

    /** 后端 API 基础地址 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="连接",
              meta=(DisplayName="后端基础URL"))
    FString BackendBaseUrl = TEXT("http://localhost:5000");

    /** UE 工程稳定身份。默认使用 ueproj_<工程名>；后端用它强绑定数据集，防止工程串台。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="连接",
              meta=(DisplayName="UE工程ID"))
    FString UEProjectId;

    /** UE 工程显示名。默认取当前 .uproject 的工程名。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="连接",
              meta=(DisplayName="UE工程名称"))
    FString UEProjectName;

    /**
     * 场景 ID：只拉取该场景的实例。
     * 留空 = 跟随后端当前激活的数据集（单工程常用）。
     * 多个 UE 工程对接同一后端时，各填各的场景名以互不干扰。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="连接",
              meta=(DisplayName="场景ID(留空=跟随后端)"))
    FString SceneId = TEXT("");

    /** 轮询间隔（秒） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="连接",
              meta=(DisplayName="轮询间隔(秒)", ClampMin="0.1", ClampMax="10.0"))
    float PollInterval = 0.5f;

    /** 连续失败次数阈值，超过后标记离线 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="连接",
              meta=(DisplayName="离线阈值(次)"))
    int32 OfflineThreshold = 3;

    /** 连接 AGV 实时状态流；HTTP 快照仍负责实例建档、模型绑定与非空间属性。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="连接|WebSocket",
              meta=(DisplayName="启用实时WebSocket"))
    bool bEnableRealtimeWebSocket = false;

    /** OntoTwin 中间层实时目标地址。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="连接|WebSocket",
              meta=(DisplayName="实时WebSocket URL"))
    FString RealtimeWebSocketUrl = TEXT("ws://10.191.12.40:8080/ws/targets");

    /** WebSocket 断开后的重连间隔。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="连接|WebSocket",
              meta=(DisplayName="重连间隔(秒)", ClampMin="0.5", ClampMax="60.0"))
    float RealtimeReconnectSeconds = 3.0f;

    /** 收到实时帧后，HTTP 在这段时间内不得覆盖该实例的空间坐标。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="连接|WebSocket",
              meta=(DisplayName="实时空间优先保持(秒)", ClampMin="0.1", ClampMax="10.0"))
    float RealtimeSpatialHoldSeconds = 1.0f;

    // ═══════════════════════════════════════════════════════════════════════
    // 实例配置
    // ═══════════════════════════════════════════════════════════════════════

    /** 运行时自动生成的孪生体蓝图类（请在这里选你的 BP_TwinInstance_Advanced） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="实例配置",
              meta=(DisplayName="孪生体蓝图类"))
    TSubclassOf<ATwinInstance> InstanceClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="顶部信息面板",
              meta=(DisplayName="启用顶部信息面板"))
    bool bEnableOverlays = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="顶部信息面板",
              meta=(DisplayName="选中面板类"))
    TSubclassOf<UOntoTwinOverlayWidget> OverlayWidgetClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="顶部信息面板",
              meta=(DisplayName="常显最大距离(cm)", ClampMin="100.0", ClampMax="1000000.0"))
    float OverlayCullDistanceCm = 50000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="顶部信息面板",
              meta=(DisplayName="常显最大数量", ClampMin="1", ClampMax="500"))
    int32 MaxVisibleAlwaysOverlays = 100;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="顶部信息面板|可读性",
              meta=(DisplayName="常显目标屏幕宽度(px)", ClampMin="120.0", ClampMax="600.0"))
    float AlwaysOverlayTargetScreenWidthPx = 260.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="顶部信息面板|可读性",
              meta=(DisplayName="常显最小世界缩放", ClampMin="0.01", ClampMax="1.0"))
    float AlwaysOverlayMinWorldScale = 0.06f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="顶部信息面板|可读性",
              meta=(DisplayName="常显最大世界缩放", ClampMin="0.01", ClampMax="2.0"))
    float AlwaysOverlayMaxWorldScale = 0.35f;

    /** 4.0 场景交互运行组件。人物、输入、路线和心跳不进入 SceneManager 主体。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="场景交互")
    UTwinInteractionManagerComponent* InteractionManager;

    /** 3.8 Web 配置轮询、单例浏览器、Bridge 与场景显隐。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="场景交互|Web")
    UOntoTwinWebInteractionComponent* WebInteractionManager;

    /** UE 项目资产目录同步工具；发布目录树并响应 Web 端按需扫描，供 CAD 类型审核选择与推荐。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="连接|资产目录")
    UOntoTwinUEAssetCatalogSyncComponent* AssetCatalogSync;

    // ═══════════════════════════════════════════════════════════════════════
    // Runtime Editor（打包 exe 内的轻量场景编辑入口）
    // ═══════════════════════════════════════════════════════════════════════

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Runtime Editor",
              meta=(DisplayName="启用Runtime Editor"))
    bool bEnableRuntimeEditor = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Runtime Editor",
              meta=(DisplayName="编辑模式轮询间隔(秒)", ClampMin="0.2", ClampMax="10.0"))
    float EditModePollInterval = 1.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Runtime Editor",
              meta=(DisplayName="Runtime Editor面板类"))
    TSubclassOf<UOntoTwinRuntimeEditorPanel> RuntimeEditorPanelClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Runtime Editor",
              meta=(DisplayName="Runtime Gizmo类"))
    TSubclassOf<AOntoTwinRuntimeGizmo> RuntimeGizmoClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Runtime Editor|输入",
              meta=(DisplayName="场景编辑快捷键"))
    FKey RuntimeEditorToggleKey = EKeys::F10;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Runtime Editor|输入",
              meta=(DisplayName="保存键"))
    FKey SaveKey = EKeys::S;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Runtime Editor|输入",
              meta=(DisplayName="取消键"))
    FKey CancelKey = EKeys::Escape;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Runtime Editor|相机",
              meta=(DisplayName="启用独立自由相机"))
    bool bEnableRuntimeEditorFreeCamera = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Runtime Editor|相机",
              meta=(DisplayName="初始移动速度(cm/s)", ClampMin="100.0", ClampMax="10000.0"))
    float RuntimeEditorCameraMoveSpeedCmS = 600.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Runtime Editor|相机",
              meta=(DisplayName="右键观察灵敏度回退值", ClampMin="0.1", ClampMax="5.0",
                    ToolTip="F7 上帝视角运行配置尚未就绪时使用的回退灵敏度"))
    float RuntimeEditorCameraLookSensitivity = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Runtime Editor|吸附",
              meta=(DisplayName="启用靠墙吸附"))
    bool bEnableWallSnap = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Runtime Editor|吸附",
              meta=(DisplayName="启用网格吸附"))
    bool bEnableGridSnap = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Runtime Editor|吸附",
              meta=(DisplayName="网格吸附步长(cm)", ClampMin="1.0", ClampMax="10000.0"))
    float GridSnapSizeCm = 50.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Runtime Editor|吸附",
              meta=(DisplayName="靠墙吸附距离(cm)", ClampMin="1.0", ClampMax="10000.0"))
    float WallSnapDistanceCm = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Runtime Editor|吸附",
              meta=(DisplayName="墙体Tag"))
    FName WallTag = TEXT("OntoTwinWall");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Runtime Editor|批量",
              meta=(DisplayName="单次最多选择实例", ClampMin="1", ClampMax="100"))
    int32 RuntimeEditorMaxSelection = 20;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Runtime Editor|批量",
              meta=(DisplayName="会话最多待保存实例", ClampMin="1", ClampMax="500"))
    int32 RuntimeEditorMaxPending = 100;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Runtime Editor|批量",
              meta=(DisplayName="最多撤销步数", ClampMin="1", ClampMax="200"))
    int32 RuntimeEditorMaxHistory = 50;

    UFUNCTION(BlueprintCallable, Category="Runtime Editor")
    void ToggleRuntimeEditMode();

    /** F7 从编辑模式切到新漫游会话；脏数据仍走原有保存/放弃确认。 */
    void RequestRoamingAfterRuntimeEdit();
    void CancelRuntimeEditExitRequest();
    bool IsRoamingRequestedAfterRuntimeEditExit() const
    {
        return bRuntimeEnterRoamingAfterExit;
    }

    UFUNCTION(BlueprintCallable, Category="Runtime Editor")
    void SaveRuntimeEdit();

    UFUNCTION(BlueprintCallable, Category="Runtime Editor")
    void CancelRuntimeEdit();

    UFUNCTION(BlueprintCallable, Category="Runtime Editor")
    void UndoRuntimeEdit();

    UFUNCTION(BlueprintCallable, Category="Runtime Editor")
    void RedoRuntimeEdit();

    UFUNCTION(BlueprintCallable, Category="Runtime Editor")
    void RemoveRuntimeSelectionFromScene();

    UFUNCTION(BlueprintCallable, Category="Runtime Editor")
    void SaveAndExitRuntimeEdit();

    UFUNCTION(BlueprintCallable, Category="Runtime Editor")
    void DiscardAndExitRuntimeEdit();

    UFUNCTION(BlueprintCallable, Category="Runtime Editor")
    void BindCurrentRuntimeProject();

    UFUNCTION(BlueprintCallable, Category="Runtime Editor")
    void RetryRuntimeBindingStatus();

    UFUNCTION(BlueprintCallable, Category="Runtime Editor")
    void SetRuntimeWallSnapEnabled(bool bEnabled);

    UFUNCTION(BlueprintCallable, Category="Runtime Editor")
    void SetRuntimeGridSnapEnabled(bool bEnabled);

    FString GetRuntimeEditorModeText() const;
    FString GetRuntimeEditorBindingText() const;
    FString GetRuntimeEditorSelectionText() const;
    FString GetRuntimeEditorTransformText() const;
    FString GetRuntimeEditorStatusText() const;
    FString GetRuntimeEditorHeaderStateText() const;
    FString GetRuntimeEditorDisplayName() const;
    FString GetRuntimeEditorDisplayNameRaw() const;
    FString GetRuntimeEditorTypeName() const;
    FString GetRuntimeEditorTypeNameRaw() const;
    FString GetRuntimeEditorInstanceIdText() const;
    bool GetRuntimeEditorTransform(FVector& OutLocation, float& OutYaw) const;
    EOntoTwinRuntimeAccessState GetRuntimeEditorAccessState() const;
    bool CanBindRuntimeProject() const;
    bool CanRetryRuntimeBindingStatus() const;
    bool CanSaveRuntimeEdit() const;
    bool CanCancelRuntimeEdit() const;
    bool CanUndoRuntimeEdit() const;
    bool CanRedoRuntimeEdit() const;
    bool CanRemoveRuntimeSelection() const;
    bool HasRuntimeEditSelection() const;
    int32 GetRuntimeEditSelectionCount() const;
    void GetRuntimeSelectedInstanceDisplayNames(TArray<FString>& OutNames) const;
    int32 GetRuntimeEditPendingCount() const;
    void GetRuntimeEditPendingLines(TArray<FString>& OutLines) const;
    void GetRuntimeBusinessMembershipRows(
        TArray<FString>& OutBusinessIds,
        TArray<FString>& OutNames,
        TArray<uint8>& OutStates) const;
    bool ToggleRuntimeBusinessMembership(const FString& BusinessId);
    bool CreateRuntimeBusiness(const FString& Name);
    bool HasRuntimeBusinessSnapshot() const { return RuntimeBusinessConfig.IsValid(); }
    bool IsRuntimeSelectionMultiple() const { return RuntimeSelectedInstances.Num() > 1; }
    bool IsRuntimeEditDirty() const { return bRuntimeEditDirty; }
    bool IsRuntimeEditSaving() const { return bRuntimeEditSaving; }
    bool IsRuntimeWallSnapEnabled() const { return bEnableWallSnap; }
    bool IsRuntimeGridSnapEnabled() const { return bEnableGridSnap; }
    bool IsRuntimeEditModeActive() const { return bRuntimeEditMode; }

    /** 共享选择入口：人物模块只产生选择事件，Overlay 仍负责内容与 Widget 生命周期。 */
    void SelectOverlayFromSceneInteraction(ATwinInstance* Instance);
    void ClearOverlayFromSceneInteraction();

    /** Web 交互组件只读访问当前运行时实例注册表。 */
    ATwinInstance* FindManagedInstance(const FString& InstanceId) const;
    void GetManagedInstances(TArray<ATwinInstance*>& OutInstances) const;
    void FocusManagedInstance(ATwinInstance* Instance) const;
    void FocusManagedInstances(const TSet<FString>& InstanceIds) const;

    /** 将屏幕坐标解析为当前可见的 always Overlay，供漫游和语义输入共用。 */
    UFUNCTION(BlueprintCallable, Category="场景交互")
    bool SelectOverlayAtScreenPosition(const FVector2D& ScreenPosition);

    /** 兼容旧场景 Actor：按命中位置找到同位的 OntoTwin Overlay 实例。 */
    ATwinInstance* FindOverlayInstanceNearHit(
        const FHitResult& Hit,
        float MaxDistanceCm = 300.0f) const;

    /** 构造不含坐标的 WebSocket 健康快照，随现有 UE 心跳回报后端。 */
    TSharedRef<FJsonObject> BuildRealtimeChannelHealth() const;

    /** 应用外部数据监控台下发的实时流开关；HTTP 快照轮询不受影响。 */
    void ApplyRealtimeWebSocketEnabled(bool bEnabled);

    /** 把当前 UE 工程身份绑定到后端当前激活数据集（数据集之后只接受该工程的 UE 请求） */
    UFUNCTION(CallInEditor, Category="连接",
              meta=(DisplayName="绑定当前UE工程到激活数据集"))
    void BindCurrentUEProjectToActiveDataset();

    /** 手动把当前 UE 工程资产目录同步到 OntoTwin。 */
    UFUNCTION(CallInEditor, BlueprintCallable, Category="连接|资产目录",
              meta=(DisplayName="同步UE资产目录"))
    void SyncUEAssetCatalog();

    // ═══════════════════════════════════════════════════════════════════════
    // 编辑器预览（FR-4）—— 从数据库临时 spawn 供查看/微调，绝不存进 .umap
    //   已废弃"固化"：孪生实例只存在于数据库，编辑器里看的是 transient 预览。
    // ═══════════════════════════════════════════════════════════════════════

    /** 从数据库拉取本场景实例，生成【临时(transient)】预览 Actor（保存关卡不会写入 .umap） */
    UFUNCTION(CallInEditor, BlueprintCallable, Category="预览",
              meta=(DisplayName="从数据库拉取预览"))
    void PullPreviewFromDB();

    /**
     * 自动验收入口：读取 Saved/OntoTwinMigration/ue_snapshots.json，生成 transient
     * 预览并写 ue_preview_audit.json。用于命令行编辑器验证资产加载，不保存关卡。
     */
    UFUNCTION(CallInEditor, BlueprintCallable, Category="迁移",
              meta=(DisplayName="② 从迁移快照文件生成预览并审计"))
    void PreviewMigratedActorsFromSnapshotFile();

    /** 清除所有预览 Actor */
    UFUNCTION(CallInEditor, BlueprintCallable, Category="预览",
              meta=(DisplayName="清除预览"))
    void ClearPreview();

    /**
     * FR-5 回写：把预览 Actor 里被手动挪动过的（当前 transform ≠ 拉取时基线），
     * 逐个 POST /api/v2/state/writeback 提交回后端真源。只提交动过的，一处不多。
     */
    UFUNCTION(CallInEditor, Category="预览",
              meta=(DisplayName="提交空间变更(回写)"))
    void CommitPreviewChanges();

    /** 把预览组件覆写或已保存 StaticMesh 默认槽的材质变更原子回写到数据库。 */
    UFUNCTION(CallInEditor, Category="预览",
              meta=(DisplayName="提交材质变更(回写)"))
    void CommitPreviewMaterialChanges();

    // ═══════════════════════════════════════════════════════════════════════
    // FR-6 历史 actor 迁移工具（编辑器一次性；配合后端 migrate_ue_actors.py）
    //   流程：① 框选历史 actor → 右键"移动到文件夹"→ 填 MigrationFolderName
    //         → 单独选中本 Manager → 点导出 → ② 跑后端脚本收编入库 →
    //         ③ 清除已迁移的原 actor（之后由 DB 驱动重新 spawn）
    //   （不用"同时选中 actor + Manager"：UE 细节面板多选不同类型时只显示
    //    共同按钮，Manager 专属按钮会被隐藏——改用文件夹分两步选，彻底避开）
    // ═══════════════════════════════════════════════════════════════════════

    /** 待迁移母 Actor 的 Outliner 文件夹名；其附着后代会递归作为同一 assembly_v1 实例的部件导出。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="迁移",
              meta=(DisplayName="待迁移文件夹名"))
    FString MigrationFolderName = TEXT("ToMigrate");

    /** ① 导出文件夹内的顶层母 Actor；候选后代去重，并递归导出全部附着部件与特殊组件审计。 */
    UFUNCTION(CallInEditor, BlueprintCallable, Category="迁移",
              meta=(DisplayName="① 导出待迁移Actor"))
    void ExportSelectedActorsForMigration();

    /** ③ 优先按 assembly_v1.delete_actor_guids 清除母 Actor 及全部已收编后代；兼容旧映射。 */
    UFUNCTION(CallInEditor, BlueprintCallable, Category="迁移",
              meta=(DisplayName="③ 清除已迁移Actor"))
    void RemoveMigratedActors();

private:
    // ── 内部状态 ─────────────────────────────────────────────────────────

    /** 轮询定时器句柄 */
    FTimerHandle PollTimerHandle;

    /** 请求锁 */
    bool bRequestInFlight = false;

    /** 首轮数据库模型基线完成前保持启动加载层，并阻止自动漫游接管镜头。 */
    bool bInitialModelBaselineReady = false;

    UPROPERTY(Transient)
    UOntoTwinModelLoadingWidget* ModelLoadingWidget = nullptr;

    FTimerHandle ModelLoadingDismissTimer;

    /** 连续失败计数 */
    int32 ConsecutiveFailures = 0;

    /** 4.1 增量快照游标与当前会话兼容回退状态。 */
    FString IncrementalSnapshotCursor;
    bool bIncrementalSnapshotsFellBackToFull = false;

    /** 实例注册表：InstanceId → ATwinInstance* */
    UPROPERTY()
    TMap<FString, ATwinInstance*> InstanceRegistry;

    /** AGV 实时状态流。它只更新 InstanceRegistry 中由 HTTP 已创建的 Actor。 */
    TSharedPtr<IWebSocket> RealtimeSocket;
    FTimerHandle RealtimeReconnectTimerHandle;
    /**
     * Provider 注册发生在 World 初始化/Actor BeginPlay 的另一条时序上。
     * 先留出一个很短的探测窗口，再决定是否启用兼容回退 socket，避免
     * MetaverseClient 已存在时 TwinSceneManager 抢先建立第二条连接。
     */
    FTimerHandle RealtimeProviderProbeTimerHandle;
    int32 RealtimeConnectionGeneration = 0;
    int64 RealtimeFrameCount = 0;
    int64 RealtimeLastSourceTimestampMs = 0;
    double RealtimeLastFramePlatformSeconds = -1.0;
    FString RealtimeConnectionState = TEXT("disabled");
    FString RealtimeLastError;
    TMap<FString, FString> RealtimeTargetStates;
    TSet<FString> RealtimeAppliedInstanceIds;
    int32 RealtimeLastTargetCount = 0;
    int32 RealtimeLastAppliedCount = 0;
    struct FRealtimeCategoryHealth
    {
        int32 TargetCount = 0;
        int32 AppliedCount = 0;
        TMap<FString, int32> UnappliedReasons;
    };
    TMap<FString, FRealtimeCategoryHealth> RealtimeCategoryHealth;
    bool bRealtimeReconnectScheduled = false;
    bool bRealtimeClosing = false;
    bool bRealtimeProviderProbePending = false;
    bool bRealtimeUsingExternalProvider = false;
    TSet<FString> RealtimeMissingInstanceWarnings;

    /** 编辑器预览 Actor（transient，不入 .umap；由 ClearPreview 清理） */
    UPROPERTY(Transient)
    TArray<ATwinInstance*> PreviewActors;

    /** FR-5 回写基线：InstanceId → 拉取预览时数据库给的 transform（提交时据此 diff） */
    TMap<FString, FTransform> PreviewBaseline;

    /** 材质回写基线：数据库路径、组件实际路径、StaticMesh 已保存默认槽。 */
    TMap<FString, FPreviewMaterialInstanceBaseline> PreviewMaterialBaseline;

    bool bMaterialWritebackInFlight = false;

    /** FR-5 回写在途/成功计数（编辑器单线程回调，无需加锁） */
    int32 PendingWritebacks = 0;
    int32 SucceededWritebacks = 0;

    // Runtime Editor state
    UPROPERTY()
    ATwinInstance* RuntimeSelectedInstance = nullptr;

    UPROPERTY()
    TArray<ATwinInstance*> RuntimeSelectedInstances;

    UPROPERTY()
    AOntoTwinRuntimeGizmo* RuntimeGizmo = nullptr;

    UPROPERTY()
    UOntoTwinRuntimeEditorPanel* RuntimeEditorPanel = nullptr;

    UPROPERTY()
    ATwinRuntimeEditorCameraPawn* RuntimeEditorCameraPawn = nullptr;

    UPROPERTY()
    APawn* RuntimeEditorOriginalPawn = nullptr;

    UPROPERTY()
    ATwinInstance* OverlaySelectedInstance = nullptr;

    UPROPERTY()
    UOntoTwinOverlayWidget* SelectedOverlayWidget = nullptr;

    uint64 SelectedOverlayPayloadSerial = 0;

    bool bOverlayPointerInputActive = false;
    bool bOverlayPreviousMouseCursor = false;

    UPROPERTY()
    UMediaPlayer* OverlayMediaPlayer = nullptr;

    UPROPERTY()
    UMediaTexture* OverlayMediaTexture = nullptr;

    UPROPERTY()
    UMediaSoundComponent* OverlayMediaSound = nullptr;

public:
    /** 使用 4.1 增量快照；追加在既有反射属性之后，保持旧关卡的属性索引兼容。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="连接",
              meta=(DisplayName="启用增量快照"))
    bool bEnableIncrementalSnapshots = true;

private:

    FHttpRequestPtr OverlayMediaResolveRequest;
    FTimerHandle OverlayMediaRetryTimer;
    FString OverlayMediaInstanceId;
    FString OverlayMediaSourceRevision;
    FString OverlayMediaKind;
    bool bOverlayMediaMuted = true;
    bool bOverlayMediaAutoplay = true;
    bool bOverlayMediaLoop = false;
    bool bOverlayMediaOpening = false;
    bool bOverlayMediaReachedEnd = false;
    bool bOverlayMediaTextureSampleLogged = false;
    double OverlayMediaDurationSeconds = 0.0;
    double OverlayMediaPlayedSeconds = 0.0;
    double OverlayMediaPlaybackStartedAtSeconds = 0.0;
    bool bOverlayMediaPlayWhenOpened = false;
    bool bOverlayMediaManualRetryRequired = false;
    int32 OverlayMediaRetryIndex = 0;

    bool bRuntimeEditMode = false;
    /** Opt-in, non-persistent command-line acceptance test for F10 singleton selection. */
    bool bRuntimeEditSelfTestRequested = false;
    bool bRuntimeEditSelfTestComplete = false;
    bool bRuntimeEditSelfTestQuit = false;
    int32 RuntimeEditSelfTestExpectedInstances = -1;
    int32 RuntimeEditSelfTestExpectedTypes = -1;
    double RuntimeEditSelfTestStartSeconds = 0.0;
    bool bRuntimeEditDirty = false;
    bool bRuntimeEditSaving = false;
    bool bRuntimeBindingRequestInFlight = false;
    bool bRuntimeCanSave = false;
    bool bRuntimeDragging = false;
    bool bRuntimeCameraLookSuppressed = false;
    bool bRuntimeLookInputWasAlreadyIgnored = false;
    bool bRuntimeCameraRotating = false;
    bool bRuntimePreviousMouseCursor = false;
    bool bRuntimePreviousAnimRunning = false;
    bool bRuntimeExitAfterSave = false;
    bool bRuntimeEnterRoamingAfterExit = false;
    bool bRuntimeBusinessDirty = false;
    float RuntimeLastToggleInputTime = -1000.0f;
    FString RuntimePreviousAnimState;
    FString RuntimeBindingMode = TEXT("unknown");
    FString RuntimeStatusMessage = TEXT("F10：场景编辑");
    FTransform RuntimeEditBaseline = FTransform::Identity;
    FTransform RuntimeDragStartTransform = FTransform::Identity;
    FBox RuntimeEditLocalBounds = FBox(ForceInit);
    FVector RuntimeEditPivotLocal = FVector::ZeroVector;
    FVector RuntimeDragPivotWorld = FVector::ZeroVector;
    FVector RuntimeDragStartPivotWorld = FVector::ZeroVector;
    FVector RuntimeDragStartPoint = FVector::ZeroVector;
    FVector RuntimeZDragStartPoint = FVector::ZeroVector;
    FPlane RuntimeZDragPlane = FPlane(FVector::ZeroVector, FVector::ForwardVector);
    float RuntimeEditPlaneZ = 0.0f;
    float RuntimeDragPlaneZ = 0.0f;
    float RuntimeDragStartAngleDeg = 0.0f;
    float RuntimeDragStartYaw = 0.0f;
    enum class ERuntimeDragPart : uint8 { None, MoveX, MoveY, MoveZ, MoveXY, RotateYaw };
    enum class ERuntimeSnapFeedback : uint8 { None, Grid, Wall };
    ERuntimeDragPart RuntimeHoverPart = ERuntimeDragPart::None;
    ERuntimeDragPart RuntimeDragPart = ERuntimeDragPart::None;
    ERuntimeSnapFeedback RuntimeSnapFeedback = ERuntimeSnapFeedback::None;
    FVector RuntimeSnapFeedbackPoint = FVector::ZeroVector;
    FVector2D RuntimeCameraCursorRestorePosition = FVector2D::ZeroVector;

    struct FRuntimeEditInstanceState
    {
        TWeakObjectPtr<ATwinInstance> Instance;
        FTransform BaselineTransform = FTransform::Identity;
        bool bBaselineLoaded = true;
        FString BaselineStateHash;
        FString PreviousAnimState;
        bool bPreviousAnimRunning = false;
        bool bTransformDirty = false;
        bool bLoadedDirty = false;
        bool bConflict = false;
    };

    struct FRuntimeEditValue
    {
        TWeakObjectPtr<ATwinInstance> Instance;
        FTransform Transform = FTransform::Identity;
        bool bLoaded = true;
    };

    struct FRuntimeEditCommand
    {
        FString Label;
        TArray<FRuntimeEditValue> Before;
        TArray<FRuntimeEditValue> After;
        bool bBusinessCommand = false;
        TSharedPtr<FJsonObject> BusinessBefore;
        TSharedPtr<FJsonObject> BusinessAfter;
    };

    TMap<FString, FRuntimeEditInstanceState> RuntimeEditStates;
    TArray<FRuntimeEditCommand> RuntimeUndoStack;
    TArray<FRuntimeEditCommand> RuntimeRedoStack;
    TSharedPtr<FJsonObject> RuntimeBusinessBaseline;
    TSharedPtr<FJsonObject> RuntimeBusinessConfig;
    int32 RuntimeBusinessRevision = -1;
    TMap<FString, FTransform> RuntimeDragStartTransforms;
    TArray<FRuntimeEditValue> RuntimeDragBeforeValues;
    TArray<FBox> RuntimeSelectionLocalBounds;
    FBox RuntimeSelectionWorldBounds = FBox(ForceInit);
    FVector RuntimeSelectionPivotWorld = FVector::ZeroVector;
    float RuntimeSelectionYawDelta = 0.0f;
    float RuntimeDragStartGroupYawDelta = 0.0f;

    // ── 内部方法 ─────────────────────────────────────────────────────────

    /** 拼接快照接口 URL（SceneId 非空时追加 ?scene= 查询参数） */
    FString BuildSnapshotsUrl() const;

    /** 拼接 4.1 增量快照 URL（可携带不透明 cursor）。 */
    FString BuildSnapshotChangesUrl() const;

    /** 给 UE→后端请求附加 UE 工程身份头（用于数据集强绑定校验） */
    void AddUEProjectHeaders(TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest) const;

    void SetPollTimerInterval(float IntervalSeconds, float FirstDelaySeconds = 0.0f);

    /** 定时轮询回调 */
    void PollBackend();

    void EnsureModelLoadingWidget();
    void UpdateInitialModelLoadingProgress(int32 Completed, int32 Total);
    void CompleteInitialModelBaseline(int32 Total);
    void FailInitialModelBaseline(const FString& Detail);
    void DismissModelLoadingWidget();

    /** HTTP 响应回调 */
    void OnPollResponse(FHttpRequestPtr HttpRequest, FHttpResponsePtr Response, bool bWasSuccessful);

    /** 4.1 增量快照响应回调。 */
    void OnIncrementalPollResponse(FHttpRequestPtr HttpRequest, FHttpResponsePtr Response, bool bWasSuccessful);

    /** 新接口不兼容时，仅在当前会话退回旧全量接口。 */
    void FallBackToFullSnapshots(const FString& Reason);

    /** 连接、消费并自动重连 AGV 实时状态流。 */
    void ConnectRealtimeWebSocket();
    /** 在启动竞态窗口结束后选择 Provider 或兼容回退 owner。 */
    void ProbeRealtimeProviderAndConnect();
    /** 每帧轻量校验：Provider 晚注册时立即收回兼容 socket。 */
    void ReconcileRealtimeProviderOwnership();
    /** 查找当前 World 的 Provider，并记录同 World 多 owner 诊断。 */
    IOntoTwinRealtimeStreamProvider* FindRealtimeProviderForWorld(bool bLogDuplicates = false) const;
    /** 停止兼容回退 socket并清空它持有的瞬时统计。 */
    void StopLegacyRealtimeSocketForExternalProvider();
    void ScheduleRealtimeReconnect();
    void HandleRealtimeMessage(const FString& Message);

    /** 编辑器预览 HTTP 回调：把快照 spawn 成 transient 预览 Actor */
    void OnPreviewResponse(FHttpRequestPtr HttpRequest, FHttpResponsePtr Response, bool bWasSuccessful);

    /** 解析快照数组、生成 transient TwinInstance，并写可机读的资产加载审计。 */
    int32 SpawnPreviewActorsFromJson(const FString& JsonPayload, const FString& SourceLabel);

    /** 从拉取快照和已生成组件捕获一次材质并发基线。 */
    bool CapturePreviewMaterialBaseline(
        ATwinInstance* Instance,
        const TSharedPtr<FJsonObject>& Snapshot,
        FString* OutError = nullptr);

    /** Saved/OntoTwinMigration/ue_snapshots.json */
    FString MigrationPreviewSnapshotPath() const;

    /** Saved/OntoTwinMigration/ue_preview_audit.json */
    FString MigrationPreviewAuditPath() const;

    /** 处理单个实例快照；delta 模式下缺席接口保持现状。 */
    bool ProcessSnapshot(const TSharedPtr<FJsonObject>& Snapshot, bool bIsDelta = false);

    /** Spawn 新的孪生体 Actor */
    ATwinInstance* SpawnTwinInstance(const FString& InstanceId, const TSharedPtr<FJsonObject>& Snapshot);

    /** 销毁孪生体 Actor */
    void DestroyTwinInstance(const FString& InstanceId);

    void TickRuntimeEditor(float DeltaTime);
    void TickOverlays();
    void FocusManagedBounds(
        const FBox& Bounds,
        const FString& FocusLabel,
        int32 InstanceCount) const;
    ATwinInstance* FindAlwaysOverlayAtScreenPosition(
        APlayerController* PlayerController,
        const FVector2D& ScreenPosition) const;
    void SelectOverlayInstance(ATwinInstance* Instance);
    void ClearOverlaySelection();
    void UpdateAlwaysOverlays(APlayerController* PlayerController);
    void UpdateOverlayPointerInput(APlayerController* PlayerController, bool bShouldOwnPointer);
    void RefreshOverlayMediaForSelection(bool bForceResolve = false);
    void RequestOverlayMediaResolve(bool bResetRetry = false);
    void OnOverlayMediaResolveResponse(
        FHttpRequestPtr HttpRequest,
        FHttpResponsePtr Response,
        bool bWasSuccessful);
    void EnsureOverlayMediaPlayer();
    void UpdateOverlayMediaPlaybackClock();
    void HandleOverlayMediaAction(EOntoTwinOverlayMediaAction Action);
    void ScheduleOverlayMediaRetry(const FString& StatusMessage);
    void StopOverlayMedia(bool bResetWidget = true);

    UFUNCTION()
    void OnOverlayMediaOpened(FString OpenedUrl);

    UFUNCTION()
    void OnOverlayMediaTracksChanged();

    UFUNCTION()
    void OnOverlayMediaOpenFailed(FString FailedUrl);

    UFUNCTION()
    void OnOverlayMediaEndReached();

    UFUNCTION()
    void OnOverlayMediaPlaybackResumed();

    UFUNCTION()
    void OnOverlayMediaPlaybackSuspended();

    void RequestRuntimeEditToggle();
    void EnterRuntimeEditMode();
    void ExitRuntimeEditMode();
    bool StartRuntimeEditorCamera(APlayerController* PlayerController);
    void StopRuntimeEditorCamera(APlayerController* PlayerController);
    void TickRuntimeEditorCamera(
        APlayerController* PlayerController,
        float DeltaTime,
        bool bPointerOverRuntimePanel);
    void BeginRuntimeEditorCameraLook(APlayerController* PlayerController);
    void EndRuntimeEditorCameraLook(APlayerController* PlayerController);
    FTransform BuildRuntimeEditorCameraTransform(APlayerController* PlayerController) const;
    void ShowRuntimeEditorPanel();
    void HideRuntimeEditorPanel();
    void EnsureRuntimeGizmo();
    void UpdateRuntimeEditorPanel();
    void CheckRuntimeBindingStatus();
    void SelectRuntimeInstance(ATwinInstance* Instance, bool bToggleSelection = false);
    void ClearRuntimeSelection(bool bRestoreBaseline);
    FRuntimeEditInstanceState& EnsureRuntimeEditState(ATwinInstance* Instance);
    void ReleaseRuntimeEditState(const FString& InstanceId, bool bRestoreBaseline);
    void ReleaseCleanUnselectedRuntimeStates();
    void RefreshRuntimeEditDirtyState();
    void RefreshRuntimeBusinessDirtyState();
    bool RefreshRuntimeBusinessSnapshot();
    TSharedPtr<FJsonObject> CloneRuntimeBusinessConfig(
        const TSharedPtr<FJsonObject>& Source) const;
    void GetRuntimeSelectedInstanceIds(TArray<FString>& OutInstanceIds) const;
    void UpdateRuntimeSelectionGeometry(bool bKeepDragPivot = false);
    void RunRuntimeEditSelfTest();
    bool CanStageRuntimeSelection() const;
    void RecordRuntimeCommand(FRuntimeEditCommand&& Command);
    void ApplyRuntimeCommand(const FRuntimeEditCommand& Command, bool bUseAfter);
    FRuntimeEditValue CaptureRuntimeValue(ATwinInstance* Instance) const;
    void ApplyRuntimeValue(const FRuntimeEditValue& Value);
    void ApplyRuntimeGroupTransform(const FVector& NewPivot, float YawDeltaDegrees);
    float CalculateRuntimeSelectionRadiusAlongNormal(
        const FVector& WorldNormal,
        float YawDeltaDegrees) const;
    void ApplyRuntimeGroupSnaps(FVector& InOutPivot, float& InOutYawDelta);
    void FinishExitRuntimeEditMode();
    bool CalculateRuntimeEditLocalBounds(AActor* Actor, FBox& OutLocalBounds) const;
    FVector CalculateRuntimeActorLocationForPivot(
        const FVector& PivotWorld,
        const FRotator& Rotation,
        const FVector& Scale) const;
    float CalculateRuntimeBoundsRadiusAlongNormal(
        const FVector& WorldNormal,
        const FRotator& Rotation,
        const FVector& Scale) const;
    bool TraceRuntimeCursor(FHitResult& OutHit) const;
    bool GetRuntimeCursorPointOnPlane(const FPlane& Plane, FVector& OutPoint) const;
    bool GetRuntimeCursorPlanePoint(FVector& OutPoint) const;
    void BeginRuntimeGizmoDrag(ERuntimeDragPart Part);
    void UpdateRuntimeGizmoDrag();
    void EndRuntimeGizmoDrag(bool bCommit = true);
    void SetRuntimeCameraLookSuppressed(bool bSuppress);
    void ApplyRuntimeSnaps(FVector& InOutLocation, FRotator& InOutRotation);
    void MarkRuntimeDirtyFromTransform();
    void ApplyRuntimeSnapshotIfPresent(const TSharedPtr<FJsonObject>& ResponseObj);

    /** FR-6 迁移导出文件绝对路径（Saved/OntoTwinMigration/ue_actors_export.json） */
    FString MigrationExportPath() const;
    /** FR-6 迁移结果文件绝对路径（Saved/OntoTwinMigration/ue_migration_result.json） */
    FString MigrationResultPath() const;
};
