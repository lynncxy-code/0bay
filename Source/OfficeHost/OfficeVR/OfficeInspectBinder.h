#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "OfficeInspectBinder.generated.h"

UCLASS()
class OFFICEHOST_API UOfficeInspectBinder : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "OfficeVR")
	static int32 BindExistingActors(UWorld* World);

	UFUNCTION(BlueprintCallable, Category = "OfficeVR")
	static void EnsureDemoActors(UWorld* World);

	UFUNCTION(BlueprintCallable, Category = "OfficeVR")
	static void EnsureWalkCollision(UWorld* World);
};
