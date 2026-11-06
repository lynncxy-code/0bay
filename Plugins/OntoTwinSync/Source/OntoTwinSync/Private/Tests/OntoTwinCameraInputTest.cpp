#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "SceneInteraction/TwinGodViewPawn.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Slate/SceneViewport.h"
#include "HAL/IConsoleManager.h"

// Development-only real-map regression driver. Goes through the controller's
// actual input stack, not directly through MovePlanar/Look.
static FAutoConsoleCommandWithWorldAndArgs CameraTestKey(
    TEXT("OntoTwin.Test.CameraKey"), TEXT("Development test: <key> <down|up|axis-value>"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        if (!World || Args.Num() != 2) return;
        if (APlayerController* PC = World->GetFirstPlayerController())
        {
            const bool bAxis = Args[1] != TEXT("down") && Args[1] != TEXT("up");
            PC->InputKey(FInputKeyEventArgs::CreateSimulated(FKey(*Args[0]),
                bAxis ? IE_Axis : Args[1] == TEXT("down") ? IE_Pressed : IE_Released,
                bAxis ? FCString::Atof(*Args[1]) : 1.f, bAxis ? 1 : 0,
                IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(), false,
                World->GetGameViewport() ? World->GetGameViewport()->Viewport : nullptr));
        }
    }));

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOntoTwinCameraFocusTest,
    "OntoTwin.Camera.FocusHandoff", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOntoTwinCameraFocusTest::RunTest(const FString& Parameters)
{
    const auto Settings = UWorld::InitializationValues().AllowAudioPlayback(false)
        .CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None,
        nullptr, true, ERHIFeatureLevel::Num, &Settings);
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);
    auto* Client = NewObject<UGameViewportClient>(GEngine);
    Context.GameViewport = Client;
    auto ViewportWidget = SNew(SViewport);
    auto Panel = SNew(SButton);
    auto Window = SNew(SWindow).ClientSize(FVector2D(400, 300))
        [SNew(SVerticalBox)
            + SVerticalBox::Slot()[ViewportWidget]
            + SVerticalBox::Slot()[Panel]];
    FSlateApplication& Slate = FSlateApplication::Get();
    Slate.AddWindow(Window, false);
    TSharedPtr<FSceneViewport> Viewport = MakeShared<FSceneViewport>(Client, ViewportWidget);
    Client->Viewport = Viewport.Get();
    auto* Player = World->SpawnActor<APlayerController>();
    Player->PlayerInput = NewObject<UPlayerInput>(Player);
    auto* Pawn = World->SpawnActor<ATwinGodViewPawn>();
    Player->Possess(Pawn);
    Pawn->DispatchBeginPlay();
    auto Press = [&]()
    {
        Player->PlayerInput->InputKey(FInputKeyEventArgs(Viewport.Get(),
            IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(), EKeys::W, IE_Pressed, 0u));
        Player->PlayerInput->ProcessInputStack({}, .016f, false);
    };
    Slate.SetUserFocus(0, ViewportWidget, EFocusCause::SetDirectly);
    Press();
    Pawn->Movement->Velocity = FVector(100, 0, 0);
    Slate.SetUserFocus(0, Panel, EFocusCause::SetDirectly);
    TestFalse(TEXT("Leaving scene releases a held movement key"), Player->IsInputKeyDown(EKeys::W));
    TestTrue(TEXT("Leaving scene clears momentum"), Pawn->Movement->Velocity.IsNearlyZero());
    // The same input that gives focus back must not be erased by our callback.
    Press();
    Slate.SetUserFocus(0, ViewportWidget, EFocusCause::SetDirectly);
    TestTrue(TEXT("Returning to scene preserves new input"), Player->IsInputKeyDown(EKeys::W));
    Pawn->MovePlanar(FVector2D(0, 1));
    const FVector Start = Pawn->GetActorLocation();
    Pawn->Tick(.1f);
    Pawn->Movement->TickComponent(.1f, LEVELTICK_All, nullptr);
    TestTrue(TEXT("Movement actually advances after focus handoff"), FVector::Distance(Start, Pawn->GetActorLocation()) > 1.f);
    Pawn->Destroy();
    Client->Viewport = nullptr;
    Viewport.Reset();
    Slate.RequestDestroyWindow(Window);
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}
#endif
