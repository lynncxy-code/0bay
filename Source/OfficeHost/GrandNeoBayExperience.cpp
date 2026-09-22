#include "GrandNeoBayExperience.h"
#include "GrandNeoBayWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Camera/CameraComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InputComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "UnrealClient.h"

AGrandNeoBayExperience::AGrandNeoBayExperience()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BuildingPlacement"));
    SceneRoot->SetMobility(EComponentMobility::Static);
    RootComponent = SceneRoot;
    Building = CreateDefaultSubobject<UChildActorComponent>(TEXT("BuildingExterior"));
    Building->SetupAttachment(SceneRoot);
    Building->SetMobility(EComponentMobility::Static);
    FloorRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Floor09Placement"));
    FloorRoot->SetupAttachment(SceneRoot);
    FloorVolume = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Floor09Volume"));
    FloorVolume->SetupAttachment(FloorRoot);
    FloorVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    FloorVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
    FloorVolume->SetGenerateOverlapEvents(false);
    FloorVolume->SetCastShadow(false);
    FloorVolume->SetCanEverAffectNavigation(false);
    FloorOutline = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Floor09Outline"));
    FloorOutline->SetupAttachment(FloorRoot);
    FloorOutline->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    FloorOutline->SetCastShadow(false);
    FloorOutline->SetCanEverAffectNavigation(false);
    OverviewCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("BuildingPreviewCamera"));
    OverviewCamera->SetupAttachment(SceneRoot);
    OverviewCamera->SetFieldOfView(48);
    FloorTitle = NSLOCTEXT("GrandNeoBay", "FloorTitle", "9F · 上海办公室");
    FloorDescription = NSLOCTEXT("GrandNeoBay", "FloorDescription", "中航金网 · 上海分公司\n选择楼层，进入办公空间");
}

void AGrandNeoBayExperience::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    FloorRoot->SetRelativeLocation(FloorOffset);
    FloorVolume->SetRelativeScale3D(FVector(1, 1, FMath::Max(50.f, FloorHeight) / 100.f));
    FloorOutline->SetRelativeScale3D(FVector(1, 1, FMath::Max(50.f, FloorHeight) / 100.f));
    PreviewDistance = OverviewDistance;
    PreviewYaw = OverviewYaw;
    PreviewElevation = OverviewElevation;
    UpdateCamera();
}

void AGrandNeoBayExperience::BeginPlay()
{
    Super::BeginPlay();
    BeginSeconds = FPlatformTime::Seconds();
    bSelfTest = FParse::Param(FCommandLine::Get(), TEXT("GNBTest"));
    PreviewDistance = OverviewDistance;
    PreviewYaw = OverviewYaw;
    PreviewElevation = OverviewElevation;
    if (FloorVolume->GetMaterial(0)) VolumeMaterial = FloorVolume->CreateDynamicMaterialInstance(0);
    if (FloorOutline->GetMaterial(0)) OutlineMaterial = FloorOutline->CreateDynamicMaterialInstance(0);
    UpdatePresentation();
    UpdateCamera();
}

void AGrandNeoBayExperience::EndPlay(const EEndPlayReason::Type Reason)
{
    if (Controller.IsValid() && Controller->GetViewTarget() == this && SavedViewTarget.IsValid())
        Controller->SetViewTarget(SavedViewTarget.Get());
    ReleaseInput();
    if (Widget) Widget->RemoveFromParent();
    Super::EndPlay(Reason);
}

