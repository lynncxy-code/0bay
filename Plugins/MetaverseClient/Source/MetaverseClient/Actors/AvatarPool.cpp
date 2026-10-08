#include "Actors/AvatarPool.h"
#include "Actors/AvatarActor.h"
#include "Engine/World.h"

AAvatarPool::AAvatarPool()
{
    PrimaryActorTick.bCanEverTick = false;
}

IAvatar* AAvatarPool::Spawn(const FString& Key, const FString& ObjectTypeRid)
{
    return Spawn(Key, ObjectTypeRid, FString());
}

IAvatar* AAvatarPool::Spawn(const FString& Key, const FString& ObjectTypeRid,
                            const FString& Clazz)
{
    const FResolvedAvatar Selection = ResolveAvatar(Key, ObjectTypeRid, Clazz);
    TSubclassOf<AAvatarActor> Class = Selection.DriverClass;
    if (Class == nullptr)
    {
        return nullptr;
    }

    AAvatarActor* Actor = nullptr;
    TArray<AAvatarActor*>* Bucket = FreePool.Find(Class);
    if (Bucket != nullptr && Bucket->Num() > 0)
    {
        Actor = Bucket->Pop();   // 复用空闲实例
    }
    else
    {
        FActorSpawnParameters Sp;
        Sp.Owner = this;
        Actor = GetWorld()->SpawnActor<AAvatarActor>(Class, FTransform::Identity, Sp);
    }

    if (Actor != nullptr)
    {
        Managed.Add(Actor);   // 记录归属,Recycle 时校验
        ManagedInterfaces.Add(static_cast<IAvatar*>(Actor));
        Actor->ConfigureVisual(Selection.VisualClass, Selection.CharacterAsset,
                               Selection.VisualRelativeTransform);
        Actor->ResetForReuse();
        Actor->SetRuntimeActive(true);
    }
    return Actor;   // AAvatarActor 同时是 IAvatar
}

void AAvatarPool::Recycle(IAvatar* Avatar)
{
    if (Avatar == nullptr)
    {
        return;
    }
    // 本池只回收自己 Spawn 出的 AAvatarActor;先按接口指针校验归属,再 static_cast(否则为 UB)
    if (!ManagedInterfaces.Contains(Avatar))
    {
        ensureMsgf(false, TEXT("Recycle 收到非本池 Spawn 的分身,已忽略"));
        return;
    }
    AAvatarActor* Actor = static_cast<AAvatarActor*>(Avatar);
    Actor->SetRuntimeActive(false);
    FreePool.FindOrAdd(Actor->GetClass()).Add(Actor);
}

int32 AAvatarPool::PickStableVariantIndex(const FString& Key, int32 Seed, int32 VariantCount)
{
    if (VariantCount <= 0)
    {
        return INDEX_NONE;
    }
    const uint32 Hash = HashCombine(GetTypeHash(Key), GetTypeHash(Seed));
    return static_cast<int32>(Hash % static_cast<uint32>(VariantCount));
}

void AAvatarPool::ConfigurePeopleAssets(const TArray<FSoftObjectPath>& AssetPaths,
                                        const FString& ObjectTypeRid,
                                        const FString& Clazz,
                                        int32 Seed)
{
    FObjectTypeAvatar Entry;
    Entry.ObjectTypeRid = ObjectTypeRid;
    Entry.Clazz = Clazz;
    for (const FSoftObjectPath& AssetPath : AssetPaths)
    {
        if (!AssetPath.IsValid())
        {
            continue;
        }
        FAvatarVisualVariant Variant;
        Variant.CharacterAsset = TSoftObjectPtr<UObject>(AssetPath);
        Entry.RandomVisuals.Add(MoveTemp(Variant));
    }
    if (!Entry.RandomVisuals.IsEmpty())
    {
        AvatarClasses.Add(MoveTemp(Entry));
    }
    RandomSeed = Seed;
}

AAvatarPool::FResolvedAvatar AAvatarPool::ResolveAvatar(
    const FString& Key, const FString& ObjectTypeRid, const FString& Clazz) const
{
    const FObjectTypeAvatar* Matched = nullptr;

    // 本体 rid 是跨系统的首选稳定标识。
    for (const FObjectTypeAvatar& Entry : AvatarClasses)
    {
        if (!ObjectTypeRid.IsEmpty() && Entry.ObjectTypeRid == ObjectTypeRid)
        {
            Matched = &Entry;
            break;
        }
    }

    // 现场本体 rid 暂缺/变更时，让 person 等内部类别仍能加载人物资产。
    if (Matched == nullptr && !Clazz.IsEmpty())
    {
        for (const FObjectTypeAvatar& Entry : AvatarClasses)
        {
            if (!Entry.Clazz.IsEmpty() && Entry.Clazz.Equals(Clazz, ESearchCase::IgnoreCase))
            {
                Matched = &Entry;
                break;
            }
        }
    }

    FResolvedAvatar Result;
    Result.DriverClass = (Matched != nullptr && Matched->AvatarClass != nullptr)
        ? Matched->AvatarClass : DefaultAvatarClass;

    if (Matched == nullptr || Matched->RandomVisuals.IsEmpty())
    {
        return Result;
    }

    // 跳过配置数组中的空槽，避免随机命中空资产后人物偶发不可见。
    TArray<int32, TInlineAllocator<16>> ValidIndices;
    for (int32 Index = 0; Index < Matched->RandomVisuals.Num(); ++Index)
    {
        if (Matched->RandomVisuals[Index].ActorClass != nullptr
            || !Matched->RandomVisuals[Index].CharacterAsset.IsNull())
        {
            ValidIndices.Add(Index);
        }
    }
    const int32 Pick = PickStableVariantIndex(Key, RandomSeed, ValidIndices.Num());
    if (Pick != INDEX_NONE)
    {
        const FAvatarVisualVariant& Variant = Matched->RandomVisuals[ValidIndices[Pick]];
        // 配了任务包外观但没指定驱动器时，自动使用原生轨迹驱动器；只对已匹配项
        // 生效，未知类型且没有 DefaultAvatarClass 仍会跳过，保持旧版行为。
        if (Result.DriverClass == nullptr)
        {
            Result.DriverClass = AAvatarActor::StaticClass();
        }
        Result.VisualClass = Variant.ActorClass;
        Result.CharacterAsset = Variant.CharacterAsset.LoadSynchronous();
        Result.VisualRelativeTransform = Variant.RelativeTransform;
    }
    return Result;
}
