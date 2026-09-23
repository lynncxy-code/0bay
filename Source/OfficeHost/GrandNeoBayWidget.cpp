#include "GrandNeoBayWidget.h"
#include "GrandNeoBayExperience.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Border.h"
#include "Components/BackgroundBlur.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "Input/Reply.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"

namespace
{
// Balanced Screen glass: a conservative neutral scrim over real UBackgroundBlur.
const FLinearColor GlassInk(.025f, .055f, .078f, .50f);
const FLinearColor Cyan(.12f, .85f, .91f, 1);
const FLinearColor Muted(.72f, .82f, .86f, .96f);
UTextBlock* Label(UWidgetTree* Tree, const FText& Text, int32 Size, FLinearColor Color, bool bBold = false)
{
    auto* L = Tree->ConstructWidget<UTextBlock>();
    L->SetText(Text);
    L->SetColorAndOpacity(Color);
    L->SetFont(FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size));
    L->SetAutoWrapText(true);
    L->SetVisibility(ESlateVisibility::HitTestInvisible);
    return L;
}
UButton* Button(UWidgetTree* Tree, const FText& Text, bool bPrimary = false)
{
    auto* B = Tree->ConstructWidget<UButton>();
    FButtonStyle Style;
    Style.SetNormal(FSlateRoundedBoxBrush(bPrimary ? FLinearColor(.07f, .70f, .77f, .76f) : FLinearColor(.18f, .29f, .36f, .32f), 10.f, FLinearColor(1, 1, 1, .20f), 1.f));
    Style.SetHovered(FSlateRoundedBoxBrush(bPrimary ? FLinearColor(.16f, .86f, .90f, .88f) : FLinearColor(.27f, .44f, .52f, .44f), 10.f, FLinearColor(1, 1, 1, .34f), 1.f));
    Style.SetPressed(FSlateRoundedBoxBrush(FLinearColor(.04f, .48f, .55f, .84f), 10.f));
    Style.SetNormalPadding(FMargin(16, 11));
    Style.SetPressedPadding(FMargin(16, 11));
    B->SetStyle(Style);
    auto* TextLabel = Label(Tree, Text, 14, FLinearColor::White, true);
    TextLabel->SetAutoWrapText(false);
    B->AddChild(TextLabel);
    return B;
}
UBorder* GlassPanel(UWidgetTree* Tree)
{
    auto* P = Tree->ConstructWidget<UBorder>();
    P->SetBrush(FSlateRoundedBoxBrush(GlassInk, 22.f, FLinearColor(1.f, 1.f, 1.f, .28f), 1.f));
    P->SetPadding(FMargin(24, 22));
    P->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    return P;
}
UCanvasPanelSlot* Place(UCanvasPanel* Canvas, UWidget* Widget, FVector2D Position, FVector2D Size)
{
    auto* Slot = Canvas->AddChildToCanvas(Widget);
    Slot->SetPosition(Position);
    Slot->SetSize(Size);
    return Slot;
}
void Row(UVerticalBox* Box, UWidget* Widget, float Bottom = 10)
{
    Box->AddChildToVerticalBox(Widget)->SetPadding(FMargin(0, 0, 0, Bottom));
}
}

void UGrandNeoBayWidget::SetExperience(AGrandNeoBayExperience* InExperience)
{
    Experience = InExperience;
}

void UGrandNeoBayWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    SetIsFocusable(true);
    if (Canvas) return;
    Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("GrandNeoBayCanvas"));
    WidgetTree->RootWidget = Canvas;
    SceneSurface = WidgetTree->ConstructWidget<UBorder>();
    SceneSurface->SetBrushColor(FLinearColor(0, 0, 0, 0));
    auto* SurfaceSlot = Canvas->AddChildToCanvas(SceneSurface);
    SurfaceSlot->SetAnchors(FAnchors(0, 0, 1, 1));
    SurfaceSlot->SetOffsets(FMargin(0));

    CardGlass = WidgetTree->ConstructWidget<UBackgroundBlur>();
    CardGlass->SetBlurStrength(22.f);
    CardGlass->SetApplyAlphaToBlur(false);
    CardGlass->SetCornerRadius(FVector4(22.f));
    CardGlass->SetLowQualityFallbackBrush(FSlateRoundedBoxBrush(FLinearColor(.025f, .055f, .078f, .82f), 22.f));
    Card = GlassPanel(WidgetTree);
    CardGlass->AddChild(Card);
    auto* CardBox = WidgetTree->ConstructWidget<UVerticalBox>();
    Card->SetContent(CardBox);
    CardEyebrow = Label(WidgetTree, FText::FromString(TEXT("●  当前楼层")), 12, Cyan, true);
    Row(CardBox, CardEyebrow, 8);
    CardTitle = Label(WidgetTree, FText::GetEmpty(), 24, FLinearColor::White, true);
    Row(CardBox, CardTitle, 12);
    CardDescription = Label(WidgetTree, FText::GetEmpty(), 14, Muted);
    Row(CardBox, CardDescription, 19);
    EnterButton = Button(WidgetTree, FText::FromString(TEXT("进入九层   →")), true);
    EnterButton->OnClicked.AddDynamic(this, &UGrandNeoBayWidget::Enter);
    Row(CardBox, EnterButton, 8);
    CloseButton = Button(WidgetTree, FText::FromString(TEXT("收起楼层")));
    CloseLabel = Cast<UTextBlock>(CloseButton->GetChildAt(0));
    CloseButton->OnClicked.AddDynamic(this, &UGrandNeoBayWidget::CloseCard);
    Row(CardBox, CloseButton, 0);
    Place(Canvas, CardGlass, FVector2D(750, 250), FVector2D(325, 286));

    Status = Label(WidgetTree, FText::FromString(TEXT("右键拖动观察  ·  滚轮缩放  ·  9 选择楼层  ·  Esc 返回")), 13, FLinearColor(.85f, .94f, .97f, 1));
    auto* StatusSlot = Place(Canvas, Status, FVector2D(28, -42), FVector2D(700, 30));
    StatusSlot->SetAnchors(FAnchors(0, 1));

    Refresh();
}

