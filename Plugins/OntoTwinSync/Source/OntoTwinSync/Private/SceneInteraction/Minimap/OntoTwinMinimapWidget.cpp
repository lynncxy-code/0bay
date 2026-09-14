#include "SceneInteraction/Minimap/OntoTwinMinimapWidget.h"
#include "SceneInteraction/TwinInteractionManagerComponent.h"
#include "UI/OntoTwinGlassRenderer.h"
#include "UI/OntoTwinGlassTheme.h"

#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Layout/SlateRect.h"
#include "Rendering/DrawElementTypes.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SWidget.h"

namespace
{
constexpr float ShellPadding = 8.0f;
constexpr float ShellCornerRadius = 20.0f;
constexpr float MapCornerRadius = ShellCornerRadius - ShellPadding;
constexpr float DefaultContentWidth = 320.0f;
constexpr float ToggleSize = 44.0f;
constexpr float SafeEdge = 24.0f;
constexpr float ToggleInset = 4.0f;
constexpr float DirectionDeadZone = 32.0f;
constexpr float ExpandDuration = 0.22f;
constexpr float CollapseDuration = 0.15f;
constexpr float ReduceMotionDuration = 0.12f;
constexpr double TouchDoubleClickSeconds = 0.45;
constexpr float TouchDoubleClickDistance = 18.0f;
const FLinearColor ShellFill(0.04f, 0.045f, 0.055f, 0.82f);
const FLinearColor ShellStroke(0.92f, 0.92f, 0.92f, 0.28f);
const FLinearColor MarkerOutline(0.16f, 0.01f, 0.01f, 0.46f);
const FLinearColor MarkerRed(1.0f, 0.035f, 0.025f, 0.70f);
const FLinearColor ToggleFill(0.035f, 0.04f, 0.05f, 0.90f);
const FLinearColor ToggleHover(0.12f, 0.13f, 0.15f, 0.94f);
const FLinearColor TogglePressed(0.19f, 0.20f, 0.22f, 0.96f);
const FLinearColor ToggleStroke(0.92f, 0.92f, 0.92f, 0.32f);
const FLinearColor ToggleDisabled(0.025f, 0.028f, 0.032f, 0.68f);
const FLinearColor ToggleDisabledStroke(0.70f, 0.70f, 0.70f, 0.18f);
const FLinearColor ToggleFocus(1.0f, 1.0f, 1.0f, 0.96f);
const FLinearColor ToggleIcon(0.96f, 0.96f, 0.96f, 0.94f);
const FLinearColor FeedbackFill(0.025f, 0.03f, 0.04f, 0.91f);
const FLinearColor FeedbackValid(0.94f, 0.94f, 0.94f, 0.92f);
const FLinearColor FeedbackAdjusted(1.0f, 0.63f, 0.12f, 0.94f);
const FLinearColor FeedbackInvalid(1.0f, 0.18f, 0.14f, 0.94f);
const FLinearColor FeedbackSuccess(0.31f, 0.88f, 0.58f, 0.94f);
}

TSharedRef<SWidget> UOntoTwinMinimapWidget::RebuildWidget()
{
    if (!WidgetTree)
    {
        WidgetTree = NewObject<UWidgetTree>(this, TEXT("MinimapWidgetTree"), RF_Transient);
    }
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        BuildDefaultLayout();
    }
    TSharedRef<SWidget> Result = Super::RebuildWidget();
    UpdateToggleSemantics();
    ApplyMapTexture();
    ApplyExpansionVisuals();
    return Result;
}

void UOntoTwinMinimapWidget::NativeTick(
    const FGeometry& MyGeometry,
    float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    const FVector2D PreviousViewport = ViewportLogicalSize;
    const FVector2D PreviousPanelTopLeft = PanelTopLeft;
    const EMinimapGrowthDirection PreviousDirection = GrowthDirection;
    UpdateViewportLayout(MyGeometry);
    bool bNeedsPaint = PreviousViewport != ViewportLogicalSize
        || PreviousPanelTopLeft != PanelTopLeft
        || PreviousDirection != GrowthDirection;
    const bool bReduceMotion = FOntoTwinGlassRenderer::ShouldReduceMotion();
    if (bMarkerVisible && MapTexture && !bReduceMotion)
    {
        MarkerPulsePhase = FMath::Fmod(
            MarkerPulsePhase + InDeltaTime * (2.0f * PI / 1.25f),
            2.0f * PI);
        bNeedsPaint = true;
    }

    if (FeedbackSecondsRemaining > 0.0f)
    {
        FeedbackSecondsRemaining = FMath::Max(
            0.0f, FeedbackSecondsRemaining - InDeltaTime);
        if (FeedbackSecondsRemaining <= 0.0f)
        {
            ClearTeleportFeedback();
        }
        bNeedsPaint = true;
    }

    const float TargetAlpha = bExpanded ? 1.0f : 0.0f;
    if (!FMath::IsNearlyEqual(ExpandAlpha, TargetAlpha, KINDA_SMALL_NUMBER))
    {
        const float Duration = bReduceMotion
            ? ReduceMotionDuration
            : (bExpanded ? ExpandDuration : CollapseDuration);
        ExpandAlpha = FMath::FInterpConstantTo(
            ExpandAlpha,
            TargetAlpha,
            InDeltaTime,
            1.0f / FMath::Max(Duration, KINDA_SMALL_NUMBER));
        ApplyExpansionVisuals();
        bNeedsPaint = true;
    }

    if (bNeedsPaint) InvalidateLayoutAndVolatility();
}

