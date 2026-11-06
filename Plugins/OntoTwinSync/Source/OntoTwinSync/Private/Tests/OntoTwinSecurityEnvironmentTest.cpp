#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "WebInteraction/OntoTwinWebInteractionComponent.h"
#include "Engine/LevelStreamingAlwaysLoaded.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOntoTwinSecurityEnvironmentTest,
    "OntoTwin.WebInteraction.SecurityEnvironment",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOntoTwinSecurityEnvironmentTest::RunTest(const FString& Parameters)
{
    const auto Settings = UWorld::InitializationValues().AllowAudioPlayback(false)
        .CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None,
        nullptr, true, ERHIFeatureLevel::Num, &Settings);
    AActor* Owner = World->SpawnActor<AActor>();
    auto* Web = NewObject<UOntoTwinWebInteractionComponent>(Owner);
    auto* Exterior = NewObject<ULevelStreamingAlwaysLoaded>(World);
    Exterior->SetWorldAssetByPackageName(TEXT("/Game/OfficeShell/Maps/L_GrandNeoBay_Building"));
    Exterior->SetShouldBeVisible(true);
    World->AddStreamingLevel(Exterior);
    auto* Office = NewObject<ULevelStreamingAlwaysLoaded>(World);
    Office->SetWorldAssetByPackageName(TEXT("/Game/OfficeShell/Maps/cxy_ArtMerged"));
    Office->SetShouldBeVisible(true);
    World->AddStreamingLevel(Office);
    AActor* Satellite = World->SpawnActor<AActor>();
    Satellite->Tags.Add(TEXT("OntoTwinSatelliteMap"));
    Satellite->SetActorHiddenInGame(false);
    Satellite->SetActorEnableCollision(true);
    Web->SetSecurityEnvironmentSuppressed(true);
    TestTrue(TEXT("AlwaysLoaded uses an actual unload request"), Exterior->GetIsRequestingUnloadAndRemoval());
    TestFalse(TEXT("Office stays loaded"), Office->GetIsRequestingUnloadAndRemoval());
    TestTrue(TEXT("Satellite off"), Satellite->IsHidden());
    TestFalse(TEXT("Satellite collision off"), Satellite->GetActorEnableCollision());
    Web->SetSecurityEnvironmentSuppressed(true);
    TestEqual(TEXT("Repeated ticks retain original state only once"), Web->SecuritySuppressedLevels.Num(), 1);
    World->RemoveStreamingLevel(Exterior); // Simulate completion of engine removal.
    Web->SetSecurityEnvironmentSuppressed(false);
    TestTrue(TEXT("Descriptor reattached on leaving security"), World->GetStreamingLevels().Contains(Exterior));
    TestFalse(TEXT("Unload request cleared"), Exterior->GetIsRequestingUnloadAndRemoval());
    TestTrue(TEXT("Original visibility restored"), Exterior->GetShouldBeVisibleFlag());
    TestFalse(TEXT("Satellite restored"), Satellite->IsHidden());
    TestTrue(TEXT("Satellite collision restored"), Satellite->GetActorEnableCollision());
    Satellite->SetActorHiddenInGame(true);
    Web->SetSecurityEnvironmentSuppressed(true);
    Web->SetSecurityEnvironmentSuppressed(false);
    TestTrue(TEXT("Already-hidden satellite stays hidden on exit"), Satellite->IsHidden());
    AActor* Background = World->SpawnActor<AActor>();
    Background->Tags.Add(TEXT("OntoTwinSecurityOverviewBackground"));
    Background->SetActorHiddenInGame(true);
    AActor* DefaultSky = World->SpawnActor<AActor>();
    DefaultSky->Tags.Add(TEXT("OntoTwinDefaultBackground"));
    DefaultSky->SetActorHiddenInGame(false);
    AActor* HiddenSky = World->SpawnActor<AActor>();
    HiddenSky->Tags.Add(TEXT("OntoTwinDefaultBackground"));
    HiddenSky->SetActorHiddenInGame(true);
    AActor* Light = World->SpawnActor<AActor>();
    Light->SetActorHiddenInGame(false);
    Web->SetSecurityBackground(true);
    TestFalse(TEXT("Overview background becomes visible"), Background->IsHidden());
    TestTrue(TEXT("Original background suppressed"), DefaultSky->IsHidden());
    TestFalse(TEXT("Unrelated lighting untouched"), Light->IsHidden());
    Web->SetSecurityBackground(true);
    TestEqual(TEXT("Repeated ticks retain the original sky states"), Web->SecuritySkyHiddenStates.Num(), 3);
    Web->SetSecurityBackground(false);
    TestTrue(TEXT("Overview background hidden on exit"), Background->IsHidden());
    TestFalse(TEXT("Visible original sky restored"), DefaultSky->IsHidden());
    TestTrue(TEXT("Originally hidden sky stays hidden"), HiddenSky->IsHidden());
    TestFalse(TEXT("No business context cannot activate background"), Web->IsSecurityContextActive());
    World->DestroyWorld(false);
    return true;
}
#endif
