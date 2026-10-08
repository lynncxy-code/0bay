#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Core/TargetSyncManager.h"
#include "Dto/StateFrame.h"
#include "Tests/FakeAvatarPool.h"

static FTargetView MakeAgv(const FString& Key, double X, double Y, const FString& State = TEXT("active"))
{
    FTargetView T;
    T.Key = Key; T.Clazz = TEXT("agv"); T.ObjectTypeRid = TEXT("ri.obj.agv");
    T.X = X; T.Y = Y; T.Heading = 0; T.State = State;
    return T;
}

static FStateFrame MakeFrame(const TArray<FTargetView>& Ts)
{
    FStateFrame F; F.Ts = 1; F.Targets = Ts; return F;
}

/** MVP 混合流夹具：只注册人员处理器，AGV 应进入 instance_missing 统计。 */
class FPersonOnlyAvatarPool : public FFakeAvatarPool
{
public:
    virtual IAvatar* Spawn(const FString& Key, const FString& ObjectTypeRid,
                           const FString& Clazz) override
    {
        if (!Clazz.Equals(TEXT("person"), ESearchCase::IgnoreCase))
        {
            return nullptr;
        }
        return FFakeAvatarPool::Spawn(Key, ObjectTypeRid, Clazz);
    }
};

static FTargetView MakePerson(const FString& Key, double X)
{
    FTargetView T;
    T.Key = Key; T.Clazz = TEXT("person"); T.ObjectTypeRid = TEXT("ri.obj.person");
    T.X = X; T.Y = 0.0; T.Heading = 0.0; T.State = TEXT("active");
    return T;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSyncMixedFrameStatsTest,
    "Metaverse.Sync.MixedFrameReportsActualAppliedAndReasons",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSyncMixedFrameStatsTest::RunTest(const FString& Params)
{
    FPersonOnlyAvatarPool Pool;
    FTargetSyncManager Mgr(&Pool, 0.0);
    TArray<FTargetView> Targets;
    for (int32 Index = 0; Index < 20; ++Index)
    {
        Targets.Add(MakePerson(FString::Printf(TEXT("person:%02d"), Index + 1), Index));
    }
    for (int32 Index = 0; Index < 5; ++Index)
    {
        Targets.Add(MakeAgv(FString::Printf(TEXT("agv:%02d"), Index + 1), 100 + Index, 0));
    }
    Mgr.ApplyFrame(MakeFrame(Targets));

    TestEqual(TEXT("收到目标数来自帧而非活动分身"), Mgr.LastTargetCount(), 25);
    TestEqual(TEXT("已应用数来自成功 Spawn/Update"), Mgr.LastAppliedCount(), 20);
    const FTargetSyncCategoryStats* Person = Mgr.LastCategoryStats().Find(TEXT("person"));
    const FTargetSyncCategoryStats* Agv = Mgr.LastCategoryStats().Find(TEXT("agv"));
    TestNotNull(TEXT("人员分类统计"), Person);
    TestNotNull(TEXT("AGV分类统计"), Agv);
    if (Person)
    {
        TestEqual(TEXT("人员 20/20"), Person->TargetCount, 20);
        TestEqual(TEXT("人员全部应用"), Person->AppliedCount, 20);
    }
    if (Agv)
    {
        TestEqual(TEXT("AGV 5/0"), Agv->TargetCount, 5);
        TestEqual(TEXT("AGV 无应用"), Agv->AppliedCount, 0);
        TestEqual(TEXT("AGV 缺少正式实例原因"), Agv->UnappliedReasons.FindRef(TEXT("instance_missing")), 5);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSyncSpawnTest,
    "Metaverse.Sync.NewAgvSpawns",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSyncSpawnTest::RunTest(const FString& Params)
{
    FFakeAvatarPool Pool;
    FTargetSyncManager Mgr(&Pool, 0.0);
    Mgr.ApplyFrame(MakeFrame({ MakeAgv(TEXT("agv:4053"), 3240.8, 1222.7) }));
    TestEqual(TEXT("spawned"), Pool.Spawned.Num(), 1);
    TestEqual(TEXT("active"), Mgr.ActiveCount(), 1);
    TestEqual(TEXT("spawn 按 objectTypeRid 选型"), Pool.Spawned[0]->ObjectTypeRid, FString(TEXT("ri.obj.agv")));
    TestEqual(TEXT("clazz 传给对象池作兜底选型"), Pool.Spawned[0]->Clazz, FString(TEXT("agv")));
    TestEqual(TEXT("x"), Pool.Spawned[0]->X, 3240.8, 1e-4);
    TestEqual(TEXT("y"), Pool.Spawned[0]->Y, 1222.7, 1e-4);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSyncUpdateTest,
    "Metaverse.Sync.ExistingAgvUpdatesNoRespawn",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSyncUpdateTest::RunTest(const FString& Params)
{
    FFakeAvatarPool Pool;
    FTargetSyncManager Mgr(&Pool, 0.0);
    Mgr.ApplyFrame(MakeFrame({ MakeAgv(TEXT("agv:4053"), 0, 0) }));
    Mgr.ApplyFrame(MakeFrame({ MakeAgv(TEXT("agv:4053"), 500, 600) }));
    TestEqual(TEXT("no respawn"), Pool.Spawned.Num(), 1);
    TestEqual(TEXT("x"), Pool.Spawned[0]->X, 500.0, 1e-4);
    TestEqual(TEXT("y"), Pool.Spawned[0]->Y, 600.0, 1e-4);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSyncRecycleTest,
    "Metaverse.Sync.AbsentAgvRecycled",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSyncRecycleTest::RunTest(const FString& Params)
{
    FFakeAvatarPool Pool;
    FTargetSyncManager Mgr(&Pool, 0.0);
    Mgr.ApplyFrame(MakeFrame({ MakeAgv(TEXT("agv:4053"), 0, 0) }));
    Mgr.ApplyFrame(MakeFrame({}));   // 下一帧不含该 AGV
    TestEqual(TEXT("recycled"), Pool.Recycled.Num(), 1);
    TestEqual(TEXT("active"), Mgr.ActiveCount(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSyncClearActiveTargetsTest,
    "Metaverse.Sync.DisconnectClearsAllTargets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSyncClearActiveTargetsTest::RunTest(const FString& Params)
{
    FFakeAvatarPool Pool;
    FTargetSyncManager Mgr(&Pool, 0.0);
    Mgr.ApplyFrame(MakeFrame({
        MakeAgv(TEXT("person:1"), 10, 20),
        MakeAgv(TEXT("person:2"), 30, 40),
    }));

    Mgr.ClearActiveTargets();
    TestEqual(TEXT("断流后全部回收"), Pool.Recycled.Num(), 2);
    TestEqual(TEXT("断流后无活动目标"), Mgr.ActiveCount(), 0);

    // 重复关闭必须幂等；重新收到帧时按新会话正常生成。
    Mgr.ClearActiveTargets();
    TestEqual(TEXT("重复清理不重复回收"), Pool.Recycled.Num(), 2);
    Mgr.ApplyFrame(MakeFrame({ MakeAgv(TEXT("person:1"), 50, 60) }));
    TestEqual(TEXT("重连后可重新生成"), Pool.Spawned.Num(), 3);
    TestEqual(TEXT("重连后活动目标恢复"), Mgr.ActiveCount(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSyncGroundZTest,
    "Metaverse.Sync.GroundZAppliedToZ",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSyncGroundZTest::RunTest(const FString& Params)
{
    FFakeAvatarPool Pool;
    FTargetSyncManager Mgr(&Pool, 15.0);   // groundZ = 15cm
    // MakeAgv 不设 z(模拟老版本总线只发 x/y):必须回退到 GroundZ,行为不变
    Mgr.ApplyFrame(MakeFrame({ MakeAgv(TEXT("agv:4053"), 0, 0) }));
    TestEqual(TEXT("z"), Pool.Spawned[0]->Z, 15.0, 1e-4);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSyncFrameZTest,
    "Metaverse.Sync.FrameZLiftsAvatarAboveGround",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSyncFrameZTest::RunTest(const FString& Params)
{
    FFakeAvatarPool Pool;
    FTargetSyncManager Mgr(&Pool, 15.0);          // 地面基准 15cm

    FTargetView Ground = MakeAgv(TEXT("p:1"), 0, 0);
    Ground.Z = 0.0;                                // 贴地:结果应等于 GroundZ
    FTargetView Upstairs = MakeAgv(TEXT("p:2"), 0, 0);
    Upstairs.Z = 450.0;                            // 二层:GroundZ + 层高
    Mgr.ApplyFrame(MakeFrame({ Ground, Upstairs }));

    TestEqual(TEXT("贴地目标 z=0 → GroundZ"), Pool.Spawned[0]->Z, 15.0, 1e-4);
    TestEqual(TEXT("二层目标 z=450 → GroundZ+450"), Pool.Spawned[1]->Z, 465.0, 1e-4);

    // 同一个人上楼:高度要跟着帧变,不能钉在地面
    FTargetView Climbing = MakeAgv(TEXT("p:1"), 0, 0);
    Climbing.Z = 220.0;
    Mgr.ApplyFrame(MakeFrame({ Climbing, Upstairs }));
    TestEqual(TEXT("爬楼中途高度跟随"), Pool.Spawned[0]->Z, 235.0, 1e-4);
    TestEqual(TEXT("没有重复 spawn"), Pool.Spawned.Num(), 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSyncLostTest,
    "Metaverse.Sync.LostStateRecycled",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSyncLostTest::RunTest(const FString& Params)
{
    FFakeAvatarPool Pool;
    FTargetSyncManager Mgr(&Pool, 0.0);
    Mgr.ApplyFrame(MakeFrame({ MakeAgv(TEXT("agv:4053"), 0, 0) }));
    Mgr.ApplyFrame(MakeFrame({ MakeAgv(TEXT("agv:4053"), 0, 0, TEXT("lost")) }));
    TestEqual(TEXT("lost 不重复 spawn"), Pool.Spawned.Num(), 1);
    TestEqual(TEXT("lost 立即回收"), Pool.Recycled.Num(), 1);
    TestEqual(TEXT("lost 后无活动分身"), Mgr.ActiveCount(), 0);
    const FTargetSyncCategoryStats* Stats = Mgr.LastCategoryStats().Find(TEXT("agv"));
    TestNotNull(TEXT("lost 分类统计"), Stats);
    if (Stats)
    {
        TestEqual(TEXT("lost 不计入 applied"), Stats->AppliedCount, 0);
        TestEqual(TEXT("lost 原因"), Stats->UnappliedReasons.FindRef(TEXT("lost")), 1);
    }

    // 新出现的 lost 目标也不能生成临时 Actor。
    Mgr.ApplyFrame(MakeFrame({ MakeAgv(TEXT("agv:new"), 1, 1, TEXT("lost")) }));
    TestEqual(TEXT("新 lost 不 spawn"), Pool.Spawned.Num(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSyncStaleFrameTest,
    "Metaverse.Sync.StaleFrameIgnored",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSyncStaleFrameTest::RunTest(const FString& Params)
{
    FFakeAvatarPool Pool;
    FTargetSyncManager Mgr(&Pool, 0.0);

    FStateFrame Newer; Newer.Ts = 100; Newer.Targets = { MakeAgv(TEXT("agv:4053"), 500, 600) };
    Mgr.ApplyFrame(Newer);

    FStateFrame Older; Older.Ts = 50; Older.Targets = { MakeAgv(TEXT("agv:4053"), 0, 0) };
    Mgr.ApplyFrame(Older);   // 更旧的帧(重连残留/乱序)应被忽略,位置不回退

    TestEqual(TEXT("x unchanged"), Pool.Spawned[0]->X, 500.0, 1e-4);
    TestEqual(TEXT("no respawn"), Pool.Spawned.Num(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSyncEmptyKeyIgnoredTest,
    "Metaverse.Sync.EmptyKeyIgnored",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSyncEmptyKeyIgnoredTest::RunTest(const FString& Params)
{
    FFakeAvatarPool Pool;
    FTargetSyncManager Mgr(&Pool, 0.0);
    FTargetView Bad = MakeAgv(TEXT(""), 100, 200);
    Mgr.ApplyFrame(MakeFrame({ Bad }));
    TestEqual(TEXT("no spawn"), Pool.Spawned.Num(), 0);
    TestEqual(TEXT("active"), Mgr.ActiveCount(), 0);
    return true;
}

static FStateFrame MakeTsFrame(int64 Ts, double X)
{
    FStateFrame F; F.Ts = Ts; F.Targets = { MakeAgv(TEXT("agv:1"), X, 0) };
    return F;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSyncBurstKeepsServerSpacingTest,
    "Metaverse.Sync.BurstArrivalKeepsServerSpacing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSyncBurstKeepsServerSpacingTest::RunTest(const FString& Params)
{
    // 积压批量到达(同一到达时刻,服务端 ts 每帧 +33ms):映射间距必须保持
    // 服务端节拍 33ms,而不是被折叠成到达间距 0(偏移冻结的核心价值)
    double Clock = 100.0;
    FFakeAvatarPool Pool;
    FTargetSyncManager Mgr(&Pool, 0.0, [&Clock]() { return Clock; });
    Mgr.ApplyFrame(MakeTsFrame(1000, 0));   // 锚定:offset=99,预热至 102
    Clock = 103.0;                          // 预热结束,偏移冻结
    const int64 BurstTs[] = { 4000, 4033, 4066, 4099 };
    for (int64 Ts : BurstTs)
    {
        Mgr.ApplyFrame(MakeTsFrame(Ts, 0));
    }
    FFakeAvatar* A = Pool.Spawned[0];
    const int32 N = A->SampleTimes.Num();
    TestEqual(TEXT("样本数"), N, 5);
    TestEqual(TEXT("间距1"), A->SampleTimes[N - 1] - A->SampleTimes[N - 2], 0.033, 1e-6);
    TestEqual(TEXT("间距2"), A->SampleTimes[N - 2] - A->SampleTimes[N - 3], 0.033, 1e-6);
    TestEqual(TEXT("间距3"), A->SampleTimes[N - 3] - A->SampleTimes[N - 4], 0.033, 1e-6);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSyncRestartResetsTimelineTest,
    "Metaverse.Sync.ServerRestartResetsTimeline",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSyncRestartResetsTimelineTest::RunTest(const FString& Params)
{
    double Clock = 100.0;
    FFakeAvatarPool Pool;
    FTargetSyncManager Mgr(&Pool, 0.0, [&Clock]() { return Clock; });
    Mgr.ApplyFrame(MakeTsFrame(100000, 1));
    Clock = 101.0;
    Mgr.ApplyFrame(MakeTsFrame(2000, 2));   // 回退 98s ≥5s:服务重启,重锚并清时间轴
    FFakeAvatar* A = Pool.Spawned[0];
    TestEqual(TEXT("重启后位置生效"), A->X, 2.0, 1e-9);
    TestEqual(TEXT("时间轴已重置"), A->TimelineResets, 1);
    Mgr.ApplyFrame(MakeTsFrame(1900, 3));   // 新基准 2000 下小幅回退:仍被闸门丢弃
    TestEqual(TEXT("乱序仍拦截"), A->X, 2.0, 1e-9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSyncTsZeroSafeTest,
    "Metaverse.Sync.TsZeroKeepsGateAndMonotonicTimeline",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSyncTsZeroSafeTest::RunTest(const FString& Params)
{
    // 时钟推进必须与 ts 推进大体同速:二者相对漂移 >5s 会命中"时钟突变重锚定"
    // 分支(ApplyFrame 里 |Off-ClockOffset|>5.0),那样测到的就不是单调钳制了。
    double Clock = 100.0;
    FFakeAvatarPool Pool;
    FTargetSyncManager Mgr(&Pool, 0.0, [&Clock]() { return Clock; });
    Mgr.ApplyFrame(MakeTsFrame(1000, 1));   // 锚定 offset=99,预热至 102.0
    Clock = 103.0;                          // 预热已结束,offset 冻结在 99
    Mgr.ApplyFrame(MakeTsFrame(0, 2));      // 无 ts 帧:接受,退化为到达时刻(样本 103),不得清掉乱序基准
    FFakeAvatar* A = Pool.Spawned[0];
    TestEqual(TEXT("无 ts 帧位置生效"), A->X, 2.0, 1e-9);
    Clock = 103.05;
    Mgr.ApplyFrame(MakeTsFrame(500, 9));    // 相对基准 1000 回退:必须仍被丢弃
    TestEqual(TEXT("基准未被 Ts=0 清掉"), A->X, 2.0, 1e-9);
    // Off=103.05-1.9=101.15,与 offset 99 差 2.15<5:不重锚;映射值 1.9+99=100.9
    // 早于上一样本(103,由 Ts=0 帧抬高)→ 必须被单调钳制到 103,而不是让时间轴回退
    Mgr.ApplyFrame(MakeTsFrame(1900, 3));
    TestEqual(TEXT("位置生效"), A->X, 3.0, 1e-9);
    TestEqual(TEXT("时间轴未被清"), A->TimelineResets, 0);
    for (int32 i = 1; i < A->SampleTimes.Num(); ++i)
    {
        TestTrue(TEXT("样本时间单调不减"), A->SampleTimes[i] >= A->SampleTimes[i - 1]);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSyncFiveSecondBoundaryTest,
    "Metaverse.Sync.FiveSecondRegressionBoundary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSyncFiveSecondBoundaryTest::RunTest(const FString& Params)
{
    double Clock = 100.0;
    FFakeAvatarPool Pool;
    FTargetSyncManager Mgr(&Pool, 0.0, [&Clock]() { return Clock; });
    Mgr.ApplyFrame(MakeTsFrame(10000, 1));
    Mgr.ApplyFrame(MakeTsFrame(5001, 2));   // 回退 4999ms < 5000:丢弃
    FFakeAvatar* A = Pool.Spawned[0];
    TestEqual(TEXT("4999ms 回退丢弃"), A->X, 1.0, 1e-9);
    Mgr.ApplyFrame(MakeTsFrame(5000, 3));   // 回退 5000ms ≥ 5000:按服务重启接受
    TestEqual(TEXT("5000ms 回退按重启接受"), A->X, 3.0, 1e-9);
    TestEqual(TEXT("重启清时间轴"), A->TimelineResets, 1);
    return true;
}
#endif
