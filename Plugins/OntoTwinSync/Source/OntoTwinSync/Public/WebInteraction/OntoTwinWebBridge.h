#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "OntoTwinWebBridge.generated.h"

class UOntoTwinWebInteractionComponent;

/** Fixed Web Bridge 1.0 endpoint exposed as window.ue.ontotwinwebbridge. */
UCLASS()
class ONTOTWINSYNC_API UOntoTwinWebBridge : public UObject
{
    GENERATED_BODY()

public:
    void Initialize(UOntoTwinWebInteractionComponent* InOwner);

    UFUNCTION()
    void OnMessage(const FString& MessageJson);

private:
    UPROPERTY()
    UOntoTwinWebInteractionComponent* OwnerComponent = nullptr;
};
