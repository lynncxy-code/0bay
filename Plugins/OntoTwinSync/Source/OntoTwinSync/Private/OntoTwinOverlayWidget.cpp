#include "OntoTwinOverlayWidget.h"
#include "UI/OntoTwinGlassRenderer.h"
#include "UI/OntoTwinGlassTheme.h"
#include "UI/OntoTwinGaugeWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Blueprint/AsyncTaskDownloadImage.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/BackgroundBlur.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/GridPanel.h"
#include "Components/GridSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2DDynamic.h"
#include "Engine/World.h"
#include "Styling/SlateColor.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
const FLinearColor OverlayBackground(0.055f, 0.055f, 0.055f, 0.94f);
const FLinearColor GlassRim = FOntoTwinGlassTheme::Rim();
const FLinearColor PrimaryText = FOntoTwinGlassTheme::PrimaryText();
const FLinearColor SecondaryText = FOntoTwinGlassTheme::SecondaryText();
const FLinearColor MutedText = FOntoTwinGlassTheme::MutedText();
constexpr float TextPanelWidth = 360.0f;
constexpr float SubtitlePanelWidth = 400.0f;
constexpr float MetricsPanelWidth = 340.0f;
constexpr float StatusMetricsPanelWidth = 360.0f;
constexpr float MediaPanelWidth = 480.0f;
constexpr float ExpandedPanelWidth = 720.0f;
constexpr float ScreenPanelMinHeight = 80.0f;
constexpr float WorldRenderDensity = 2.0f;
constexpr float GlassCornerRadius = 22.0f;
constexpr float BalancedBlurStrength = 14.0f;
constexpr float OpenDurationSeconds = 0.21f;
constexpr float CloseDurationSeconds = 0.15f;
constexpr float InteractionDurationSeconds = 0.14f;
constexpr float StatePulseDurationSeconds = 0.80f;
constexpr float VisualTickSeconds = 1.0f / 60.0f;

const FName TemplateTitleBody(TEXT("title_body"));
const FName TemplateTitleSubtitleBody(TEXT("title_subtitle_body"));
const FName TemplateTitleMetrics(TEXT("title_metrics"));
const FName TemplateTitleStatusMetrics(TEXT("title_status_metrics"));
const FName TemplateTitleVideo(TEXT("title_video"));
const FName TemplateTitleVideoBody(TEXT("title_video_body"));

FButtonStyle BuildGlassControlStyle()
{
    FButtonStyle Style;
    Style.SetNormal(FSlateRoundedBoxBrush(
        FLinearColor(1.0f, 1.0f, 1.0f, 0.06f),
        8.0f,
        FLinearColor(1.0f, 1.0f, 1.0f, 0.14f),
        1.0f));
    Style.SetHovered(FSlateRoundedBoxBrush(
        FLinearColor(1.0f, 1.0f, 1.0f, 0.11f),
        8.0f,
        FLinearColor(1.0f, 1.0f, 1.0f, 0.28f),
        1.0f));
    Style.SetPressed(FSlateRoundedBoxBrush(
        FLinearColor(1.0f, 1.0f, 1.0f, 0.16f),
        8.0f,
        FLinearColor(1.0f, 1.0f, 1.0f, 0.34f),
        1.0f));
    Style.SetDisabled(FSlateRoundedBoxBrush(
        FLinearColor(1.0f, 1.0f, 1.0f, 0.03f),
        8.0f,
        FLinearColor(1.0f, 1.0f, 1.0f, 0.08f),
        1.0f));
    Style.SetNormalPadding(FMargin(7.0f, 5.0f));
    Style.SetPressedPadding(FMargin(7.0f, 6.0f, 7.0f, 4.0f));
    return Style;
}

FName NormalizeTemplateId(const FString& Value)
{
    FString Normalized = Value;
    Normalized.TrimStartAndEndInline();
    Normalized.ToLowerInline();
    const FName Candidate(*Normalized);
    if (Candidate == TemplateTitleBody
        || Candidate == TemplateTitleSubtitleBody
        || Candidate == TemplateTitleMetrics
        || Candidate == TemplateTitleStatusMetrics
        || Candidate == TemplateTitleVideo
        || Candidate == TemplateTitleVideoBody)
    {
        return Candidate;
    }
    return TemplateTitleBody;
}

bool TemplateUsesSubtitle(const FName TemplateId)
{
    return TemplateId == TemplateTitleSubtitleBody;
}

bool TemplateUsesBody(const FName TemplateId)
{
    return TemplateId == TemplateTitleBody
        || TemplateId == TemplateTitleSubtitleBody
        || TemplateId == TemplateTitleVideoBody;
}

bool TemplateUsesStatus(const FName TemplateId)
{
    return TemplateId == TemplateTitleStatusMetrics;
}

bool TemplateUsesMetrics(const FName TemplateId)
{
    return TemplateId == TemplateTitleMetrics
        || TemplateId == TemplateTitleStatusMetrics;
}

bool TemplateUsesMedia(const FName TemplateId)
{
    return TemplateId == TemplateTitleVideo
        || TemplateId == TemplateTitleVideoBody;
}

float RenderDensityForDecision(
    const FOntoTwinGlassDecision& Decision, const bool bWorldSpace)
{
    if (!bWorldSpace) return 1.0f;
    if (Decision.EffectiveQuality == EOntoTwinGlassQuality::High) return WorldRenderDensity;
    if (Decision.EffectiveQuality == EOntoTwinGlassQuality::Balanced) return 1.5f;
    return 1.0f;
}

FLinearColor WorldTintForQuality(const EOntoTwinGlassQuality Quality)
{
    return FOntoTwinGlassTheme::WorldTint(Quality);
}

FString SlotDisplayValue(const TSharedPtr<FJsonObject>& Slots, const TCHAR* SlotName)
{
    if (!Slots.IsValid()) return FString();
    const TSharedPtr<FJsonObject>* Slot = nullptr;
    FString Value;
    if (Slots->TryGetObjectField(SlotName, Slot) && Slot && Slot->IsValid())
    {
        (*Slot)->TryGetStringField(TEXT("display_value"), Value);
    }
    return Value;
}
}

UOntoTwinOverlayWidget::UOntoTwinOverlayWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    static const TCHAR* MaterialPaths[] = {
        TEXT("/OntoTwinSync/UI/RendererSpike/M_OT_GlassHigh_RT0.M_OT_GlassHigh_RT0"),
        TEXT("/OntoTwinSync/UI/RendererSpike/M_OT_GlassHigh_RT1.M_OT_GlassHigh_RT1"),
        TEXT("/OntoTwinSync/UI/RendererSpike/M_OT_GlassHigh_RT2.M_OT_GlassHigh_RT2"),
        TEXT("/OntoTwinSync/UI/RendererSpike/M_OT_GlassHigh_RT3.M_OT_GlassHigh_RT3"),
        TEXT("/OntoTwinSync/UI/RendererSpike/M_OT_GlassHigh_RT4.M_OT_GlassHigh_RT4"),
    };
    CookedHighGlassMaterials.Reserve(UE_ARRAY_COUNT(MaterialPaths));
    for (const TCHAR* MaterialPath : MaterialPaths)
    {
        ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(MaterialPath);
        if (MaterialFinder.Succeeded())
        {
            CookedHighGlassMaterials.Add(MaterialFinder.Object);
        }
    }
}

TSharedRef<SWidget> UOntoTwinOverlayWidget::RebuildWidget()
{
    if (!WidgetTree)
    {
        WidgetTree = NewObject<UWidgetTree>(this, TEXT("OverlayWidgetTree"), RF_Transient);
    }
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        BuildDefaultLayout();
    }
    TSharedRef<SWidget> Result = Super::RebuildWidget();
    ApplyPendingData();
    return Result;
}

void UOntoTwinOverlayWidget::NativeConstruct()
{
    Super::NativeConstruct();
    SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
    StartOpenAnimation();
}

void UOntoTwinOverlayWidget::NativeDestruct()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(VisualAnimationTimer);
    }
    CloseAnimationCompletion = nullptr;
    Super::NativeDestruct();
}

bool UOntoTwinOverlayWidget::IsReducedMotion() const
{
    if (bWorldSpacePresentation || FOntoTwinGlassRenderer::ShouldReduceMotion())
    {
        return true;
    }
    return FOntoTwinGlassRenderer::Resolve(
        bWorldSpacePresentation,
        RequestedGlassQuality).EffectiveQuality == EOntoTwinGlassQuality::Performance;
}

