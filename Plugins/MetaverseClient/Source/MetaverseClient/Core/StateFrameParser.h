#pragma once

#include "CoreMinimal.h"
#include "Dto/StateFrame.h"

/** 把 JSON 状态帧解析为 FStateFrame;失败时返回 false,Out.Targets 保持空数组。 */
class FStateFrameParser
{
public:
    static bool Parse(const FString& Json, FStateFrame& Out);
};
