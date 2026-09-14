#include "OntoTwinModelLoadingWidget.h"

#include "UI/OntoTwinGlassRenderer.h"
#include "UI/OntoTwinGlassTheme.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/BackgroundBlur.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/CoreStyle.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace
{
const FLinearColor LoadingNeutral(0.92f, 0.92f, 0.92f, 1.0f);
const FLinearColor LoadingGreen(0.10f, 0.58f, 0.36f, 1.0f);
const FLinearColor LoadingWarning(0.82f, 0.54f, 0.16f, 1.0f);
constexpr float WaitingProgressCeiling = 0.15f;
constexpr float ApplyingProgressCeiling = 0.95f;
constexpr float WaitingProgressPerSecond = 0.0075f;
constexpr float ApplyingProgressPerSecond = 0.85f;
constexpr float FinalProgressPerSecond = 0.35f;

float AdvanceWaitingProgress(const float Current, const float DeltaSeconds)
{
    return FMath::Min(
        WaitingProgressCeiling,
        Current + FMath::Max(0.0f, DeltaSeconds) * WaitingProgressPerSecond);
}

float CalculateApplyingProgressTarget(const int32 Completed, const int32 Total)
{
    const int32 SafeTotal = FMath::Max(1, Total);
    const int32 SafeCompleted = FMath::Clamp(Completed, 0, SafeTotal);
    const float Ratio = static_cast<float>(SafeCompleted) / static_cast<float>(SafeTotal);
    return WaitingProgressCeiling
        + (ApplyingProgressCeiling - WaitingProgressCeiling) * Ratio;
}

float KeepProgressMonotonic(const float Displayed, const float Proposed)
{
    return FMath::Max(
        FMath::Clamp(Displayed, 0.0f, 1.0f),
        FMath::Clamp(Proposed, 0.0f, 1.0f));
}
}

TSharedRef<SWidget> UOntoTwinModelLoadingWidget::RebuildWidget()
{
    if (!WidgetTree)
    {
        WidgetTree = NewObject<UWidgetTree>(this, TEXT("ModelLoadingWidgetTree"), RF_Transient);
    }
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        BuildDefaultLayout();
    }
    return Super::RebuildWidget();
}

void UOntoTwinModelLoadingWidget::NativeTick(
    const FGeometry& MyGeometry,
    const float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    if (!ProgressBar)
    {
        return;
    }

    if (FOntoTwinGlassRenderer::ShouldReduceMotion())
    {
        DisplayedPercent = TargetPercent;
        if (VisualPhase == ELoadingVisualPhase::Completing)
        {
            DisplayedPercent = 1.0f;
            VisualPhase = ELoadingVisualPhase::Finished;
        }
    }
    else
    {
        switch (VisualPhase)
        {
        case ELoadingVisualPhase::Waiting:
            DisplayedPercent = FMath::Min(
                TargetPercent,
                AdvanceWaitingProgress(DisplayedPercent, InDeltaTime));
            break;
        case ELoadingVisualPhase::Applying:
            DisplayedPercent = FMath::FInterpConstantTo(
                DisplayedPercent,
                TargetPercent,
                InDeltaTime,
                ApplyingProgressPerSecond);
            break;
        case ELoadingVisualPhase::Completing:
            if (DisplayedPercent < ApplyingProgressCeiling)
            {
                DisplayedPercent = FMath::FInterpConstantTo(
                    DisplayedPercent,
                    ApplyingProgressCeiling,
                    InDeltaTime,
                    ApplyingProgressPerSecond);
            }
            else
            {
                DisplayedPercent = FMath::FInterpConstantTo(
                    DisplayedPercent,
                    1.0f,
                    InDeltaTime,
                    FinalProgressPerSecond);
                if (DisplayedPercent >= 1.0f - KINDA_SMALL_NUMBER)
                {
                    DisplayedPercent = 1.0f;
                    VisualPhase = ELoadingVisualPhase::Finished;
                }
            }
            break;
        case ELoadingVisualPhase::Failed:
        case ELoadingVisualPhase::Finished:
            break;
        }
    }

    ProgressBar->SetPercent(FMath::Clamp(DisplayedPercent, 0.0f, 1.0f));
    RefreshProgressText();
}

