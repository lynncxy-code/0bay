#include "SceneInteraction/OntoTwinRuntimeDockWidget.h"

#include "SceneInteraction/TwinInteractionManagerComponent.h"
#include "UI/OntoTwinGlassRenderer.h"
#include "UI/OntoTwinGlassTheme.h"

#include "Blueprint/WidgetTree.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/BackgroundBlur.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ComboBoxString.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace
{
constexpr float CompactDockWidth = 870.0f;
constexpr float ReloadDockWidth = 930.0f;
constexpr float CompactActionWidth = 194.0f;
constexpr float ReloadActionWidth = 254.0f;
constexpr float SectionDividerMargin = 9.0f;
constexpr float DockHeight = 158.0f;
constexpr float ContentHeight = 110.0f;
constexpr float SurfaceRadius = 18.0f;
constexpr float DrawerHandleVisualWidth = 56.0f;
constexpr float DrawerHandleVisualHeight = 24.0f;
constexpr float DrawerHandleHitWidth = 64.0f;
constexpr float DrawerHandleHitHeight = 48.0f;
constexpr float DrawerHandleBottomSafeInset = 4.0f;
constexpr float DrawerHandleTopOverlap = 8.0f;
constexpr float DrawerOpenDuration = 0.22f;
constexpr float DrawerCloseDuration = 0.16f;
constexpr float DrawerReduceMotionDuration = 0.12f;
// Space controls use the same typeface, size, radii and motion as the Dock.
constexpr float DockBodyFontSize = 10.0f;
constexpr float SpaceControlHeight = 44.0f;

FLinearColor InnerFill()
{
    return FLinearColor(0.055f, 0.055f, 0.055f, 0.42f);
}

FLinearColor InnerHover()
{
    return FLinearColor(0.18f, 0.18f, 0.18f, 0.70f);
}

FLinearColor ActiveFill()
{
    return FLinearColor(0.82f, 0.82f, 0.82f, 0.24f);
}

FLinearColor InactiveFill()
{
    return FLinearColor(0.10f, 0.10f, 0.10f, 0.34f);
}

FLinearColor CardRim()
{
    FLinearColor Rim = FOntoTwinGlassTheme::Rim();
    Rim.A = FMath::Min(Rim.A, 0.18f);
    return Rim;
}

FButtonStyle BuildButtonStyle(const float Radius = 8.0f)
{
    FButtonStyle Style = FCoreStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Button"));
    return Style
        .SetNormal(FSlateRoundedBoxBrush(
            InactiveFill(), Radius, CardRim(), 1.0f))
        .SetHovered(FSlateRoundedBoxBrush(
            InnerHover(), Radius, FOntoTwinGlassTheme::Rim(), 1.0f))
        .SetPressed(FSlateRoundedBoxBrush(
            ActiveFill(), Radius, FOntoTwinGlassTheme::Rim(), 1.0f))
        .SetDisabled(FSlateRoundedBoxBrush(
            FLinearColor(0.05f, 0.05f, 0.05f, 0.22f),
            Radius,
            FLinearColor(0.8f, 0.8f, 0.8f, 0.08f),
        1.0f));
}

FButtonStyle BuildTabButtonStyle(const bool bActive)
{
    FButtonStyle Style = FCoreStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Button"));
    const FLinearColor Normal = bActive
        ? FLinearColor(0.96f, 0.96f, 0.96f, 0.15f)
        : FLinearColor::Transparent;
    return Style
        .SetNormal(FSlateRoundedBoxBrush(Normal, 7.0f))
        .SetHovered(FSlateRoundedBoxBrush(
            FLinearColor(0.96f, 0.96f, 0.96f, bActive ? 0.21f : 0.08f), 7.0f))
        .SetPressed(FSlateRoundedBoxBrush(
            FLinearColor(0.96f, 0.96f, 0.96f, 0.25f), 7.0f))
        .SetDisabled(FSlateRoundedBoxBrush(FLinearColor::Transparent, 7.0f))
        .SetNormalForeground(FSlateColor(FOntoTwinGlassTheme::PrimaryText()))
        .SetHoveredForeground(FSlateColor(FOntoTwinGlassTheme::PrimaryText()))
        .SetPressedForeground(FSlateColor(FOntoTwinGlassTheme::PrimaryText()))
        .SetDisabledForeground(FSlateColor(FOntoTwinGlassTheme::MutedText()));
}

FButtonStyle BuildIconButtonStyle(const bool bActive)
{
    FButtonStyle Style = FCoreStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Button"));
    const FLinearColor Normal = bActive
        ? FLinearColor(0.96f, 0.96f, 0.96f, 0.20f)
        : FLinearColor(0.96f, 0.96f, 0.96f, 0.06f);
    const FLinearColor Rim = bActive
        ? FLinearColor(0.96f, 0.96f, 0.96f, 0.24f)
        : FLinearColor::Transparent;
    return Style
        .SetNormal(FSlateRoundedBoxBrush(Normal, 18.0f, Rim, 1.0f))
        .SetHovered(FSlateRoundedBoxBrush(
            FLinearColor(0.96f, 0.96f, 0.96f, 0.15f), 18.0f))
        .SetPressed(FSlateRoundedBoxBrush(
            FLinearColor(0.96f, 0.96f, 0.96f, 0.24f), 18.0f))
        .SetDisabled(FSlateRoundedBoxBrush(
            FLinearColor(0.96f, 0.96f, 0.96f, 0.025f), 18.0f))
        .SetNormalForeground(FSlateColor(FOntoTwinGlassTheme::PrimaryText()))
        .SetHoveredForeground(FSlateColor(FOntoTwinGlassTheme::PrimaryText()))
        .SetPressedForeground(FSlateColor(FOntoTwinGlassTheme::PrimaryText()))
        .SetDisabledForeground(FSlateColor(FOntoTwinGlassTheme::MutedText()));
}

FButtonStyle BuildDrawerHandleStyle()
{
    FButtonStyle Style = FCoreStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Button"));
    const FSlateColor Foreground(FOntoTwinGlassTheme::PrimaryText());
    return Style
        .SetNormal(FSlateRoundedBoxBrush(
            FLinearColor(0.055f, 0.055f, 0.055f, 0.78f),
            DrawerHandleVisualHeight * 0.5f,
            FLinearColor(0.96f, 0.96f, 0.96f, 0.24f),
            1.0f))
        .SetHovered(FSlateRoundedBoxBrush(
            FLinearColor(0.13f, 0.13f, 0.13f, 0.86f),
            DrawerHandleVisualHeight * 0.5f,
            FLinearColor(0.96f, 0.96f, 0.96f, 0.34f),
            1.0f))
        .SetPressed(FSlateRoundedBoxBrush(
            FLinearColor(0.20f, 0.20f, 0.20f, 0.90f),
            DrawerHandleVisualHeight * 0.5f,
            FLinearColor(0.96f, 0.96f, 0.96f, 0.40f),
            1.0f))
        .SetDisabled(FSlateRoundedBoxBrush(
            FLinearColor(0.04f, 0.04f, 0.04f, 0.48f),
            DrawerHandleVisualHeight * 0.5f,
            FLinearColor(0.96f, 0.96f, 0.96f, 0.12f),
            1.0f))
        .SetNormalForeground(Foreground)
        .SetHoveredForeground(Foreground)
        .SetPressedForeground(Foreground)
        .SetDisabledForeground(FSlateColor(FOntoTwinGlassTheme::MutedText()))
        .SetNormalPadding(FMargin(0.0f))
        .SetPressedPadding(FMargin(0.0f));
}

FComboBoxStyle BuildComboStyle()
{
    FComboBoxStyle Style =
        FCoreStyle::Get().GetWidgetStyle<FComboBoxStyle>(TEXT("ComboBox"));
    FComboButtonStyle ComboButton = Style.ComboButtonStyle;
    FButtonStyle SelectorStyle =
        FCoreStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Button"));
    SelectorStyle
        .SetNormal(FSlateRoundedBoxBrush(
            FLinearColor(0.025f, 0.025f, 0.025f, 0.54f), 9.0f))
        .SetHovered(FSlateRoundedBoxBrush(
            FLinearColor(0.075f, 0.075f, 0.075f, 0.66f), 9.0f))
        .SetPressed(FSlateRoundedBoxBrush(
            FLinearColor(0.11f, 0.11f, 0.11f, 0.74f), 9.0f))
        .SetDisabled(FSlateRoundedBoxBrush(
            FLinearColor(0.025f, 0.025f, 0.025f, 0.30f), 9.0f))
        .SetNormalForeground(FSlateColor(FOntoTwinGlassTheme::PrimaryText()))
        .SetHoveredForeground(FSlateColor(FOntoTwinGlassTheme::PrimaryText()))
        .SetPressedForeground(FSlateColor(FOntoTwinGlassTheme::PrimaryText()))
        .SetDisabledForeground(FSlateColor(FOntoTwinGlassTheme::MutedText()));
    FSlateBrush Arrow = ComboButton.DownArrowImage;
    Arrow.TintColor = FSlateColor(FOntoTwinGlassTheme::PrimaryText());
    ComboButton
        .SetButtonStyle(SelectorStyle)
        .SetDownArrowImage(Arrow)
        .SetMenuBorderBrush(FSlateRoundedBoxBrush(
            FLinearColor(0.035f, 0.035f, 0.035f, 0.98f),
            10.0f,
            FOntoTwinGlassTheme::Rim(),
            1.0f))
        .SetMenuBorderPadding(FMargin(4.0f))
        .SetDownArrowPadding(FMargin(8.0f, 0.0f));
    return Style.SetComboButtonStyle(ComboButton).SetMenuRowPadding(FMargin(8.0f, 5.0f));
}

FTableRowStyle BuildComboRowStyle()
{
    const FSlateRoundedBoxBrush Clear(FLinearColor::Transparent, 6.0f);
    const FSlateRoundedBoxBrush Hover(
        FLinearColor(0.92f, 0.92f, 0.92f, 0.10f), 6.0f);
    const FSlateRoundedBoxBrush Selected(
        FLinearColor(0.92f, 0.92f, 0.92f, 0.18f),
        6.0f,
        FOntoTwinGlassTheme::Rim(),
        1.0f);
    FTableRowStyle Style =
        FCoreStyle::Get().GetWidgetStyle<FTableRowStyle>(TEXT("ComboBox.Row"));
    return Style
        .SetSelectorFocusedBrush(Selected)
        .SetActiveHoveredBrush(Selected)
        .SetActiveBrush(Selected)
        .SetInactiveHoveredBrush(Selected)
        .SetInactiveBrush(Selected)
        .SetEvenRowBackgroundHoveredBrush(Hover)
        .SetEvenRowBackgroundBrush(Clear)
        .SetOddRowBackgroundHoveredBrush(Hover)
        .SetOddRowBackgroundBrush(Clear)
        .SetTextColor(FSlateColor(FOntoTwinGlassTheme::PrimaryText()))
        .SetSelectedTextColor(FSlateColor(FOntoTwinGlassTheme::PrimaryText()));
}

UBorder* MakeInnerCard(
    UWidgetTree* Tree,
    const FName Name,
    const FMargin& Padding,
    const float Radius = 12.0f)
{
    UBorder* Card = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), Name);
    Card->SetPadding(Padding);
    Card->SetBrush(FSlateRoundedBoxBrush(InnerFill(), Radius, CardRim(), 1.0f));
    Card->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    return Card;
}

FString JoinPathNames(
    const TArray<FString>& Path,
    const TArray<FString>& Ids,
    const TArray<FString>& Names)
{
    TArray<FString> Parts;
    for (const FString& Id : Path)
    {
        const int32 Index = Ids.IndexOfByKey(Id);
        Parts.Add(Names.IsValidIndex(Index) && !Names[Index].IsEmpty()
            ? Names[Index] : TEXT("未命名空间"));
    }
    return Parts.Num() > 0 ? FString::Join(Parts, TEXT("  /  ")) : TEXT("全部空间");
}
}

void UOntoTwinRuntimeDockIconWidget::SetIcon(EOntoTwinRuntimeDockIcon InIcon)
{
    Icon = InIcon;
    SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    InvalidateLayoutAndVolatility();
}

