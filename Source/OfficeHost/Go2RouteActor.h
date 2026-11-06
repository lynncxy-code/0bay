#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Go2RouteActor.generated.h"

class UPoseableMeshComponent;
class USplineComponent;

UENUM(BlueprintType)
enum class EGo2RouteState : uint8 { Crouched, StandingUp, Walking, SettlingFeet, StandingPause, LyingDown, Complete };

/** Offline, flat-floor visualization on the official Go2 skeleton. Never connects to hardware. */
UCLASS(BlueprintType)
class OFFICEHOST_API AGo2RouteActor : public AActor
{
    GENERATED_BODY()
public:
    AGo2RouteActor();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Go2") TObjectPtr<USplineComponent> Route;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Go2") TObjectPtr<UPoseableMeshComponent> RobotMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Go2|Sequence") bool bAutoStart = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Go2|Sequence", meta=(ClampMin="10",ClampMax="50",Units="cm/s")) float SpeedCmPerSecond = 35.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Go2|Sequence", meta=(ClampMin="0.5",Units="s")) float StandUpSeconds = 3.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Go2|Sequence", meta=(ClampMin="0.5",Units="s")) float LieDownSeconds = 3.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Go2|Sequence", meta=(ClampMin="0",Units="s")) float PauseBeforeLieDownSeconds = 1.f;
    /** Loop requires a closed spline, so replay never teleports between different endpoints. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Go2|Sequence") bool bLoop = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Go2|Sequence", meta=(ClampMin="0",Units="s")) float LoopDelaySeconds = 2.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Go2|Status") EGo2RouteState State = EGo2RouteState::Crouched;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Go2|Status") bool bRunning = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Go2|Status") bool bPaused = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Go2|Status") float DistanceCm = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Go2|Status") float CurrentSpeedCmPerSecond = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Go2|Status") FString Status = TEXT("Offline / crouched");
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Go2|Status") TArray<float> JointAnglesRadians;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Go2|Status") TArray<FVector> FootTargetsWorld;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Go2|Status") TArray<bool> FeetInSwing;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Go2|Status") float MaxFootErrorCm = 0.f;
    UFUNCTION(BlueprintCallable, CallInEditor, Category="Go2") bool StartRoute();
    UFUNCTION(BlueprintCallable, CallInEditor, Category="Go2") void ResetRoute();
    UFUNCTION(BlueprintCallable, CallInEditor, Category="Go2") void TogglePause();
    /** Runtime Tick and editor preview use the same deterministic simulation entry point. */
    UFUNCTION(BlueprintCallable, Category="Go2") void AdvanceSimulation(float DeltaSeconds);
    UFUNCTION(BlueprintImplementableEvent, Category="Go2") void OnRouteStateChanged(EGo2RouteState NewState);
private:
    void Step(float DeltaSeconds);
    void EnterState(EGo2RouteState NewState);
    void SetBodyAtDistance(float Distance);
    void ApplyJoints(bool bGroundPose);
    void SolveFeet();
    void BeginSwing(int32 Pair, float TargetDistance);
    void UpdateSwing(float Alpha);
    FVector NominalFoot(int32 Leg, float AtDistance) const;
    bool ValidateRoute(FString& Error) const;
    float StateSeconds = 0.f;
    float SwingSeconds = 0.f;
    float BodyHeight = 37.2f;
    int32 StepNumber = -1;
    int32 SwingPair = -1;
    FVector GroundPosition = FVector::ZeroVector;
    FQuat BodyHeading = FQuat::Identity;
    TArray<FVector> SwingStarts;
    TArray<FVector> SwingEnds;
};
