#include "SceneInteraction/TwinCameraModeComponent.h"

#include "SceneInteraction/TwinCameraVisibility.h"
#include "SceneInteraction/TwinGodViewPawn.h"
#include "SceneInteraction/TwinRoamingCharacter.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

namespace
{
constexpr float PersonCameraBlendSeconds = 0.35f;
constexpr float GlobalCameraBlendSeconds = 0.50f;
}

UTwinCameraModeComponent::UTwinCameraModeComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UTwinCameraModeComponent::Configure(
    const FTwinFirstPersonCameraSettings& InFirstPerson,
    const FTwinNearCameraSettings& InNear,
    const FTwinGodCameraSettings& InGod)
{
    FirstPersonSettings = InFirstPerson;
    NearSettings = InNear;
    GodSettings = InGod;
    if (ATwinRoamingCharacter* Character = Cast<ATwinRoamingCharacter>(GetOwner()))
    {
        Character->ConfigurePersonCameras(FirstPersonSettings, NearSettings);
        if (Mode == ETwinRoamingCameraMode::God)
        {
            Character->SetFirstPersonPresentation(false);
        }
    }
    if (GodPawn)
    {
        GodPawn->Configure(GodSettings.MoveSpeedCmS, GodSettings.LookSensitivity);
    }
}

void UTwinCameraModeComponent::ApplyGodViewVisibility(
    APlayerController* PlayerController,
    ATwinGodViewAnchor* Anchor)
{
    TArray<AActor*> HiddenActors;
    const TArray<FName> EmptyNames;
    OntoTwinCameraVisibility::ResolveHiddenActors(
        GetWorld(),
        ETwinCameraVisibilityProfile::GodView,
        Anchor ? Anchor->HiddenActorNames : EmptyNames,
        Anchor ? Anchor->HiddenLevelNames : EmptyNames,
        HiddenActors);
    OntoTwinCameraVisibility::ApplyToPlayer(
        PlayerController, HiddenActors, GodViewVisibilityState);
    UE_LOG(LogTemp, Log,
        TEXT("OntoTwin god-view visibility applied: hidden_actors=%d hidden_primitives=%d suppressed_lights=%d"),
        GodViewVisibilityState.AddedPlayerHiddenActors.Num(),
        GodViewVisibilityState.AddedPlayerHiddenPrimitiveComponents.Num(),
        GodViewVisibilityState.SuppressedLights.Num());
}

void UTwinCameraModeComponent::ClearGodViewVisibility(
    APlayerController* PlayerController)
{
    OntoTwinCameraVisibility::ClearFromPlayer(
        PlayerController, GodViewVisibilityState);
}

bool UTwinCameraModeComponent::EnsureGodPawn(
    ATwinGodViewAnchor* StartAnchor,
    FString& OutError)
{
    if (GodPawn && IsValid(GodPawn)) return true;
    if (!GetWorld())
    {
        OutError = TEXT("World is unavailable");
        return false;
    }
    if (!StartAnchor)
    {
        OutError = TEXT("Configured global-view CameraActor was not found");
        return false;
    }

    FActorSpawnParameters Params;
    Params.Owner = GetOwner();
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    GodPawn = GetWorld()->SpawnActor<ATwinGodViewPawn>(
        ATwinGodViewPawn::StaticClass(), StartAnchor->GetActorTransform(), Params);
    if (!GodPawn)
    {
        OutError = TEXT("Global-view Pawn could not be spawned");
        return false;
    }
    GodPawn->Configure(GodSettings.MoveSpeedCmS, GodSettings.LookSensitivity);
    return true;
}

void UTwinCameraModeComponent::ApplyPersonControlRotation(
    APlayerController* PlayerController,
    ETwinRoamingCameraMode PersonMode)
{
    const ATwinRoamingCharacter* Character = Cast<ATwinRoamingCharacter>(GetOwner());
    if (!PlayerController || !Character) return;
    PlayerController->SetControlRotation(FRotator(
        PersonMode == ETwinRoamingCameraMode::FirstPerson ? 0.0f : -10.0f,
        Character->GetActorRotation().Yaw,
        0.0f));
}

