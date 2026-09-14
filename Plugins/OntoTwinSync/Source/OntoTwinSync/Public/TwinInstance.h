// ============================================================================
// TwinInstance.h
//
// 孪生体实例 Actor — 每个后端实例在 UE 中的渲染载体
//
// 功能说明：
//   1. 根据 asset_id（UE 内容路径）动态加载 StaticMesh、SkeletalMesh 或 Actor Blueprint
//   2. 接收 ATwinSceneManager 下发的 JSON 快照，驱动空间/材质/行为
//   3. 支持编辑器模式固化：可手动放置到关卡并在编辑器里调整位置
//   4. bLocalOverrideLock：锁定后忽略后端的空间变换数据
//
// 使用方式：
//   ● 自动模式：由 ATwinSceneManager 运行时自动 Spawn
//   ● 固化模式：通过 ATwinSceneManager 的"快照固化"按钮生成编辑器持久 Actor
// ============================================================================

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/ChildActorComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/WidgetComponent.h"
#include "Dom/JsonObject.h"
#include "TwinInstance.generated.h"

class UDigitalTwinSyncComponent;
class UOntoTwinOverlayWidget;
class UMeshComponent;
class UTwinRepresentationHostComponent;
class ATwinInstance;

/** Runtime representation mesh became renderable (for one-shot scene captures). */
DECLARE_MULTICAST_DELEGATE_OneParam(
    FOnTwinRepresentationVisualReady,
    ATwinInstance*);

// ============================================================================
// 动画配方结构体（内置库使用）
// ============================================================================
struct FAnimRecipe
{
    FVector  TranslationDelta; // 每次循环相对位移（cm）
    FRotator RotationDelta;    // 每次循环额外旋转度数
    float    Duration;         // 单次时长（秒）
    bool     bLoop;            // 是否循环
    bool     bPingPong;        // 是否来回往返

    FAnimRecipe()
        : TranslationDelta(FVector::ZeroVector)
        , RotationDelta(FRotator::ZeroRotator)
        , Duration(1.0f)
        , bLoop(true)
        , bPingPong(true)
    {}

    FAnimRecipe(FVector InTrans, FRotator InRot, float InDur, bool InLoop, bool InPingPong)
        : TranslationDelta(InTrans)
        , RotationDelta(InRot)
        , Duration(InDur)
        , bLoop(InLoop)
        , bPingPong(InPingPong)
    {}
};

/**
 * ATwinInstance
 *
 * 单个数字孪生体在 UE 场景中的具象化 Actor。
 * 由 ATwinSceneManager 管控生命周期。
 */
UCLASS(ClassGroup=(DigitalTwin), meta=(DisplayName="孪生体实例"))
class ONTOTWINSYNC_API ATwinInstance : public AActor
{
    GENERATED_BODY()

public:
    ATwinInstance();

    /** Process-local signal emitted after a runtime GLB has produced its final mesh. */
    static FOnTwinRepresentationVisualReady OnRepresentationVisualReady;

    // ═══════════════════════════════════════════════════════════════════════
    // 编辑器可配置属性
    // ═══════════════════════════════════════════════════════════════════════

