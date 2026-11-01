#include "OntoTwinRuntimeGizmo.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "SceneTypes.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
constexpr int32 RingVisualSegmentCount = 40;
constexpr int32 RingHitSegmentCount = 28;
constexpr int32 BoundsEdgeCount = 12;
constexpr int32 BoundsCornerCount = 8;
constexpr int32 BoundsSegmentsPerCorner = 3;
constexpr float BaseStrokeWidth = 0.052f;
constexpr float BaseStrokeHeight = 0.022f;

FLinearColor ColorFromHex(uint8 R, uint8 G, uint8 B)
{
    return FLinearColor::FromSRGBColor(FColor(R, G, B));
}

float InteractionScale(
    EOntoTwinRuntimeGizmoPart Part,
    EOntoTwinRuntimeGizmoPart HoverPart,
    EOntoTwinRuntimeGizmoPart ActivePart)
{
    if (Part == ActivePart) return 1.24f;
    if (Part == HoverPart) return 1.12f;
    return 1.0f;
}
}

AOntoTwinRuntimeGizmo::AOntoTwinRuntimeGizmo()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;

    GizmoRoot = CreateDefaultSubobject<USceneComponent>(TEXT("GizmoRoot"));
    RootComponent = GizmoRoot;

    XAxisHit = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MoveXHit"));
    XAxisHit->SetupAttachment(GizmoRoot);
    YAxisHit = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MoveYHit"));
    YAxisHit->SetupAttachment(GizmoRoot);
    ZAxisHit = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MoveZHit"));
    ZAxisHit->SetupAttachment(GizmoRoot);
    MoveXYHit = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MoveXYHit"));
    MoveXYHit->SetupAttachment(GizmoRoot);

    XAxisShaftVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MoveXShaftVisual"));
    XAxisShaftVisual->SetupAttachment(GizmoRoot);
    YAxisShaftVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MoveYShaftVisual"));
    YAxisShaftVisual->SetupAttachment(GizmoRoot);
    ZAxisShaftVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MoveZShaftVisual"));
    ZAxisShaftVisual->SetupAttachment(GizmoRoot);

    XAxisArrowVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MoveXArrowVisual"));
    XAxisArrowVisual->SetupAttachment(GizmoRoot);
    YAxisArrowVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MoveYArrowVisual"));
    YAxisArrowVisual->SetupAttachment(GizmoRoot);
    ZAxisArrowVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MoveZArrowVisual"));
    ZAxisArrowVisual->SetupAttachment(GizmoRoot);

    HubOuterVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MoveXYHubOuterVisual"));
    HubOuterVisual->SetupAttachment(GizmoRoot);
    HubCoreVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MoveXYHubCoreVisual"));
    HubCoreVisual->SetupAttachment(GizmoRoot);

    RotateHitHandle = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RotateYawHit"));
    RotateHitHandle->SetupAttachment(GizmoRoot);
    RotateHandleVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RotateHandleVisual"));
    RotateHandleVisual->SetupAttachment(GizmoRoot);

    for (int32 Index = 0; Index < RingVisualSegmentCount; ++Index)
    {
        UStaticMeshComponent* Segment = CreateDefaultSubobject<UStaticMeshComponent>(
            FName(*FString::Printf(TEXT("RotateVisual_%02d"), Index)));
        Segment->SetupAttachment(GizmoRoot);
        RotateRingVisuals.Add(Segment);
    }
    for (int32 Index = 0; Index < RingHitSegmentCount; ++Index)
    {
        UStaticMeshComponent* Segment = CreateDefaultSubobject<UStaticMeshComponent>(
            FName(*FString::Printf(TEXT("RotateHit_%02d"), Index)));
        Segment->SetupAttachment(GizmoRoot);
        RotateHitSegments.Add(Segment);
    }

    for (int32 Index = 0; Index < BoundsEdgeCount; ++Index)
    {
        UStaticMeshComponent* EdgeSegment = CreateDefaultSubobject<UStaticMeshComponent>(
            FName(*FString::Printf(TEXT("BoundsEdge_%02d"), Index)));
        EdgeSegment->SetupAttachment(GizmoRoot);
        BoundsEdgeVisuals.Add(EdgeSegment);
    }
    for (int32 Index = 0; Index < BoundsCornerCount * BoundsSegmentsPerCorner; ++Index)
    {
        UStaticMeshComponent* CornerSegment = CreateDefaultSubobject<UStaticMeshComponent>(
            FName(*FString::Printf(TEXT("BoundsCorner_%02d"), Index)));
        CornerSegment->SetupAttachment(GizmoRoot);
        BoundsCornerVisuals.Add(CornerSegment);
    }

    IndividualBoundsCorners = CreateDefaultSubobject<UInstancedStaticMeshComponent>(
        TEXT("IndividualBoundsCorners"));
    IndividualBoundsCorners->SetupAttachment(GizmoRoot);

    SnapMarker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SnapMarker"));
    SnapMarker->SetupAttachment(GizmoRoot);

    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMesh(TEXT("/Engine/BasicShapes/Cone.Cone"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMaterial(
        TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

    BaseGizmoMaterial = BasicMaterial.Succeeded() ? BasicMaterial.Object : nullptr;

    if (CubeMesh.Succeeded())
    {
        XAxisHit->SetStaticMesh(CubeMesh.Object);
        YAxisHit->SetStaticMesh(CubeMesh.Object);
        ZAxisHit->SetStaticMesh(CubeMesh.Object);
        for (UStaticMeshComponent* Segment : RotateRingVisuals) Segment->SetStaticMesh(CubeMesh.Object);
        for (UStaticMeshComponent* Segment : RotateHitSegments) Segment->SetStaticMesh(CubeMesh.Object);
        for (UStaticMeshComponent* Segment : BoundsEdgeVisuals) Segment->SetStaticMesh(CubeMesh.Object);
        for (UStaticMeshComponent* Segment : BoundsCornerVisuals) Segment->SetStaticMesh(CubeMesh.Object);
        IndividualBoundsCorners->SetStaticMesh(CubeMesh.Object);
    }
    if (CylinderMesh.Succeeded())
    {
        XAxisShaftVisual->SetStaticMesh(CylinderMesh.Object);
        YAxisShaftVisual->SetStaticMesh(CylinderMesh.Object);
        ZAxisShaftVisual->SetStaticMesh(CylinderMesh.Object);
    }
    if (SphereMesh.Succeeded())
    {
        MoveXYHit->SetStaticMesh(SphereMesh.Object);
        HubOuterVisual->SetStaticMesh(SphereMesh.Object);
        HubCoreVisual->SetStaticMesh(SphereMesh.Object);
        RotateHitHandle->SetStaticMesh(SphereMesh.Object);
        RotateHandleVisual->SetStaticMesh(SphereMesh.Object);
        SnapMarker->SetStaticMesh(SphereMesh.Object);
    }
    if (ConeMesh.Succeeded())
    {
        XAxisArrowVisual->SetStaticMesh(ConeMesh.Object);
        YAxisArrowVisual->SetStaticMesh(ConeMesh.Object);
        ZAxisArrowVisual->SetStaticMesh(ConeMesh.Object);
    }

    XAxisHit->SetRelativeLocation(FVector(43.0f, 0.0f, 0.0f));
    XAxisHit->SetRelativeScale3D(FVector(0.62f, 0.14f, 0.14f));
    ConfigureHitPart(XAxisHit);
    YAxisHit->SetRelativeLocation(FVector(0.0f, 43.0f, 0.0f));
    YAxisHit->SetRelativeScale3D(FVector(0.14f, 0.62f, 0.14f));
    ConfigureHitPart(YAxisHit);
    ZAxisHit->SetRelativeLocation(FVector(0.0f, 0.0f, 48.0f));
    ZAxisHit->SetRelativeScale3D(FVector(0.14f, 0.14f, 0.76f));
    ConfigureHitPart(ZAxisHit);
    MoveXYHit->SetRelativeScale3D(FVector(0.30f));
    ConfigureHitPart(MoveXYHit);

    const FRotator XAxisRotation = FQuat::FindBetweenNormals(FVector::UpVector, FVector::ForwardVector).Rotator();
    const FRotator YAxisRotation = FQuat::FindBetweenNormals(FVector::UpVector, FVector::RightVector).Rotator();

    XAxisShaftVisual->SetRelativeLocation(FVector(36.5f, 0.0f, 0.0f));
    XAxisShaftVisual->SetRelativeRotation(XAxisRotation);
    XAxisShaftVisual->SetRelativeScale3D(FVector(BaseStrokeWidth, BaseStrokeWidth, 0.49f));
    ConfigureVisualPart(XAxisShaftVisual);
    YAxisShaftVisual->SetRelativeLocation(FVector(0.0f, 36.5f, 0.0f));
    YAxisShaftVisual->SetRelativeRotation(YAxisRotation);
    YAxisShaftVisual->SetRelativeScale3D(FVector(BaseStrokeWidth, BaseStrokeWidth, 0.49f));
    ConfigureVisualPart(YAxisShaftVisual);
    ZAxisShaftVisual->SetRelativeLocation(FVector(0.0f, 0.0f, 43.0f));
    ZAxisShaftVisual->SetRelativeScale3D(FVector(BaseStrokeWidth, BaseStrokeWidth, 0.62f));
    ConfigureVisualPart(ZAxisShaftVisual);

    XAxisArrowVisual->SetRelativeLocation(FVector(70.0f, 0.0f, 0.0f));
    XAxisArrowVisual->SetRelativeRotation(XAxisRotation);
    XAxisArrowVisual->SetRelativeScale3D(FVector(0.12f, 0.12f, 0.18f));
    ConfigureVisualPart(XAxisArrowVisual);
    YAxisArrowVisual->SetRelativeLocation(FVector(0.0f, 70.0f, 0.0f));
    YAxisArrowVisual->SetRelativeRotation(YAxisRotation);
    YAxisArrowVisual->SetRelativeScale3D(FVector(0.12f, 0.12f, 0.18f));
    ConfigureVisualPart(YAxisArrowVisual);
    ZAxisArrowVisual->SetRelativeLocation(FVector(0.0f, 0.0f, 84.0f));
    ZAxisArrowVisual->SetRelativeScale3D(FVector(0.12f, 0.12f, 0.18f));
    ConfigureVisualPart(ZAxisArrowVisual);

    HubOuterVisual->SetRelativeScale3D(FVector(0.22f));
    ConfigureVisualPart(HubOuterVisual);
    HubCoreVisual->SetRelativeScale3D(FVector(0.105f));
    ConfigureVisualPart(HubCoreVisual);

    const float RingVisualSegmentLength = (2.0f * PI * RingRadius / RingVisualSegmentCount) * 0.92f;
    for (int32 Index = 0; Index < RotateRingVisuals.Num(); ++Index)
    {
        const float AngleDeg = (360.0f * Index) / RotateRingVisuals.Num();
        const float AngleRad = FMath::DegreesToRadians(AngleDeg);
        UStaticMeshComponent* Segment = RotateRingVisuals[Index];
        Segment->SetRelativeLocation(FVector(
            FMath::Cos(AngleRad) * RingRadius,
            FMath::Sin(AngleRad) * RingRadius,
            0.0f));
        Segment->SetRelativeRotation(FRotator(0.0f, AngleDeg + 90.0f, 0.0f));
        Segment->SetRelativeScale3D(
            FVector(RingVisualSegmentLength / 100.0f, BaseStrokeWidth, BaseStrokeHeight));
        ConfigureVisualPart(Segment);
    }

    const float RingHitSegmentLength = (2.0f * PI * RingRadius / RingHitSegmentCount) * 1.12f;
    for (int32 Index = 0; Index < RotateHitSegments.Num(); ++Index)
    {
        const float AngleDeg = (360.0f * Index) / RotateHitSegments.Num();
        const float AngleRad = FMath::DegreesToRadians(AngleDeg);
        UStaticMeshComponent* Segment = RotateHitSegments[Index];
        Segment->SetRelativeLocation(FVector(
            FMath::Cos(AngleRad) * RingRadius,
            FMath::Sin(AngleRad) * RingRadius,
            0.0f));
        Segment->SetRelativeRotation(FRotator(0.0f, AngleDeg + 90.0f, 0.0f));
        Segment->SetRelativeScale3D(FVector(RingHitSegmentLength / 100.0f, 0.13f, 0.08f));
        ConfigureHitPart(Segment);
    }

    const float HandleAngleRad = FMath::DegreesToRadians(45.0f);
    const FVector HandleLocation(
        FMath::Cos(HandleAngleRad) * RingRadius,
        FMath::Sin(HandleAngleRad) * RingRadius,
        0.0f);
    RotateHitHandle->SetRelativeLocation(HandleLocation);
    RotateHitHandle->SetRelativeScale3D(FVector(0.18f));
    ConfigureHitPart(RotateHitHandle);
    RotateHandleVisual->SetRelativeLocation(HandleLocation);
    RotateHandleVisual->SetRelativeScale3D(FVector(0.085f));
    ConfigureVisualPart(RotateHandleVisual);

    for (UStaticMeshComponent* Segment : BoundsEdgeVisuals) ConfigureBoundsPart(Segment);
    for (UStaticMeshComponent* Segment : BoundsCornerVisuals) ConfigureBoundsPart(Segment);
    ConfigureBoundsPart(IndividualBoundsCorners);

    SnapMarker->SetRelativeScale3D(FVector(0.07f));
    ConfigureVisualPart(SnapMarker);

    SetGizmoEnabled(false);
}

void AOntoTwinRuntimeGizmo::BeginPlay()
{
    Super::BeginPlay();
    CreateRuntimeMaterials();
    ApplyInteractionColors();
}

void AOntoTwinRuntimeGizmo::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bGizmoEnabled) return;

    UpdateScreenScale();
    UpdateHubFacing();
    UpdateBoundsVisual();
}