void UOntoTwinMinimapWidget::BuildDefaultLayout()
{
    RootBounds = WidgetTree->ConstructWidget<USizeBox>(
        USizeBox::StaticClass(), TEXT("MinimapBounds"));
    RootBounds->SetWidthOverride(ContentSize.X + ShellPadding * 2.0f);
    RootBounds->SetHeightOverride(ContentSize.Y + ShellPadding * 2.0f);
    RootBounds->SetClipping(EWidgetClipping::ClipToBoundsAlways);
    RootBounds->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    WidgetTree->RootWidget = RootBounds;

    UOverlay* RootOverlay = WidgetTree->ConstructWidget<UOverlay>(
        UOverlay::StaticClass(), TEXT("MinimapRootOverlay"));
    RootOverlay->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    RootBounds->AddChild(RootOverlay);

    MapShell = WidgetTree->ConstructWidget<UBorder>(
        UBorder::StaticClass(), TEXT("MinimapShell"));
    MapShell->SetPadding(FMargin(ShellPadding));
    MapShell->SetBrush(FSlateRoundedBoxBrush(
        ShellFill, ShellCornerRadius, ShellStroke, 1.0f));
    MapShell->SetRenderTransformPivot(FVector2D(1.0f, 0.0f));
    MapShell->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    MapShellSlot = RootOverlay->AddChildToOverlay(MapShell);
    MapShellSlot->SetHorizontalAlignment(HAlign_Right);
    MapShellSlot->SetVerticalAlignment(VAlign_Top);
    MapShellSlot->SetPadding(FMargin(0.0f));

    UOverlay* Content = WidgetTree->ConstructWidget<UOverlay>(
        UOverlay::StaticClass(), TEXT("MinimapContent"));
    Content->SetClipping(EWidgetClipping::ClipToBounds);
    Content->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    MapShell->SetContent(Content);

    MapImage = WidgetTree->ConstructWidget<UImage>(
        UImage::StaticClass(), TEXT("MinimapImage"));
    MapImage->SetDesiredSizeOverride(ContentSize);
    MapImage->SetColorAndOpacity(FLinearColor::White);
    MapImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UOverlaySlot* MapSlot = Content->AddChildToOverlay(MapImage);
    MapSlot->SetHorizontalAlignment(HAlign_Fill);
    MapSlot->SetVerticalAlignment(VAlign_Fill);

    TeleportStatusPanel = WidgetTree->ConstructWidget<UBorder>(
        UBorder::StaticClass(), TEXT("MinimapTeleportStatus"));
    TeleportStatusPanel->SetPadding(FMargin(9.0f, 6.0f));
    TeleportStatusPanel->SetBrush(FSlateRoundedBoxBrush(
        FeedbackFill, 9.0f, FeedbackValid, 1.0f));
    TeleportStatusPanel->SetVisibility(ESlateVisibility::Collapsed);
    TeleportStatusText = WidgetTree->ConstructWidget<UTextBlock>(
        UTextBlock::StaticClass(), TEXT("MinimapTeleportStatusText"));
    TeleportStatusText->SetColorAndOpacity(FLinearColor::White);
    TeleportStatusText->SetFont(FOntoTwinGlassTheme::Font(10.0f, true));
    TeleportStatusText->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    TeleportStatusPanel->SetContent(TeleportStatusText);
    UOverlaySlot* FeedbackSlot = Content->AddChildToOverlay(TeleportStatusPanel);
    FeedbackSlot->SetHorizontalAlignment(HAlign_Left);
    FeedbackSlot->SetVerticalAlignment(VAlign_Bottom);
    FeedbackSlot->SetPadding(FMargin(8.0f));

    UndoButton = WidgetTree->ConstructWidget<UButton>(
        UButton::StaticClass(), TEXT("MinimapUndoButton"));
    FButtonStyle UndoStyle;
    UndoStyle.SetNormal(FSlateRoundedBoxBrush(
        ToggleFill, 10.0f, ToggleStroke, 1.0f));
    UndoStyle.SetHovered(FSlateRoundedBoxBrush(
        ToggleHover, 10.0f, FeedbackValid, 1.0f));
    UndoStyle.SetPressed(FSlateRoundedBoxBrush(
        TogglePressed, 10.0f, FeedbackValid, 1.0f));
    UndoButton->SetStyle(UndoStyle);
    UndoButton->SetToolTipText(FText::FromString(TEXT("返回最近一次传送前的位置")));
    UndoButton->OnClicked.AddDynamic(
        this, &UOntoTwinMinimapWidget::OnUndoTeleport);
    UTextBlock* UndoText = WidgetTree->ConstructWidget<UTextBlock>(
        UTextBlock::StaticClass(), TEXT("MinimapUndoText"));
    UndoText->SetText(FText::FromString(TEXT("返回原位置")));
    UndoText->SetColorAndOpacity(FLinearColor::White);
    UndoText->SetFont(FOntoTwinGlassTheme::Font(10.0f, true));
    UndoText->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UndoButton->SetContent(UndoText);
    if (UButtonSlot* UndoContentSlot = Cast<UButtonSlot>(UndoText->Slot))
    {
        UndoContentSlot->SetPadding(FMargin(10.0f, 6.0f));
    }
    UndoButton->SetVisibility(ESlateVisibility::Collapsed);
    UOverlaySlot* UndoSlot = Content->AddChildToOverlay(UndoButton);
    UndoSlot->SetHorizontalAlignment(HAlign_Right);
    UndoSlot->SetVerticalAlignment(VAlign_Bottom);
    UndoSlot->SetPadding(FMargin(8.0f));

    ToggleButton = WidgetTree->ConstructWidget<UButton>(
        UButton::StaticClass(), TEXT("MinimapToggleButton"));
    FButtonStyle ToggleStyle;
    ToggleStyle.SetNormal(FSlateRoundedBoxBrush(
        ToggleFill, 14.0f, ToggleStroke, 1.0f));
    ToggleStyle.SetHovered(FSlateRoundedBoxBrush(
        ToggleHover, 14.0f, ToggleStroke, 1.0f));
    ToggleStyle.SetPressed(FSlateRoundedBoxBrush(
        TogglePressed, 14.0f, ToggleStroke, 1.0f));
    ToggleStyle.SetDisabled(FSlateRoundedBoxBrush(
        ToggleDisabled, 14.0f, ToggleDisabledStroke, 1.0f));
    ToggleButton->SetStyle(ToggleStyle);
    ToggleButton->SetToolTipText(FText::FromString(TEXT("收起小地图")));
    ToggleButton->OnClicked.AddDynamic(
        this, &UOntoTwinMinimapWidget::OnToggleExpanded);
    USizeBox* ToggleBounds = WidgetTree->ConstructWidget<USizeBox>(
        USizeBox::StaticClass(), TEXT("MinimapToggleBounds"));
    ToggleBounds->SetWidthOverride(ToggleSize);
    ToggleBounds->SetHeightOverride(ToggleSize);
    ToggleButton->SetContent(ToggleBounds);
    if (UButtonSlot* ToggleContentSlot = Cast<UButtonSlot>(ToggleBounds->Slot))
    {
        ToggleContentSlot->SetPadding(FMargin(0.0f));
    }
    ToggleSlot = RootOverlay->AddChildToOverlay(ToggleButton);
    ToggleSlot->SetHorizontalAlignment(HAlign_Right);
    ToggleSlot->SetVerticalAlignment(VAlign_Top);
    ToggleSlot->SetPadding(FMargin(ToggleInset));

    ToggleFocusRing = WidgetTree->ConstructWidget<UBorder>(
        UBorder::StaticClass(), TEXT("MinimapToggleFocusRing"));
    ToggleFocusRing->SetPadding(FMargin(0.0f));
    ToggleFocusRing->SetBrush(FSlateRoundedBoxBrush(
        FLinearColor::Transparent, 16.0f, ToggleFocus, 2.0f));
    ToggleFocusRing->SetVisibility(ESlateVisibility::Collapsed);
    USizeBox* FocusBounds = WidgetTree->ConstructWidget<USizeBox>(
        USizeBox::StaticClass(), TEXT("MinimapToggleFocusBounds"));
    FocusBounds->SetWidthOverride(ToggleSize + 6.0f);
    FocusBounds->SetHeightOverride(ToggleSize + 6.0f);
    FocusBounds->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    ToggleFocusRing->SetContent(FocusBounds);
    FocusRingSlot = RootOverlay->AddChildToOverlay(ToggleFocusRing);
    FocusRingSlot->SetHorizontalAlignment(HAlign_Right);
    FocusRingSlot->SetVerticalAlignment(VAlign_Top);
    FocusRingSlot->SetPadding(FMargin(ToggleInset - 3.0f));

    SetVisibility(MapTexture
        ? ESlateVisibility::Visible
        : ESlateVisibility::Collapsed);
    ToggleButton->SetIsEnabled(MapTexture != nullptr);
}

