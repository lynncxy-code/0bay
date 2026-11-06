#include "SceneInteraction/TwinGodViewPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "UnrealClient.h"
#include "Framework/Application/SlateApplication.h"
#include "SceneInteraction/OntoTwinOverviewTour.h"
#include "Camera/PlayerCameraManager.h"
#include "Widgets/SViewport.h"
#include "TwinSceneManager.h"
#include "WebInteraction/OntoTwinWebInteractionComponent.h"

ATwinGodViewPawn::ATwinGodViewPawn()
{
    PrimaryActorTick.bCanEverTick = true;
    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    RootComponent = SceneRoot;
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(SceneRoot);
    Movement = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("Movement"));
    Movement->MaxSpeed = 1800.0f;
    Movement->Acceleration = 5000.0f;
    Movement->Deceleration = 6000.0f;
    // Stop stale input before FloatingPawnMovement integrates this frame.
    Movement->bTickBeforeOwner = false;
    Movement->AddTickPrerequisiteActor(this);
}

void ATwinGodViewPawn::BeginPlay()
{
    Super::BeginPlay();
    if (FSlateApplication::IsInitialized())
    {
        FocusChangingHandle = FSlateApplication::Get().OnFocusChanging().AddWeakLambda(this,
            [this](const FFocusEvent& Event, const FWeakWidgetPath&, const TSharedPtr<SWidget>& OldFocus,
                const FWidgetPath&, const TSharedPtr<SWidget>& NewFocus)
            {
                const UGameViewportClient* Client = GetWorld()->GetGameViewport();
                const TSharedPtr<SViewport> Viewport = Client ? Client->GetGameViewportWidget() : nullptr;
                // Clear only when leaving this viewport, never when a click/key
                // returns control to it. Clearing on return eats that first input.
                if (Event.GetUser() == 0 && Viewport.IsValid()
                    && OldFocus == Viewport && NewFocus != Viewport) ReleaseManualInput();
            });
        ApplicationActivationHandle = FSlateApplication::Get().OnApplicationActivationStateChanged().AddWeakLambda(
            this, [this](bool bActive) { if (!bActive) ReleaseManualInput(); });
    }
}

void ATwinGodViewPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (FSlateApplication::IsInitialized())
    {
        FSlateApplication::Get().OnFocusChanging().Remove(FocusChangingHandle);
        FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(ApplicationActivationHandle);
    }
    Super::EndPlay(EndPlayReason);
}

bool ATwinGodViewPawn::CanAcceptManualInput() const
{
    const UGameViewportClient* ViewportClient = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
    return !ViewportClient || !ViewportClient->Viewport
        || (FSlateApplication::IsInitialized() && FSlateApplication::Get().IsActive());
}

void ATwinGodViewPawn::ReleaseManualInput()
{
    APlayerController* PC = Cast<APlayerController>(GetController());
    if (!PC || !PC->IsLocalController()) return;
    PC->FlushPressedKeys();
    StopManualMovement();
}

void ATwinGodViewPawn::RestoreSceneInputFocus()
{
    APlayerController* PC = Cast<APlayerController>(GetController());
    if (!PC || PC->GetViewTarget() != this || !FSlateApplication::IsInitialized()
        || !FSlateApplication::Get().IsActive()) return;
    const ATwinSceneManager* Manager = GetOwner() ? Cast<ATwinSceneManager>(GetOwner()->GetOwner()) : nullptr;
    if (Manager && Manager->WebInteractionManager
        && Manager->WebInteractionManager->IsPointerOverInteractiveWeb()) return;
    if (UGameViewportClient* Client = GetWorld()->GetGameViewport())
    {
        if (TSharedPtr<SViewport> Viewport = Client->GetGameViewportWidget())
            FSlateApplication::Get().SetUserFocus(0, Viewport, EFocusCause::SetDirectly);
    }
}

void ATwinGodViewPawn::StopManualMovement()
{
    ConsumeMovementInputVector();
    Movement->StopMovementImmediately();
}

void ATwinGodViewPawn::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    TickOverviewTour(DeltaSeconds);
    if (!CanAcceptManualInput())
    {
        StopManualMovement();
    }
    if (!bFocusTransitionActive) return;
    StopManualMovement();

    FocusTransitionElapsed += DeltaSeconds;
    const float Alpha = FMath::Clamp(
        FocusTransitionElapsed / FMath::Max(FocusTransitionDuration, KINDA_SMALL_NUMBER),
        0.0f,
        1.0f);
    const float SmoothAlpha = FMath::SmoothStep(0.0f, 1.0f, Alpha);
    const FVector Location = FMath::Lerp(
        FocusTransitionStart.GetLocation(),
        FocusTransitionTarget.GetLocation(),
        SmoothAlpha);
    const FQuat Rotation = FQuat::Slerp(
        FocusTransitionStart.GetRotation(),
        FocusTransitionTarget.GetRotation(),
        SmoothAlpha).GetNormalized();
    SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
    if (Alpha >= 1.0f)
    {
        bFocusTransitionActive = false;
    }
}

void ATwinGodViewPawn::Configure(float InMoveSpeedCmS, float InLookSensitivity)
{
    Movement->MaxSpeed = FMath::Clamp(InMoveSpeedCmS, 100.0f, 10000.0f);
    LookSensitivity = FMath::Clamp(InLookSensitivity, 0.1f, 5.0f);
}

