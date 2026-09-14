#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MouseWorldLibrary.generated.h"

UCLASS()
class DIGITALFACTORYBASEEDITOR_API UMouseWorldLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "EditorTools|MouseWorld",
        meta = (DisplayName = "Get Cursor World Location (Editor)"))
    static bool GetCursorWorldLocation(
        FVector& OutLocation,
        FVector& OutNormal,
        FString& OutHitActorName,
        FString& OutViewTypeName,
        bool& bOutHitObject,
        float DepthAxisValue = 0.0f);
};
