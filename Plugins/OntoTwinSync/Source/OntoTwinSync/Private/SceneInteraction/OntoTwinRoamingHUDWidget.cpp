#include "SceneInteraction/OntoTwinRoamingHUDWidget.h"

#include "SceneInteraction/Minimap/OntoTwinMinimapWidget.h"
#include "SceneInteraction/TwinInteractionManagerComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ComboBoxString.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/CoreStyle.h"

namespace
{
// Performance glass: neutral deep gray at 20% opacity, with no blur/postbuffer.
const FLinearColor GlassFill(0.10f, 0.10f, 0.10f, 0.20f);
const FLinearColor GlassStroke(0.86f, 0.86f, 0.86f, 0.16f);
const FLinearColor KeyFill(0.12f, 0.12f, 0.12f, 0.20f);
const FLinearColor KeyStroke(0.90f, 0.90f, 0.90f, 0.18f);
const FLinearColor PrimaryText(0.96f, 0.96f, 0.96f, 1.0f);
const FLinearColor SecondaryText(0.80f, 0.80f, 0.80f, 0.96f);
const FLinearColor TextShadow(0.0f, 0.0f, 0.0f, 0.58f);
const FLinearColor ActiveButton(0.34f, 0.34f, 0.34f, 0.58f);
const FLinearColor InactiveButton(0.14f, 0.14f, 0.14f, 0.30f);

FComboBoxStyle BuildRouteSelectorStyle()
{
    FComboBoxStyle Style =
        FCoreStyle::Get().GetWidgetStyle<FComboBoxStyle>(TEXT("ComboBox"));
    FComboButtonStyle ComboButton = Style.ComboButtonStyle;

    FButtonStyle ButtonStyle = ComboButton.ButtonStyle;
    ButtonStyle
        .SetNormal(FSlateRoundedBoxBrush(
            FLinearColor(0.10f, 0.10f, 0.10f, 0.34f),
            7.0f,
            FLinearColor(0.90f, 0.90f, 0.90f, 0.16f),
            1.0f))
        .SetHovered(FSlateRoundedBoxBrush(
            FLinearColor(0.18f, 0.18f, 0.18f, 0.48f),
            7.0f,
            FLinearColor(0.96f, 0.96f, 0.96f, 0.30f),
            1.0f))
        .SetPressed(FSlateRoundedBoxBrush(
            FLinearColor(0.24f, 0.24f, 0.24f, 0.58f),
            7.0f,
            FLinearColor(0.98f, 0.98f, 0.98f, 0.38f),
            1.0f))
        .SetDisabled(FSlateRoundedBoxBrush(
            FLinearColor(0.08f, 0.08f, 0.08f, 0.24f),
            7.0f,
            FLinearColor(0.80f, 0.80f, 0.80f, 0.10f),
            1.0f));

    FSlateBrush ArrowBrush = ComboButton.DownArrowImage;
    ArrowBrush.TintColor = FSlateColor(PrimaryText);
    ComboButton
        .SetButtonStyle(ButtonStyle)
        .SetDownArrowImage(ArrowBrush)
        .SetMenuBorderBrush(FSlateRoundedBoxBrush(
            FLinearColor(0.055f, 0.055f, 0.055f, 0.96f),
            8.0f,
            FLinearColor(0.94f, 0.94f, 0.94f, 0.20f),
            1.0f))
        .SetMenuBorderPadding(FMargin(4.0f))
        .SetDownArrowPadding(FMargin(8.0f, 0.0f, 8.0f, 0.0f));

    return Style
        .SetComboButtonStyle(ComboButton)
        .SetMenuRowPadding(FMargin(8.0f, 5.0f));
}

FTableRowStyle BuildRouteSelectorRowStyle()
{
    const FSlateRoundedBoxBrush ClearRow(FLinearColor::Transparent, 5.0f);
    const FSlateRoundedBoxBrush HoveredRow(
        FLinearColor(0.94f, 0.94f, 0.94f, 0.10f), 5.0f);
    const FSlateRoundedBoxBrush SelectedRow(
        FLinearColor(0.94f, 0.94f, 0.94f, 0.16f),
        5.0f,
        FLinearColor(0.98f, 0.98f, 0.98f, 0.22f),
        1.0f);
    const FSlateRoundedBoxBrush SelectedHoveredRow(
        FLinearColor(0.98f, 0.98f, 0.98f, 0.22f),
        5.0f,
        FLinearColor(1.0f, 1.0f, 1.0f, 0.30f),
        1.0f);

    FTableRowStyle Style =
        FCoreStyle::Get().GetWidgetStyle<FTableRowStyle>(TEXT("ComboBox.Row"));
    return Style
        .SetSelectorFocusedBrush(SelectedHoveredRow)
        .SetActiveHoveredBrush(SelectedHoveredRow)
        .SetActiveBrush(SelectedRow)
        .SetInactiveHoveredBrush(SelectedHoveredRow)
        .SetInactiveBrush(SelectedRow)
        .SetEvenRowBackgroundHoveredBrush(HoveredRow)
        .SetEvenRowBackgroundBrush(ClearRow)
        .SetOddRowBackgroundHoveredBrush(HoveredRow)
        .SetOddRowBackgroundBrush(ClearRow)
        .SetTextColor(FSlateColor(PrimaryText))
        .SetSelectedTextColor(FSlateColor(PrimaryText));
}
}

TSharedRef<SWidget> UOntoTwinRoamingHUDWidget::RebuildWidget()
{
    if (!WidgetTree)
    {
        WidgetTree = NewObject<UWidgetTree>(this, TEXT("RoamingHUDWidgetTree"), RF_Transient);
    }
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        BuildDefaultLayout();
    }
    TSharedRef<SWidget> Result = Super::RebuildWidget();
    RefreshFromManager();
    return Result;
}

void UOntoTwinRoamingHUDWidget::NativeTick(
    const FGeometry& MyGeometry,
    float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    if (!StatusPulse) return;
    StatusPulsePhase = FMath::Fmod(
        StatusPulsePhase + InDeltaTime * 2.2f, 2.0f * PI);
    const float Normalized = 0.5f + 0.5f * FMath::Sin(StatusPulsePhase);
    const float Scale = FMath::Lerp(0.92f, 1.08f, Normalized);
    StatusPulse->SetRenderScale(FVector2D(Scale, Scale));
}

UBorder* UOntoTwinRoamingHUDWidget::BuildGlassSurface(
    const FName Name,
    const FMargin& SurfacePadding,
    float Radius)
{
    UBorder* Glass = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), Name);
    Glass->SetPadding(SurfacePadding);
    Glass->SetBrush(FSlateRoundedBoxBrush(GlassFill, Radius, GlassStroke, 1.0f));
    Glass->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    return Glass;
}

