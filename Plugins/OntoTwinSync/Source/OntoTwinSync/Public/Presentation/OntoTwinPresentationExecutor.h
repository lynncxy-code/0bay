#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Features/IModularFeatures.h"

class ATwinInstance;

/**
 * Optional runtime implementation for one semantic presentation route.
 *
 * OntoTwinSync owns normalization, route precedence, snapshot transport and
 * safe fallback.  Project and platform packages register this feature to
 * execute logical behaviour IDs without exposing UE asset paths to Nexus.
 */
class ONTOTWINSYNC_API IOntoTwinPresentationExecutor : public IModularFeature
{
public:
    static FName GetModularFeatureName()
    {
        static const FName Name(TEXT("OntoTwinPresentationExecutor"));
        return Name;
    }

    virtual FString GetPresentationExecutorId() const = 0;

    /** Higher priority wins when multiple executors claim the same route. */
    virtual int32 GetPresentationExecutorPriority() const { return 0; }

    virtual bool SupportsPresentationRoute(
        const FString& Channel,
        const FString& BehaviorId,
        const FString& Slot,
        const FString& Source) const = 0;

    virtual bool ExecutePresentationRoute(
        ATwinInstance* Twin,
        const FString& Channel,
        const FString& BehaviorId,
        const FString& Slot,
        const FString& Source,
        const TSharedPtr<FJsonObject>& Params) = 0;
};
