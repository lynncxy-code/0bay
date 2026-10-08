#include "Core/TargetSyncManager.h"
#include "Dto/StateFrame.h"
#include "HAL/PlatformTime.h"

FTargetSyncManager::FTargetSyncManager(IAvatarPool* InPool, double InGroundZ,
                                       TFunction<double()> InNowFn)
    : Pool(InPool), GroundZ(InGroundZ)
{
    NowFn = InNowFn ? MoveTemp(InNowFn)
                    : TFunction<double()>([]() { return FPlatformTime::Seconds(); });
}

void FTargetSyncManager::ResetTimelines()
{
    for (const TPair<FString, IAvatar*>& Kv : Active)
    {
        Kv.Value->ResetTimeline();
    }
    LastSampleT = 0.0;
}

void FTargetSyncManager::ClearActiveTargets()
{
    for (const TPair<FString, IAvatar*>& Kv : Active)
    {
        Pool->Recycle(Kv.Value);
    }
    Active.Empty();

    // 下一次成功连接必须被视为全新会话，不能继承断线前的乱序闸门或时钟锚点。
    LastFrameTs = 0;
    bClockInit = false;
    ClockOffset = 0.0;
    WarmupUntil = 0.0;
    LastSampleT = 0.0;
    LastStatsTargetCount = 0;
    LastStatsAppliedCount = 0;
    LastCategoryStatsById.Empty();
}

