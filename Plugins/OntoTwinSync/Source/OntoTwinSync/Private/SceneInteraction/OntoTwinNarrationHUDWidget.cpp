#include "SceneInteraction/OntoTwinNarrationHUDWidget.h"

#include "SceneInteraction/TwinInteractionManagerComponent.h"
#include "UI/OntoTwinGlassRenderer.h"
#include "UI/OntoTwinGlassTheme.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/BackgroundBlur.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

TSharedRef<SWidget> UOntoTwinNarrationHUDWidget::RebuildWidget()
{
    if (!WidgetTree)
    {
        WidgetTree = NewObject<UWidgetTree>(this, TEXT("NarrationHUDWidgetTree"), RF_Transient);
    }
    if (WidgetTree && !WidgetTree->RootWidget) BuildDefaultLayout();
    return Super::RebuildWidget();
}

void UOntoTwinNarrationHUDWidget::SetInteractionManager(
    UTwinInteractionManagerComponent* InManager)
{
    Manager = InManager;
}

void UOntoTwinNarrationHUDWidget::BuildDefaultLayout()
{
    UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
        UCanvasPanel::StaticClass(), TEXT("NarrationCanvas"));
    Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    WidgetTree->RootWidget = Root;

    Bounds = WidgetTree->ConstructWidget<USizeBox>(
        USizeBox::StaticClass(), TEXT("NarrationBounds"));
    Bounds->SetWidthOverride(900.0f);
    Bounds->SetMinDesiredHeight(0.0f);
    Bounds->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UCanvasPanelSlot* BoundsSlot = Root->AddChildToCanvas(Bounds);
    BoundsSlot->SetAnchors(FAnchors(0.5f, 1.0f));
    BoundsSlot->SetAlignment(FVector2D(0.5f, 1.0f));
    BoundsSlot->SetPosition(FVector2D(0.0f, -52.0f));
    BoundsSlot->SetAutoSize(true);

    Surface = WidgetTree->ConstructWidget<UOverlay>(
        UOverlay::StaticClass(), TEXT("NarrationGlassLayers"));
    Surface->SetVisibility(ESlateVisibility::Collapsed);
    Bounds->AddChild(Surface);

    constexpr float SurfaceRadius = 16.0f;
    const FVector4 RoundedCorners(
        SurfaceRadius, SurfaceRadius, SurfaceRadius, SurfaceRadius);
    const FOntoTwinGlassDecision GlassDecision = FOntoTwinGlassRenderer::Resolve(false);
    const bool bUseHigh =
        GlassDecision.EffectiveQuality == EOntoTwinGlassQuality::High
        && GlassDecision.HighMaterial != nullptr;
    const bool bUseBalanced =
        GlassDecision.EffectiveQuality == EOntoTwinGlassQuality::Balanced;

    UImage* HighGlassSurface = WidgetTree->ConstructWidget<UImage>(
        UImage::StaticClass(), TEXT("NarrationHighGlassSurface"));
    if (bUseHigh)
    {
        FSlateRoundedBoxBrush HighGlassBrush(FLinearColor::White, RoundedCorners);
        HighGlassBrush.ImageType = ESlateBrushImageType::FullColor;
        HighGlassBrush.SetResourceObject(GlassDecision.HighMaterial);
        HighGlassSurface->SetBrush(HighGlassBrush);
    }
    HighGlassSurface->SetVisibility(bUseHigh
        ? ESlateVisibility::SelfHitTestInvisible
        : ESlateVisibility::Collapsed);
    UOverlaySlot* HighGlassSlot = Surface->AddChildToOverlay(HighGlassSurface);
    HighGlassSlot->SetHorizontalAlignment(HAlign_Fill);
    HighGlassSlot->SetVerticalAlignment(VAlign_Fill);

    UBackgroundBlur* BalancedBlur = WidgetTree->ConstructWidget<UBackgroundBlur>(
        UBackgroundBlur::StaticClass(), TEXT("NarrationBalancedBlur"));
    BalancedBlur->SetBlurStrength(14.0f);
    BalancedBlur->SetApplyAlphaToBlur(true);
    BalancedBlur->SetCornerRadius(RoundedCorners);
    BalancedBlur->SetLowQualityFallbackBrush(FSlateRoundedBoxBrush(
        FOntoTwinGlassTheme::ScreenTint(EOntoTwinGlassQuality::Performance),
        SurfaceRadius,
        FOntoTwinGlassTheme::Rim(),
        1.0f));
    BalancedBlur->SetVisibility(bUseBalanced
        ? ESlateVisibility::SelfHitTestInvisible
        : ESlateVisibility::Collapsed);
    UOverlaySlot* BlurSlot = Surface->AddChildToOverlay(BalancedBlur);
    BlurSlot->SetHorizontalAlignment(HAlign_Fill);
    BlurSlot->SetVerticalAlignment(VAlign_Fill);

    FLinearColor SurfaceTint =
        FOntoTwinGlassTheme::ScreenTint(GlassDecision.EffectiveQuality);
    FLinearColor RimColor = FOntoTwinGlassTheme::Rim();
    if (FOntoTwinGlassRenderer::ShouldUseHighContrast())
    {
        SurfaceTint.A = FMath::Clamp(SurfaceTint.A + 0.12f, 0.0f, 0.96f);
        RimColor.A = 0.38f;
    }
    UBorder* SurfaceTintLayer = WidgetTree->ConstructWidget<UBorder>(
        UBorder::StaticClass(), TEXT("NarrationSurfaceTint"));
    SurfaceTintLayer->SetBrush(FSlateRoundedBoxBrush(
        SurfaceTint,
        SurfaceRadius,
        RimColor,
        1.0f));
    SurfaceTintLayer->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UOverlaySlot* SurfaceTintSlot = Surface->AddChildToOverlay(SurfaceTintLayer);
    SurfaceTintSlot->SetHorizontalAlignment(HAlign_Fill);
    SurfaceTintSlot->SetVerticalAlignment(VAlign_Fill);

    UImage* FineNoiseLayer = WidgetTree->ConstructWidget<UImage>(
        UImage::StaticClass(), TEXT("NarrationFineNoise"));
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
    const float NoiseOpacity =
        GlassDecision.EffectiveQuality == EOntoTwinGlassQuality::High ? 0.018f
        : GlassDecision.EffectiveQuality == EOntoTwinGlassQuality::Balanced ? 0.012f
        : 0.0f;
    FineNoiseLayer->SetRenderOpacity(NoiseOpacity);
    FineNoiseLayer->SetVisibility(NoiseOpacity > 0.0f
        ? ESlateVisibility::SelfHitTestInvisible
        : ESlateVisibility::Collapsed);
    UOverlaySlot* NoiseSlot = Surface->AddChildToOverlay(FineNoiseLayer);
    NoiseSlot->SetHorizontalAlignment(HAlign_Fill);
    NoiseSlot->SetVerticalAlignment(VAlign_Fill);

    UBorder* TopHighlight = WidgetTree->ConstructWidget<UBorder>(
        UBorder::StaticClass(), TEXT("NarrationTopHighlight"));
    const float HighlightOpacity =
        GlassDecision.EffectiveQuality == EOntoTwinGlassQuality::High ? 0.22f
        : GlassDecision.EffectiveQuality == EOntoTwinGlassQuality::Balanced ? 0.15f
        : 0.07f;
    TopHighlight->SetBrush(FSlateRoundedBoxBrush(
        FLinearColor(1.0f, 1.0f, 1.0f, HighlightOpacity), 1.0f));
    TopHighlight->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    USizeBox* HighlightBounds = WidgetTree->ConstructWidget<USizeBox>(
        USizeBox::StaticClass(), TEXT("NarrationTopHighlightBounds"));
    HighlightBounds->SetHeightOverride(1.0f);
    HighlightBounds->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    HighlightBounds->AddChild(TopHighlight);
    UOverlaySlot* HighlightSlot = Surface->AddChildToOverlay(HighlightBounds);
    HighlightSlot->SetHorizontalAlignment(HAlign_Fill);
    HighlightSlot->SetVerticalAlignment(VAlign_Top);
    HighlightSlot->SetPadding(FMargin(18.0f, 1.0f, 18.0f, 0.0f));

    UBorder* ContentContainer = WidgetTree->ConstructWidget<UBorder>(
        UBorder::StaticClass(), TEXT("NarrationContentContainer"));
    ContentContainer->SetBrush(FSlateColorBrush(FLinearColor::Transparent));
    ContentContainer->SetPadding(FMargin(16.0f, 9.0f));
    ContentContainer->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UOverlaySlot* ContentSlot = Surface->AddChildToOverlay(ContentContainer);
    ContentSlot->SetHorizontalAlignment(HAlign_Fill);
    ContentSlot->SetVerticalAlignment(VAlign_Fill);

    UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(
        UVerticalBox::StaticClass(), TEXT("NarrationStack"));
    Stack->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    ContentContainer->SetContent(Stack);

    UHorizontalBox* Meta = WidgetTree->ConstructWidget<UHorizontalBox>(
        UHorizontalBox::StaticClass(), TEXT("NarrationMeta"));
    Meta->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UVerticalBoxSlot* MetaSlot = Stack->AddChildToVerticalBox(Meta);
    MetaSlot->SetVerticalAlignment(VAlign_Center);

    ProgressText = WidgetTree->ConstructWidget<UTextBlock>(
        UTextBlock::StaticClass(), TEXT("NarrationProgress"));
    ProgressText->SetFont(FOntoTwinGlassTheme::Font(10.0f, true));
    ProgressText->SetColorAndOpacity(FOntoTwinGlassTheme::SecondaryText());
    ProgressText->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UHorizontalBoxSlot* ProgressSlot = Meta->AddChildToHorizontalBox(ProgressText);
    ProgressSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    ProgressSlot->SetHorizontalAlignment(HAlign_Left);
    ProgressSlot->SetVerticalAlignment(VAlign_Center);

    SkipButton = WidgetTree->ConstructWidget<UButton>(
        UButton::StaticClass(), TEXT("NarrationSkip"));
