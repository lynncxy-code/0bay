#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/IAvatarPool.h"
#include "AvatarPool.generated.h"

class AAvatarActor;

/** 一个可随机选中的任务包角色；可直接引用任意 Actor/Character 蓝图。 */
USTRUCT(BlueprintType)
struct FAvatarVisualVariant
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Metaverse", meta = (DisplayName = "任务包角色类"))
    TSubclassOf<AActor> ActorClass;

    /**
     * OntoTwinSync 的 UTwinCharacterAsset 等角色任务资产。宿主存在 OntoTwinSync 时
     * 直接应用其骨骼、动画蓝图和走路动画；通用工程可留空。
     */
    UPROPERTY(EditAnywhere, Category = "Metaverse", meta = (DisplayName = "OntoTwin 角色任务资产"))
    TSoftObjectPtr<UObject> CharacterAsset;

    /** 修正不同任务包角色的原点、朝向或缩放。 */
    UPROPERTY(EditAnywhere, Category = "Metaverse", meta = (DisplayName = "相对变换"))
    FTransform RelativeTransform = FTransform::Identity;
};

/** objectTypeRid/clazz → 分身驱动器及任务包角色随机池。 */
USTRUCT(BlueprintType)
struct FObjectTypeAvatar
{
    GENERATED_BODY()

    /** 优先匹配本体对象类型 rid；可留空，改用 Clazz 兜底。 */
    UPROPERTY(EditAnywhere, Category = "Metaverse", meta = (DisplayName = "对象类型 RID"))
    FString ObjectTypeRid;                      // 本体对象类型 rid,如 "ri.obj.<uuid>"

    /** 总线内部类别，如 person；RID 未命中时使用，不区分大小写。 */
    UPROPERTY(EditAnywhere, Category = "Metaverse", meta = (DisplayName = "总线类别 Clazz"))
    FString Clazz;

    /** 可选自定义驱动器；有随机角色时可留空，插件会使用原生轨迹驱动器。 */
    UPROPERTY(EditAnywhere, Category = "Metaverse", meta = (DisplayName = "分身驱动器类"))
    TSubclassOf<AAvatarActor> AvatarClass;

    /**
     * 从内容浏览器选择任务包已有的多个角色蓝图。每个目标 key 会稳定随机命中一个，
     * 离场回收后再次出现仍保持同一外观。
     */
    UPROPERTY(EditAnywhere, Category = "Metaverse", meta = (DisplayName = "随机任务包角色"))
    TArray<FAvatarVisualVariant> RandomVisuals;
};

/** 按目标类别取 Actor 类的简单对象池:Spawn 复用空闲实例,Recycle 隐藏回栈。 */
UCLASS()
class AAvatarPool : public AActor, public IAvatarPool
{
    GENERATED_BODY()

public:
    AAvatarPool();

    // IAvatarPool
    virtual IAvatar* Spawn(const FString& Key, const FString& ObjectTypeRid) override;
    virtual IAvatar* Spawn(const FString& Key, const FString& ObjectTypeRid,
                           const FString& Clazz) override;
    virtual void Recycle(IAvatar* Avatar) override;

    /** 纯函数测试缝；相同 key/seed 永远得到相同的合法下标。 */
    static int32 PickStableVariantIndex(const FString& Key, int32 Seed, int32 VariantCount);

    /** PIE 自动启动入口：用项目配置的任务资产建立 person 映射。 */
    void ConfigurePeopleAssets(const TArray<FSoftObjectPath>& AssetPaths,
                               const FString& ObjectTypeRid,
                               const FString& Clazz,
                               int32 Seed);

protected:
    UPROPERTY(EditAnywhere, Category = "Metaverse", meta = (DisplayName = "类型与角色映射"))
    TArray<FObjectTypeAvatar> AvatarClasses;

    UPROPERTY(EditAnywhere, Category = "Metaverse", meta = (DisplayName = "默认分身驱动器类"))
    TSubclassOf<AAvatarActor> DefaultAvatarClass;

    /** 改这个值可整体重洗角色分配；同一 seed 下分配跨 PIE 保持稳定。 */
    UPROPERTY(EditAnywhere, Category = "Metaverse", meta = (DisplayName = "角色随机种子"))
    int32 RandomSeed = 20260821;

private:
    struct FResolvedAvatar
    {
        TSubclassOf<AAvatarActor> DriverClass;
        TSubclassOf<AActor> VisualClass;
        UObject* CharacterAsset = nullptr;
        FTransform VisualRelativeTransform = FTransform::Identity;
    };

    FResolvedAvatar ResolveAvatar(const FString& Key, const FString& ObjectTypeRid,
                                  const FString& Clazz) const;

    // 按 Actor 类分桶的空闲栈
    TMap<UClass*, TArray<AAvatarActor*>> FreePool;
    // 本池创建过的分身;Recycle 前校验,给 IAvatar*→AAvatarActor* 的 static_cast 兜底
    TSet<AAvatarActor*> Managed;
    TSet<IAvatar*> ManagedInterfaces;
};