    /** 实例 ID（关联后端数据） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="孪生体|标识",
              meta=(DisplayName="实例ID"))
    FString InstanceId;

    /** 后端实例显示名，供 Runtime Editor 等运行时界面使用。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="孪生体|标识",
              meta=(DisplayName="实例显示名"))
    FString TwinDisplayName;

    /** 后端对象类型名称，供 Runtime Editor 等运行时界面使用。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="孪生体|标识",
              meta=(DisplayName="类型名称"))
    FString TwinObjectTypeName;

    /** OntoTwin 数据侧是否允许 F10 修改空间状态；缺失时兼容为允许。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="孪生体|同步控制",
              meta=(DisplayName="允许运行时空间编辑"))
    bool bRuntimeSpatialEditable = true;

    /** Runtime Editor 可写字段的后端摘要，用于原子保存时检测并发冲突。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="孪生体|同步控制",
              meta=(DisplayName="运行时编辑状态摘要"))
    FString RuntimeEditStateHash;

    /** UE 资产路径（/Game/...） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="孪生体|标识",
              meta=(DisplayName="UE资产路径"))
    FString AssetPath;

    /** Optional project Blueprint used as a child-Actor representation container. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="孪生体|标识",
              meta=(DisplayName="容器Blueprint路径"))
    FString ContainerBlueprintPath;

    /** Logical slot resolved by UTwinRepresentationHostComponent. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="孪生体|标识",
              meta=(DisplayName="容器槽位"))
    FString ContainerSlot = TEXT("primary");

    /** 🔒 本地锁定：锁定后，后端空间变换数据不会覆盖编辑器中的位置/旋转/缩放 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="孪生体|同步控制",
              meta=(DisplayName="🔒 锁定本地空间变换"))
    bool bLocalOverrideLock = false;

    /** 是否由编辑器"快照固化"或手动放置（非运行时 Spawn） */
    UPROPERTY(VisibleAnywhere, Category="孪生体|同步控制",
              meta=(DisplayName="编辑器预置"))
    bool bEditorPlaced = false;

    // ═══════════════════════════════════════════════════════════════════════
    // 视觉特效属性与蓝图接口
    // ═══════════════════════════════════════════════════════════════════════

    /** 灰模材质 (用于 material_variant = gray) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="孪生体|视觉展示", meta=(DisplayName="灰模通用材质"))
    UMaterialInterface* MatGray = nullptr;

    /** 线框材质 (用于 material_variant = wireframe) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="孪生体|视觉展示", meta=(DisplayName="线框通用材质"))
    UMaterialInterface* MatWireframe = nullptr;

    /** 缓存加载时的原始材质（以支持从灰模/线框恢复） */
    UPROPERTY()
    TArray<UMaterialInterface*> OriginalMaterials;

    // ── 3D 文字标签配置 ──────────────────────────────────────────────────────