void UOntoTwinMinimapWidget::UpdateViewportLayout(const FGeometry& MyGeometry)
{
    FVector2D NewViewport = MyGeometry.GetLocalSize();
    if (APlayerController* PlayerController = GetOwningPlayer())
    {
        int32 ViewportX = 0;
        int32 ViewportY = 0;
        PlayerController->GetViewportSize(ViewportX, ViewportY);
        const float ViewportScale = FMath::Max(
            0.01f, UWidgetLayoutLibrary::GetViewportScale(this));
        const FVector2D DpiAwareViewport(
            static_cast<float>(ViewportX) / ViewportScale,
            static_cast<float>(ViewportY) / ViewportScale);
        if (DpiAwareViewport.X > 1.0f && DpiAwareViewport.Y > 1.0f)
        {
            // Slate layout coordinates are logical pixels.  Convert the
            // physical viewport exactly once and use that space for all
            // anchor, safe-area, and panel calculations.
            NewViewport = DpiAwareViewport;
        }
    }
    if (NewViewport.X <= 1.0f || NewViewport.Y <= 1.0f)
    {
        return;
    }

    const bool bViewportChanged = !bHasViewportMetrics
        || !FMath::IsNearlyEqual(NewViewport.X, ViewportLogicalSize.X, 0.5f)
        || !FMath::IsNearlyEqual(NewViewport.Y, ViewportLogicalSize.Y, 0.5f);
    ViewportLogicalSize = NewViewport;
    bHasViewportMetrics = true;
    if (RootBounds)
    {
        RootBounds->SetWidthOverride(ViewportLogicalSize.X);
        RootBounds->SetHeightOverride(ViewportLogicalSize.Y);
    }

    if (!bHasStableButtonAnchor)
    {
        CaptureStableButtonAnchor(MyGeometry);
    }
    else if (bViewportChanged && LastViewportLogicalSize.X > 1.0f
        && LastViewportLogicalSize.Y > 1.0f)
    {
        // Preserve the edge inset across resize/DPI changes.  The button is
        // never derived from the animated panel size.
        if (bAnchorRight)
        {
            StableButtonTopLeft.X = ViewportLogicalSize.X
                - ButtonEdgeInsets.X - ToggleSize;
        }
        else
        {
            StableButtonTopLeft.X = ButtonEdgeInsets.X;
        }
        if (bAnchorBottom)
        {
            StableButtonTopLeft.Y = ViewportLogicalSize.Y
                - ButtonEdgeInsets.Y - ToggleSize;
        }
        else
        {
            StableButtonTopLeft.Y = ButtonEdgeInsets.Y;
        }
    }

    PanelLogicalSize = FVector2D(
        ContentSize.X + ShellPadding * 2.0f,
        ContentSize.Y + ShellPadding * 2.0f);
    ResolveGrowthDirection();
    ApplyOverlayGeometry();
    ApplyExpansionVisuals();
    LastViewportLogicalSize = ViewportLogicalSize;
}

void UOntoTwinMinimapWidget::CaptureStableButtonAnchor(const FGeometry& MyGeometry)
{
    if (ViewportLogicalSize.X <= 1.0f || ViewportLogicalSize.Y <= 1.0f)
    {
        return;
    }

    FVector2D Captured = FVector2D(
        ViewportLogicalSize.X - SafeEdge - ToggleInset - ToggleSize,
        SafeEdge + ToggleInset);
    if (ToggleButton)
    {
        const FGeometry ToggleGeometry = ToggleButton->GetCachedGeometry();
        if (ToggleGeometry.GetLocalSize().X > 1.0f
            && ToggleGeometry.GetLocalSize().Y > 1.0f)
        {
            const FVector2D AbsoluteTopLeft = ToggleGeometry.GetAbsolutePosition();
            Captured = MyGeometry.AbsoluteToLocal(AbsoluteTopLeft);
        }
    }
    Captured.X = FMath::Clamp(
        Captured.X, 0.0f, FMath::Max(0.0f, ViewportLogicalSize.X - ToggleSize));
    Captured.Y = FMath::Clamp(
        Captured.Y, 0.0f, FMath::Max(0.0f, ViewportLogicalSize.Y - ToggleSize));
    StableButtonTopLeft = Captured;
    const FVector2D Center = GetButtonCenter();
    bAnchorRight = Center.X >= ViewportLogicalSize.X * 0.5f;
    bAnchorBottom = Center.Y >= ViewportLogicalSize.Y * 0.5f;
    ButtonEdgeInsets.X = bAnchorRight
        ? ViewportLogicalSize.X - (StableButtonTopLeft.X + ToggleSize)
        : StableButtonTopLeft.X;
    ButtonEdgeInsets.Y = bAnchorBottom
        ? ViewportLogicalSize.Y - (StableButtonTopLeft.Y + ToggleSize)
        : StableButtonTopLeft.Y;
    bHasStableButtonAnchor = true;
}

float UOntoTwinMinimapWidget::RectOverflow(
    const FSlateRect& Rect,
    const FSlateRect& SafeRect)
{
    return FMath::Max(0.0f, SafeRect.Left - Rect.Left)
        + FMath::Max(0.0f, Rect.Right - SafeRect.Right)
        + FMath::Max(0.0f, SafeRect.Top - Rect.Top)
        + FMath::Max(0.0f, Rect.Bottom - SafeRect.Bottom);
}

float UOntoTwinMinimapWidget::RectVisibleArea(
    const FSlateRect& Rect,
    const FSlateRect& ViewRect)
{
    const float Width = FMath::Max(
        0.0f, FMath::Min(Rect.Right, ViewRect.Right)
            - FMath::Max(Rect.Left, ViewRect.Left));
    const float Height = FMath::Max(
        0.0f, FMath::Min(Rect.Bottom, ViewRect.Bottom)
            - FMath::Max(Rect.Top, ViewRect.Top));
    return Width * Height;
}