void FTargetSyncManager::ApplyFrame(const FStateFrame& Frame)
{
    // 乱序闸门:小幅回退(<5s)是重连残留/网络乱序,丢弃防位置回退;
    // 大幅回退(≥5s)视为服务重启/换实例,重建时间映射后继续消费。
    // Ts=0(无时间戳)帧不参与闸门,也不得清掉已建立的基准。
    if (Frame.Ts != 0)
    {
        if (LastFrameTs != 0 && Frame.Ts < LastFrameTs)
        {
            if (LastFrameTs - Frame.Ts < 5000)
            {
                return;
            }
            bClockInit = false;
        }
        LastFrameTs = Frame.Ts;
    }

    // 样本时间轴 = 服务端帧 ts(恒定节拍)+ 锚定偏移。
    // 偏移只在(重)锚定后的 2s 预热期内取最小延迟,之后冻结:样本间距从此
    // 严格等于服务端 ts 间距,网络到达抖动/积压批量到达都进不了时间轴。
    const double Now = NowFn();
    double SampleT;
    const double TsSec = Frame.Ts / 1000.0;
    if (TsSec > 0.0)
    {
        const double Off = Now - TsSec;
        if (!bClockInit || FMath::Abs(Off - ClockOffset) > 5.0)
        {
            // 初始 / 服务重启 / 时钟突变:重锚定,旧时间轴样本全部作废。
            // 权衡(已知):此判据看的是"本地钟与服务端 ts 的相对漂移",除真实重启外,
            // 单帧处理被卡住数秒、或长时间运行后两端晶振漂移累积超 5s,同样会命中,
            // 后果是一次 ResetTimelines() → 全体分身可见瞬移一次。现场若观察到无故
            // 瞬移,先查是否为本分支触发(而非服务端真的重启)。收紧方向:要求连续
            // 多帧确认再动作,或漂移场景改渐进重新预热而非整体重置。
            ClockOffset = Off;
            bClockInit = true;
            WarmupUntil = Now + 2.0;
            ResetTimelines();
        }
        else if (Now < WarmupUntil && Off < ClockOffset)
        {
            ClockOffset = Off;   // 预热期内向最小延迟收敛;预热结束即冻结
        }
        SampleT = TsSec + ClockOffset;
    }
    else
    {
        SampleT = Now;   // 无 ts 帧退化为到达时刻,由下方单调钳制约束
    }
    // 时间轴单调兜底:映射值不回退(等值允许,分身端 alpha=0 安全)
    if (SampleT < LastSampleT)
    {
        SampleT = LastSampleT;
    }
    LastSampleT = SampleT;

    // 统计必须来自本帧逐目标执行结果，不能把收到数量当作已应用数量。
    // 统计对象只保留在内存中，供 BridgeClient 健康心跳使用。
    LastStatsTargetCount = Frame.Targets.Num();
    LastStatsAppliedCount = 0;
    LastCategoryStatsById.Empty();

    auto NormalizeCategory = [](FString Category)
    {
        Category.TrimStartAndEndInline();
        Category.ToLowerInline();
        return Category.IsEmpty() ? FString(TEXT("unknown")) : Category;
    };
    auto AddReason = [](FTargetSyncCategoryStats& Stats, const TCHAR* Reason)
    {
        Stats.UnappliedReasons.FindOrAdd(Reason)++;
    };

    TSet<FString> Seen;
    TSet<FString> AppliedKeys;
    for (const FTargetView& T : Frame.Targets)
    {
        const FString Category = NormalizeCategory(T.Clazz);
        FTargetSyncCategoryStats& CategoryStats = LastCategoryStatsById.FindOrAdd(Category);
        ++CategoryStats.TargetCount;
        if (T.Key.IsEmpty())
        {
            UE_LOG(LogTemp, Warning, TEXT("[TargetSyncManager] 跳过空 key 目标(clazz=%s)"), *T.Clazz);
            AddReason(CategoryStats, TEXT("invalid"));
            continue;
        }
        Seen.Add(T.Key);

        // lost 是生命周期事件，不是可渲染状态：已有分身立即回收到池中，
        // 新出现的 lost 目标也不得 Spawn，避免短暂幽灵人物/AGV。
        if (T.State.Equals(TEXT("lost"), ESearchCase::IgnoreCase))
        {
            if (IAvatar** FoundLost = Active.Find(T.Key))
            {
                Pool->Recycle(*FoundLost);
                Active.Remove(T.Key);
            }
            AddReason(CategoryStats, TEXT("lost"));
            continue;
        }

        IAvatar** Found = Active.Find(T.Key);
        IAvatar* Avatar;
        if (Found == nullptr)
        {
            // 新增 → 优先按 objectTypeRid、缺省时按 clazz 选型。传 clazz 可让 person
            // 在不同环境的 worker rid 不一致/暂未灌本体时仍命中人物任务包配置。
            Avatar = Pool->Spawn(T.Key, T.ObjectTypeRid, T.Clazz);
            if (Avatar == nullptr)
            {
                AddReason(CategoryStats,
                    Category == TEXT("agv")
                        ? TEXT("instance_missing")
                        : ((!T.ObjectTypeRid.IsEmpty() || !T.Clazz.IsEmpty())
                            ? TEXT("handler_unavailable")
                            : TEXT("unmapped")));
                continue;   // 无对应模型类,跳过该目标
            }
            Active.Add(T.Key, Avatar);
        }
        else
        {
            Avatar = *Found;
        }
        // (x,y,z)→ 世界坐标;heading → Yaw。不做二次换算(服务端已给 cm/度)。
        // 世界 Z = GroundZ(本场地地面基准) + 总线给的离地高度:贴地目标 z=0 结果
        // 就是 GroundZ,与只发 x/y 的老版本行为一致;上二层的人 z=层高,自然抬起来。
        // 老版本总线不发 z,此时 T.Z 保持哨兵值,回退到 GroundZ。
        const double WorldZ = (T.Z <= FTargetView::UnsetZ) ? GroundZ : GroundZ + T.Z;
        Avatar->SetTarget(T.X, T.Y, WorldZ, T.Heading, SampleT);
        Avatar->SetState(T.State);
        if (!AppliedKeys.Contains(T.Key))
        {
            AppliedKeys.Add(T.Key);
            ++CategoryStats.AppliedCount;
            ++LastStatsAppliedCount;
        }
    }

    // 本帧未出现的 key → 回收(对应中间层的 lost/evict)
    TArray<FString> ToRemove;
    for (const TPair<FString, IAvatar*>& Kv : Active)
    {
        if (!Seen.Contains(Kv.Key))
        {
            ToRemove.Add(Kv.Key);
        }
    }
    for (const FString& Key : ToRemove)
    {
        Pool->Recycle(Active[Key]);
        Active.Remove(Key);
    }
}
