#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Go2DeviceScanner.generated.h"

class ATempoGo2Course;
class UNiagaraComponent;
class UPostProcessComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UMaterialParameterCollection;
class UMeshComponent;
class UCameraComponent;
class UOntoTwinWebInteractionComponent;

/** Combines the imported ScanGrid environment sweep with ScanningSystem device highlights. */
UCLASS(BlueprintType)
class OFFICEHOST_API AGo2DeviceScanner : public AActor
{
    GENERATED_BODY()
public:
    AGo2DeviceScanner();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UPROPERTY(EditInstanceOnly,BlueprintReadWrite,Category="Go2 Scan") TObjectPtr<ATempoGo2Course> Robot;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Go2 Scan") bool bAutoScan=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Go2 Scan",meta=(ClampMin="0",ClampMax="1")) float ScanIntensity=.18f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Go2 Scan") bool bUseDemoCamera=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Go2 Scan",meta=(ClampMin="100",ClampMax="3000",Units="cm")) float ScanRadius=850.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Go2 Scan",meta=(ClampMin="1",Units="s")) float ScanDuration=4.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Go2 Scan",meta=(ClampMin="1",Units="s")) float ScanInterval=9.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Go2 Scan",meta=(ClampMin="1",Units="s")) float HighlightDuration=8.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Go2 Scan") TObjectPtr<UMaterialInterface> ScanMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Go2 Scan") TObjectPtr<UMaterialInterface> HighlightMaterial;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Go2 Scan") TObjectPtr<UNiagaraComponent> ScanGrid;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Go2 Scan") TObjectPtr<UPostProcessComponent> ScanPostProcess;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Go2 Scan") TObjectPtr<UCameraComponent> DemoCamera;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Go2 Scan|Status") bool bScanning=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Go2 Scan|Status") int32 DetectedCount=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Go2 Scan|Status") int32 TargetCount=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Go2 Scan|Status") int32 ScanCount=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Go2 Scan|Status") float CurrentRadius=0;
    UFUNCTION(BlueprintCallable,Category="Go2 Scan") void StartScan();
    UFUNCTION(BlueprintCallable,Category="Go2 Scan") void ToggleFloorScan();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Go2 Scan|Status") bool bFloorGridActive = false;
    UFUNCTION(BlueprintCallable,Category="Go2 Scan") void SetFollowCamera(bool bEnabled, bool bRestoreView=true);
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Go2 Scan|Status") bool bSecurityIntroActive=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Go2 Scan|Status") int32 StopScanRequests=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Go2 Scan") float SecurityIntroSeconds=10.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Go2 Scan") float SecurityScanDelaySeconds=3.f;
private:
    TWeakObjectPtr<UOntoTwinWebInteractionComponent> BusinessWeb;
    TWeakObjectPtr<AActor> PreviousCamera;
    FTimerHandle BusinessIntroTimer;
    FTimerHandle BusinessScanTimer;
    int32 PendingScans=0;
    bool bIntroWaitingForCamera=false;
    void StartIntroWhenCameraReady();
    void BindBusinessEntry();
    bool BeginBusinessIntro(const FString& BusinessId);
    void CancelBusinessIntro();
    void FinishBusinessIntro();
    void StopSecurityEffects();
    void OnRouteStop(int32 StopIndex);
    void RequestAutomaticScan();
    void UpdateFollowCamera();
    UPROPERTY() TObjectPtr<UMaterialParameterCollection> ScanCollection;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> ScanMID;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> HighlightMID;
    struct FTarget {
        TWeakObjectPtr<UMeshComponent> Mesh;
        TWeakObjectPtr<UMaterialInterface> OriginalOverlay;
        float Expiry=0;
        bool bHighlighted=false;
        bool bHeldByFloor=false;
        int32 LastScan=0;
    };
    TArray<FTarget> Targets;
    float Age=0, ScanAge=0, NextScan=2.f, RefreshAge=0;
    UPROPERTY() TObjectPtr<UMaterialInterface> FloorScanMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> FloorScanMID;
    FVector FloorOrigin = FVector::ZeroVector;
    float FloorSweepRadius = 0.f;
    float FloorSweepAge = 0.f;
    bool bManualDiscovery = false;
    void ConfigureScanParticles(bool bUseOriginal);
    void UpdateFloorSweep(float DeltaSeconds);
    void RefreshTargets();
    void RestoreHighlights();
};

