#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraActor.h"
#include "GameFramework/Pawn.h"
#include "TwinGodViewPawn.generated.h"

class UCameraComponent;
class UFloatingPawnMovement;
class USceneComponent;

/** 关卡预放置的稳定相机锚点；CameraId 决定开局或漫游上帝视角职责。 */
UCLASS(BlueprintType)
class ONTOTWINSYNC_API ATwinGodViewAnchor : public ACameraActor
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="OntoTwin")
    FString CameraId = TEXT("camera.god.default");

    /** Additional Actor object names or editor labels hidden while this view is active. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="OntoTwin|Camera Visibility")
    TArray<FName> HiddenActorNames;

    /** Additional short streamed-level names hidden while this view is active. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="OntoTwin|Camera Visibility")
    TArray<FName> HiddenLevelNames;
};

/** 与人物解耦的自由观察 Pawn；不提供自动定位人物。 */
UCLASS(BlueprintType)
class ONTOTWINSYNC_API ATwinGodViewPawn : public APawn
{
    GENERATED_BODY()

public:
    ATwinGodViewPawn();
    virtual void Tick(float DeltaSeconds) override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Camera")
    USceneComponent* SceneRoot;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Camera")
    UCameraComponent* Camera;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Movement")
    UFloatingPawnMovement* Movement;

    void Configure(float InMoveSpeedCmS, float InLookSensitivity);
    void MovePlanar(const FVector2D& Input);
    void MoveVertical(float Input);
    void Look(const FVector2D& Input);
    void AdjustSpeed(float Axis);
    void FocusToTransform(const FTransform& TargetTransform, float DurationSeconds);
    void StopManualMovement();
    void StartOverviewTour(const FTransform& Overview, const FBox& FloorBounds);
    void CancelOverviewTour();
    UFUNCTION(BlueprintPure, Category="Camera|Overview Tour")
    bool IsOverviewTourActive() const { return bOverviewTourActive; }
    UFUNCTION(BlueprintPure, Category="Camera|Overview Tour")
    float GetOverviewTourElapsed() const { return OverviewTourElapsed; }

private:
    bool CanAcceptManualInput() const;
    FDelegateHandle FocusChangingHandle;
    FDelegateHandle ApplicationActivationHandle;
    void ReleaseManualInput();
    void RestoreSceneInputFocus();
    bool bOverviewTourActive = false;
    bool bOverviewTourStarted = false;
    float OverviewTourElapsed = 0.f;
    float OverviewTourWaitElapsed = 0.f;
    FTransform OverviewTourAnchor;
    FBox OverviewTourBounds = FBox(ForceInit);
    void TickOverviewTour(float DeltaSeconds);
    float LookSensitivity = 1.0f;
    bool bFocusTransitionActive = false;
    float FocusTransitionElapsed = 0.0f;
    float FocusTransitionDuration = 0.0f;
    FTransform FocusTransitionStart;
    FTransform FocusTransitionTarget;
};
