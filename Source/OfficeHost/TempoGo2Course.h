#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TempoGo2Course.generated.h"
class UControlRigComponent;
class USkeletalMeshComponent;
class USplineComponent;
class UCameraComponent;
DECLARE_MULTICAST_DELEGATE_OneParam(FTempoGo2StopSettled, int32);
DECLARE_DELEGATE_RetVal(bool, FTempoGo2StopWorkPending);

USTRUCT(BlueprintType)
struct FTempoGo2Leg
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Course",meta=(ClampMin="0",ClampMax="1")) float EndFraction=1.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Course",meta=(ClampMin="10",ClampMax="150",Units="cm/s")) float Speed=60.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Course",meta=(ClampMin="0",Units="s")) float HoldSeconds=3.f;
};

/** Drives the real TempoQuadruped Control Rig with a moving target. No baked walking clips. */
UCLASS(BlueprintType)
class OFFICEHOST_API ATempoGo2Course : public AActor
{
    GENERATED_BODY()
public:
    ATempoGo2Course();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Tempo Go2") TObjectPtr<USplineComponent> Route;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Tempo Go2") TObjectPtr<USkeletalMeshComponent> RobotMesh;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Tempo Go2") TObjectPtr<UControlRigComponent> ProceduralRig;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Tempo Go2") TObjectPtr<UCameraComponent> PreviewCamera;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Tempo Go2|Preview") bool bUsePreviewCamera=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Tempo Go2|Preview") bool bFollowCamera=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Tempo Go2|Course") TArray<FTempoGo2Leg> Legs;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Tempo Go2|Course") bool bAutoStart=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Tempo Go2|Course") bool bRepeat=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Tempo Go2|Preview") bool bShowRoute=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Tempo Go2|Preview") bool bShowStatus=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Tempo Go2|Course",meta=(ClampMin="10")) float AccelerationCm=60.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Tempo Go2|Course",meta=(ClampMin="10")) float BrakingCm=60.f;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Tempo Go2|Status") FString State=TEXT("Ready");
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Tempo Go2|Status") bool bRunning=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Tempo Go2|Status") bool bPaused=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Tempo Go2|Status") int32 CurrentLeg=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Tempo Go2|Status") int32 CompletedStops=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Tempo Go2|Status") int32 RigSolveCount=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Tempo Go2|Status") float DistanceCm=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Tempo Go2|Status") float TargetSpeedCm=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Tempo Go2|Status") float ActualBodySpeedCm=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Tempo Go2|Status") FVector BodyWorldPosition=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Tempo Go2|Status") TArray<FVector> FeetWorld;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Tempo Go2|Status") float TargetYawDegrees=0;
    UFUNCTION(BlueprintCallable,CallInEditor,Category="Tempo Go2") bool StartCourse();
    UFUNCTION(BlueprintCallable,CallInEditor,Category="Tempo Go2") void ResetCourse();
    UFUNCTION(BlueprintCallable,CallInEditor,Category="Tempo Go2") void TogglePause();
    UFUNCTION(BlueprintCallable,Category="Tempo Go2") void AdvanceCourse(float DeltaSeconds);
    FTempoGo2StopSettled OnStopSettled;
    FTempoGo2StopWorkPending StopWorkPending;
private:
    UFUNCTION() void MapMesh(UControlRigComponent* Component);
    UFUNCTION() void SetRigTarget(UControlRigComponent* Component);
    void Solve(float DeltaSeconds);
    void Step(float DeltaSeconds);
    void DrawCourse();
    void UpdatePreviewCamera();
    bool Validate(FString& Error) const;
    float HoldTime=0;
    float SettledTime=0;
    bool bStopEventSent=false;
    bool bInitialized=false;
    FTransform TargetTransform=FTransform::Identity;
};
