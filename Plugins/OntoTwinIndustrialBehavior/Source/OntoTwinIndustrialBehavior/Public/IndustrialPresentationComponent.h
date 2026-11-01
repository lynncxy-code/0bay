#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "IndustrialPresentationComponent.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMeshComponent;
class UMaterialInterface;
class UTextRenderComponent;

/** Executable example resources. All motion stays on owned indicator components. */
UCLASS()
class ONTOTWININDUSTRIALBEHAVIOR_API UIndustrialPresentationComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UIndustrialPresentationComponent();
    bool Apply(const FString& Channel, const FString& Behavior);
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Motion;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Beacon;
    UPROPERTY(Transient) TObjectPtr<UTextRenderComponent> StatusLabel;
    FString Animation;
    FString Effect;
    FString Visual;
private:
    UStaticMeshComponent* CreateIndicator(FName Name);
    bool UpdateVisual();
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> Overlay;
    TMap<TWeakObjectPtr<UMeshComponent>, TWeakObjectPtr<UMaterialInterface>> PreviousOverlays;
    float Phase = 0.f;
};
