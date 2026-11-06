#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "SceneInteraction/TwinRoamingTypes.h"
#include "TwinRoamingCharacter.generated.h"

class UCameraComponent;
class UAnimationAsset;
class USkeletalMeshComponent;
class USpringArmComponent;
class UTwinCharacterAsset;
class UTwinCameraModeComponent;
class UTwinRouteFollowerComponent;
class UTwinSkinComponent;

/** 普通观察者，不是 OntoTwin 业务实例。 */
UCLASS(BlueprintType)
class ONTOTWINSYNC_API ATwinRoamingCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    ATwinRoamingCharacter();
    virtual void Tick(float DeltaSeconds) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Roaming")
    USpringArmComponent* NearCameraArm;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Roaming")
    UCameraComponent* NearCamera;

    /** Hidden Manny/Quinn source pose for project-side runtime retargeting. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Roaming|Animation")
    USkeletalMeshComponent* AnimationSourceMesh;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Roaming")
    UTwinSkinComponent* SkinComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Roaming")
    UTwinRouteFollowerComponent* RouteFollower;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Roaming")
    UTwinCameraModeComponent* CameraMode;

    bool ApplyCharacterAsset(UTwinCharacterAsset* Asset, FString& OutError);
    void ApplyMovementSettings(const FTwinRoamingMovementSettings& Settings);
    void ApplyNearCameraSettings(const FTwinNearCameraSettings& Settings);
    void ConfigurePersonCameras(
        const FTwinFirstPersonCameraSettings& FirstPersonSettings,
        const FTwinNearCameraSettings& NearSettings);
    void ActivatePersonCamera(ETwinRoamingCameraMode Mode, float BlendDurationSeconds);
    void SetFirstPersonPresentation(bool bEnabled);
    bool IsPersonCameraTransitioning() const { return bPersonCameraTransitioning; }
    void MoveRelativeToView(const FVector2D& Input, bool bSprint);
    void Look(const FVector2D& Input, float Sensitivity);
    void SetAutoRouteCameraSmoothing(bool bEnabled);
    bool SetAutoRouteAnimation(bool bActive, float SpeedCmS = 0.0f);
    /** Re-assert the current character's animation after a skin mesh swap. */
    void RefreshLocomotionAnimation();

private:
    UPROPERTY(Transient)
    UAnimationAsset* AutoRouteAnimation = nullptr;

    UPROPERTY(Transient)
    UAnimationAsset* DirectIdleAnimation = nullptr;

    UPROPERTY(Transient)
    UAnimationAsset* DirectWalkAnimation = nullptr;

    UPROPERTY(Transient)
    UAnimationAsset* ActiveDirectAnimation = nullptr;

    UPROPERTY(Transient)
    TSubclassOf<UAnimInstance> VisibleAnimClass;

    UPROPERTY(Transient)
    TSubclassOf<UAnimInstance> AnimationSourceAnimClass;

    float AutoRouteAnimationReferenceSpeedCmS = 180.0f;
    bool bAutoRouteAnimationActive = false;
    bool bVisibleAutoRouteAnimationActive = false;
    bool bDirectLocomotion = false;
    float WalkSpeedCmS = 250.0f;
    float SprintSpeedCmS = 500.0f;
    FTwinFirstPersonCameraSettings FirstPersonCameraSettings;
    FTwinNearCameraSettings NearCameraSettings;
    ETwinRoamingCameraMode TargetPersonCameraMode = ETwinRoamingCameraMode::NearFollow;
    bool bPersonCameraTransitioning = false;
    float PersonCameraTransitionElapsed = 0.0f;
    float PersonCameraTransitionDuration = 0.0f;
    float PersonCameraStartArmLength = 120.0f;
    float PersonCameraTargetArmLength = 120.0f;
    float PersonCameraStartHeight = 35.0f;
    float PersonCameraTargetHeight = 35.0f;
    float PersonCameraStartFov = 90.0f;
    float PersonCameraTargetFov = 90.0f;

    void UpdateDirectLocomotionAnimation();
    void ApplyDirectAnimation(UAnimationAsset* Animation);
};