bool UOntoTwinMinimapWidget::IsHorizontalRight(
    EMinimapGrowthDirection Direction)
{
    return Direction == EMinimapGrowthDirection::RightDown
        || Direction == EMinimapGrowthDirection::RightUp;
}

bool UOntoTwinMinimapWidget::IsVerticalDown(
    EMinimapGrowthDirection Direction)
{
    return Direction == EMinimapGrowthDirection::LeftDown
        || Direction == EMinimapGrowthDirection::RightDown;
}

FVector2D UOntoTwinMinimapWidget::DirectionVector(
    EMinimapGrowthDirection Direction)
{
    return FVector2D(
        IsHorizontalRight(Direction) ? 1.0f : -1.0f,
        IsVerticalDown(Direction) ? 1.0f : -1.0f);
}

void UOntoTwinMinimapWidget::ResolveGrowthDirection()
{
    if (!bHasStableButtonAnchor || ViewportLogicalSize.X <= 1.0f
        || ViewportLogicalSize.Y <= 1.0f)
    {
        return;
    }

    const FSlateRect ViewRect(
        0.0f, 0.0f, ViewportLogicalSize.X, ViewportLogicalSize.Y);
    const FSlateRect SafeRect(
        SafeEdge,
        SafeEdge,
        FMath::Max(SafeEdge, ViewportLogicalSize.X - SafeEdge),
        FMath::Max(SafeEdge, ViewportLogicalSize.Y - SafeEdge));
    const FVector2D ButtonBottomRight = StableButtonTopLeft
        + FVector2D(ToggleSize, ToggleSize);

    const auto MakePanelRect = [this, &ButtonBottomRight](
        EMinimapGrowthDirection Direction)
    {
        const bool bRight = IsHorizontalRight(Direction);
        const bool bDown = IsVerticalDown(Direction);
        const float Left = bRight
            ? StableButtonTopLeft.X - ToggleInset
            : ButtonBottomRight.X + ToggleInset - PanelLogicalSize.X;
        const float Top = bDown
            ? StableButtonTopLeft.Y - ToggleInset
            : ButtonBottomRight.Y + ToggleInset - PanelLogicalSize.Y;
        return FSlateRect(
            Left,
            Top,
            Left + PanelLogicalSize.X,
            Top + PanelLogicalSize.Y);
    };

    const EMinimapGrowthDirection Directions[] = {
        EMinimapGrowthDirection::LeftDown,
        EMinimapGrowthDirection::RightDown,
        EMinimapGrowthDirection::LeftUp,
        EMinimapGrowthDirection::RightUp,
    };
    const float PanelArea = FMath::Max(
        1.0f, PanelLogicalSize.X * PanelLogicalSize.Y);
    const float Hysteresis = DirectionDeadZone
        / FMath::Max(1.0f, PanelLogicalSize.X + PanelLogicalSize.Y);
    const auto Score = [this, &SafeRect, &ViewRect, PanelArea](
        const FSlateRect& Rect)
    {
        const float Overflow = RectOverflow(Rect, SafeRect)
            / FMath::Max(1.0f, Rect.Right - Rect.Left + Rect.Bottom - Rect.Top);
        const float VisibleLoss = 1.0f
            - FMath::Clamp(RectVisibleArea(Rect, ViewRect) / PanelArea, 0.0f, 1.0f);
        return Overflow + VisibleLoss;
    };

    const FSlateRect CurrentRect = MakePanelRect(GrowthDirection);
    const float CurrentOverflow = RectOverflow(CurrentRect, SafeRect);
    // Dead-zone hysteresis: keep the current direction while its safe-area
    // overflow is within 32 logical px, even if another candidate scores
    // marginally better due to a one-pixel resize or camera/viewport jitter.
    if (CurrentOverflow <= DirectionDeadZone)
    {
        PanelTopLeft = FVector2D(CurrentRect.Left, CurrentRect.Top);
        return;
    }

    EMinimapGrowthDirection BestDirection = GrowthDirection;
    float BestScore = TNumericLimits<float>::Max();
    for (const EMinimapGrowthDirection Direction : Directions)
    {
        const float CandidateScore = Score(MakePanelRect(Direction));
        if (CandidateScore < BestScore)
        {
            BestScore = CandidateScore;
            BestDirection = Direction;
        }
    }
    if (BestDirection != GrowthDirection
        && (CurrentOverflow > DirectionDeadZone
            || BestScore + Hysteresis < Score(CurrentRect)))
    {
        GrowthDirection = BestDirection;
    }
    const FSlateRect ResolvedRect = MakePanelRect(GrowthDirection);
    PanelTopLeft = FVector2D(ResolvedRect.Left, ResolvedRect.Top);
}

void UOntoTwinMinimapWidget::ApplyOverlayGeometry()
{
    if (!bHasStableButtonAnchor || ViewportLogicalSize.X <= 1.0f
        || ViewportLogicalSize.Y <= 1.0f)
    {
        return;
    }

    const FSlateRect PanelRect(
        PanelTopLeft.X,
        PanelTopLeft.Y,
        PanelTopLeft.X + PanelLogicalSize.X,
        PanelTopLeft.Y + PanelLogicalSize.Y);
    PanelPivot = FVector2D(
        IsHorizontalRight(GrowthDirection) ? 0.0f : 1.0f,
        IsVerticalDown(GrowthDirection) ? 0.0f : 1.0f);

    if (RootBounds)
    {
        RootBounds->SetWidthOverride(ViewportLogicalSize.X);
        RootBounds->SetHeightOverride(ViewportLogicalSize.Y);
    }
    if (MapShell)
    {
        MapShell->SetRenderTransformPivot(PanelPivot);
    }
    if (MapShellSlot)
    {
        MapShellSlot->SetHorizontalAlignment(
            IsHorizontalRight(GrowthDirection) ? HAlign_Left : HAlign_Right);
        MapShellSlot->SetVerticalAlignment(
            IsVerticalDown(GrowthDirection) ? VAlign_Top : VAlign_Bottom);
        const float Left = IsHorizontalRight(GrowthDirection)
            ? PanelRect.Left : 0.0f;
        const float Right = IsHorizontalRight(GrowthDirection)
            ? 0.0f : ViewportLogicalSize.X - PanelRect.Right;
        const float Top = IsVerticalDown(GrowthDirection)
            ? PanelRect.Top : 0.0f;
        const float Bottom = IsVerticalDown(GrowthDirection)
            ? 0.0f : ViewportLogicalSize.Y - PanelRect.Bottom;
        MapShellSlot->SetPadding(FMargin(Left, Top, Right, Bottom));
    }
    if (ToggleSlot)
    {
        ToggleSlot->SetHorizontalAlignment(HAlign_Left);
        ToggleSlot->SetVerticalAlignment(VAlign_Top);
        ToggleSlot->SetPadding(FMargin(
            StableButtonTopLeft.X, StableButtonTopLeft.Y, 0.0f, 0.0f));
    }
    if (FocusRingSlot)
    {
        FocusRingSlot->SetHorizontalAlignment(HAlign_Left);
        FocusRingSlot->SetVerticalAlignment(VAlign_Top);
        FocusRingSlot->SetPadding(FMargin(
            StableButtonTopLeft.X - 3.0f,
            StableButtonTopLeft.Y - 3.0f,
            0.0f,
            0.0f));
    }
}