int32 UOntoTwinRuntimeDockIconWidget::NativePaint(
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
    const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
    const float Scale = FMath::Max(0.1f, FMath::Min(LocalSize.X, LocalSize.Y) / 20.0f);
    const FVector2D Offset =
        (LocalSize - FVector2D(20.0f * Scale, 20.0f * Scale)) * 0.5f;
    const auto P = [&Offset, Scale](const float X, const float Y)
    {
        return FVector2f(
            static_cast<float>(Offset.X + X * Scale),
            static_cast<float>(Offset.Y + Y * Scale));
    };
    const FLinearColor Tint =
        FOntoTwinGlassTheme::PrimaryText() * InWidgetStyle.GetColorAndOpacityTint();
    const float Stroke =
        (Icon == EOntoTwinRuntimeDockIcon::DrawerUp
            || Icon == EOntoTwinRuntimeDockIcon::DrawerDown
            ? 2.0f
            : 1.55f) * Scale;
    const int32 PaintLayer = BaseLayer + 1;
    const auto Draw = [&](TArray<FVector2f> Points)
    {
        if (Points.Num() < 2)
        {
            return;
        }
        FSlateDrawElement::MakeLines(
            OutDrawElements,
            PaintLayer,
            AllottedGeometry.ToPaintGeometry(),
            MoveTemp(Points),
            ESlateDrawEffect::None,
            Tint,
            true,
            Stroke);
    };
    const auto Circle = [&](const float X, const float Y, const float Radius)
    {
        TArray<FVector2f> Points;
        constexpr int32 SegmentCount = 18;
        Points.Reserve(SegmentCount + 1);
        for (int32 Index = 0; Index <= SegmentCount; ++Index)
        {
            const float Angle = 2.0f * PI * static_cast<float>(Index) / SegmentCount;
            Points.Add(P(X + FMath::Cos(Angle) * Radius, Y + FMath::Sin(Angle) * Radius));
        }
        Draw(MoveTemp(Points));
    };
    const auto Arc = [&](
        const float X,
        const float Y,
        const float Radius,
        const float StartDegrees,
        const float EndDegrees)
    {
        TArray<FVector2f> Points;
        constexpr int32 SegmentCount = 14;
        Points.Reserve(SegmentCount + 1);
        for (int32 Index = 0; Index <= SegmentCount; ++Index)
        {
            const float Alpha = static_cast<float>(Index) / SegmentCount;
            const float Angle = FMath::DegreesToRadians(
                FMath::Lerp(StartDegrees, EndDegrees, Alpha));
            Points.Add(P(X + FMath::Cos(Angle) * Radius, Y + FMath::Sin(Angle) * Radius));
        }
        Draw(MoveTemp(Points));
    };

    switch (Icon)
    {
    case EOntoTwinRuntimeDockIcon::Home:
        Draw({P(3.0f, 9.0f), P(10.0f, 3.0f), P(17.0f, 9.0f)});
        Draw({P(5.0f, 8.0f), P(5.0f, 17.0f), P(15.0f, 17.0f), P(15.0f, 8.0f)});
        Draw({P(9.0f, 17.0f), P(9.0f, 12.0f), P(12.0f, 12.0f), P(12.0f, 17.0f)});
        break;
    case EOntoTwinRuntimeDockIcon::DrawerUp:
        Draw({P(4.0f, 13.0f), P(10.0f, 7.0f), P(16.0f, 13.0f)});
        break;
    case EOntoTwinRuntimeDockIcon::DrawerDown:
        Draw({P(4.0f, 7.0f), P(10.0f, 13.0f), P(16.0f, 7.0f)});
        break;
    case EOntoTwinRuntimeDockIcon::ViewGlobal:
        Draw({P(2.0f, 10.0f), P(5.0f, 6.0f), P(10.0f, 4.0f), P(15.0f, 6.0f), P(18.0f, 10.0f)});
        Draw({P(2.0f, 10.0f), P(5.0f, 14.0f), P(10.0f, 16.0f), P(15.0f, 14.0f), P(18.0f, 10.0f)});
        Circle(10.0f, 10.0f, 2.6f);
        break;
    case EOntoTwinRuntimeDockIcon::ViewShoulder:
        Circle(5.5f, 6.0f, 2.5f);
        Draw({P(1.8f, 16.5f), P(2.8f, 11.0f), P(8.2f, 10.5f), P(10.5f, 16.5f)});
        Draw({P(12.0f, 7.0f), P(12.0f, 4.0f), P(15.0f, 4.0f)});
        Draw({P(18.0f, 7.0f), P(18.0f, 4.0f), P(15.0f, 4.0f)});
        Draw({P(12.0f, 13.0f), P(12.0f, 16.0f), P(15.0f, 16.0f)});
        Draw({P(18.0f, 13.0f), P(18.0f, 16.0f), P(15.0f, 16.0f)});
        Circle(15.0f, 10.0f, 1.2f);
        break;
    case EOntoTwinRuntimeDockIcon::ViewFirstPerson:
        Draw({P(7.0f, 3.0f), P(3.0f, 3.0f), P(3.0f, 7.0f)});
        Draw({P(13.0f, 3.0f), P(17.0f, 3.0f), P(17.0f, 7.0f)});
        Draw({P(3.0f, 13.0f), P(3.0f, 17.0f), P(7.0f, 17.0f)});
        Draw({P(17.0f, 13.0f), P(17.0f, 17.0f), P(13.0f, 17.0f)});
        Circle(10.0f, 10.0f, 1.5f);
        break;
    case EOntoTwinRuntimeDockIcon::Crosshair:
        Circle(10.0f, 10.0f, 4.0f);
        Draw({P(10.0f, 2.0f), P(10.0f, 6.0f)});
        Draw({P(10.0f, 14.0f), P(10.0f, 18.0f)});
        Draw({P(2.0f, 10.0f), P(6.0f, 10.0f)});
        Draw({P(14.0f, 10.0f), P(18.0f, 10.0f)});
        break;
    case EOntoTwinRuntimeDockIcon::Skin:
        Draw({P(7.0f, 4.0f), P(3.0f, 6.5f), P(5.0f, 10.0f), P(7.0f, 9.0f),
              P(7.0f, 17.0f), P(13.0f, 17.0f), P(13.0f, 9.0f), P(15.0f, 10.0f),
              P(17.0f, 6.5f), P(13.0f, 4.0f)});
        Draw({P(7.0f, 4.0f), P(10.0f, 6.5f), P(13.0f, 4.0f)});
        break;
    case EOntoTwinRuntimeDockIcon::ReturnRoute:
        Draw({P(17.0f, 15.0f), P(17.0f, 9.0f), P(4.0f, 9.0f)});
        Draw({P(8.0f, 5.0f), P(4.0f, 9.0f), P(8.0f, 13.0f)});
        break;
    case EOntoTwinRuntimeDockIcon::RestartRoute:
        Draw({P(6.5f, 4.5f), P(15.0f, 10.0f), P(6.5f, 15.5f), P(6.5f, 4.5f)});
        break;
    case EOntoTwinRuntimeDockIcon::ReloadCharacter:
        Circle(6.0f, 6.0f, 2.2f);
        Draw({P(2.5f, 16.0f), P(3.5f, 11.0f), P(8.5f, 11.0f), P(9.5f, 16.0f)});
        Arc(12.0f, 10.0f, 5.5f, -75.0f, 70.0f);
        Draw({P(17.0f, 14.0f), P(14.0f, 15.2f), P(14.5f, 12.0f)});
        break;
    }
    return PaintLayer;
}

void UOntoTwinRuntimeDockButton::Configure(
    UOntoTwinRuntimeDockWidget* InOwner,
    EOntoTwinRuntimeDockAction InAction,
    const FString& InPayload,
    int32 InDepth)
{
    DockOwner = InOwner;
    DockAction = InAction;
    Payload = InPayload;
    Depth = InDepth;
    OnClicked.RemoveDynamic(this, &UOntoTwinRuntimeDockButton::HandleClicked);
    OnClicked.AddDynamic(this, &UOntoTwinRuntimeDockButton::HandleClicked);
}

void UOntoTwinRuntimeDockButton::HandleClicked()
{
    if (DockOwner)
    {
        DockOwner->HandleDockAction(DockAction, Payload, Depth);
    }
}

void UOntoTwinRuntimeDockButton::SetAccessibleLabel(const FText& InLabel)
{
    SetToolTipText(InLabel);

#if WITH_EDITORONLY_DATA
    bOverrideAccessibleDefaults = true;
    bCanChildrenBeAccessible = false;
    AccessibleBehavior = ESlateAccessibleBehavior::Custom;
    AccessibleSummaryBehavior = ESlateAccessibleBehavior::Custom;
    AccessibleText = InLabel;
    AccessibleSummaryText = InLabel;
    SynchronizeAccessibleData();
#endif
}

TSharedRef<SWidget> UOntoTwinRuntimeDockWidget::RebuildWidget()
{
    if (!WidgetTree)
    {
        WidgetTree = NewObject<UWidgetTree>(this, TEXT("RuntimeDockWidgetTree"), RF_Transient);
    }
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        BuildDefaultLayout();
    }
    TSharedRef<SWidget> Result = Super::RebuildWidget();
    RefreshFromManager();
    SetDockOpen(bDockOpen);
    return Result;
}

void UOntoTwinRuntimeDockWidget::SetInteractionManager(
    UTwinInteractionManagerComponent* InManager)
{
    Manager = InManager;
    RefreshFromManager();
}

UTextBlock* UOntoTwinRuntimeDockWidget::MakeText(
    const FName Name,
    const FString& Text,
    float Size,
    bool bSemibold,
    const FLinearColor& Color)
{
    UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(
        UTextBlock::StaticClass(), Name);
    Label->SetText(FText::FromString(Text));
    Label->SetFont(FOntoTwinGlassTheme::Font(Size, bSemibold));
    Label->SetColorAndOpacity(Color);
    Label->SetShadowOffset(FVector2D(1.0f, 1.0f));
    Label->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.52f));
    Label->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    return Label;
}

UOntoTwinRuntimeDockButton* UOntoTwinRuntimeDockWidget::MakeButton(
    const FName Name,
    const FString& Label,
    EOntoTwinRuntimeDockAction Action,
    const FString& Payload,
    int32 Depth,
    bool bCompact)
{
    UOntoTwinRuntimeDockButton* Button =
        WidgetTree->ConstructWidget<UOntoTwinRuntimeDockButton>(
            UOntoTwinRuntimeDockButton::StaticClass(), Name);
    Button->Configure(this, Action, Payload, Depth);
    Button->SetAccessibleLabel(FText::FromString(Label));
    Button->SetStyle(BuildButtonStyle(bCompact ? 7.0f : 9.0f));
    UTextBlock* Text = MakeText(
        NAME_None,
        Label,
        bCompact ? DockBodyFontSize : 11.0f,
        false,
        FOntoTwinGlassTheme::PrimaryText());
    Text->SetJustification(ETextJustify::Center);
    Button->AddChild(Text);
    if (UButtonSlot* ContentSlot = Cast<UButtonSlot>(Text->Slot))
    {
        ContentSlot->SetPadding(
            bCompact ? FMargin(8.0f, 4.0f) : FMargin(11.0f, 7.0f));
    }
    return Button;
}

