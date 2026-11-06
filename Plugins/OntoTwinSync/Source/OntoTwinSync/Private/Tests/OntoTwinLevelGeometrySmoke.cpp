#if WITH_DEV_AUTOMATION_TESTS
#include "TwinSceneManager.h"
#include "TwinInstance.h"
#include "SceneInteraction/TwinInteractionManagerComponent.h"
#include "WebInteraction/OntoTwinWebInteractionComponent.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/UserWidget.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

// Explicit opt-in integration check against the running local backend. No scene or asset writes.
static FAutoConsoleCommandWithWorldAndArgs GLevelGeometrySmoke(
    TEXT("OntoTwin.ValidateLevelGeometry"),
    TEXT("Verify restored roaming, dashboards and unchanged authored geometry after startup."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        if (!World || !World->IsGameWorld()) return;
        ATwinSceneManager* Scene = nullptr;
        int32 ManagerCount = 0;
        for (TActorIterator<ATwinSceneManager> It(World); It; ++It) { Scene = *It; ++ManagerCount; }
        if (ManagerCount != 1 || !Scene->bUseLevelGeometry) return;
        const FString BusinessViewId = Args.Num() > 0 ? Args[0] : FString();

        struct FGeometryBaseline
        {
            TWeakObjectPtr<AStaticMeshActor> Actor;
            FTransform Transform;
            TWeakObjectPtr<UStaticMesh> Mesh;
        };
        TArray<FGeometryBaseline> Baseline;
        for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
            Baseline.Add({*It, It->GetActorTransform(), It->GetStaticMeshComponent()->GetStaticMesh()});
        FTimerHandle Timer;
        World->GetTimerManager().SetTimer(Timer, FTimerDelegate::CreateWeakLambda(Scene, [Scene, Baseline, BusinessViewId]()
        {
            UWorld* W = Scene->GetWorld();
            bool bGeometryUnchanged = true;
            for (const auto& Item : Baseline)
                bGeometryUnchanged &= Item.Actor.IsValid()
                    && Item.Actor->GetActorTransform().Equals(Item.Transform)
                    && Item.Actor->GetStaticMeshComponent()->GetStaticMesh() == Item.Mesh.Get();
            int32 DynamicInstances = 0;
            for (TActorIterator<ATwinInstance> It(W); It; ++It) ++DynamicInstances;
            TArray<UUserWidget*> Widgets;
            UWidgetBlueprintLibrary::GetAllWidgetsOfClass(W, Widgets, UUserWidget::StaticClass(), false);
            bool bDock = false, bHud = false;
            for (const auto* Widget : Widgets)
            {
                bDock |= Widget->GetClass()->GetName() == TEXT("OntoTwinRuntimeDockWidget");
                bHud |= Widget->GetClass()->GetName() == TEXT("OntoTwinRoamingHUDWidget");
            }
            auto* Interaction = Scene->InteractionManager;
            const bool bRoaming = Interaction && Interaction->IsRoamingActive();
            UE_LOG(LogTemp, Display, TEXT("ONTOTWIN_RESTORE_CHECK geometry=%d static_actors=%d dynamic_instances=%d roaming=%d dock=%d hud=%d status=%s"),
                bGeometryUnchanged, Baseline.Num(), DynamicInstances, bRoaming, bDock, bHud,
                Interaction ? *Interaction->GetHudDetailText() : TEXT("missing"));
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("OntoTwinRestore_Roaming.png"), true, false);
            FTimerHandle CameraTimer;
            W->GetTimerManager().SetTimer(CameraTimer, FTimerDelegate::CreateWeakLambda(Scene, [Scene, BusinessViewId]()
            {
                Scene->InteractionManager->ToggleCameraMode();
                FTimerHandle WebTimer;
                Scene->GetWorldTimerManager().SetTimer(WebTimer, FTimerDelegate::CreateWeakLambda(Scene, [Scene, BusinessViewId]()
                {
                    UE_LOG(LogTemp, Display, TEXT("ONTOTWIN_RESTORE_CAMERA mode=%d roaming=%d"),
                        static_cast<int32>(Scene->InteractionManager->GetCameraMode()), Scene->InteractionManager->IsRoamingActive());
                    const bool bOpened = BusinessViewId.IsEmpty()
                        ? Scene->WebInteractionManager->OpenProjectHome()
                        : Scene->WebInteractionManager->OpenBusinessView(BusinessViewId, FString());
                    UE_LOG(LogTemp, Display, TEXT("ONTOTWIN_RESTORE_WEB ready=%d open_request=%d"),
                        Scene->WebInteractionManager->IsRuntimeReady(), bOpened);
                    FTimerHandle CaptureTimer;
                    Scene->GetWorldTimerManager().SetTimer(CaptureTimer, FTimerDelegate::CreateWeakLambda(Scene, [Scene]()
                    {
                        UE_LOG(LogTemp, Display, TEXT("ONTOTWIN_RESTORE_WEB_RESULT open=%d"), Scene->WebInteractionManager->IsWebOpen());
                        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("OntoTwinRestore_Dashboard.png"), true, false);
                    }), 5.0f, false);
                }), 2.0f, false);
            }), 2.0f, false);
        }), 15.0f, false);
    }));
#endif
