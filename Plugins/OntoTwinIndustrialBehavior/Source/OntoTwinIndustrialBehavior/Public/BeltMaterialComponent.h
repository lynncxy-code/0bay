#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Dom/JsonObject.h"
#include "BeltMaterialComponent.generated.h"
class UMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

UCLASS()
class ONTOTWININDUSTRIALBEHAVIOR_API UBeltMaterialComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UBeltMaterialComponent();
    bool Apply(const TSharedPtr<FJsonObject>& Params);
    void ResetMaterials();
    virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Function) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> Material;
private:
    struct FSlot { TWeakObjectPtr<UMeshComponent> Mesh; int32 Index; TWeakObjectPtr<UMaterialInterface> Original; };
    TArray<FSlot> Slots;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInterface>> HeldOriginals;
    void FindMeshes();
    bool bActive = false, bTaggedOnly = false;
    double Speed = .5, Offset = 0;
};
