#pragma once

#include "CoreMinimal.h"
#include "TargetView.generated.h"

/** 状态帧内单个目标;字段与中间层 StateFrame/TargetView 序列化严格对齐(大小写不敏感)。 */
USTRUCT()
struct FTargetView
{
    GENERATED_BODY()

    UPROPERTY() FString Key;            // 目标唯一键,如 "agv:4053";客户端按它 diff
    UPROPERTY() FString Clazz;          // 内部类别: "agv" / "person" / ...(保留,供日志/调试)
    UPROPERTY() FString ObjectTypeRid;  // 本体对象类型 rid,如 "ri.obj.<uuid>";客户端按它选蓝图
    /** 总线未下发 z 时的哨兵:老版本只发 x/y,取不到高度就回退 GroundZ(见 TargetSyncManager)。
     *  JSON 里没有 z 字段时,反序列化不会写这个成员,默认值得以保留,据此判断"服务端没给"。 */
    static constexpr double UnsetZ = -1.0e18;

    UPROPERTY() double  X = 0.0;        // Unreal 世界坐标 X(cm)
    UPROPERTY() double  Y = 0.0;    // Unreal 世界坐标 Y(cm)
    UPROPERTY() double  Z = UnsetZ;     // 离地高度(cm);0=贴地,层高如 450=在二层。缺省=服务端未下发
    UPROPERTY() double  Heading = 0.0;  // 朝向角(度)= Unreal Yaw
    UPROPERTY() FString State;      // "active" / "lost"
};