void UOntoTwinModelLoadingWidget::BuildDefaultLayout()
{
    UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
        UCanvasPanel::StaticClass(), TEXT("LoadingCanvas"));
    Root->SetVisibility(ESlateVisibility::HitTestInvisible);
    WidgetTree->RootWidget = Root;

    UBorder* ScreenDim = WidgetTree->ConstructWidget<UBorder>(
        UBorder::StaticClass(), TEXT("LoadingScreenDim"));
    ScreenDim->SetBrushColor(FLinearColor(0.012f, 0.012f, 0.012f, 0.42f));
    ScreenDim->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UCanvasPanelSlot* DimSlot = Root->AddChildToCanvas(ScreenDim);
    DimSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
    DimSlot->SetOffsets(FMargin(0.0f));

    USizeBox* CardBounds = WidgetTree->ConstructWidget<USizeBox>(
        USizeBox::StaticClass(), TEXT("LoadingCardBounds"));
    CardBounds->SetWidthOverride(430.0f);
    CardBounds->SetMinDesiredHeight(150.0f);
    CardBounds->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

    UOverlay* GlassLayers = WidgetTree->ConstructWidget<UOverlay>(
        UOverlay::StaticClass(), TEXT("LoadingGlassLayers"));
    GlassLayers->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    CardBounds->AddChild(GlassLayers);

    constexpr float CardRadius = 20.0f;
    const FVector4 RoundedCorners(CardRadius, CardRadius, CardRadius, CardRadius);

    UBackgroundBlur* BackgroundBlur = WidgetTree->ConstructWidget<UBackgroundBlur>(
        UBackgroundBlur::StaticClass(), TEXT("LoadingBackgroundBlur"));
    BackgroundBlur->SetBlurStrength(18.0f);
    BackgroundBlur->SetApplyAlphaToBlur(true);
    BackgroundBlur->SetCornerRadius(RoundedCorners);
    BackgroundBlur->SetLowQualityFallbackBrush(FSlateRoundedBoxBrush(
        FOntoTwinGlassTheme::ScreenTint(EOntoTwinGlassQuality::Performance),
        CardRadius,
        FOntoTwinGlassTheme::Rim(),
        1.0f));
    BackgroundBlur->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UOverlaySlot* BlurSlot = GlassLayers->AddChildToOverlay(BackgroundBlur);
    BlurSlot->SetHorizontalAlignment(HAlign_Fill);
    BlurSlot->SetVerticalAlignment(VAlign_Fill);

    UBorder* Card = WidgetTree->ConstructWidget<UBorder>(
        UBorder::StaticClass(), TEXT("LoadingCard"));
    Card->SetBrush(FSlateRoundedBoxBrush(
        FOntoTwinGlassTheme::ScreenTint(EOntoTwinGlassQuality::Balanced),
        CardRadius,
        FOntoTwinGlassTheme::Rim(),
        1.0f));
    Card->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UOverlaySlot* CardLayerSlot = GlassLayers->AddChildToOverlay(Card);
    CardLayerSlot->SetHorizontalAlignment(HAlign_Fill);
    CardLayerSlot->SetVerticalAlignment(VAlign_Fill);

    UBorder* TopHighlight = WidgetTree->ConstructWidget<UBorder>(
        UBorder::StaticClass(), TEXT("LoadingTopHighlight"));
    TopHighlight->SetBrush(FSlateRoundedBoxBrush(
        FLinearColor(1.0f, 1.0f, 1.0f, 0.16f), 1.0f));
    TopHighlight->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    USizeBox* HighlightBounds = WidgetTree->ConstructWidget<USizeBox>(
        USizeBox::StaticClass(), TEXT("LoadingTopHighlightBounds"));
    HighlightBounds->SetHeightOverride(1.0f);
    HighlightBounds->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    HighlightBounds->AddChild(TopHighlight);
    UOverlaySlot* HighlightSlot = GlassLayers->AddChildToOverlay(HighlightBounds);
    HighlightSlot->SetHorizontalAlignment(HAlign_Fill);
    HighlightSlot->SetVerticalAlignment(VAlign_Top);
    HighlightSlot->SetPadding(FMargin(22.0f, 1.0f, 22.0f, 0.0f));

    UBorder* ContentContainer = WidgetTree->ConstructWidget<UBorder>(
        UBorder::StaticClass(), TEXT("LoadingContentContainer"));
    ContentContainer->SetBrush(FSlateColorBrush(FLinearColor::Transparent));
    ContentContainer->SetPadding(FMargin(24.0f, 18.0f));
    ContentContainer->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UOverlaySlot* ContentLayerSlot = GlassLayers->AddChildToOverlay(ContentContainer);
    ContentLayerSlot->SetHorizontalAlignment(HAlign_Fill);
    ContentLayerSlot->SetVerticalAlignment(VAlign_Fill);

    UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(
        UVerticalBox::StaticClass(), TEXT("LoadingContent"));
    Content->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    ContentContainer->SetContent(Content);

    TitleText = WidgetTree->ConstructWidget<UTextBlock>(
        UTextBlock::StaticClass(), TEXT("LoadingTitle"));
    TitleText->SetFont(FOntoTwinGlassTheme::Font(19.0f, true));
    TitleText->SetColorAndOpacity(FOntoTwinGlassTheme::PrimaryText());
    TitleText->SetText(FText::FromString(TEXT("正在加载数据模型")));
    UVerticalBoxSlot* TitleSlot = Content->AddChildToVerticalBox(TitleText);
    TitleSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 5.0f));

    DetailText = WidgetTree->ConstructWidget<UTextBlock>(
        UTextBlock::StaticClass(), TEXT("LoadingDetail"));
    DetailText->SetFont(FOntoTwinGlassTheme::Font(12.0f));
    DetailText->SetColorAndOpacity(FOntoTwinGlassTheme::SecondaryText());
    DetailText->SetAutoWrapText(true);
    DetailText->SetText(FText::FromString(TEXT("正在连接 OntoTwin，场景交互将在模型加载完成后启动。")));
    UVerticalBoxSlot* DetailSlot = Content->AddChildToVerticalBox(DetailText);
    DetailSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 14.0f));

    ProgressBar = WidgetTree->ConstructWidget<UProgressBar>(
        UProgressBar::StaticClass(), TEXT("ModelLoadingProgress"));
    FProgressBarStyle ProgressStyle =
        FCoreStyle::Get().GetWidgetStyle<FProgressBarStyle>(TEXT("ProgressBar"));
    ProgressStyle.SetBackgroundImage(FSlateRoundedBoxBrush(
        FLinearColor(0.03f, 0.03f, 0.03f, 0.72f), 4.0f));
    ProgressStyle.SetFillImage(FSlateRoundedBoxBrush(LoadingNeutral, 4.0f));
    ProgressStyle.SetMarqueeImage(FSlateRoundedBoxBrush(LoadingNeutral, 4.0f));
    ProgressBar->SetWidgetStyle(ProgressStyle);
    ProgressBar->SetFillColorAndOpacity(LoadingNeutral);
    ProgressBar->SetPercent(0.0f);
    ProgressBar->SetIsMarquee(true);

    USizeBox* ProgressBounds = WidgetTree->ConstructWidget<USizeBox>(
        USizeBox::StaticClass(), TEXT("ProgressBounds"));
    ProgressBounds->SetHeightOverride(7.0f);
    ProgressBounds->AddChild(ProgressBar);
    UVerticalBoxSlot* ProgressSlot = Content->AddChildToVerticalBox(ProgressBounds);
    ProgressSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));

    PercentText = WidgetTree->ConstructWidget<UTextBlock>(
        UTextBlock::StaticClass(), TEXT("LoadingPercent"));
    PercentText->SetFont(FOntoTwinGlassTheme::Font(11.0f, true));
    PercentText->SetColorAndOpacity(FOntoTwinGlassTheme::SecondaryText());
    PercentText->SetJustification(ETextJustify::Right);
    PercentText->SetText(FText::FromString(TEXT("正在等待模型数据…")));
    Content->AddChildToVerticalBox(PercentText);

    UCanvasPanelSlot* CardSlot = Root->AddChildToCanvas(CardBounds);
    CardSlot->SetAnchors(FAnchors(0.5f, 0.5f));
    CardSlot->SetAlignment(FVector2D(0.5f, 0.5f));
    CardSlot->SetAutoSize(true);
}

