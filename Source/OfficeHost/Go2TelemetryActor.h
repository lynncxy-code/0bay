#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Go2TelemetryActor.generated.h"

class UPoseableMeshComponent;
class UTextRenderComponent;
class FSocket;

/** Receive-only visualization. The robot DDS subscriber is a separate local Python process. */
UCLASS(BlueprintType)
class OFFICEHOST_API AGo2TelemetryActor : public AActor
{
    GENERATED_BODY()
public:
    AGo2TelemetryActor();
    virtual ~AGo2TelemetryActor();
    virtual void Tick(float DeltaSeconds) override;
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual bool ShouldTickIfViewportsOnly() const override { return true; }

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Go2") TObjectPtr<UPoseableMeshComponent> RobotMesh;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Go2") TObjectPtr<UTextRenderComponent> StatusLabel;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Go2|Connection") bool bReceiveEnabled = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Go2|Connection", meta=(ClampMin="1024",ClampMax="65535")) int32 ListenPort = 17702;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Go2|Connection") FString ExpectedRobotIP = TEXT("192.168.123.18");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Go2|Connection") bool bAllowSyntheticData = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Go2|Pose") bool bFollowBodyOrientation = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Go2|Pose") bool bFollowOdometry = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Go2|Pose") bool bGroundFeetWithoutOdometry = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Go2|Pose", meta=(ClampMin="0",ClampMax="0.5")) float SmoothingSeconds = 0.04f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Go2|Connection", meta=(ClampMin="0.1")) float StaleAfterSeconds = 0.5f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Go2|Status") FString ConnectionStatus = TEXT("Waiting for telemetry");
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Go2|Status") float DataAgeSeconds = -1.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Go2|Status") bool bLiveDataFresh = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Go2|Status") int32 ReportedMode = -1;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Go2|Status") int32 ReportedGait = -1;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Go2|Status") int32 AcceptedFrames = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Go2|Status") TArray<float> JointAnglesRadians;
    UFUNCTION(BlueprintCallable, Category="Go2") bool ApplyTelemetryJson(const FString& Json);
    /** Also usable from an editor Slate callback when the viewport world is not ticking. */
    UFUNCTION(BlueprintCallable, Category="Go2") void PollTelemetry(float DeltaSeconds);
    UFUNCTION(BlueprintCallable, CallInEditor, Category="Go2") void RecenterOdometry();
    UFUNCTION(BlueprintImplementableEvent, Category="Go2") void OnReportedStateChanged(int32 NewMode, int32 NewGait);

private:
    void OpenSocket();
    void CloseSocket();
    void ApplyPose();
    void UpdateStatus();
    FSocket* Socket = nullptr;
    int32 BoundPort = 0;
    double LastOpenAttempt = -10.0;
    double LastReceive = -1.0;
    double LastSequence = -1.0;
    FString Session;
    FString Source;
    TArray<float> TargetAngles;
    FQuat BodyRotation = FQuat::Identity;
    FVector BodyPosition = FVector::ZeroVector;
    FVector InitialPosition = FVector::ZeroVector;
    float InitialYaw = 0.f;
    bool bHaveOrigin = false;
    bool bHavePosition = false;
    bool bHavePositionOrigin = false;
};