void UOntoTwinRoamingHUDWidget::BuildDefaultLayout()
{
    UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
        UCanvasPanel::StaticClass(), TEXT("HUDCanvas"));
    Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    WidgetTree->RootWidget = Root;

    MinimapWidget = WidgetTree->ConstructWidget<UOntoTwinMinimapWidget>(
        UOntoTwinMinimapWidget::StaticClass(), TEXT("MinimapWidget"));
    MinimapWidget->SetInteractionManager(Manager);
    MinimapWidget->SetVisibility(ESlateVisibility::Collapsed);
    UCanvasPanelSlot* MinimapSlot = Root->AddChildToCanvas(MinimapWidget);
    // The minimap owns a full logical-viewport canvas.  Its visible panel and
    // button are positioned inside that canvas so changing panel size or
    // growth direction never changes the button's screen anchor.
    MinimapSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
    MinimapSlot->SetOffsets(FMargin(0.0f));
    MinimapSlot->SetAlignment(FVector2D::ZeroVector);
    MinimapSlot->SetAutoSize(false);

    // Bottom-center status is direct content without a backing plate.
    UHorizontalBox* StatusRow = WidgetTree->ConstructWidget<UHorizontalBox>(
        UHorizontalBox::StaticClass(), TEXT("StatusRow"));
    StatusRow->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

    StatusPulse = WidgetTree->ConstructWidget<USizeBox>(
        USizeBox::StaticClass(), TEXT("StatusPulse"));
    StatusPulse->SetWidthOverride(18.0f);
    StatusPulse->SetHeightOverride(18.0f);
    StatusPulse->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
    StatusPulse->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UOverlay* PulseLayers = WidgetTree->ConstructWidget<UOverlay>(
        UOverlay::StaticClass(), TEXT("StatusPulseLayers"));
    PulseLayers->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    StatusPulse->AddChild(PulseLayers);

    UBorder* PulseRing = WidgetTree->ConstructWidget<UBorder>(
        UBorder::StaticClass(), TEXT("StatusPulseRing"));
    PulseRing->SetBrush(FSlateRoundedBoxBrush(
        FLinearColor::Transparent, 9.0f, PrimaryText, 1.0f));
    PulseRing->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    PulseLayers->AddChildToOverlay(PulseRing);

    USizeBox* PulseCoreBounds = WidgetTree->ConstructWidget<USizeBox>(
        USizeBox::StaticClass(), TEXT("StatusPulseCoreBounds"));
    PulseCoreBounds->SetWidthOverride(9.0f);
    PulseCoreBounds->SetHeightOverride(9.0f);
    PulseCoreBounds->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UBorder* PulseCore = WidgetTree->ConstructWidget<UBorder>(
        UBorder::StaticClass(), TEXT("StatusPulseCore"));
    PulseCore->SetBrush(FSlateRoundedBoxBrush(
        FLinearColor(0.92f, 0.92f, 0.92f, 0.76f), 4.5f));
    PulseCore->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    PulseCoreBounds->AddChild(PulseCore);
    UOverlaySlot* PulseCoreSlot = PulseLayers->AddChildToOverlay(PulseCoreBounds);
    PulseCoreSlot->SetHorizontalAlignment(HAlign_Center);
    PulseCoreSlot->SetVerticalAlignment(VAlign_Center);
    UHorizontalBoxSlot* PulseSlot = StatusRow->AddChildToHorizontalBox(StatusPulse);
    PulseSlot->SetVerticalAlignment(VAlign_Center);
    PulseSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));

    StatusText = WidgetTree->ConstructWidget<UTextBlock>(
        UTextBlock::StaticClass(), TEXT("StatusText"));
    StatusText->SetColorAndOpacity(PrimaryText);
    StatusText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 11));
    StatusText->SetShadowOffset(FVector2D(1.0f, 1.0f));
    StatusText->SetShadowColorAndOpacity(TextShadow);
    StatusText->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UHorizontalBoxSlot* StatusTextSlot = StatusRow->AddChildToHorizontalBox(StatusText);
    StatusTextSlot->SetVerticalAlignment(VAlign_Center);
    UCanvasPanelSlot* StatusSlot = Root->AddChildToCanvas(StatusRow);
    StatusSlot->SetAnchors(FAnchors(0.5f, 1.0f));
    StatusSlot->SetAlignment(FVector2D(0.5f, 1.0f));
    // Leave a clear visual lane for the RuntimeDock's compact bottom handle.
    StatusSlot->SetPosition(FVector2D(0.0f, -40.0f));
    StatusSlot->SetAutoSize(true);

    // Contextual shortcuts have no shared plate. Only each key gets a small chip.
    USizeBox* HintBounds = WidgetTree->ConstructWidget<USizeBox>(
        USizeBox::StaticClass(), TEXT("HintBounds"));
    HintBounds->SetWidthOverride(152.0f);
    HintBounds->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    HintList = WidgetTree->ConstructWidget<UVerticalBox>(
        UVerticalBox::StaticClass(), TEXT("HintList"));
    HintList->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    HintBounds->AddChild(HintList);
    UCanvasPanelSlot* HintSlot = Root->AddChildToCanvas(HintBounds);
    HintSlot->SetAnchors(FAnchors(1.0f, 1.0f));
    HintSlot->SetAlignment(FVector2D(1.0f, 1.0f));
    HintSlot->SetPosition(FVector2D(-24.0f, -24.0f));
    HintSlot->SetAutoSize(true);

    // Tab opens a compact action drawer above the status capsule.
    USizeBox* DrawerBounds = WidgetTree->ConstructWidget<USizeBox>(
        USizeBox::StaticClass(), TEXT("DrawerBounds"));
    DrawerBounds->SetWidthOverride(650.0f);
    DetailPanel = BuildGlassSurface(
        TEXT("ActionDrawer"), FMargin(16.0f, 12.0f), 18.0f);
    DrawerBounds->AddChild(DetailPanel);
    UCanvasPanelSlot* DrawerSlot = Root->AddChildToCanvas(DrawerBounds);
    DrawerSlot->SetAnchors(FAnchors(0.5f, 1.0f));
    DrawerSlot->SetAlignment(FVector2D(0.5f, 1.0f));
    DrawerSlot->SetPosition(FVector2D(0.0f, -78.0f));
    DrawerSlot->SetAutoSize(true);

    UVerticalBox* DrawerStack = WidgetTree->ConstructWidget<UVerticalBox>(
        UVerticalBox::StaticClass(), TEXT("DrawerStack"));
    DetailPanel->SetContent(DrawerStack);

    DrawerTitle = WidgetTree->ConstructWidget<UTextBlock>(
        UTextBlock::StaticClass(), TEXT("DrawerTitle"));
    DrawerTitle->SetText(FText::FromString(TEXT("漫游控制")));
    DrawerTitle->SetColorAndOpacity(PrimaryText);
    DrawerTitle->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 11));
    DrawerTitle->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    DrawerStack->AddChildToVerticalBox(DrawerTitle);

    DetailText = WidgetTree->ConstructWidget<UTextBlock>(
        UTextBlock::StaticClass(), TEXT("DetailText"));
    DetailText->SetColorAndOpacity(SecondaryText);
    DetailText->SetAutoWrapText(true);
    DetailText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9));
    DetailText->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UVerticalBoxSlot* DetailTextSlot = DrawerStack->AddChildToVerticalBox(DetailText);
    DetailTextSlot->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 8.0f));

    UHorizontalBox* DockTabs = WidgetTree->ConstructWidget<UHorizontalBox>(
        UHorizontalBox::StaticClass(), TEXT("DockTabs"));
    UVerticalBoxSlot* DockTabsSlot = DrawerStack->AddChildToVerticalBox(DockTabs);
    DockTabsSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
    HomeTabButton = AddActionButton(nullptr, TEXT("HomeTabButton"), TEXT("Home"));
    ZoneTabButton = AddActionButton(nullptr, TEXT("ZoneTabButton"), TEXT("空间"));
    BusinessTabButton = AddActionButton(nullptr, TEXT("BusinessTabButton"), TEXT("业务"));
    RoamingTabButton = AddActionButton(nullptr, TEXT("RoamingTabButton"), TEXT("漫游"));
    for (UButton* Button : {HomeTabButton, ZoneTabButton, BusinessTabButton, RoamingTabButton})
    {
        UHorizontalBoxSlot* TabSlot = DockTabs->AddChildToHorizontalBox(Button);
        TabSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        TabSlot->SetPadding(FMargin(0.0f, 0.0f, 6.0f, 0.0f));
    }
    HomeTabButton->OnClicked.AddDynamic(this, &UOntoTwinRoamingHUDWidget::OnHomeTab);
    ZoneTabButton->OnClicked.AddDynamic(this, &UOntoTwinRoamingHUDWidget::OnZoneTab);
    BusinessTabButton->OnClicked.AddDynamic(this, &UOntoTwinRoamingHUDWidget::OnBusinessTab);
    RoamingTabButton->OnClicked.AddDynamic(this, &UOntoTwinRoamingHUDWidget::OnRoamingTab);

    const auto CreateWebRow = [this, DrawerStack](const FName Name, const FString& Label)
    {
        UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(
            UHorizontalBox::StaticClass(), Name);
        Row->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        UVerticalBoxSlot* RowSlot = DrawerStack->AddChildToVerticalBox(Row);
        RowSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));

        USizeBox* LabelBounds = WidgetTree->ConstructWidget<USizeBox>(
            USizeBox::StaticClass(), FName(*(Name.ToString() + TEXT("LabelBounds"))));
        LabelBounds->SetWidthOverride(76.0f);
        LabelBounds->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        UTextBlock* RowLabel = WidgetTree->ConstructWidget<UTextBlock>(
            UTextBlock::StaticClass(), FName(*(Name.ToString() + TEXT("Label"))));
        RowLabel->SetText(FText::FromString(Label));
        RowLabel->SetColorAndOpacity(SecondaryText);
        RowLabel->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9));
        RowLabel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        LabelBounds->AddChild(RowLabel);
        UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(LabelBounds);
        LabelSlot->SetVerticalAlignment(VAlign_Center);
        LabelSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
        return Row;
    };

    const auto AddWebSelector = [this](
        UHorizontalBox* Row,
        const FName Name,
        const float Width,
        UComboBoxString*& OutSelector)
    {
        USizeBox* Bounds = WidgetTree->ConstructWidget<USizeBox>(
            USizeBox::StaticClass(), FName(*(Name.ToString() + TEXT("Bounds"))));
        Bounds->SetWidthOverride(Width);
        OutSelector = WidgetTree->ConstructWidget<UComboBoxString>(
            UComboBoxString::StaticClass(), Name);
        OutSelector->SetWidgetStyle(BuildRouteSelectorStyle());
        OutSelector->SetItemStyle(BuildRouteSelectorRowStyle());
        OutSelector->SetContentPadding(FMargin(10.0f, 6.0f));
        OutSelector->SetMaxListHeight(260.0f);
        OutSelector->OnGenerateWidgetEvent.BindDynamic(
            this, &UOntoTwinRoamingHUDWidget::GenerateRouteOptionWidget);
        Bounds->AddChild(OutSelector);
        UHorizontalBoxSlot* Slot = Row->AddChildToHorizontalBox(Bounds);
        Slot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
    };

    UVerticalBox* ZoneStack = WidgetTree->ConstructWidget<UVerticalBox>(
        UVerticalBox::StaticClass(), TEXT("ZonePanel"));
    ZonePanel = ZoneStack;
    DrawerStack->AddChildToVerticalBox(ZoneStack);
    UHorizontalBox* WebZoneRow = CreateWebRow(TEXT("WebZoneRow"), TEXT("空间分区"));
    if (WebZoneRow->Slot) WebZoneRow->RemoveFromParent();
    ZoneStack->AddChildToVerticalBox(WebZoneRow);
    AddWebSelector(WebZoneRow, TEXT("WebZoneSelector"), 510.0f, WebZoneSelector);
    WebZoneSelector->OnSelectionChanged.AddDynamic(
        this, &UOntoTwinRoamingHUDWidget::OnWebZoneSelected);

    UVerticalBox* BusinessStack = WidgetTree->ConstructWidget<UVerticalBox>(
        UVerticalBox::StaticClass(), TEXT("BusinessPanel"));
    BusinessPanel = BusinessStack;
    DrawerStack->AddChildToVerticalBox(BusinessStack);
    UHorizontalBox* WebBusinessScopeRow = CreateWebRow(
        TEXT("WebBusinessScopeRow"), TEXT("作用范围"));
    if (WebBusinessScopeRow->Slot) WebBusinessScopeRow->RemoveFromParent();
    BusinessStack->AddChildToVerticalBox(WebBusinessScopeRow);
    AddWebSelector(
        WebBusinessScopeRow, TEXT("WebBusinessZoneSelector"), 510.0f,
        WebBusinessZoneSelector);
    WebBusinessZoneSelector->OnSelectionChanged.AddDynamic(
        this, &UOntoTwinRoamingHUDWidget::OnWebBusinessScopeSelected);
    UHorizontalBox* WebBusinessRow = CreateWebRow(TEXT("WebBusinessRow"), TEXT("业务管理"));
    if (WebBusinessRow->Slot) WebBusinessRow->RemoveFromParent();
    BusinessStack->AddChildToVerticalBox(WebBusinessRow);
    AddWebSelector(
        WebBusinessRow, TEXT("WebBusinessSelector"), 510.0f, WebBusinessSelector);
    WebBusinessSelector->OnSelectionChanged.AddDynamic(
        this, &UOntoTwinRoamingHUDWidget::OnWebBusinessSelected);

    UVerticalBox* RoamingStack = WidgetTree->ConstructWidget<UVerticalBox>(
        UVerticalBox::StaticClass(), TEXT("RoamingPanel"));
    RoamingPanel = RoamingStack;
    DrawerStack->AddChildToVerticalBox(RoamingStack);

    UHorizontalBox* CharacterRow = WidgetTree->ConstructWidget<UHorizontalBox>(
        UHorizontalBox::StaticClass(), TEXT("CharacterSelectorRow"));
    RoamingCharacterRow = CharacterRow;
    CharacterRow->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UVerticalBoxSlot* CharacterRowSlot = RoamingStack->AddChildToVerticalBox(CharacterRow);
    CharacterRowSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));

    UTextBlock* CharacterLabel = WidgetTree->ConstructWidget<UTextBlock>(
        UTextBlock::StaticClass(), TEXT("CharacterSelectorLabel"));
    CharacterLabel->SetText(FText::FromString(TEXT("当前人物")));
    CharacterLabel->SetColorAndOpacity(SecondaryText);
    CharacterLabel->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9));
    CharacterLabel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UHorizontalBoxSlot* CharacterLabelSlot =
        CharacterRow->AddChildToHorizontalBox(CharacterLabel);
    CharacterLabelSlot->SetVerticalAlignment(VAlign_Center);
    CharacterLabelSlot->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));

    USizeBox* CharacterSelectorBounds = WidgetTree->ConstructWidget<USizeBox>(
        USizeBox::StaticClass(), TEXT("CharacterSelectorBounds"));
    CharacterSelectorBounds->SetWidthOverride(360.0f);
    CharacterSelector = WidgetTree->ConstructWidget<UComboBoxString>(
        UComboBoxString::StaticClass(), TEXT("CharacterSelector"));
    CharacterSelector->SetWidgetStyle(BuildRouteSelectorStyle());
    CharacterSelector->SetItemStyle(BuildRouteSelectorRowStyle());
    CharacterSelector->SetContentPadding(FMargin(10.0f, 6.0f));
    CharacterSelector->SetMaxListHeight(260.0f);
    CharacterSelector->OnGenerateWidgetEvent.BindDynamic(
        this, &UOntoTwinRoamingHUDWidget::GenerateRouteOptionWidget);
    CharacterSelector->OnSelectionChanged.AddDynamic(
        this, &UOntoTwinRoamingHUDWidget::OnCharacterSelected);
    CharacterSelectorBounds->AddChild(CharacterSelector);
    CharacterRow->AddChildToHorizontalBox(CharacterSelectorBounds);

    UHorizontalBox* RouteRow = WidgetTree->ConstructWidget<UHorizontalBox>(
        UHorizontalBox::StaticClass(), TEXT("RouteSelectorRow"));
    RoamingRouteRow = RouteRow;
    RouteRow->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UVerticalBoxSlot* RouteRowSlot = RoamingStack->AddChildToVerticalBox(RouteRow);
    RouteRowSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));

    UTextBlock* RouteLabel = WidgetTree->ConstructWidget<UTextBlock>(
        UTextBlock::StaticClass(), TEXT("RouteSelectorLabel"));
    RouteLabel->SetText(FText::FromString(TEXT("运行线路")));
    RouteLabel->SetColorAndOpacity(SecondaryText);
    RouteLabel->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9));
    RouteLabel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UHorizontalBoxSlot* RouteLabelSlot = RouteRow->AddChildToHorizontalBox(RouteLabel);
    RouteLabelSlot->SetVerticalAlignment(VAlign_Center);
    RouteLabelSlot->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));

    USizeBox* RouteSelectorBounds = WidgetTree->ConstructWidget<USizeBox>(
        USizeBox::StaticClass(), TEXT("RouteSelectorBounds"));
    RouteSelectorBounds->SetWidthOverride(360.0f);
    RouteSelector = WidgetTree->ConstructWidget<UComboBoxString>(
        UComboBoxString::StaticClass(), TEXT("RouteSelector"));
    RouteSelector->SetWidgetStyle(BuildRouteSelectorStyle());
    RouteSelector->SetItemStyle(BuildRouteSelectorRowStyle());
    RouteSelector->SetContentPadding(FMargin(10.0f, 6.0f));
    RouteSelector->SetMaxListHeight(260.0f);
    RouteSelector->OnGenerateWidgetEvent.BindDynamic(
        this, &UOntoTwinRoamingHUDWidget::GenerateRouteOptionWidget);
    RouteSelector->OnSelectionChanged.AddDynamic(
        this, &UOntoTwinRoamingHUDWidget::OnRouteSelected);
    RouteSelectorBounds->AddChild(RouteSelector);
    RouteRow->AddChildToHorizontalBox(RouteSelectorBounds);

    UHorizontalBox* ViewModes = WidgetTree->ConstructWidget<UHorizontalBox>(
        UHorizontalBox::StaticClass(), TEXT("ViewModes"));
    RoamingViewModes = ViewModes;
    UVerticalBoxSlot* ViewModesSlot = RoamingStack->AddChildToVerticalBox(ViewModes);
    ViewModesSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
    GlobalButton = AddActionButton(nullptr, TEXT("GlobalButton"), TEXT("上帝视角"));
    ShoulderButton = AddActionButton(nullptr, TEXT("ShoulderButton"), TEXT("过肩视角"));
    FirstPersonButton = AddActionButton(nullptr, TEXT("FirstPersonButton"), TEXT("第一人称"));
    for (UButton* Button : {GlobalButton, ShoulderButton, FirstPersonButton})
    {
        UHorizontalBoxSlot* ViewSlot = ViewModes->AddChildToHorizontalBox(Button);
        ViewSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
    }
    FirstPersonButton->OnClicked.AddDynamic(this, &UOntoTwinRoamingHUDWidget::OnFirstPerson);
    ShoulderButton->OnClicked.AddDynamic(this, &UOntoTwinRoamingHUDWidget::OnShoulder);
    GlobalButton->OnClicked.AddDynamic(this, &UOntoTwinRoamingHUDWidget::OnGlobal);

    UHorizontalBox* Actions = WidgetTree->ConstructWidget<UHorizontalBox>(
        UHorizontalBox::StaticClass(), TEXT("Actions"));
    RoamingActions = Actions;
    RoamingStack->AddChildToVerticalBox(Actions);
    UButton* SkinButton = AddActionButton(nullptr, TEXT("SkinButton"), TEXT("切换皮肤"));
    UButton* ResumeButton = AddActionButton(nullptr, TEXT("ResumeButton"), TEXT("返回线路"));
    UButton* RestartButton = AddActionButton(nullptr, TEXT("RestartButton"), TEXT("从头开始"));
    UButton* ReloadButton = AddActionButton(nullptr, TEXT("ReloadButton"), TEXT("重载人物"));
    for (UButton* Button : {SkinButton, ResumeButton, RestartButton, ReloadButton})
    {
        UHorizontalBoxSlot* ActionSlot = Actions->AddChildToHorizontalBox(Button);
        ActionSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
    }
    SkinButton->OnClicked.AddDynamic(this, &UOntoTwinRoamingHUDWidget::OnCycleSkin);
    ResumeButton->OnClicked.AddDynamic(this, &UOntoTwinRoamingHUDWidget::OnResumeRoute);
    RestartButton->OnClicked.AddDynamic(this, &UOntoTwinRoamingHUDWidget::OnRestartRoute);
    ReloadButton->OnClicked.AddDynamic(this, &UOntoTwinRoamingHUDWidget::OnReloadCharacter);
    DetailPanel->SetVisibility(ESlateVisibility::Collapsed);
    SetDockTab(1);
    RefreshShortcutList();
}