void AGrandNeoBayExperience::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!Widget)
    {
        if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0); PC && PC->IsLocalController())
        {
            Controller = PC;
            Widget = CreateWidget<UGrandNeoBayWidget>(PC);
            Widget->SetExperience(this);
            Widget->AddToViewport(10000);
            EnableInput(PC);
            InputComponent->BindKey(EKeys::F9, IE_Pressed, this, &AGrandNeoBayExperience::OpenBuilding);
            if (bStartInOverview && !bSelfTest) OpenBuilding();
        }
    }
    TransitionRemaining = FMath::Max(0.f, TransitionRemaining - DeltaSeconds);
    if (bReturning && TransitionRemaining <= 0)
    {
        bReturning = false;
        bOverview = false;
        ReleaseInput();
        UE_LOG(LogTemp, Display, TEXT("GNB: returned to office; original view and pawn retained"));
    }
    if (bOverview && !bReturning && Controller.IsValid())
    {
        HideOfficeWidgets();
        // UIOnly owns preview input; the project remains responsible for all office camera modes.
        bHovered = !bPointerOverUI && !IsTransitioning() && HitFloor();
    }
    else bHovered = false;
    const float Target = bSelected && !bReturning ? 1.f : 0.f;
    DrawerAlpha = FMath::FInterpConstantTo(DrawerAlpha, Target, DeltaSeconds, 1.f / FMath::Max(0.1f, DrawerDuration));
    const float Ease = DrawerAlpha * DrawerAlpha * (3.f - 2.f * DrawerAlpha);
    FloorRoot->SetRelativeLocation(FloorOffset + DrawerOffset * Ease);
    UpdatePresentation();
    if (Widget) Widget->Refresh();
    if (bSelfTest) TickSelfTest();
}

void AGrandNeoBayExperience::UpdateCamera()
{
    const FRotator Orbit(PreviewElevation, PreviewYaw, 0);
    const FVector Position = OverviewTarget + Orbit.Vector() * PreviewDistance;
    OverviewCamera->SetRelativeLocation(Position);
    OverviewCamera->SetRelativeRotation((OverviewTarget - Position).Rotation());
}

void AGrandNeoBayExperience::UpdatePresentation()
{
    if (AActor* Exterior = Building->GetChildActor())
    {
        const bool bHide = !bOverview && !bShowBuildingInOffice;
        if (Exterior->IsHidden() != bHide) Exterior->SetActorHiddenInGame(bHide);
    }
    FloorVolume->SetVisibility(bOverview);
    FloorOutline->SetVisibility(bOverview);
    if (VolumeMaterial)
    {
        VolumeMaterial->SetScalarParameterValue(TEXT("Opacity"), bSelected ? .34f : (bHovered ? .30f : .09f));
        VolumeMaterial->SetScalarParameterValue(TEXT("Glow"), bSelected || bHovered ? 1.5f : .65f);
    }
    if (OutlineMaterial)
        OutlineMaterial->SetScalarParameterValue(TEXT("Glow"), bSelected || bHovered ? 3.f : 1.25f);
}

void AGrandNeoBayExperience::OpenBuilding()
{
    if (bOverview || !Controller.IsValid() || IsTransitioning()) return;
    APlayerController* PC = Controller.Get();
    SavedViewTarget = PC->GetViewTarget();
    SavedPawn = PC->GetPawn();
    if (SavedPawn.IsValid())
    {
        // Avoid showing the first-person character's head during the outgoing/incoming blend.
        bAddedPawnToHidden = !PC->HiddenActors.Contains(SavedPawn.Get());
        if (bAddedPawnToHidden) PC->HiddenActors.Add(SavedPawn.Get());
        TInlineComponentArray<UPrimitiveComponent*> Primitives;
        SavedPawn->GetComponents(Primitives);
        for (UPrimitiveComponent* Primitive : Primitives)
        {
            if (!PC->HiddenPrimitiveComponents.Contains(Primitive))
            {
                PC->HiddenPrimitiveComponents.Add(Primitive);
                AddedHiddenPawnPrimitives.Add(Primitive);
            }
        }
        // Suspend only this pawn while browsing, including its automatic route and movement.
        // Keep component states intact so returning resumes the existing session.
        bSavedPawnTick = SavedPawn->IsActorTickEnabled();
        SavedPawn->SetActorTickEnabled(false);
        TInlineComponentArray<UActorComponent*> Components;
        SavedPawn->GetComponents(Components);
        for (UActorComponent* Component : Components)
        {
            if (Component->IsComponentTickEnabled())
            {
                SuspendedPawnComponents.Add(Component);
                Component->SetComponentTickEnabled(false);
            }
        }
    }
    bSavedCursor = PC->bShowMouseCursor;
    PC->SetIgnoreMoveInput(true);
    PC->SetIgnoreLookInput(true);
    bInputClaimed = true;
    bOverview = true;
    bReturning = false;
    bSelected = bHovered = false;
    DrawerAlpha = 0;
    PreviewDistance = OverviewDistance;
    PreviewYaw = OverviewYaw;
    PreviewElevation = OverviewElevation;
    UpdateCamera();
    UpdatePresentation();
    PC->SetViewTargetWithBlend(this, CameraBlendSeconds, VTBlend_Cubic, 0, true);
    TransitionRemaining = CameraBlendSeconds;
    PC->bShowMouseCursor = true;
    Widget->Refresh();
    FInputModeUIOnly Mode;
    Mode.SetWidgetToFocus(Widget->TakeWidget());
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    PC->SetInputMode(Mode);
    UE_LOG(LogTemp, Display, TEXT("GNB: building overview opened"));
}

