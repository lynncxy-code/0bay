#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OntoTwinRuntimeGizmo.generated.h"

class UPrimitiveComponent;
class USceneComponent;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

UENUM(BlueprintType)
enum class EOntoTwinRuntimeGizmoPart : uint8
{
    None,
    MoveX,
    MoveY,
    MoveZ,
    MoveXY,
    RotateYaw
};

UENUM(BlueprintType)
enum class EOntoTwinRuntimeSnapState : uint8
{
    None,
    Grid,
    Wall
};

UCLASS(ClassGroup=(DigitalTwin), meta=(DisplayName="OntoTwin Runtime Gizmo"))
class ONTOTWINSYNC_API AOntoTwinRuntimeGizmo : public AActor
{
    GENERATED_BODY()

public:
    AOntoTwinRuntimeGizmo();

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

public:
    void UpdateForTarget(AActor* InTargetActor, const FBox& LocalBounds, const FVector& EditPivotLocal);
    void UpdateForSelection(
        const TArray<AActor*>& InTargetActors,
        const TArray<FBox>& LocalBounds,
        const FBox& GroupWorldBounds,
        const FVector& GroupPivotWorld);
    void SetGizmoEnabled(bool bEnabled);
    bool IsGizmoEnabled() const { return bGizmoEnabled; }
    void SetInteractionState(EOntoTwinRuntimeGizmoPart HoverPart, EOntoTwinRuntimeGizmoPart ActivePart);
    void SetSnapFeedback(EOntoTwinRuntimeSnapState SnapState, const FVector& WorldPoint);
    EOntoTwinRuntimeGizmoPart GetPartForComponent(const UPrimitiveComponent* Component) const;
    float GetMoveInteractionPlaneZ() const;
    float GetRotateInteractionPlaneZ() const;

private:
    UPROPERTY(VisibleAnywhere, Category="Runtime Editor")
    USceneComponent* GizmoRoot = nullptr;

    UPROPERTY(VisibleAnywhere, Category="Runtime Editor")
    UStaticMeshComponent* XAxisHit = nullptr;

    UPROPERTY(VisibleAnywhere, Category="Runtime Editor")
    UStaticMeshComponent* YAxisHit = nullptr;

    UPROPERTY(VisibleAnywhere, Category="Runtime Editor")
    UStaticMeshComponent* ZAxisHit = nullptr;

    UPROPERTY(VisibleAnywhere, Category="Runtime Editor")
    UStaticMeshComponent* MoveXYHit = nullptr;

    UPROPERTY(VisibleAnywhere, Category="Runtime Editor")
    UStaticMeshComponent* XAxisShaftVisual = nullptr;

    UPROPERTY(VisibleAnywhere, Category="Runtime Editor")
    UStaticMeshComponent* YAxisShaftVisual = nullptr;

    UPROPERTY(VisibleAnywhere, Category="Runtime Editor")
    UStaticMeshComponent* ZAxisShaftVisual = nullptr;

    UPROPERTY(VisibleAnywhere, Category="Runtime Editor")
    UStaticMeshComponent* XAxisArrowVisual = nullptr;

    UPROPERTY(VisibleAnywhere, Category="Runtime Editor")
    UStaticMeshComponent* YAxisArrowVisual = nullptr;

    UPROPERTY(VisibleAnywhere, Category="Runtime Editor")
    UStaticMeshComponent* ZAxisArrowVisual = nullptr;

    UPROPERTY(VisibleAnywhere, Category="Runtime Editor")
    UStaticMeshComponent* HubOuterVisual = nullptr;

    UPROPERTY(VisibleAnywhere, Category="Runtime Editor")
    UStaticMeshComponent* HubCoreVisual = nullptr;

    UPROPERTY(VisibleAnywhere, Category="Runtime Editor")
    UStaticMeshComponent* RotateHitHandle = nullptr;