    /** 标签字体（在 Details 面板中拖入已导入的 Font 资产，支持中文） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="孪生体|文字标签", meta=(DisplayName="标签字体"))
    UFont* LabelFont = nullptr;

    /** 标签文字大小（世界坐标单位，默认 8cm） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="孪生体|文字标签", meta=(DisplayName="文字大小"))
    float LabelWorldSize = 8.0f;

    /** 标签颜色 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="孪生体|文字标签", meta=(DisplayName="文字颜色"))
    FColor LabelColor = FColor::White;

    /** 相对模型原点的 Z 轴偏移（cm），默认 20cm */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="孪生体|文字标签", meta=(DisplayName="标签高度偏移"))
    float LabelZOffset = 20.0f;

    /** 当从 Web 接收到全新的动画指令（平移/跳跃/翻转）时抛出，供美术在蓝图中实现运镜或 Timeline */
    UFUNCTION(BlueprintImplementableEvent, Category="孪生体|行为事件", meta=(DisplayName="当触发新动画状态时"))
    void OnAnimationStateChanged(const FString& NewAnimState);

    /**
     * 当 Web 端下发的 material_variant 不在 C++ 内置列表中时抛出。
     * 蓝图可在此事件中通过 Map 查找并应用自定义材质。
     * @param VariantName  前端传来的变体名称字符串，如 "metal", "glass", "xray"
     */
    UFUNCTION(BlueprintImplementableEvent, Category="孪生体|视觉事件", meta=(DisplayName="当材质变体变化时"))
    void OnMaterialVariantChanged(const FString& VariantName);

    /**
     * 当 Web 端下发特效触发指令时抛出。
     * 蓝图可在此事件中通过 Map 查找对应的 Niagara/Particle System 并播放。
     * @param FxName  前端传来的特效名称字符串，如 "fire", "smoke", "warning_glow"
     */
    UFUNCTION(BlueprintImplementableEvent, Category="孪生体|行为事件", meta=(DisplayName="当触发特效时"))
    void OnFxTriggered(const FString& FxName);

    // ═══════════════════════════════════════════════════════════════════════
    // 公开接口（供 ATwinSceneManager 调用）
    // ═══════════════════════════════════════════════════════════════════════

    /** 初始化孪生体：加载资产、配置同步组件 */
    void InitializeTwin(
        const FString& InInstanceId,
        const FString& InAssetPath,
        const FString& InBackendBaseUrl,
        const FString& InContainerBlueprintPath,
        const FString& InContainerSlot);

    /** 应用后端快照到 Actor（由 SceneManager 每 500ms 调用） */
    void ApplySnapshot(const TSharedPtr<FJsonObject>& Snapshot, bool bIsDelta = false);

    /**
     * 执行语义表现路由的受控入口。工业/项目插件通过
     * IOntoTwinPresentationExecutor 注册实现；核心层负责发现、去重和安全回退。
     */
    bool ExecutePresentationRoute(
        const FString& Channel,
        const FString& BehaviorId,
        const FString& Slot,
        const FString& Source = TEXT("platform"),
        const TSharedPtr<FJsonObject>& Params = nullptr);

    /** 供可选行为插件调用的稳定核心动作，不暴露 UE 资产路径。 */
    void PlayPresentationAnimation(const FString& StateName);
    void ApplyPresentationMaterialVariant(const FString& VariantName);
    void TriggerPresentationFx(const FString& FxName);

    /** 应用 WebSocket 实时空间数据；保持期内优先于 HTTP 快照。 */
    void ApplyRealtimeSpatial(double X, double Y, double HeadingDeg, float HoldSeconds);

    /** 当前实例的空间坐标是否仍由实时流持有。 */
    bool IsRealtimeSpatialActive() const;

    /** 获取实例 ID */
    FString GetInstanceId() const { return InstanceId; }

    /** 获取后端实例显示名；为空时回退到实例 ID。 */
    FString GetTwinDisplayName() const { return TwinDisplayName.IsEmpty() ? InstanceId : TwinDisplayName; }

    /** 获取后端对象类型名称。 */
    const FString& GetTwinObjectTypeName() const { return TwinObjectTypeName; }

    /** 当前主表现（含 Blueprint 子 Actor）的世界包围盒。 */
    FBox GetRepresentationWorldBounds(bool bIncludeNonColliding = true) const;

    /** 当前主表现转换到 TwinInstance 局部空间后的包围盒。 */
    bool GetRepresentationLocalBounds(FBox& OutLocalBounds) const;

    bool IsRuntimeSpatialEditable() const { return bRuntimeSpatialEditable; }
    const FString& GetRuntimeEditStateHash() const { return RuntimeEditStateHash; }
    bool IsRuntimeLoaded() const { return bRuntimeLoaded; }
    void SetRuntimeEditorLoadedOverride(bool bLoaded);
    void ClearRuntimeEditorLoadedOverride();

    /** assembly_v1 预览审计：当前已实际创建的静态网格部件数。 */
    int32 GetRenderPartComponentCount() const { return RenderPartComponents.Num(); }

    /** 读取预览组件当前实际使用的材质路径（包含编辑器里的组件材质覆写）。 */
    bool GetRenderPartMaterialPaths(int32 PartIndex, TArray<FString>& OutPaths) const;

    /** 读取部件 StaticMesh 资产已保存的默认材质槽路径。 */
    bool GetSavedRenderPartMaterialPaths(int32 PartIndex, TArray<FString>& OutPaths) const;

    /** assembly_v1 预览审计：仅在全部网格与材质加载成功时保留导出签名。 */
    const FString& GetCurrentAssemblySignature() const { return CurrentAssemblySignature; }

    /** assembly_v1 预览审计：当前快照是否正在驱动复合表现。 */
    bool IsAssemblyRenderActive() const { return bAssemblyRenderActive; }
    bool IsContainerRepresentationActive() const;

    /**
     * assembly_v1 预览审计：逐项核对已创建的部件与快照 render_parts。
     *
     * 返回 false 时 OutFailures 中每项都是可直接写入审计 JSON 的结构化对象，
     * 包含 part_index / field / expected / actual，材质错误还包含 material_slot。
     * bOverallVisible 应传 I3D_Representable.is_visible，因为实际部件可见性是
     * overall visibility 与源部件 visible 的合并结果。此方法只读，不加载资产、不改变组件。
     */
    bool ValidateRenderPartsAgainstSnapshot(
        const TArray<TSharedPtr<FJsonValue>>& RenderParts,
        bool bOverallVisible,
        TArray<TSharedPtr<FJsonValue>>& OutFailures) const;

    bool HasSelectedOverlay() const { return bOverlayEnabled && OverlayDisplayMode == TEXT("selected"); }
    bool HasAlwaysOverlay() const { return bOverlayEnabled && OverlayDisplayMode == TEXT("always"); }
    bool HasOverlay() const { return bOverlayEnabled && CurrentOverlayData.IsValid(); }
    FVector GetOverlayAnchorWorldLocation() const;
    float GetOverlayRenderWidthPixels() const;
    bool IsScreenPointOverAlwaysOverlay(
        APlayerController* PlayerController,
        const FVector2D& ScreenPoint,
        float PaddingPixels = 8.0f) const;
    TSharedPtr<FJsonObject> GetOverlayData() const { return CurrentOverlayData; }
    uint64 GetOverlayPayloadSerial() const { return OverlayPayloadSerial; }
    void RefreshAlwaysOverlay(const FVector& CameraLocation, bool bShouldShow, float WorldScale = -1.0f);

    /** Runtime Editor: stop local behavior animation while a gizmo edit owns spatial transform. */
    FString PauseRuntimeEditorAnimation(bool& bOutWasRunning);

    /** Runtime Editor: restore the behavior animation that was active before editing, if requested. */
    void ResumeRuntimeEditorAnimation(const FString& PreviousState, bool bWasRunning);

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

    // ── 组件（供蓝图访问） ────────────────────────────────────────────────
    /** 网格体组件 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="孪生体", meta=(AllowPrivateAccess="true"))
    UStaticMeshComponent* MeshComponent = nullptr;

    /** 直接绑定 SkeletalMesh 时使用；不自动推断动画。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="孪生体", meta=(AllowPrivateAccess="true"))
    USkeletalMeshComponent* SkeletalMeshComponent = nullptr;

    /** 直接绑定普通 Actor Blueprint 时使用；子 Actor 保留自身生命周期。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="孪生体", meta=(AllowPrivateAccess="true"))
    UChildActorComponent* BlueprintActorComponent = nullptr;

    /** assembly_v1 复合实例的动态渲染部件；母实例仍只有一个 Actor。 */
    UPROPERTY(Transient)
    TArray<UStaticMeshComponent*> RenderPartComponents;

    /** 每个复合部件在源关卡中的可见状态，供全局 is_visible 往返切换后恢复。 */
    TArray<bool> RenderPartSourceVisibility;

    /** 3D 文字标签组件（显示 ui_label_content） */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="孪生体", meta=(AllowPrivateAccess="true"))
    UTextRenderComponent* LabelComponent = nullptr;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="孪生体|顶部信息面板", meta=(AllowPrivateAccess="true"))
    UWidgetComponent* OverlayWidgetComponent = nullptr;