void AOntoTwinRuntimeGizmo::ConfigureHitPart(UStaticMeshComponent* Component)
{
    if (!Component) return;

    Component->SetMobility(EComponentMobility::Movable);
    Component->SetVisibility(false, true);
    Component->SetHiddenInGame(true);
    Component->SetGenerateOverlapEvents(false);
    Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Component->SetCollisionResponseToAllChannels(ECR_Ignore);
    Component->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    Component->SetCastShadow(false);
    Component->SetRenderInMainPass(false);
}

void AOntoTwinRuntimeGizmo::ConfigureVisualPart(UStaticMeshComponent* Component)
{
    if (!Component) return;

    Component->SetMobility(EComponentMobility::Movable);
    Component->SetVisibility(false, true);
    Component->SetHiddenInGame(true);
    Component->SetGenerateOverlapEvents(false);
    Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Component->SetCollisionResponseToAllChannels(ECR_Ignore);
    Component->SetCastShadow(false);
    Component->SetDepthPriorityGroup(SDPG_Foreground);
}

void AOntoTwinRuntimeGizmo::ConfigureBoundsPart(UStaticMeshComponent* Component)
{
    ConfigureVisualPart(Component);
    if (Component)
    {
        Component->SetAbsolute(true, true, true);
    }
}

void AOntoTwinRuntimeGizmo::CreateRuntimeMaterials()
{
    if (!BaseGizmoMaterial) return;

    const auto MakeMaterial = [this](UMaterialInstanceDynamic*& Material, const TCHAR* Name)
    {
        if (!Material)
        {
            Material = UMaterialInstanceDynamic::Create(BaseGizmoMaterial, this, FName(Name));
        }
    };

    MakeMaterial(XMaterial, TEXT("RuntimeGizmoX"));
    MakeMaterial(YMaterial, TEXT("RuntimeGizmoY"));
    MakeMaterial(ZMaterial, TEXT("RuntimeGizmoZ"));
    MakeMaterial(HubOuterMaterial, TEXT("RuntimeGizmoHubOuter"));
    MakeMaterial(HubCoreMaterial, TEXT("RuntimeGizmoHubCore"));
    MakeMaterial(RotateMaterial, TEXT("RuntimeGizmoRotate"));
    MakeMaterial(BoundsMaterial, TEXT("RuntimeGizmoBounds"));
    MakeMaterial(IndividualBoundsMaterial, TEXT("RuntimeGizmoIndividualBounds"));
    MakeMaterial(SnapMaterial, TEXT("RuntimeGizmoSnap"));

    if (XAxisShaftVisual) XAxisShaftVisual->SetMaterial(0, XMaterial);
    if (XAxisArrowVisual) XAxisArrowVisual->SetMaterial(0, XMaterial);
    if (YAxisShaftVisual) YAxisShaftVisual->SetMaterial(0, YMaterial);
    if (YAxisArrowVisual) YAxisArrowVisual->SetMaterial(0, YMaterial);
    if (ZAxisShaftVisual) ZAxisShaftVisual->SetMaterial(0, ZMaterial);
    if (ZAxisArrowVisual) ZAxisArrowVisual->SetMaterial(0, ZMaterial);
    if (HubOuterVisual) HubOuterVisual->SetMaterial(0, HubOuterMaterial);
    if (HubCoreVisual) HubCoreVisual->SetMaterial(0, HubCoreMaterial);
    for (UStaticMeshComponent* Segment : RotateRingVisuals) Segment->SetMaterial(0, RotateMaterial);
    if (RotateHandleVisual) RotateHandleVisual->SetMaterial(0, RotateMaterial);
    for (UStaticMeshComponent* Segment : BoundsEdgeVisuals) Segment->SetMaterial(0, BoundsMaterial);
    for (UStaticMeshComponent* Segment : BoundsCornerVisuals) Segment->SetMaterial(0, BoundsMaterial);
    if (IndividualBoundsCorners) IndividualBoundsCorners->SetMaterial(0, IndividualBoundsMaterial);
    if (SnapMarker) SnapMarker->SetMaterial(0, SnapMaterial);
}