void UGrandNeoBayWidget::Refresh()
{
    if (!Canvas || !Experience) return;
    const bool bOpen = Experience->IsOverviewOpen();
    SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    SceneSurface->SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    Status->SetVisibility(bOpen ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    FText BuildingTitle, BuildingDescription;
    const bool bBuildingInfo = Experience->GetSelectedBuildingInfo(BuildingTitle, BuildingDescription);
    const bool bFloor = Experience->IsFloorSelected();
    CardGlass->SetVisibility(bOpen && (bFloor || bBuildingInfo) ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    EnterButton->SetVisibility(bFloor ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    EnterButton->SetIsEnabled(!Experience->IsTransitioning());
    CardEyebrow->SetText(FText::FromString(bFloor ? TEXT("●  当前楼层") : TEXT("●  建筑信息")));
    CardTitle->SetText(bFloor ? Experience->FloorTitle : BuildingTitle);
    CardDescription->SetText(bFloor ? Experience->FloorDescription : BuildingDescription);
    CloseLabel->SetText(FText::FromString(bFloor ? TEXT("收起楼层") : TEXT("收起信息")));
    Cast<UCanvasPanelSlot>(CardGlass->Slot)->SetSize(FVector2D(325, bFloor ? 286 : 222));
    if (!bOpen) bOrbiting = false;
}

bool UGrandNeoBayWidget::IsBuildingInfoCardVisible() const
{
    return CardGlass && EnterButton && CardGlass->GetVisibility() != ESlateVisibility::Collapsed
        && EnterButton->GetVisibility() == ESlateVisibility::Collapsed;
}

void UGrandNeoBayWidget::NativeTick(const FGeometry& Geometry, float DeltaSeconds)
{
    Super::NativeTick(Geometry, DeltaSeconds);
    if (!Experience || !Experience->IsOverviewOpen()) return;
    const FVector2D CursorPosition = FSlateApplication::Get().GetCursorPos();
    UpdatePreviewPointer(Geometry, CursorPosition);
    Experience->SetPointerOverUI(IsPointerOverCard(CursorPosition));
    FVector2D Point;
    const FVector2D Size = Geometry.GetLocalSize();
    const bool bProjected = UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
        GetOwningPlayer(), Experience->GetCardWorldPosition(), Point, true);
    if (!bProjected)
    {
        CardGlass->SetVisibility(ESlateVisibility::Collapsed);
        return;
    }
    Point += FVector2D(35, -80);
    Point.X = FMath::Max(Point.X, Size.X * .72);
    Point.X = FMath::Clamp(Point.X, 24., FMath::Max(24., Size.X - 349));
    const float CardHeight = Experience->IsFloorSelected() ? 286.f : 222.f;
    Point.Y = FMath::Clamp(Point.Y, 24., FMath::Max(24., Size.Y - CardHeight - 24.f));
    Cast<UCanvasPanelSlot>(CardGlass->Slot)->SetPosition(Point);
}

FReply UGrandNeoBayWidget::NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
    if (!Experience || !Experience->IsOverviewOpen()) return FReply::Unhandled();
    UpdatePreviewPointer(Geometry, Event.GetScreenSpacePosition());
    const bool bOverCard = IsPointerOverCard(Event.GetScreenSpacePosition());
    Experience->SetPointerOverUI(bOverCard);
    if (bOverCard) return FReply::Handled();
    if (Event.GetEffectingButton() == EKeys::RightMouseButton)
    {
        bOrbiting = true;
        return FReply::Handled().CaptureMouse(TakeWidget());
    }
    if (Event.GetEffectingButton() == EKeys::LeftMouseButton) Experience->ClickScene();
    return FReply::Handled();
}

FReply UGrandNeoBayWidget::NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
    if (!Experience || !Experience->IsOverviewOpen()) return FReply::Unhandled();
    if (Event.GetEffectingButton() == EKeys::RightMouseButton)
    {
        bOrbiting = false;
        return FReply::Handled().ReleaseMouseCapture();
    }
    return FReply::Handled();
}

