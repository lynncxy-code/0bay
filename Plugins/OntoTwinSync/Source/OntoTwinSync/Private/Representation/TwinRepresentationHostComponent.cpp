#include "Representation/TwinRepresentationHostComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"

UTwinRepresentationHostComponent::UTwinRepresentationHostComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    TargetStaticMeshComponent.ComponentProperty = TEXT("StaticMesh");
}

UStaticMeshComponent* UTwinRepresentationHostComponent::ResolveTargetStaticMeshComponent() const
{
    AActor* Owner = GetOwner();
    if (!Owner)
    {
        return nullptr;
    }

    if (UActorComponent* Referenced = TargetStaticMeshComponent.GetComponent(Owner))
    {
        if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Referenced))
        {
            return StaticMeshComponent;
        }
    }

    if (!TargetComponentNameFallback.IsNone())
    {
        TInlineComponentArray<UStaticMeshComponent*> StaticMeshComponents(Owner);
        for (UStaticMeshComponent* Component : StaticMeshComponents)
        {
            if (Component && Component->GetFName() == TargetComponentNameFallback)
            {
                return Component;
            }
        }
    }
    return nullptr;
}

bool UTwinRepresentationHostComponent::ConfigureStaticMesh(
    UStaticMesh* StaticMesh,
    const FString& InstanceId,
    const FString& DisplayName,
    const FString& AssetPath,
    FString& OutError)
{
    OutError.Empty();
    if (!StaticMesh)
    {
        OutError = TEXT("asset_id 未加载为 StaticMesh");
        return false;
    }

    UStaticMeshComponent* Target = ResolveTargetStaticMeshComponent();
    if (!Target)
    {
        OutError = FString::Printf(
            TEXT("槽位 %s 未配置有效的 StaticMeshComponent"),
            *SlotName.ToString());
        return false;
    }

    Target->SetStaticMesh(StaticMesh);
    Target->SetVisibility(true, true);
    OnRepresentationConfigured.Broadcast(
        InstanceId,
        DisplayName,
        AssetPath,
        StaticMesh);
    return true;
}

void UTwinRepresentationHostComponent::ClearRepresentation(const FString& InstanceId)
{
    if (UStaticMeshComponent* Target = ResolveTargetStaticMeshComponent())
    {
        Target->SetStaticMesh(nullptr);
    }
    OnRepresentationCleared.Broadcast(InstanceId);
}

void UTwinRepresentationHostComponent::ForwardVisualState(
    const FString& MaterialVariant,
    bool bVisible)
{
    OnVisualStateChanged.Broadcast(MaterialVariant, bVisible);
}

void UTwinRepresentationHostComponent::ForwardBehaviorState(
    const FString& AnimationState,
    const FString& FxTrigger)
{
    OnBehaviorStateChanged.Broadcast(AnimationState, FxTrigger);
}