UOverlay* UOntoTwinRuntimeDockWidget::MakeGlassLayers(const FName Name)
{
    UOverlay* GlassLayers = WidgetTree->ConstructWidget<UOverlay>(
        UOverlay::StaticClass(), Name);
    GlassLayers->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

    const FVector4 Corners(
        SurfaceRadius, SurfaceRadius, SurfaceRadius, SurfaceRadius);
    const FOntoTwinGlassDecision GlassDecision = FOntoTwinGlassRenderer::Resolve(false);
    const bool bUseHigh =
        GlassDecision.EffectiveQuality == EOntoTwinGlassQuality::High
        && GlassDecision.HighMaterial != nullptr;
    const bool bUseBalanced =
        GlassDecision.EffectiveQuality == EOntoTwinGlassQuality::Balanced;

    UImage* HighGlass = WidgetTree->ConstructWidget<UImage>(
        UImage::StaticClass(), NAME_None);
    if (bUseHigh)
    {
        FSlateRoundedBoxBrush HighBrush(FLinearColor::White, Corners);
        HighBrush.ImageType = ESlateBrushImageType::FullColor;
        HighBrush.SetResourceObject(GlassDecision.HighMaterial);
        HighGlass->SetBrush(HighBrush);
    }
    HighGlass->SetVisibility(bUseHigh
        ? ESlateVisibility::SelfHitTestInvisible
        : ESlateVisibility::Collapsed);
    UOverlaySlot* HighSlot = GlassLayers->AddChildToOverlay(HighGlass);
    HighSlot->SetHorizontalAlignment(HAlign_Fill);
    HighSlot->SetVerticalAlignment(VAlign_Fill);

    UBackgroundBlur* BalancedBlur = WidgetTree->ConstructWidget<UBackgroundBlur>(
        UBackgroundBlur::StaticClass(), NAME_None);
    BalancedBlur->SetBlurStrength(16.0f);
    BalancedBlur->SetApplyAlphaToBlur(true);
    BalancedBlur->SetCornerRadius(Corners);
    BalancedBlur->SetLowQualityFallbackBrush(FSlateRoundedBoxBrush(
        FOntoTwinGlassTheme::ScreenTint(EOntoTwinGlassQuality::Performance),
        SurfaceRadius,
        FOntoTwinGlassTheme::Rim(),
        1.0f));
    BalancedBlur->SetVisibility(bUseBalanced
        ? ESlateVisibility::SelfHitTestInvisible
        : ESlateVisibility::Collapsed);
    UOverlaySlot* BlurSlot = GlassLayers->AddChildToOverlay(BalancedBlur);
    BlurSlot->SetHorizontalAlignment(HAlign_Fill);
    BlurSlot->SetVerticalAlignment(VAlign_Fill);

    FLinearColor SurfaceTint =
        FOntoTwinGlassTheme::ScreenTint(GlassDecision.EffectiveQuality);
    FLinearColor SurfaceRim = FOntoTwinGlassTheme::Rim();
    if (FOntoTwinGlassRenderer::ShouldUseHighContrast())
    {
        SurfaceTint.A = FMath::Clamp(SurfaceTint.A + 0.12f, 0.0f, 0.96f);
        SurfaceRim.A = 0.38f;
    }
    UBorder* TintLayer = WidgetTree->ConstructWidget<UBorder>(
        UBorder::StaticClass(), NAME_None);
    TintLayer->SetBrush(FSlateRoundedBoxBrush(
        SurfaceTint, SurfaceRadius, SurfaceRim, 1.0f));
    TintLayer->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UOverlaySlot* TintSlot = GlassLayers->AddChildToOverlay(TintLayer);
    TintSlot->SetHorizontalAlignment(HAlign_Fill);
    TintSlot->SetVerticalAlignment(VAlign_Fill);

    UImage* NoiseLayer = WidgetTree->ConstructWidget<UImage>(
        UImage::StaticClass(), NAME_None);
    if (UTexture2D* Noise = FOntoTwinGlassTheme::FineNoiseTexture())
    {
        FSlateBrush NoiseBrush;
        NoiseBrush.DrawAs = ESlateBrushDrawType::Image;
        NoiseBrush.ImageSize = FVector2D(32.0f, 32.0f);
        NoiseBrush.Tiling = ESlateBrushTileType::Both;
        NoiseBrush.SetResourceObject(Noise);
        NoiseLayer->SetBrush(NoiseBrush);
    }
    const float NoiseOpacity =
        GlassDecision.EffectiveQuality == EOntoTwinGlassQuality::High ? 0.018f
        : GlassDecision.EffectiveQuality == EOntoTwinGlassQuality::Balanced ? 0.012f
        : 0.0f;
    NoiseLayer->SetRenderOpacity(NoiseOpacity);
    NoiseLayer->SetVisibility(NoiseOpacity > 0.0f
        ? ESlateVisibility::SelfHitTestInvisible
        : ESlateVisibility::Collapsed);
    UOverlaySlot* NoiseSlot = GlassLayers->AddChildToOverlay(NoiseLayer);
    NoiseSlot->SetHorizontalAlignment(HAlign_Fill);
    NoiseSlot->SetVerticalAlignment(VAlign_Fill);

    return GlassLayers;
}

void UOntoTwinRuntimeDockWidget::BuildDefaultLayout()
{
    UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
        UCanvasPanel::StaticClass(), TEXT("RuntimeDockCanvas"));
    Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    WidgetTree->RootWidget = Root;

    DockTrigger = WidgetTree->ConstructWidget<USizeBox>(
        USizeBox::StaticClass(), TEXT("RuntimeDockTriggerBounds"));
    DockTrigger->SetWidthOverride(DrawerHandleHitWidth);
    DockTrigger->SetHeightOverride(DrawerHandleHitHeight);
    DockTriggerButton =
        WidgetTree->ConstructWidget<UOntoTwinRuntimeDockButton>(
            UOntoTwinRuntimeDockButton::StaticClass(), TEXT("RuntimeDockTrigger"));
    DockTriggerButton->Configure(this, EOntoTwinRuntimeDockAction::ToggleDock);
    DockTriggerButton->SetStyle(BuildDrawerHandleStyle());
    DockTriggerButton->SetAccessibleLabel(FText::FromString(TEXT("展开控制面板")));
    DockTriggerButton->SetVisibility(ESlateVisibility::Visible);

    UOverlay* TriggerOverlay = WidgetTree->ConstructWidget<UOverlay>(
        UOverlay::StaticClass(), TEXT("RuntimeDockTriggerOverlay"));
    TriggerOverlay->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    DockTrigger->AddChild(TriggerOverlay);

    USizeBox* TriggerVisualBounds = WidgetTree->ConstructWidget<USizeBox>(
        USizeBox::StaticClass(), TEXT("RuntimeDockTriggerVisualBounds"));
    TriggerVisualBounds->SetWidthOverride(DrawerHandleVisualWidth);
    TriggerVisualBounds->SetHeightOverride(DrawerHandleVisualHeight);
    TriggerVisualBounds->AddChild(DockTriggerButton);
    UOverlaySlot* TriggerVisualSlot =
        TriggerOverlay->AddChildToOverlay(TriggerVisualBounds);
    TriggerVisualSlot->SetHorizontalAlignment(HAlign_Center);
    TriggerVisualSlot->SetVerticalAlignment(VAlign_Center);

    USizeBox* TriggerIconBounds = WidgetTree->ConstructWidget<USizeBox>(
        USizeBox::StaticClass(), TEXT("RuntimeDockTriggerIconBounds"));
    TriggerIconBounds->SetWidthOverride(16.0f);
    TriggerIconBounds->SetHeightOverride(16.0f);
    DockTriggerIcon = WidgetTree->ConstructWidget<UOntoTwinRuntimeDockIconWidget>(
        UOntoTwinRuntimeDockIconWidget::StaticClass(), TEXT("RuntimeDockTriggerIcon"));
    DockTriggerIcon->SetIcon(EOntoTwinRuntimeDockIcon::DrawerUp);
    TriggerIconBounds->AddChild(DockTriggerIcon);
    DockTriggerButton->AddChild(TriggerIconBounds);
    if (UButtonSlot* HandleContentSlot = Cast<UButtonSlot>(TriggerIconBounds->Slot))
    {
        HandleContentSlot->SetPadding(FMargin(0.0f));
    }

    USizeBox* FocusRingBounds = WidgetTree->ConstructWidget<USizeBox>(
        USizeBox::StaticClass(), TEXT("RuntimeDockTriggerFocusBounds"));
    FocusRingBounds->SetWidthOverride(DrawerHandleHitWidth);
    FocusRingBounds->SetHeightOverride(DrawerHandleVisualHeight + 2.0f);
    DockTriggerFocusRing = WidgetTree->ConstructWidget<UBorder>(
        UBorder::StaticClass(), TEXT("RuntimeDockTriggerFocusRing"));
    DockTriggerFocusRing->SetBrush(FSlateRoundedBoxBrush(
        FLinearColor::Transparent,
        (DrawerHandleVisualHeight + 2.0f) * 0.5f,
        FLinearColor(1.0f, 1.0f, 1.0f, 0.34f),
        1.0f));
    DockTriggerFocusRing->SetVisibility(ESlateVisibility::Collapsed);
    FocusRingBounds->AddChild(DockTriggerFocusRing);
    UOverlaySlot* FocusRingSlot = TriggerOverlay->AddChildToOverlay(FocusRingBounds);
    FocusRingSlot->SetHorizontalAlignment(HAlign_Center);
    FocusRingSlot->SetVerticalAlignment(VAlign_Center);

    DockTriggerSlot = Root->AddChildToCanvas(DockTrigger);
    DockTriggerSlot->SetAnchors(FAnchors(0.5f, 1.0f));
    DockTriggerSlot->SetAlignment(FVector2D(0.5f, 1.0f));
    DockTriggerSlot->SetPosition(FVector2D::ZeroVector);
    DockTriggerSlot->SetAutoSize(true);
    DockTriggerSlot->SetZOrder(2);

    DockShell = WidgetTree->ConstructWidget<USizeBox>(
        USizeBox::StaticClass(), TEXT("RuntimeDockFixedBounds"));
    DockShell->SetWidthOverride(CompactDockWidth);
    DockShell->SetHeightOverride(DockHeight);
    DockShellSlot = Root->AddChildToCanvas(DockShell);
    DockShellSlot->SetAnchors(FAnchors(0.5f, 1.0f));
    DockShellSlot->SetAlignment(FVector2D(0.5f, 1.0f));
    DockShellSlot->SetPosition(FVector2D::ZeroVector);
    DockShellSlot->SetAutoSize(true);
    DockShellSlot->SetZOrder(4);
    DockTriggerSlot->SetZOrder(5);

    UOverlay* GlassLayers = MakeGlassLayers(TEXT("RuntimeDockGlassLayers"));
    DockShell->AddChild(GlassLayers);

    UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(
        UVerticalBox::StaticClass(), TEXT("RuntimeDockContentStack"));
    Stack->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UOverlaySlot* ContentLayerSlot = GlassLayers->AddChildToOverlay(Stack);
    ContentLayerSlot->SetHorizontalAlignment(HAlign_Fill);
    ContentLayerSlot->SetVerticalAlignment(VAlign_Fill);
    ContentLayerSlot->SetPadding(FMargin(14.0f, 8.0f, 14.0f, 6.0f));

    USizeBox* HeaderBounds = WidgetTree->ConstructWidget<USizeBox>(
        USizeBox::StaticClass(), TEXT("RuntimeDockHeaderBounds"));
    HeaderBounds->SetHeightOverride(30.0f);
    UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>(
        UHorizontalBox::StaticClass(), TEXT("RuntimeDockHeader"));
    Header->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    HeaderBounds->AddChild(Header);
    Stack->AddChildToVerticalBox(HeaderBounds);

    USizeBox* BrandBounds = WidgetTree->ConstructWidget<USizeBox>(
        USizeBox::StaticClass(), TEXT("RuntimeDockBrandBounds"));
    BrandBounds->SetWidthOverride(112.0f);
    BrandBounds->AddChild(MakeText(
        TEXT("RuntimeDockTitle"), TEXT("控制面板"), 11.0f, true,
        FOntoTwinGlassTheme::PrimaryText()));
    UHorizontalBoxSlot* BrandSlot = Header->AddChildToHorizontalBox(BrandBounds);
    BrandSlot->SetVerticalAlignment(VAlign_Center);

    UBorder* NavRail = WidgetTree->ConstructWidget<UBorder>(
        UBorder::StaticClass(), TEXT("RuntimeDockNavigationRail"));
    NavRail->SetBrush(FSlateRoundedBoxBrush(
        FLinearColor(0.025f, 0.025f, 0.025f, 0.38f), 10.0f));
    NavRail->SetPadding(FMargin(2.0f));
    NavRail->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UHorizontalBox* NavRow = WidgetTree->ConstructWidget<UHorizontalBox>(
        UHorizontalBox::StaticClass(), TEXT("RuntimeDockNavigationRow"));
    NavRow->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    NavRail->SetContent(NavRow);
    UHorizontalBoxSlot* NavRailSlot = Header->AddChildToHorizontalBox(NavRail);
    NavRailSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    NavRailSlot->SetVerticalAlignment(VAlign_Center);

    USizeBox* HomeBounds = WidgetTree->ConstructWidget<USizeBox>(
        USizeBox::StaticClass(), TEXT("RuntimeDockHomeBounds"));
    HomeBounds->SetWidthOverride(52.0f);
    HomeBounds->SetHeightOverride(26.0f);
    UOntoTwinRuntimeDockButton* HomeButton =
        WidgetTree->ConstructWidget<UOntoTwinRuntimeDockButton>(
            UOntoTwinRuntimeDockButton::StaticClass(), TEXT("RuntimeDockHome"));
    HomeButton->Configure(this, EOntoTwinRuntimeDockAction::Home);
    HomeButton->SetStyle(BuildTabButtonStyle(false));
    HomeButton->SetToolTipText(FText::FromString(TEXT("返回主页")));
    UTextBlock* HomeLabel = MakeText(
        TEXT("RuntimeDockHomeLabel"), TEXT("主页"), 10.0f, false,
        FOntoTwinGlassTheme::PrimaryText());
    HomeLabel->SetJustification(ETextJustify::Center);
    HomeButton->AddChild(HomeLabel);
    if (UButtonSlot* HomeContentSlot = Cast<UButtonSlot>(HomeLabel->Slot))
    {
        HomeContentSlot->SetPadding(FMargin(8.0f, 3.0f));
    }
    HomeBounds->AddChild(HomeButton);
    UHorizontalBoxSlot* HomeSlot = NavRow->AddChildToHorizontalBox(HomeBounds);
    HomeSlot->SetVerticalAlignment(VAlign_Center);
    HomeSlot->SetPadding(FMargin(0.0f, 0.0f, 3.0f, 0.0f));

    const auto AddNavSeparator = [this, NavRow]()
    {
        USizeBox* SeparatorBounds = WidgetTree->ConstructWidget<USizeBox>(
            USizeBox::StaticClass(), NAME_None);
        SeparatorBounds->SetWidthOverride(1.0f);
        SeparatorBounds->SetHeightOverride(16.0f);
        UBorder* Separator = WidgetTree->ConstructWidget<UBorder>(
            UBorder::StaticClass(), NAME_None);
        Separator->SetBrush(FSlateRoundedBoxBrush(
            FLinearColor(0.96f, 0.96f, 0.96f, 0.13f), 0.5f));
        Separator->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        SeparatorBounds->AddChild(Separator);
        UHorizontalBoxSlot* SeparatorSlot = NavRow->AddChildToHorizontalBox(SeparatorBounds);
        SeparatorSlot->SetVerticalAlignment(VAlign_Center);
        SeparatorSlot->SetPadding(FMargin(3.0f, 0.0f));
    };
    AddNavSeparator();

    SpaceTabButton = MakeButton(
        TEXT("RuntimeDockSpaceTab"), TEXT("空间"),
        EOntoTwinRuntimeDockAction::TabSpace, FString(), INDEX_NONE, true);
    BusinessTabButton = MakeButton(
        TEXT("RuntimeDockBusinessTab"), TEXT("业务"),
        EOntoTwinRuntimeDockAction::TabBusiness, FString(), INDEX_NONE, true);
    RoamingTabButton = MakeButton(
        TEXT("RuntimeDockRoamingTab"), TEXT("漫游"),
        EOntoTwinRuntimeDockAction::TabRoaming, FString(), INDEX_NONE, true);
    const TArray<UOntoTwinRuntimeDockButton*> Tabs = {
        RoamingTabButton, SpaceTabButton, BusinessTabButton};
    for (int32 TabIndex = 0; TabIndex < Tabs.Num(); ++TabIndex)
    {
        if (TabIndex > 0)
        {
            AddNavSeparator();
        }
        UHorizontalBoxSlot* TabSlot = NavRow->AddChildToHorizontalBox(Tabs[TabIndex]);
        TabSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        TabSlot->SetVerticalAlignment(VAlign_Center);
        TabSlot->SetPadding(FMargin(2.0f, 0.0f));
    }

    AddNavSeparator();
    USizeBox* SceneEditBounds = WidgetTree->ConstructWidget<USizeBox>(
        USizeBox::StaticClass(), TEXT("RuntimeDockSceneEditBounds"));
    SceneEditBounds->SetWidthOverride(52.0f);
    SceneEditBounds->SetHeightOverride(26.0f);
    SceneEditButton = WidgetTree->ConstructWidget<UOntoTwinRuntimeDockButton>(
        UOntoTwinRuntimeDockButton::StaticClass(), TEXT("RuntimeDockSceneEdit"));
    SceneEditButton->Configure(this, EOntoTwinRuntimeDockAction::ToggleRuntimeEditor);
    SceneEditButton->SetStyle(BuildTabButtonStyle(false));
    SceneEditButton->SetToolTipText(FText::FromString(TEXT("进入场景编辑（F10）")));
    SceneEditButton->SetAccessibleLabel(FText::FromString(TEXT("进入场景编辑，快捷键 F10")));
    UTextBlock* SceneEditLabel = MakeText(
        TEXT("RuntimeDockSceneEditLabel"), TEXT("编辑"), 10.0f, false,
        FOntoTwinGlassTheme::PrimaryText());
    SceneEditLabel->SetJustification(ETextJustify::Center);
    SceneEditButton->AddChild(SceneEditLabel);
    if (UButtonSlot* SceneEditContentSlot = Cast<UButtonSlot>(SceneEditLabel->Slot))
    {
        SceneEditContentSlot->SetPadding(FMargin(8.0f, 3.0f));
    }
    SceneEditBounds->AddChild(SceneEditButton);
    UHorizontalBoxSlot* SceneEditSlot =
        NavRow->AddChildToHorizontalBox(SceneEditBounds);
    SceneEditSlot->SetVerticalAlignment(VAlign_Center);
    SceneEditSlot->SetPadding(FMargin(2.0f, 0.0f));

    USizeBox* SwitcherBounds = WidgetTree->ConstructWidget<USizeBox>(
        USizeBox::StaticClass(), TEXT("RuntimeDockFixedContentBounds"));
    SwitcherBounds->SetHeightOverride(ContentHeight);
    ContentSwitcher = WidgetTree->ConstructWidget<UWidgetSwitcher>(
        UWidgetSwitcher::StaticClass(), TEXT("RuntimeDockContentSwitcher"));
    SwitcherBounds->AddChild(ContentSwitcher);
    UVerticalBoxSlot* SwitcherSlot = Stack->AddChildToVerticalBox(SwitcherBounds);
    SwitcherSlot->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f));

    BuildRoamingPanel();
    BuildSpacePanel();
    BuildBusinessPanel();

    SetActiveTab(0);
    DockShell->SetVisibility(ESlateVisibility::Collapsed);
    DockTrigger->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

