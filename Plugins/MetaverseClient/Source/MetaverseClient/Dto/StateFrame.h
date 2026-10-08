#pragma once

#include "CoreMinimal.h"
#include "Dto/TargetView.h"
#include "StateFrame.generated.h"

/** 中间层 /ws/targets 每帧推送的全量状态快照。 */
USTRUCT()
struct FStateFrame
{
    GENERATED_BODY()

    UPROPERTY() int64 Ts = 0;                 // 服务端时间戳(ms)
    UPROPERTY() TArray<FTargetView> Targets;  // 当前所有活跃目标
};