void AOntoTwinRuntimeGizmo::ApplyInteractionColors()
{
    const auto ResolveColor = [this](
        EOntoTwinRuntimeGizmoPart Part,
        const FLinearColor& Base,
        const FLinearColor& Hover,
        const FLinearColor& Active)
    {
        if (Part == CurrentActivePart) return Active;
        if (Part == CurrentHoverPart) return Hover;
        return Base;
    };

    if (XMaterial)
    {
        XMaterial->SetVectorParameterValue(TEXT("Color"), ResolveColor(
            EOntoTwinRuntimeGizmoPart::MoveX,
            ColorFromHex(224, 36, 36), ColorFromHex(255, 68, 68), ColorFromHex(255, 112, 112)));
    }
    if (YMaterial)
    {
        YMaterial->SetVectorParameterValue(TEXT("Color"), ResolveColor(
            EOntoTwinRuntimeGizmoPart::MoveY,
            ColorFromHex(14, 166, 74), ColorFromHex(30, 205, 94), ColorFromHex(83, 235, 135)));
    }
    if (ZMaterial)
    {
        ZMaterial->SetVectorParameterValue(TEXT("Color"), ResolveColor(
            EOntoTwinRuntimeGizmoPart::MoveZ,
            ColorFromHex(37, 99, 235), ColorFromHex(55, 134, 255), ColorFromHex(110, 170, 255)));
    }
    if (RotateMaterial)
    {
        RotateMaterial->SetVectorParameterValue(TEXT("Color"), ResolveColor(
            EOntoTwinRuntimeGizmoPart::RotateYaw,
            ColorFromHex(245, 124, 0), ColorFromHex(255, 157, 36), ColorFromHex(255, 190, 92)));
    }
    if (HubOuterMaterial)
    {
        HubOuterMaterial->SetVectorParameterValue(TEXT("Color"), ResolveColor(
            EOntoTwinRuntimeGizmoPart::MoveXY,
            ColorFromHex(30, 35, 42), ColorFromHex(15, 119, 145), ColorFromHex(0, 166, 196)));
    }
    if (HubCoreMaterial)
    {
        HubCoreMaterial->SetVectorParameterValue(TEXT("Color"), ColorFromHex(250, 252, 255));
    }
    if (BoundsMaterial)
    {
        BoundsMaterial->SetVectorParameterValue(
            TEXT("Color"), bMultiSelection ? ColorFromHex(37, 132, 255) : ColorFromHex(250, 252, 255));
    }
    if (IndividualBoundsMaterial)
    {
        IndividualBoundsMaterial->SetVectorParameterValue(TEXT("Color"), ColorFromHex(224, 229, 236));
    }
    if (SnapMaterial)
    {
        SnapMaterial->SetVectorParameterValue(TEXT("Color"), ColorFromHex(67, 214, 132));
    }

    const float XScale = InteractionScale(
        EOntoTwinRuntimeGizmoPart::MoveX, CurrentHoverPart, CurrentActivePart);
    const float YScale = InteractionScale(
        EOntoTwinRuntimeGizmoPart::MoveY, CurrentHoverPart, CurrentActivePart);
    const float ZScale = InteractionScale(
        EOntoTwinRuntimeGizmoPart::MoveZ, CurrentHoverPart, CurrentActivePart);
    const float HubScale = InteractionScale(
        EOntoTwinRuntimeGizmoPart::MoveXY, CurrentHoverPart, CurrentActivePart);
    const float RotateScale = InteractionScale(
        EOntoTwinRuntimeGizmoPart::RotateYaw, CurrentHoverPart, CurrentActivePart);

    if (XAxisShaftVisual) XAxisShaftVisual->SetRelativeScale3D(FVector(BaseStrokeWidth * XScale, BaseStrokeWidth * XScale, 0.49f));
    if (YAxisShaftVisual) YAxisShaftVisual->SetRelativeScale3D(FVector(BaseStrokeWidth * YScale, BaseStrokeWidth * YScale, 0.49f));
    if (ZAxisShaftVisual) ZAxisShaftVisual->SetRelativeScale3D(FVector(BaseStrokeWidth * ZScale, BaseStrokeWidth * ZScale, 0.62f));
    if (XAxisArrowVisual) XAxisArrowVisual->SetRelativeScale3D(FVector(0.12f * XScale, 0.12f * XScale, 0.18f * XScale));
    if (YAxisArrowVisual) YAxisArrowVisual->SetRelativeScale3D(FVector(0.12f * YScale, 0.12f * YScale, 0.18f * YScale));
    if (ZAxisArrowVisual) ZAxisArrowVisual->SetRelativeScale3D(FVector(0.12f * ZScale, 0.12f * ZScale, 0.18f * ZScale));
    if (HubOuterVisual) HubOuterVisual->SetRelativeScale3D(FVector(0.22f * HubScale));
    if (HubCoreVisual) HubCoreVisual->SetRelativeScale3D(FVector(0.105f * HubScale));

    const float RingSegmentLength = (2.0f * PI * RingRadius / RingVisualSegmentCount) * 0.92f;
    for (UStaticMeshComponent* Segment : RotateRingVisuals)
    {
        Segment->SetRelativeScale3D(FVector(
            RingSegmentLength / 100.0f,
            BaseStrokeWidth * RotateScale,
            BaseStrokeHeight * RotateScale));
    }
    if (RotateHandleVisual) RotateHandleVisual->SetRelativeScale3D(FVector(0.085f * RotateScale));
}