private:
    enum class ERepresentationKind : uint8
    {
        None,
        StaticMesh,
        SkeletalMesh,
        BlueprintActor,
        ContainerBlueprint
    };

    // ── 同步组件 ─────────────────────────────────────────────────────────────

    /** 同步组件（复用老插件） */
    UPROPERTY()
    UDigitalTwinSyncComponent* SyncComponent = nullptr;

    // ── 属性 ─────────────────────────────────────────────────────────────

    /** 后端 API 基础地址 */
    FString BackendBaseUrl;

    /** 是否已完成初始化 */
    bool bInitialized = false;

    /** 当前是否由 I3D_Representable.render_parts 驱动。 */
    bool bAssemblyRenderActive = false;

    /** 当前单资产表现类型；assembly_v1 仍由 bAssemblyRenderActive 标识。 */
    ERepresentationKind ActiveRepresentationKind = ERepresentationKind::None;

    /** I3D_Visual.is_visible 的最后值；与 Representable 的加载状态分离。 */
    bool bVisualVisible = true;

    bool bRuntimeLoaded = true;
    bool bRuntimeEditorLoadedOverride = false;

    /** 防止 500ms 快照轮询重复销毁/重建相同的复合部件。 */
    FString CurrentAssemblySignature;

    /** render_parts 完整状态摘要；装配签名不变但材质变化时仍会刷新组件。 */
    FString CurrentRenderPartsStateHash;

    // ── 内部方法 ─────────────────────────────────────────────────────────

    /** 根据 asset_id 加载 StaticMesh、SkeletalMesh 或 Actor Blueprint。 */
    bool LoadMeshFromPath(const FString& MeshPath);

    /** 清理当前单资产表现，不影响 assembly_v1 动态部件。 */
    void ClearSingleAssetRepresentation();

    /** 加载直接绑定的骨骼网格；只显示参考姿势。 */
    bool LoadSkeletalMeshAsset(const FString& ObjectPath);

    /** 加载普通 Actor Blueprint，并通过 ChildActorComponent 运行。 */
    bool LoadBlueprintActorAsset(const FString& ObjectPath);

    /** Create a project BP child Actor and inject a cooked StaticMesh into its host slot. */
    bool LoadContainerBlueprintRepresentation(
        const FString& BlueprintPath,
        const FString& SlotName,
        const FString& StaticMeshPath);

    /** Reload current assembly-free representation after load/unload or hot changes. */
    bool ReloadCurrentSingleRepresentation();

    UTwinRepresentationHostComponent* FindContainerHost(
        AActor* ChildActor,
        const FString& SlotName) const;

    /** 把 Blueprint 对象路径规范化为 Package.Asset_C 生成类路径。 */
    static FString MakeBlueprintGeneratedClassPath(const FString& ObjectPath);

    /** 校验 Blueprint 生成类是否属于 4.3.0 支持的普通表现 Actor。 */
    static bool IsSupportedBlueprintActorClass(const UClass* ActorClass, FString& OutReason);

    /** 当前直接网格表现，用于统一材质缓存与恢复。 */
    UMeshComponent* GetActiveMeshComponent() const;

    /** 应用 I3D_Visual 显隐，并显式同步 Blueprint 子 Actor。 */
    void ApplyVisualVisibility(bool bVisible);

    /** assembly_v1：按母 Actor 相对变换创建多个静态网格部件。 */
    void ApplyRenderPartsFromSnapshot(
        const TArray<TSharedPtr<FJsonValue>>& RenderParts,
        const FString& AssemblySignature);

    /** 销毁 assembly_v1 动态部件并回到旧版单 Mesh 模式。 */
    void ClearRenderParts();

    /** 运行时从磁盘加载 glb/gltf（glTFRuntime）；成功返回 true 并已 SetStaticMesh */
    bool LoadRuntimeGltf(const FString& AssetId);

    /** 从绝对路径加载 glb（glTFRuntime 核心，被本地/远程缓存复用） */
    bool LoadGltfFromFile(const FString& FilePath);

    /** 把 asset_id 解析成 glb 文件的绝对磁盘路径（固定目录/exe 旁 Models/） */
    FString ResolveModelFilePath(const FString& AssetId) const;

    /** ArtStudio 远程加载：artstudio:{id}:v{n} → 命中缓存即时加载，否则占位 Cube + 异步下载（3.3） */
    void LoadRemoteGltf(const FString& StableId);

    /** ArtStudio 下载失败后的有限指数退避重试。 */
    void ScheduleRemoteGltfRetry(const FString& StableId);

    /** 设置占位立方体（加载失败/下载中） */
    void SetPlaceholderCube();

    /** 清理同一 ArtStudio 资产的旧版本缓存（保留 KeepFile），防升版后缓存堆积 */
    void PurgeOldCacheVersions(const FString& AssetId, const FString& KeepFile);

    /** 正在下载的稳定标识，防重复请求；空=无下载在途 */
    FString PendingRemoteId;

    /** 当前稳定标识的下载重试代次；模型改绑后自动清零。 */
    FString RemoteRetryId;
    int32 RemoteRetryAttempt = 0;

    /** 把当前的材质全部存下 */
    void CacheOriginalMaterials();

    /** 还原材质 */
    void RestoreOriginalMaterials();

    /** 从 JSON 接口数据中驱动三大能力 */
    void ApplySpatialFromSnapshot(const TSharedPtr<FJsonObject>& SpatialObj);
    void ApplyPresentationFromSnapshot(const TSharedPtr<FJsonObject>& PresentationObj);
    void ApplyVisualFromSnapshot(const TSharedPtr<FJsonObject>& VisualObj);
    void ApplyBehavioralFromSnapshot(const TSharedPtr<FJsonObject>& BehaviorObj);
    void ApplyRepresentableFromSnapshot(
        const TSharedPtr<FJsonObject>& RepObj,
        bool bIsDelta);
    void ApplyRuntimeLoadedVisibility(bool bVisible);
    void ApplyOverlayFromSnapshot(const TSharedPtr<FJsonObject>& OverlayObj);
    void UpdateWorldOverlayRenderTarget();
    void ClearOverlay();

    /** UE 世界时间；超过该时刻后 HTTP 空间快照可自动接管。 */
    double RealtimeSpatialValidUntilSeconds = -1.0;

    /** 当前材质变体缓存 */
    FString CurrentMaterialVariant;

    /** 当前动画状态缓存 */
    FString CurrentAnimState;

    /** 当前特效状态缓存 */
    FString CurrentFxTrigger;

    TWeakObjectPtr<UTwinRepresentationHostComponent> ActiveContainerHost;

    /** 当前标签文字缓存（防止重复刷新）*/
    FString CurrentLabelContent;

    /** I3D_Presentation 存在时，Visual/Behavioral 的旧动画/特效字段只作兼容投影。 */
    bool bPresentationAuthoritative = false;
    int32 CurrentPresentationRevision = 0;
    FString CurrentPresentationDecisionId;
    TSet<FString> ConsumedPresentationEventIds;
    TMap<FString, FString> ActivePresentationRouteKeys;

    UPROPERTY()
    UOntoTwinOverlayWidget* WorldOverlayWidget = nullptr;

    TSharedPtr<FJsonObject> CurrentOverlayData;
    FString OverlayDisplayMode;
    FVector OverlayOffsetCm = FVector(0.0f, 0.0f, 20.0f);
    bool bOverlayEnabled = false;
    uint64 OverlayPayloadSerial = 0;

    // ── 程序化动画状态 ────────────────────────────────────────────────
    /** 内置动画配方字典（state名 → 执行配方）*/
    TMap<FString, FAnimRecipe> AnimLibrary;

    /** 动画计时器 */
    float  AnimTimer = 0.0f;

    /** 动画各自的基准動画开始时的位置和旋转（用于计算相对偏移） */
    FVector  AnimBaseLocation  = FVector::ZeroVector;
    FRotator AnimBaseRotation  = FRotator::ZeroRotator;

    /** 当前活跃的动画配方 */
    FAnimRecipe ActiveRecipe;

    /** 动画是否正在运行 */
    bool bAnimRunning = false;

    /** 动画内部立即切换动画状态 */
    void PlayAnimationState(const FString& StateName);

    /** 初始化动画配方字典 */
    void InitAnimLibrary();

    /** 插值目标值 */
    FVector TargetLocation = FVector::ZeroVector;
    FRotator TargetRotation = FRotator::ZeroRotator;
    FVector TargetScale = FVector::OneVector;

    /** 位置/旋转插值速度 */
    float LocationInterpSpeed = 5.0f;
    float RotationInterpSpeed = 5.0f;
};
