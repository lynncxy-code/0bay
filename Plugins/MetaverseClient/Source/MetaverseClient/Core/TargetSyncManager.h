#pragma once

#include "CoreMinimal.h"
#include "Core/IAvatar.h"
#include "Core/IAvatarPool.h"

struct FStateFrame;

/** 单个类别在最近一帧中的真实执行结果；只保存在运行时内存，不写入业务库。 */
struct FTargetSyncCategoryStats
{
    int32 TargetCount = 0;
    int32 AppliedCount = 0;
    TMap<FString, int32> UnappliedReasons;
};

/** 把每帧全量状态与当前分身集合做 diff:新增 spawn、存在 update、缺失 recycle。 */
class FTargetSyncManager
{
public:
    /** @param InNowFn 本地单调时钟(秒);缺省用 FPlatformTime::Seconds,测试可注入假时钟 */
    FTargetSyncManager(IAvatarPool* InPool, double InGroundZ,
                       TFunction<double()> InNowFn = nullptr);
    void ApplyFrame(const FStateFrame& Frame);
    /** 断流或人工关闭时回收全部分身，并重置本次实时会话的时间基准。 */
    void ClearActiveTargets();
    int32 ActiveCount() const { return Active.Num(); }
    /** 最近一帧输入目标数（包含未映射/无效目标）。 */
    int32 LastTargetCount() const { return LastStatsTargetCount; }
    /** 最近一帧成功送入分身的唯一目标数。 */
    int32 LastAppliedCount() const { return LastStatsAppliedCount; }
    const TMap<FString, FTargetSyncCategoryStats>& LastCategoryStats() const
    {
        return LastCategoryStatsById;
    }

private:
    void ResetTimelines();   // 时间映射重锚定时作废全部旧样本

    IAvatarPool* Pool;
    double GroundZ;
    TFunction<double()> NowFn;
    TMap<FString, IAvatar*> Active;   // key → 分身
    int32 LastStatsTargetCount = 0;
    int32 LastStatsAppliedCount = 0;
    TMap<FString, FTargetSyncCategoryStats> LastCategoryStatsById;
    int64 LastFrameTs = 0;            // 乱序闸门:丢弃比已处理更旧的帧(重连旧帧不回退位置)
    // 服务端 ts → 本地单调时钟的映射:预热期取最小延迟基线,之后冻结——
    // 会话时长内不存在需要持续追踪的时钟漂移,持续追随 Off 会把网络延迟变化
    // 吞进映射(蚕食渲染缓冲)或把积压批量到达折叠成同一时刻(摧毁 33ms 节拍)
    bool bClockInit = false;
    double ClockOffset = 0.0;
    double WarmupUntil = 0.0;
    double LastSampleT = 0.0;         // 映射时间轴单调兜底
};