void UOntoTwinOverlayWidget::StartOpenAnimation()
{
    if (bWorldSpacePresentation)
    {
        VisualPhase = EOntoTwinOverlayVisualPhase::Idle;
        SetRenderOpacity(1.0f);
        SetRenderScale(FVector2D(1.0f, 1.0f));
        return;
    }

    VisualPhase = EOntoTwinOverlayVisualPhase::Opening;
    VisualPhaseElapsed = 0.0f;
    SetRenderOpacity(0.0f);
    const float StartScale = IsReducedMotion() ? 1.0f : 0.97f;
    SetRenderScale(FVector2D(StartScale, StartScale));
    EnsureVisualAnimationTimer();
}

void UOntoTwinOverlayWidget::PlayCloseAnimation(TFunction<void()> Completion)
{
    if (bWorldSpacePresentation || !GetWorld())
    {
        if (Completion) Completion();
        return;
    }

    CloseAnimationCompletion = MoveTemp(Completion);
    VisualPhase = EOntoTwinOverlayVisualPhase::Closing;
    VisualPhaseElapsed = 0.0f;
    InteractionTarget = 0.0f;
    EnsureVisualAnimationTimer();
}

void UOntoTwinOverlayWidget::EnsureVisualAnimationTimer()
{
    if (bWorldSpacePresentation) return;
    if (UWorld* World = GetWorld())
    {
        FTimerManager& TimerManager = World->GetTimerManager();
        if (!TimerManager.IsTimerActive(VisualAnimationTimer))
        {
            TimerManager.SetTimer(
                VisualAnimationTimer,
                this,
                &UOntoTwinOverlayWidget::TickVisualEffects,
                VisualTickSeconds,
                true);
        }
    }
}

void UOntoTwinOverlayWidget::TickVisualEffects()
{
    UWorld* World = GetWorld();
    if (!World || bWorldSpacePresentation)
    {
        if (World) World->GetTimerManager().ClearTimer(VisualAnimationTimer);
        return;
    }

    const float DeltaSeconds = FMath::Max(World->GetDeltaSeconds(), VisualTickSeconds);
    bool bKeepTicking = false;

    if (!FMath::IsNearlyEqual(InteractionProgress, InteractionTarget, 0.001f))
    {
        const float Step = DeltaSeconds / InteractionDurationSeconds;
        InteractionProgress = FMath::Clamp(
            InteractionProgress
                + (InteractionTarget > InteractionProgress ? Step : -Step),
            0.0f,
            1.0f);
        bKeepTicking = true;
    }

    float TransitionOpacity = 1.0f;
    float TransitionScale = 1.0f;
    if (VisualPhase == EOntoTwinOverlayVisualPhase::Opening)
    {
        VisualPhaseElapsed += DeltaSeconds;
        const float T = FMath::Clamp(
            VisualPhaseElapsed / OpenDurationSeconds, 0.0f, 1.0f);
        const float Ease = 1.0f - FMath::Pow(1.0f - T, 3.0f);
        TransitionOpacity = Ease;
        TransitionScale = IsReducedMotion() ? 1.0f : FMath::Lerp(0.97f, 1.0f, Ease);
        if (T >= 1.0f)
        {
            VisualPhase = EOntoTwinOverlayVisualPhase::Idle;
            VisualPhaseElapsed = 0.0f;
        }
        else
        {
            bKeepTicking = true;
        }
    }
    else if (VisualPhase == EOntoTwinOverlayVisualPhase::Closing)
    {
        VisualPhaseElapsed += DeltaSeconds;
        const float T = FMath::Clamp(
            VisualPhaseElapsed / CloseDurationSeconds, 0.0f, 1.0f);
        const float Ease = T * T * (3.0f - 2.0f * T);
        TransitionOpacity = 1.0f - Ease;
        TransitionScale = IsReducedMotion() ? 1.0f : FMath::Lerp(1.0f, 0.985f, Ease);
        if (T >= 1.0f)
        {
            VisualPhase = EOntoTwinOverlayVisualPhase::Idle;
            VisualPhaseElapsed = 0.0f;
            World->GetTimerManager().ClearTimer(VisualAnimationTimer);
            TFunction<void()> Completion = MoveTemp(CloseAnimationCompletion);
            CloseAnimationCompletion = nullptr;
            if (Completion) Completion();
            return;
        }
        bKeepTicking = true;
    }

    if (StatePulseElapsed >= 0.0f)
    {
        StatePulseElapsed += DeltaSeconds;
        if (StatePulseElapsed >= StatePulseDurationSeconds || IsReducedMotion())
        {
            StatePulseElapsed = -1.0f;
        }
        else
        {
            bKeepTicking = true;
        }
    }

    ApplyTransientVisuals(TransitionOpacity, TransitionScale);
    if (!bKeepTicking)
    {
        World->GetTimerManager().ClearTimer(VisualAnimationTimer);
    }
}

void UOntoTwinOverlayWidget::ApplyTransientVisuals(
    const float TransitionOpacity,
    const float TransitionScale)
{
    const float OnlineOpacity = bPanelOnline ? 1.0f : 0.68f;
    const float InteractionEase =
        InteractionProgress * InteractionProgress * (3.0f - 2.0f * InteractionProgress);
    float Pulse = 0.0f;
    if (StatePulseElapsed >= 0.0f && !IsReducedMotion())
    {
        const float T = FMath::Clamp(
            StatePulseElapsed / StatePulseDurationSeconds, 0.0f, 1.0f);
        Pulse = FMath::Sin(PI * T) * FMath::Exp(-1.65f * T);
    }

    SetRenderOpacity(FMath::Clamp(TransitionOpacity, 0.0f, 1.0f));
    SetRenderScale(FVector2D(TransitionScale, TransitionScale));
    if (PanelBorder) PanelBorder->SetRenderOpacity(OnlineOpacity);
    if (ContentContainer) ContentContainer->SetRenderOpacity(OnlineOpacity);
    if (HighGlassSurface) HighGlassSurface->SetRenderOpacity(OnlineOpacity);
    if (BalancedBackgroundBlur) BalancedBackgroundBlur->SetRenderOpacity(OnlineOpacity);
    if (FineNoiseLayer)
    {
        FineNoiseLayer->SetRenderOpacity(BaseFineNoiseOpacity * OnlineOpacity);
    }
    if (TopHighlight)
    {
        TopHighlight->SetRenderOpacity(FMath::Clamp(
            BaseTopHighlightOpacity + 0.14f * InteractionEase + 0.08f * Pulse,
            0.0f,
            0.48f) * OnlineOpacity);
    }
    if (InteractionRim)
    {
        InteractionRim->SetRenderOpacity(
            (0.18f + 0.82f * InteractionEase) * InteractionEase * OnlineOpacity);
    }
    if (StatusAccent)
    {
        StatusAccent->SetRenderOpacity(0.0f);
    }
    if (StatusDotBounds)
    {
        const float DotScale = 1.0f + 0.30f * Pulse;
        StatusDotBounds->SetRenderScale(FVector2D(DotScale, DotScale));
    }
    if (StatusDot)
    {
        StatusDot->SetRenderOpacity((0.88f + 0.12f * Pulse) * OnlineOpacity);
    }
}

UTextBlock* UOntoTwinOverlayWidget::CreateText(
    const FName Name, int32 FontSize, const FLinearColor& Color, bool bBold) const
{
    if (!WidgetTree) return nullptr;
    UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
    Text->SetColorAndOpacity(FSlateColor(Color));
    Text->SetFont(FOntoTwinGlassTheme::Font(FontSize, bBold));
    Text->SetAutoWrapText(true);
    return Text;
}

