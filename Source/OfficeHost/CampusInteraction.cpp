#include "CampusInteraction.h"
#include "ProceduralMeshComponent.h"

ACampusInteraction::ACampusInteraction()
{
    PrimaryActorTick.bCanEverTick = false;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("CampusPlacement"));
    Skin = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("SharedBuildingGlass"));
    Rim = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("SharedBuildingOutline"));
    for (UProceduralMeshComponent* Mesh : {Skin.Get(), Rim.Get()})
    {
        Mesh->SetupAttachment(RootComponent);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetGenerateOverlapEvents(false);
        Mesh->SetCanEverAffectNavigation(false);
        Mesh->SetCastShadow(false);
        Mesh->SetVisibility(false);
    }
}

void ACampusInteraction::BeginPlay()
{
    Super::BeginPlay();
    Bounds.Reserve(Shapes.Num());
    for (const FCampusBuildingShape& Shape : Shapes)
        Bounds.Add(FBox(Shape.Vertices));
    Skin->SetMaterial(0, GlassMaterial);
    Rim->SetMaterial(0, OutlineMaterial);
}

int32 ACampusInteraction::Pick(const FVector& Start, const FVector& End, float& Distance) const
{
    const FVector A = GetActorTransform().InverseTransformPosition(Start);
    const FVector B = GetActorTransform().InverseTransformPosition(End);
    int32 Result = INDEX_NONE;
    Distance = TNumericLimits<float>::Max();
    for (int32 Index = 0; Index < Shapes.Num(); ++Index)
    {
        const FVector Offset = ShapeOffset(Index);
        const FVector LocalA = A - Offset;
        const FVector LocalB = B - Offset;
        if (!Bounds.IsValidIndex(Index) || !FMath::LineBoxIntersection(Bounds[Index], LocalA, LocalB, LocalB - LocalA)) continue;
        const FCampusBuildingShape& Shape = Shapes[Index];
        for (int32 T = 0; T + 2 < Shape.Triangles.Num(); T += 3)
        {
            const int32 I = Shape.Triangles[T], J = Shape.Triangles[T+1], K = Shape.Triangles[T+2];
            if (!Shape.Vertices.IsValidIndex(I) || !Shape.Vertices.IsValidIndex(J) || !Shape.Vertices.IsValidIndex(K)) continue;
            FVector Hit, Normal;
            if (FMath::SegmentTriangleIntersection(LocalA, LocalB, Shape.Vertices[I], Shape.Vertices[J], Shape.Vertices[K], Hit, Normal))
            {
                const float D = FVector::Distance(Start, GetActorTransform().TransformPosition(Hit + Offset));
                if (D < Distance) { Distance = D; Result = Index; }
            }
        }
    }
    return Result;
}

FVector ACampusInteraction::GetShapeWorldCenter(int32 Index) const
{
    return Bounds.IsValidIndex(Index) ? GetActorTransform().TransformPosition(Bounds[Index].GetCenter() + ShapeOffset(Index)) : GetActorLocation();
}

FVector ACampusInteraction::GetShapeWorldTopCenter(int32 Index) const
{
    if (!Bounds.IsValidIndex(Index)) return GetActorLocation();
    FVector Point = Bounds[Index].GetCenter() + ShapeOffset(Index);
    Point.Z = Bounds[Index].Max.Z + ShapeOffset(Index).Z;
    return GetActorTransform().TransformPosition(Point);
}

FVector ACampusInteraction::ShapeOffset(int32 Index) const
{
    return Shapes.IsValidIndex(Index) && Shapes[Index].Key == TEXT("GrandNeoBay") ? MainBuildingLocalOffset : FVector::ZeroVector;
}

void ACampusInteraction::SetMainBuildingWorldOffset(const FVector& WorldOffset)
{
    const FVector LocalOffset = GetActorTransform().InverseTransformVector(WorldOffset);
    if (MainBuildingLocalOffset.Equals(LocalOffset, .01f)) return;
    MainBuildingLocalOffset = LocalOffset;
    Displayed = INDEX_NONE; // Force a rebuild if the main shell is already visible.
}

void ACampusInteraction::Show(int32 Hovered, int32 Selected)
{
    const int32 Next = Selected != INDEX_NONE ? Selected : Hovered;
    if (Next == Displayed) return;
    Displayed = Shapes.IsValidIndex(Next) ? Next : INDEX_NONE;
    Skin->SetVisibility(Displayed != INDEX_NONE);
    Rim->SetVisibility(Displayed != INDEX_NONE);
    if (Displayed == INDEX_NONE) return;
    const FCampusBuildingShape& Shape = Shapes[Displayed];
    const FVector Offset = ShapeOffset(Displayed);
    TArray<FVector> PositionedVertices;
    PositionedVertices.Reserve(Shape.Vertices.Num());
    for (const FVector& Vertex : Shape.Vertices) PositionedVertices.Add(Vertex + Offset);
    Skin->CreateMeshSection_LinearColor(0, PositionedVertices, Shape.Triangles, {}, {}, {}, {}, false);
    TArray<FVector> V;
    TArray<int32> T;
    for (int32 E = 0; E + 1 < Shape.Edges.Num(); E += 2)
    {
        const FVector A = Shape.Edges[E] + Offset, B = Shape.Edges[E+1] + Offset;
        const FVector Direction = (B-A).GetSafeNormal();
        FVector U, W;
        Direction.FindBestAxisVectors(U, W);
        U *= 7.f; W *= 7.f;
        const int32 O = V.Num();
        for (const FVector& P : {A, B})
            V.Append({P-U-W, P+U-W, P+U+W, P-U+W});
        for (int32 I : {0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,1,2,6,1,6,5,2,3,7,2,7,6,3,0,4,3,4,7}) T.Add(O+I);
    }
    Rim->CreateMeshSection_LinearColor(0, V, T, {}, {}, {}, {}, false);
}
