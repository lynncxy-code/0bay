#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SlateWrapperTypes.h"
#include "GrandNeoBayExperience.generated.h"

class UCameraComponent;
class UChildActorComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UGrandNeoBayWidget;
class ACampusInteraction;
class AVideoTwinRuntimeFeature;

/** Optional building presentation. The office actors and the existing roaming system stay in place. */
UCLASS(Blueprintable)
class OFFICEHOST_API AGrandNeoBayExperience : public AActor
{
    GENERATED_BODY()
public:
    AGrandNeoBayExperience();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float DeltaSeconds) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="GrandNeoBay")
    TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="GrandNeoBay")
    TObjectPtr<UChildActorComponent> Building;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="GrandNeoBay")
    TObjectPtr<USceneComponent> FloorRoot;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="GrandNeoBay")
    TObjectPtr<UStaticMeshComponent> FloorVolume;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="GrandNeoBay")
    TObjectPtr<UStaticMeshComponent> FloorOutline;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="GrandNeoBay")
    TObjectPtr<UCameraComponent> OverviewCamera;
    /** Runtime video-twin feature, spawned additively in the authored Office_Demo map. */
    UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category="GrandNeoBay|Video Twin")
    TObjectPtr<AVideoTwinRuntimeFeature> VideoTwinRuntime;

    /** In building-local centimetres; the floor follows the placed building actor. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GrandNeoBay|Floor", meta=(MakeEditWidget=true))
    FVector FloorOffset = FVector(0, 0, 3840);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GrandNeoBay|Floor", meta=(ClampMin="50"))
    float FloorHeight = 480;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GrandNeoBay|Floor", meta=(MakeEditWidget=true))
    FVector DrawerOffset = FVector(850, 0, 0);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GrandNeoBay|Floor", meta=(ClampMin="0.1"))
    float DrawerDuration = 0.65f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GrandNeoBay|Floor")
    FText FloorTitle;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GrandNeoBay|Floor")
    FText FloorDescription;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GrandNeoBay|Floor")
    FVector CardAnchor = FVector(3086, -1000, 240);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GrandNeoBay|Preview", meta=(MakeEditWidget=true))
    FVector OverviewTarget = FVector(0, 0, 3100);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GrandNeoBay|Preview", meta=(ClampMin="4000"))
    float OverviewDistance = 22000;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GrandNeoBay|Preview")
    float OverviewYaw = -35;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GrandNeoBay|Preview", meta=(ClampMin="5", ClampMax="75"))
    float OverviewElevation = 22;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GrandNeoBay|Preview", meta=(ClampMin="0.1"))
    float CameraBlendSeconds = 0.8f;
    /** Keep false until office/god-view facade clipping is designed. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GrandNeoBay|Preview")
    bool bShowBuildingInOffice = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GrandNeoBay|Preview")
    bool bStartInOverview = false;

    UFUNCTION(BlueprintCallable, Category="GrandNeoBay")
    void OpenBuilding();
    UFUNCTION(BlueprintCallable, Category="GrandNeoBay")
    void SelectFloor();
    UFUNCTION(BlueprintCallable, Category="GrandNeoBay")
    void ClearSelection();
    UFUNCTION(BlueprintCallable, Category="GrandNeoBay")
    void EnterFloor();
    UFUNCTION(BlueprintCallable, Category="GrandNeoBay")
    void ReturnToOffice();
    UFUNCTION(BlueprintPure, Category="GrandNeoBay")
    bool IsOverviewOpen() const { return bOverview; }
    UFUNCTION(BlueprintPure, Category="GrandNeoBay")
    bool IsFloorSelected() const { return bSelected; }

    bool IsTransitioning() const { return bReturning || TransitionRemaining > 0; }
    bool IsFloorHovered() const { return bHovered; }
    bool HasSelection() const { return bSelected || SelectedBuilding != INDEX_NONE; }
    bool GetSelectedBuildingInfo(FText& Title, FText& Description) const;
    FVector GetCardWorldPosition() const;
    void ClickScene();
    void OrbitPreview(FVector2D Delta);
    void ZoomPreview(float Delta);
    void SetPointerOverUI(bool bOver) { bPointerOverUI = bOver; }
    void SetPreviewPointer(FVector2D Pixels, bool bInViewport) { PreviewPointer = Pixels; bPreviewPointerValid = bInViewport; }

private:
    UPROPERTY(Transient) TObjectPtr<UGrandNeoBayWidget> Widget;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> VolumeMaterial;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> OutlineMaterial;
    TWeakObjectPtr<APlayerController> Controller;
    TWeakObjectPtr<AActor> SavedViewTarget;
    TWeakObjectPtr<APawn> SavedPawn;
    TWeakObjectPtr<ACampusInteraction> Campus;
    int32 SelectedBuilding = INDEX_NONE;
    TMap<TWeakObjectPtr<class UUserWidget>, ESlateVisibility> SavedWidgets;
    TArray<TWeakObjectPtr<UActorComponent>> SuspendedPawnComponents;
    bool bSavedPawnTick = false;
    bool bAddedPawnToHidden = false;
    TArray<TWeakObjectPtr<class UPrimitiveComponent>> AddedHiddenPawnPrimitives;
    bool bSavedCursor = true;
    bool bInputClaimed = false;
    bool bOverview = false;
    bool bSelected = false;
    bool bHovered = false;
    bool bReturning = false;
    bool bPointerOverUI = false;
    bool bPreviewPointerValid = false;
    FVector2D PreviewPointer = FVector2D::ZeroVector;
    float DrawerAlpha = 0;
    float TransitionRemaining = 0;
    float PreviewDistance = 0;
    float PreviewYaw = 0;
    float PreviewElevation = 0;
    double BeginSeconds = 0;
    int32 TestStep = 0;
    bool bSelfTest = false;
    bool bCampusTest = false;
    bool bLandmarkTest = false;
    int32 TestCampusBuilding = INDEX_NONE;
    bool bTestFailed = false;
    TArray<FString> TestChecks;
    FVector TestSavedPawnLocation = FVector::ZeroVector;

    void UpdateCamera();
    void UpdatePresentation();
    void UpdateHoverSelection();
    void SyncMainBuildingHighlight();
    void ReleaseInput();
    void HideOfficeWidgets();
    bool HitFloor(float* Distance = nullptr) const;
    int32 PickBuilding(bool& bFloor) const;
    void TickSelfTest();
    void TickCampusTest();
    void TickLandmarkTest();
    void RecordCheck(bool bPassed, const FString& Name);
};
