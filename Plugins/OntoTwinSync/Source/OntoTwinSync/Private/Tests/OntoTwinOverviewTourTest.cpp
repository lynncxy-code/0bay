#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "SceneInteraction/OntoTwinOverviewTour.h"
#include "SceneInteraction/TwinGodViewPawn.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/FloatingPawnMovement.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOntoTwinOverviewTourTest,
    "OntoTwin.Camera.OverviewTour", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOntoTwinOverviewTourTest::RunTest(const FString& Parameters)
{
    const FTransform Overview(FRotator(-50, 90, 0), FVector(3105, -2994, 7281));
    const FBox Floor(FVector(500, -2000, 4300), FVector(6000, 3000, 4800));
    const OntoTwinOverviewTour::FRoute Route(Overview, Floor);
    TestTrue(TEXT("Opening five seconds hold authored composition"), Route.Sample(4.99f).Equals(Overview));
    TestTrue(TEXT("Seventy seconds restores exact authored pose"), Route.Sample(70).Equals(Overview));
    TestTrue(TEXT("No looping after completion"), Route.Sample(140).Equals(Overview));
    const FVector Right = Overview.GetRotation().GetRightVector();
    TestTrue(TEXT("Left view"), FVector::DotProduct(Route.Sample(20).GetLocation() - Overview.GetLocation(), Right) < 0);
    TestTrue(TEXT("Right view"), FVector::DotProduct(Route.Sample(55).GetLocation() - Overview.GetLocation(), Right) > 0);
    TestTrue(TEXT("Close pass advances toward floor"), FVector::DotProduct(Route.Sample(35).GetLocation() - Overview.GetLocation(), Overview.GetRotation().GetForwardVector()) > 0);
    for (float Time = .1f; Time < 70; Time += .1f)
    {
        const FTransform Pose = Route.Sample(Time), Previous = Route.Sample(Time - .1f);
        TestFalse(TEXT("Finite route"), Pose.ContainsNaN());
        TestTrue(TEXT("No camera jumps"), FVector::Distance(Pose.GetLocation(), Previous.GetLocation()) < 30.);
        TestTrue(TEXT("No abrupt turns"), Pose.GetRotation().AngularDistance(Previous.GetRotation()) < FMath::DegreesToRadians(2.));
    }
    for (float Time : {20.f, 35.f, 55.f})
    {
        const FVector Before = (Route.Sample(Time).GetLocation() - Route.Sample(Time - .01f).GetLocation()) / .01f;
        const FVector After = (Route.Sample(Time + .01f).GetLocation() - Route.Sample(Time).GetLocation()) / .01f;
        TestTrue(TEXT("Continuous velocity at segment joins"), FVector::Distance(Before, After) < 1.);
    }
    const auto Settings = UWorld::InitializationValues().AllowAudioPlayback(false)
        .CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None,
        nullptr, true, ERHIFeatureLevel::Num, &Settings);
    auto* Pawn = World->SpawnActor<ATwinGodViewPawn>();
    auto* Player = World->SpawnActor<APlayerController>();
    Player->Possess(Pawn);
    for (int32 Input = 0; Input < 5; ++Input)
    {
        Pawn->SetActorTransform(Overview);
        Pawn->StartOverviewTour(Overview, Floor);
        TestTrue(TEXT("Explicit entry arms one run"), Pawn->IsOverviewTourActive());
        if (Input == 0) Pawn->MovePlanar(FVector2D(0, 1));
        if (Input == 1) Pawn->MoveVertical(1);
        if (Input == 2) Pawn->Look(FVector2D(1, 0));
        if (Input == 3) Pawn->AdjustSpeed(1);
        if (Input == 4) Pawn->FocusToTransform(Overview, 0);
        TestFalse(TEXT("Input or detail focus cancels queued tour"), Pawn->IsOverviewTourActive());
        const FTransform Manual = Pawn->GetActorTransform();
        Pawn->Tick(80.f);
        TestFalse(TEXT("Idle time cannot resume tour"), Pawn->IsOverviewTourActive());
        TestTrue(TEXT("No jump on takeover"), Pawn->GetActorTransform().Equals(Manual));
        Pawn->StopManualMovement();
    }
    Pawn->StartOverviewTour(Overview, FBox(ForceInit));
    TestFalse(TEXT("Missing floor data does not start a blind tour"), Pawn->IsOverviewTourActive());
    World->DestroyWorld(false);
    return true;
}
#endif