void UOntoTwinOverlayWidget::BuildDefaultLayout()
{
    if (!WidgetTree) return;

    OverlayBounds = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("OverlayBounds"));
    OverlayBounds->SetWidthOverride(TextPanelWidth);
    OverlayBounds->SetMinDesiredHeight(ScreenPanelMinHeight);
    WidgetTree->RootWidget = OverlayBounds;

    GlassLayerRoot = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("GlassLayerRoot"));
    GlassLayerRoot->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    OverlayBounds->AddChild(GlassLayerRoot);

    HighGlassSurface = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("HighGlassSurface"));
    HighGlassSurface->SetVisibility(ESlateVisibility::Collapsed);
    UOverlaySlot* HighGlassSlot = GlassLayerRoot->AddChildToOverlay(HighGlassSurface);
    HighGlassSlot->SetHorizontalAlignment(HAlign_Fill);
    HighGlassSlot->SetVerticalAlignment(VAlign_Fill);

    BalancedBackgroundBlur = WidgetTree->ConstructWidget<UBackgroundBlur>(
        UBackgroundBlur::StaticClass(), TEXT("BalancedBackgroundBlur"));
    BalancedBackgroundBlur->SetVisibility(ESlateVisibility::Collapsed);
    BalancedBackgroundBlur->SetBlurStrength(BalancedBlurStrength);
    BalancedBackgroundBlur->SetApplyAlphaToBlur(true);
    UOverlaySlot* BlurSlot = GlassLayerRoot->AddChildToOverlay(BalancedBackgroundBlur);
    BlurSlot->SetHorizontalAlignment(HAlign_Fill);
    BlurSlot->SetVerticalAlignment(VAlign_Fill);

    PanelBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("OverlayPanel"));
    PanelBorder->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    PanelBorder->SetBrush(FSlateRoundedBoxBrush(OverlayBackground, 6.0f));
    UOverlaySlot* PanelSlot = GlassLayerRoot->AddChildToOverlay(PanelBorder);
    PanelSlot->SetHorizontalAlignment(HAlign_Fill);
    PanelSlot->SetVerticalAlignment(VAlign_Fill);

    FineNoiseLayer = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("OverlayFineNoise"));
    FineNoiseLayer->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    if (UTexture2D* FineNoiseTexture = FOntoTwinGlassTheme::FineNoiseTexture())
    {
        FSlateBrush NoiseBrush;
        NoiseBrush.DrawAs = ESlateBrushDrawType::Image;
        NoiseBrush.ImageSize = FVector2D(32.0f, 32.0f);
        NoiseBrush.Tiling = ESlateBrushTileType::Both;
        NoiseBrush.TintColor = FSlateColor(FLinearColor::White);
        NoiseBrush.SetResourceObject(FineNoiseTexture);
        FineNoiseLayer->SetBrush(NoiseBrush);
    }
    UOverlaySlot* NoiseSlot = GlassLayerRoot->AddChildToOverlay(FineNoiseLayer);
    NoiseSlot->SetHorizontalAlignment(HAlign_Fill);
    NoiseSlot->SetVerticalAlignment(VAlign_Fill);

    TopHighlight = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("OverlayTopHighlight"));
    TopHighlight->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    TopHighlight->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White, 1.0f));
    TopHighlightBounds = WidgetTree->ConstructWidget<USizeBox>(
        USizeBox::StaticClass(), TEXT("OverlayTopHighlightBounds"));
    TopHighlightBounds->SetHeightOverride(1.0f);
    TopHighlightBounds->AddChild(TopHighlight);
    UOverlaySlot* TopHighlightSlot = GlassLayerRoot->AddChildToOverlay(TopHighlightBounds);
    TopHighlightSlot->SetHorizontalAlignment(HAlign_Fill);
    TopHighlightSlot->SetVerticalAlignment(VAlign_Top);
    TopHighlightSlot->SetPadding(FMargin(18.0f, 1.0f, 18.0f, 0.0f));

    StatusAccent = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("OverlayStatusAccent"));
    StatusAccent->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    StatusAccent->SetBrush(FSlateRoundedBoxBrush(MutedText, 1.0f));
    StatusAccentBounds = WidgetTree->ConstructWidget<USizeBox>(
        USizeBox::StaticClass(), TEXT("OverlayStatusAccentBounds"));
    StatusAccentBounds->SetWidthOverride(2.0f);
    StatusAccentBounds->AddChild(StatusAccent);
    StatusAccentBounds->SetVisibility(ESlateVisibility::Collapsed);
    UOverlaySlot* StatusAccentSlot = GlassLayerRoot->AddChildToOverlay(StatusAccentBounds);
    StatusAccentSlot->SetHorizontalAlignment(HAlign_Left);
    StatusAccentSlot->SetVerticalAlignment(VAlign_Fill);
    StatusAccentSlot->SetPadding(FMargin(1.0f, 18.0f, 0.0f, 18.0f));

    InteractionRim = WidgetTree->ConstructWidget<UBorder>(
        UBorder::StaticClass(), TEXT("OverlayInteractionRim"));
    InteractionRim->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    InteractionRim->SetBrush(FSlateRoundedBoxBrush(
        FLinearColor::Transparent,
        GlassCornerRadius,
        FLinearColor(1.0f, 1.0f, 1.0f, 0.30f),
        1.0f));
    InteractionRim->SetRenderOpacity(0.0f);
    UOverlaySlot* InteractionSlot = GlassLayerRoot->AddChildToOverlay(InteractionRim);
    InteractionSlot->SetHorizontalAlignment(HAlign_Fill);
    InteractionSlot->SetVerticalAlignment(VAlign_Fill);

    ContentContainer = WidgetTree->ConstructWidget<UBorder>(
        UBorder::StaticClass(), TEXT("OverlayContentContainer"));
    ContentContainer->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    ContentContainer->SetBrush(FSlateColorBrush(FLinearColor::Transparent));
    ContentContainer->SetPadding(FMargin(14.0f, 12.0f));
    UOverlaySlot* ContentSlot = GlassLayerRoot->AddChildToOverlay(ContentContainer);
    ContentSlot->SetHorizontalAlignment(HAlign_Fill);
    ContentSlot->SetVerticalAlignment(VAlign_Fill);

    ContentStack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("OverlayStack"));
    ContentStack->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    ContentContainer->SetContent(ContentStack);

    TitleText = CreateText(TEXT("OverlayTitle"), 15, PrimaryText, true);

    SubtitleText = CreateText(TEXT("OverlaySubtitle"), 11, SecondaryText);
    SubtitleText->SetVisibility(ESlateVisibility::Collapsed);

    MediaBounds = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("OverlayMediaBounds"));
    UOverlay* MediaOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("OverlayMedia"));
    MediaBounds->AddChild(MediaOverlay);

    MediaImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("OverlayMediaImage"));
    UOverlaySlot* ImageSlot = MediaOverlay->AddChildToOverlay(MediaImage);
    ImageSlot->SetHorizontalAlignment(HAlign_Fill);
    ImageSlot->SetVerticalAlignment(VAlign_Fill);

    MediaStateText = CreateText(TEXT("OverlayMediaState"), 11, SecondaryText, true);
    MediaStateText->SetJustification(ETextJustify::Center);
    UOverlaySlot* MediaStateSlot = MediaOverlay->AddChildToOverlay(MediaStateText);
    MediaStateSlot->SetHorizontalAlignment(HAlign_Center);
    MediaStateSlot->SetVerticalAlignment(VAlign_Center);
    MediaBounds->SetVisibility(ESlateVisibility::Collapsed);

    MediaControls = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("OverlayMediaControls"));
    MediaControls->SetVisibility(ESlateVisibility::Collapsed);

    const auto AddControl = [this](const TCHAR* ButtonName, const TCHAR* TextValue,
                                   UButton*& OutButton, UTextBlock*& OutText)
    {
        OutButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(ButtonName));
        OutButton->SetStyle(BuildGlassControlStyle());
        OutButton->SetBackgroundColor(FLinearColor::White);
        OutText = CreateText(FName(*FString(ButtonName).Append(TEXT("Text"))), 10, PrimaryText, true);
        OutText->SetText(FText::FromString(TextValue));
        OutText->SetJustification(ETextJustify::Center);
        OutButton->AddChild(OutText);
        UHorizontalBoxSlot* ButtonSlot = MediaControls->AddChildToHorizontalBox(OutButton);
        ButtonSlot->SetPadding(FMargin(0.0f, 0.0f, 6.0f, 0.0f));
        ButtonSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    };
    AddControl(TEXT("OverlayPlayPause"), TEXT("播放"), PlayPauseButton, PlayPauseButtonText);
    AddControl(TEXT("OverlayMute"), TEXT("静音"), MuteButton, MuteButtonText);
    AddControl(TEXT("OverlayExpand"), TEXT("放大"), ExpandButton, ExpandButtonText);
    UTextBlock* RetryText = nullptr;
    AddControl(TEXT("OverlayRetry"), TEXT("重试"), RetryButton, RetryText);
    RetryButton->SetVisibility(ESlateVisibility::Collapsed);
    UTextBlock* CloseText = nullptr;
    AddControl(TEXT("OverlayClose"), TEXT("关闭"), CloseButton, CloseText);

    PlayPauseButton->OnClicked.AddDynamic(this, &UOntoTwinOverlayWidget::HandlePlayPauseClicked);
    MuteButton->OnClicked.AddDynamic(this, &UOntoTwinOverlayWidget::HandleMuteClicked);
    ExpandButton->OnClicked.AddDynamic(this, &UOntoTwinOverlayWidget::HandleExpandClicked);
    RetryButton->OnClicked.AddDynamic(this, &UOntoTwinOverlayWidget::HandleRetryClicked);
    CloseButton->OnClicked.AddDynamic(this, &UOntoTwinOverlayWidget::HandleCloseClicked);

    BodyText = CreateText(TEXT("OverlayBody"), 12, SecondaryText);
    BodyText->SetVisibility(ESlateVisibility::Collapsed);

    StatusRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("OverlayStatusRow"));
    StatusRow->SetVisibility(ESlateVisibility::Collapsed);

    StatusDot = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("OverlayStatusDot"));
    StatusDot->SetBrush(FSlateRoundedBoxBrush(MutedText, 3.0f));
    StatusDotBounds = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("OverlayStatusDotBounds"));
    StatusDotBounds->SetWidthOverride(6.0f);
    StatusDotBounds->SetHeightOverride(6.0f);
    StatusDotBounds->AddChild(StatusDot);
    UHorizontalBoxSlot* DotSlot = StatusRow->AddChildToHorizontalBox(StatusDotBounds);
    DotSlot->SetPadding(FMargin(0.0f, 0.0f, 7.0f, 0.0f));
    DotSlot->SetVerticalAlignment(VAlign_Center);

    StatusSemanticText = CreateText(TEXT("OverlayStatusSemantic"), 10, MutedText, true);
    UHorizontalBoxSlot* SemanticSlot = StatusRow->AddChildToHorizontalBox(StatusSemanticText);
    SemanticSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
    SemanticSlot->SetVerticalAlignment(VAlign_Center);

    StatusText = CreateText(TEXT("OverlayStatusText"), 12, PrimaryText);
    StatusRow->AddChildToHorizontalBox(StatusText);

    GaugeRegion = WidgetTree->ConstructWidget<UVerticalBox>(
        UVerticalBox::StaticClass(), TEXT("OverlayGaugeRegion"));
    GaugeRegion->SetVisibility(ESlateVisibility::Collapsed);
    GaugeRow = WidgetTree->ConstructWidget<UHorizontalBox>(
        UHorizontalBox::StaticClass(), TEXT("OverlayGaugeRow"));
    GaugeRegion->AddChildToVerticalBox(GaugeRow);
    USizeBox* GaugeBounds = WidgetTree->ConstructWidget<USizeBox>(
        USizeBox::StaticClass(), TEXT("OverlayGaugeBounds"));
    GaugeBounds->SetWidthOverride(104.0f);
    GaugeBounds->SetHeightOverride(82.0f);
    GaugeWidget = WidgetTree->ConstructWidget<UOntoTwinGaugeWidget>(
        UOntoTwinGaugeWidget::StaticClass(), TEXT("OverlayGauge"));
    GaugeBounds->AddChild(GaugeWidget);
    UHorizontalBoxSlot* GaugeBoundsSlot = GaugeRow->AddChildToHorizontalBox(GaugeBounds);
    GaugeBoundsSlot->SetVerticalAlignment(VAlign_Center);
    GaugeBoundsSlot->SetPadding(FMargin(0.0f, 0.0f, 12.0f, 0.0f));

    UVerticalBox* GaugeCopy = WidgetTree->ConstructWidget<UVerticalBox>(
        UVerticalBox::StaticClass(), TEXT("OverlayGaugeCopy"));
    GaugeLabel = CreateText(TEXT("OverlayGaugeLabel"), 10, MutedText);
    GaugeValue = CreateText(TEXT("OverlayGaugeValue"), 22, PrimaryText, true);
    GaugeRange = CreateText(TEXT("OverlayGaugeRange"), 9, MutedText);
    GaugeCopy->AddChildToVerticalBox(GaugeLabel);
    GaugeCopy->AddChildToVerticalBox(GaugeValue);
    GaugeCopy->AddChildToVerticalBox(GaugeRange);
    UHorizontalBoxSlot* GaugeCopySlot = GaugeRow->AddChildToHorizontalBox(GaugeCopy);
    GaugeCopySlot->SetVerticalAlignment(VAlign_Center);
    GaugeCopySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

    GaugeFallback = CreateText(TEXT("OverlayGaugeFallback"), 9, MutedText);
    GaugeFallback->SetText(FText::FromString(TEXT("仪表数据不可用，已按数值显示")));
    GaugeFallback->SetVisibility(ESlateVisibility::Collapsed);
    GaugeRegion->AddChildToVerticalBox(GaugeFallback);

    MetricsGrid = WidgetTree->ConstructWidget<UGridPanel>(UGridPanel::StaticClass(), TEXT("OverlayMetrics"));
    MetricsGrid->SetColumnFill(0, 0.45f);
    MetricsGrid->SetColumnFill(1, 0.55f);
    MetricsGrid->SetVisibility(ESlateVisibility::Collapsed);

    for (int32 Index = 0; Index < 4; ++Index)
    {
        UTextBlock* Label = CreateText(FName(*FString::Printf(TEXT("MetricLabel%d"), Index)), 10, MutedText);
        UGridSlot* LabelSlot = MetricsGrid->AddChildToGrid(Label, Index, 0);
        LabelSlot->SetPadding(FMargin(0.0f, 4.0f, 8.0f, 4.0f));

        UTextBlock* Value = CreateText(FName(*FString::Printf(TEXT("MetricValue%d"), Index)), 12, PrimaryText);
        Value->SetJustification(ETextJustify::Right);
        UGridSlot* ValueSlot = MetricsGrid->AddChildToGrid(Value, Index, 1);
        ValueSlot->SetPadding(FMargin(8.0f, 4.0f, 0.0f, 4.0f));

        MetricLabels.Add(Label);
        MetricValues.Add(Value);
        MetricEmphasis.Add(false);
    }

    ApplyTemplateRecipe(TemplateTitleBody.ToString(), true);
    ApplyPresentationStyle();
}