void AOntoTwinRuntimeGizmo::UpdateForTarget(
    AActor* InTargetActor,
    const FBox& LocalBounds,
    const FVector& EditPivotLocal)
{
    if (!InTargetActor || !IsValid(InTargetActor) || !LocalBounds.IsValid)
    {
        TargetActor.Reset();
        SetGizmoEnabled(false);
        return;
    }

    TArray<AActor*> Targets;
    Targets.Add(InTargetActor);
    TArray<FBox> Bounds;
    Bounds.Add(LocalBounds);
    UpdateForSelection(
        Targets,
        Bounds,
        LocalBounds.TransformBy(InTargetActor->GetActorTransform()),
        InTargetActor->GetActorTransform().TransformPosition(EditPivotLocal));
}

void AOntoTwinRuntimeGizmo::UpdateForSelection(
    const TArray<AActor*>& InTargetActors,
    const TArray<FBox>& LocalBounds,
    const FBox& GroupWorldBounds,
    const FVector& GroupPivotWorld)
{
    if (InTargetActors.Num() == 0 || InTargetActors.Num() != LocalBounds.Num() || !GroupWorldBounds.IsValid)
    {
        TargetActor.Reset();
        SelectionTargets.Reset();
        SelectionLocalBounds.Reset();
        SelectionGroupWorldBounds = FBox(ForceInit);
        SetGizmoEnabled(false);
        return;
    }

    SelectionTargets.Reset(InTargetActors.Num());
    for (AActor* Target : InTargetActors)
    {
        SelectionTargets.Add(Target);
    }
    SelectionLocalBounds = LocalBounds;
    SelectionGroupWorldBounds = GroupWorldBounds;
    bMultiSelection = InTargetActors.Num() > 1;
    TargetActor = InTargetActors[0];
    TargetLocalBounds = LocalBounds[0];
    TargetEditPivotLocal = TargetLocalBounds.GetCenter();
    SetActorLocation(GroupPivotWorld);
    SetActorRotation(FRotator::ZeroRotator);
    ApplyInteractionColors();
    SetGizmoEnabled(true);
    UpdateBoundsVisual();
}

