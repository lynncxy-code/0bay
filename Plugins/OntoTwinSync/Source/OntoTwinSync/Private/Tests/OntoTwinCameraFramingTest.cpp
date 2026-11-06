#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "SceneInteraction/OntoTwinCameraFraming.h"
#include "TwinSceneManager.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOntoTwinCameraFramingTest,
    "OntoTwin.Camera.Framing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOntoTwinCameraFramingTest::RunTest(const FString& Parameters)
{
    using namespace OntoTwinCameraFraming;
    const FSlateRect Clear = LargestClearRect({FSlateRect(0, 0, .22f, 1),
        FSlateRect(.78f, 0, 1, 1), FSlateRect(0, 0, 1, .12f)});
    TestTrue(TEXT("Both side panels and top bar excluded"),
        FMath::IsNearlyEqual(Clear.Left, .22f) && FMath::IsNearlyEqual(Clear.Right, .78f)
        && FMath::IsNearlyEqual(Clear.Top, .12f) && Clear.Bottom == 1);
    const FSlateRect Full = LargestClearRect({});
    TestTrue(TEXT("No web uses full viewport"), Full.Left == 0 && Full.Top == 0 && Full.Right == 1 && Full.Bottom == 1);
    FVector Location;
    double Distance;
    TestFalse(TEXT("Opaque page does not produce an extreme camera jump"),
        FitBounds(FBox(FVector(-100), FVector(100)), FRotator::ZeroRotator, 1, 1,
            LargestClearRect({FSlateRect(0, 0, 1, 1)}), Location, Distance));
    // Independently project corners, across portrait, landscape, ultrawide,
    // asymmetric overlays, deep boxes and multiple viewing directions.
    for (double Aspect : {.5625, 1.0, 16.0 / 9.0, 32.0 / 9.0})
    for (const FSlateRect Rect : {Full, Clear, FSlateRect(0, .1f, .55f, .9f)})
    for (double Yaw : {0.0, 37.0, 135.0})
    {
        const FBox Bounds(FVector(-1800, -500, -200), FVector(2400, 900, 1400));
        const FRotator Rotation(-32, Yaw, 0);
        TestTrue(TEXT("Fit succeeds"), FitBounds(Bounds, Rotation, 1, 1 / Aspect, Rect, Location, Distance));
        for (int32 I = 0; I < 8; ++I)
        {
            const FVector Corner((I & 1) ? Bounds.Max.X : Bounds.Min.X,
                (I & 2) ? Bounds.Max.Y : Bounds.Min.Y, (I & 4) ? Bounds.Max.Z : Bounds.Min.Z);
            const FVector Local = Rotation.UnrotateVector(Corner - Location);
            const double U = .5 + Local.Y / Local.X * .5;
            const double V = .5 - Local.Z / Local.X * Aspect * .5;
            TestTrue(TEXT("Corner in front of near plane"), Local.X >= 49.99);
            TestTrue(TEXT("Corner stays inside usable viewport with padding"),
                U >= Rect.Left && U <= Rect.Right && V >= Rect.Top && V <= Rect.Bottom);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOntoTwinBusinessCameraTest,
    "OntoTwin.Camera.BusinessDefault", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOntoTwinBusinessCameraTest::RunTest(const FString& Parameters)
{
    const auto Settings = UWorld::InitializationValues().AllowAudioPlayback(false)
        .CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None,
        nullptr, true, ERHIFeatureLevel::Num, &Settings);
    auto* Player = World->SpawnActor<APlayerController>();
    // This transient test world deliberately never initializes actors for play.
    World->AddController(Player);
    Player->SpawnPlayerCameraManager();
    auto* Manager = World->SpawnActor<ATwinSceneManager>();
    auto* Camera = World->SpawnActor<ACameraActor>();
    Camera->SetActorLocationAndRotation(FVector(123, 456, 789), FRotator(-40, 65, 0));
    Camera->GetCameraComponent()->SetFieldOfView(72);
    const FTransform Authored = Camera->GetActorTransform();
    TestFalse(TEXT("Missing business camera falls back"), Manager->TryFocusBusinessCamera(TEXT("test")));
    Camera->Tags.Add(TEXT("OntoTwinViewForBusiness=test"));
    TestTrue(TEXT("Saved business camera takes precedence"), Manager->TryFocusBusinessCamera(TEXT("test")));
    TestTrue(TEXT("Authored transform retained"), Camera->GetActorTransform().Equals(Authored));
    TestEqual(TEXT("Authored FOV retained"), Camera->GetCameraComponent()->FieldOfView, 72.f);
    auto* Duplicate = World->SpawnActor<ACameraActor>();
    Duplicate->Tags = Camera->Tags;
    TestFalse(TEXT("Ambiguous binding falls back instead of arbitrary selection"), Manager->TryFocusBusinessCamera(TEXT("test")));
    World->DestroyWorld(false);
    return true;
}
#endif
