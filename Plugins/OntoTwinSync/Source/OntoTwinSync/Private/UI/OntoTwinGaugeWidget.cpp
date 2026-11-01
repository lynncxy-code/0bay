#include "UI/OntoTwinGaugeWidget.h"

#include "Rendering/DrawElements.h"
#include "Widgets/SLeafWidget.h"

class SOntoTwinGaugeWidget final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SOntoTwinGaugeWidget) {}
    SLATE_END_ARGS()

    void Construct(const FArguments&) {}

    void SetGauge(float InNormalizedValue, const FLinearColor& InAccent, bool bInAvailable)
    {
        NormalizedValue = FMath::Clamp(InNormalizedValue, 0.0f, 1.0f);
        Accent = InAccent;
        bAvailable = bInAvailable;
        Invalidate(EInvalidateWidgetReason::Paint);
    }

    virtual FVector2D ComputeDesiredSize(float) const override
    {
        return FVector2D(104.0f, 82.0f);
    }

    virtual int32 OnPaint(
        const FPaintArgs& Args,
        const FGeometry& AllottedGeometry,
        const FSlateRect& MyCullingRect,
        FSlateWindowElementList& OutDrawElements,
        int32 LayerId,
        const FWidgetStyle& InWidgetStyle,
        bool bParentEnabled) const override
    {
        constexpr int32 SegmentCount = 48;
        constexpr float StartDegrees = 150.0f;
        constexpr float SweepDegrees = 240.0f;
        const FVector2D Size = AllottedGeometry.GetLocalSize();
        const FVector2D Center(Size.X * 0.5f, Size.Y * 0.57f);
        const float Radius = FMath::Max(4.0f, FMath::Min(Size.X * 0.43f, Size.Y * 0.48f));
        TArray<FVector2D> TrackPoints;
        TArray<FVector2D> ValuePoints;
        TrackPoints.Reserve(SegmentCount + 1);
        ValuePoints.Reserve(SegmentCount + 1);
        const int32 ActiveSegments = bAvailable
            ? FMath::Clamp(FMath::CeilToInt(NormalizedValue * SegmentCount), 0, SegmentCount)
            : 0;
        for (int32 Index = 0; Index <= SegmentCount; ++Index)
        {
            const float Angle = FMath::DegreesToRadians(
                StartDegrees + SweepDegrees * static_cast<float>(Index) / SegmentCount);
            const FVector2D Point = Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius;
            TrackPoints.Add(Point);
            if (Index <= ActiveSegments)
            {
                ValuePoints.Add(Point);
            }
        }

        const bool bEnabled = ShouldBeEnabled(bParentEnabled);
        const ESlateDrawEffect DrawEffects = bEnabled
            ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
        FSlateDrawElement::MakeLines(
            OutDrawElements,
            LayerId,
            AllottedGeometry.ToPaintGeometry(),
            TrackPoints,
            DrawEffects,
            FLinearColor(1.0f, 1.0f, 1.0f, 0.14f) * InWidgetStyle.GetColorAndOpacityTint(),
            true,
            6.0f);
        if (ValuePoints.Num() > 1)
        {
            FSlateDrawElement::MakeLines(
                OutDrawElements,
                LayerId + 1,
                AllottedGeometry.ToPaintGeometry(),
                ValuePoints,
                DrawEffects,
                Accent * InWidgetStyle.GetColorAndOpacityTint(),
                true,
                6.0f);
        }
        return LayerId + 1;
    }

private:
    float NormalizedValue = 0.0f;
    FLinearColor Accent = FLinearColor::White;
    bool bAvailable = false;
};

void UOntoTwinGaugeWidget::SetGauge(
    float InNormalizedValue, const FLinearColor& InAccent, bool bInAvailable)
{
    NormalizedValue = FMath::Clamp(InNormalizedValue, 0.0f, 1.0f);
    Accent = InAccent;
    bAvailable = bInAvailable;
    if (GaugeWidget.IsValid())
    {
        GaugeWidget->SetGauge(NormalizedValue, Accent, bAvailable);
    }
}

TSharedRef<SWidget> UOntoTwinGaugeWidget::RebuildWidget()
{
    GaugeWidget = SNew(SOntoTwinGaugeWidget);
    GaugeWidget->SetGauge(NormalizedValue, Accent, bAvailable);
    return GaugeWidget.ToSharedRef();
}

void UOntoTwinGaugeWidget::ReleaseSlateResources(bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren);
    GaugeWidget.Reset();
}
