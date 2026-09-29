#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OfficeVR/InspectTypes.h"
#include "InspectableComponent.generated.h"

UCLASS(ClassGroup = (OfficeVR), meta = (BlueprintSpawnableComponent))
class OFFICEHOST_API UInspectableComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInspectableComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inspect")
	FInspectableItemData Item;

	void SetHighlighted(bool bHighlighted);

	FString GetPanelText() const { return Item.ToPanelText(); }
};
