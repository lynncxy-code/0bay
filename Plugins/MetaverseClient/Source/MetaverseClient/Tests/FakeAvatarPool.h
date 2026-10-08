#pragma once

#include "CoreMinimal.h"
#include "Core/IAvatar.h"
#include "Core/IAvatarPool.h"

/** 测试替身:纯 C++,不依赖 UObject/UWorld,记录被设置的位姿与状态。 */
class FFakeAvatar : public IAvatar
{
public:
    FString Key;
    FString ObjectTypeRid;
    FString Clazz;
    double X = 0, Y = 0, Z = 0, Heading = 0, SampleTime = 0;
    TArray<double> SampleTimes;   // 全量记录,供时间轴间距/单调性断言
    int32 TimelineResets = 0;
    FString State;

    virtual void SetTarget(double InX, double InY, double InZ, double InHeading,
                           double InSampleTime) override
    {
        X = InX; Y = InY; Z = InZ; Heading = InHeading; SampleTime = InSampleTime;
        SampleTimes.Add(InSampleTime);
    }
    virtual void SetState(const FString& InState) override { State = InState; }
    virtual void ResetTimeline() override { ++TimelineResets; }
};

class FFakeAvatarPool : public IAvatarPool
{
public:
    TArray<TUniquePtr<FFakeAvatar>> Owned;   // 拥有权,测试结束自动释放
    TArray<FFakeAvatar*> Spawned;
    TArray<FFakeAvatar*> Recycled;

    virtual IAvatar* Spawn(const FString& Key, const FString& ObjectTypeRid) override
    {
        return Spawn(Key, ObjectTypeRid, FString());
    }

    virtual IAvatar* Spawn(const FString& Key, const FString& ObjectTypeRid,
                           const FString& Clazz) override
    {
        TUniquePtr<FFakeAvatar> A = MakeUnique<FFakeAvatar>();
        A->Key = Key; A->ObjectTypeRid = ObjectTypeRid; A->Clazz = Clazz;
        FFakeAvatar* Ptr = A.Get();
        Spawned.Add(Ptr);
        Owned.Add(MoveTemp(A));
        return Ptr;
    }

    virtual void Recycle(IAvatar* Avatar) override
    {
        Recycled.Add(static_cast<FFakeAvatar*>(Avatar));
    }
};