PRAGMA_DISABLE_DEPRECATION_WARNINGS
    SkipButton->IsFocusable = false;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
    FButtonStyle ButtonStyle;
    ButtonStyle
        .SetNormal(FSlateRoundedBoxBrush(
            FLinearColor(0.95f, 0.95f, 0.95f, 0.08f), 14.0f,
            FLinearColor(0.96f, 0.96f, 0.96f, 0.16f), 1.0f))
        .SetHovered(FSlateRoundedBoxBrush(
            FLinearColor(0.98f, 0.98f, 0.98f, 0.18f), 14.0f,
            FLinearColor(1.0f, 1.0f, 1.0f, 0.34f), 1.0f))
        .SetPressed(FSlateRoundedBoxBrush(
            FLinearColor(0.98f, 0.98f, 0.98f, 0.24f), 14.0f,
            FLinearColor(1.0f, 1.0f, 1.0f, 0.42f), 1.0f));
    ButtonStyle.SetNormalPadding(FMargin(0.0f));
    ButtonStyle.SetPressedPadding(FMargin(0.0f, 1.0f, 0.0f, 0.0f));
    SkipButton->SetStyle(ButtonStyle);
    SkipButton->SetToolTipText(FText::FromString(TEXT("跳过当前段")));
    SkipButton->OnClicked.AddDynamic(this, &UOntoTwinNarrationHUDWidget::OnSkipClicked);

    UTextBlock* SkipIcon = WidgetTree->ConstructWidget<UTextBlock>(
        UTextBlock::StaticClass(), TEXT("NarrationSkipIcon"));
    SkipIcon->SetText(FText::FromString(TEXT("\x00BB")));
    SkipIcon->SetFont(FOntoTwinGlassTheme::Font(16.0f, true));
    SkipIcon->SetColorAndOpacity(FOntoTwinGlassTheme::PrimaryText());
    SkipIcon->SetJustification(ETextJustify::Center);
    SkipIcon->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    SkipButton->AddChild(SkipIcon);

    USizeBox* SkipBounds = WidgetTree->ConstructWidget<USizeBox>(
        USizeBox::StaticClass(), TEXT("NarrationSkipBounds"));
    SkipBounds->SetWidthOverride(28.0f);
    SkipBounds->SetHeightOverride(28.0f);
    SkipBounds->AddChild(SkipButton);
    UHorizontalBoxSlot* SkipSlot = Meta->AddChildToHorizontalBox(SkipBounds);
    SkipSlot->SetHorizontalAlignment(HAlign_Right);
    SkipSlot->SetVerticalAlignment(VAlign_Center);

    BodyText = WidgetTree->ConstructWidget<UTextBlock>(
        UTextBlock::StaticClass(), TEXT("NarrationBody"));
    BodyText->SetFont(FOntoTwinGlassTheme::Font(16.0f, false));
    BodyText->SetColorAndOpacity(FOntoTwinGlassTheme::PrimaryText());
    BodyText->SetAutoWrapText(true);
    BodyText->SetWrapTextAt(850.0f);
    BodyText->SetLineHeightPercentage(1.15f);
    BodyText->SetShadowOffset(FVector2D(1.0f, 1.0f));
    BodyText->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.42f));
    BodyText->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UVerticalBoxSlot* BodySlot = Stack->AddChildToVerticalBox(BodyText);
    BodySlot->SetPadding(FMargin(0.0f, 7.0f, 0.0f, 0.0f));
}