bool UTwinCameraModeComponent::ActivateMode(
    ETwinRoamingCameraMode NewMode,
    APlayerController* PlayerController,
    ATwinGodViewAnchor* StartAnchor,
    FString& OutError,
    bool bInstant)
{
    ATwinRoamingCharacter* Character = Cast<ATwinRoamingCharacter>(GetOwner());
    if (!PlayerController || !Character || !GetWorld())
    {
        OutError = TEXT("Player controller or roaming character is unavailable");
        return false;
    }
    if (IsTransitioning())
    {
        OutError = TEXT("Camera transition is already in progress");
        return false;
    }
    if (NewMode == Mode
        && ((NewMode == ETwinRoamingCameraMode::God && PlayerController->GetPawn() == GodPawn)
            || (NewMode != ETwinRoamingCameraMode::God && PlayerController->GetPawn() == Character)))
    {
        return true;
    }

    if (NewMode == ETwinRoamingCameraMode::God && !EnsureGodPawn(StartAnchor, OutError))
    {
        return false;
    }
    if (NewMode == ETwinRoamingCameraMode::God)
    {
        ApplyGodViewVisibility(PlayerController, StartAnchor);
    }

    const ETwinRoamingCameraMode PreviousMode = Mode;
    PendingMode = NewMode;
    Mode = NewMode;

    if (bInstant)
    {
        GetWorld()->GetTimerManager().ClearTimer(TransitionTimer);
        bViewTargetTransitioning = false;
        TransitionController = nullptr;
        if (NewMode == ETwinRoamingCameraMode::God)
        {
            Character->SetFirstPersonPresentation(false);
            PlayerController->Possess(GodPawn);
        }
        else
        {
            Character->ActivatePersonCamera(NewMode, 0.0f);
            PlayerController->Possess(Character);
            ApplyPersonControlRotation(PlayerController, NewMode);
            ClearGodViewVisibility(PlayerController);
        }
        return true;
    }

    if (PreviousMode != ETwinRoamingCameraMode::God
        && NewMode != ETwinRoamingCameraMode::God)
    {
        Character->ActivatePersonCamera(NewMode, PersonCameraBlendSeconds);
        ClearGodViewVisibility(PlayerController);
        return true;
    }

    bViewTargetTransitioning = true;
    TransitionController = PlayerController;
    if (NewMode == ETwinRoamingCameraMode::God)
    {
        Character->SetFirstPersonPresentation(false);
        PlayerController->SetViewTargetWithBlend(
            GodPawn,
            GlobalCameraBlendSeconds,
            EViewTargetBlendFunction::VTBlend_Cubic,
            2.0f,
            true);
    }
    else
    {
        Character->ActivatePersonCamera(NewMode, 0.0f);
        PlayerController->SetViewTargetWithBlend(
            Character,
            GlobalCameraBlendSeconds,
            EViewTargetBlendFunction::VTBlend_Cubic,
            2.0f,
            true);
    }
    GetWorld()->GetTimerManager().SetTimer(
        TransitionTimer,
        this,
        &UTwinCameraModeComponent::FinishViewTargetTransition,
        GlobalCameraBlendSeconds,
        false);
    return true;
}

void UTwinCameraModeComponent::FinishViewTargetTransition()
{
    APlayerController* PlayerController = TransitionController;
    ATwinRoamingCharacter* Character = Cast<ATwinRoamingCharacter>(GetOwner());
    if (PlayerController && Character)
    {
        if (PendingMode == ETwinRoamingCameraMode::God && GodPawn)
        {
            PlayerController->Possess(GodPawn);
        }
        else
        {
            PlayerController->Possess(Character);
            ApplyPersonControlRotation(PlayerController, PendingMode);
            ClearGodViewVisibility(PlayerController);
        }
    }
    bViewTargetTransitioning = false;
    TransitionController = nullptr;
}

bool UTwinCameraModeComponent::ActivateNear(APlayerController* PlayerController, bool bInstant)
{
    FString Error;
    return ActivateMode(
        ETwinRoamingCameraMode::NearFollow, PlayerController, nullptr, Error, bInstant);
}

bool UTwinCameraModeComponent::ActivateFirstPerson(
    APlayerController* PlayerController,
    bool bInstant)
{
    FString Error;
    return ActivateMode(
        ETwinRoamingCameraMode::FirstPerson, PlayerController, nullptr, Error, bInstant);
}