void UOntoTwinModelLoadingWidget::SetState(
    const FString& Title,
    const FString& Detail,
    const float Percent,
    const bool bMarquee,
    const FLinearColor& Accent)
{
    if (TitleText) TitleText->SetText(FText::FromString(Title));
    if (DetailText) DetailText->SetText(FText::FromString(Detail));
    if (ProgressBar)
    {
        ProgressBar->SetIsMarquee(bMarquee);
        ProgressBar->SetFillColorAndOpacity(Accent);
        TargetPercent = KeepProgressMonotonic(DisplayedPercent, Percent);
        ProgressBar->SetPercent(FMath::Clamp(DisplayedPercent, 0.0f, 1.0f));
    }
    if (PercentText) PercentText->SetColorAndOpacity(Accent);
}

void UOntoTwinModelLoadingWidget::RefreshProgressText()
{
    if (!PercentText)
    {
        return;
    }

    switch (VisualPhase)
    {
    case ELoadingVisualPhase::Waiting:
        PercentText->SetText(FText::FromString(TEXT("正在等待模型数据…")));
        break;
    case ELoadingVisualPhase::Applying:
        PercentText->SetText(FText::FromString(FString::Printf(
            TEXT("已处理 %d / %d    总体 %d%%"),
            LatestCompleted,
            LatestTotal,
            FMath::RoundToInt(DisplayedPercent * 100.0f))));
        break;
    case ELoadingVisualPhase::Completing:
        PercentText->SetText(FText::FromString(FString::Printf(
            TEXT("正在完成场景准备…    %d%%"),
            FMath::RoundToInt(DisplayedPercent * 100.0f))));
        break;
    case ELoadingVisualPhase::Failed:
        PercentText->SetText(FText::FromString(TEXT("正在自动重试…")));
        break;
    case ELoadingVisualPhase::Finished:
        if (TitleText) TitleText->SetText(FText::FromString(TEXT("模型加载完成")));
        if (DetailText) DetailText->SetText(FText::FromString(
            TEXT("数据模型已就绪，正在进入场景。")));
        PercentText->SetText(FText::FromString(FString::Printf(
            TEXT("已加载 %d / %d    100%%"), LatestTotal, LatestTotal)));
        break;
    }
}

