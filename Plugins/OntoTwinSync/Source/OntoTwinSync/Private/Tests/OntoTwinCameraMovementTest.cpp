#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "SceneInteraction/TwinGodViewPawn.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/FloatingPawnMovement.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOntoTwinCameraMovementTest,
    "OntoTwin.Camera.ManualMovement", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOntoTwinCameraMovementTest::RunTest(const FString& Parameters)
{
    const auto Settings = UWorld::InitializationValues().AllowAudioPlayback(false)
        .CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None,
        nullptr, true, ERHIFeatureLevel::Num, &Settings);
    auto* Pawn = World->SpawnActor<ATwinGodViewPawn>();
    auto* Player = World->SpawnActor<APlayerController>();
    Player->Possess(Pawn);
    const FTransform Target(FRotator(0, 90, 0), FVector(1000, 2000, 3000));

    Pawn->MovePlanar(FVector2D(0, 1));
    Pawn->Movement->Velocity = FVector(1800, 0, 0);
    Pawn->StopManualMovement();
    TestTrue(TEXT("Focus handoff clears queued input"), Pawn->GetPendingMovementInputVector().IsNearlyZero());
    TestTrue(TEXT("Focus handoff stops velocity immediately"), Pawn->Movement->Velocity.IsNearlyZero());
    const FVector Stopped = Pawn->GetActorLocation();
    Pawn->Movement->TickComponent(.1f, LEVELTICK_All, nullptr);
    TestTrue(TEXT("No drift after handoff"), Pawn->GetActorLocation().Equals(Stopped));

    Pawn->Movement->Velocity = FVector(1800, 0, 0);
    Pawn->MovePlanar(FVector2D(0, 1));
    Pawn->FocusToTransform(Target, .5f);
    TestTrue(TEXT("Auto focus clears old velocity and input"), Pawn->Movement->Velocity.IsNearlyZero()
        && Pawn->GetPendingMovementInputVector().IsNearlyZero());
    Pawn->MoveVertical(0);
    Pawn->Look(FVector2D::ZeroVector);
    Pawn->Tick(.5f);
    TestTrue(TEXT("Zero input preserves automatic transition"), Pawn->GetActorTransform().Equals(Target));

    for (int32 InputKind = 0; InputKind < 3; ++InputKind)
    {
        Pawn->SetActorTransform(FTransform::Identity);
        Pawn->FocusToTransform(Target, .5f);
        Pawn->Tick(.1f);
        if (InputKind == 0) Pawn->MovePlanar(FVector2D(0, 1));
        if (InputKind == 1) Pawn->MoveVertical(1);
        if (InputKind == 2) Pawn->Look(FVector2D(10, 0));
        const FTransform Manual = Pawn->GetActorTransform();
        Pawn->Tick(1.f);
        TestTrue(TEXT("Manual control prevents later auto-focus overwrite"), Pawn->GetActorTransform().Equals(Manual));
        Pawn->StopManualMovement();
    }
    World->DestroyWorld(false);
    return true;
}
#endif