void UOntoTwinNarrationHUDWidget::ShowSegment(
    const FString& Text,
    int32 SegmentIndex,
    int32 SegmentCount,
    const FString& Mode,
    bool bAudioFallback,
    bool bShowText)
{
    if (!Surface || !Bounds || !ProgressText || !BodyText) return;
    static_cast<void>(Mode);
    static_cast<void>(bAudioFallback);

    const int32 SafeSegmentCount = FMath::Max(1, SegmentCount);
    const int32 SafeSegmentIndex = FMath::Clamp(
        SegmentIndex + 1, 1, SafeSegmentCount);
    ProgressText->SetText(FText::FromString(FString::Printf(
        TEXT("%d / %d"), SafeSegmentIndex, SafeSegmentCount)));
    BodyText->SetText(FText::FromString(Text));
    const bool bHasVisibleText = bShowText && !Text.TrimStartAndEnd().IsEmpty();
    BodyText->SetVisibility(bHasVisibleText
        ? ESlateVisibility::SelfHitTestInvisible
        : ESlateVisibility::Collapsed);
    Bounds->SetWidthOverride(bHasVisibleText ? 900.0f : 124.0f);
    Surface->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

void UOntoTwinNarrationHUDWidget::HideNarration()
{
    if (Surface) Surface->SetVisibility(ESlateVisibility::Collapsed);
}

void UOntoTwinNarrationHUDWidget::OnSkipClicked()
{
    if (Manager) Manager->SkipNarrationSegment();
}