void UOntoTwinOverlayWidget::ApplyTemplateRecipe(const FString& TemplateId, bool bForce)
{
    if (!ContentStack) return;

    const FName ResolvedTemplateId = NormalizeTemplateId(TemplateId);
    if (!bForce && ResolvedTemplateId == ActiveTemplateId
        && ContentStack->GetChildrenCount() > 0)
    {
        return;
    }

    ActiveTemplateId = ResolvedTemplateId;
    ContentStack->ClearChildren();

    const auto AddBlock = [this](UWidget* Widget, float TopPadding)
    {
        if (!Widget) return;
        UVerticalBoxSlot* Slot = ContentStack->AddChildToVerticalBox(Widget);
        if (Slot)
        {
            Slot->SetPadding(FMargin(0.0f, TopPadding, 0.0f, 0.0f));
        }
    };

    AddBlock(TitleText, 0.0f);
    if (TemplateUsesSubtitle(ActiveTemplateId))
    {
        AddBlock(SubtitleText, 2.0f);
    }
    if (TemplateUsesMedia(ActiveTemplateId))
    {
        AddBlock(MediaBounds, 8.0f);
    }
    if (TemplateUsesBody(ActiveTemplateId))
    {
        AddBlock(BodyText, 8.0f);
    }
    if (TemplateUsesStatus(ActiveTemplateId))
    {
        AddBlock(StatusRow, 8.0f);
    }
    if (TemplateUsesMetrics(ActiveTemplateId))
    {
        AddBlock(GaugeRegion, 8.0f);
        AddBlock(MetricsGrid, 8.0f);
    }
    if (TemplateUsesMedia(ActiveTemplateId) && !bWorldSpacePresentation)
    {
        AddBlock(MediaControls, 8.0f);
    }
    InvalidateLayoutAndVolatility();
}

float UOntoTwinOverlayWidget::GetBasePanelWidth() const
{
    if (ActiveTemplateId == TemplateTitleSubtitleBody) return SubtitlePanelWidth;
    if (ActiveTemplateId == TemplateTitleMetrics) return MetricsPanelWidth;
    if (ActiveTemplateId == TemplateTitleStatusMetrics) return StatusMetricsPanelWidth;
    if (TemplateUsesMedia(ActiveTemplateId)) return MediaPanelWidth;
    return TextPanelWidth;
}

float UOntoTwinOverlayWidget::GetMinimumPanelHeight() const
{
    // Text-only cards fit the visible text instead of reserving a body row.
    return ActiveTemplateId == TemplateTitleBody || ActiveTemplateId == TemplateTitleSubtitleBody
        ? 0.0f : ScreenPanelMinHeight;
}

void UOntoTwinOverlayWidget::SetWorldSpacePresentation(bool bEnabled)
{
    if (bWorldSpacePresentation == bEnabled) return;
    bWorldSpacePresentation = bEnabled;
    if (bWorldSpacePresentation)
    {
        if (UWorld* World = GetWorld())
        {
            World->GetTimerManager().ClearTimer(VisualAnimationTimer);
        }
        VisualPhase = EOntoTwinOverlayVisualPhase::Idle;
        VisualPhaseElapsed = 0.0f;
        InteractionProgress = 0.0f;
        InteractionTarget = 0.0f;
        StatePulseElapsed = -1.0f;
        SetRenderOpacity(1.0f);
        SetRenderScale(FVector2D(1.0f, 1.0f));
    }
    ApplyTemplateRecipe(ActiveTemplateId.ToString(), true);
    ApplyPresentationStyle();
}