bool UTwinCameraModeComponent::ActivateGod(
    APlayerController* PlayerController,
    ATwinGodViewAnchor* StartAnchor,
    FString& OutError,
    bool bInstant)
{
    return ActivateMode(
        ETwinRoamingCameraMode::God, PlayerController, StartAnchor, OutError, bInstant);
}

bool UTwinCameraModeComponent::Cycle(
    APlayerController* PlayerController,
    ATwinGodViewAnchor* StartAnchor,
    FString& OutError)
{
    ETwinRoamingCameraMode NextMode = ETwinRoamingCameraMode::NearFollow;
    if (Mode == ETwinRoamingCameraMode::God)
    {
        NextMode = ETwinRoamingCameraMode::NearFollow;
    }
    else if (Mode == ETwinRoamingCameraMode::NearFollow)
    {
        NextMode = ETwinRoamingCameraMode::FirstPerson;
    }
    else if (IsValid(StartAnchor))
    {
        NextMode = ETwinRoamingCameraMode::God;
    }
    // A level without TwinGodViewAnchor still has a complete two-mode camera
    // loop. Explicitly selecting God view continues to report the missing anchor.
    return ActivateMode(NextMode, PlayerController, StartAnchor, OutError, false);
}

bool UTwinCameraModeComponent::FocusAtTransform(
    APlayerController* PlayerController,
    ATwinGodViewAnchor* VisibilityAnchor,
    const FTransform& TargetTransform,
    float FovDegrees,
    FString& OutError)
{
    if (!PlayerController || !GetWorld())
    {
        OutError = TEXT("Player controller or world is unavailable");
        return false;
    }
    if (IsTransitioning())
    {
        OutError = TEXT("Camera transition is already in progress");
        return false;
    }

    if (!GodPawn || !IsValid(GodPawn))
    {
        FActorSpawnParameters Params;
        Params.Owner = GetOwner();
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        GodPawn = GetWorld()->SpawnActor<ATwinGodViewPawn>(
            ATwinGodViewPawn::StaticClass(), TargetTransform, Params);
        if (!GodPawn)
        {
            OutError = TEXT("Instance-focus observation Pawn could not be spawned");
            return false;
        }
        GodPawn->Configure(GodSettings.MoveSpeedCmS, GodSettings.LookSensitivity);
    }
    if (GodPawn->Camera)
    {
        GodPawn->Camera->SetFieldOfView(FMath::Clamp(FovDegrees, 20.0f, 120.0f));
    }

    const bool bAlreadyObserving = Mode == ETwinRoamingCameraMode::God
        && PlayerController->GetPawn() == GodPawn;
    if (bAlreadyObserving)
    {
        ApplyGodViewVisibility(PlayerController, VisibilityAnchor);
        GodPawn->FocusToTransform(TargetTransform, GlobalCameraBlendSeconds);
        return true;
    }

    GodPawn->FocusToTransform(TargetTransform, 0.0f);
    return ActivateMode(
        ETwinRoamingCameraMode::God,
        PlayerController,
        VisibilityAnchor,
        OutError,
        false);
}

bool UTwinCameraModeComponent::IsTransitioning() const
{
    const ATwinRoamingCharacter* Character = Cast<ATwinRoamingCharacter>(GetOwner());
    return bViewTargetTransitioning
        || (Character && Character->IsPersonCameraTransitioning());
}

void UTwinCameraModeComponent::Shutdown(APlayerController* PlayerController)
{
    if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(TransitionTimer);
    ClearGodViewVisibility(PlayerController);
    bViewTargetTransitioning = false;
    TransitionController = nullptr;
    if (ATwinRoamingCharacter* Character = Cast<ATwinRoamingCharacter>(GetOwner()))
    {
        Character->SetFirstPersonPresentation(false);
    }
    if (PlayerController && PlayerController->GetPawn() == GodPawn)
    {
        PlayerController->UnPossess();
    }
    if (GodPawn && IsValid(GodPawn))
    {
        GodPawn->Destroy();
    }
    GodPawn = nullptr;
    PendingMode = ETwinRoamingCameraMode::NearFollow;
    Mode = ETwinRoamingCameraMode::NearFollow;
}