void AGrandNeoBayExperience::SelectFloor()
{
    if (!bOverview || IsTransitioning()) return;
    bSelected = true;
    UE_LOG(LogTemp, Display, TEXT("GNB: floor 09 selected"));
}

void AGrandNeoBayExperience::ClearSelection()
{
    if (!IsTransitioning()) bSelected = false;
}

void AGrandNeoBayExperience::EnterFloor()
{
    if (bSelected) ReturnToOffice();
}

void AGrandNeoBayExperience::ReturnToOffice()
{
    if (!bOverview || IsTransitioning() || !Controller.IsValid()) return;
    AActor* Destination = SavedViewTarget.Get();
    if (!Destination) Destination = Controller->GetPawn();
    if (!Destination)
    {
        UE_LOG(LogTemp, Warning, TEXT("GNB: office camera unavailable; remaining in overview"));
        return;
    }
    bSelected = bHovered = false;
    bReturning = true;
    Controller->SetViewTargetWithBlend(Destination, CameraBlendSeconds, VTBlend_Cubic, 0, true);
    TransitionRemaining = FMath::Max(CameraBlendSeconds, DrawerDuration);
}

void AGrandNeoBayExperience::ReleaseInput()
{
    for (const auto& Pair : SavedWidgets)
        if (Pair.Key.IsValid()) Pair.Key->SetVisibility(Pair.Value);
    SavedWidgets.Reset();
    if (bInputClaimed && SavedPawn.IsValid()) SavedPawn->SetActorTickEnabled(bSavedPawnTick);
    for (const auto& Component : SuspendedPawnComponents)
        if (Component.IsValid()) Component->SetComponentTickEnabled(true);
    SuspendedPawnComponents.Reset();
    if (!bInputClaimed || !Controller.IsValid())
    {
        bInputClaimed = false;
        return;
    }
    APlayerController* PC = Controller.Get();
    if (bAddedPawnToHidden && SavedPawn.IsValid()) PC->HiddenActors.Remove(SavedPawn.Get());
    bAddedPawnToHidden = false;
    for (const auto& Primitive : AddedHiddenPawnPrimitives)
        if (Primitive.IsValid()) PC->HiddenPrimitiveComponents.Remove(Primitive.Get());
    AddedHiddenPawnPrimitives.Reset();
    PC->SetIgnoreMoveInput(false);
    PC->SetIgnoreLookInput(false);
    PC->bShowMouseCursor = bSavedCursor;
    if (bSavedCursor)
    {
        FInputModeGameAndUI Mode;
        Mode.SetHideCursorDuringCapture(false);
        Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        PC->SetInputMode(Mode);
    }
    else PC->SetInputMode(FInputModeGameOnly());
    bInputClaimed = false;
}

void AGrandNeoBayExperience::HideOfficeWidgets()
{
    TArray<UUserWidget*> Widgets;
    UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Widgets, UUserWidget::StaticClass(), true);
    for (UUserWidget* Other : Widgets)
    {
        if (!Other || Other == Widget) continue;
        const TWeakObjectPtr<UUserWidget> Key(Other);
        if (!SavedWidgets.Contains(Key)) SavedWidgets.Add(Key, Other->GetVisibility());
        Other->SetVisibility(ESlateVisibility::Collapsed);
    }
}

FVector AGrandNeoBayExperience::GetCardWorldPosition() const
{
    return FloorRoot->GetComponentTransform().TransformPosition(CardAnchor);
}