FSlateRect UOntoTwinMinimapWidget::GetAnimatedPanelRect() const
{
    const float SmoothAlpha = ExpandAlpha * ExpandAlpha
        * (3.0f - 2.0f * ExpandAlpha);
    const float Scale = FOntoTwinGlassRenderer::ShouldReduceMotion()
        ? 1.0f : FMath::Lerp(0.97f, 1.0f, SmoothAlpha);
    const FVector2D AnimatedSize = PanelLogicalSize * Scale;
    const float AnchorX = IsHorizontalRight(GrowthDirection)
        ? PanelTopLeft.X : PanelTopLeft.X + PanelLogicalSize.X;
    const float AnchorY = IsVerticalDown(GrowthDirection)
        ? PanelTopLeft.Y : PanelTopLeft.Y + PanelLogicalSize.Y;
    const float Left = IsHorizontalRight(GrowthDirection)
        ? AnchorX : AnchorX - AnimatedSize.X;
    const float Top = IsVerticalDown(GrowthDirection)
        ? AnchorY : AnchorY - AnimatedSize.Y;
    return FSlateRect(Left, Top, Left + AnimatedSize.X, Top + AnimatedSize.Y);
}

FVector2D UOntoTwinMinimapWidget::GetButtonCenter() const
{
    return StableButtonTopLeft + FVector2D(ToggleSize * 0.5f);
}

void UOntoTwinMinimapWidget::UpdateToggleSemantics()
{
    if (!ToggleButton) return;
    const FText Label = FText::FromString(
        bExpanded ? TEXT("收起小地图") : TEXT("展开小地图"));
    ToggleButton->SetToolTipText(Label);
    if (const TSharedPtr<SWidget> SlateButton = ToggleButton->GetCachedWidget())
    {
        SlateButton->SetAccessibleBehavior(
            EAccessibleBehavior::Custom,
            TAttribute<FText>(Label));
    }
}

void UOntoTwinMinimapWidget::SetInteractionManager(
    UTwinInteractionManagerComponent* InManager)
{
    Manager = InManager;
}

void UOntoTwinMinimapWidget::SetMapTexture(
    UTextureRenderTarget2D* InTexture,
    const FIntPoint& InCaptureSize)
{
    MapTexture = InTexture;
    const float Aspect = InCaptureSize.Y > 0
        ? static_cast<float>(InCaptureSize.X) / static_cast<float>(InCaptureSize.Y)
        : (4.0f / 3.0f);
    ContentSize.X = DefaultContentWidth;
    ContentSize.Y = FMath::Clamp(DefaultContentWidth / FMath::Max(Aspect, 0.1f), 180.0f, 240.0f);
    if (MapImage) MapImage->SetDesiredSizeOverride(ContentSize);
    PanelLogicalSize = FVector2D(
        ContentSize.X + ShellPadding * 2.0f,
        ContentSize.Y + ShellPadding * 2.0f);
    if (bHasViewportMetrics)
    {
        ResolveGrowthDirection();
        ApplyOverlayGeometry();
    }
    ApplyMapTexture();
    ApplyExpansionVisuals();
    SetVisibility(MapTexture
        ? ESlateVisibility::Visible
        : ESlateVisibility::Collapsed);
    if (ToggleButton)
    {
        ToggleButton->SetIsEnabled(MapTexture != nullptr);
    }
}

void UOntoTwinMinimapWidget::ApplyMapTexture()
{
    if (!MapImage) return;
    FSlateRoundedBoxBrush MapBrush(FLinearColor::White, MapCornerRadius);
    MapBrush.ImageType = ESlateBrushImageType::FullColor;
    MapBrush.SetResourceObject(MapTexture);
    MapImage->SetBrush(MapBrush);
}

void UOntoTwinMinimapWidget::ApplyExpansionVisuals()
{
    const float SmoothAlpha = ExpandAlpha * ExpandAlpha * (3.0f - 2.0f * ExpandAlpha);
    if (MapShell)
    {
        if (ExpandAlpha > KINDA_SMALL_NUMBER)
        {
            MapShell->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        }
        else if (!bExpanded)
        {
            MapShell->SetVisibility(ESlateVisibility::Collapsed);
        }
        MapShell->SetRenderOpacity(FMath::Clamp(
            (SmoothAlpha - 0.08f) / 0.92f, 0.0f, 1.0f));
        const float Scale = FOntoTwinGlassRenderer::ShouldReduceMotion()
            ? 1.0f : FMath::Lerp(0.97f, 1.0f, SmoothAlpha);
        MapShell->SetRenderScale(FVector2D(Scale, Scale));
    }
    if (ToggleFocusRing && ToggleButton)
    {
        ToggleFocusRing->SetVisibility(
            ToggleButton->HasKeyboardFocus()
                ? ESlateVisibility::SelfHitTestInvisible
                : ESlateVisibility::Collapsed);
    }
}

void UOntoTwinMinimapWidget::OnToggleExpanded()
{
    if (!ToggleButton || !ToggleButton->GetIsEnabled()) return;
    bExpanded = !bExpanded;
    if (bExpanded && MapShell)
    {
        MapShell->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    }
    UpdateToggleSemantics();
    ApplyExpansionVisuals();
    InvalidateLayoutAndVolatility();
}

void UOntoTwinMinimapWidget::OnUndoTeleport()
{
    if (Manager) Manager->UndoLastMinimapTeleport();
}