void AOntoTwinRuntimeGizmo::SetVisualEnabled(UStaticMeshComponent* Component, bool bEnabled) const
{
    if (!Component) return;
    Component->SetHiddenInGame(!bEnabled);
    Component->SetVisibility(bEnabled, true);
}

void AOntoTwinRuntimeGizmo::SetGizmoEnabled(bool bEnabled)
{
    bGizmoEnabled = bEnabled;
    SetActorHiddenInGame(!bEnabled);
    SetActorEnableCollision(bEnabled);
    SetActorTickEnabled(bEnabled);

    const ECollisionEnabled::Type HitCollision = bEnabled
        ? ECollisionEnabled::QueryOnly
        : ECollisionEnabled::NoCollision;
    if (XAxisHit) XAxisHit->SetCollisionEnabled(HitCollision);
    if (YAxisHit) YAxisHit->SetCollisionEnabled(HitCollision);
    if (ZAxisHit) ZAxisHit->SetCollisionEnabled(HitCollision);
    if (MoveXYHit) MoveXYHit->SetCollisionEnabled(HitCollision);
    if (RotateHitHandle) RotateHitHandle->SetCollisionEnabled(HitCollision);
    for (UStaticMeshComponent* Segment : RotateHitSegments) Segment->SetCollisionEnabled(HitCollision);

    SetVisualEnabled(XAxisShaftVisual, bEnabled);
    SetVisualEnabled(YAxisShaftVisual, bEnabled);
    SetVisualEnabled(ZAxisShaftVisual, bEnabled);
    SetVisualEnabled(XAxisArrowVisual, bEnabled);
    SetVisualEnabled(YAxisArrowVisual, bEnabled);
    SetVisualEnabled(ZAxisArrowVisual, bEnabled);
    SetVisualEnabled(HubOuterVisual, bEnabled);
    SetVisualEnabled(HubCoreVisual, bEnabled);
    SetVisualEnabled(RotateHandleVisual, bEnabled);
    for (UStaticMeshComponent* Segment : RotateRingVisuals) SetVisualEnabled(Segment, bEnabled);
    for (UStaticMeshComponent* Segment : BoundsEdgeVisuals) SetVisualEnabled(Segment, false);
    for (UStaticMeshComponent* Segment : BoundsCornerVisuals) SetVisualEnabled(Segment, bEnabled);
    SetVisualEnabled(IndividualBoundsCorners, bEnabled && bMultiSelection);
    if (!bEnabled && IndividualBoundsCorners) IndividualBoundsCorners->ClearInstances();

    const bool bShowSnapMarker = bEnabled && CurrentSnapState == EOntoTwinRuntimeSnapState::Wall;
    SetVisualEnabled(SnapMarker, bShowSnapMarker);
}