bool AGrandNeoBayExperience::HitFloor() const
{
    if (!Controller.IsValid() || !FloorVolume->GetStaticMesh() || !bPreviewPointerValid) return false;
    FVector Origin, Direction;
    // UIOnly consumes pointer events before FSceneViewport can update its cached mouse position.
    // Use the DPI-correct position from the preview widget's actual pointer event instead.
    if (!Controller->DeprojectScreenPositionToWorld(PreviewPointer.X, PreviewPointer.Y, Origin, Direction)) return false;
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(GrandNeoBayFloor), true);
    return FloorVolume->LineTraceComponent(Hit, Origin, Origin + Direction * 100000, Params);
}

void AGrandNeoBayExperience::ClickScene()
{
    if (IsTransitioning()) return;
    if (HitFloor()) SelectFloor();
    else ClearSelection();
}

void AGrandNeoBayExperience::OrbitPreview(FVector2D Delta)
{
    if (!bOverview || IsTransitioning()) return;
    PreviewYaw -= Delta.X * .2f;
    PreviewElevation = FMath::Clamp(PreviewElevation + static_cast<float>(Delta.Y) * .15f, 5.f, 70.f);
    UpdateCamera();
}

void AGrandNeoBayExperience::ZoomPreview(float Delta)
{
    if (!bOverview || IsTransitioning()) return;
    PreviewDistance = FMath::Clamp(PreviewDistance * FMath::Pow(.88f, Delta), 6500.f, 30000.f);
    UpdateCamera();
}

void AGrandNeoBayExperience::RecordCheck(bool bPassed, const FString& Name)
{
    TestChecks.Add((bPassed ? TEXT("PASS: ") : TEXT("FAIL: ")) + Name);
    bTestFailed |= !bPassed;
    UE_LOG(LogTemp, Display, TEXT("GNB_TEST %s"), *TestChecks.Last());
}

