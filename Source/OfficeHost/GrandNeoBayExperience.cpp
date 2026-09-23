#include "GrandNeoBayExperience.h"
#include "CampusInteraction.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#if WITH_EDITOR
#include "MeshDescription.h"
#endif
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
    bCampusTest = FParse::Param(FCommandLine::Get(), TEXT("CampusTest"));
    bLandmarkTest = FParse::Param(FCommandLine::Get(), TEXT("LandmarkTest"));
    // The host loads its initial dataset/camera asynchronously. Start standalone
    // acceptance after that startup phase; normal preview timing is unchanged.
    if (bSelfTest || bCampusTest || bLandmarkTest) BeginSeconds += 25;
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
    if (Campus.IsValid()) Campus->Show(INDEX_NONE, INDEX_NONE);
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
        UpdateHoverSelection();
    }
    else bHovered = false;
    const float Target = bSelected && !bReturning ? 1.f : 0.f;
    DrawerAlpha = FMath::FInterpConstantTo(DrawerAlpha, Target, DeltaSeconds, 1.f / FMath::Max(0.1f, DrawerDuration));
    const float Ease = DrawerAlpha * DrawerAlpha * (3.f - 2.f * DrawerAlpha);
    FloorRoot->SetRelativeLocation(FloorOffset + DrawerOffset * Ease);
    UpdatePresentation();
    if (Widget) Widget->Refresh();
    if (bSelfTest) TickSelfTest();
    if (bCampusTest) TickCampusTest();
    if (bLandmarkTest) TickLandmarkTest();
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
    SelectedBuilding = INDEX_NONE;
    for (TActorIterator<ACampusInteraction> It(GetWorld()); It; ++It) { Campus = *It; break; }
    SyncMainBuildingHighlight();
    if (Campus.IsValid()) Campus->Show(INDEX_NONE, INDEX_NONE);
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
    SelectedBuilding = INDEX_NONE;
    if (Campus.IsValid()) Campus->Show(INDEX_NONE, INDEX_NONE);
    if (Widget) Widget->Refresh();
    UE_LOG(LogTemp, Display, TEXT("GNB: floor 09 selected"));
}

void AGrandNeoBayExperience::ClearSelection()
{
    if (!IsTransitioning())
    {
        bSelected = false;
        SelectedBuilding = INDEX_NONE;
        if (Campus.IsValid()) Campus->Show(INDEX_NONE, INDEX_NONE);
        if (Widget) Widget->Refresh();
    }
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
    SelectedBuilding = INDEX_NONE;
    if (Campus.IsValid()) Campus->Show(INDEX_NONE, INDEX_NONE);
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
    FText Title, Description;
    if (GetSelectedBuildingInfo(Title, Description) && Campus.IsValid())
        return Campus->GetShapeWorldTopCenter(SelectedBuilding);
    return FloorRoot->GetComponentTransform().TransformPosition(CardAnchor);
}

bool AGrandNeoBayExperience::GetSelectedBuildingInfo(FText& Title, FText& Description) const
{
    if (!Campus.IsValid() || !Campus->Shapes.IsValidIndex(SelectedBuilding)) return false;
    const FString& Key = Campus->Shapes[SelectedBuilding].Key;
    if (Key == TEXT("GrandNeoBay"))
    {
        Title = FText::FromString(TEXT("大零号湾 · 办公楼"));
        Description = FText::FromString(TEXT("沧源路 1300 号 · 上海闵行\n九层为上海办公室"));
        return true;
    }
    if (Key == TEXT("osm219751253CHN"))
    {
        Title = FText::FromString(TEXT("霍英东体育馆"));
        Description = FText::FromString(TEXT("上海交通大学闵行校区\n白色膜结构屋顶与环形场馆"));
        return true;
    }
    static const TSet<FString> ScienceKeys = {
        TEXT("osm1207394279CHN"), TEXT("osm1207394280CHN"), TEXT("osm1207394281CHN"),
        TEXT("osm1207394283CHN"), TEXT("osm1207394297CHN"), TEXT("osm1207394298CHN"),
        TEXT("osm1207394299CHN")
    };
    if (ScienceKeys.Contains(Key))
    {
        Title = FText::FromString(TEXT("交大理科楼群"));
        Description = FText::FromString(TEXT("上海交通大学闵行校区\n红砖院落与蓝灰色坡屋顶"));
        return true;
    }
    return false;
}