UWidget* UOntoTwinRoamingHUDWidget::GenerateRouteOptionWidget(FString Item)
{
    UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(
        UTextBlock::StaticClass());
    Text->SetText(FText::FromString(Item));
    Text->SetColorAndOpacity(PrimaryText);
    Text->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9));
    Text->SetShadowOffset(FVector2D(1.0f, 1.0f));
    Text->SetShadowColorAndOpacity(TextShadow);
    Text->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    RetainedComboTextWidgets.Add(Text);
    return Text;
}

void UOntoTwinRoamingHUDWidget::RefreshShortcutList()
{
    if (!Manager || !HintList) return;
    TArray<FString> Keys;
    TArray<FString> Descriptions;
    Manager->GetHudShortcutItems(Keys, Descriptions);
    FString NextSignature;
    for (int32 Index = 0; Index < Keys.Num() && Index < Descriptions.Num(); ++Index)
    {
        NextSignature += Keys[Index] + TEXT("\x1f") + Descriptions[Index] + TEXT("\x1e");
    }
    if (NextSignature == ShortcutSignature) return;

    ShortcutSignature = NextSignature;
    HintList->ClearChildren();
    for (int32 Index = 0; Index < Keys.Num() && Index < Descriptions.Num(); ++Index)
    {
        AddShortcutRow(Index, Keys[Index], Descriptions[Index]);
    }
}

