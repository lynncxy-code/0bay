#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Actors/AvatarActor.h"
#include "Components/CapsuleComponent.h"
#include "Components/ChildActorComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAvatarCollisionRootTest,
    "Metaverse.Avatar.CollisionCapsuleIsMovementRoot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FAvatarCollisionRootTest::RunTest(const FString& Params)
{
    const AAvatarActor* DefaultAvatar = AAvatarActor::StaticClass()->GetDefaultObject<AAvatarActor>();
    TestNotNull(TEXT("分身类默认对象存在"), DefaultAvatar);
    if (DefaultAvatar == nullptr)
    {
        return false;
    }

    const UCapsuleComponent* RootCapsule = Cast<UCapsuleComponent>(DefaultAvatar->GetRootComponent());
    TestNotNull(TEXT("被流式位置移动的根组件必须是胶囊体"), RootCapsule);
    if (RootCapsule == nullptr)
    {
        return false;
    }
    // 默认对象处于对象池暂停态(NoCollision)，此时 UE 会把 ProfileName 标成 Custom；
    // 真正决定恢复运行后响应的是对象类型和各通道响应。
    TestEqual(TEXT("根胶囊对象类型为 Pawn"),
              RootCapsule->GetCollisionObjectType(), ECC_Pawn);
    TestEqual(TEXT("根胶囊阻挡 WorldStatic 地面/楼板/楼梯"),
              RootCapsule->GetCollisionResponseToChannel(ECC_WorldStatic), ECR_Block);

    const UChildActorComponent* Visual = DefaultAvatar->FindComponentByClass<UChildActorComponent>();
    TestNotNull(TEXT("任务包人物外观组件存在"), Visual);
    if (Visual != nullptr)
    {
        TestTrue(TEXT("人物外观挂在移动胶囊之下"), Visual->GetAttachParent() == RootCapsule);
    }
    return true;
}
#endif