void AGrandNeoBayExperience::SyncMainBuildingHighlight()
{
    if (!Campus.IsValid()) return;
    // The pick shell was authored when this actor was at Z=480 cm.
    // The child model and ninth-floor proxy already follow the actor directly.
    Campus->SetMainBuildingWorldOffset(GetActorLocation() - FVector(3275.f, -3080.f, 480.f));
}

void AGrandNeoBayExperience::UpdateHoverSelection()
{
    int32 HoveredBuilding = INDEX_NONE;
    bHovered = false;
    if (!bPointerOverUI && !IsTransitioning()) HoveredBuilding = PickBuilding(bHovered);
    if (Campus.IsValid()) Campus->Show(bSelected ? INDEX_NONE : HoveredBuilding, SelectedBuilding);
}

bool AGrandNeoBayExperience::HitFloor(float* Distance) const
{
    if (!Controller.IsValid() || !FloorVolume->GetStaticMesh() || !bPreviewPointerValid) return false;
    FVector Origin, Direction;
    // UIOnly consumes pointer events before FSceneViewport can update its cached mouse position.
    // Use the DPI-correct position from the preview widget's actual pointer event instead.
    if (!Controller->DeprojectScreenPositionToWorld(PreviewPointer.X, PreviewPointer.Y, Origin, Direction)) return false;
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(GrandNeoBayFloor), true);
    const bool bHit = FloorVolume->LineTraceComponent(Hit, Origin, Origin + Direction * 400000, Params);
    if (bHit && Distance) *Distance = Hit.Distance;
    return bHit;
}

int32 AGrandNeoBayExperience::PickBuilding(bool& bFloor) const
{
    float FloorDistance = TNumericLimits<float>::Max();
    bFloor = HitFloor(&FloorDistance);
    if (!Campus.IsValid() || !Controller.IsValid() || !bPreviewPointerValid) return INDEX_NONE;
    FVector Origin, Direction;
    if (!Controller->DeprojectScreenPositionToWorld(PreviewPointer.X, PreviewPointer.Y, Origin, Direction)) return INDEX_NONE;
    float BuildingDistance;
    const int32 Index = Campus->Pick(Origin, Origin + Direction * 400000, BuildingDistance);
    // The floor lies inside its own exterior skin: keep that floor selectable.
    const bool bMainBuilding = Campus->Shapes.IsValidIndex(Index) && Campus->Shapes[Index].Key == TEXT("GrandNeoBay");
    if (bFloor && (bMainBuilding || FloorDistance <= BuildingDistance + 25.f)) return INDEX_NONE;
    bFloor = false;
    return Index;
}

void AGrandNeoBayExperience::ClickScene()
{
    if (!bOverview || bPointerOverUI || IsTransitioning()) return;
    bool bFloor = false;
    const int32 Hit = PickBuilding(bFloor);
    if (bFloor) SelectFloor();
    else
    {
        bSelected = false;
        SelectedBuilding = Hit;
        if (Campus.IsValid()) Campus->Show(INDEX_NONE, Hit);
    }
    if (Widget) Widget->Refresh();
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
    PreviewDistance = FMath::Clamp(PreviewDistance * FMath::Pow(.88f, Delta), 6500.f, 160000.f);
    UpdateCamera();
}

void AGrandNeoBayExperience::RecordCheck(bool bPassed, const FString& Name)
{
    TestChecks.Add((bPassed ? TEXT("PASS: ") : TEXT("FAIL: ")) + Name);
    bTestFailed |= !bPassed;
    UE_LOG(LogTemp, Display, TEXT("GNB_TEST %s"), *TestChecks.Last());
}

