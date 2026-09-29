#include "OfficeVR/InspectableComponent.h"
#include "Components/MeshComponent.h"

UInspectableComponent::UInspectableComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UInspectableComponent::SetHighlighted(bool bHighlighted)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	TArray<UMeshComponent*> Meshes;
	Owner->GetComponents<UMeshComponent>(Meshes);
	for (UMeshComponent* Mesh : Meshes)
	{
		if (!Mesh)
		{
			continue;
		}
		Mesh->SetRenderCustomDepth(bHighlighted);
		Mesh->SetCustomDepthStencilValue(bHighlighted ? 1 : 0);
	}
}
