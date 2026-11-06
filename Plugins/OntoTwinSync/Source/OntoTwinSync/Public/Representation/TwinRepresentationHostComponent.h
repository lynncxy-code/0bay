#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "TwinRepresentationHostComponent.generated.h"

class UStaticMesh;
class UStaticMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(
    FTwinRepresentationConfigured,
    const FString&, InstanceId,
    const FString&, DisplayName,
    const FString&, AssetPath,
    UStaticMesh*, StaticMesh);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FTwinRepresentationCleared,
    const FString&, InstanceId);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FTwinHostVisualStateChanged,
    const FString&, MaterialVariant,
    bool, bVisible);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FTwinHostBehaviorStateChanged,
    const FString&, AnimationState,
    const FString&, FxTrigger);

/**
 * Explicit adapter between an OntoTwin instance and a project-owned Actor Blueprint.
 * Add one component to the container BP and point TargetStaticMeshComponent at the
 * component that should receive I3D_Representable.asset_id.
 */
UCLASS(ClassGroup=(DigitalTwin), meta=(BlueprintSpawnableComponent, DisplayName="OntoTwin 表现宿主"))
class ONTOTWINSYNC_API UTwinRepresentationHostComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UTwinRepresentationHostComponent();

    /** Logical slot selected by I3D_Representable.container_slot. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="OntoTwin|表现容器")
    FName SlotName = TEXT("primary");

    /** Explicit reference to the StaticMeshComponent controlled by this slot. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="OntoTwin|表现容器")
    FComponentReference TargetStaticMeshComponent;

    /** Compatibility fallback used only when the component reference is unresolved. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="OntoTwin|表现容器", meta=(AdvancedDisplay))
    FName TargetComponentNameFallback = TEXT("StaticMesh");

    UPROPERTY(BlueprintAssignable, Category="OntoTwin|表现容器")
    FTwinRepresentationConfigured OnRepresentationConfigured;

    UPROPERTY(BlueprintAssignable, Category="OntoTwin|表现容器")
    FTwinRepresentationCleared OnRepresentationCleared;

    UPROPERTY(BlueprintAssignable, Category="OntoTwin|表现容器")
    FTwinHostVisualStateChanged OnVisualStateChanged;

    UPROPERTY(BlueprintAssignable, Category="OntoTwin|表现容器")
    FTwinHostBehaviorStateChanged OnBehaviorStateChanged;

    bool ConfigureStaticMesh(
        UStaticMesh* StaticMesh,
        const FString& InstanceId,
        const FString& DisplayName,
        const FString& AssetPath,
        FString& OutError);

    void ClearRepresentation(const FString& InstanceId);
    void ForwardVisualState(const FString& MaterialVariant, bool bVisible);
    void ForwardBehaviorState(const FString& AnimationState, const FString& FxTrigger);

    UStaticMeshComponent* ResolveTargetStaticMeshComponent() const;
};
