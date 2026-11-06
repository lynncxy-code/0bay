#include "SceneInteraction/OntoTwinCrosshairWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"

namespace
{
const FLinearColor ReticleIdle(0.72f, 0.91f, 0.97f, 0.72f);
const FLinearColor ReticleInteractive(0.12f, 0.90f, 1.0f, 1.0f);
}

TSharedRef<SWidget> UOntoTwinCrosshairWidget::RebuildWidget()
{
    if (!WidgetTree)
    {
        WidgetTree = NewObject<UWidgetTree>(this, TEXT("CrosshairWidgetTree"), RF_Transient);
    }
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        BuildDefaultLayout();
    }
    TSharedRef<SWidget> Result = Super::RebuildWidget();
    ApplyReticleState();
    return Result;
}

void UOntoTwinCrosshairWidget::BuildDefaultLayout()
{
    USizeBox* Bounds = WidgetTree->ConstructWidget<USizeBox>(
        USizeBox::StaticClass(), TEXT("CrosshairBounds"));
    Bounds->SetWidthOverride(46.0f);
    Bounds->SetHeightOverride(46.0f);
    Bounds->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    Bounds->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
    WidgetTree->RootWidget = Bounds;

    UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(
        UCanvasPanel::StaticClass(), TEXT("CrosshairCanvas"));
    Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    Bounds->AddChild(Canvas);

    // Four open corner brackets keep the target visible while still defining
    // a precise interaction zone. The four short cardinal ticks add an AR HUD cue.
    AddSegment(Canvas, TEXT("TopLeftH"), FVector2D(4.0f, 7.0f), FVector2D(10.0f, 1.0f));
    AddSegment(Canvas, TEXT("TopLeftV"), FVector2D(4.0f, 7.0f), FVector2D(1.0f, 10.0f));
    AddSegment(Canvas, TEXT("TopRightH"), FVector2D(32.0f, 7.0f), FVector2D(10.0f, 1.0f));
    AddSegment(Canvas, TEXT("TopRightV"), FVector2D(41.0f, 7.0f), FVector2D(1.0f, 10.0f));
    AddSegment(Canvas, TEXT("BottomLeftH"), FVector2D(4.0f, 38.0f), FVector2D(10.0f, 1.0f));
    AddSegment(Canvas, TEXT("BottomLeftV"), FVector2D(4.0f, 29.0f), FVector2D(1.0f, 10.0f));
    AddSegment(Canvas, TEXT("BottomRightH"), FVector2D(32.0f, 38.0f), FVector2D(10.0f, 1.0f));
    AddSegment(Canvas, TEXT("BottomRightV"), FVector2D(41.0f, 29.0f), FVector2D(1.0f, 10.0f));
    AddSegment(Canvas, TEXT("TopTick"), FVector2D(22.5f, 2.0f), FVector2D(1.0f, 4.0f));
    AddSegment(Canvas, TEXT("BottomTick"), FVector2D(22.5f, 40.0f), FVector2D(1.0f, 4.0f));
    AddSegment(Canvas, TEXT("LeftTick"), FVector2D(0.0f, 22.5f), FVector2D(4.0f, 1.0f));
    AddSegment(Canvas, TEXT("RightTick"), FVector2D(42.0f, 22.5f), FVector2D(4.0f, 1.0f));
    AddSegment(Canvas, TEXT("CenterDot"), FVector2D(22.0f, 22.0f), FVector2D(2.0f, 2.0f));
}

UBorder* UOntoTwinCrosshairWidget::AddSegment(
    UCanvasPanel* Canvas,
    const FName Name,
    const FVector2D& Position,
    const FVector2D& Size)
{
    UBorder* Segment = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), Name);
    Segment->SetBrushColor(ReticleIdle);
    Segment->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UCanvasPanelSlot* CanvasSlot = Canvas->AddChildToCanvas(Segment);
    CanvasSlot->SetPosition(Position);
    CanvasSlot->SetSize(Size);
    ReticleSegments.Add(Segment);
    return Segment;
}

void UOntoTwinCrosshairWidget::SetReticleState(bool bVisible, bool bInteractive)
{
    bPendingVisible = bVisible;
    bPendingInteractive = bInteractive;
    ApplyReticleState();
}

void UOntoTwinCrosshairWidget::ApplyReticleState()
{
    SetVisibility(bPendingVisible
        ? ESlateVisibility::SelfHitTestInvisible
        : ESlateVisibility::Collapsed);
    const FLinearColor Color = bPendingInteractive ? ReticleInteractive : ReticleIdle;
    for (UBorder* Segment : ReticleSegments)
    {
        if (Segment) Segment->SetBrushColor(Color);
    }
    if (UWidget* Root = WidgetTree ? WidgetTree->RootWidget : nullptr)
    {
        Root->SetRenderScale(
            bPendingInteractive ? FVector2D(1.10f, 1.10f) : FVector2D(1.0f, 1.0f));
    }
}