void AOntoTwinRuntimeGizmo::SetInteractionState(
    EOntoTwinRuntimeGizmoPart HoverPart,
    EOntoTwinRuntimeGizmoPart ActivePart)
{
    if (CurrentHoverPart == HoverPart && CurrentActivePart == ActivePart) return;

    CurrentHoverPart = HoverPart;
    CurrentActivePart = ActivePart;
    ApplyInteractionColors();
}

void AOntoTwinRuntimeGizmo::SetSnapFeedback(
    EOntoTwinRuntimeSnapState SnapState,
    const FVector& WorldPoint)
{
    CurrentSnapState = SnapState;
    if (!SnapMarker) return;

    const bool bShowMarker = bGizmoEnabled && SnapState == EOntoTwinRuntimeSnapState::Wall;
    if (bShowMarker)
    {
        SnapMarker->SetWorldLocation(WorldPoint + FVector(0.0f, 0.0f, 8.0f));
    }
    SetVisualEnabled(SnapMarker, bShowMarker);
}

EOntoTwinRuntimeGizmoPart AOntoTwinRuntimeGizmo::GetPartForComponent(
    const UPrimitiveComponent* Component) const
{
    if (!Component) return EOntoTwinRuntimeGizmoPart::None;
    if (Component == XAxisHit) return EOntoTwinRuntimeGizmoPart::MoveX;
    if (Component == YAxisHit) return EOntoTwinRuntimeGizmoPart::MoveY;
    if (Component == ZAxisHit) return EOntoTwinRuntimeGizmoPart::MoveZ;
    if (Component == MoveXYHit) return EOntoTwinRuntimeGizmoPart::MoveXY;
    if (Component == RotateHitHandle) return EOntoTwinRuntimeGizmoPart::RotateYaw;
    for (const UStaticMeshComponent* Segment : RotateHitSegments)
    {
        if (Component == Segment) return EOntoTwinRuntimeGizmoPart::RotateYaw;
    }
    return EOntoTwinRuntimeGizmoPart::None;
}

