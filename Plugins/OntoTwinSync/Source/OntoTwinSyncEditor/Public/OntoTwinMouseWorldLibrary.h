#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "OntoTwinMouseWorldLibrary.generated.h"

/** Editor-only helpers used by the OntoTwin mouse world-coordinate utility widget. */
UCLASS()
class ONTOTWINSYNCEDITOR_API UOntoTwinMouseWorldLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Gets the world coordinate under the mouse in the active Level Editor viewport.
	 * Orthographic misses use DepthAxisValue for the view's depth axis; perspective
	 * misses return false because they do not define a unique world position.
	 */
	UFUNCTION(BlueprintCallable, Category = "OntoTwin|Editor Tools|Mouse World",
		meta = (DisplayName = "Get Cursor World Location (Editor)"))
	static bool GetCursorWorldLocation(
		FVector& OutLocation,
		FVector& OutNormal,
		FString& OutHitActorName,
		FString& OutViewTypeName,
		bool& bOutHitObject,
		float DepthAxisValue = 0.f);
};
