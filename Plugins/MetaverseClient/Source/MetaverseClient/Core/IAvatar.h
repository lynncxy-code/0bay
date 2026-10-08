#pragma once

#include "CoreMinimal.h"

/**
 * 分身抽象(普通 C++ 接口,非 UINTERFACE):AAvatarActor 与测试替身各自实现。
 * 这样 FTargetSyncManager 与假替身都不依赖 UObject/UWorld,可在 Automation 里纯逻辑验证。
 */
class IAvatar
{
public:
    virtual ~IAvatar() {}
    /**
     * 送入一条位姿样本。SampleTimeSec 是"服务端帧时间映射到本地单调时钟"的采样时刻
     * (由 FTargetSyncManager 计算)——分身按该时间轴延迟插值渲染,而不是一到就贴,
     * 这样网络到达抖动(突发/停顿)不会变成画面顿挫。
     */
    virtual void SetTarget(double X, double Y, double Z, double HeadingDeg, double SampleTimeSec) = 0;
    virtual void SetState(const FString& State) = 0;
    /** 时间映射重锚定(服务重启/换实例/时钟突变):旧时间轴样本作废。 */
    virtual void ResetTimeline() = 0;
};
