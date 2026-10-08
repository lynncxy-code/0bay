#pragma once

#include "CoreMinimal.h"

class IAvatar;

/** 分身对象池抽象:按 key 唯一标识、按 objectTypeRid/clazz 选蓝图取分身,回收分身。 */
class IAvatarPool
{
public:
    virtual ~IAvatarPool() {}
    virtual IAvatar* Spawn(const FString& Key, const FString& ObjectTypeRid) = 0;

    /**
     * 带总线内部类别的扩展入口。保留两参数纯虚函数以兼容已有宿主实现；
     * 未关心 Clazz 的旧对象池会自动退回原入口。
     */
    virtual IAvatar* Spawn(const FString& Key, const FString& ObjectTypeRid,
                           const FString& Clazz)
    {
        return Spawn(Key, ObjectTypeRid);
    }
    virtual void Recycle(IAvatar* Avatar) = 0;
};
