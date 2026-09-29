#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "OfficeVR/InspectTypes.h"
#include "InspectCatalog.generated.h"

UCLASS()
class OFFICEHOST_API UInspectCatalog : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	static const TArray<FInspectableItemData>& GetItems();
	static bool FindForName(const FString& ActorOrMeshName, FInspectableItemData& OutItem);
	static FString InferCategory(const FString& DisplayName);
	static FString FormatDimensionsMm(const TArray<double>& Mm);
	static FInspectableItemData MakeHelpPlaque();
	static TArray<FInspectableItemData> MakeGateDemoItems();
};