USizeBox* UOntoTwinRuntimeDockWidget::WrapSpaceControl(UWidget* Control)
{
    USizeBox* Bounds = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), NAME_None);
    Bounds->SetHeightOverride(SpaceControlHeight);
    Bounds->SetMinDesiredWidth(SpaceControlHeight);
    Bounds->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    Bounds->AddChild(Control);
    return Bounds;
}

void UOntoTwinSpaceComboBox::Configure(UOntoTwinRuntimeDockWidget* InOwner, const int32 InDepth)
{
    DockOwner = InOwner;
    Depth = InDepth;
}

TSharedRef<SWidget> UOntoTwinSpaceComboBox::RebuildWidget()
{
    TSharedRef<SWidget> Widget = Super::RebuildWidget();
    MyComboBox->SetMenuPlacement(MenuPlacement_AboveAnchor);
    return Widget;
}

void UOntoTwinSpaceComboBox::CloseMenu()
{
    if (MyComboBox.IsValid()) MyComboBox->SetIsOpen(false);
}

TSharedRef<SWidget> UOntoTwinSpaceComboBox::HandleGenerateWidget(TSharedPtr<FString> Item) const
{
    return SNew(SBox).MinDesiredHeight(32.0f).VAlign(VAlign_Center)
    [
        SNew(STextBlock)
        .Text(FText::FromString(Item.IsValid() ? *Item : TEXT("请选择")))
        .Font(FOntoTwinGlassTheme::Font(DockBodyFontSize, false))
        .ColorAndOpacity(FOntoTwinGlassTheme::PrimaryText())
        .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
    ];
}

void UOntoTwinSpaceComboBox::HandleSelectionChanged(TSharedPtr<FString> Item, ESelectInfo::Type SelectionType)
{
    Super::HandleSelectionChanged(Item, SelectionType);
    if (SelectionType == ESelectInfo::Direct || !DockOwner || !Item.IsValid()) return;
    const int32 Index = FindOptionIndex(*Item);
    if (ZoneOptionIds.IsValidIndex(Index))
        DockOwner->HandleDockAction(EOntoTwinRuntimeDockAction::SelectZone, ZoneOptionIds[Index], Depth);
}

void UOntoTwinRuntimeDockWidget::BuildSpacePanel()
{
    UVerticalBox* Panel = WidgetTree->ConstructWidget<UVerticalBox>(
        UVerticalBox::StaticClass(), TEXT("RuntimeDockSpacePanel"));
    Panel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    ContentSwitcher->AddChild(Panel);

    SpaceBreadcrumb = MakeText(TEXT("RuntimeDockSpaceBreadcrumb"),
        TEXT("当前位置：全部空间"), DockBodyFontSize, false, FOntoTwinGlassTheme::PrimaryText());
    SpaceBreadcrumb->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
    USizeBox* Location = WrapSpaceControl(SpaceBreadcrumb);
    CastChecked<USizeBoxSlot>(SpaceBreadcrumb->Slot)->SetVerticalAlignment(VAlign_Center);
    Panel->AddChildToVerticalBox(Location);

    UHorizontalBox* TargetRow = WidgetTree->ConstructWidget<UHorizontalBox>(
        UHorizontalBox::StaticClass(), TEXT("RuntimeDockSpaceTargetRow"));
    TargetRow->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UTextBlock* TargetLabel = MakeText(NAME_None, TEXT("目标位置："),
        DockBodyFontSize, false, FOntoTwinGlassTheme::SecondaryText());
    TargetRow->AddChildToHorizontalBox(TargetLabel)->SetVerticalAlignment(VAlign_Center);

    SpaceSelectorsHost = WidgetTree->ConstructWidget<UHorizontalBox>(
        UHorizontalBox::StaticClass(), TEXT("RuntimeDockSpaceSelectors"));
    SpaceSelectorsHost->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UHorizontalBoxSlot* SelectorsSlot = TargetRow->AddChildToHorizontalBox(SpaceSelectorsHost);
    SelectorsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    SelectorsSlot->SetPadding(FMargin(8.0f, 0.0f));

    EnterSpaceButton = MakeButton(TEXT("RuntimeDockEnterSpace"), TEXT("前往位置"),
        EOntoTwinRuntimeDockAction::EnterZone, FString(), INDEX_NONE, true);
    TargetRow->AddChildToHorizontalBox(WrapSpaceControl(EnterSpaceButton));
    UVerticalBoxSlot* TargetSlot = Panel->AddChildToVerticalBox(WrapSpaceControl(TargetRow));
    TargetSlot->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 0.0f));
    RefreshSpaceSelector();
}

void UOntoTwinRuntimeDockWidget::BuildBusinessPanel()
{
    UHorizontalBox* Panel = WidgetTree->ConstructWidget<UHorizontalBox>(
        UHorizontalBox::StaticClass(), TEXT("RuntimeDockBusinessPanel"));
    Panel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    ContentSwitcher->AddChild(Panel);

    USizeBox* ScopeBounds = WidgetTree->ConstructWidget<USizeBox>(
        USizeBox::StaticClass(), TEXT("RuntimeDockBusinessScopeBounds"));
    ScopeBounds->SetWidthOverride(224.0f);
    ScopeBounds->SetClipping(EWidgetClipping::ClipToBounds);
    UBorder* ScopeCard = MakeInnerCard(
        WidgetTree,
        TEXT("RuntimeDockBusinessScopeCard"),
        FMargin(10.0f, 5.0f),
        10.0f);
    ScopeBounds->AddChild(ScopeCard);
    UVerticalBox* ScopeStack = WidgetTree->ConstructWidget<UVerticalBox>(
        UVerticalBox::StaticClass(), TEXT("RuntimeDockBusinessScopeStack"));
    ScopeStack->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    ScopeCard->SetContent(ScopeStack);
    ScopeStack->AddChildToVerticalBox(MakeText(
        TEXT("RuntimeDockBusinessScopeTitle"), TEXT("作用范围"), 10.0f, true,
        FOntoTwinGlassTheme::PrimaryText()));
    ScopeAllButton = MakeButton(
        TEXT("RuntimeDockScopeAll"), TEXT("全部空间"),
        EOntoTwinRuntimeDockAction::ScopeAll, FString(), INDEX_NONE, true);
    UVerticalBoxSlot* ScopeAllSlot = ScopeStack->AddChildToVerticalBox(ScopeAllButton);
    ScopeAllSlot->SetPadding(FMargin(0.0f, 3.0f, 0.0f, 2.0f));
    ScopeCurrentButton = MakeButton(
        TEXT("RuntimeDockScopeCurrent"), TEXT("当前空间"),
        EOntoTwinRuntimeDockAction::ScopeCurrent, FString(), INDEX_NONE, true);
    ScopeStack->AddChildToVerticalBox(ScopeCurrentButton);
    BusinessScopeText = MakeText(
        TEXT("RuntimeDockBusinessScopeText"), TEXT("当前：全部空间"), 8.0f, false,
        FOntoTwinGlassTheme::SecondaryText());
    BusinessScopeText->SetAutoWrapText(false);
    BusinessScopeText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
    UVerticalBoxSlot* ScopeTextSlot = ScopeStack->AddChildToVerticalBox(BusinessScopeText);
    ScopeTextSlot->SetPadding(FMargin(0.0f, 3.0f, 0.0f, 0.0f));
    UHorizontalBoxSlot* ScopeSlot = Panel->AddChildToHorizontalBox(ScopeBounds);
    ScopeSlot->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));

    UBorder* ListCard = MakeInnerCard(
        WidgetTree, TEXT("RuntimeDockBusinessListCard"), FMargin(10.0f), 10.0f);
    UHorizontalBoxSlot* ListCardSlot = Panel->AddChildToHorizontalBox(ListCard);
    ListCardSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    UVerticalBox* ListStack = WidgetTree->ConstructWidget<UVerticalBox>(
        UVerticalBox::StaticClass(), TEXT("RuntimeDockBusinessListStack"));
    ListStack->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    ListCard->SetContent(ListStack);
    ListStack->AddChildToVerticalBox(MakeText(
        TEXT("RuntimeDockBusinessTitle"), TEXT("业务视图"), 10.0f, true,
        FOntoTwinGlassTheme::PrimaryText()));
    UTextBlock* ListHint = MakeText(
        TEXT("RuntimeDockBusinessHint"), TEXT("选择业务进入对应页面"),
        9.0f, false, FOntoTwinGlassTheme::MutedText());
    UVerticalBoxSlot* ListHintSlot = ListStack->AddChildToVerticalBox(ListHint);
    ListHintSlot->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 4.0f));
    UScrollBox* ListScroll = WidgetTree->ConstructWidget<UScrollBox>(
        UScrollBox::StaticClass(), TEXT("RuntimeDockBusinessScroll"));
    ListScroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);
    UVerticalBoxSlot* ListScrollSlot = ListStack->AddChildToVerticalBox(ListScroll);
    ListScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    BusinessList = WidgetTree->ConstructWidget<UVerticalBox>(
        UVerticalBox::StaticClass(), TEXT("RuntimeDockBusinessRows"));
    BusinessList->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    ListScroll->AddChild(BusinessList);
}

