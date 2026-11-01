#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Features/IModularFeatures.h"

class UWorld;

/**
 * Optional runtime adapter for the WebSocket client that actually owns an
 * external data stream. Entity-specific plugins (people, vehicles, etc.) can
 * register an implementation without making OntoTwinSync depend on them.
 */
class ONTOTWINSYNC_API IOntoTwinRealtimeStreamProvider : public IModularFeature
{
public:
    static FName GetModularFeatureName()
    {
        static const FName Name(TEXT("OntoTwinRealtimeStreamProvider"));
        return Name;
    }

    virtual UWorld* GetRealtimeStreamWorld() const = 0;
    virtual FString GetRealtimeStreamId() const = 0;
    virtual void ApplyRealtimeStreamEnabled(bool bEnabled) = 0;
    virtual TSharedRef<FJsonObject> BuildRealtimeStreamHealth() const = 0;
};
