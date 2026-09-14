#include "OntoTwinIndustrialBehaviorModule.h"

#include "GameFramework/Actor.h"
#include "TwinInstance.h"
#include "IndustrialPresentationComponent.h"

void FOntoTwinIndustrialBehaviorModule::StartupModule()
{
    IModularFeatures::Get().RegisterModularFeature(
        IOntoTwinPresentationExecutor::GetModularFeatureName(), this);
}

void FOntoTwinIndustrialBehaviorModule::ShutdownModule()
{
    if (IModularFeatures::Get().IsModularFeatureAvailable(
        IOntoTwinPresentationExecutor::GetModularFeatureName()))
    {
        IModularFeatures::Get().UnregisterModularFeature(
            IOntoTwinPresentationExecutor::GetModularFeatureName(), this);
    }
}

FString FOntoTwinIndustrialBehaviorModule::GetPresentationExecutorId() const
{
    return TEXT("platform.industrial.default");
}

int32 FOntoTwinIndustrialBehaviorModule::GetPresentationExecutorPriority() const
{
    return 10;
}

bool FOntoTwinIndustrialBehaviorModule::SupportsPresentationRoute(
    const FString& Channel,
    const FString& BehaviorId,
    const FString& Slot,
    const FString& Source) const
{
    // A project route must be handled by the project executor.  The platform
    // library only claims platform-resolved routes, preserving precedence.
    if (Source == TEXT("project"))
    {
        return false;
    }
    if (BehaviorId.StartsWith(TEXT("safe."))) return true;
    if (Channel == TEXT("label")) return BehaviorId == TEXT("industrial.label.demo");
    const bool bKnownSlot = Slot.IsEmpty()
        || Slot == TEXT("primary")
        || Slot == TEXT("motion")
        || Slot == TEXT("status_indicator")
        || Slot == TEXT("alarm")
        || Slot == TEXT("label");
    if (!bKnownSlot)
    {
        return false;
    }

    if (Channel == TEXT("animation"))
    {
        return BehaviorId == TEXT("industrial.machine.idle")
            || BehaviorId == TEXT("industrial.machine.running")
            || BehaviorId == TEXT("industrial.machine.fault")
            || BehaviorId == TEXT("industrial.machine.offline");
    }
    if (Channel == TEXT("visual"))
    {
        return BehaviorId == TEXT("industrial.visual.warning")
            || BehaviorId == TEXT("industrial.visual.critical")
            || BehaviorId == TEXT("industrial.visual.maintenance");
    }
    if (Channel == TEXT("fx") || Channel == TEXT("action"))
    {
        return BehaviorId == TEXT("industrial.fx.warning_flash")
            || BehaviorId == TEXT("industrial.fx.critical_flash")
            || BehaviorId == TEXT("industrial.fx.start_pulse");
    }
    return false;
}

bool FOntoTwinIndustrialBehaviorModule::ExecutePresentationRoute(
    ATwinInstance* Twin,
    const FString& Channel,
    const FString& BehaviorId,
    const FString& Slot,
    const FString& Source,
    const TSharedPtr<FJsonObject>& Params)
{
    if (!Twin || !SupportsPresentationRoute(Channel, BehaviorId, Slot, Source))
    {
        return false;
    }

    auto* Component = Twin->FindComponentByClass<UIndustrialPresentationComponent>();
    if (!Component && BehaviorId.StartsWith(TEXT("safe."))) return true;
    if (!Component)
    {
        Component = NewObject<UIndustrialPresentationComponent>(Twin, TEXT("OT_IndustrialPresentation"), RF_Transient);
        Twin->AddInstanceComponent(Component);
        Component->RegisterComponent();
    }
    const bool bApplied = Component->Apply(Channel, BehaviorId);
    UE_LOG(LogTemp, Log, TEXT("[OT-Presentation] instance=%s channel=%s behavior=%s applied=%d"),
        *Twin->GetInstanceId(), *Channel, *BehaviorId, bApplied);
    return bApplied;
}

IMPLEMENT_MODULE(FOntoTwinIndustrialBehaviorModule, OntoTwinIndustrialBehavior)
