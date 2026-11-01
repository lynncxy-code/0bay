// ============================================================================
// DigitalTwinSyncComponent.h
// 
// 数字孪生同步组件 — UE5 渲染适配层
//
// ⚠ LEGACY（1.x 链路）：对接旧版 /api/state + /api/update，配合 index.html 演示页。
//   新场景请用 2.x 的 TwinSceneManager + TwinInstance（/api/v2/state/snapshots）。
//   保留原因：本组件含全项目唯一的 UE→后端回写能力（PushStateToBackend）。
//
// 功能说明：
//   本组件附加到场景中的飞机 Actor 上，每 0.5 秒通过 HTTP GET 请求从后端
//   获取权威状态 JSON，并根据三大本体接口（I3DSpatial / I3DVisual / I3DBehavior）
//   驱动 Actor 的空间变换、材质切换、动画状态和粒子特效。
//
// 使用方式：
//   1. 将此组件添加到飞机 Actor（可在蓝图中 Add Component）
//   2. 在 Actor 的 Tags 中添加一个与后端 JSON 中 asset_id 对应的标签
//      例如：Tags[0] = "aircraft_001"
//   3. 运行时组件会自动轮询后端并驱动 Actor 行为
//
// 依赖模块（.Build.cs 中需添加）：
//   "Http", "HttpServer", "Json", "JsonUtilities", "Niagara", "UMG"
// ============================================================================

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "NiagaraComponent.h"
#include "Components/WidgetComponent.h"
#include "DigitalTwinSyncComponent.generated.h"

// ── 前向声明 ─────────────────────────────────────────────────────────────────
class UNiagaraComponent;
class UWidgetComponent;
class UMaterialInstanceDynamic;

/**
 * UDigitalTwinSyncComponent
 * 
 * 本体驱动的数字孪生渲染适配器组件。
 * 将后端 REST API 返回的 JSON 状态实时映射到所属 Actor 的
 * 位置、旋转、缩放、材质、动画、粒子和 UI 标签。
 */
UCLASS(ClassGroup=(DigitalTwin), meta=(BlueprintSpawnableComponent),
       DisplayName="数字孪生同步组件")
class ONTOTWINSYNC_API UDigitalTwinSyncComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UDigitalTwinSyncComponent();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
                               FActorComponentTickFunction* ThisTickFunction) override;

    // ═══════════════════════════════════════════════════════════════════════
    // 可在编辑器中配置的属性
    // ═══════════════════════════════════════════════════════════════════════

    /** 后端 API 地址（完整 URL） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="数字孪生|连接",
              meta=(DisplayName="后端API地址"))
    FString ApiUrl = TEXT("http://localhost:5000/api/state");

    /** 轮询间隔（秒），默认 0.5 秒 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="数字孪生|连接",
              meta=(DisplayName="轮询间隔(秒)", ClampMin="0.1", ClampMax="10.0"))
    float PollIntervalSeconds = 0.5f;

    /** 位置插值速度（越大越快到达目标位置） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="数字孪生|空间",
              meta=(DisplayName="位置插值速度"))
    float LocationInterpSpeed = 5.0f;

    /** 旋转插值速度 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="数字孪生|空间",
              meta=(DisplayName="旋转插值速度"))
    float RotationInterpSpeed = 5.0f;

    /** 匹配用的实体 ID（若为空则自动从 Actor Tags[0] 读取） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="数字孪生|连接",
              meta=(DisplayName="实体ID(可选)"))
    FString InstanceId;

    // ── I3DVisual：材质变体映射 ───────────────────────────────────────────
    /** "normal" 状态对应的颜色 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="数字孪生|视觉")
    FLinearColor ColorNormal = FLinearColor(0.8f, 0.8f, 0.8f, 1.0f);

    /** "fault" 状态对应的颜色 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="数字孪生|视觉")
    FLinearColor ColorFault = FLinearColor(1.0f, 0.15f, 0.15f, 1.0f);

    /** "alarm" 状态对应的颜色 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="数字孪生|视觉")
    FLinearColor ColorAlarm = FLinearColor(1.0f, 0.7f, 0.0f, 1.0f);

    /** "offline" 状态对应的颜色 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="数字孪生|视觉")
    FLinearColor ColorOffline = FLinearColor(0.3f, 0.3f, 0.3f, 1.0f);

    // ── I3DBehavior：Niagara 特效组件引用 ─────────────────────────────────
    /** 电火花（Spark）Niagara 粒子组件（在蓝图中拖入关联） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="数字孪生|行为",
              meta=(DisplayName="电火花 Niagara 组件"))
    UNiagaraComponent* SparkFxComponent = nullptr;

    /** 烟雾（Smoke）Niagara 粒子组件 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="数字孪生|行为",
              meta=(DisplayName="烟雾 Niagara 组件"))
    UNiagaraComponent* SmokeFxComponent = nullptr;

    /** 头顶 UI 标签的 Widget 组件 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="数字孪生|行为",
              meta=(DisplayName="UI标签 Widget 组件"))
    UWidgetComponent* LabelWidgetComponent = nullptr;

private:
    // ── 内部状态 ─────────────────────────────────────────────────────────
    /** 轮询计时器 */
    float TimeSinceLastPoll = 0.0f;

    /** 请求锁：防止在上一次请求未返回前发起重复请求 */
    bool bRequestInFlight = false;

    /** 上次收到的时间戳，用于判断状态是否变化 */
    double LastTimestamp = 0.0;

    // ── 目标值（用于插值平滑） ───────────────────────────────────────────
    FVector TargetLocation = FVector::ZeroVector;
    FRotator TargetRotation = FRotator::ZeroRotator;
    FVector TargetScale = FVector::OneVector;

    /** 当前材质变体缓存，避免重复切换 */
    FString CurrentMaterialVariant;

    /** 当前动画状态缓存 */
    FString CurrentAnimationState;

    /** 当前特效缓存 */
    FString CurrentFxTrigger;

    /** 动态材质实例（运行时创建） */
    UPROPERTY()
    UMaterialInstanceDynamic* DynMaterial = nullptr;

    // ── 内部方法 ─────────────────────────────────────────────────────────

    /** 发起一次 HTTP GET 请求 */
    void SendHttpRequest();
    void OnHttpResponseReceived(FHttpRequestPtr Request,
                                FHttpResponsePtr Response,
                                bool bWasSuccessful);

    /** 将 UE 实体的当前空间坐标推送到后端 */
    void PushStateToBackend();
    void OnStatePushed(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);

    /** 解析 JSON 并驱动 Actor 行为 */
    void ApplyStateFromJson(TSharedPtr<FJsonObject> JsonObject);

    // ── 三大接口驱动函数 ─────────────────────────────────────────────────
    void ApplySpatial(const TSharedPtr<FJsonObject>& Json);
    void ApplyVisual(const TSharedPtr<FJsonObject>& Json);
    void ApplyBehavior(const TSharedPtr<FJsonObject>& Json);

    /** 设置 Niagara 粒子激活状态 */
    void SetFxActive(UNiagaraComponent* Comp, bool bActive);
};
