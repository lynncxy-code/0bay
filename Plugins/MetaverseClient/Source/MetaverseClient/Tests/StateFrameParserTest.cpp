#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Core/StateFrameParser.h"
#include "Dto/StateFrame.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStateFrameParserAgvTest,
    "Metaverse.StateFrameParser.ParsesAgvTarget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FStateFrameParserAgvTest::RunTest(const FString& Params)
{
    const FString Json =
        TEXT("{\"ts\":1719720000123,\"targets\":[")
        TEXT("{\"key\":\"agv:4053\",\"clazz\":\"agv\",\"objectTypeRid\":\"ri.obj.75186947\",")
        TEXT("\"x\":3240.8,\"y\":1222.7,\"heading\":180.0,\"state\":\"active\"}]}");
    FStateFrame Frame;
    TestTrue(TEXT("parse ok"), FStateFrameParser::Parse(Json, Frame));
    TestEqual(TEXT("ts"), Frame.Ts, (int64)1719720000123);
    TestEqual(TEXT("count"), Frame.Targets.Num(), 1);
    const FTargetView& T = Frame.Targets[0];
    TestEqual(TEXT("key"), T.Key, FString(TEXT("agv:4053")));
    TestEqual(TEXT("clazz"), T.Clazz, FString(TEXT("agv")));
    TestEqual(TEXT("objectTypeRid"), T.ObjectTypeRid, FString(TEXT("ri.obj.75186947")));
    TestEqual(TEXT("x"), T.X, 3240.8, 1e-4);
    TestEqual(TEXT("y"), T.Y, 1222.7, 1e-4);
    TestEqual(TEXT("heading"), T.Heading, 180.0, 1e-3);
    TestEqual(TEXT("state"), T.State, FString(TEXT("active")));
    // 这条报文没有 z(老版本总线):Z 必须保持哨兵,消费端据此回退 GroundZ
    TestEqual(TEXT("缺 z → 保持哨兵"), T.Z, FTargetView::UnsetZ, 1e-4);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStateFrameParserZTest,
    "Metaverse.StateFrameParser.ParsesZHeight",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FStateFrameParserZTest::RunTest(const FString& Params)
{
    // 新版总线的真实报文形态(含 z);上二层的人 z=层高
    const FString Json =
        TEXT("{\"ts\":1719720000123,\"targets\":[")
        TEXT("{\"key\":\"person:024\",\"clazz\":\"person\",\"objectTypeRid\":\"ri.obj.worker\",")
        TEXT("\"x\":-1317.9,\"y\":617.9,\"z\":450.0,\"heading\":41.2,\"state\":\"active\"},")
        TEXT("{\"key\":\"person:037\",\"clazz\":\"person\",\"objectTypeRid\":\"ri.obj.worker\",")
        TEXT("\"x\":-2670.4,\"y\":417.7,\"z\":0,\"heading\":118.6,\"state\":\"active\"}]}");
    FStateFrame Frame;
    TestTrue(TEXT("parse ok"), FStateFrameParser::Parse(Json, Frame));
    TestEqual(TEXT("count"), Frame.Targets.Num(), 2);
    TestEqual(TEXT("楼上 z"), Frame.Targets[0].Z, 450.0, 1e-4);
    TestEqual(TEXT("贴地 z=0 与'没给'要能区分"), Frame.Targets[1].Z, 0.0, 1e-4);
    TestTrue(TEXT("z=0 不是哨兵"), Frame.Targets[1].Z > FTargetView::UnsetZ);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStateFrameParserEmptyTest,
    "Metaverse.StateFrameParser.EmptyAndNullTargets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FStateFrameParserEmptyTest::RunTest(const FString& Params)
{
    FStateFrame Empty;
    TestTrue(TEXT("empty ok"), FStateFrameParser::Parse(TEXT("{\"ts\":1,\"targets\":[]}"), Empty));
    TestEqual(TEXT("empty count"), Empty.Targets.Num(), 0);

    FStateFrame NoTargets;
    TestFalse(TEXT("缺少 targets 必须拒绝"), FStateFrameParser::Parse(TEXT("{\"ts\":1}"), NoTargets));
    TestEqual(TEXT("no-targets count"), NoTargets.Targets.Num(), 0);

    FStateFrame NonArrayTargets;
    TestFalse(
        TEXT("targets 非数组必须拒绝"),
        FStateFrameParser::Parse(TEXT("{\"ts\":1,\"targets\":{}}"), NonArrayTargets));
    TestEqual(TEXT("非数组不产生目标"), NonArrayTargets.Targets.Num(), 0);
    return true;
}
#endif