void UOntoTwinRuntimeDockWidget::BuildRoamingPanel()
{
    UOverlay* Panel = WidgetTree->ConstructWidget<UOverlay>(
        UOverlay::StaticClass(), TEXT("RuntimeDockRoamingPanel"));
    Panel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    ContentSwitcher->AddChild(Panel);

    RoamingControls = WidgetTree->ConstructWidget<UHorizontalBox>(
        UHorizontalBox::StaticClass(), TEXT("RuntimeDockRoamingControlDeck"));
    RoamingControls->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UOverlaySlot* ControlsOverlaySlot = Panel->AddChildToOverlay(RoamingControls);
    ControlsOverlaySlot->SetHorizontalAlignment(HAlign_Fill);
    ControlsOverlaySlot->SetVerticalAlignment(VAlign_Center);

    const auto AddSeparator = [this]()
    {
        USizeBox* SeparatorBounds = WidgetTree->ConstructWidget<USizeBox>(
            USizeBox::StaticClass(), NAME_None);
        SeparatorBounds->SetWidthOverride(1.0f);
        SeparatorBounds->SetHeightOverride(78.0f);
        UBorder* Separator = WidgetTree->ConstructWidget<UBorder>(
            UBorder::StaticClass(), NAME_None);
        Separator->SetBrush(FSlateRoundedBoxBrush(
            FLinearColor(0.96f, 0.96f, 0.96f, 0.13f), 0.5f));
        Separator->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        SeparatorBounds->AddChild(Separator);
        UHorizontalBoxSlot* Slot = RoamingControls->AddChildToHorizontalBox(SeparatorBounds);
        Slot->SetVerticalAlignment(VAlign_Center);
        Slot->SetPadding(FMargin(SectionDividerMargin, 0.0f));
    };

    const auto MakeCommandButton = [this](
        const FName Name,
        EOntoTwinRuntimeDockIcon Icon,
        const FString& Label,
        EOntoTwinRuntimeDockAction Action,
        const FString& Tooltip)
    {
        UOntoTwinRuntimeDockButton* Button =
            WidgetTree->ConstructWidget<UOntoTwinRuntimeDockButton>(
                UOntoTwinRuntimeDockButton::StaticClass(), Name);
        Button->Configure(this, Action);
        Button->SetStyle(BuildIconButtonStyle(false));
        Button->SetToolTipText(FText::FromString(Tooltip));
        Button->SetAccessibleLabel(FText::FromString(Tooltip));
        UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(
            UVerticalBox::StaticClass(), NAME_None);
        Content->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        USizeBox* IconBounds = WidgetTree->ConstructWidget<USizeBox>(
            USizeBox::StaticClass(), NAME_None);
        IconBounds->SetWidthOverride(18.0f);
        IconBounds->SetHeightOverride(18.0f);
        UOntoTwinRuntimeDockIconWidget* IconWidget =
            WidgetTree->ConstructWidget<UOntoTwinRuntimeDockIconWidget>(
                UOntoTwinRuntimeDockIconWidget::StaticClass(), NAME_None);
        IconWidget->SetIcon(Icon);
        IconBounds->AddChild(IconWidget);
        UVerticalBoxSlot* IconSlot = Content->AddChildToVerticalBox(IconBounds);
        IconSlot->SetHorizontalAlignment(HAlign_Center);
        USizeBox* LabelBounds = WidgetTree->ConstructWidget<USizeBox>(
            USizeBox::StaticClass(), NAME_None);
        LabelBounds->SetHeightOverride(10.0f);
        if (!Label.IsEmpty())
        {
            UTextBlock* LabelText = MakeText(
                NAME_None, Label, 8.0f, false, FOntoTwinGlassTheme::SecondaryText());
            LabelText->SetJustification(ETextJustify::Center);
            LabelBounds->AddChild(LabelText);
        }
        UVerticalBoxSlot* LabelSlot = Content->AddChildToVerticalBox(LabelBounds);
        LabelSlot->SetHorizontalAlignment(HAlign_Fill);
        LabelSlot->SetPadding(FMargin(0.0f, 1.0f, 0.0f, 0.0f));
        Button->AddChild(Content);
        if (UButtonSlot* ContentSlot = Cast<UButtonSlot>(Content->Slot))
        {
            ContentSlot->SetPadding(FMargin(5.0f, 3.0f));
        }
        return Button;
    };

    USizeBox* SessionBounds = WidgetTree->ConstructWidget<USizeBox>(
        USizeBox::StaticClass(), TEXT("RuntimeDockSessionBounds"));
    SessionBounds->SetWidthOverride(350.0f);
    SessionBounds->SetHeightOverride(90.0f);
    UHorizontalBox* Session = WidgetTree->ConstructWidget<UHorizontalBox>(
        UHorizontalBox::StaticClass(), TEXT("RuntimeDockSession"));
    Session->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    SessionBounds->AddChild(Session);
    UHorizontalBoxSlot* SessionSlot = RoamingControls->AddChildToHorizontalBox(SessionBounds);
    SessionSlot->SetVerticalAlignment(VAlign_Center);

    USizeBox* AvatarBounds = WidgetTree->ConstructWidget<USizeBox>(
        USizeBox::StaticClass(), TEXT("RuntimeDockAvatarBounds"));
    AvatarBounds->SetWidthOverride(48.0f);
    AvatarBounds->SetHeightOverride(48.0f);
    UBorder* AvatarPlate = WidgetTree->ConstructWidget<UBorder>(
        UBorder::StaticClass(), TEXT("RuntimeDockAvatarPlate"));
    AvatarPlate->SetBrush(FSlateRoundedBoxBrush(
        FLinearColor(0.96f, 0.96f, 0.96f, 0.09f), 24.0f));
    AvatarPlate->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    AvatarBounds->AddChild(AvatarPlate);
    UCanvasPanel* AvatarIcon = WidgetTree->ConstructWidget<UCanvasPanel>(
        UCanvasPanel::StaticClass(), TEXT("RuntimeDockAvatarIcon"));
    AvatarIcon->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    AvatarPlate->SetContent(AvatarIcon);
    UBorder* AvatarHead = WidgetTree->ConstructWidget<UBorder>(
        UBorder::StaticClass(), TEXT("RuntimeDockAvatarHead"));
    AvatarHead->SetBrush(FSlateRoundedBoxBrush(
        FOntoTwinGlassTheme::PrimaryText(), 6.0f));
    AvatarHead->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UCanvasPanelSlot* AvatarHeadSlot = AvatarIcon->AddChildToCanvas(AvatarHead);
    AvatarHeadSlot->SetAnchors(FAnchors(0.5f, 0.5f));
    AvatarHeadSlot->SetAlignment(FVector2D(0.5f, 0.5f));
    AvatarHeadSlot->SetPosition(FVector2D(0.0f, -7.0f));
    AvatarHeadSlot->SetSize(FVector2D(12.0f, 12.0f));
    UBorder* AvatarBody = WidgetTree->ConstructWidget<UBorder>(
        UBorder::StaticClass(), TEXT("RuntimeDockAvatarBody"));
    AvatarBody->SetBrush(FSlateRoundedBoxBrush(
        FOntoTwinGlassTheme::PrimaryText(), 9.0f));
    AvatarBody->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UCanvasPanelSlot* AvatarBodySlot = AvatarIcon->AddChildToCanvas(AvatarBody);
    AvatarBodySlot->SetAnchors(FAnchors(0.5f, 0.5f));
    AvatarBodySlot->SetAlignment(FVector2D(0.5f, 0.5f));
    AvatarBodySlot->SetPosition(FVector2D(0.0f, 9.0f));
    AvatarBodySlot->SetSize(FVector2D(25.0f, 14.0f));
    UHorizontalBoxSlot* AvatarSlot = Session->AddChildToHorizontalBox(AvatarBounds);
    AvatarSlot->SetVerticalAlignment(VAlign_Center);
    AvatarSlot->SetPadding(FMargin(0.0f, 0.0f, 12.0f, 0.0f));

    UVerticalBox* Selectors = WidgetTree->ConstructWidget<UVerticalBox>(
        UVerticalBox::StaticClass(), TEXT("RuntimeDockSessionSelectors"));
    Selectors->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UHorizontalBoxSlot* SelectorsSlot = Session->AddChildToHorizontalBox(Selectors);
    SelectorsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    SelectorsSlot->SetVerticalAlignment(VAlign_Fill);

    UVerticalBox* CharacterStack = WidgetTree->ConstructWidget<UVerticalBox>(
        UVerticalBox::StaticClass(), TEXT("RuntimeDockCharacterStack"));
    CharacterStack->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    CharacterStack->AddChildToVerticalBox(MakeText(
        TEXT("RuntimeDockCharacterLabel"), TEXT("人物"), 9.0f, true,
        FOntoTwinGlassTheme::MutedText()));
    CharacterSelector = WidgetTree->ConstructWidget<UComboBoxString>(
        UComboBoxString::StaticClass(), TEXT("RuntimeDockCharacterSelector"));
PRAGMA_DISABLE_DEPRECATION_WARNINGS
    CharacterSelector->ForegroundColor =
        FSlateColor(FOntoTwinGlassTheme::PrimaryText());
PRAGMA_ENABLE_DEPRECATION_WARNINGS
    CharacterSelector->SetWidgetStyle(BuildComboStyle());
    CharacterSelector->SetItemStyle(BuildComboRowStyle());
    CharacterSelector->SetContentPadding(FMargin(8.0f, 2.0f));
    CharacterSelector->SetMaxListHeight(260.0f);
    CharacterSelector->OnGenerateWidgetEvent.BindDynamic(
        this, &UOntoTwinRuntimeDockWidget::GenerateSelectorItem);
    CharacterSelector->OnSelectionChanged.AddDynamic(
        this, &UOntoTwinRuntimeDockWidget::OnCharacterSelected);
    UVerticalBoxSlot* CharacterSelectorSlot =
        CharacterStack->AddChildToVerticalBox(CharacterSelector);
    CharacterSelectorSlot->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 0.0f));
    UVerticalBoxSlot* CharacterStackSlot =
        Selectors->AddChildToVerticalBox(CharacterStack);
    CharacterStackSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    CharacterStackSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 2.0f));

    UVerticalBox* RouteStack = WidgetTree->ConstructWidget<UVerticalBox>(
        UVerticalBox::StaticClass(), TEXT("RuntimeDockRouteStack"));
    RouteStack->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    RouteStack->AddChildToVerticalBox(MakeText(
        TEXT("RuntimeDockRouteLabel"), TEXT("路线"), 9.0f, true,
        FOntoTwinGlassTheme::MutedText()));
    RouteSelector = WidgetTree->ConstructWidget<UComboBoxString>(
        UComboBoxString::StaticClass(), TEXT("RuntimeDockRouteSelector"));