float AOntoTwinRuntimeGizmo::GetMoveInteractionPlaneZ() const
{
    return GetActorLocation().Z;
}

float AOntoTwinRuntimeGizmo::GetRotateInteractionPlaneZ() const
{
    return GetActorLocation().Z;
}

void AOntoTwinRuntimeGizmo::UpdateScreenScale()
{
    UWorld* World = GetWorld();
    APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
    if (!PlayerController || !PlayerController->PlayerCameraManager) return;

    int32 ViewportWidth = 0;
    int32 ViewportHeight = 0;
    PlayerController->GetViewportSize(ViewportWidth, ViewportHeight);
    if (ViewportHeight <= 0) return;

    FVector CameraLocation = FVector::ZeroVector;
    FRotator CameraRotation = FRotator::ZeroRotator;
    PlayerController->GetPlayerViewPoint(CameraLocation, CameraRotation);

    const float Distance = FMath::Max(FVector::Distance(CameraLocation, GetActorLocation()), 1.0f);
    const float HalfFovRadians = FMath::DegreesToRadians(
        FMath::Clamp(PlayerController->PlayerCameraManager->GetFOVAngle(), 30.0f, 140.0f) * 0.5f);
    const float WorldDiameter = 2.0f * Distance * FMath::Tan(HalfFovRadians) *
        (TargetScreenDiameterPx / static_cast<float>(ViewportHeight));
    const float UniformScale = FMath::Clamp(WorldDiameter / BaseGizmoDiameter, 0.25f, 10.0f);
    SetActorScale3D(FVector(UniformScale));
}

void AOntoTwinRuntimeGizmo::UpdateHubFacing()
{
    UWorld* World = GetWorld();
    APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
    if (!PlayerController || !HubCoreVisual) return;

    FVector CameraLocation = FVector::ZeroVector;
    FRotator CameraRotation = FRotator::ZeroRotator;
    PlayerController->GetPlayerViewPoint(CameraLocation, CameraRotation);
    const FVector TowardCamera = (CameraLocation - GetActorLocation()).GetSafeNormal();
    HubCoreVisual->SetRelativeLocation(TowardCamera * 7.0f);
}

void AOntoTwinRuntimeGizmo::PlaceWorldSegment(
    UStaticMeshComponent* Component,
    const FVector& Start,
    const FVector& End,
    float ThicknessWorld) const
{
    if (!Component) return;

    const FVector Direction = End - Start;
    const float Length = Direction.Size();
    if (Length <= UE_SMALL_NUMBER)
    {
        SetVisualEnabled(Component, false);
        return;
    }

    Component->SetWorldLocation((Start + End) * 0.5f);
    Component->SetWorldRotation(FRotationMatrix::MakeFromX(Direction).Rotator());
    Component->SetWorldScale3D(FVector(
        Length / 100.0f,
        ThicknessWorld / 100.0f,
        ThicknessWorld / 100.0f));
    SetVisualEnabled(Component, bGizmoEnabled);
}