FVector2D UOntoTwinOverlayWidget::GetDesiredRenderSize()
{
    ForceLayoutPrepass();
    FVector2D Desired = GetDesiredSize();
    const FOntoTwinGlassDecision GlassDecision =
        FOntoTwinGlassRenderer::Resolve(bWorldSpacePresentation, RequestedGlassQuality);
    const float Density = RenderDensityForDecision(GlassDecision, bWorldSpacePresentation);
    const float PanelWidth = bMediaExpanded && !bWorldSpacePresentation
        ? ExpandedPanelWidth : GetBasePanelWidth();
    Desired.X = FMath::Max(Desired.X, PanelWidth * Density);
    Desired.Y = FMath::Clamp(
        Desired.Y,
        GetMinimumPanelHeight() * Density,
        (TemplateUsesMedia(ActiveTemplateId) ? 720.0f : 360.0f) * Density);
    return FVector2D(FMath::CeilToFloat(Desired.X), FMath::CeilToFloat(Desired.Y));
}

void UOntoTwinOverlayWidget::ApplyPresentationStyle()
{
    if (!OverlayBounds || !PanelBorder || !TitleText) return;

    const FOntoTwinGlassDecision GlassDecision =
        FOntoTwinGlassRenderer::Resolve(bWorldSpacePresentation, RequestedGlassQuality);
    const float Density = RenderDensityForDecision(GlassDecision, bWorldSpacePresentation);
    const auto FontSize = [this, Density](int32 ScreenSize, int32 WorldSize)
    {
        return FMath::RoundToInt((bWorldSpacePresentation ? WorldSize : ScreenSize) * Density);
    };
    const auto SetFont = [](UTextBlock* Text, int32 Size, bool bBold = false)
    {
        if (Text)
        {
            Text->SetFont(FOntoTwinGlassTheme::Font(Size, bBold));
        }
    };

    const float PanelWidth = bMediaExpanded && !bWorldSpacePresentation
        ? ExpandedPanelWidth : GetBasePanelWidth();
    OverlayBounds->SetWidthOverride(PanelWidth * Density);
    OverlayBounds->SetMinDesiredHeight(GetMinimumPanelHeight() * Density);
    if (ContentContainer)
    {
        ContentContainer->SetPadding(FMargin(14.0f * Density, 12.0f * Density));
    }

    const bool bUseHighScreen = !bWorldSpacePresentation
        && GlassDecision.EffectiveQuality == EOntoTwinGlassQuality::High
        && GlassDecision.HighMaterial != nullptr;
    const bool bUseBalancedScreen = !bWorldSpacePresentation
        && GlassDecision.EffectiveQuality == EOntoTwinGlassQuality::Balanced;

    const float CornerRadius = GlassCornerRadius * Density;
    const FVector4 RoundedCorners(CornerRadius, CornerRadius, CornerRadius, CornerRadius);
    HighGlassMaterial = bUseHighScreen ? GlassDecision.HighMaterial : nullptr;
    if (HighGlassSurface)
    {
        if (bUseHighScreen)
        {
            FSlateRoundedBoxBrush HighGlassBrush(FLinearColor::White, RoundedCorners);
            HighGlassBrush.ImageType = ESlateBrushImageType::FullColor;
            HighGlassBrush.SetResourceObject(HighGlassMaterial);
            HighGlassSurface->SetBrush(HighGlassBrush);
        }
        else
        {
            HighGlassSurface->SetBrush(FSlateBrush());
        }
        HighGlassSurface->SetColorAndOpacity(FLinearColor::White);
        HighGlassSurface->SetVisibility(
            bUseHighScreen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    }

    if (BalancedBackgroundBlur)
    {
        BalancedBackgroundBlur->SetBlurStrength(BalancedBlurStrength);
        BalancedBackgroundBlur->SetCornerRadius(RoundedCorners);
        BalancedBackgroundBlur->SetLowQualityFallbackBrush(FSlateRoundedBoxBrush(
            FOntoTwinGlassTheme::ScreenTint(EOntoTwinGlassQuality::Performance),
            CornerRadius, GlassRim, 1.0f * Density));
        BalancedBackgroundBlur->SetVisibility(
            bUseBalancedScreen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    }

    FLinearColor PanelTint = bWorldSpacePresentation
        ? WorldTintForQuality(GlassDecision.EffectiveQuality)
        : FOntoTwinGlassTheme::ScreenTint(GlassDecision.EffectiveQuality);
    FLinearColor RimColor = GlassRim;
    const bool bHighContrast = FOntoTwinGlassRenderer::ShouldUseHighContrast();
    if (bHighContrast)
    {
        PanelTint.A = FMath::Clamp(PanelTint.A + 0.12f, 0.0f, 0.96f);
        RimColor.A = 0.38f;
    }
    PanelBorder->SetBrush(FSlateRoundedBoxBrush(
        PanelTint, CornerRadius, RimColor, 1.0f * Density));

    CurrentRenderDensity = Density;
    if (FineNoiseLayer)
    {
        if (GlassDecision.EffectiveQuality == EOntoTwinGlassQuality::Performance)
        {
            BaseFineNoiseOpacity = 0.0f;
        }
        else if (bWorldSpacePresentation)
        {
            BaseFineNoiseOpacity =
                GlassDecision.EffectiveQuality == EOntoTwinGlassQuality::High ? 0.020f : 0.012f;
        }
        else
        {
            BaseFineNoiseOpacity =
                GlassDecision.EffectiveQuality == EOntoTwinGlassQuality::High ? 0.018f : 0.012f;
        }
        FineNoiseLayer->SetVisibility(BaseFineNoiseOpacity > 0.0f
            ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    }
    BaseTopHighlightOpacity =
        GlassDecision.EffectiveQuality == EOntoTwinGlassQuality::High ? 0.22f
        : GlassDecision.EffectiveQuality == EOntoTwinGlassQuality::Balanced ? 0.15f
        : 0.07f;
    if (bHighContrast)
    {
        BaseTopHighlightOpacity = FMath::Min(0.34f, BaseTopHighlightOpacity + 0.08f);
    }
    if (TopHighlightBounds)
    {
        TopHighlightBounds->SetHeightOverride(FMath::Max(1.0f, 1.0f * Density));
        if (UOverlaySlot* HighlightSlot = Cast<UOverlaySlot>(TopHighlightBounds->Slot))
        {
            HighlightSlot->SetPadding(FMargin(
                CornerRadius * 0.60f,
                FMath::Max(1.0f, 1.0f * Density),
                CornerRadius * 0.60f,
                0.0f));
        }
    }
    if (StatusAccentBounds)
    {
        StatusAccentBounds->SetWidthOverride(FMath::Max(2.0f, 2.0f * Density));
        if (UOverlaySlot* AccentSlot = Cast<UOverlaySlot>(StatusAccentBounds->Slot))
        {
            AccentSlot->SetPadding(FMargin(
                FMath::Max(1.0f, 1.0f * Density),
                CornerRadius * 0.70f,
                0.0f,
                CornerRadius * 0.70f));
        }
    }
    if (InteractionRim)
    {
        const FLinearColor InteractionColor(
            1.0f, 1.0f, 1.0f, bHighContrast ? 0.46f : 0.30f);
        InteractionRim->SetBrush(FSlateRoundedBoxBrush(
            FLinearColor::Transparent,
            CornerRadius,
            InteractionColor,
            FMath::Max(1.0f, 1.25f * Density)));
    }
    if (TopHighlight)
    {
        TopHighlight->SetBrush(FSlateRoundedBoxBrush(
            FLinearColor::White,
            FMath::Max(1.0f, 1.0f * Density)));
    }
    if (StatusAccent)
    {
        StatusAccent->SetBrush(FSlateRoundedBoxBrush(
            CurrentStatusAccent,
            FMath::Max(1.0f, 1.0f * Density)));
    }

    SetFont(TitleText, FontSize(15, 20), true);
    SetFont(SubtitleText, FontSize(11, 14));
    SetFont(BodyText, FontSize(12, 16));
    SetFont(StatusSemanticText, FontSize(10, 13), true);
    SetFont(StatusText, FontSize(12, 16));

    const float TextWrapWidth = (PanelWidth - 28.0f) * Density;
    TitleText->SetWrapTextAt(TextWrapWidth);
    SubtitleText->SetWrapTextAt(TextWrapWidth);
    BodyText->SetWrapTextAt(TextWrapWidth);
    StatusText->SetWrapTextAt(TextWrapWidth);

    if (UVerticalBoxSlot* LayoutSlot = Cast<UVerticalBoxSlot>(SubtitleText->Slot))
    {
        LayoutSlot->SetPadding(FMargin(0.0f, 2.0f * Density, 0.0f, 0.0f));
    }
    if (UVerticalBoxSlot* LayoutSlot = Cast<UVerticalBoxSlot>(BodyText->Slot))
    {
        LayoutSlot->SetPadding(FMargin(0.0f, 8.0f * Density, 0.0f, 0.0f));
    }
    if (MediaBounds)
    {
        MediaBounds->SetHeightOverride((PanelWidth - 28.0f) * Density * 9.0f / 16.0f);
        if (UVerticalBoxSlot* LayoutSlot = Cast<UVerticalBoxSlot>(MediaBounds->Slot))
        {
            LayoutSlot->SetPadding(FMargin(0.0f, 8.0f * Density, 0.0f, 0.0f));
        }
    }
    if (MediaStateText)
    {
        SetFont(MediaStateText, FontSize(11, 14), true);
        MediaStateText->SetWrapTextAt(FMath::Max(80.0f, TextWrapWidth - 24.0f * Density));
    }
    if (MediaControls)
    {
        if (UVerticalBoxSlot* LayoutSlot = Cast<UVerticalBoxSlot>(MediaControls->Slot))
        {
            LayoutSlot->SetPadding(FMargin(0.0f, 8.0f * Density, 0.0f, 0.0f));
        }
        MediaControls->SetVisibility(
            TemplateUsesMedia(ActiveTemplateId) && bMediaAvailable && !bWorldSpacePresentation
                ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    }
    if (UVerticalBoxSlot* LayoutSlot = StatusRow
        ? Cast<UVerticalBoxSlot>(StatusRow->Slot) : nullptr)
    {
        LayoutSlot->SetPadding(FMargin(0.0f, 8.0f * Density, 0.0f, 0.0f));
    }
    if (UVerticalBoxSlot* LayoutSlot = MetricsGrid ? Cast<UVerticalBoxSlot>(MetricsGrid->Slot) : nullptr)
    {
        LayoutSlot->SetPadding(FMargin(0.0f, 8.0f * Density, 0.0f, 0.0f));
    }
    if (UVerticalBoxSlot* LayoutSlot = GaugeRegion ? Cast<UVerticalBoxSlot>(GaugeRegion->Slot) : nullptr)
    {
        LayoutSlot->SetPadding(FMargin(0.0f, 8.0f * Density, 0.0f, 0.0f));
    }
    SetFont(GaugeLabel, FontSize(10, 14));
    SetFont(GaugeValue, FontSize(22, 26), true);
    SetFont(GaugeRange, FontSize(9, 12));
    SetFont(GaugeFallback, FontSize(9, 12));

    if (StatusDotBounds)
    {
        StatusDotBounds->SetWidthOverride(6.0f * Density);
        StatusDotBounds->SetHeightOverride(6.0f * Density);
        if (UHorizontalBoxSlot* LayoutSlot = Cast<UHorizontalBoxSlot>(StatusDotBounds->Slot))
        {
            LayoutSlot->SetPadding(FMargin(0.0f, 0.0f, 7.0f * Density, 0.0f));
        }
    }
    if (StatusSemanticText)
    {
        if (UHorizontalBoxSlot* LayoutSlot = Cast<UHorizontalBoxSlot>(StatusSemanticText->Slot))
        {
            LayoutSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f * Density, 0.0f));
        }
    }

    for (int32 Index = 0; Index < MetricLabels.Num(); ++Index)
    {
        const bool bEmphasized = MetricEmphasis.IsValidIndex(Index) && MetricEmphasis[Index];
        SetFont(MetricLabels[Index], FontSize(10, 14));
        SetFont(
            MetricValues.IsValidIndex(Index) ? MetricValues[Index] : nullptr,
            bEmphasized ? FontSize(22, 26) : FontSize(12, 16),
            bEmphasized);
        if (UGridSlot* LayoutSlot = Cast<UGridSlot>(MetricLabels[Index]->Slot))
        {
            LayoutSlot->SetPadding(FMargin(0.0f, 4.0f * Density, 8.0f * Density, 4.0f * Density));
        }
        if (MetricValues.IsValidIndex(Index))
        {
            if (UGridSlot* LayoutSlot = Cast<UGridSlot>(MetricValues[Index]->Slot))
            {
                LayoutSlot->SetPadding(FMargin(8.0f * Density, 4.0f * Density, 0.0f, 4.0f * Density));
            }
        }
    }

    // Style refresh must not restore padding for collapsed optional blocks.
    bool bHasVisibleBlock = false;
    for (UWidget* Block : ContentStack->GetAllChildren())
    {
        const bool bVisibleBlock = Block->GetVisibility() != ESlateVisibility::Collapsed;
        if (UVerticalBoxSlot* LayoutSlot = Cast<UVerticalBoxSlot>(Block->Slot))
        {
            const float Gap = Block == SubtitleText ? 2.0f : 8.0f;
            LayoutSlot->SetPadding(FMargin(0.0f,
                bVisibleBlock && bHasVisibleBlock ? Gap * Density : 0.0f, 0.0f, 0.0f));
        }
        bHasVisibleBlock |= bVisibleBlock;
    }
    ApplyTransientVisuals(GetRenderOpacity(), GetRenderTransform().Scale.X);
    InvalidateLayoutAndVolatility();
    ForceLayoutPrepass();
}

void UOntoTwinOverlayWidget::ApplyOverlayData(const TSharedPtr<FJsonObject>& OverlayData)
{
    PendingData = OverlayData;
    ApplyPendingData();
}

void UOntoTwinOverlayWidget::ApplyPendingData()
{
    if (!PendingData.IsValid() || !PanelBorder || !TitleText) return;

    PendingData->TryGetStringField(TEXT("config_revision"), ConfigRevision);
    RequestedGlassQuality = EOntoTwinGlassQuality::Balanced;
    const TSharedPtr<FJsonObject>* PresentationPtr = nullptr;
    if (PendingData->TryGetObjectField(TEXT("presentation"), PresentationPtr)
        && PresentationPtr && PresentationPtr->IsValid())
    {
        FString QualityTier;
        (*PresentationPtr)->TryGetStringField(TEXT("quality_tier"), QualityTier);
        if (QualityTier.Equals(TEXT("high"), ESearchCase::IgnoreCase))
        {
            RequestedGlassQuality = EOntoTwinGlassQuality::High;
        }
        else if (QualityTier.Equals(TEXT("performance"), ESearchCase::IgnoreCase))
        {
            RequestedGlassQuality = EOntoTwinGlassQuality::Performance;
        }
    }
    FString TemplateId;
    PendingData->TryGetStringField(TEXT("template_id"), TemplateId);
    ApplyTemplateRecipe(TemplateId);
    bool bOnline = true;
    PendingData->TryGetBoolField(TEXT("online"), bOnline);
    bPanelOnline = bOnline;
    const TSharedPtr<FJsonObject>* SlotsPtr = nullptr;
    TSharedPtr<FJsonObject> Slots;
    if (PendingData->TryGetObjectField(TEXT("resolved_slots"), SlotsPtr) && SlotsPtr)
    {
        Slots = *SlotsPtr;
    }

    const FString Title = SlotDisplayValue(Slots, TEXT("title"));
    TitleText->SetText(FText::FromString(Title.IsEmpty() ? TEXT("--") : Title));

    const FString Subtitle = TemplateUsesSubtitle(ActiveTemplateId)
        ? SlotDisplayValue(Slots, TEXT("subtitle")).TrimStartAndEnd() : FString();
    SubtitleText->SetText(FText::FromString(Subtitle));
    SubtitleText->SetVisibility(Subtitle.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);

    const TSharedPtr<FJsonObject>* MediaPtr = nullptr;
    const bool bHasMedia = TemplateUsesMedia(ActiveTemplateId)
        && Slots.IsValid() && Slots->TryGetObjectField(TEXT("media"), MediaPtr)
        && MediaPtr && MediaPtr->IsValid();
    bMediaAvailable = false;
    MediaKind.Empty();
    MediaSourceRevision.Empty();
    if (bHasMedia)
    {
        (*MediaPtr)->TryGetBoolField(TEXT("available"), bMediaAvailable);
        (*MediaPtr)->TryGetStringField(TEXT("kind"), MediaKind);
        (*MediaPtr)->TryGetStringField(TEXT("source_revision"), MediaSourceRevision);
        FString NewPosterUrl;
        (*MediaPtr)->TryGetStringField(TEXT("poster_url"), NewPosterUrl);
        const TSharedPtr<FJsonObject>* Playback = nullptr;
        if ((*MediaPtr)->TryGetObjectField(TEXT("playback"), Playback) && Playback && Playback->IsValid())
        {
            (*Playback)->TryGetBoolField(TEXT("autoplay"), bMediaAutoplay);
            if (!PlaybackTexture)
            {
                (*Playback)->TryGetBoolField(TEXT("muted"), bMediaMuted);
            }
        }
        if (NewPosterUrl != PosterUrl)
        {
            if (PosterDownloadTask)
            {
                PosterDownloadTask->OnSuccess.RemoveAll(this);
                PosterDownloadTask->OnFail.RemoveAll(this);
                PosterDownloadTask = nullptr;
            }
            PosterUrl = NewPosterUrl;
            PosterTexture = nullptr;
            if (!PosterUrl.IsEmpty()) LoadPoster(PosterUrl);
        }
        FString ErrorCode;
        (*MediaPtr)->TryGetStringField(TEXT("error_code"), ErrorCode);
        if (!bMediaAvailable)
        {
            MediaStatusMessage = ErrorCode == TEXT("media_url_empty")
                ? TEXT("Video URL not configured") : TEXT("Video source unavailable");
        }
        else if (bWorldSpacePresentation)
        {
            MediaStatusMessage = MediaKind == TEXT("hls")
                ? TEXT("LIVE - Select to play") : TEXT("Select to play video");
        }
        else if (!PlaybackTexture)
        {
            MediaStatusMessage = bMediaAutoplay ? TEXT("Loading video...") : TEXT("Select Play to start");
        }
    }
    else
    {
        if (PosterDownloadTask)
        {
            PosterDownloadTask->OnSuccess.RemoveAll(this);
            PosterDownloadTask->OnFail.RemoveAll(this);
            PosterDownloadTask = nullptr;
        }
        PosterUrl.Empty();
        PosterTexture = nullptr;
        PlaybackTexture = nullptr;
        MediaStatusMessage.Empty();
        bMediaExpanded = false;
        bMediaAutoplay = true;
        bMediaMuted = true;
        bMediaPlaying = false;
        if (PlayPauseButtonText)
        {
            PlayPauseButtonText->SetText(FText::FromString(TEXT("播放")));
        }
        if (MuteButtonText)
        {
            MuteButtonText->SetText(FText::FromString(TEXT("静音")));
        }
        if (ExpandButtonText)
        {
            ExpandButtonText->SetText(FText::FromString(TEXT("放大")));
        }
        if (RetryButton)
        {
            RetryButton->SetVisibility(ESlateVisibility::Collapsed);
        }
    }
    MediaBounds->SetVisibility(bHasMedia ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    ApplyMediaVisual();

    const FString Body = TemplateUsesBody(ActiveTemplateId)
        ? SlotDisplayValue(Slots, TEXT("body")).TrimStartAndEnd() : FString();
    BodyText->SetText(FText::FromString(Body));
    BodyText->SetVisibility(Body.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
    if (UVerticalBoxSlot* BodySlot = Cast<UVerticalBoxSlot>(BodyText->Slot))
    {
        // A template can include a body slot while its value is intentionally
        // empty. Remove the slot's spacing as well as hiding the text widget.
        BodySlot->SetPadding(FMargin(0.0f, Body.IsEmpty() ? 0.0f : 8.0f, 0.0f, 0.0f));
    }

    const TSharedPtr<FJsonObject>* StatusPtr = nullptr;
    FString StatusValue;
    FString StatusDetail;
    FString StatusLevel = TEXT("unknown");
    FString StatusAccentToken;
    bool bHasStatus = false;
    if (TemplateUsesStatus(ActiveTemplateId) && Slots.IsValid()
        && Slots->TryGetObjectField(TEXT("status"), StatusPtr) && StatusPtr && StatusPtr->IsValid())
    {
        bHasStatus = true;
        (*StatusPtr)->TryGetStringField(TEXT("display_value"), StatusValue);
        (*StatusPtr)->TryGetStringField(TEXT("detail_value"), StatusDetail);
        (*StatusPtr)->TryGetStringField(TEXT("level"), StatusLevel);
        (*StatusPtr)->TryGetStringField(TEXT("accent_token"), StatusAccentToken);
    }
    if (!bOnline && TemplateUsesStatus(ActiveTemplateId))
    {
        bHasStatus = true;
        StatusLevel = TEXT("offline");
    }
    if (bHasStatus && StatusValue.IsEmpty())
    {
        StatusValue = FOntoTwinGlassTheme::StatusLabel(StatusLevel);
    }
    StatusSemanticText->SetText(FText::FromString(StatusValue));
    StatusText->SetText(FText::FromString(StatusDetail));
    StatusText->SetVisibility(StatusDetail.IsEmpty()
        ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
    StatusRow->SetVisibility(bHasStatus ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    const FLinearColor StatusColor = FOntoTwinGlassTheme::StatusAccent(
        StatusAccentToken.IsEmpty() ? StatusLevel : StatusAccentToken);
    const bool bStatusChanged = bHasStatus
        && !LastStatusLevel.IsEmpty()
        && LastStatusLevel != StatusLevel;
    LastStatusLevel = bHasStatus ? StatusLevel : FString();
    CurrentStatusAccent = StatusColor;
    bHasStatusAccent = false;
    if (StatusAccent)
    {
        StatusAccent->SetBrush(FSlateRoundedBoxBrush(
            CurrentStatusAccent,
            FMath::Max(1.0f, CurrentRenderDensity)));
    }
    if (StatusAccentBounds)
    {
        StatusAccentBounds->SetVisibility(ESlateVisibility::Collapsed);
    }
    if (bStatusChanged && !bWorldSpacePresentation && !IsReducedMotion())
    {
        StatePulseElapsed = 0.0f;
        EnsureVisualAnimationTimer();
    }
    StatusSemanticText->SetColorAndOpacity(FSlateColor(PrimaryText));
    const FOntoTwinGlassDecision StatusGlassDecision =
        FOntoTwinGlassRenderer::Resolve(bWorldSpacePresentation, RequestedGlassQuality);
    const float Density = RenderDensityForDecision(
        StatusGlassDecision, bWorldSpacePresentation);
    StatusDot->SetBrush(FSlateRoundedBoxBrush(StatusColor, 3.0f * Density));

    const TArray<TSharedPtr<FJsonValue>>* Metrics = nullptr;
    const bool bHasMetrics = TemplateUsesMetrics(ActiveTemplateId)
        && Slots.IsValid() && Slots->TryGetArrayField(TEXT("metrics"), Metrics) && Metrics;
    bool bGaugeRequested = false;
    bool bGaugeUsable = false;
    FString GaugePrimaryId;
    FString GaugePrimaryLabel;
    FString GaugePrimaryDisplay;
    double GaugeMinimum = 0.0;
    double GaugeMaximum = 0.0;
    double GaugeNumericValue = 0.0;
    bool bClampGaugeVisual = false;
    const TSharedPtr<FJsonObject>* MetricsVisualPtr = nullptr;
    if (bHasMetrics && Slots->TryGetObjectField(TEXT("metrics_visual"), MetricsVisualPtr)
        && MetricsVisualPtr && MetricsVisualPtr->IsValid())
    {
        FString Style;
        (*MetricsVisualPtr)->TryGetStringField(TEXT("style"), Style);
        bGaugeRequested = Style.Equals(TEXT("gauge"), ESearchCase::IgnoreCase);
        (*MetricsVisualPtr)->TryGetStringField(TEXT("primary_metric_id"), GaugePrimaryId);
        const TSharedPtr<FJsonObject>* RangePtr = nullptr;
        const bool bHasRange = (*MetricsVisualPtr)->TryGetObjectField(TEXT("range"), RangePtr)
            && RangePtr && RangePtr->IsValid()
            && (*RangePtr)->TryGetNumberField(TEXT("min"), GaugeMinimum)
            && (*RangePtr)->TryGetNumberField(TEXT("max"), GaugeMaximum);
        if (bHasRange)
        {
            (*RangePtr)->TryGetBoolField(TEXT("clamp_visual"), bClampGaugeVisual);
        }
        bool bHasRawNumeric = false;
        if (bGaugeRequested)
        {
            for (const TSharedPtr<FJsonValue>& MetricValue : *Metrics)
            {
                const TSharedPtr<FJsonObject> Metric = MetricValue->AsObject();
                FString MetricId;
                if (!Metric.IsValid() || !Metric->TryGetStringField(TEXT("id"), MetricId)
                    || MetricId != GaugePrimaryId)
                {
                    continue;
                }
                Metric->TryGetStringField(TEXT("label"), GaugePrimaryLabel);
                Metric->TryGetStringField(TEXT("display_value"), GaugePrimaryDisplay);
                bHasRawNumeric = Metric->TryGetNumberField(TEXT("numeric_value"), GaugeNumericValue);
                break;
            }
        }
        const bool bFinite = FMath::IsFinite(GaugeMinimum)
            && FMath::IsFinite(GaugeMaximum)
            && FMath::IsFinite(GaugeNumericValue);
        const bool bRangeValid = bHasRange && bFinite && GaugeMaximum > GaugeMinimum;
        const bool bOutOfRange = bRangeValid
            && (GaugeNumericValue < GaugeMinimum || GaugeNumericValue > GaugeMaximum);
        bGaugeUsable = bGaugeRequested && bHasRawNumeric && bRangeValid
            && (!bOutOfRange || bClampGaugeVisual);
    }
    if (GaugeRegion && GaugeRow && GaugeFallback)
    {
        GaugeRegion->SetVisibility(bGaugeRequested
            ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
        GaugeRow->SetVisibility(bGaugeUsable
            ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
        GaugeFallback->SetVisibility(bGaugeRequested && !bGaugeUsable
            ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    }
    if (bGaugeUsable)
    {
        const float Normalized = static_cast<float>(FMath::Clamp(
            (GaugeNumericValue - GaugeMinimum) / (GaugeMaximum - GaugeMinimum), 0.0, 1.0));
        GaugeWidget->SetGauge(Normalized, bHasStatus ? StatusColor : PrimaryText, true);
        GaugeLabel->SetText(FText::FromString(GaugePrimaryLabel));
        GaugeValue->SetText(FText::FromString(GaugePrimaryDisplay));
        GaugeRange->SetText(FText::FromString(FString::Printf(
            TEXT("%g — %g"), GaugeMinimum, GaugeMaximum)));
    }
    int32 VisibleMetricIndex = 0;
    for (int32 Index = 0; Index < MetricLabels.Num(); ++Index)
    {
        FString LabelValue;
        FString MetricValue;
        bool bEmphasized = false;
        while (bHasMetrics && Metrics->IsValidIndex(VisibleMetricIndex))
        {
            const TSharedPtr<FJsonObject> Metric = (*Metrics)[VisibleMetricIndex++]->AsObject();
            if (Metric.IsValid())
            {
                FString MetricId;
                Metric->TryGetStringField(TEXT("id"), MetricId);
                if (bGaugeUsable && MetricId == GaugePrimaryId)
                {
                    continue;
                }
                Metric->TryGetStringField(TEXT("label"), LabelValue);
                Metric->TryGetStringField(TEXT("display_value"), MetricValue);
                Metric->TryGetBoolField(TEXT("emphasized"), bEmphasized);
            }
            break;
        }
        if (MetricEmphasis.IsValidIndex(Index))
        {
            MetricEmphasis[Index] = bEmphasized;
        }
        MetricLabels[Index]->SetText(FText::FromString(LabelValue));
        MetricValues[Index]->SetText(FText::FromString(MetricValue));
        MetricValues[Index]->SetColorAndOpacity(FSlateColor(
            bEmphasized && bHasStatus ? StatusColor : PrimaryText));
        const ESlateVisibility RowVisibility = LabelValue.IsEmpty() && MetricValue.IsEmpty()
            ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible;
        MetricLabels[Index]->SetVisibility(RowVisibility);
        MetricValues[Index]->SetVisibility(RowVisibility);
    }
    MetricsGrid->SetVisibility(bHasMetrics && (Metrics->Num() - (bGaugeUsable ? 1 : 0)) > 0
        ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    ApplyPresentationStyle();
}

void UOntoTwinOverlayWidget::SetMediaActionHandler(
    TFunction<void(EOntoTwinOverlayMediaAction)> Handler)
{
    MediaActionHandler = MoveTemp(Handler);
}

void UOntoTwinOverlayWidget::SetMediaTexture(UTexture* Texture)
{
    PlaybackTexture = Texture;
    if (Texture)
    {
        MediaStatusMessage.Empty();
    }
    ApplyMediaVisual();
}

void UOntoTwinOverlayWidget::SetMediaPlaybackState(
    bool bPlaying, bool bMuted, const FString& StatusMessage, bool bShowRetry)
{
    bMediaPlaying = bPlaying;
    bMediaMuted = bMuted;
    MediaStatusMessage = StatusMessage;
    if (PlayPauseButtonText)
    {
        PlayPauseButtonText->SetText(FText::FromString(bPlaying ? TEXT("暂停") : TEXT("播放")));
    }
    if (MuteButtonText)
    {
        MuteButtonText->SetText(FText::FromString(bMuted ? TEXT("静音") : TEXT("有声")));
    }
    if (RetryButton)
    {
        RetryButton->SetVisibility(
            bShowRetry ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    }
    ApplyMediaVisual();
}

void UOntoTwinOverlayWidget::SetMediaExpanded(bool bExpanded)
{
    bMediaExpanded = bExpanded && bMediaAvailable && !bWorldSpacePresentation;
    if (ExpandButtonText)
    {
        ExpandButtonText->SetText(FText::FromString(bMediaExpanded ? TEXT("还原") : TEXT("放大")));
    }
    ApplyPresentationStyle();
}

void UOntoTwinOverlayWidget::LoadPoster(const FString& Url)
{
    if (Url.IsEmpty()) return;
    if (PosterDownloadTask)
    {
        PosterDownloadTask->OnSuccess.RemoveAll(this);
        PosterDownloadTask->OnFail.RemoveAll(this);
        PosterDownloadTask = nullptr;
    }
    PosterDownloadTask = UAsyncTaskDownloadImage::DownloadImage(Url);
    if (!PosterDownloadTask) return;
    PosterDownloadTask->OnSuccess.AddDynamic(this, &UOntoTwinOverlayWidget::HandlePosterDownloaded);
    PosterDownloadTask->OnFail.AddDynamic(this, &UOntoTwinOverlayWidget::HandlePosterFailed);
}

void UOntoTwinOverlayWidget::HandlePosterDownloaded(UTexture2DDynamic* Texture)
{
    PosterTexture = Texture;
    PosterDownloadTask = nullptr;
    ApplyMediaVisual();
}

void UOntoTwinOverlayWidget::HandlePosterFailed(UTexture2DDynamic* Texture)
{
    PosterTexture = nullptr;
    PosterDownloadTask = nullptr;
    ApplyMediaVisual();
}

void UOntoTwinOverlayWidget::ApplyMediaVisual()
{
    if (!MediaImage || !MediaStateText) return;
    UTexture* Texture = PlaybackTexture ? PlaybackTexture : PosterTexture;
    FSlateBrush Brush;
    Brush.DrawAs = ESlateBrushDrawType::Image;
    Brush.ImageSize = FVector2D(640.0f, 360.0f);
    Brush.TintColor = FSlateColor(FLinearColor::White);
    Brush.SetResourceObject(Texture);
    MediaImage->SetBrush(Brush);
    MediaImage->SetColorAndOpacity(Texture ? FLinearColor::White : FLinearColor(0.04f, 0.04f, 0.04f, 1.0f));
    MediaStateText->SetText(FText::FromString(MediaStatusMessage));
    MediaStateText->SetVisibility(MediaStatusMessage.IsEmpty()
        ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}

void UOntoTwinOverlayWidget::NativeOnMouseEnter(
    const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
    bPointerOverPanel = true;
    InteractionTarget = 1.0f;
    EnsureVisualAnimationTimer();
}

void UOntoTwinOverlayWidget::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
    Super::NativeOnMouseLeave(InMouseEvent);
    bPointerOverPanel = false;
    InteractionTarget = bFocusWithinPanel ? 1.0f : 0.0f;
    EnsureVisualAnimationTimer();
}

void UOntoTwinOverlayWidget::NativeOnAddedToFocusPath(const FFocusEvent& InFocusEvent)
{
    Super::NativeOnAddedToFocusPath(InFocusEvent);
    bFocusWithinPanel = true;
    InteractionTarget = 1.0f;
    EnsureVisualAnimationTimer();
}

void UOntoTwinOverlayWidget::NativeOnRemovedFromFocusPath(const FFocusEvent& InFocusEvent)
{
    Super::NativeOnRemovedFromFocusPath(InFocusEvent);
    bFocusWithinPanel = false;
    InteractionTarget = bPointerOverPanel ? 1.0f : 0.0f;
    EnsureVisualAnimationTimer();
}

void UOntoTwinOverlayWidget::HandlePlayPauseClicked()
{
    if (MediaActionHandler) MediaActionHandler(EOntoTwinOverlayMediaAction::PlayPause);
}

void UOntoTwinOverlayWidget::HandleMuteClicked()
{
    if (MediaActionHandler) MediaActionHandler(EOntoTwinOverlayMediaAction::ToggleMute);
}

void UOntoTwinOverlayWidget::HandleExpandClicked()
{
    if (MediaActionHandler) MediaActionHandler(EOntoTwinOverlayMediaAction::ToggleExpanded);
}

void UOntoTwinOverlayWidget::HandleCloseClicked()
{
    if (MediaActionHandler) MediaActionHandler(EOntoTwinOverlayMediaAction::Close);
}

void UOntoTwinOverlayWidget::HandleRetryClicked()
{
    if (MediaActionHandler) MediaActionHandler(EOntoTwinOverlayMediaAction::Retry);
}