void AGrandNeoBayExperience::TickSelfTest()
{
    if (!Widget || !Controller.IsValid()) return;
    const double Age = FPlatformTime::Seconds() - BeginSeconds;
    auto Shot = [this](const TCHAR* Name)
    {
        if (!FParse::Param(FCommandLine::Get(), TEXT("NullRHI")))
        {
            const FString Path = FPaths::ProjectSavedDir() / TEXT("GrandNeoBay") / Name;
            FScreenshotRequest::RequestScreenshot(Path, true, false);
        }
    };
    if (TestStep == 0 && Age > 8)
    {
        RecordCheck(Building->GetChildActor() != nullptr, TEXT("Building BP is loaded"));
        RecordCheck(FloorVolume->GetStaticMesh() && FloorOutline->GetStaticMesh(), TEXT("CAD floor proxy and outline are loaded"));
        RecordCheck(FMath::IsNearlyEqual(FloorRoot->GetComponentLocation().Z, 4320., 1.), TEXT("Floor datum is Z=4320 cm"));
        if (Controller->GetPawn()) TestSavedPawnLocation = Controller->GetPawn()->GetActorLocation();
        OpenBuilding();
        RecordCheck(bOverview, TEXT("F9 entry path opens preview without a visible prompt"));
        RecordCheck(bOverview && bInputClaimed, TEXT("Overview claims preview input"));
        ++TestStep;
    }
    else if (TestStep == 1 && Age > 11)
    {
        RecordCheck(Widget->GetRootWidget() && Widget->GetRootWidget()->GetCachedGeometry().GetLocalSize().X > 100,
            TEXT("Preview UI has a rendered widget tree"));
        RecordCheck(Controller->GetViewTarget() == this, TEXT("Overview camera blend completes"));
        RecordCheck(Controller->GetPawn() == SavedPawn.Get(), TEXT("Preview preserves possession"));
        FHitResult Hit;
        const FVector Center = FloorRoot->GetComponentTransform().TransformPosition(FVector(2043, 0, FloorHeight / 2));
        const FVector Outward = GetActorTransform().TransformVectorNoScale(FVector(10000, 0, 0));
        RecordCheck(FloorVolume->LineTraceComponent(Hit, Center + Outward, Center, FCollisionQueryParams(NAME_None, true)), TEXT("Floor proxy is ray-pickable"));
        Shot(TEXT("01_building_overview.png"));
        FVector2D FloorScreen;
        const FVector Surface = FloorRoot->GetComponentTransform().TransformPosition(FVector(3080, -1500, FloorHeight / 2));
        const bool bProjected = Controller->ProjectWorldLocationToScreen(Surface, FloorScreen);
        RecordCheck(bProjected && Widget->TestActivateControl(TEXT("Scene"), FloorScreen) && bSelected,
            TEXT("UIOnly pointer event selects the 3D floor with correct DPI coordinates"));
        ++TestStep;
    }
    else if (TestStep == 2 && Age > 14)
    {
        RecordCheck(bSelected && FMath::IsNearlyEqual(DrawerAlpha, 1.f), TEXT("Selection reaches full drawer offset"));
        RecordCheck(FloorRoot->GetRelativeLocation().Equals(FloorOffset + DrawerOffset, 1), TEXT("Only proxy moves by configured offset"));
        Shot(TEXT("02_floor09_selected.png"));
        ++TestStep;
    }
    else if (TestStep == 3 && Age > 16)
    {
        ClearSelection();
        ++TestStep;
    }
    else if (TestStep == 4 && Age > 18)
    {
        RecordCheck(!bSelected && FMath::IsNearlyZero(DrawerAlpha), TEXT("Dismissal retracts drawer"));
        FVector2D FloorScreen;
        const FVector Surface = FloorRoot->GetComponentTransform().TransformPosition(FVector(3080, -1500, FloorHeight / 2));
        const bool bProjected = Controller->ProjectWorldLocationToScreen(Surface, FloorScreen);
        RecordCheck(bProjected && Widget->TestActivateControl(TEXT("Scene"), FloorScreen) && bSelected,
            TEXT("Floor can be selected again without the removed overview panel"));
        ++TestStep;
    }
    else if (TestStep == 5 && Age > 20)
    {
        RecordCheck(!SavedPawn.IsValid() || SavedPawn->GetActorLocation().Equals(TestSavedPawnLocation, 1),
            TEXT("Preview pauses the existing pawn without relocating it"));
        RecordCheck(Widget->TestActivateControl(TEXT("Enter")) && bReturning, TEXT("Enter card button starts return transition"));
        ++TestStep;
    }
    else if (TestStep == 6 && Age > 23)
    {
        RecordCheck(!bOverview && !bInputClaimed, TEXT("Entry releases preview input"));
        RecordCheck(Controller->GetViewTarget() == SavedViewTarget.Get(), TEXT("Entry restores original office camera"));
        RecordCheck(Controller->GetPawn() == SavedPawn.Get(), TEXT("Entry preserves original pawn"));
        RecordCheck(SuspendedPawnComponents.IsEmpty(), TEXT("Office pawn component ticks are restored"));
        RecordCheck(!FloorVolume->IsVisible(), TEXT("Floor proxy is hidden in office"));
        Shot(TEXT("03_return_to_office.png"));
        ++TestStep;
    }
    else if (TestStep == 7 && Age > 25)
    {
        OpenBuilding();
        RecordCheck(bOverview, TEXT("F9 entry path works again after returning"));
        ++TestStep;
    }
    else if (TestStep == 8 && Age > 27)
    {
        ReturnToOffice();
        RecordCheck(bReturning, TEXT("Escape return path works without a selected floor"));
        ReturnToOffice();
        ++TestStep;
    }
    else if (TestStep == 9 && Age > 30)
    {
        RecordCheck(!bOverview && !bInputClaimed && FMath::IsNearlyZero(DrawerAlpha), TEXT("Repeated open/back is reversible"));
        TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
        Report->SetBoolField(TEXT("passed"), !bTestFailed);
        TArray<TSharedPtr<FJsonValue>> Checks;
        for (const FString& Check : TestChecks) Checks.Add(MakeShared<FJsonValueString>(Check));
        Report->SetArrayField(TEXT("checks"), Checks);
        FString Json;
        FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
        const FString Dir = FPaths::ProjectSavedDir() / TEXT("GrandNeoBay");
        IFileManager::Get().MakeDirectory(*Dir, true);
        FFileHelper::SaveStringToFile(Json, *(Dir / TEXT("runtime_test.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        UE_LOG(LogTemp, Display, TEXT("GNB_TEST_COMPLETE passed=%d"), !bTestFailed);
        bSelfTest = false;
        Controller->ConsoleCommand(TEXT("quit"));
    }
}
