#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/IAvatar.h"
#include "AvatarActor.generated.h"

class UChildActorComponent;
class USceneComponent;

/**
 * 分身:按"服务端时间轴 - RenderDelaySeconds"在样本缓冲里插值渲染。
 * 消息一到就贴最新点(旧实现 VInterpTo 追赶)会把网络到达抖动直接变成顿挫;
 * 延迟缓冲让渲染在已知样本间插值(首样本前/断流后为端点钳制),
 * 位置连续、不暴露到达抖动(速度在样本边界处分段线性)。
 */
UCLASS()
class AAvatarActor : public AActor, public IAvatar
{
    GENERATED_BODY()

public:
    AAvatarActor();

    // IAvatar
    virtual void SetTarget(double X, double Y, double Z, double HeadingDeg,
                           double SampleTimeSec) override;
    virtual void SetState(const FString& State) override;
    virtual void ResetTimeline() override;

    virtual void Tick(float DeltaSeconds) override;

    /** 入池复用前重置,供 AAvatarPool 调用。 */
    void ResetForReuse();

    /**
     * 挂载任务包里已有的角色 Actor/Character 蓝图作为外观。
     * 外观不需要继承 AAvatarActor；本 Actor 只负责总线轨迹和对象池生命周期。
     */
    void ConfigureVisual(TSubclassOf<AActor> VisualClass, UObject* CharacterAsset,
                         const FTransform& RelativeTransform);

    /** 入池/出池时同时控制驱动器与子角色，避免隐藏角色仍碰撞或 Tick。 */
    void SetRuntimeActive(bool bActive);

protected:
    UPROPERTY(VisibleAnywhere, Category = "Metaverse")
    USceneComponent* SceneRoot = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Metaverse")
    UChildActorComponent* VisualActorComponent = nullptr;

    /**
     * 渲染延迟(秒):吸收网络到达抖动的缓冲窗口。
     * 抖动大的现场网络可调大(0.3~0.5);延迟对孪生展示无感。
     */
    UPROPERTY(EditAnywhere, Category = "Metaverse", meta = (ClampMin = "0.05", ClampMax = "2.0"))
    float RenderDelaySeconds = 0.25f;

private:
    struct FPoseSample
    {
        double T;          // 本地单调时钟域的采样时刻(SyncManager 由服务端 ts 映射)
        FVector Loc;
        double HeadingDeg;
    };
    TArray<FPoseSample> Samples;   // 时间升序;上限 ~8s
    FString CurrentState = TEXT("active");
    /** 仅任务包角色成功建立原生胶囊后启用；入池时由 SetRuntimeActive 临时关闭。 */
    bool bVisualCollisionEnabled = false;
};