bool UOntoTwinMinimapWidget::TryGetMapUV(
    const FGeometry& Geometry,
    const FVector2D& ScreenPosition,
    FVector2D& OutUV) const
{
    if (!MapTexture || !bExpanded || ExpandAlpha < 0.98f || !Manager)
    {
        return false;
    }

    const FVector2D Local = Geometry.AbsoluteToLocal(ScreenPosition);
    const FSlateRect PanelRect(
        PanelTopLeft.X,
        PanelTopLeft.Y,
        PanelTopLeft.X + PanelLogicalSize.X,
        PanelTopLeft.Y + PanelLogicalSize.Y);
    const FVector2D MapSize(
        PanelLogicalSize.X - ShellPadding * 2.0f,
        PanelLogicalSize.Y - ShellPadding * 2.0f);
    const FVector2D MapLocal = Local
        - FVector2D(PanelRect.Left + ShellPadding, PanelRect.Top + ShellPadding);
    if (MapSize.X <= 1.0f || MapSize.Y <= 1.0f
        || Local.X < PanelRect.Left || Local.Y < PanelRect.Top
        || Local.X > PanelRect.Right || Local.Y > PanelRect.Bottom
        || MapLocal.X < 0.0f || MapLocal.Y < 0.0f
        || MapLocal.X > MapSize.X || MapLocal.Y > MapSize.Y)
    {
        return false;
    }

    // The toggle is a real button; explicitly reserve its rectangle as well so
    // bubbling or Pixel Streaming pointer synthesis cannot become a teleport.
    if (Local.X >= StableButtonTopLeft.X
        && Local.X <= StableButtonTopLeft.X + ToggleSize
        && Local.Y >= StableButtonTopLeft.Y
        && Local.Y <= StableButtonTopLeft.Y + ToggleSize)
    {
        return false;
    }

    const auto OutsideRoundedCorner = [&MapLocal, &MapSize](
        const FVector2D& Corner,
        const bool bCheckX,
        const bool bCheckY)
    {
        if (!bCheckX || !bCheckY) return false;
        return FVector2D::Distance(MapLocal, Corner) > MapCornerRadius;
    };
    if (OutsideRoundedCorner(
            FVector2D(MapCornerRadius, MapCornerRadius),
            MapLocal.X < MapCornerRadius,
            MapLocal.Y < MapCornerRadius)
        || OutsideRoundedCorner(
            FVector2D(MapSize.X - MapCornerRadius, MapCornerRadius),
            MapLocal.X > MapSize.X - MapCornerRadius,
            MapLocal.Y < MapCornerRadius)
        || OutsideRoundedCorner(
            FVector2D(MapCornerRadius, MapSize.Y - MapCornerRadius),
            MapLocal.X < MapCornerRadius,
            MapLocal.Y > MapSize.Y - MapCornerRadius)
        || OutsideRoundedCorner(
            FVector2D(MapSize.X - MapCornerRadius, MapSize.Y - MapCornerRadius),
            MapLocal.X > MapSize.X - MapCornerRadius,
            MapLocal.Y > MapSize.Y - MapCornerRadius))
    {
        return false;
    }

    OutUV.X = FMath::Clamp(MapLocal.X / MapSize.X, 0.0f, 1.0f);
    OutUV.Y = FMath::Clamp(MapLocal.Y / MapSize.Y, 0.0f, 1.0f);
    return true;
}

void UOntoTwinMinimapWidget::HandleMapPreview(const FVector2D& UV)
{
    if (Manager) Manager->PreviewMinimapTeleport(UV);
}

void UOntoTwinMinimapWidget::HandleMapTeleport(const FVector2D& UV)
{
    if (Manager) Manager->RequestMinimapTeleport(UV);
}

