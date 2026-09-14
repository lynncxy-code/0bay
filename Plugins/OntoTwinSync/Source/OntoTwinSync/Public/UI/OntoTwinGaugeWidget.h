#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "OntoTwinGaugeWidget.generated.h"

class SOntoTwinGaugeWidget;

/** Lightweight cached Slate gauge used by Overlay metric templates. */
UCLASS()
class ONTOTWINSYNC_API UOntoTwinGaugeWidget : public UWidget
{
    GENERATED_BODY()

public:
    void SetGauge(float InNormalizedValue, const FLinearColor& InAccent, bool bInAvailable);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
    TSharedPtr<SOntoTwinGaugeWidget> GaugeWidget;
    float NormalizedValue = 0.0f;
    FLinearColor Accent = FLinearColor::White;
    bool bAvailable = false;
};