void AOntoTwinRuntimeGizmo::UpdateBoundsVisual()
{
    if (!bGizmoEnabled || SelectionTargets.Num() == 0 ||
        SelectionTargets.Num() != SelectionLocalBounds.Num() || !SelectionGroupWorldBounds.IsValid)
    {
        for (UStaticMeshComponent* Segment : BoundsEdgeVisuals) SetVisualEnabled(Segment, false);
        for (UStaticMeshComponent* Segment : BoundsCornerVisuals) SetVisualEnabled(Segment, false);
        if (IndividualBoundsCorners)
        {
            IndividualBoundsCorners->ClearInstances();
            SetVisualEnabled(IndividualBoundsCorners, false);
        }
        return;
    }

    const int32 CornerNeighbors[BoundsCornerCount][BoundsSegmentsPerCorner] =
    {
        {1, 3, 4}, {0, 2, 5}, {3, 1, 6}, {2, 0, 7},
        {5, 7, 0}, {4, 6, 1}, {7, 5, 2}, {6, 4, 3}
    };

    FVector CameraLocation = GetActorLocation() + FVector::UpVector;
    if (UWorld* World = GetWorld())
    {
        if (APlayerController* PlayerController = World->GetFirstPlayerController())
        {
            FRotator CameraRotation = FRotator::ZeroRotator;
            PlayerController->GetPlayerViewPoint(CameraLocation, CameraRotation);
        }
    }

    const float UniformScale = FMath::Max(GetActorScale3D().X, 0.25f);
    const float WorldPerPixel = BaseGizmoDiameter * UniformScale / TargetScreenDiameterPx;
    const float EdgeThicknessWorld = FMath::Max(WorldPerPixel * 0.85f, 0.18f);
    const float CornerLengthWorld = WorldPerPixel * 18.0f;
    const float CornerThicknessWorld = FMath::Max(WorldPerPixel * 3.2f, 0.5f);

    for (UStaticMeshComponent* Segment : BoundsEdgeVisuals)
    {
        SetVisualEnabled(Segment, false);
    }

    const auto BuildCorners = [](const FBox& Bounds, const FTransform* Transform, FVector OutCorners[8])
    {
        const FVector Min = Bounds.Min;
        const FVector Max = Bounds.Max;
        const FVector Corners[8] =
        {
            FVector(Min.X, Min.Y, Min.Z), FVector(Max.X, Min.Y, Min.Z),
            FVector(Max.X, Max.Y, Min.Z), FVector(Min.X, Max.Y, Min.Z),
            FVector(Min.X, Min.Y, Max.Z), FVector(Max.X, Min.Y, Max.Z),
            FVector(Max.X, Max.Y, Max.Z), FVector(Min.X, Max.Y, Max.Z)
        };
        for (int32 Index = 0; Index < 8; ++Index)
        {
            OutCorners[Index] = Transform ? Transform->TransformPosition(Corners[Index]) : Corners[Index];
        }
    };

    const auto PlaceCornerSet = [this, &CameraLocation, &CornerNeighbors,
        CornerLengthWorld, CornerThicknessWorld](
            const FVector Corners[8],
            bool bUseInstances)
    {
        for (int32 CornerIndex = 0; CornerIndex < BoundsCornerCount; ++CornerIndex)
        {
            const FVector Corner = Corners[CornerIndex];
            const FVector CameraOffset =
                (CameraLocation - Corner).GetSafeNormal() * CornerThicknessWorld * 0.45f;

            for (int32 AxisIndex = 0; AxisIndex < BoundsSegmentsPerCorner; ++AxisIndex)
            {
                const int32 ComponentIndex = CornerIndex * BoundsSegmentsPerCorner + AxisIndex;
                const FVector FullEdge = Corners[CornerNeighbors[CornerIndex][AxisIndex]] - Corner;
                const float SegmentLength = FMath::Min(CornerLengthWorld, FullEdge.Size() * 0.22f);
                const FVector Start = Corner + CameraOffset;
                const FVector End = Corner + FullEdge.GetSafeNormal() * SegmentLength + CameraOffset;
                if (bUseInstances)
                {
                    if (IndividualBoundsCorners)
                    {
                        IndividualBoundsCorners->AddInstance(
                            MakeWorldSegmentTransform(Start, End, CornerThicknessWorld), true);
                    }
                }
                else
                {
                    PlaceWorldSegment(
                        BoundsCornerVisuals[ComponentIndex], Start, End, CornerThicknessWorld);
                }
            }
        }
    };

    if (IndividualBoundsCorners)
    {
        IndividualBoundsCorners->ClearInstances();
    }

    if (bMultiSelection && IndividualBoundsCorners)
    {
        for (int32 Index = 0; Index < SelectionTargets.Num(); ++Index)
        {
            AActor* Target = SelectionTargets[Index].Get();
            if (!Target || !IsValid(Target) || !SelectionLocalBounds[Index].IsValid) continue;
            FVector ActorCorners[8];
            const FTransform ActorTransform = Target->GetActorTransform();
            BuildCorners(SelectionLocalBounds[Index], &ActorTransform, ActorCorners);
            PlaceCornerSet(ActorCorners, true);
        }
        SetVisualEnabled(IndividualBoundsCorners, true);
    }
    else if (IndividualBoundsCorners)
    {
        SetVisualEnabled(IndividualBoundsCorners, false);
    }

    FVector GroupCorners[8];
    if (bMultiSelection)
    {
        BuildCorners(SelectionGroupWorldBounds, nullptr, GroupCorners);
    }
    else
    {
        AActor* Target = SelectionTargets[0].Get();
        if (!Target || !IsValid(Target)) return;
        const FTransform TargetTransform = Target->GetActorTransform();
        BuildCorners(SelectionLocalBounds[0], &TargetTransform, GroupCorners);
    }
    PlaceCornerSet(GroupCorners, false);
}

FTransform AOntoTwinRuntimeGizmo::MakeWorldSegmentTransform(
    const FVector& Start,
    const FVector& End,
    float ThicknessWorld)
{
    const FVector Direction = End - Start;
    const float Length = Direction.Size();
    if (Length <= KINDA_SMALL_NUMBER)
    {
        return FTransform::Identity;
    }
    return FTransform(
        FRotationMatrix::MakeFromX(Direction).ToQuat(),
        (Start + End) * 0.5f,
        FVector(Length / 100.0f, ThicknessWorld / 100.0f, ThicknessWorld / 100.0f));
}
