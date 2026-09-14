#include "WebInteraction/OntoTwinWebBridge.h"

#include "WebInteraction/OntoTwinWebInteractionComponent.h"

void UOntoTwinWebBridge::Initialize(UOntoTwinWebInteractionComponent* InOwner)
{
    OwnerComponent = InOwner;
}

void UOntoTwinWebBridge::OnMessage(const FString& MessageJson)
{
    if (OwnerComponent)
    {
        OwnerComponent->HandleBridgeMessage(MessageJson);
    }
}
