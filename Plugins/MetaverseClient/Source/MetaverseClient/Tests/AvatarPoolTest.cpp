#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Actors/AvatarPool.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAvatarPoolStableRandomTest,
    "Metaverse.AvatarPool.StableRandomCharacter",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FAvatarPoolStableRandomTest::RunTest(const FString& Params)
{
    const int32 First = AAvatarPool::PickStableVariantIndex(TEXT("person:024"), 42, 6);
    TestTrue(TEXT("下标合法"), First >= 0 && First < 6);
    TestEqual(TEXT("相同 key/seed 跨 PIE 保持同一人物外观"),
              AAvatarPool::PickStableVariantIndex(TEXT("person:024"), 42, 6), First);
    TestEqual(TEXT("空角色池不选择"),
              AAvatarPool::PickStableVariantIndex(TEXT("person:024"), 42, 0), INDEX_NONE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAvatarPoolRandomDistributionTest,
    "Metaverse.AvatarPool.RandomCharactersUseMultipleVariants",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FAvatarPoolRandomDistributionTest::RunTest(const FString& Params)
{
    TSet<int32> Picks;
    for (int32 Index = 0; Index < 100; ++Index)
    {
        Picks.Add(AAvatarPool::PickStableVariantIndex(
            FString::Printf(TEXT("person:%03d"), Index), 42, 6));
    }
    TestTrue(TEXT("一批人物会使用多个任务包角色"), Picks.Num() > 1);
    return true;
}
#endif