FReply UGrandNeoBayWidget::NativeOnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
    if (!Experience || !Experience->IsOverviewOpen()) return FReply::Unhandled();
    UpdatePreviewPointer(Geometry, Event.GetScreenSpacePosition());
    if (bOrbiting) Experience->OrbitPreview(Event.GetCursorDelta());
    return FReply::Handled();
}

FReply UGrandNeoBayWidget::NativeOnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event)
{
    if (!Experience || !Experience->IsOverviewOpen()) return FReply::Unhandled();
    if (!IsPointerOverCard(Event.GetScreenSpacePosition())) Experience->ZoomPreview(Event.GetWheelDelta());
    return FReply::Handled();
}

void UGrandNeoBayWidget::NativeOnMouseCaptureLost(const FCaptureLostEvent& Event)
{
    bOrbiting = false;
    Super::NativeOnMouseCaptureLost(Event);
}

FReply UGrandNeoBayWidget::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    if (!Experience || !Experience->IsOverviewOpen()) return FReply::Unhandled();
    if (Event.GetKey() == EKeys::Escape)
    {
        if (Experience->HasSelection()) CloseCard();
        else Experience->ReturnToOffice();
    }
    else if (Event.GetKey() == EKeys::F9) Experience->ReturnToOffice();
    else if (Event.GetKey() == EKeys::Nine || Event.GetKey() == EKeys::NumPadNine) Experience->SelectFloor();
    else if (Event.GetKey() == EKeys::Enter) Enter();
    return FReply::Handled();
}

void UGrandNeoBayWidget::Enter() { if (Experience) Experience->EnterFloor(); }
void UGrandNeoBayWidget::CloseCard() { if (Experience) Experience->ClearSelection(); }

bool UGrandNeoBayWidget::IsPointerOverCard(FVector2D AbsolutePosition) const
{
    return CardGlass && CardGlass->GetVisibility() != ESlateVisibility::Collapsed
        && CardGlass->GetCachedGeometry().IsUnderLocation(AbsolutePosition);
}

void UGrandNeoBayWidget::UpdatePreviewPointer(const FGeometry& Geometry, FVector2D AbsolutePosition)
{
    if (!Experience || !GetOwningPlayer()) return;
    int32 Width = 0, Height = 0;
    GetOwningPlayer()->GetViewportSize(Width, Height);
    const FVector2D Size = Geometry.GetLocalSize();
    if (Size.X <= 0 || Size.Y <= 0) return;
    const FVector2D Local = Geometry.AbsoluteToLocal(AbsolutePosition);
    const bool bInside = Local.X >= 0 && Local.Y >= 0 && Local.X < Size.X && Local.Y < Size.Y;
    Experience->SetPreviewPointer(FVector2D(Local.X * Width / Size.X, Local.Y * Height / Size.Y), bInside);
}

bool UGrandNeoBayWidget::TestActivateControl(FName Control, FVector2D ViewportPoint)
{
    UWidget* Target = Control == TEXT("Enter") ? static_cast<UWidget*>(EnterButton)
        : Control == TEXT("Close") ? static_cast<UWidget*>(CloseButton) : static_cast<UWidget*>(SceneSurface);
    if (!Target || !Target->GetCachedWidget().IsValid()) return false;
    if (UButton* TestButton = Cast<UButton>(Target))
    {
        if (!TestButton->GetIsEnabled() || TestButton->GetVisibility() == ESlateVisibility::Collapsed) return false;
        TestButton->OnClicked.Broadcast();
        return true;
    }
    const FGeometry& Geometry = Target->GetCachedGeometry();
    FVector2D Point = Geometry.LocalToAbsolute(Geometry.GetLocalSize() * .5);
    if (Control == TEXT("Scene") || Control == TEXT("Hover"))
    {
        int32 Width = 0, Height = 0;
        GetOwningPlayer()->GetViewportSize(Width, Height);
        const FGeometry& RootGeometry = GetCachedGeometry();
        const FVector2D Size = RootGeometry.GetLocalSize();
        Point = RootGeometry.LocalToAbsolute(FVector2D(ViewportPoint.X * Size.X / Width, ViewportPoint.Y * Size.Y / Height));
    }
    const TSet<FKey> Pressed{EKeys::LeftMouseButton};
    const TSet<FKey> Released;
    const FPointerEvent Down(0, Point, Point, Pressed, EKeys::LeftMouseButton, 0, FModifierKeysState());
    const FPointerEvent Up(0, Point, Point, Released, EKeys::LeftMouseButton, 0, FModifierKeysState());
    if (Control == TEXT("Hover"))
    {
        FSlateApplication::Get().SetCursorPos(Point);
        return NativeOnMouseMove(GetCachedGeometry(), Up).IsEventHandled();
    }
    const bool bDown = NativeOnMouseButtonDown(GetCachedGeometry(), Down).IsEventHandled();
    const bool bUp = NativeOnMouseButtonUp(GetCachedGeometry(), Up).IsEventHandled();
    return bDown && bUp;
}