PRAGMA_DISABLE_DEPRECATION_WARNINGS
    RouteSelector->ForegroundColor =
        FSlateColor(FOntoTwinGlassTheme::PrimaryText());
PRAGMA_ENABLE_DEPRECATION_WARNINGS
    RouteSelector->SetWidgetStyle(BuildComboStyle());
    RouteSelector->SetItemStyle(BuildComboRowStyle());
    RouteSelector->SetContentPadding(FMargin(8.0f, 2.0f));
    RouteSelector->SetMaxListHeight(260.0f);
    RouteSelector->OnGenerateWidgetEvent.BindDynamic(
        this, &UOntoTwinRuntimeDockWidget::GenerateSelectorItem);
    RouteSelector->OnSelectionChanged.AddDynamic(
        this, &UOntoTwinRuntimeDockWidget::OnRouteSelected);
    UVerticalBoxSlot* RouteSelectorSlot = RouteStack->AddChildToVerticalBox(RouteSelector);
    RouteSelectorSlot->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 0.0f));
    UVerticalBoxSlot* RouteStackSlot = Selectors->AddChildToVerticalBox(RouteStack);
    RouteStackSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

    AddSeparator();

    USizeBox* ViewBounds = WidgetTree->ConstructWidget<USizeBox>(
        USizeBox::StaticClass(), TEXT("RuntimeDockViewBounds"));
    ViewBounds->SetWidthOverride(254.0f);
    ViewBounds->SetHeightOverride(90.0f);
    UVerticalBox* ViewStack = WidgetTree->ConstructWidget<UVerticalBox>(
        UVerticalBox::StaticClass(), TEXT("RuntimeDockViewStack"));
    ViewStack->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    ViewBounds->AddChild(ViewStack);
    ViewStack->AddChildToVerticalBox(MakeText(
        TEXT("RuntimeDockViewLabel"), TEXT("视角"), 9.0f, true,
        FOntoTwinGlassTheme::MutedText()));
    UHorizontalBox* ViewRow = WidgetTree->ConstructWidget<UHorizontalBox>(
        UHorizontalBox::StaticClass(), TEXT("RuntimeDockViewActions"));
    ViewRow->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UVerticalBoxSlot* ViewRowSlot = ViewStack->AddChildToVerticalBox(ViewRow);
    ViewRowSlot->SetPadding(FMargin(0.0f, 7.0f, 0.0f, 0.0f));
    CameraGlobalButton = MakeCommandButton(
        TEXT("RuntimeDockCameraGlobal"), EOntoTwinRuntimeDockIcon::ViewGlobal, TEXT("全局"),
        EOntoTwinRuntimeDockAction::CameraGlobal, TEXT("上帝视角"));
    CameraShoulderButton = MakeCommandButton(
        TEXT("RuntimeDockCameraShoulder"), EOntoTwinRuntimeDockIcon::ViewShoulder, TEXT("过肩"),
        EOntoTwinRuntimeDockAction::CameraShoulder, TEXT("过肩视角"));
    CameraFirstPersonButton = MakeCommandButton(
        TEXT("RuntimeDockCameraFirst"), EOntoTwinRuntimeDockIcon::ViewFirstPerson, TEXT("第一"),
        EOntoTwinRuntimeDockAction::CameraFirstPerson, TEXT("第一人称"));
    CrosshairButton = MakeCommandButton(
        TEXT("RuntimeDockCrosshair"), EOntoTwinRuntimeDockIcon::Crosshair, TEXT("准星"),
        EOntoTwinRuntimeDockAction::ToggleCrosshair,
        TEXT("第一人称准星（默认关闭）"));
    for (UOntoTwinRuntimeDockButton* Button :
        {CameraGlobalButton, CameraShoulderButton, CameraFirstPersonButton, CrosshairButton})
    {
        USizeBox* ButtonBounds = WidgetTree->ConstructWidget<USizeBox>(
            USizeBox::StaticClass(), NAME_None);
        ButtonBounds->SetWidthOverride(54.0f);
        ButtonBounds->SetHeightOverride(58.0f);
        ButtonBounds->AddChild(Button);
        UHorizontalBoxSlot* ButtonSlot = ViewRow->AddChildToHorizontalBox(ButtonBounds);
        ButtonSlot->SetPadding(FMargin(0.0f, 0.0f, 6.0f, 0.0f));
    }
    UHorizontalBoxSlot* ViewBoundsSlot = RoamingControls->AddChildToHorizontalBox(ViewBounds);
    ViewBoundsSlot->SetVerticalAlignment(VAlign_Center);

    AddSeparator();

    QuickActionBounds = WidgetTree->ConstructWidget<USizeBox>(
        USizeBox::StaticClass(), TEXT("RuntimeDockActionBounds"));
    QuickActionBounds->SetWidthOverride(CompactActionWidth);
    QuickActionBounds->SetHeightOverride(90.0f);
    UVerticalBox* ActionStack = WidgetTree->ConstructWidget<UVerticalBox>(
        UVerticalBox::StaticClass(), TEXT("RuntimeDockActionStack"));
    ActionStack->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    QuickActionBounds->AddChild(ActionStack);
    ActionStack->AddChildToVerticalBox(MakeText(
        TEXT("RuntimeDockActionLabel"), TEXT("快捷操作"), 9.0f, true,
        FOntoTwinGlassTheme::MutedText()));
    UHorizontalBox* ActionRow = WidgetTree->ConstructWidget<UHorizontalBox>(
        UHorizontalBox::StaticClass(), TEXT("RuntimeDockActionRow"));
    ActionRow->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UVerticalBoxSlot* ActionRowSlot = ActionStack->AddChildToVerticalBox(ActionRow);
    ActionRowSlot->SetPadding(FMargin(0.0f, 7.0f, 0.0f, 0.0f));
    struct FActionDef
    {
        EOntoTwinRuntimeDockIcon Icon;
        FString Label;
        FString Tooltip;
        EOntoTwinRuntimeDockAction Action;
    };
    const TArray<FActionDef> ActionDefs = {
        {EOntoTwinRuntimeDockIcon::Skin, TEXT("换肤"), TEXT("切换皮肤"), EOntoTwinRuntimeDockAction::CycleSkin},
        {EOntoTwinRuntimeDockIcon::ReturnRoute, TEXT("返回"), TEXT("返回线路"), EOntoTwinRuntimeDockAction::ResumeRoute},
        {EOntoTwinRuntimeDockIcon::RestartRoute, TEXT("重播"), TEXT("从头开始"), EOntoTwinRuntimeDockAction::RestartRoute},
        {EOntoTwinRuntimeDockIcon::ReloadCharacter, TEXT("重载"), TEXT("重载人物"), EOntoTwinRuntimeDockAction::ReloadCharacter}
    };
    for (int32 Index = 0; Index < ActionDefs.Num(); ++Index)
    {
        const FActionDef& Def = ActionDefs[Index];
        UOntoTwinRuntimeDockButton* Button = MakeCommandButton(
            NAME_None, Def.Icon, Def.Label, Def.Action, Def.Tooltip);
        USizeBox* ButtonBounds = WidgetTree->ConstructWidget<USizeBox>(
            USizeBox::StaticClass(), NAME_None);
        ButtonBounds->SetWidthOverride(54.0f);
        ButtonBounds->SetHeightOverride(58.0f);
        ButtonBounds->AddChild(Button);
        if (Def.Action == EOntoTwinRuntimeDockAction::ReloadCharacter)
        {
            ReloadCharacterBounds = ButtonBounds;
            ReloadCharacterBounds->SetVisibility(ESlateVisibility::Collapsed);
        }
        UHorizontalBoxSlot* ButtonSlot = ActionRow->AddChildToHorizontalBox(ButtonBounds);
        ButtonSlot->SetPadding(FMargin(0.0f, 0.0f, 6.0f, 0.0f));
    }
    UHorizontalBoxSlot* ActionBoundsSlot =
        RoamingControls->AddChildToHorizontalBox(QuickActionBounds);
    ActionBoundsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
    ActionBoundsSlot->SetVerticalAlignment(VAlign_Center);

    RoamingUnavailable = WidgetTree->ConstructWidget<UVerticalBox>(
        UVerticalBox::StaticClass(), TEXT("RuntimeDockRoamingUnavailable"));
    RoamingUnavailable->SetVisibility(ESlateVisibility::Collapsed);
    RoamingUnavailable->AddChildToVerticalBox(MakeText(
        TEXT("RuntimeDockRoamingUnavailableTitle"), TEXT("尚未进入漫游模式"),
        12.0f, true, FOntoTwinGlassTheme::PrimaryText()));
    EnterRoamingButton = MakeButton(
        TEXT("RuntimeDockEnterRoaming"), TEXT("F7 进入漫游"),
        EOntoTwinRuntimeDockAction::EnterRoaming, FString(), INDEX_NONE, false);
    EnterRoamingButton->SetToolTipText(FText::FromString(TEXT("进入漫游模式（快捷键 F7）")));
    UVerticalBoxSlot* EnterRoamingSlot = RoamingUnavailable->AddChildToVerticalBox(EnterRoamingButton);
    EnterRoamingSlot->SetHorizontalAlignment(HAlign_Center);
    EnterRoamingSlot->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 0.0f));
    UOverlaySlot* UnavailableSlot = Panel->AddChildToOverlay(RoamingUnavailable);
    UnavailableSlot->SetHorizontalAlignment(HAlign_Center);
    UnavailableSlot->SetVerticalAlignment(VAlign_Center);
}

void UOntoTwinRuntimeDockWidget::RefreshFromManager()
{
    if (!Manager) return;
    RefreshSpaceCatalog();
    RefreshBusinessCatalog();
    RefreshCharacterSelector();
    RefreshRouteSelector();
    UpdateBusinessScope();
    UpdateRoamingState();
}



void UOntoTwinRuntimeDockWidget::NativeTick(
    const FGeometry& MyGeometry,
    const float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    SpaceRefreshElapsed += FMath::Max(0.0f, InDeltaTime);
    if (bDockOpen && SpaceRefreshElapsed >= 0.5f)
    {
        SpaceRefreshElapsed = 0.0f;
        RefreshSpaceCatalog();
    }
    if (bSpaceSelectorsDirty)
    {
        bSpaceSelectorsDirty = false;
        RefreshSpaceSelector();
    }

    if (bDockAnimationActive)
    {
        const float DeltaTime = FMath::Max(0.0f, InDeltaTime);
        if (FOntoTwinGlassRenderer::ShouldReduceMotion())
        {
            DockAnimationDuration = FMath::Min(
                DockAnimationDuration,
                DrawerReduceMotionDuration);
        }
        DockAnimationElapsed += DeltaTime;
        const float Alpha = DockAnimationDuration > KINDA_SMALL_NUMBER
            ? FMath::Clamp(
                DockAnimationElapsed / DockAnimationDuration,
                0.0f,
                1.0f)
            : 1.0f;
        const float EasedAlpha = FMath::InterpEaseOut(
            0.0f,
            1.0f,
            Alpha,
            2.0f);
        DockAnimationProgress = FMath::Lerp(
            DockAnimationStartProgress,
            DockAnimationTargetProgress,
            EasedAlpha);
        ApplyDockVisualState(DockAnimationProgress);

        if (Alpha >= 1.0f)
        {
            bDockAnimationActive = false;
            DockAnimationProgress = DockAnimationTargetProgress;
            ApplyDockVisualState(DockAnimationProgress);
            if (!bDockOpen && DockShell)
            {
                DockShell->SetVisibility(ESlateVisibility::Collapsed);
            }
        }
    }

    UpdateDockTriggerFocusVisual();
}

void UOntoTwinRuntimeDockWidget::ApplyDockVisualState(const float OpenProgress)
{
    const float Progress = FMath::Clamp(OpenProgress, 0.0f, 1.0f);
    const float HitVerticalPadding =
        (DrawerHandleHitHeight - DrawerHandleVisualHeight) * 0.5f;
    // Keep the complete 48px hit target inside the bottom safe area. The
    // visual capsule therefore sits 9px above that minimum inset.
    const float CollapsedPositionY = -DrawerHandleBottomSafeInset;
    const float ExpandedPositionY =
        -DockHeight + DrawerHandleTopOverlap + HitVerticalPadding;

    if (DockShellSlot)
    {
        DockShellSlot->SetPosition(FVector2D(
            0.0f,
            DockHeight * (1.0f - Progress)));
    }
    if (DockTriggerSlot)
    {
        DockTriggerSlot->SetPosition(FVector2D(
            0.0f,
            FMath::Lerp(CollapsedPositionY, ExpandedPositionY, Progress)));
    }
    if (DockShell && Progress > KINDA_SMALL_NUMBER
        && DockShell->GetVisibility() == ESlateVisibility::Collapsed)
    {
        DockShell->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    }
}

