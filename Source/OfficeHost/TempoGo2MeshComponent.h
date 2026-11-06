#pragma once
#include "CoreMinimal.h"
#include "Components/SkeletalMeshComponent.h"
#include "TempoGo2MeshComponent.generated.h"

// Locomotor translates the body beneath a stationary Root bone. Imported mesh
// bounds therefore cannot follow it; derive bounds from the evaluated bones.
UCLASS()
class OFFICEHOST_API UTempoGo2MeshComponent : public USkeletalMeshComponent
{
    GENERATED_BODY()
public:
    virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override
    {
        const auto& Pose=GetComponentSpaceTransforms();
        if(Pose.Num()<2)return Super::CalcBounds(LocalToWorld);
        FBox Box(ForceInit);
        for(int32 I=1;I<Pose.Num();I++)Box+=Pose[I].GetLocation();
        return FBoxSphereBounds(Box.ExpandBy(25.f)).TransformBy(LocalToWorld);
    }
};