void UOntoTwinModelLoadingWidget::ShowWaiting(const FString& Detail)
{
    VisualPhase = ELoadingVisualPhase::Waiting;
    SetState(
        TEXT("正在准备数据模型"),
        Detail,
        WaitingProgressCeiling,
        false,
        LoadingNeutral);
    RefreshProgressText();
}

void UOntoTwinModelLoadingWidget::ShowProgress(const int32 Completed, const int32 Total)
{
    const float Percent = CalculateApplyingProgressTarget(Completed, Total);
    VisualPhase = ELoadingVisualPhase::Applying;
    LatestCompleted = FMath::Clamp(Completed, 0, FMath::Max(0, Total));
    LatestTotal = FMath::Max(0, Total);
    SetState(
        TEXT("正在加载数据模型"),
        TEXT("模型加载完成后将启动当前项目配置的场景交互。"),
        Percent,
        false,
        LoadingNeutral);
    RefreshProgressText();
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FOntoTwinModelLoadingProgressMathTest,
    "OntoTwin.UI.ModelLoading.ProgressMath",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOntoTwinModelLoadingProgressMathTest::RunTest(const FString& Parameters)
{
    TestTrue(
        TEXT("等待二十秒到达 15% 封顶"),
        FMath::IsNearlyEqual(AdvanceWaitingProgress(0.0f, 20.0f), 0.15f, 0.0001f));
    TestTrue(
        TEXT("等待超过二十秒不突破 15%"),
        FMath::IsNearlyEqual(AdvanceWaitingProgress(0.14f, 20.0f), 0.15f, 0.0001f));
    TestTrue(
        TEXT("实例处理起点映射为 15%"),
        FMath::IsNearlyEqual(CalculateApplyingProgressTarget(0, 100), 0.15f, 0.0001f));
    TestTrue(
        TEXT("实例处理一半映射为 55%"),
        FMath::IsNearlyEqual(CalculateApplyingProgressTarget(50, 100), 0.55f, 0.0001f));
    TestTrue(
        TEXT("实例处理完成映射为 95%"),
        FMath::IsNearlyEqual(CalculateApplyingProgressTarget(100, 100), 0.95f, 0.0001f));
    TestTrue(
        TEXT("重试目标不能让显示进度倒退"),
        FMath::IsNearlyEqual(KeepProgressMonotonic(0.72f, 0.31f), 0.72f, 0.0001f));
    return true;
}
#endif

void UOntoTwinModelLoadingWidget::ShowFailure(const FString& Detail)
{
    VisualPhase = ELoadingVisualPhase::Failed;
    SetState(
        TEXT("模型暂未加载完成"),
        Detail,
        DisplayedPercent,
        !FOntoTwinGlassRenderer::ShouldReduceMotion(),
        LoadingWarning);
    RefreshProgressText();
}

void UOntoTwinModelLoadingWidget::ShowComplete(const int32 Total)
{
    VisualPhase = ELoadingVisualPhase::Completing;
    LatestCompleted = FMath::Max(0, Total);
    LatestTotal = FMath::Max(0, Total);
    SetState(
        TEXT("正在完成数据模型"),
        TEXT("首轮模型基线已就绪，正在完成场景准备。"),
        1.0f,
        false,
        LoadingGreen);
    RefreshProgressText();
}