    UPROPERTY(VisibleAnywhere, Category="Runtime Editor")
    TArray<UStaticMeshComponent*> RotateHitSegments;

    UPROPERTY(VisibleAnywhere, Category="Runtime Editor")
    TArray<UStaticMeshComponent*> RotateRingVisuals;

    UPROPERTY(VisibleAnywhere, Category="Runtime Editor")
    UStaticMeshComponent* RotateHandleVisual = nullptr;

    UPROPERTY(VisibleAnywhere, Category="Runtime Editor")
    TArray<UStaticMeshComponent*> BoundsEdgeVisuals;

    UPROPERTY(VisibleAnywhere, Category="Runtime Editor")
    TArray<UStaticMeshComponent*> BoundsCornerVisuals;

    UPROPERTY(VisibleAnywhere, Category="Runtime Editor")
    UInstancedStaticMeshComponent* IndividualBoundsCorners = nullptr;

    UPROPERTY(VisibleAnywhere, Category="Runtime Editor")
    UStaticMeshComponent* SnapMarker = nullptr;

    UPROPERTY()
    TWeakObjectPtr<AActor> TargetActor;

    UPROPERTY()
    UMaterialInterface* BaseGizmoMaterial = nullptr;

    UPROPERTY(Transient)
    UMaterialInstanceDynamic* XMaterial = nullptr;

    UPROPERTY(Transient)
    UMaterialInstanceDynamic* YMaterial = nullptr;

    UPROPERTY(Transient)
    UMaterialInstanceDynamic* ZMaterial = nullptr;

    UPROPERTY(Transient)
    UMaterialInstanceDynamic* HubOuterMaterial = nullptr;

    UPROPERTY(Transient)
    UMaterialInstanceDynamic* HubCoreMaterial = nullptr;

    UPROPERTY(Transient)
    UMaterialInstanceDynamic* RotateMaterial = nullptr;

    UPROPERTY(Transient)
    UMaterialInstanceDynamic* BoundsMaterial = nullptr;

    UPROPERTY(Transient)
    UMaterialInstanceDynamic* IndividualBoundsMaterial = nullptr;

    UPROPERTY(Transient)
    UMaterialInstanceDynamic* SnapMaterial = nullptr;

    bool bGizmoEnabled = false;
    EOntoTwinRuntimeGizmoPart CurrentHoverPart = EOntoTwinRuntimeGizmoPart::None;
    EOntoTwinRuntimeGizmoPart CurrentActivePart = EOntoTwinRuntimeGizmoPart::None;
    EOntoTwinRuntimeSnapState CurrentSnapState = EOntoTwinRuntimeSnapState::None;
    FBox TargetLocalBounds = FBox(ForceInit);
    FVector TargetEditPivotLocal = FVector::ZeroVector;
    TArray<TWeakObjectPtr<AActor>> SelectionTargets;
    TArray<FBox> SelectionLocalBounds;
    FBox SelectionGroupWorldBounds = FBox(ForceInit);
    bool bMultiSelection = false;

    static constexpr float RingRadius = 82.0f;
    static constexpr float BaseGizmoDiameter = 180.0f;
    static constexpr float TargetScreenDiameterPx = 180.0f;

    void ConfigureHitPart(UStaticMeshComponent* Component);
    void ConfigureVisualPart(UStaticMeshComponent* Component);
    void ConfigureBoundsPart(UStaticMeshComponent* Component);
    void CreateRuntimeMaterials();
    void ApplyInteractionColors();
    void UpdateScreenScale();
    void UpdateHubFacing();
    void UpdateBoundsVisual();
    void SetVisualEnabled(UStaticMeshComponent* Component, bool bEnabled) const;
    void PlaceWorldSegment(
        UStaticMeshComponent* Component,
        const FVector& Start,
        const FVector& End,
        float ThicknessWorld) const;
    static FTransform MakeWorldSegmentTransform(
        const FVector& Start,
        const FVector& End,
        float ThicknessWorld);
};