void ATwinGodViewPawn::MovePlanar(const FVector2D& Input)
{
    if (Input.IsNearlyZero() || !CanAcceptManualInput()) return;
    CancelOverviewTour();
    bFocusTransitionActive = false;
    const FRotator YawRotation(0.0f, GetActorRotation().Yaw, 0.0f);
    AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X), Input.Y);
    AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y), Input.X);
}

void ATwinGodViewPawn::MoveVertical(float Input)
{
    if (FMath::IsNearlyZero(Input) || !CanAcceptManualInput()) return;
    CancelOverviewTour();
    bFocusTransitionActive = false;
    AddMovementInput(FVector::UpVector, Input);
}

void ATwinGodViewPawn::Look(const FVector2D& Input)
{
    if (Input.IsNearlyZero() || !CanAcceptManualInput()) return;
    CancelOverviewTour();
    bFocusTransitionActive = false;
    FRotator Rotation = GetActorRotation();
    Rotation.Yaw += Input.X * LookSensitivity;
    Rotation.Pitch = FMath::Clamp(Rotation.Pitch - Input.Y * LookSensitivity, -89.0f, 89.0f);
    SetActorRotation(Rotation);
}

void ATwinGodViewPawn::AdjustSpeed(float Axis)
{
    if (FMath::IsNearlyZero(Axis) || !CanAcceptManualInput()) return;
    CancelOverviewTour();
    Movement->MaxSpeed = FMath::Clamp(Movement->MaxSpeed * (Axis > 0.0f ? 1.2f : 0.8f), 100.0f, 10000.0f);
}

void ATwinGodViewPawn::FocusToTransform(
    const FTransform& TargetTransform,
    float DurationSeconds)
{
    CancelOverviewTour();
    StopManualMovement();
    if (DurationSeconds <= KINDA_SMALL_NUMBER)
    {
        bFocusTransitionActive = false;
        SetActorTransform(TargetTransform, false, nullptr, ETeleportType::TeleportPhysics);
        return;
    }
    FocusTransitionStart = GetActorTransform();
    FocusTransitionTarget = TargetTransform;
    FocusTransitionElapsed = 0.0f;
    FocusTransitionDuration = DurationSeconds;
    bFocusTransitionActive = true;
}

void ATwinGodViewPawn::StartOverviewTour(const FTransform& Overview, const FBox& FloorBounds)
{
    CancelOverviewTour();
    if (!FloorBounds.IsValid) return;
    OverviewTourAnchor = Overview;
    OverviewTourBounds = FloorBounds;
    OverviewTourElapsed = OverviewTourWaitElapsed = 0.f;
    bOverviewTourStarted = false;
    bOverviewTourActive = true;
    StopManualMovement();
}

void ATwinGodViewPawn::CancelOverviewTour()
{
    if (!bOverviewTourActive) return;
    bOverviewTourActive = false;
    StopManualMovement();
    UE_LOG(LogTemp, Display, TEXT("OntoTwin overview tour stopped at %.2fs"), OverviewTourElapsed);
}

void ATwinGodViewPawn::TickOverviewTour(float DeltaSeconds)
{
    if (!bOverviewTourActive) return;
    APlayerController* PC = Cast<APlayerController>(GetController());
    const bool bOwnsView = PC && PC->GetPawn() == this && PC->GetViewTarget() == this;
    if (!bOverviewTourStarted)
    {
        OverviewTourWaitElapsed += DeltaSeconds;
        // Wait for both possession and the view-target/focus blend to finish.
        if (OverviewTourWaitElapsed > 5.f) { CancelOverviewTour(); return; }
        if (!bOwnsView || bFocusTransitionActive
            || (PC->PlayerCameraManager && PC->PlayerCameraManager->PendingViewTarget.Target)) return;
        bOverviewTourStarted = true;
        UE_LOG(LogTemp, Display, TEXT("OntoTwin overview tour started: duration=70s"));
    }
    if (!bOwnsView) { CancelOverviewTour(); return; }
    // Losing the application ends this run; never resume behind the user's back.
    if (FSlateApplication::IsInitialized() && !FSlateApplication::Get().IsActive())
    {
        CancelOverviewTour();
        return;
    }
    // Detect takeover before applying this frame's pose, even if the input
    // component dispatches later in the frame. UI focus prevents interception.
    if (CanAcceptManualInput()
        && (PC->IsInputKeyDown(EKeys::W) || PC->IsInputKeyDown(EKeys::A)
            || PC->IsInputKeyDown(EKeys::S) || PC->IsInputKeyDown(EKeys::D)
            || PC->IsInputKeyDown(EKeys::Q) || PC->IsInputKeyDown(EKeys::E)))
    {
        CancelOverviewTour();
        return;
    }
    StopManualMovement();
    OverviewTourElapsed = FMath::Min(70.f, OverviewTourElapsed + DeltaSeconds);
    const FTransform Next = OntoTwinOverviewTour::FRoute(OverviewTourAnchor, OverviewTourBounds).Sample(OverviewTourElapsed);
    FHitResult Hit;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(OntoTwinOverviewTour), false, this);
    if (GetWorld()->SweepSingleByChannel(Hit, GetActorLocation(), Next.GetLocation(),
        FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(25.f), Query))
    {
        // A blocked route yields control at the last safe pose; never push through walls.
        CancelOverviewTour();
        return;
    }
    SetActorTransform(Next, false, nullptr, ETeleportType::TeleportPhysics);
    if (OverviewTourElapsed >= 70.f)
    {
        CancelOverviewTour();
        RestoreSceneInputFocus();
    }
}