void AGrandNeoBayExperience::TickLandmarkTest()
{
    if (!Widget || !Controller.IsValid()) return;
    const double Age = FPlatformTime::Seconds() - BeginSeconds;
    const FString Stadium(TEXT("osm219751253CHN")), Science(TEXT("osm1207394279CHN"));
    auto Find = [this](const FString& Key) {
        return Campus.IsValid() ? Campus->Shapes.IndexOfByPredicate([&Key](const FCampusBuildingShape& S) { return S.Key == Key; }) : INDEX_NONE;
    };
    auto RoofPoint = [this](int32 Index) {
        FVector Point = FVector::ZeroVector;
        double Highest = -DBL_MAX;
        if (!Campus.IsValid() || !Campus->Shapes.IsValidIndex(Index)) return Point;
        const FCampusBuildingShape& Shape = Campus->Shapes[Index];
        for (int32 I = 0; I + 2 < Shape.Triangles.Num(); I += 3)
        {
            const FVector A = Shape.Vertices[Shape.Triangles[I]], B = Shape.Vertices[Shape.Triangles[I+1]], C = Shape.Vertices[Shape.Triangles[I+2]];
            const FVector Center = (A+B+C)/3.;
            if (Center.Z > Highest && FMath::Abs(FVector::CrossProduct(B-A,C-A).GetSafeNormal().Z) > .1)
            { Highest = Center.Z; Point = Center; }
        }
        return Campus->GetActorTransform().TransformPosition(Point);
    };
    auto Focus = [this](int32 Index, float Distance, float Yaw) {
        if (!Campus.IsValid() || Index == INDEX_NONE) return;
        OverviewTarget = GetActorTransform().InverseTransformPosition(Campus->GetShapeWorldCenter(Index));
        PreviewDistance = Distance; PreviewYaw = Yaw; PreviewElevation = 38;
        ClearSelection();
        Widget->TestActivateControl(TEXT("Hover"), FVector2D(-20,-20));
        UpdateCamera();
    };
    auto Shot = [](const TCHAR* Name) {
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("HaijuNorthBeta") / Name, true, false);
    };
    if (TestStep == 0 && Age > 8)
    {
        OpenBuilding();
        RecordCheck(Campus.IsValid() && Campus->Shapes.Num() == 1676, TEXT("Refined landmark shapes loaded; stadium annex targets merged"));
#if WITH_EDITOR
        int32 SourceTriangles = 0, RemainingTriangles = 0;
        for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
        {
            UStaticMesh* Mesh = It->GetStaticMeshComponent()->GetStaticMesh();
            if (!Mesh || !Mesh->GetPathName().Contains(TEXT("/Landmarks/Imported/"))) continue;
            if (const FMeshDescription* Description = Mesh->GetMeshDescription(0))
            {
                if (Mesh->GetPathName().Contains(TEXT("/Landmarks_Combined/"))) SourceTriangles += Description->Triangles().Num();
                if (Mesh->GetPathName().Contains(TEXT("/Background_Changed_Batches/"))) RemainingTriangles += Description->Triangles().Num();
            }
        }
        RecordCheck(SourceTriangles == 26844, FString::Printf(TEXT("Imported landmark source geometry retains 26844 triangles (actual %d)"),SourceTriangles));
        RecordCheck(RemainingTriangles == 7854, FString::Printf(TEXT("Filtered background source geometry retains 7854 triangles (actual %d)"),RemainingTriangles));
#endif
        for (const FString& Key : {Stadium, Science, FString(TEXT("osm1207394280CHN")), FString(TEXT("osm1207394281CHN")), FString(TEXT("osm1207394283CHN")), FString(TEXT("osm1207394297CHN")), FString(TEXT("osm1207394298CHN")), FString(TEXT("osm1207394299CHN"))})
        {
            const int32 Index = Find(Key);
            const FVector Roof = RoofPoint(Index);
            float Distance = TNumericLimits<float>::Max();
            const int32 Hit = Campus.IsValid() ? Campus->Pick(Roof+FVector(0,0,10000), Roof-FVector(0,0,10000), Distance) : INDEX_NONE;
            RecordCheck(Index != INDEX_NONE && Hit == Index, TEXT("Vertical roof ray picks ") + Key);
            RecordCheck(Hit == Index && Index != INDEX_NONE && FMath::Abs(Distance-10000.f)<1.f, TEXT("Roof ray hit matches triangle surface height: ") + Key);
            if (Key == Stadium && Index != INDEX_NONE)
            {
                double MaxZ = -DBL_MAX;
                for (const FVector& V : Campus->Shapes[Index].Vertices) MaxZ = FMath::Max(MaxZ,V.Z);
                RecordCheck(FMath::Abs(MaxZ-2946.1)<.1, TEXT("Stadium apex proxy is 29.341m roof plus 0.12m skin offset"));
            }
            if (Key == Stadium) RecordCheck(Roof.Z > 2800 && Roof.Z < 2960, TEXT("Stadium roof triangle centroid is above the old flat height"));
            if (Key == Science) RecordCheck(Roof.Z > 3900 && Roof.Z < 3950, TEXT("Science pick reaches the new high tower roof"));
        }
        Focus(Find(Stadium),23000,-20);
        ++TestStep;
    }
    else if (TestStep == 1 && Age > 12)
    {
        Shot(TEXT("landmark_01_stadium.png")); ++TestStep;
    }
    else if (TestStep == 2 && Age > 14)
    {
        RecordCheck(bOverview && !IsTransitioning() && Controller->GetViewTarget()==this, TEXT("Stadium click starts with preview camera active and blend complete"));
        FVector2D Screen;
        const int32 Index = Find(Stadium);
        RecordCheck(Controller->ProjectWorldLocationToScreen(RoofPoint(Index),Screen) && Widget->TestActivateControl(TEXT("Scene"),Screen) && SelectedBuilding == Index,
            TEXT("UI pointer selects the stadium membrane roof"));
        FText Title, Description;
        RecordCheck(GetSelectedBuildingInfo(Title, Description) && Title.ToString() == TEXT("霍英东体育馆") && Widget->IsBuildingInfoCardVisible(),
            TEXT("Stadium click opens the building card without an entry button"));
        ++TestStep;
    }
    else if (TestStep == 3 && Age > 17)
    {
        Shot(TEXT("landmark_02_stadium_highlight.png")); ++TestStep;
    }
    else if (TestStep == 4 && Age > 19)
    {
        RecordCheck(Widget->TestActivateControl(TEXT("Close")) && !HasSelection() && Campus.IsValid() && Campus->GetDisplayedShape() == INDEX_NONE,
            TEXT("Stadium information card closes and clears its highlight"));
        // Mid-courtyard is south of the north tower used as the interaction target.
        Focus(Find(Science),32000,15);
        OverviewTarget = GetActorTransform().InverseTransformPosition(Campus->GetActorTransform().TransformPosition(FVector(17000,4500,1400)));
        UpdateCamera(); ++TestStep;
    }
    else if (TestStep == 5 && Age > 22)
    {
        Shot(TEXT("landmark_03_science.png")); ++TestStep;
    }
    else if (TestStep == 6 && Age > 24)
    {
        RecordCheck(bOverview && !IsTransitioning() && Controller->GetViewTarget()==this, TEXT("Science click starts with preview camera active and blend complete"));
        FVector2D Screen;
        const int32 Index = Find(Science);
        RecordCheck(Controller->ProjectWorldLocationToScreen(RoofPoint(Index),Screen) && Widget->TestActivateControl(TEXT("Scene"),Screen) && SelectedBuilding == Index,
            TEXT("UI pointer selects the science tower roof"));
        FText Title, Description;
        RecordCheck(GetSelectedBuildingInfo(Title, Description) && Title.ToString() == TEXT("交大理科楼群") && Widget->IsBuildingInfoCardVisible(),
            TEXT("Science click opens the building card without an entry button"));
        ++TestStep;
    }
    else if (TestStep == 7 && Age > 27)
    {
        Shot(TEXT("landmark_04_science_highlight.png")); ++TestStep;
    }
    else if (TestStep == 8 && Age > 29)
    {
        RecordCheck(Widget->TestActivateControl(TEXT("Close")) && !HasSelection() && Campus.IsValid() && Campus->GetDisplayedShape() == INDEX_NONE,
            TEXT("Science information card closes and clears its highlight"));
        Widget->TestActivateControl(TEXT("Hover"),FVector2D(-20,-20));
        OverviewTarget = FVector(0,0,3100); PreviewDistance = 100000; PreviewYaw = OverviewYaw; PreviewElevation = 58;
        UpdateCamera(); ++TestStep;
    }
    else if (TestStep == 9 && Age > 32)
    {
        Shot(TEXT("landmark_05_campus.png")); ++TestStep;
    }
    else if (TestStep == 10 && Age > 34)
    {
        ReturnToOffice(); ++TestStep;
    }
    else if (TestStep == 11 && Age > 37)
    {
        RecordCheck(!bOverview && !bInputClaimed && Campus.IsValid() && Campus->GetDisplayedShape() == INDEX_NONE,
            TEXT("Landmark preview returns to original office with clean input and highlights"));
        TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
        Report->SetBoolField(TEXT("passed"),!bTestFailed);
        TArray<TSharedPtr<FJsonValue>> Checks;
        for (const FString& Check : TestChecks) Checks.Add(MakeShared<FJsonValueString>(Check));
        Report->SetArrayField(TEXT("checks"),Checks);
        FString Json;
        FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));
        FFileHelper::SaveStringToFile(Json,*(FPaths::ProjectSavedDir()/TEXT("HaijuNorthBeta/landmark_runtime_test.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        bLandmarkTest=false; Controller->ConsoleCommand(TEXT("quit"));
    }
}

void AGrandNeoBayExperience::TickCampusTest()
{
    if (!Widget || !Controller.IsValid()) return;
    const double Age = FPlatformTime::Seconds() - BeginSeconds;
    auto Shot = [](const TCHAR* Name)
    {
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("HaijuNorthBeta") / Name, true, false);
    };
    if (TestStep == 0 && Age > 8)
    {
        OpenBuilding();
        PreviewDistance = 105000; PreviewElevation = 58;
        UpdateCamera();
        RecordCheck(Campus.IsValid() && Campus->Shapes.Num() == 1676, TEXT("All surrounding buildings and main building have pick shapes; stadium annexes share its target"));
        ++TestStep;
    }
    else if (TestStep == 1 && Age > 12)
    {
        Shot(TEXT("campus_01_ground.png"));
        TestStep = 101;
    }
    else if (TestStep == 101 && Age > 13)
    {
        RecordCheck(Controller->GetViewTarget() == this, TEXT("Campus overview uses the existing preview camera"));
        if (Campus.IsValid())
        {
            for (int32 I = 0; I < Campus->Shapes.Num(); ++I)
            {
                const FVector Center = Campus->GetShapeWorldCenter(I);
                const float Range = FVector::Dist2D(Center, GetActorLocation());
                if (Range < 12000 || Range > 28000 || Campus->Shapes[I].Key == TEXT("GrandNeoBay")) continue;
                FVector2D Screen;
                if (!Controller->ProjectWorldLocationToScreen(Center, Screen) || Screen.X < 150 || Screen.X > 1400 || Screen.Y < 150 || Screen.Y > 850) continue;
                SetPreviewPointer(Screen, true);
                bool bFloor = false;
                if (PickBuilding(bFloor) != I || bFloor) continue;
                TestCampusBuilding = I;
                RecordCheck(Widget->TestActivateControl(TEXT("Scene"), Screen) && SelectedBuilding == I && !bSelected,
                    TEXT("Real UI pointer event selects one visible campus building"));
                break;
            }
        }
        RecordCheck(TestCampusBuilding != INDEX_NONE, TEXT("A visible nearby building is ray-pickable"));
        TestStep = 2;
    }
    else if (TestStep == 2 && Age > 15)
    {
        RecordCheck(Campus.IsValid() && Campus->GetDisplayedShape() == TestCampusBuilding, TEXT("Click selection shows only the selected building skin and edges"));
        Shot(TEXT("campus_02_highlight.png"));
        TestStep = 102;
    }
    else if (TestStep == 102 && Age > 16)
    {
        SetPreviewPointer(FVector2D(4,4), true);
        SetPointerOverUI(true);
        ClickScene();
        RecordCheck(SelectedBuilding == TestCampusBuilding, TEXT("UI hit targets block scene selection"));
        SetPointerOverUI(false);
        SetPreviewPointer(FVector2D(-1,-1), false);
        ClickScene();
        RecordCheck(!HasSelection() && Campus.IsValid() && Campus->GetDisplayedShape() == INDEX_NONE, TEXT("Empty scene click clears building selection"));
        TestStep = 3;
    }
    else if (TestStep == 3 && Age > 17)
    {
        if (Campus.IsValid() && TestCampusBuilding != INDEX_NONE)
        {
            FVector2D Screen;
            Controller->ProjectWorldLocationToScreen(Campus->GetShapeWorldCenter(TestCampusBuilding), Screen);
            Widget->TestActivateControl(TEXT("Hover"), Screen);
            UpdateHoverSelection();
            RecordCheck(Campus->GetDisplayedShape() == TestCampusBuilding && !HasSelection(),
                TEXT("Hover highlights a building without selecting it"));
        }
        ++TestStep;
    }
    else if (TestStep == 4 && Age > 19)
    {
        Widget->TestActivateControl(TEXT("Hover"), FVector2D(-20,-20));
        UpdateHoverSelection();
        RecordCheck(Campus.IsValid() && Campus->GetDisplayedShape() == INDEX_NONE,
            TEXT("Leaving a building clears transient hover"));
        ++TestStep;
    }
    else if (TestStep == 5 && Age > 21)
    {
        PreviewDistance = OverviewDistance; PreviewElevation = OverviewElevation;
        UpdateCamera();
        ++TestStep;
    }
    else if (TestStep == 6 && Age > 23)
    {
        if (Campus.IsValid())
        {
            const int32 Main = Campus->Shapes.IndexOfByPredicate([](const FCampusBuildingShape& S) { return S.Key == TEXT("GrandNeoBay"); });
            RecordCheck(Main != INDEX_NONE && FMath::IsNearlyEqual(Campus->GetShapeWorldTopCenter(Main).Z, 6340.f + GetActorLocation().Z - 480.f, 1.f)
                && FMath::IsNearlyEqual(FloorRoot->GetComponentLocation().Z, GetActorLocation().Z + FloorOffset.Z, 1.f),
                TEXT("Main building shell and ninth-floor proxy follow the placed building height"));
            // Aim at the near wing above the ninth floor, so the floor keeps its own click target.
            const FVector Roof = GetActorTransform().TransformPosition(FVector(2043, -1500, 5700));
            FVector2D Screen;
            const bool Projected = Controller->ProjectWorldLocationToScreen(Roof, Screen);
            RecordCheck(Projected && Widget->TestActivateControl(TEXT("Scene"), Screen) && SelectedBuilding == Main,
                TEXT("Main building facade selects the complete building outside the ninth-floor target"));
            FText Title, Description;
            RecordCheck(GetSelectedBuildingInfo(Title, Description) && Title.ToString() == TEXT("大零号湾 · 办公楼") && Widget->IsBuildingInfoCardVisible(),
                TEXT("Main building click opens the information card without an entry button"));
        }
        ++TestStep;
    }
    else if (TestStep == 7 && Age > 25)
    {
        Shot(TEXT("campus_03_main_building.png"));
        TestStep = 107;
    }
    else if (TestStep == 107 && Age > 26)
    {
        RecordCheck(Widget->TestActivateControl(TEXT("Close")) && !HasSelection() && Campus.IsValid() && Campus->GetDisplayedShape() == INDEX_NONE,
            TEXT("Main building information card closes and clears its highlight"));
        SelectFloor();
        RecordCheck(bSelected && SelectedBuilding == INDEX_NONE && Campus.IsValid() && Campus->GetDisplayedShape() == INDEX_NONE,
            TEXT("Ninth-floor selection replaces whole-building highlight"));
        TestStep = 8;
    }
    else if (TestStep == 8 && Age > 28)
    {
        Shot(TEXT("campus_04_floor_detail.png"));
        TestStep = 108;
    }
    else if (TestStep == 108 && Age > 29)
    {
        ReturnToOffice();
        TestStep = 9;
    }
    else if (TestStep == 9 && Age > 31)
    {
        RecordCheck(!bOverview && !HasSelection() && Campus.IsValid() && Campus->GetDisplayedShape() == INDEX_NONE,
            TEXT("Return to office clears campus highlights and restores input"));
        TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
        Report->SetBoolField(TEXT("passed"), !bTestFailed);
        TArray<TSharedPtr<FJsonValue>> Checks;
        for (const FString& Check : TestChecks) Checks.Add(MakeShared<FJsonValueString>(Check));
        Report->SetArrayField(TEXT("checks"), Checks);
        FString Json;
        FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
        FFileHelper::SaveStringToFile(Json, *(FPaths::ProjectSavedDir() / TEXT("HaijuNorthBeta/campus_runtime_test.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        bCampusTest = false;
        Controller->ConsoleCommand(TEXT("quit"));
    }
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
        RecordCheck(FMath::IsNearlyEqual(FloorRoot->GetComponentLocation().Z, GetActorLocation().Z + FloorOffset.Z, 1.),
            TEXT("Floor datum follows the current building placement"));
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