FReply UOntoTwinMinimapWidget::NativeOnMouseButtonDown(
    const FGeometry& InGeometry,
    const FPointerEvent& InMouseEvent)
{
    if (InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
    {
        return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
    }
    FVector2D UV;
    if (!TryGetMapUV(InGeometry, InMouseEvent.GetScreenSpacePosition(), UV))
    {
        return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
    }
    HandleMapPreview(UV);
    return FReply::Handled();
}

FReply UOntoTwinMinimapWidget::NativeOnMouseButtonDoubleClick(
    const FGeometry& InGeometry,
    const FPointerEvent& InMouseEvent)
{
    if (InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
    {
        return Super::NativeOnMouseButtonDoubleClick(InGeometry, InMouseEvent);
    }
    FVector2D UV;
    if (!TryGetMapUV(InGeometry, InMouseEvent.GetScreenSpacePosition(), UV))
    {
        return Super::NativeOnMouseButtonDoubleClick(InGeometry, InMouseEvent);
    }
    HandleMapTeleport(UV);
    return FReply::Handled();
}

FReply UOntoTwinMinimapWidget::NativeOnTouchStarted(
    const FGeometry& InGeometry,
    const FPointerEvent& InGestureEvent)
{
    FVector2D UV;
    if (!TryGetMapUV(InGeometry, InGestureEvent.GetScreenSpacePosition(), UV))
    {
        return Super::NativeOnTouchStarted(InGeometry, InGestureEvent);
    }

    const double Now = FPlatformTime::Seconds();
    const FVector2D Local = InGeometry.AbsoluteToLocal(
        InGestureEvent.GetScreenSpacePosition());
    const bool bIsSecondTap = LastTouchSeconds >= 0.0
        && Now - LastTouchSeconds <= TouchDoubleClickSeconds
        && FVector2D::Distance(Local, LastTouchLocal) <= TouchDoubleClickDistance;
    LastTouchSeconds = bIsSecondTap ? -1.0 : Now;
    LastTouchLocal = Local;
    if (bIsSecondTap) HandleMapTeleport(UV);
    else HandleMapPreview(UV);
    return FReply::Handled();
}

void UOntoTwinMinimapWidget::SetMarker(
    const FVector2D& InUV,
    float InAngleDegrees,
    bool bInOffMap)
{
    MarkerUV.X = FMath::Clamp(InUV.X, 0.0f, 1.0f);
    MarkerUV.Y = FMath::Clamp(InUV.Y, 0.0f, 1.0f);
    MarkerAngleDegrees = InAngleDegrees;
    bMarkerOffMap = bInOffMap;
    bMarkerVisible = MapTexture != nullptr;
    InvalidateLayoutAndVolatility();
}

void UOntoTwinMinimapWidget::SetTeleportFeedback(
    const FVector2D& InUV,
    ETwinMinimapTeleportFeedback InFeedback,
    const FString& InMessage)
{
    TeleportTargetUV.X = FMath::Clamp(InUV.X, 0.0f, 1.0f);
    TeleportTargetUV.Y = FMath::Clamp(InUV.Y, 0.0f, 1.0f);
    TeleportFeedback = InFeedback;
    bTeleportTargetVisible = MapTexture
        && InFeedback != ETwinMinimapTeleportFeedback::Hidden;
    FeedbackSecondsRemaining = InFeedback == ETwinMinimapTeleportFeedback::Invalid
        ? 2.0f : 1.5f;

    FLinearColor Accent = FeedbackValid;
    if (InFeedback == ETwinMinimapTeleportFeedback::Adjusted)
        Accent = FeedbackAdjusted;
    else if (InFeedback == ETwinMinimapTeleportFeedback::Invalid)
        Accent = FeedbackInvalid;
    else if (InFeedback == ETwinMinimapTeleportFeedback::Success)
        Accent = FeedbackSuccess;
    if (TeleportStatusPanel)
    {
        TeleportStatusPanel->SetBrush(FSlateRoundedBoxBrush(
            FeedbackFill, 9.0f, Accent, 1.0f));
        TeleportStatusPanel->SetVisibility(
            InFeedback == ETwinMinimapTeleportFeedback::Hidden
                ? ESlateVisibility::Collapsed
                : ESlateVisibility::SelfHitTestInvisible);
    }
    if (TeleportStatusText)
    {
        TeleportStatusText->SetText(FText::FromString(InMessage));
    }
    InvalidateLayoutAndVolatility();
}

void UOntoTwinMinimapWidget::SetUndoAvailable(bool bAvailable)
{
    if (UndoButton)
    {
        UndoButton->SetVisibility(
            bAvailable ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    }
}

void UOntoTwinMinimapWidget::ClearTeleportFeedback()
{
    FeedbackSecondsRemaining = 0.0f;
    TeleportFeedback = ETwinMinimapTeleportFeedback::Hidden;
    bTeleportTargetVisible = false;
    if (TeleportStatusPanel)
    {
        TeleportStatusPanel->SetVisibility(ESlateVisibility::Collapsed);
    }
    InvalidateLayoutAndVolatility();
}

void UOntoTwinMinimapWidget::HideMarker()
{
    bMarkerVisible = false;
    InvalidateLayoutAndVolatility();
}

void UOntoTwinMinimapWidget::ClearMap()
{
    MapTexture = nullptr;
    bMarkerVisible = false;
    bMarkerOffMap = false;
    SetUndoAvailable(false);
    ClearTeleportFeedback();
    ApplyMapTexture();
    if (ToggleButton) ToggleButton->SetIsEnabled(false);
    SetVisibility(ESlateVisibility::Collapsed);
}

int32 UOntoTwinMinimapWidget::NativePaint(
    const FPaintArgs& Args,
    const FGeometry& AllottedGeometry,
    const FSlateRect& MyCullingRect,
    FSlateWindowElementList& OutDrawElements,
    int32 LayerId,
    const FWidgetStyle& InWidgetStyle,
    bool bParentEnabled) const
{
    const int32 BaseLayer = Super::NativePaint(
        Args,
        AllottedGeometry,
        MyCullingRect,
        OutDrawElements,
        LayerId,
        InWidgetStyle,
        bParentEnabled);
    int32 PaintedLayer = BaseLayer;

    if (bTeleportTargetVisible && MapTexture && ExpandAlpha > 0.98f)
    {
        const FSlateRect PanelRect = GetAnimatedPanelRect();
        const FVector2D MapSize(
            FMath::Max(1.0f, PanelRect.Right - PanelRect.Left - ShellPadding * 2.0f),
            FMath::Max(1.0f, PanelRect.Bottom - PanelRect.Top - ShellPadding * 2.0f));
        const FVector2D Center(
            PanelRect.Left + ShellPadding + TeleportTargetUV.X * MapSize.X,
            PanelRect.Top + ShellPadding + TeleportTargetUV.Y * MapSize.Y);
        FLinearColor Accent = FeedbackValid;
        if (TeleportFeedback == ETwinMinimapTeleportFeedback::Adjusted)
            Accent = FeedbackAdjusted;
        else if (TeleportFeedback == ETwinMinimapTeleportFeedback::Invalid)
            Accent = FeedbackInvalid;
        else if (TeleportFeedback == ETwinMinimapTeleportFeedback::Success)
            Accent = FeedbackSuccess;

        TArray<FVector2f> Ring;
        constexpr int32 SegmentCount = 20;
        Ring.Reserve(SegmentCount + 1);
        for (int32 Index = 0; Index <= SegmentCount; ++Index)
        {
            const float Angle = 2.0f * PI * static_cast<float>(Index)
                / static_cast<float>(SegmentCount);
            Ring.Add(FVector2f(Center + FVector2D(
                FMath::Cos(Angle), FMath::Sin(Angle)) * 9.0f));
        }
        FSlateDrawElement::MakeLines(
            OutDrawElements,
            BaseLayer + 1,
            AllottedGeometry.ToPaintGeometry(),
            Ring,
            ESlateDrawEffect::None,
            Accent,
            true,
            2.0f);
        TArray<FVector2f> CrossA = {
            FVector2f(Center + FVector2D(-4.0f, 0.0f)),
            FVector2f(Center + FVector2D(4.0f, 0.0f))};
        TArray<FVector2f> CrossB = {
            FVector2f(Center + FVector2D(0.0f, -4.0f)),
            FVector2f(Center + FVector2D(0.0f, 4.0f))};
        FSlateDrawElement::MakeLines(
            OutDrawElements, BaseLayer + 1,
            AllottedGeometry.ToPaintGeometry(), CrossA,
            ESlateDrawEffect::None, Accent, true, 1.5f);
        FSlateDrawElement::MakeLines(
            OutDrawElements, BaseLayer + 1,
            AllottedGeometry.ToPaintGeometry(), CrossB,
            ESlateDrawEffect::None, Accent, true, 1.5f);
        PaintedLayer = BaseLayer + 1;
    }

    if (bMarkerVisible && MapTexture && ExpandAlpha > 0.12f)
    {
        const FSlateRect PanelRect = GetAnimatedPanelRect();
        const FVector2D MapSize(
            FMath::Max(1.0f, PanelRect.Right - PanelRect.Left - ShellPadding * 2.0f),
            FMath::Max(1.0f, PanelRect.Bottom - PanelRect.Top - ShellPadding * 2.0f));
        const FVector2D Center(
            PanelRect.Left + ShellPadding + MarkerUV.X * MapSize.X,
            PanelRect.Top + ShellPadding + MarkerUV.Y * MapSize.Y);
        const float AngleRadians = FMath::DegreesToRadians(MarkerAngleDegrees);
        const FVector2D Direction(FMath::Cos(AngleRadians), FMath::Sin(AngleRadians));
        const FVector2D Perpendicular(-Direction.Y, Direction.X);
        const float Pulse = FOntoTwinGlassRenderer::ShouldReduceMotion()
            ? 1.0f
            : 0.5f + 0.5f * FMath::Sin(MarkerPulsePhase);
        const float PulseScale = FMath::Lerp(0.92f, 1.10f, Pulse);
        const float TipDistance = (bMarkerOffMap ? 11.0f : 10.0f) * PulseScale;
        const float BackDistance = (bMarkerOffMap ? 8.0f : 7.0f) * PulseScale;
        const float HalfWidth = (bMarkerOffMap ? 7.0f : 6.0f) * PulseScale;
        const FVector2D Tip = Center + Direction * TipDistance;
        const FVector2D Back = Center - Direction * BackDistance;

        FLinearColor PulsedRed = MarkerRed;
        PulsedRed.A *= FMath::Lerp(0.52f, 1.0f, Pulse);

        const FSlateRenderTransform& AccumulatedRenderTransform =
            AllottedGeometry.GetAccumulatedRenderTransform();
        const FVector2f TextureCoordinate(0.0f, 0.0f);
        const FSlateResourceHandle WhiteResource =
            FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"))->GetRenderingResource();
        const TArray<SlateIndex> TriangleIndices = {0, 1, 2};
        const auto DrawFilledTriangle = [&OutDrawElements, &AccumulatedRenderTransform,
            &TextureCoordinate, &WhiteResource, &TriangleIndices](
                int32 DrawLayer,
                const FVector2D& DrawTip,
                const FVector2D& DrawLeft,
                const FVector2D& DrawRight,
                const FLinearColor& DrawColor)
        {
            const FColor VertexColor = DrawColor.ToFColor(true);
            const TArray<FSlateVertex> Vertices = {
                FSlateVertex::Make<ESlateVertexRounding::Disabled>(
                    AccumulatedRenderTransform, FVector2f(DrawTip), TextureCoordinate, VertexColor),
                FSlateVertex::Make<ESlateVertexRounding::Disabled>(
                    AccumulatedRenderTransform, FVector2f(DrawLeft), TextureCoordinate, VertexColor),
                FSlateVertex::Make<ESlateVertexRounding::Disabled>(
                    AccumulatedRenderTransform, FVector2f(DrawRight), TextureCoordinate, VertexColor),
            };
            FSlateDrawElement::MakeCustomVerts(
                OutDrawElements,
                DrawLayer,
                WhiteResource,
                Vertices,
                TriangleIndices,
                nullptr,
                0,
                0);
        };

        const float OutlineScale = 1.14f;
        const FVector2D OutlineTip = Center + Direction * TipDistance * OutlineScale;
        const FVector2D OutlineBack = Center - Direction * BackDistance * OutlineScale;
        DrawFilledTriangle(
            BaseLayer + 1,
            OutlineTip,
            OutlineBack + Perpendicular * HalfWidth * OutlineScale,
            OutlineBack - Perpendicular * HalfWidth * OutlineScale,
            MarkerOutline);
        DrawFilledTriangle(
            BaseLayer + 2,
            Tip,
            Back + Perpendicular * HalfWidth,
            Back - Perpendicular * HalfWidth,
            PulsedRed);

        if (bMarkerOffMap)
        {
            TArray<FVector2f> EdgeBar;
            EdgeBar.Add(FVector2f(Back + Perpendicular * (HalfWidth + 2.0f)));
            EdgeBar.Add(FVector2f(Back - Perpendicular * (HalfWidth + 2.0f)));
            FSlateDrawElement::MakeLines(
                OutDrawElements,
                BaseLayer + 2,
                AllottedGeometry.ToPaintGeometry(),
                EdgeBar,
                ESlateDrawEffect::None,
                PulsedRed,
                true,
                2.8f);
        }
        PaintedLayer = FMath::Max(PaintedLayer, BaseLayer + 2);
    }

    if (MapTexture && ToggleButton)
    {
        const FVector2D Center = GetButtonCenter();
        const FVector2D MapCenter = Center + FVector2D(-5.0f, 0.0f);
        const float HalfWidth = 9.0f;
        const float HalfHeight = 8.0f;
        TArray<FVector2f> MapGlyph;
        MapGlyph.Reserve(9);
        MapGlyph.Add(FVector2f(MapCenter + FVector2D(-HalfWidth, -HalfHeight + 2.0f)));
        MapGlyph.Add(FVector2f(MapCenter + FVector2D(-3.0f, -HalfHeight)));
        MapGlyph.Add(FVector2f(MapCenter + FVector2D(3.0f, -HalfHeight + 2.0f)));
        MapGlyph.Add(FVector2f(MapCenter + FVector2D(HalfWidth, -HalfHeight)));
        MapGlyph.Add(FVector2f(MapCenter + FVector2D(HalfWidth, HalfHeight - 2.0f)));
        MapGlyph.Add(FVector2f(MapCenter + FVector2D(3.0f, HalfHeight)));
        MapGlyph.Add(FVector2f(MapCenter + FVector2D(-3.0f, HalfHeight - 2.0f)));
        MapGlyph.Add(FVector2f(MapCenter + FVector2D(-HalfWidth, HalfHeight)));
        MapGlyph.Add(FVector2f(MapCenter + FVector2D(-HalfWidth, -HalfHeight + 2.0f)));
        const int32 IconLayer = FMath::Max(PaintedLayer + 1, BaseLayer + 3);
        FSlateDrawElement::MakeLines(
            OutDrawElements,
            IconLayer,
            AllottedGeometry.ToPaintGeometry(),
            MapGlyph,
            ESlateDrawEffect::None,
            ToggleIcon,
            true,
            1.6f);

        TArray<FVector2f> LeftFold;
        LeftFold.Add(FVector2f(MapCenter + FVector2D(-3.0f, -HalfHeight)));
        LeftFold.Add(FVector2f(MapCenter + FVector2D(-3.0f, HalfHeight - 2.0f)));
        FSlateDrawElement::MakeLines(
            OutDrawElements,
            IconLayer,
            AllottedGeometry.ToPaintGeometry(),
            LeftFold,
            ESlateDrawEffect::None,
            ToggleIcon,
            true,
            1.2f);
        TArray<FVector2f> RightFold;
        RightFold.Add(FVector2f(MapCenter + FVector2D(3.0f, -HalfHeight + 2.0f)));
        RightFold.Add(FVector2f(MapCenter + FVector2D(3.0f, HalfHeight)));
        FSlateDrawElement::MakeLines(
            OutDrawElements,
            IconLayer,
            AllottedGeometry.ToPaintGeometry(),
            RightFold,
            ESlateDrawEffect::None,
            ToggleIcon,
            true,
            1.2f);

        // A compact chevron communicates the current expand/collapse vector
        // without changing the established map glyph or adding a second
        // interactive control.
        const FVector2D Growth = DirectionVector(GrowthDirection).GetSafeNormal();
        const FVector2D ChevronDirection = bExpanded ? -Growth : Growth;
        const FVector2D ChevronPerpendicular(-ChevronDirection.Y, ChevronDirection.X);
        const FVector2D ChevronCenter = Center + FVector2D(11.0f, 0.0f);
        const FVector2D ChevronTip = ChevronCenter + ChevronDirection * 4.0f;
        const FVector2D ChevronBack = ChevronCenter - ChevronDirection * 3.0f;
        TArray<FVector2f> Chevron;
        Chevron.Add(FVector2f(ChevronBack + ChevronPerpendicular * 3.0f));
        Chevron.Add(FVector2f(ChevronTip));
        Chevron.Add(FVector2f(ChevronBack - ChevronPerpendicular * 3.0f));
        FSlateDrawElement::MakeLines(
            OutDrawElements,
            IconLayer,
            AllottedGeometry.ToPaintGeometry(),
            Chevron,
            ESlateDrawEffect::None,
            ToggleIcon,
            true,
            1.4f);
        PaintedLayer = IconLayer;
    }
    return PaintedLayer;
}
