#pragma once

#include "CoreMinimal.h"
#include "InspectTypes.generated.h"

USTRUCT(BlueprintType)
struct FInspectableItemData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inspect")
	FString DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inspect")
	FString Category;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inspect")
	FString Room;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inspect")
	FString Dimensions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inspect")
	FString Material;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inspect")
	FString AssetId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inspect")
	FString SystemName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inspect")
	FString MaintenanceCycle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inspect")
	FString NextMaintenance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inspect")
	FString Description;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inspect")
	int32 CatalogId = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inspect")
	TArray<FString> MatchKeys;

	FString ToPanelText() const
	{
		return FString::Printf(
			TEXT("%s\n\n类别    %s\n区域    %s\n尺寸    %s\n材质    %s\n资产编号  %s\n所属系统  %s\n维保周期  %s\n下次维保  %s\n\n%s"),
			*DisplayName,
			*Category,
			*Room,
			*Dimensions,
			*Material,
			*AssetId,
			*SystemName,
			*MaintenanceCycle,
			*NextMaintenance,
			*Description);
	}
};