void UOntoTwinRoamingHUDWidget::RefreshRouteSelector()
{
    if (!Manager || !RouteSelector) return;
    TArray<FString> RouteIds;
    TArray<FString> DisplayNames;
    TArray<bool> DefaultFlags;
    Manager->GetAvailableRuntimeRoutes(RouteIds, DisplayNames, DefaultFlags);

    FString NextSignature = Manager->GetActiveRuntimeRouteId();
    NextSignature += Manager->IsRouteSwitching() ? TEXT("|switching") : TEXT("|ready");
    for (int32 Index = 0; Index < RouteIds.Num(); ++Index)
    {
        NextSignature += TEXT("|") + RouteIds[Index];
        if (DisplayNames.IsValidIndex(Index)) NextSignature += TEXT(":") + DisplayNames[Index];
        if (DefaultFlags.IsValidIndex(Index) && DefaultFlags[Index]) NextSignature += TEXT(":default");
    }
    if (NextSignature == RouteSignature) return;

    RouteSignature = NextSignature;
    RouteOptionIds.Reset();
    RouteOptionLabels.Reset();
    bRefreshingRouteSelector = true;
    RouteSelector->ClearOptions();
    for (int32 Index = 0; Index < RouteIds.Num(); ++Index)
    {
        const FString RouteId = RouteIds[Index];
        FString Label = DisplayNames.IsValidIndex(Index) && !DisplayNames[Index].IsEmpty()
            ? DisplayNames[Index] : RouteId;
        if (DefaultFlags.IsValidIndex(Index) && DefaultFlags[Index])
        {
            Label += TEXT("（默认）");
        }
        if (RouteOptionLabels.Contains(Label))
        {
            Label += FString::Printf(TEXT("（%s）"), *RouteId);
        }
        RouteOptionIds.Add(RouteId);
        RouteOptionLabels.Add(Label);
        RouteSelector->AddOption(Label);
    }

    if (RouteOptionIds.IsEmpty())
    {
        const FString EmptyLabel = TEXT("当前没有可切换路线");
        RouteOptionLabels.Add(EmptyLabel);
        RouteOptionIds.Add(FString());
        RouteSelector->AddOption(EmptyLabel);
        RouteSelector->SetSelectedIndex(0);
        RouteSelector->SetIsEnabled(false);
    }
    else
    {
        const int32 ActiveIndex = RouteOptionIds.IndexOfByKey(
            Manager->GetActiveRuntimeRouteId());
        RouteSelector->SetSelectedIndex(ActiveIndex == INDEX_NONE ? 0 : ActiveIndex);
        RouteSelector->SetIsEnabled(
            RouteOptionIds.Num() > 1 && !Manager->IsRouteSwitching());
    }
    bRefreshingRouteSelector = false;
}

