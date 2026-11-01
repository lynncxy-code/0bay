#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Presentation/OntoTwinPresentationExecutor.h"

/** Optional platform behavior library for OntoTwin 4.5 industrial equipment. */
class FOntoTwinIndustrialBehaviorModule final
    : public IModuleInterface
    , public IOntoTwinPresentationExecutor
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

    virtual FString GetPresentationExecutorId() const override;
    virtual int32 GetPresentationExecutorPriority() const override;
    virtual bool SupportsPresentationRoute(
        const FString& Channel,
        const FString& BehaviorId,
        const FString& Slot,
        const FString& Source) const override;
    virtual bool ExecutePresentationRoute(
        ATwinInstance* Twin,
        const FString& Channel,
        const FString& BehaviorId,
        const FString& Slot,
        const FString& Source,
        const TSharedPtr<FJsonObject>& Params) override;
};
