#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Dom/JsonObject.h"
#include "BasicMotionComponent.generated.h"

class USceneComponent;

/** Display-space motion only. The owner's business/spatial transform is never written. */
UCLASS()
class ONTOTWININDUSTRIALBEHAVIOR_API UBasicMotionComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UBasicMotionComponent();
    bool Apply(const FString& Behavior, const TSharedPtr<FJsonObject>& Params);
    void ResetMotion();
    virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Function) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    void DiscoverTargets();
    void ApplyOffset();
    struct FTarget
    {
        TWeakObjectPtr<USceneComponent> Component;
        FTransform OriginalRelative;
        FTransform OriginalInOwner;
    };
    TArray<FTarget> Targets;
    FString Mode;
    bool bTaggedOnly = false;
    FVector Axis = FVector::UpVector;
    double Direction = 1, Speed = 90, Distance = 100, Angle = 90, Duration = 2, Elapsed = 0;
};