void UOntoTwinRoamingHUDWidget::RefreshCharacterSelector()
{
    if (!Manager || !CharacterSelector) return;
    TArray<FString> CharacterIds;
    TArray<FString> DisplayNames;
    Manager->GetAvailableRuntimeCharacters(CharacterIds, DisplayNames);

    FString NextSignature = Manager->GetActiveRuntimeCharacterId();
    NextSignature += Manager->IsCharacterSwitching()
        ? TEXT("|switching") : TEXT("|ready");
    for (int32 Index = 0; Index < CharacterIds.Num(); ++Index)
    {
        NextSignature += TEXT("|") + CharacterIds[Index];
        if (DisplayNames.IsValidIndex(Index))
        {
            NextSignature += TEXT(":") + DisplayNames[Index];
        }
    }
    if (NextSignature == CharacterSignature) return;

    CharacterSignature = NextSignature;
    CharacterOptionIds.Reset();
    CharacterOptionLabels.Reset();
    bRefreshingCharacterSelector = true;
    CharacterSelector->ClearOptions();
    for (int32 Index = 0; Index < CharacterIds.Num(); ++Index)
    {
        const FString CharacterId = CharacterIds[Index];
        FString Label = DisplayNames.IsValidIndex(Index) && !DisplayNames[Index].IsEmpty()
            ? DisplayNames[Index] : CharacterId;
        if (CharacterOptionLabels.Contains(Label))
        {
            Label += FString::Printf(TEXT("（%s）"), *CharacterId);
        }
        CharacterOptionIds.Add(CharacterId);
        CharacterOptionLabels.Add(Label);
        CharacterSelector->AddOption(Label);
    }

    if (CharacterOptionIds.IsEmpty())
    {
        CharacterOptionIds.Add(FString());
        CharacterOptionLabels.Add(TEXT("当前没有可用人物"));
        CharacterSelector->AddOption(CharacterOptionLabels[0]);
        CharacterSelector->SetSelectedIndex(0);
        CharacterSelector->SetIsEnabled(false);
    }
    else
    {
        const int32 ActiveIndex = CharacterOptionIds.IndexOfByKey(
            Manager->GetActiveRuntimeCharacterId());
        CharacterSelector->SetSelectedIndex(ActiveIndex == INDEX_NONE ? 0 : ActiveIndex);
        CharacterSelector->SetIsEnabled(
            CharacterOptionIds.Num() > 1 && !Manager->IsCharacterSwitching());
    }
    bRefreshingCharacterSelector = false;
}