void UOntoTwinRuntimeDockWidget::UpdateDockTriggerAccessibility()
{
    if (!DockTriggerButton)
    {
        return;
    }

    const FText AccessibleLabel = FText::FromString(
        bDockOpen ? TEXT("收起控制面板") : TEXT("展开控制面板"));
    DockTriggerButton->SetAccessibleLabel(AccessibleLabel);
}

void UOntoTwinRuntimeDockWidget::UpdateDockTriggerFocusVisual()
{
    if (!DockTriggerFocusRing || !DockTriggerButton)
    {
        return;
    }

    const bool bFocused = DockTriggerButton->HasKeyboardFocus();
    if (bFocused == bDrawerFocusVisible)
    {
        return;
    }

    bDrawerFocusVisible = bFocused;
    DockTriggerFocusRing->SetVisibility(
        bFocused
            ? ESlateVisibility::SelfHitTestInvisible
            : ESlateVisibility::Collapsed);
}

void UOntoTwinRuntimeDockWidget::SetDockOpen(bool bOpen)
{
    const float TargetProgress = bOpen ? 1.0f : 0.0f;
    const bool bSameTarget = FMath::IsNearlyEqual(
        DockAnimationTargetProgress,
        TargetProgress);
    bDockOpen = bOpen;
    if (!bOpen)
    {
        for (UOntoTwinSpaceComboBox* Selector : SpaceSelectors) Selector->CloseMenu();
    }
    if (bOpen)
    {
        RefreshFromManager();
    }
    UpdateDockTriggerAccessibility();

    if (bSameTarget && bDockAnimationActive)
    {
        return;
    }

    if (bSameTarget && !bDockAnimationActive)
    {
        DockAnimationProgress = TargetProgress;
        ApplyDockVisualState(DockAnimationProgress);
        if (!bOpen && DockShell)
        {
            DockShell->SetVisibility(ESlateVisibility::Collapsed);
        }
        return;
    }

    DockAnimationStartProgress = DockAnimationProgress;
    DockAnimationTargetProgress = TargetProgress;
    DockAnimationElapsed = 0.0f;
    DockAnimationDuration = FOntoTwinGlassRenderer::ShouldReduceMotion()
        ? DrawerReduceMotionDuration
        : (bOpen ? DrawerOpenDuration : DrawerCloseDuration);
    bDockAnimationActive = !FMath::IsNearlyEqual(
        DockAnimationStartProgress,
        DockAnimationTargetProgress);

    if (bOpen && DockShell)
    {
        DockShell->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    }
    ApplyDockVisualState(DockAnimationProgress);

    if (!bDockAnimationActive)
    {
        if (!bOpen && DockShell)
        {
            DockShell->SetVisibility(ESlateVisibility::Collapsed);
        }
    }
    if (DockTriggerIcon)
    {
        DockTriggerIcon->SetIcon(
            bOpen
                ? EOntoTwinRuntimeDockIcon::DrawerDown
                : EOntoTwinRuntimeDockIcon::DrawerUp);
    }
    if (DockTrigger)
    {
        DockTrigger->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    }
    SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

void UOntoTwinRuntimeDockWidget::SetActiveTab(int32 TabIndex)
{
    for (UOntoTwinSpaceComboBox* Selector : SpaceSelectors) Selector->CloseMenu();
    ActiveTabIndex = FMath::Clamp(TabIndex, 0, 2);
    if (ContentSwitcher)
    {
        ContentSwitcher->SetActiveWidgetIndex(ActiveTabIndex);
    }
    UpdateTabStyles();
    UpdateRoamingState();
}

void UOntoTwinRuntimeDockWidget::UpdateTabStyles()
{
    if (SpaceTabButton)
    {
        SpaceTabButton->SetStyle(BuildTabButtonStyle(ActiveTabIndex == 1));
    }
    if (BusinessTabButton)
    {
        BusinessTabButton->SetStyle(BuildTabButtonStyle(ActiveTabIndex == 2));
    }
    if (RoamingTabButton)
    {
        RoamingTabButton->SetStyle(BuildTabButtonStyle(ActiveTabIndex == 0));
    }
}

void UOntoTwinRuntimeDockWidget::RefreshSpaceCatalog()
{
    if (!Manager || !SpaceSelectorsHost) return;
    TArray<FString> NextIds;
    TArray<FString> NextNames;
    TArray<FString> NextParents;
    Manager->GetAvailableWebZoneTree(NextIds, NextNames, NextParents);
    FString Signature;
    for (int32 Index = 0; Index < NextIds.Num(); ++Index)
    {
        Signature += NextIds[Index] + TEXT("|");
        Signature += NextNames.IsValidIndex(Index) ? NextNames[Index] : FString();
        Signature += TEXT("|");
        Signature += NextParents.IsValidIndex(Index) ? NextParents[Index] : FString();
        Signature += TEXT("\x1e");
    }
    if (Signature == ZoneCatalogSignature) return;
    ZoneCatalogSignature = Signature;
    ZoneIds = MoveTemp(NextIds);
    ZoneNames = MoveTemp(NextNames);
    ZoneParentIds = MoveTemp(NextParents);

    FString ExpectedParent;
    for (int32 Depth = 0; Depth < SelectedZonePath.Num(); ++Depth)
    {
        const int32 Index = ZoneIds.IndexOfByKey(SelectedZonePath[Depth]);
        const FString ActualParent = ZoneParentIds.IsValidIndex(Index)
            ? ZoneParentIds[Index] : FString();
        if (Index == INDEX_NONE || (Depth > 0 && ActualParent != ExpectedParent))
        {
            SelectedZonePath.SetNum(Depth);
            break;
        }
        ExpectedParent = SelectedZonePath[Depth];
    }
    bSpaceSelectorsDirty = true;
}

void UOntoTwinRuntimeDockWidget::RefreshSpaceSelector()
{
    if (!SpaceSelectorsHost) return;
    // Rebuild after the selecting Slate callback has returned.
    for (UOntoTwinSpaceComboBox* Selector : SpaceSelectors) Selector->CloseMenu();
    SpaceSelectorsHost->ClearChildren();
    SpaceSelectors.Reset();
    const int32 Count = FMath::Max(3, SelectedZonePath.Num() + 1);
    FString Parent;
    for (int32 Depth = 0; Depth < Count; ++Depth)
    {
        TArray<int32> Children;
        for (int32 Index = 0; Index < ZoneIds.Num(); ++Index)
        {
            const FString CandidateParent = ZoneParentIds.IsValidIndex(Index) ? ZoneParentIds[Index] : FString();
            const bool bRoot = CandidateParent.IsEmpty() || !ZoneIds.Contains(CandidateParent);
            if ((Depth == 0 && bRoot) || (Depth > 0 && SelectedZonePath.IsValidIndex(Depth - 1)
                && CandidateParent == Parent))
                Children.Add(Index);
        }
        if (Depth >= 3 && Children.IsEmpty()) break;
        auto* Selector = WidgetTree->ConstructWidget<UOntoTwinSpaceComboBox>(
            UOntoTwinSpaceComboBox::StaticClass(), NAME_None);
        Selector->Configure(this, Depth);
        Selector->SetWidgetStyle(BuildComboStyle());
        Selector->SetItemStyle(BuildComboRowStyle());
        Selector->SetMaxListHeight(264.0f);
        const FString Prompt = Depth == 0 ? (ZoneIds.IsEmpty() ? TEXT("暂无可用空间") : TEXT("选择公司"))
            : Depth == 1 ? TEXT("选择楼层") : TEXT("选择区域");
        Selector->AddOption(Prompt);
        Selector->ZoneOptionIds.Add(FString());
        int32 SelectedIndex = 0;
        for (const int32 Index : Children)
        {
            FString Name = ZoneNames.IsValidIndex(Index) && !ZoneNames[Index].IsEmpty()
                ? ZoneNames[Index] : TEXT("未命名空间");
            const FString BaseName = Name;
            int32 Suffix = 2;
            while (Selector->FindOptionIndex(Name) != INDEX_NONE)
                Name = FString::Printf(TEXT("%s (%d)"), *BaseName, Suffix++);
            Selector->AddOption(Name);
            Selector->ZoneOptionIds.Add(ZoneIds[Index]);
            if (SelectedZonePath.IsValidIndex(Depth) && SelectedZonePath[Depth] == ZoneIds[Index])
                SelectedIndex = Selector->ZoneOptionIds.Num() - 1;
        }
        Selector->SetSelectedIndex(SelectedIndex);
        Selector->SetIsEnabled(!Children.IsEmpty());
        UHorizontalBoxSlot* SelectorSlot = SpaceSelectorsHost->AddChildToHorizontalBox(WrapSpaceControl(Selector));
        SelectorSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        SelectorSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
        SpaceSelectors.Add(Selector);
        Parent = SelectedZonePath.IsValidIndex(Depth) ? SelectedZonePath[Depth] : FString();
    }
    UpdateSpaceSummary();
}

void UOntoTwinRuntimeDockWidget::UpdateSpaceSummary()
{
    // Browsing a target never changes the committed current location.
    const FString Current = TEXT("当前位置：") + JoinPathNames(CurrentZonePath, ZoneIds, ZoneNames);
    if (SpaceBreadcrumb) SpaceBreadcrumb->SetText(FText::FromString(Current));
    if (EnterSpaceButton)
    {
        EnterSpaceButton->SetIsEnabled(!SelectedZonePath.IsEmpty());
        EnterSpaceButton->SetAccessibleLabel(FText::FromString(
            SelectedZonePath.IsEmpty() ? TEXT("请先选择目标位置")
            : TEXT("前往位置：") + JoinPathNames(SelectedZonePath, ZoneIds, ZoneNames)));
    }
    UpdateBusinessScope();
}

void UOntoTwinRuntimeDockWidget::RefreshBusinessCatalog()
{
    if (!Manager || !BusinessList) return;
    TArray<FString> NextIds;
    TArray<FString> NextNames;
    TArray<int32> NextCounts;
    Manager->GetAvailableWebBusinessViewSummaries(NextIds, NextNames, NextCounts);
    FString Signature;
    for (int32 Index = 0; Index < NextIds.Num(); ++Index)
    {
        Signature += NextIds[Index] + TEXT("|");
        Signature += NextNames.IsValidIndex(Index) ? NextNames[Index] : FString();
        Signature += FString::Printf(TEXT("|%d\x1e"),
            NextCounts.IsValidIndex(Index) ? NextCounts[Index] : 0);
    }
    if (Signature == BusinessCatalogSignature) return;
    BusinessCatalogSignature = Signature;
    BusinessIds = MoveTemp(NextIds);
    BusinessNames = MoveTemp(NextNames);
    BusinessMemberCounts = MoveTemp(NextCounts);
    BuildBusinessRows();
}

void UOntoTwinRuntimeDockWidget::BuildBusinessRows()
{
    if (!BusinessList) return;
    BusinessList->ClearChildren();
    if (BusinessIds.Num() == 0)
    {
        UTextBlock* Empty = MakeText(
            NAME_None, TEXT("暂无可用业务视图"), 10.0f, false,
            FOntoTwinGlassTheme::MutedText());
        BusinessList->AddChildToVerticalBox(Empty);
        return;
    }
    for (int32 Index = 0; Index < BusinessIds.Num(); ++Index)
    {
        UOntoTwinRuntimeDockButton* Button =
            WidgetTree->ConstructWidget<UOntoTwinRuntimeDockButton>(
                UOntoTwinRuntimeDockButton::StaticClass(), NAME_None);
        Button->Configure(
            this, EOntoTwinRuntimeDockAction::OpenBusiness, BusinessIds[Index]);
        Button->SetStyle(BuildButtonStyle(9.0f));

        UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(
            UHorizontalBox::StaticClass(), NAME_None);
        Row->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        const FString Name = BusinessNames.IsValidIndex(Index)
            ? BusinessNames[Index] : BusinessIds[Index];
        UTextBlock* NameText = MakeText(
            NAME_None, Name, 10.0f, false, FOntoTwinGlassTheme::PrimaryText());
        UHorizontalBoxSlot* NameSlot = Row->AddChildToHorizontalBox(NameText);
        NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        NameSlot->SetVerticalAlignment(VAlign_Center);
        const int32 Count = BusinessMemberCounts.IsValidIndex(Index)
            ? BusinessMemberCounts[Index] : 0;
        UTextBlock* CountText = MakeText(
            NAME_None,
            FString::Printf(TEXT("%d 个实例   ›"), Count),
            9.0f,
            false,
            FOntoTwinGlassTheme::MutedText());
        UHorizontalBoxSlot* CountSlot = Row->AddChildToHorizontalBox(CountText);
        CountSlot->SetVerticalAlignment(VAlign_Center);
        Button->AddChild(Row);
        if (UButtonSlot* ContentSlot = Cast<UButtonSlot>(Row->Slot))
        {
            ContentSlot->SetPadding(FMargin(9.0f, 4.0f));
        }
        UVerticalBoxSlot* ButtonSlot = BusinessList->AddChildToVerticalBox(Button);
        ButtonSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
    }
}

void UOntoTwinRuntimeDockWidget::UpdateBusinessScope()
{
    if (bBusinessScopeUsesCurrent && CurrentZonePath.Num() > 0)
    {
        SelectedBusinessZoneId = CurrentZonePath.Last();
    }
    else
    {
        SelectedBusinessZoneId.Reset();
    }
    if (ScopeCurrentButton)
    {
        ScopeCurrentButton->SetIsEnabled(CurrentZonePath.Num() > 0);
        ScopeCurrentButton->SetBackgroundColor(
            bBusinessScopeUsesCurrent ? ActiveFill() : InactiveFill());
    }
    if (ScopeAllButton)
    {
        ScopeAllButton->SetBackgroundColor(
            bBusinessScopeUsesCurrent ? InactiveFill() : ActiveFill());
    }
    if (BusinessScopeText)
    {
        const FString Scope = bBusinessScopeUsesCurrent
            ? JoinPathNames(CurrentZonePath, ZoneIds, ZoneNames)
            : TEXT("全部空间");
        BusinessScopeText->SetText(FText::FromString(TEXT("当前：") + Scope));
    }
}

void UOntoTwinRuntimeDockWidget::RefreshCharacterSelector()
{
    if (!Manager || !CharacterSelector) return;
    TArray<FString> NextIds;
    TArray<FString> NextLabels;
    Manager->GetAvailableRuntimeCharacters(NextIds, NextLabels);
    FString Signature = Manager->GetActiveRuntimeCharacterId();
    Signature += Manager->IsCharacterSwitching() ? TEXT("|switching") : TEXT("|ready");
    for (int32 Index = 0; Index < NextIds.Num(); ++Index)
    {
        Signature += TEXT("|") + NextIds[Index] + TEXT(":");
        Signature += NextLabels.IsValidIndex(Index) ? NextLabels[Index] : FString();
    }
    if (Signature == CharacterSignature) return;
    CharacterSignature = Signature;
    CharacterIds = MoveTemp(NextIds);
    CharacterLabels = MoveTemp(NextLabels);
    bRefreshingSelectors = true;
    CharacterSelector->ClearOptions();
    for (const FString& Label : CharacterLabels) CharacterSelector->AddOption(Label);
    const int32 ActiveIndex = CharacterIds.IndexOfByKey(
        Manager->GetActiveRuntimeCharacterId());
    if (CharacterLabels.IsValidIndex(ActiveIndex))
    {
        CharacterSelector->SetSelectedOption(CharacterLabels[ActiveIndex]);
    }
    else if (CharacterLabels.Num() > 0)
    {
        CharacterSelector->SetSelectedIndex(0);
    }
    bRefreshingSelectors = false;
}

void UOntoTwinRuntimeDockWidget::RefreshRouteSelector()
{
    if (!Manager || !RouteSelector) return;
    TArray<FString> NextIds;
    TArray<FString> NextLabels;
    TArray<bool> DefaultFlags;
    Manager->GetAvailableRuntimeRoutes(NextIds, NextLabels, DefaultFlags);
    FString Signature = Manager->GetActiveRuntimeRouteId();
    Signature += Manager->IsRouteSwitching() ? TEXT("|switching") : TEXT("|ready");
    for (int32 Index = 0; Index < NextIds.Num(); ++Index)
    {
        Signature += TEXT("|") + NextIds[Index] + TEXT(":");
        Signature += NextLabels.IsValidIndex(Index) ? NextLabels[Index] : FString();
        if (DefaultFlags.IsValidIndex(Index) && DefaultFlags[Index]) Signature += TEXT(":default");
    }
    if (Signature == RouteSignature) return;
    RouteSignature = Signature;
    RouteIds = MoveTemp(NextIds);
    RouteLabels = MoveTemp(NextLabels);
    bRefreshingSelectors = true;
    RouteSelector->ClearOptions();
    for (const FString& Label : RouteLabels) RouteSelector->AddOption(Label);
    const int32 ActiveIndex = RouteIds.IndexOfByKey(Manager->GetActiveRuntimeRouteId());
    if (RouteLabels.IsValidIndex(ActiveIndex))
    {
        RouteSelector->SetSelectedOption(RouteLabels[ActiveIndex]);
    }
    else if (RouteLabels.Num() > 0)
    {
        RouteSelector->SetSelectedIndex(0);
    }
    bRefreshingSelectors = false;
}

void UOntoTwinRuntimeDockWidget::UpdateRoamingState()
{
    if (!Manager) return;
    const bool bActive = Manager->IsRoamingActive();
    if (SceneEditButton)
    {
        SceneEditButton->SetIsEnabled(Manager->CanToggleRuntimeEditor());
        SceneEditButton->SetStyle(BuildTabButtonStyle(false));
    }
    const bool bShowReload = bActive && Manager->HasPendingReload();
    if (ReloadCharacterBounds)
    {
        ReloadCharacterBounds->SetVisibility(
            bShowReload ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    }
    if (DockShell)
    {
        DockShell->SetWidthOverride(
            bShowReload && ActiveTabIndex == 0
                ? ReloadDockWidth
                : CompactDockWidth);
    }
    if (QuickActionBounds)
    {
        QuickActionBounds->SetWidthOverride(
            bShowReload ? ReloadActionWidth : CompactActionWidth);
    }
    if (RoamingControls)
    {
        RoamingControls->SetVisibility(
            bActive ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    }
    if (RoamingUnavailable)
    {
        RoamingUnavailable->SetVisibility(
            bActive ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
    }
    if (EnterRoamingButton)
    {
        EnterRoamingButton->SetIsEnabled(!bActive);
    }
    if (CharacterSelector)
    {
        CharacterSelector->SetIsEnabled(bActive && !Manager->IsCharacterSwitching());
    }
    if (RouteSelector)
    {
        RouteSelector->SetIsEnabled(bActive && !Manager->IsRouteSwitching());
    }
    const ETwinRoamingCameraMode Mode = Manager->GetCameraMode();
    if (CameraGlobalButton)
    {
        CameraGlobalButton->SetIsEnabled(bActive);
        CameraGlobalButton->SetStyle(BuildIconButtonStyle(
            Mode == ETwinRoamingCameraMode::God));
    }
    if (CameraShoulderButton)
    {
        CameraShoulderButton->SetIsEnabled(bActive);
        CameraShoulderButton->SetStyle(BuildIconButtonStyle(
            Mode == ETwinRoamingCameraMode::NearFollow));
    }
    if (CameraFirstPersonButton)
    {
        CameraFirstPersonButton->SetIsEnabled(bActive);
        CameraFirstPersonButton->SetStyle(BuildIconButtonStyle(
            Mode == ETwinRoamingCameraMode::FirstPerson));
    }
    if (CrosshairButton)
    {
        CrosshairButton->SetIsEnabled(Manager->CanToggleFirstPersonCrosshair());
        CrosshairButton->SetStyle(BuildIconButtonStyle(
            Manager->IsFirstPersonCrosshairEnabled()));
    }
}

UWidget* UOntoTwinRuntimeDockWidget::GenerateSelectorItem(FString Item)
{
    UTextBlock* Text = MakeText(
        NAME_None, Item, 10.0f, false, FOntoTwinGlassTheme::PrimaryText());
    RetainedComboTextWidgets.Add(Text);
    return Text;
}

void UOntoTwinRuntimeDockWidget::OnCharacterSelected(
    FString SelectedItem,
    ESelectInfo::Type SelectionType)
{
    (void)SelectionType;
    if (bRefreshingSelectors || !Manager) return;
    const int32 Index = CharacterLabels.IndexOfByKey(SelectedItem);
    if (CharacterIds.IsValidIndex(Index)
        && !Manager->SelectRuntimeCharacter(CharacterIds[Index]))
    {
        CharacterSignature.Reset();
        RefreshCharacterSelector();
    }
}

void UOntoTwinRuntimeDockWidget::OnRouteSelected(
    FString SelectedItem,
    ESelectInfo::Type SelectionType)
{
    (void)SelectionType;
    if (bRefreshingSelectors || !Manager) return;
    const int32 Index = RouteLabels.IndexOfByKey(SelectedItem);
    if (RouteIds.IsValidIndex(Index)
        && !Manager->SelectRuntimeRoute(RouteIds[Index]))
    {
        RouteSignature.Reset();
        RefreshRouteSelector();
    }
}

void UOntoTwinRuntimeDockWidget::HandleDockAction(
    EOntoTwinRuntimeDockAction Action,
    const FString& Payload,
    int32 Depth)
{
    if (!Manager) return;
    switch (Action)
    {
    case EOntoTwinRuntimeDockAction::ToggleDock:
        Manager->ToggleHudInteraction();
        return;
    case EOntoTwinRuntimeDockAction::Home:
        CurrentZonePath.Reset();
        UpdateSpaceSummary();
        Manager->ActivateRuntimeHome();
        return;
    case EOntoTwinRuntimeDockAction::ToggleRuntimeEditor:
        Manager->ToggleRuntimeEditor();
        return;
    case EOntoTwinRuntimeDockAction::EnterRoaming:
    {
        if (!Manager->IsRoamingActive()) Manager->ToggleRoaming();
        RefreshFromManager();
        return;
    }
    case EOntoTwinRuntimeDockAction::TabSpace:
        SetActiveTab(1);
        return;
    case EOntoTwinRuntimeDockAction::TabBusiness:
        SetActiveTab(2);
        return;
    case EOntoTwinRuntimeDockAction::TabRoaming:
        SetActiveTab(0);
        return;
    case EOntoTwinRuntimeDockAction::SelectZone:
        if (Depth >= 0 && Depth <= SelectedZonePath.Num())
        {
            SelectedZonePath.SetNum(Depth);
            if (!Payload.IsEmpty() && ZoneIds.Contains(Payload)) SelectedZonePath.Add(Payload);
            bSpaceSelectorsDirty = true;
            UpdateSpaceSummary();
        }
        return;
    case EOntoTwinRuntimeDockAction::EnterZone:
        if (!SelectedZonePath.IsEmpty())
        {
            const TArray<FString> TargetPath = SelectedZonePath;
            if (Manager->OpenWebZone(TargetPath.Last()))
            {
                CurrentZonePath = TargetPath;
                UpdateSpaceSummary();
            }
            else if (EnterSpaceButton)
            {
                EnterSpaceButton->SetToolTipText(FText::FromString(
                    TEXT("前往失败：该空间没有可用页面绑定或配置已失效")));
            }
        }
        return;
    case EOntoTwinRuntimeDockAction::OpenBusiness:
        Manager->OpenWebBusinessView(Payload, SelectedBusinessZoneId);
        return;
    case EOntoTwinRuntimeDockAction::ScopeAll:
        bBusinessScopeUsesCurrent = false;
        UpdateBusinessScope();
        return;
    case EOntoTwinRuntimeDockAction::ScopeCurrent:
        if (CurrentZonePath.Num() > 0)
        {
            bBusinessScopeUsesCurrent = true;
            UpdateBusinessScope();
        }
        return;
    case EOntoTwinRuntimeDockAction::CameraGlobal:
        Manager->SetCameraMode(ETwinRoamingCameraMode::God);
        break;
    case EOntoTwinRuntimeDockAction::CameraShoulder:
        Manager->SetCameraMode(ETwinRoamingCameraMode::NearFollow);
        break;
    case EOntoTwinRuntimeDockAction::CameraFirstPerson:
        Manager->SetCameraMode(ETwinRoamingCameraMode::FirstPerson);
        break;
    case EOntoTwinRuntimeDockAction::ToggleCrosshair:
        Manager->ToggleFirstPersonCrosshair();
        break;
    case EOntoTwinRuntimeDockAction::CycleSkin:
        Manager->CycleSkin();
        break;
    case EOntoTwinRuntimeDockAction::ResumeRoute:
        Manager->ResumeRoute();
        break;
    case EOntoTwinRuntimeDockAction::RestartRoute:
        Manager->RestartRoute();
        break;
    case EOntoTwinRuntimeDockAction::ReloadCharacter:
        Manager->ApplyPendingReload();
        break;
    default:
        return;
    }
    RefreshFromManager();
}
