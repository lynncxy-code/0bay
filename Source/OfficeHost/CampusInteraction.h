#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CampusInteraction.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;

/** Local centimetres, triangulated footprint extrusion. No physics or office input. */
USTRUCT(BlueprintType)
struct FCampusBuildingShape
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere) FString Key;
    UPROPERTY(EditAnywhere) TArray<FVector> Vertices;
    UPROPERTY(EditAnywhere) TArray<int32> Triangles;
    /** Pairs of endpoints for roof / footprint / corner edges. */
    UPROPERTY(EditAnywhere) TArray<FVector> Edges;
};

/** One lightweight picking database and two shared highlight meshes for the whole campus. */
UCLASS()
class OFFICEHOST_API ACampusInteraction : public AActor
{
    GENERATED_BODY()
public:
    ACampusInteraction();
    virtual void BeginPlay() override;
    UPROPERTY(EditAnywhere, Category="Campus") TArray<FCampusBuildingShape> Shapes;
    UPROPERTY(EditAnywhere, Category="Campus") TObjectPtr<UMaterialInterface> GlassMaterial;
    UPROPERTY(EditAnywhere, Category="Campus") TObjectPtr<UMaterialInterface> OutlineMaterial;
    int32 Pick(const FVector& Start, const FVector& End, float& Distance) const;
    void Show(int32 Hovered, int32 Selected);
    FVector GetShapeWorldCenter(int32 Index) const;
    FVector GetShapeWorldTopCenter(int32 Index) const;
    /** The imported main-building pick shell follows its separately placed UE actor. */
    void SetMainBuildingWorldOffset(const FVector& WorldOffset);
    int32 GetDisplayedShape() const { return Displayed; }
private:
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> Skin;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> Rim;
    TArray<FBox> Bounds;
    int32 Displayed = INDEX_NONE;
    FVector MainBuildingLocalOffset = FVector::ZeroVector;
    FVector ShapeOffset(int32 Index) const;
};