void UOntoTwinRoamingHUDWidget::RefreshWebSelectors()
{
    if (!Manager || !WebZoneSelector || !WebBusinessSelector
        || !WebBusinessZoneSelector) return;

    TArray<FString> ZoneIds;
    TArray<FString> ZoneNames;
    TArray<FString> BusinessViewIds;
    TArray<FString> BusinessViewNames;
    Manager->GetAvailableWebZones(ZoneIds, ZoneNames);
    Manager->GetAvailableWebBusinessViews(BusinessViewIds, BusinessViewNames);

    FString NextSignature = TEXT("catalog");
    for (int32 Index = 0; Index < ZoneIds.Num(); ++Index)
    {
        NextSignature += TEXT("|z:") + ZoneIds[Index] + TEXT(":");
        if (ZoneNames.IsValidIndex(Index)) NextSignature += ZoneNames[Index];
    }
    for (int32 Index = 0; Index < BusinessViewIds.Num(); ++Index)
    {
        NextSignature += TEXT("|b:") + BusinessViewIds[Index] + TEXT(":");
        if (BusinessViewNames.IsValidIndex(Index)) NextSignature += BusinessViewNames[Index];
    }
    if (NextSignature == WebCatalogSignature) return;
    WebCatalogSignature = MoveTemp(NextSignature);

    bRefreshingWebSelectors = true;
    WebZoneOptionIds.Reset();
    WebZoneOptionLabels.Reset();
    WebBusinessOptionIds.Reset();
    WebBusinessOptionLabels.Reset();
    WebBusinessZoneOptionIds.Reset();
    WebBusinessZoneOptionLabels.Reset();
    WebZoneSelector->ClearOptions();
    WebBusinessSelector->ClearOptions();
    WebBusinessZoneSelector->ClearOptions();

    WebZoneOptionIds.Add(FString());
    WebZoneOptionLabels.Add(TEXT("选择空间分区"));
    WebZoneSelector->AddOption(WebZoneOptionLabels[0]);
    WebBusinessZoneOptionIds.Add(FString());
    WebBusinessZoneOptionLabels.Add(TEXT("全部空间分区"));
    WebBusinessZoneSelector->AddOption(WebBusinessZoneOptionLabels[0]);
    for (int32 Index = 0; Index < ZoneIds.Num(); ++Index)
    {
        const FString ZoneId = ZoneIds[Index];
        FString Label = ZoneNames.IsValidIndex(Index) && !ZoneNames[Index].IsEmpty()
            ? ZoneNames[Index] : ZoneId;
        if (WebZoneOptionLabels.Contains(Label))
        {
            Label += FString::Printf(TEXT("（%s）"), *ZoneId);
        }
        WebZoneOptionIds.Add(ZoneId);
        WebZoneOptionLabels.Add(Label);
        WebZoneSelector->AddOption(Label);
        WebBusinessZoneOptionIds.Add(ZoneId);
        WebBusinessZoneOptionLabels.Add(Label);
        WebBusinessZoneSelector->AddOption(Label);
    }

    WebBusinessOptionIds.Add(FString());
    WebBusinessOptionLabels.Add(TEXT("选择业务管理"));
    WebBusinessSelector->AddOption(WebBusinessOptionLabels[0]);
    for (int32 Index = 0; Index < BusinessViewIds.Num(); ++Index)
    {
        const FString BusinessViewId = BusinessViewIds[Index];
        FString Label = BusinessViewNames.IsValidIndex(Index)
            && !BusinessViewNames[Index].IsEmpty()
            ? BusinessViewNames[Index] : BusinessViewId;
        if (WebBusinessOptionLabels.Contains(Label))
        {
            Label += FString::Printf(TEXT("（%s）"), *BusinessViewId);
        }
        WebBusinessOptionIds.Add(BusinessViewId);
        WebBusinessOptionLabels.Add(Label);
        WebBusinessSelector->AddOption(Label);
    }

    WebZoneSelector->SetSelectedIndex(0);
    WebBusinessSelector->SetSelectedIndex(0);
    WebBusinessZoneSelector->SetSelectedIndex(0);
    WebZoneSelector->SetIsEnabled(WebZoneOptionIds.Num() > 1);
    WebBusinessSelector->SetIsEnabled(WebBusinessOptionIds.Num() > 1);
    WebBusinessZoneSelector->SetIsEnabled(WebBusinessZoneOptionIds.Num() > 1);
    bRefreshingWebSelectors = false;
}

void UOntoTwinRoamingHUDWidget::AddShortcutRow(
    int32 Index,
    const FString& Key,
    const FString& Description)
{
    if (!HintList) return;
    UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(
        UHorizontalBox::StaticClass(), FName(*FString::Printf(TEXT("ShortcutRow%d"), Index)));
    Row->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UVerticalBoxSlot* RowSlot = HintList->AddChildToVerticalBox(Row);
    RowSlot->SetPadding(FMargin(0.0f, 1.5f));

    USizeBox* KeyBounds = WidgetTree->ConstructWidget<USizeBox>(
        USizeBox::StaticClass(), FName(*FString::Printf(TEXT("ShortcutKeyBounds%d"), Index)));
    KeyBounds->SetWidthOverride(58.0f);
    KeyBounds->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    UBorder* KeyChip = WidgetTree->ConstructWidget<UBorder>(
        UBorder::StaticClass(), FName(*FString::Printf(TEXT("ShortcutKeyChip%d"), Index)));
    KeyChip->SetPadding(FMargin(4.0f, 1.0f));
    KeyChip->SetBrush(FSlateRoundedBoxBrush(KeyFill, 4.0f, KeyStroke, 1.0f));
    KeyChip->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    KeyBounds->AddChild(KeyChip);
    UTextBlock* KeyText = WidgetTree->ConstructWidget<UTextBlock>(
        UTextBlock::StaticClass(), FName(*FString::Printf(TEXT("ShortcutKeyText%d"), Index)));
    KeyText->SetText(FText::FromString(Key));
    KeyText->SetJustification(ETextJustify::Center);
    KeyText->SetColorAndOpacity(PrimaryText);
    KeyText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 8));
    KeyText->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    KeyChip->SetContent(KeyText);
    UHorizontalBoxSlot* KeySlot = Row->AddChildToHorizontalBox(KeyBounds);
    KeySlot->SetPadding(FMargin(0.0f, 0.0f, 7.0f, 0.0f));

    UTextBlock* DescriptionText = WidgetTree->ConstructWidget<UTextBlock>(
        UTextBlock::StaticClass(), FName(*FString::Printf(TEXT("ShortcutDescription%d"), Index)));
    DescriptionText->SetText(FText::FromString(Description));
    DescriptionText->SetColorAndOpacity(SecondaryText);
    DescriptionText->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 8));
    DescriptionText->SetShadowOffset(FVector2D(1.0f, 1.0f));
    DescriptionText->SetShadowColorAndOpacity(TextShadow);
    DescriptionText->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    Row->AddChildToHorizontalBox(DescriptionText);
}

UButton* UOntoTwinRoamingHUDWidget::AddActionButton(
    UVerticalBox* Parent,
    const FName Name,
    const FString& Label)
{
    UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
    Button->SetBackgroundColor(InactiveButton);
    UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(
        UTextBlock::StaticClass(), FName(*(Name.ToString() + TEXT("Text"))));
    Text->SetText(FText::FromString(Label));
    Text->SetColorAndOpacity(PrimaryText);
    Text->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9));
    Button->SetContent(Text);
    if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(Text->Slot))
    {
        ButtonSlot->SetPadding(FMargin(11.0f, 6.0f));
    }
    if (Parent) Parent->AddChildToVerticalBox(Button);
    return Button;
}

void UOntoTwinRoamingHUDWidget::SetInteractionManager(
    UTwinInteractionManagerComponent* InManager)
{
    Manager = InManager;
    if (MinimapWidget) MinimapWidget->SetInteractionManager(Manager);
    RefreshFromManager();
}

void UOntoTwinRoamingHUDWidget::RefreshFromManager()
{
    if (!Manager || !StatusText) return;
    const bool bRoaming = Manager->IsRoamingActive();
    StatusText->SetText(FText::FromString(Manager->GetHudStatusText()));
    if (DrawerTitle)
    {
        DrawerTitle->SetText(FText::FromString(TEXT("运行 Dock")));
    }
    if (RoamingTabButton) RoamingTabButton->SetIsEnabled(bRoaming);
    if (!bRoaming && ActiveDockTab == 3) SetDockTab(1);
    for (UWidget* RoamingOnly : {
        RoamingCharacterRow, RoamingRouteRow, RoamingViewModes, RoamingActions})
    {
        if (RoamingOnly)
        {
            RoamingOnly->SetVisibility(
                bRoaming ? ESlateVisibility::SelfHitTestInvisible
                         : ESlateVisibility::Collapsed);
        }
    }
    RefreshShortcutList();
    RefreshCharacterSelector();
    RefreshRouteSelector();
    RefreshWebSelectors();
    DetailText->SetText(FText::FromString(Manager->GetHudDetailText()));

    const ETwinRoamingCameraMode Mode = Manager->GetCameraMode();
    const bool bEnabled = bRoaming && !Manager->IsCameraTransitioning();
    if (FirstPersonButton)
    {
        FirstPersonButton->SetIsEnabled(bEnabled);
        FirstPersonButton->SetBackgroundColor(
            Mode == ETwinRoamingCameraMode::FirstPerson ? ActiveButton : InactiveButton);
    }
    if (ShoulderButton)
    {
        ShoulderButton->SetIsEnabled(bEnabled);
        ShoulderButton->SetBackgroundColor(
            Mode == ETwinRoamingCameraMode::NearFollow ? ActiveButton : InactiveButton);
    }
    if (GlobalButton)
    {
        GlobalButton->SetIsEnabled(bEnabled);
        GlobalButton->SetBackgroundColor(
            Mode == ETwinRoamingCameraMode::God ? ActiveButton : InactiveButton);
    }
}

void UOntoTwinRoamingHUDWidget::SetInteractionOpen(bool bOpen)
{
    if (bOpen)
    {
        RefreshCharacterSelector();
        RefreshRouteSelector();
        RefreshWebSelectors();
    }
    if (DetailPanel)
    {
        DetailPanel->SetVisibility(
            bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    }
    SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

void UOntoTwinRoamingHUDWidget::SetMinimapTexture(
    UTextureRenderTarget2D* Texture,
    const FIntPoint& CaptureSize)
{
    if (MinimapWidget) MinimapWidget->SetMapTexture(Texture, CaptureSize);
}

void UOntoTwinRoamingHUDWidget::SetMinimapMarker(
    const FVector2D& UV,
    float AngleDegrees,
    bool bOffMap)
{
    if (MinimapWidget) MinimapWidget->SetMarker(UV, AngleDegrees, bOffMap);
}

void UOntoTwinRoamingHUDWidget::SetMinimapTeleportFeedback(
    const FVector2D& UV,
    ETwinMinimapTeleportFeedback Feedback,
    const FString& Message)
{
    if (MinimapWidget)
    {
        MinimapWidget->SetTeleportFeedback(UV, Feedback, Message);
    }
}

void UOntoTwinRoamingHUDWidget::SetMinimapUndoAvailable(bool bAvailable)
{
    if (MinimapWidget) MinimapWidget->SetUndoAvailable(bAvailable);
}

void UOntoTwinRoamingHUDWidget::ClearMinimapTeleportFeedback()
{
    if (MinimapWidget) MinimapWidget->ClearTeleportFeedback();
}

void UOntoTwinRoamingHUDWidget::HideMinimapMarker()
{
    if (MinimapWidget) MinimapWidget->HideMarker();
}

void UOntoTwinRoamingHUDWidget::ClearMinimap()
{
    if (MinimapWidget) MinimapWidget->ClearMap();
}

void UOntoTwinRoamingHUDWidget::SetDockTab(int32 TabIndex)
{
    ActiveDockTab = FMath::Clamp(TabIndex, 1, 3);
    if (ZonePanel) ZonePanel->SetVisibility(
        ActiveDockTab == 1 ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    if (BusinessPanel) BusinessPanel->SetVisibility(
        ActiveDockTab == 2 ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    if (RoamingPanel) RoamingPanel->SetVisibility(
        ActiveDockTab == 3 ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    if (ZoneTabButton) ZoneTabButton->SetBackgroundColor(
        ActiveDockTab == 1 ? ActiveButton : InactiveButton);
    if (BusinessTabButton) BusinessTabButton->SetBackgroundColor(
        ActiveDockTab == 2 ? ActiveButton : InactiveButton);
    if (RoamingTabButton) RoamingTabButton->SetBackgroundColor(
        ActiveDockTab == 3 ? ActiveButton : InactiveButton);
}

void UOntoTwinRoamingHUDWidget::OnCycleSkin() { if (Manager) Manager->CycleSkin(); }
void UOntoTwinRoamingHUDWidget::OnResumeRoute() { if (Manager) Manager->ResumeRoute(); }
void UOntoTwinRoamingHUDWidget::OnRestartRoute() { if (Manager) Manager->RestartRoute(); }
void UOntoTwinRoamingHUDWidget::OnReloadCharacter() { if (Manager) Manager->ApplyPendingReload(); }
void UOntoTwinRoamingHUDWidget::OnHomeTab()
{
    if (Manager) Manager->ActivateRuntimeHome();
}

void UOntoTwinRoamingHUDWidget::OnZoneTab() { SetDockTab(1); }
void UOntoTwinRoamingHUDWidget::OnBusinessTab() { SetDockTab(2); }
void UOntoTwinRoamingHUDWidget::OnRoamingTab() { SetDockTab(3); }

void UOntoTwinRoamingHUDWidget::OnWebZoneSelected(
    FString SelectedItem,
    ESelectInfo::Type SelectionType)
{
    (void)SelectionType;
    if (bRefreshingWebSelectors) return;
    const int32 Index = WebZoneOptionLabels.IndexOfByKey(SelectedItem);
    if (Manager && WebZoneOptionIds.IsValidIndex(Index)
        && !WebZoneOptionIds[Index].IsEmpty())
    {
        Manager->OpenWebZone(WebZoneOptionIds[Index]);
    }
}

void UOntoTwinRoamingHUDWidget::OnWebBusinessSelected(
    FString SelectedItem,
    ESelectInfo::Type SelectionType)
{
    (void)SelectionType;
    if (bRefreshingWebSelectors) return;
    if (!Manager || !WebBusinessSelector || !WebBusinessZoneSelector) return;
    const int32 BusinessIndex = WebBusinessOptionLabels.IndexOfByKey(SelectedItem);
    const int32 ZoneIndex = WebBusinessZoneOptionLabels.IndexOfByKey(
        WebBusinessZoneSelector->GetSelectedOption());
    if (!WebBusinessOptionIds.IsValidIndex(BusinessIndex)
        || WebBusinessOptionIds[BusinessIndex].IsEmpty()) return;

    const FString ZoneId = WebBusinessZoneOptionIds.IsValidIndex(ZoneIndex)
        ? WebBusinessZoneOptionIds[ZoneIndex]
        : FString();
    Manager->OpenWebBusinessView(WebBusinessOptionIds[BusinessIndex], ZoneId);
}

void UOntoTwinRoamingHUDWidget::OnWebBusinessScopeSelected(
    FString SelectedItem,
    ESelectInfo::Type SelectionType)
{
    (void)SelectedItem;
    (void)SelectionType;
    if (bRefreshingWebSelectors || !WebBusinessSelector) return;
    const FString CurrentBusiness = WebBusinessSelector->GetSelectedOption();
    const int32 BusinessIndex = WebBusinessOptionLabels.IndexOfByKey(CurrentBusiness);
    if (WebBusinessOptionIds.IsValidIndex(BusinessIndex)
        && !WebBusinessOptionIds[BusinessIndex].IsEmpty())
    {
        OnWebBusinessSelected(CurrentBusiness, ESelectInfo::Direct);
    }
}
void UOntoTwinRoamingHUDWidget::OnFirstPerson()
{
    if (Manager) Manager->SetCameraMode(ETwinRoamingCameraMode::FirstPerson);
}
void UOntoTwinRoamingHUDWidget::OnShoulder()
{
    if (Manager) Manager->SetCameraMode(ETwinRoamingCameraMode::NearFollow);
}
void UOntoTwinRoamingHUDWidget::OnGlobal()
{
    if (Manager) Manager->SetCameraMode(ETwinRoamingCameraMode::God);
}

void UOntoTwinRoamingHUDWidget::OnRouteSelected(
    FString SelectedItem,
    ESelectInfo::Type SelectionType)
{
    (void)SelectionType;
    if (bRefreshingRouteSelector || !Manager) return;
    const int32 Index = RouteOptionLabels.IndexOfByKey(SelectedItem);
    if (!RouteOptionIds.IsValidIndex(Index) || RouteOptionIds[Index].IsEmpty()) return;
    if (!Manager->SelectRuntimeRoute(RouteOptionIds[Index]))
    {
        RouteSignature.Reset();
        RefreshRouteSelector();
    }
}

void UOntoTwinRoamingHUDWidget::OnCharacterSelected(
    FString SelectedItem,
    ESelectInfo::Type SelectionType)
{
    (void)SelectionType;
    if (bRefreshingCharacterSelector || !Manager) return;
    const int32 Index = CharacterOptionLabels.IndexOfByKey(SelectedItem);
    if (!CharacterOptionIds.IsValidIndex(Index)
        || CharacterOptionIds[Index].IsEmpty()) return;
    if (!Manager->SelectRuntimeCharacter(CharacterOptionIds[Index]))
    {
        CharacterSignature.Reset();
        RefreshCharacterSelector();
    }
}
