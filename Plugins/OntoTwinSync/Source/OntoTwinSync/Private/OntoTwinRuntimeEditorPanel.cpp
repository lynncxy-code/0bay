#include "OntoTwinRuntimeEditorPanel.h"

#include "TwinSceneManager.h"
#include "UI/OntoTwinGlassTheme.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/GridPanel.h"
#include "Components/GridSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformApplicationMisc.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateColor.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SWidget.h"

namespace
{
const FLinearColor PanelBackground(0.078f, 0.078f, 0.078f, 0.92f);
const FLinearColor PrimaryText(0.96f, 0.96f, 0.96f, 1.0f);
const FLinearColor SecondaryText(0.64f, 0.64f, 0.64f, 1.0f);
const FLinearColor MutedText(0.46f, 0.46f, 0.46f, 1.0f);
const FLinearColor DividerColor(1.0f, 1.0f, 1.0f, 0.10f);
const FLinearColor HoverBackground(1.0f, 1.0f, 1.0f, 0.10f);
const FLinearColor PressedBackground(1.0f, 1.0f, 1.0f, 0.16f);
const FLinearColor ReadyColor(0.10f, 0.62f, 0.39f, 1.0f);
const FLinearColor WarningColor(0.83f, 0.56f, 0.20f, 1.0f);
const FLinearColor ErrorColor(0.78f, 0.24f, 0.20f, 1.0f);

FButtonStyle BuildButtonStyle(const FButtonStyle& BaseStyle, bool bPrimary)
{
    FButtonStyle Style = BaseStyle;
    const FLinearColor NormalColor = bPrimary ? PrimaryText : FLinearColor(1.0f, 1.0f, 1.0f, 0.04f);
    const FLinearColor HoverColor = bPrimary ? FLinearColor::White : HoverBackground;
    const FLinearColor PressedColor = bPrimary ? FLinearColor(0.82f, 0.82f, 0.82f, 1.0f) : PressedBackground;

    Style.SetNormal(FSlateRoundedBoxBrush(NormalColor, 6.0f));
    Style.SetHovered(FSlateRoundedBoxBrush(HoverColor, 6.0f));
    Style.SetPressed(FSlateRoundedBoxBrush(PressedColor, 6.0f));
    Style.SetDisabled(FSlateRoundedBoxBrush(FLinearColor(1.0f, 1.0f, 1.0f, 0.035f), 6.0f));
    Style.SetNormalPadding(FMargin(8.0f, 5.0f));
    Style.SetPressedPadding(FMargin(8.0f, 6.0f, 8.0f, 4.0f));
    return Style;
}

FButtonStyle BuildCopyValueButtonStyle(const FButtonStyle& BaseStyle)
{
    FButtonStyle Style = BaseStyle;
    Style.SetNormal(FSlateRoundedBoxBrush(FLinearColor::Transparent, 4.0f));
    Style.SetHovered(FSlateRoundedBoxBrush(HoverBackground, 4.0f));
    Style.SetPressed(FSlateRoundedBoxBrush(PressedBackground, 4.0f));
    Style.SetDisabled(FSlateRoundedBoxBrush(FLinearColor::Transparent, 4.0f));
    Style.SetNormalPadding(FMargin(4.0f, 1.0f));
    Style.SetPressedPadding(FMargin(4.0f, 2.0f, 4.0f, 0.0f));
    return Style;
}

FButtonStyle BuildTabButtonStyle(const FButtonStyle& BaseStyle, bool bActive)
{
    FButtonStyle Style = BaseStyle;
    const FLinearColor ActiveFill(0.96f, 0.96f, 0.96f, 0.96f);
    const FLinearColor InactiveFill(1.0f, 1.0f, 1.0f, 0.035f);
    Style.SetNormal(FSlateRoundedBoxBrush(bActive ? ActiveFill : InactiveFill, 6.0f));
    Style.SetHovered(FSlateRoundedBoxBrush(
        bActive ? FLinearColor::White : HoverBackground, 6.0f));
    Style.SetPressed(FSlateRoundedBoxBrush(
        bActive ? FLinearColor(0.84f, 0.84f, 0.84f, 1.0f) : PressedBackground, 6.0f));
    Style.SetDisabled(FSlateRoundedBoxBrush(InactiveFill, 6.0f));
    Style.SetNormalPadding(FMargin(8.0f, 6.0f));
    Style.SetPressedPadding(FMargin(8.0f, 7.0f, 8.0f, 5.0f));
    return Style;
}

FComboBoxStyle BuildCompactSelectorStyle()
{
    FComboBoxStyle Style = FCoreStyle::Get().GetWidgetStyle<FComboBoxStyle>(TEXT("ComboBox"));
    FComboButtonStyle ComboButton = Style.ComboButtonStyle;
    FButtonStyle ButtonStyle = ComboButton.ButtonStyle;
    ButtonStyle
        .SetNormal(FSlateRoundedBoxBrush(
            FLinearColor(1.0f, 1.0f, 1.0f, 0.055f), 6.0f,
            FLinearColor(1.0f, 1.0f, 1.0f, 0.16f), 1.0f))
        .SetHovered(FSlateRoundedBoxBrush(
            FLinearColor(1.0f, 1.0f, 1.0f, 0.10f), 6.0f,
            FLinearColor(1.0f, 1.0f, 1.0f, 0.26f), 1.0f))
        .SetPressed(FSlateRoundedBoxBrush(
            FLinearColor(1.0f, 1.0f, 1.0f, 0.15f), 6.0f,
            FLinearColor(1.0f, 1.0f, 1.0f, 0.32f), 1.0f))
        .SetDisabled(FSlateRoundedBoxBrush(
            FLinearColor(1.0f, 1.0f, 1.0f, 0.025f), 6.0f,
            FLinearColor(1.0f, 1.0f, 1.0f, 0.08f), 1.0f));
    ComboButton.SetButtonStyle(ButtonStyle)
        .SetMenuBorderBrush(FSlateRoundedBoxBrush(
            FLinearColor(0.055f, 0.055f, 0.055f, 0.98f), 6.0f,
            FLinearColor(1.0f, 1.0f, 1.0f, 0.18f), 1.0f))
        .SetMenuBorderPadding(FMargin(4.0f))
        .SetDownArrowPadding(FMargin(6.0f, 0.0f));
    return Style.SetComboButtonStyle(ComboButton).SetMenuRowPadding(FMargin(7.0f, 4.0f));
}

FTableRowStyle BuildCompactSelectorRowStyle()
{
    FTableRowStyle Style = FCoreStyle::Get().GetWidgetStyle<FTableRowStyle>(TEXT("ComboBox.Row"));
    const FSlateRoundedBoxBrush Clear(FLinearColor::Transparent, 4.0f);
    const FSlateRoundedBoxBrush Hovered(FLinearColor(1.0f, 1.0f, 1.0f, 0.10f), 4.0f);
    const FSlateRoundedBoxBrush Selected(FLinearColor(1.0f, 1.0f, 1.0f, 0.16f), 4.0f);
    return Style
        .SetSelectorFocusedBrush(Selected)
        .SetActiveHoveredBrush(Selected)
        .SetActiveBrush(Selected)
        .SetInactiveHoveredBrush(Selected)
        .SetInactiveBrush(Selected)
        .SetEvenRowBackgroundHoveredBrush(Hovered)
        .SetEvenRowBackgroundBrush(Clear)
        .SetOddRowBackgroundHoveredBrush(Hovered)
        .SetOddRowBackgroundBrush(Clear)
        .SetTextColor(FSlateColor(PrimaryText))
        .SetSelectedTextColor(FSlateColor(PrimaryText));
}

void SetTextColor(UTextBlock* Text, const FLinearColor& Color)
{
    if (Text)
    {
        Text->SetColorAndOpacity(FSlateColor(Color));
    }
}
}

void UOntoTwinRuntimeEditorPanel::SetSceneManager(ATwinSceneManager* InSceneManager)
{
    SceneManager = InSceneManager;
    RefreshFromManager();
}

TSharedRef<SWidget> UOntoTwinRuntimeEditorPanel::RebuildWidget()
{
    if (!WidgetTree)
    {
        WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
    }
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        BuildDefaultLayout();
    }

    return Super::RebuildWidget();
}

void UOntoTwinRuntimeEditorPanel::NativeConstruct()
{
    Super::NativeConstruct();
    SetIsFocusable(true);

    if (AccessActionButton)
    {
        AccessActionButton->OnClicked.RemoveAll(this);
        AccessActionButton->OnClicked.AddDynamic(this, &UOntoTwinRuntimeEditorPanel::HandleAccessActionClicked);
    }
    if (DisplayNameCopyButton)
    {
        DisplayNameCopyButton->OnClicked.RemoveAll(this);
        DisplayNameCopyButton->OnClicked.AddDynamic(
            this, &UOntoTwinRuntimeEditorPanel::HandleDisplayNameCopyClicked);
    }
    if (TypeNameCopyButton)
    {
        TypeNameCopyButton->OnClicked.RemoveAll(this);
        TypeNameCopyButton->OnClicked.AddDynamic(
            this, &UOntoTwinRuntimeEditorPanel::HandleTypeNameCopyClicked);
    }
    if (CloseButton)
    {
        CloseButton->OnClicked.RemoveAll(this);
        CloseButton->OnClicked.AddDynamic(this, &UOntoTwinRuntimeEditorPanel::HandleCloseClicked);
    }
    if (UndoButton)
    {
        UndoButton->OnClicked.RemoveAll(this);
        UndoButton->OnClicked.AddDynamic(this, &UOntoTwinRuntimeEditorPanel::HandleUndoClicked);
    }
    if (RedoButton)
    {
        RedoButton->OnClicked.RemoveAll(this);
        RedoButton->OnClicked.AddDynamic(this, &UOntoTwinRuntimeEditorPanel::HandleRedoClicked);
    }
    if (SceneTabButton)
    {
        SceneTabButton->OnClicked.RemoveAll(this);
        SceneTabButton->OnClicked.AddDynamic(this, &UOntoTwinRuntimeEditorPanel::HandleSceneTabClicked);
    }
    if (BusinessTabButton)
    {
        BusinessTabButton->OnClicked.RemoveAll(this);
        BusinessTabButton->OnClicked.AddDynamic(this, &UOntoTwinRuntimeEditorPanel::HandleBusinessTabClicked);
    }
    if (BusinessSelector)
    {
        BusinessSelector->OnSelectionChanged.RemoveAll(this);
        BusinessSelector->OnSelectionChanged.AddDynamic(this, &UOntoTwinRuntimeEditorPanel::HandleBusinessSelected);
    }
    if (CreateBusinessButton)
    {
        CreateBusinessButton->OnClicked.RemoveAll(this);
        CreateBusinessButton->OnClicked.AddDynamic(this, &UOntoTwinRuntimeEditorPanel::HandleCreateBusinessClicked);
    }
    if (RemoveButton)
    {
        RemoveButton->OnClicked.RemoveAll(this);
        RemoveButton->OnClicked.AddDynamic(this, &UOntoTwinRuntimeEditorPanel::HandleRemoveClicked);
    }
    if (PendingToggleButton)
    {
        PendingToggleButton->OnClicked.RemoveAll(this);
        PendingToggleButton->OnClicked.AddDynamic(this, &UOntoTwinRuntimeEditorPanel::HandlePendingToggleClicked);
    }
    if (SaveButton)
    {
        SaveButton->OnClicked.RemoveAll(this);
        SaveButton->OnClicked.AddDynamic(this, &UOntoTwinRuntimeEditorPanel::HandleSaveClicked);
    }
    if (CancelButton)
    {
        CancelButton->OnClicked.RemoveAll(this);
        CancelButton->OnClicked.AddDynamic(this, &UOntoTwinRuntimeEditorPanel::HandleCancelClicked);
    }
    if (WallSnapCheckBox)
    {
        WallSnapCheckBox->OnCheckStateChanged.RemoveAll(this);
        WallSnapCheckBox->OnCheckStateChanged.AddDynamic(this, &UOntoTwinRuntimeEditorPanel::HandleWallSnapChanged);
    }
    if (GridSnapCheckBox)
    {
        GridSnapCheckBox->OnCheckStateChanged.RemoveAll(this);
        GridSnapCheckBox->OnCheckStateChanged.AddDynamic(this, &UOntoTwinRuntimeEditorPanel::HandleGridSnapChanged);
    }
    if (ConfirmationPrimaryButton)
    {
        ConfirmationPrimaryButton->OnClicked.RemoveAll(this);
        ConfirmationPrimaryButton->OnClicked.AddDynamic(this, &UOntoTwinRuntimeEditorPanel::HandleConfirmationPrimaryClicked);
    }
    if (ConfirmationSecondaryButton)
    {
        ConfirmationSecondaryButton->OnClicked.RemoveAll(this);
        ConfirmationSecondaryButton->OnClicked.AddDynamic(this, &UOntoTwinRuntimeEditorPanel::HandleConfirmationSecondaryClicked);
    }
    if (ConfirmationContinueButton)
    {
        ConfirmationContinueButton->OnClicked.RemoveAll(this);
        ConfirmationContinueButton->OnClicked.AddDynamic(this, &UOntoTwinRuntimeEditorPanel::HandleConfirmationContinueClicked);
    }

    RefreshFromManager();
}

FReply UOntoTwinRuntimeEditorPanel::NativeOnKeyDown(
    const FGeometry& InGeometry,
    const FKeyEvent& InKeyEvent)
{
    if (InKeyEvent.GetKey() == EKeys::Escape && IsConfirmationOpen())
    {
        if (SceneManager) SceneManager->CancelRuntimeEditExitRequest();
        HideConfirmation();
        return FReply::Handled();
    }
    return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UOntoTwinRuntimeEditorPanel::NativeDestruct()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(ToastTimerHandle);
    }
    Super::NativeDestruct();
}

void UOntoTwinRuntimeEditorPanel::BuildDefaultLayout()
{
    if (!WidgetTree) return;

    UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RuntimeEditorRoot"));
    Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    WidgetTree->RootWidget = Root;

    PanelBounds = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PanelBounds"));
    PanelBounds->SetWidthOverride(380.0f);
    PanelBounds->SetMinDesiredHeight(360.0f);
    PanelBounds->SetMaxDesiredHeight(640.0f);

    PanelBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PanelBorder"));
    PanelBorder->SetPadding(FMargin(14.0f));
    PanelBorder->SetBrush(FSlateRoundedBoxBrush(PanelBackground, 6.0f));
    PanelBounds->AddChild(PanelBorder);

    UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(PanelBounds);
    if (PanelSlot)
    {
        PanelSlot->SetAnchors(FAnchors(0.0f, 0.0f));
        PanelSlot->SetPosition(FVector2D(20.0f, 20.0f));
        PanelSlot->SetAutoSize(true);
    }

    UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PanelStack"));
    PanelBorder->SetContent(Stack);

    UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Header"));
    UVerticalBoxSlot* HeaderSlot = Stack->AddChildToVerticalBox(Header);
    HeaderSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));

    UTextBlock* TitleText = CreateText(TEXT("TitleText"), TEXT("运行时编辑器"), 14, PrimaryText, true);
    UHorizontalBoxSlot* TitleSlot = Header->AddChildToHorizontalBox(TitleText);
    TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    TitleSlot->SetVerticalAlignment(VAlign_Center);

    HeaderStateText = CreateText(TEXT("HeaderStateText"), TEXT("编辑中"), 11, SecondaryText, true);
    UHorizontalBoxSlot* HeaderStateSlot = Header->AddChildToHorizontalBox(HeaderStateText);
    HeaderStateSlot->SetPadding(FMargin(8.0f, 0.0f, 6.0f, 0.0f));
    HeaderStateSlot->SetVerticalAlignment(VAlign_Center);

    UTextBlock* UndoLabel = nullptr;
    UndoButton = CreateButton(TEXT("UndoButton"), TEXT("撤销"), UndoLabel, false);
    UndoButton->SetToolTipText(FText::FromString(TEXT("撤销上一步（Ctrl+Z）")));
    UHorizontalBoxSlot* UndoSlot = Header->AddChildToHorizontalBox(UndoButton);
    UndoSlot->SetPadding(FMargin(0.0f, 0.0f, 4.0f, 0.0f));
    UndoSlot->SetVerticalAlignment(VAlign_Center);

    UTextBlock* RedoLabel = nullptr;
    RedoButton = CreateButton(TEXT("RedoButton"), TEXT("重做"), RedoLabel, false);
    RedoButton->SetToolTipText(FText::FromString(TEXT("重做（Ctrl+Y）")));
    UHorizontalBoxSlot* RedoSlot = Header->AddChildToHorizontalBox(RedoButton);
    RedoSlot->SetPadding(FMargin(0.0f, 0.0f, 6.0f, 0.0f));
    RedoSlot->SetVerticalAlignment(VAlign_Center);

    UTextBlock* CloseLabel = nullptr;
    CloseButton = CreateButton(TEXT("CloseButton"), TEXT("×"), CloseLabel, false);
    CloseButton->SetToolTipText(FText::FromString(TEXT("退出编辑器")));
    USizeBox* CloseBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("CloseBox"));
    CloseBox->SetWidthOverride(30.0f);
    CloseBox->SetHeightOverride(28.0f);
    CloseBox->AddChild(CloseButton);
    UHorizontalBoxSlot* CloseSlot = Header->AddChildToHorizontalBox(CloseBox);
    CloseSlot->SetVerticalAlignment(VAlign_Center);

    UBorder* Divider = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("HeaderDivider"));
    Divider->SetBrushColor(DividerColor);
    USizeBox* DividerBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("HeaderDividerBox"));
    DividerBox->SetHeightOverride(1.0f);
    DividerBox->AddChild(Divider);
    UVerticalBoxSlot* DividerSlot = Stack->AddChildToVerticalBox(DividerBox);
    DividerSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));

    UHorizontalBox* ModeTabs = WidgetTree->ConstructWidget<UHorizontalBox>(
        UHorizontalBox::StaticClass(), TEXT("ModeTabs"));
    UVerticalBoxSlot* ModeTabsSlot = Stack->AddChildToVerticalBox(ModeTabs);
    ModeTabsSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
    SceneTabButton = CreateButton(
        TEXT("SceneTabButton"), TEXT("场景编辑"), SceneTabLabel, false);
    BusinessTabButton = CreateButton(
        TEXT("BusinessTabButton"), TEXT("业务编辑"), BusinessTabLabel, false);
    for (UButton* TabButton : {SceneTabButton, BusinessTabButton})
    {
        UHorizontalBoxSlot* TabSlot = ModeTabs->AddChildToHorizontalBox(TabButton);
        TabSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        TabSlot->SetPadding(FMargin(0.0f, 0.0f, 4.0f, 0.0f));
    }

    UScrollBox* BodyScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("BodyScroll"));
    BodyScroll->SetScrollBarVisibility(ESlateVisibility::Visible);
    UVerticalBoxSlot* BodyScrollSlot = Stack->AddChildToVerticalBox(BodyScroll);
    BodyScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

    UVerticalBox* Body = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Body"));
    BodyScroll->AddChild(Body);

    SceneContent = WidgetTree->ConstructWidget<UVerticalBox>(
        UVerticalBox::StaticClass(), TEXT("SceneContent"));
    Body->AddChildToVerticalBox(SceneContent);

    UTextBlock* DeviceLabel = CreateText(TEXT("DeviceLabel"), TEXT("选中对象"), 10, MutedText, true);
    SceneContent->AddChildToVerticalBox(DeviceLabel);

    UGridPanel* IdentityGrid = WidgetTree->ConstructWidget<UGridPanel>(UGridPanel::StaticClass(), TEXT("IdentityGrid"));
    IdentityGrid->SetColumnFill(1, 1.0f);
    UVerticalBoxSlot* IdentitySlot = SceneContent->AddChildToVerticalBox(IdentityGrid);
    IdentitySlot->SetPadding(FMargin(0.0f, 4.0f, 6.0f, 4.0f));

    UTextBlock* NameLabel = CreateText(TEXT("NameLabel"), TEXT("实例名称"), 11, MutedText, true);
    UGridSlot* NameLabelSlot = IdentityGrid->AddChildToGrid(NameLabel, 0, 0);
    NameLabelSlot->SetPadding(FMargin(0.0f, 2.0f, 12.0f, 2.0f));
    DisplayNameCopyButton = WidgetTree->ConstructWidget<UButton>(
        UButton::StaticClass(), TEXT("DisplayNameCopyButton"));
    DisplayNameCopyButton->SetStyle(BuildCopyValueButtonStyle(DisplayNameCopyButton->GetStyle()));
    DisplayNameCopyButton->SetToolTipText(FText::FromString(TEXT("点击复制完整实例名称")));
    DisplayNameText = CreateText(TEXT("DisplayNameText"), TEXT("未选择实例"), 12, PrimaryText, true);
    DisplayNameText->SetAutoWrapText(true);
    DisplayNameText->SetWrapTextAt(250.0f);
    DisplayNameText->SetJustification(ETextJustify::Right);
    DisplayNameCopyButton->SetContent(DisplayNameText);
    UGridSlot* DisplayNameSlot = IdentityGrid->AddChildToGrid(DisplayNameCopyButton, 0, 1);
    DisplayNameSlot->SetHorizontalAlignment(HAlign_Fill);

    UTextBlock* TypeLabel = CreateText(TEXT("TypeLabel"), TEXT("类型名称"), 11, MutedText, true);
    UGridSlot* TypeLabelSlot = IdentityGrid->AddChildToGrid(TypeLabel, 1, 0);
    TypeLabelSlot->SetPadding(FMargin(0.0f, 2.0f, 12.0f, 2.0f));
    TypeNameCopyButton = WidgetTree->ConstructWidget<UButton>(
        UButton::StaticClass(), TEXT("TypeNameCopyButton"));
    TypeNameCopyButton->SetStyle(BuildCopyValueButtonStyle(TypeNameCopyButton->GetStyle()));
    TypeNameCopyButton->SetToolTipText(FText::FromString(TEXT("点击复制完整类型名称")));
    TypeNameText = CreateText(TEXT("TypeNameText"), TEXT("-"), 11, SecondaryText, true);
    TypeNameText->SetAutoWrapText(true);
    TypeNameText->SetWrapTextAt(250.0f);
    TypeNameText->SetJustification(ETextJustify::Right);
    TypeNameCopyButton->SetContent(TypeNameText);
    UGridSlot* TypeNameSlot = IdentityGrid->AddChildToGrid(TypeNameCopyButton, 1, 1);
    TypeNameSlot->SetHorizontalAlignment(HAlign_Fill);

    UTextBlock* IdLabel = CreateText(TEXT("IdLabel"), TEXT("实例 ID"), 11, MutedText, true);
    UGridSlot* IdLabelSlot = IdentityGrid->AddChildToGrid(IdLabel, 2, 0);
    IdLabelSlot->SetPadding(FMargin(0.0f, 2.0f, 12.0f, 2.0f));
    InstanceIdText = CreateText(TEXT("InstanceIdText"), TEXT("-"), 11, SecondaryText);
    UGridSlot* InstanceIdSlot = IdentityGrid->AddChildToGrid(InstanceIdText, 2, 1);
    InstanceIdSlot->SetPadding(FMargin(0.0f, 2.0f));
    InstanceIdSlot->SetHorizontalAlignment(HAlign_Right);

    UTextBlock* RemoveLabel = nullptr;
    RemoveButton = CreateButton(TEXT("RemoveButton"), TEXT("从场景移除"), RemoveLabel, false);
    RemoveButton->SetToolTipText(FText::FromString(TEXT("立即隐藏所选实例，保存后可在 Web 端重新加载")));
    UVerticalBoxSlot* RemoveSlot = SceneContent->AddChildToVerticalBox(RemoveButton);
    RemoveSlot->SetPadding(FMargin(0.0f, 0.0f, 6.0f, 10.0f));

    UGridPanel* TransformGrid = WidgetTree->ConstructWidget<UGridPanel>(UGridPanel::StaticClass(), TEXT("TransformGrid"));
    TransformGrid->SetColumnFill(1, 1.0f);
    TransformGrid->SetColumnFill(3, 1.0f);
    UVerticalBoxSlot* TransformGridSlot = SceneContent->AddChildToVerticalBox(TransformGrid);
    TransformGridSlot->SetPadding(FMargin(0.0f, 0.0f, 6.0f, 10.0f));

    auto AddTransformCell = [this, TransformGrid](const FName LabelName, const FString& Label,
        UTextBlock*& ValueText, const FName ValueName, int32 Row, int32 Column)
    {
        UTextBlock* LabelText = CreateText(LabelName, Label, 11, MutedText, true);
        UGridSlot* LabelSlot = TransformGrid->AddChildToGrid(LabelText, Row, Column);
        LabelSlot->SetPadding(FMargin(Column == 0 ? 0.0f : 12.0f, 1.0f, 6.0f, 1.0f));
        LabelSlot->SetVerticalAlignment(VAlign_Center);

        ValueText = CreateText(ValueName, TEXT("-"), 12, PrimaryText);
        UGridSlot* ValueSlot = TransformGrid->AddChildToGrid(ValueText, Row, Column + 1);
        ValueSlot->SetPadding(FMargin(0.0f, 1.0f));
        ValueSlot->SetHorizontalAlignment(HAlign_Right);
        ValueSlot->SetVerticalAlignment(VAlign_Center);
    };

    AddTransformCell(TEXT("XLabel"), TEXT("X"), XValueText, TEXT("XValue"), 0, 0);
    AddTransformCell(TEXT("YLabel"), TEXT("Y"), YValueText, TEXT("YValue"), 0, 2);
    AddTransformCell(TEXT("ZLabel"), TEXT("Z"), ZValueText, TEXT("ZValue"), 1, 0);
    AddTransformCell(TEXT("YawLabel"), TEXT("偏航"), YawValueText, TEXT("YawValue"), 1, 2);
    YawLabelText = Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("YawLabel")));

    UTextBlock* AccessLabel = CreateText(TEXT("AccessLabel"), TEXT("数据集访问"), 10, MutedText, true);
    SceneContent->AddChildToVerticalBox(AccessLabel);

    UHorizontalBox* AccessRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("AccessRow"));
    UVerticalBoxSlot* AccessRowSlot = SceneContent->AddChildToVerticalBox(AccessRow);
    AccessRowSlot->SetPadding(FMargin(0.0f, 2.0f, 6.0f, 9.0f));

    AccessStatusDot = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("AccessStatusDot"));
    AccessStatusDot->SetBrush(FSlateRoundedBoxBrush(MutedText, 3.0f));
    USizeBox* AccessDotBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("AccessDotBox"));
    AccessDotBox->SetWidthOverride(6.0f);
    AccessDotBox->SetHeightOverride(6.0f);
    AccessDotBox->AddChild(AccessStatusDot);
    UHorizontalBoxSlot* AccessDotSlot = AccessRow->AddChildToHorizontalBox(AccessDotBox);
    AccessDotSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
    AccessDotSlot->SetVerticalAlignment(VAlign_Center);

    AccessStatusText = CreateText(TEXT("AccessStatusText"), TEXT("正在检查"), 12, PrimaryText);
    UHorizontalBoxSlot* AccessTextSlot = AccessRow->AddChildToHorizontalBox(AccessStatusText);
    AccessTextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    AccessTextSlot->SetVerticalAlignment(VAlign_Center);

    AccessActionButton = CreateButton(TEXT("AccessActionButton"), TEXT("检查中..."), AccessActionLabel, false);
    UHorizontalBoxSlot* AccessActionSlot = AccessRow->AddChildToHorizontalBox(AccessActionButton);
    AccessActionSlot->SetPadding(FMargin(8.0f, 0.0f, 0.0f, 0.0f));
    AccessActionSlot->SetVerticalAlignment(VAlign_Center);

    UHorizontalBox* Toggles = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SnapToggles"));
    UVerticalBoxSlot* TogglesSlot = SceneContent->AddChildToVerticalBox(Toggles);
    TogglesSlot->SetPadding(FMargin(0.0f, 0.0f, 6.0f, 10.0f));

    auto AddSnapToggle = [this, Toggles](const FName CheckName, const FString& Label, UCheckBox*& OutCheckBox)
    {
        OutCheckBox = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), CheckName);
        UTextBlock* ToggleLabel = CreateText(FName(*(CheckName.ToString() + TEXT("Label"))), Label, 11, SecondaryText);
        OutCheckBox->SetContent(ToggleLabel);
        UHorizontalBoxSlot* ToggleSlot = Toggles->AddChildToHorizontalBox(OutCheckBox);
        ToggleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        ToggleSlot->SetVerticalAlignment(VAlign_Center);
    };

    AddSnapToggle(TEXT("WallSnapCheckBox"), TEXT("靠墙吸附"), WallSnapCheckBox);
    AddSnapToggle(TEXT("GridSnapCheckBox"), TEXT("网格吸附"), GridSnapCheckBox);

    BusinessContent = WidgetTree->ConstructWidget<UVerticalBox>(
        UVerticalBox::StaticClass(), TEXT("BusinessContent"));
    BusinessContent->SetVisibility(ESlateVisibility::Collapsed);
    Body->AddChildToVerticalBox(BusinessContent);

    BusinessSelectionText = CreateText(
        TEXT("BusinessSelectionText"), TEXT("请先在场景中选择对象"), 12, PrimaryText, true);
    BusinessSelectionText->SetAutoWrapText(true);
    UVerticalBoxSlot* BusinessSelectionSlot =
        BusinessContent->AddChildToVerticalBox(BusinessSelectionText);
    BusinessSelectionSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));

    BusinessSelector = WidgetTree->ConstructWidget<UComboBoxString>(
        UComboBoxString::StaticClass(), TEXT("BusinessSelector"));
    BusinessSelector->SetWidgetStyle(BuildCompactSelectorStyle());
    BusinessSelector->SetItemStyle(BuildCompactSelectorRowStyle());
    BusinessSelector->SetContentPadding(FMargin(8.0f, 4.0f));
    BusinessSelector->SetMaxListHeight(240.0f);
    BusinessSelector->SetToolTipText(FText::FromString(
        TEXT("选择业务后切换所选对象的业务归属。")));
    BusinessSelector->OnGenerateWidgetEvent.BindDynamic(
        this, &UOntoTwinRuntimeEditorPanel::GenerateBusinessOptionWidget);
    UVerticalBoxSlot* BusinessSelectorSlot =
        BusinessContent->AddChildToVerticalBox(BusinessSelector);
    BusinessSelectorSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));

    UHorizontalBox* CreateBusinessRow = WidgetTree->ConstructWidget<UHorizontalBox>(
        UHorizontalBox::StaticClass(), TEXT("CreateBusinessRow"));
    UVerticalBoxSlot* CreateBusinessRowSlot =
        BusinessContent->AddChildToVerticalBox(CreateBusinessRow);
    CreateBusinessRowSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));
    BusinessNameInput = WidgetTree->ConstructWidget<UEditableTextBox>(
        UEditableTextBox::StaticClass(), TEXT("BusinessNameInput"));
    BusinessNameInput->WidgetStyle = FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>(
        TEXT("NormalEditableTextBox"));
    BusinessNameInput->WidgetStyle
        .SetFont(FOntoTwinGlassTheme::Font(10.0f))
        .SetPadding(FMargin(8.0f, 4.0f));
    BusinessNameInput->SetHintText(FText::FromString(TEXT("新建业务名称")));
    BusinessNameInput->SetToolTipText(FText::FromString(
        TEXT("输入名称后，将使用当前所选对象创建业务。保存全部修改后同步到 Web。")));
    UHorizontalBoxSlot* BusinessNameSlot =
        CreateBusinessRow->AddChildToHorizontalBox(BusinessNameInput);
    BusinessNameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    BusinessNameSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
    UTextBlock* CreateBusinessButtonLabel = nullptr;
    CreateBusinessButton = CreateButton(
        TEXT("CreateBusinessButton"), TEXT("创建"), CreateBusinessButtonLabel, false);
    CreateBusinessButton->SetToolTipText(FText::FromString(
        TEXT("使用当前所选对象创建业务；创建后仍需保存全部修改。")));
    CreateBusinessRow->AddChildToHorizontalBox(CreateBusinessButton);

    PendingToggleButton = CreateButton(TEXT("PendingToggleButton"), TEXT("待保存修改（0）"), PendingToggleLabel, false);
    UVerticalBoxSlot* PendingToggleSlot = Body->AddChildToVerticalBox(PendingToggleButton);
    PendingToggleSlot->SetPadding(FMargin(0.0f, 0.0f, 6.0f, 4.0f));

    PendingList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PendingList"));
    PendingList->SetVisibility(ESlateVisibility::Collapsed);
    UVerticalBoxSlot* PendingListSlot = Body->AddChildToVerticalBox(PendingList);
    PendingListSlot->SetPadding(FMargin(8.0f, 0.0f, 12.0f, 10.0f));

    UBorder* FooterDivider = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("FooterDivider"));
    FooterDivider->SetBrushColor(DividerColor);
    USizeBox* FooterDividerBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("FooterDividerBox"));
    FooterDividerBox->SetHeightOverride(1.0f);
    FooterDividerBox->AddChild(FooterDivider);
    UVerticalBoxSlot* FooterDividerSlot = Stack->AddChildToVerticalBox(FooterDividerBox);
    FooterDividerSlot->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 10.0f));

    UHorizontalBox* Footer = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Footer"));
    Stack->AddChildToVerticalBox(Footer);

    UTextBlock* CancelLabel = nullptr;
    CancelButton = CreateButton(TEXT("CancelButton"), TEXT("取消全部修改"), CancelLabel, false);
    UHorizontalBoxSlot* CancelSlot = Footer->AddChildToHorizontalBox(CancelButton);
    CancelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    CancelSlot->SetPadding(FMargin(0.0f, 0.0f, 4.0f, 0.0f));

    SaveButton = CreateButton(TEXT("SaveButton"), TEXT("保存全部修改"), SaveButtonLabel, true);
    UHorizontalBoxSlot* SaveSlot = Footer->AddChildToHorizontalBox(SaveButton);
    SaveSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    SaveSlot->SetPadding(FMargin(4.0f, 0.0f, 0.0f, 0.0f));

    ToastBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ToastBorder"));
    ToastBorder->SetPadding(FMargin(0.0f));
    ToastBorder->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0.055f, 0.055f, 0.055f, 0.96f), 6.0f));
    ToastBorder->SetVisibility(ESlateVisibility::Collapsed);

    UCanvasPanelSlot* ToastSlot = Root->AddChildToCanvas(ToastBorder);
    if (ToastSlot)
    {
        ToastSlot->SetAnchors(FAnchors(1.0f, 0.0f));
        ToastSlot->SetAlignment(FVector2D(1.0f, 0.0f));
        ToastSlot->SetPosition(FVector2D(-24.0f, 24.0f));
        ToastSlot->SetAutoSize(true);
    }

    UHorizontalBox* ToastRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ToastRow"));
    ToastBorder->SetContent(ToastRow);

    ToastAccent = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ToastAccent"));
    ToastAccent->SetBrush(FSlateRoundedBoxBrush(SecondaryText, 1.0f));
    USizeBox* ToastAccentBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ToastAccentBox"));
    ToastAccentBox->SetWidthOverride(2.0f);
    ToastAccentBox->SetMinDesiredHeight(36.0f);
    ToastAccentBox->AddChild(ToastAccent);
    ToastRow->AddChildToHorizontalBox(ToastAccentBox);

    ToastText = CreateText(TEXT("ToastText"), TEXT(""), 12, PrimaryText);
    ToastText->SetAutoWrapText(true);
    ToastText->SetWrapTextAt(300.0f);
    USizeBox* ToastTextBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ToastTextBox"));
    ToastTextBox->SetMaxDesiredWidth(320.0f);
    ToastTextBox->AddChild(ToastText);
    UHorizontalBoxSlot* ToastTextSlot = ToastRow->AddChildToHorizontalBox(ToastTextBox);
    ToastTextSlot->SetPadding(FMargin(12.0f, 9.0f, 14.0f, 9.0f));
    ToastTextSlot->SetVerticalAlignment(VAlign_Center);

    ConfirmationOverlay = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ConfirmationOverlay"));
    ConfirmationOverlay->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.58f));
    ConfirmationOverlay->SetHorizontalAlignment(HAlign_Center);
    ConfirmationOverlay->SetVerticalAlignment(VAlign_Center);
    ConfirmationOverlay->SetVisibility(ESlateVisibility::Collapsed);
    UCanvasPanelSlot* ConfirmationSlot = Root->AddChildToCanvas(ConfirmationOverlay);
    if (ConfirmationSlot)
    {
        ConfirmationSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
        ConfirmationSlot->SetOffsets(FMargin(0.0f));
    }

    USizeBox* DialogBounds = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("DialogBounds"));
    DialogBounds->SetWidthOverride(380.0f);
    UBorder* DialogBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DialogBorder"));
    DialogBorder->SetPadding(FMargin(18.0f));
    DialogBorder->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0.07f, 0.07f, 0.07f, 0.99f), 6.0f));
    DialogBounds->AddChild(DialogBorder);
    ConfirmationOverlay->SetContent(DialogBounds);

    UVerticalBox* DialogStack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DialogStack"));
    DialogBorder->SetContent(DialogStack);
    ConfirmationTitleText = CreateText(TEXT("ConfirmationTitleText"), TEXT("确认操作"), 15, PrimaryText, true);
    DialogStack->AddChildToVerticalBox(ConfirmationTitleText);
    ConfirmationBodyText = CreateText(TEXT("ConfirmationBodyText"), TEXT(""), 12, SecondaryText);
    ConfirmationBodyText->SetAutoWrapText(true);
    ConfirmationBodyText->SetWrapTextAt(338.0f);
    UVerticalBoxSlot* ConfirmationBodySlot = DialogStack->AddChildToVerticalBox(ConfirmationBodyText);
    ConfirmationBodySlot->SetPadding(FMargin(0.0f, 6.0f, 0.0f, 14.0f));

    ConfirmationPrimaryButton = CreateButton(TEXT("ConfirmationPrimaryButton"), TEXT("确认"), ConfirmationPrimaryLabel, true);
    UVerticalBoxSlot* ConfirmationPrimarySlot = DialogStack->AddChildToVerticalBox(ConfirmationPrimaryButton);
    ConfirmationPrimarySlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
    ConfirmationSecondaryButton = CreateButton(TEXT("ConfirmationSecondaryButton"), TEXT("放弃"), ConfirmationSecondaryLabel, false);
    UVerticalBoxSlot* ConfirmationSecondarySlot = DialogStack->AddChildToVerticalBox(ConfirmationSecondaryButton);
    ConfirmationSecondarySlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
    ConfirmationContinueButton = CreateButton(TEXT("ConfirmationContinueButton"), TEXT("继续编辑"), ConfirmationContinueLabel, false);
    DialogStack->AddChildToVerticalBox(ConfirmationContinueButton);
    SetActiveTab(false);
}

UTextBlock* UOntoTwinRuntimeEditorPanel::CreateText(const FName Name, const FString& InitialText,
    int32 FontSize, const FLinearColor& Color, bool bBold) const
{
    if (!WidgetTree) return nullptr;

    UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
    Text->SetText(FText::FromString(InitialText));
    Text->SetColorAndOpacity(FSlateColor(Color));
    Text->SetFont(FOntoTwinGlassTheme::Font(FontSize, bBold));
    Text->SetAutoWrapText(false);
    return Text;
}

UButton* UOntoTwinRuntimeEditorPanel::CreateButton(const FName Name, const FString& Label,
    UTextBlock*& OutLabel, bool bPrimary) const
{
    if (!WidgetTree) return nullptr;

    UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
    Button->SetStyle(BuildButtonStyle(Button->GetStyle(), bPrimary));

    OutLabel = CreateText(FName(*(Name.ToString() + TEXT("Label"))), Label, 10,
        bPrimary ? PanelBackground : PrimaryText, true);
    OutLabel->SetJustification(ETextJustify::Center);
    Button->SetContent(OutLabel);
    return Button;
}

void UOntoTwinRuntimeEditorPanel::ShowToast(const FString& Message, EOntoTwinRuntimeToastType Type)
{
    if (!ToastBorder || !ToastAccent || !ToastText) return;

    FLinearColor AccentColor = SecondaryText;
    switch (Type)
    {
    case EOntoTwinRuntimeToastType::Success:
        AccentColor = ReadyColor;
        break;
    case EOntoTwinRuntimeToastType::Warning:
        AccentColor = WarningColor;
        break;
    case EOntoTwinRuntimeToastType::Error:
        AccentColor = ErrorColor;
        break;
    case EOntoTwinRuntimeToastType::Info:
    default:
        break;
    }

    ToastText->SetText(FText::FromString(Message));
    ToastAccent->SetBrush(FSlateRoundedBoxBrush(AccentColor, 1.0f));
    ToastBorder->SetVisibility(ESlateVisibility::HitTestInvisible);

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(ToastTimerHandle);
        World->GetTimerManager().SetTimer(
            ToastTimerHandle,
            this,
            &UOntoTwinRuntimeEditorPanel::HideToast,
            2.5f,
            false);
    }
}

void UOntoTwinRuntimeEditorPanel::ShowExitConfirmation()
{
    if (!ConfirmationOverlay) return;

    ConfirmationMode = EOntoTwinRuntimePanelConfirmation::ExitEditor;
    const bool bSwitchingToRoaming = SceneManager
        && SceneManager->IsRoamingRequestedAfterRuntimeEditExit();
    if (ConfirmationTitleText)
    {
        ConfirmationTitleText->SetText(FText::FromString(
            bSwitchingToRoaming ? TEXT("切换到人物漫游？") : TEXT("退出运行时编辑器？")));
    }
    if (ConfirmationBodyText)
    {
        ConfirmationBodyText->SetText(FText::FromString(
            FString::Printf(TEXT("当前有 %d 个实例包含未保存修改。"),
                SceneManager ? SceneManager->GetRuntimeEditPendingCount() : 0)));
    }
    if (ConfirmationPrimaryLabel)
    {
        ConfirmationPrimaryLabel->SetText(FText::FromString(
            bSwitchingToRoaming ? TEXT("保存并漫游") : TEXT("保存并退出")));
    }
    if (ConfirmationSecondaryLabel)
    {
        ConfirmationSecondaryLabel->SetText(FText::FromString(
            bSwitchingToRoaming ? TEXT("放弃修改并漫游") : TEXT("放弃修改并退出")));
    }
    if (ConfirmationContinueLabel) ConfirmationContinueLabel->SetText(FText::FromString(TEXT("继续编辑")));
    if (ConfirmationSecondaryButton) ConfirmationSecondaryButton->SetVisibility(ESlateVisibility::Visible);
    ConfirmationOverlay->SetVisibility(ESlateVisibility::Visible);
    SetKeyboardFocus();
}

void UOntoTwinRuntimeEditorPanel::ShowCancelAllConfirmation()
{
    if (!ConfirmationOverlay) return;

    ConfirmationMode = EOntoTwinRuntimePanelConfirmation::CancelAll;
    if (ConfirmationTitleText) ConfirmationTitleText->SetText(FText::FromString(TEXT("取消全部修改？")));
    if (ConfirmationBodyText)
    {
        ConfirmationBodyText->SetText(FText::FromString(
            FString::Printf(TEXT("将恢复本次编辑会话中 %d 个实例的全部修改，此操作不能通过重做恢复。"),
                SceneManager ? SceneManager->GetRuntimeEditPendingCount() : 0)));
    }
    if (ConfirmationPrimaryLabel) ConfirmationPrimaryLabel->SetText(FText::FromString(TEXT("确认取消全部修改")));
    if (ConfirmationContinueLabel) ConfirmationContinueLabel->SetText(FText::FromString(TEXT("继续编辑")));
    if (ConfirmationSecondaryButton) ConfirmationSecondaryButton->SetVisibility(ESlateVisibility::Collapsed);
    ConfirmationOverlay->SetVisibility(ESlateVisibility::Visible);
    SetKeyboardFocus();
}

bool UOntoTwinRuntimeEditorPanel::IsConfirmationOpen() const
{
    return ConfirmationMode != EOntoTwinRuntimePanelConfirmation::None &&
        ConfirmationOverlay && ConfirmationOverlay->GetVisibility() != ESlateVisibility::Collapsed;
}

bool UOntoTwinRuntimeEditorPanel::IsPointerOverPanel() const
{
    if (IsConfirmationOpen())
    {
        return true;
    }
    if (!PanelBorder || !PanelBorder->IsVisible() || !FSlateApplication::IsInitialized())
    {
        return false;
    }

    return PanelBorder->GetCachedGeometry().IsUnderLocation(FSlateApplication::Get().GetCursorPos());
}

void UOntoTwinRuntimeEditorPanel::HideConfirmation()
{
    ConfirmationMode = EOntoTwinRuntimePanelConfirmation::None;
    if (ConfirmationOverlay)
    {
        ConfirmationOverlay->SetVisibility(ESlateVisibility::Collapsed);
    }
}

void UOntoTwinRuntimeEditorPanel::RefreshPendingList()
{
    if (!SceneManager || !PendingList || !PendingToggleLabel) return;

    TArray<FString> PendingLines;
    SceneManager->GetRuntimeEditPendingLines(PendingLines);
    bool bContainsConflict = false;
    for (const FString& Line : PendingLines)
    {
        bContainsConflict |= Line.Contains(TEXT("冲突"));
    }
    if (bContainsConflict)
    {
        bPendingListExpanded = true;
    }

    PendingToggleLabel->SetText(FText::FromString(FString::Printf(
        TEXT("%s 待保存修改（%d）"),
        bPendingListExpanded ? TEXT("收起") : TEXT("展开"),
        PendingLines.Num())));
    PendingList->ClearChildren();
    for (const FString& Line : PendingLines)
    {
        const FName LineName = MakeUniqueObjectName(WidgetTree, UTextBlock::StaticClass(), TEXT("PendingLine"));
        UTextBlock* LineText = CreateText(LineName, Line, 11,
            Line.Contains(TEXT("冲突")) ? ErrorColor : SecondaryText);
        LineText->SetAutoWrapText(true);
        LineText->SetWrapTextAt(380.0f);
        UVerticalBoxSlot* LineSlot = PendingList->AddChildToVerticalBox(LineText);
        LineSlot->SetPadding(FMargin(0.0f, 2.0f));
    }
    PendingList->SetVisibility(
        bPendingListExpanded && PendingLines.Num() > 0
            ? ESlateVisibility::SelfHitTestInvisible
            : ESlateVisibility::Collapsed);
}

void UOntoTwinRuntimeEditorPanel::HideToast()
{
    if (ToastBorder)
    {
        ToastBorder->SetVisibility(ESlateVisibility::Collapsed);
    }
}

void UOntoTwinRuntimeEditorPanel::SetActiveTab(bool bBusiness)
{
    bBusinessTabActive = bBusiness;
    if (SceneContent) SceneContent->SetVisibility(
        bBusiness ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
    if (BusinessContent) BusinessContent->SetVisibility(
        bBusiness ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    if (SceneTabButton) SceneTabButton->SetStyle(
        BuildTabButtonStyle(SceneTabButton->GetStyle(), !bBusiness));
    if (BusinessTabButton) BusinessTabButton->SetStyle(
        BuildTabButtonStyle(BusinessTabButton->GetStyle(), bBusiness));
    SetTextColor(SceneTabLabel, bBusiness ? SecondaryText : PanelBackground);
    SetTextColor(BusinessTabLabel, bBusiness ? PanelBackground : SecondaryText);
    if (bBusiness) RefreshBusinessEditor();
}

void UOntoTwinRuntimeEditorPanel::RefreshBusinessEditor()
{
    if (!SceneManager || !BusinessSelector) return;
    const int32 SelectionCount = SceneManager->GetRuntimeEditSelectionCount();
    TArray<FString> SelectedNames;
    SceneManager->GetRuntimeSelectedInstanceDisplayNames(SelectedNames);
    if (BusinessSelectionText)
    {
        FString SelectionText = TEXT("尚未选择对象");
        if (SelectedNames.Num() == 1)
        {
            SelectionText = TEXT("已选：") + SelectedNames[0];
        }
        else if (SelectedNames.Num() > 1)
        {
            const int32 VisibleCount = FMath::Min(SelectedNames.Num(), 3);
            TArray<FString> VisibleNames;
            for (int32 Index = 0; Index < VisibleCount; ++Index)
            {
                VisibleNames.Add(SelectedNames[Index]);
            }
            SelectionText = FString::Printf(TEXT("已选 %d 个：%s"),
                SelectionCount, *FString::Join(VisibleNames, TEXT("、")));
            if (SelectedNames.Num() > VisibleCount)
            {
                SelectionText += FString::Printf(TEXT(" 等 %d 个"), SelectedNames.Num());
            }
        }
        BusinessSelectionText->SetText(FText::FromString(SelectionText));
    }

    TArray<FString> NextOptionIds;
    TArray<FString> NextOptionLabels;
    TArray<uint8> States;
    SceneManager->GetRuntimeBusinessMembershipRows(
        NextOptionIds, NextOptionLabels, States);
    const bool bHasSnapshot = SceneManager->HasRuntimeBusinessSnapshot();
    const FString Placeholder = !bHasSnapshot
        ? TEXT("业务同步中")
        : (NextOptionIds.Num() == 0
            ? TEXT("暂无业务")
            : (SelectionCount == 0
                ? TEXT("请先选择对象")
                : TEXT("选择业务")));

    FString OptionsSignature = FString::Printf(
        TEXT("%d|%d|%d"), bHasSnapshot ? 1 : 0, SelectionCount, NextOptionIds.Num());
    for (int32 Index = 0; Index < NextOptionIds.Num(); ++Index)
    {
        const uint8 State = States.IsValidIndex(Index) ? States[Index] : 0;
        OptionsSignature += FString::Printf(
            TEXT("|%s\x1f%s\x1f%d"),
            *NextOptionIds[Index],
            NextOptionLabels.IsValidIndex(Index) ? *NextOptionLabels[Index] : TEXT(""),
            static_cast<int32>(State));
    }

    // RefreshFromManager runs every frame. Rebuilding UComboBoxString each frame closes
    // its popup immediately after click, which makes a populated selector look inert.
    // Rebuild only when its actual data or selection context changes.
    if (BusinessOptionsSignature != OptionsSignature)
    {
        BusinessOptionsSignature = OptionsSignature;
        BusinessOptionIds = MoveTemp(NextOptionIds);
        BusinessOptionLabels = MoveTemp(NextOptionLabels);
        BusinessOptionDisplayLabels.Reset();
        bRefreshingBusinessSelector = true;
        BusinessSelector->ClearOptions();
        BusinessSelector->AddOption(Placeholder);
        for (int32 Index = 0; Index < BusinessOptionLabels.Num(); ++Index)
        {
            const uint8 State = States.IsValidIndex(Index) ? States[Index] : 0;
            const FString Prefix = State == 2 ? TEXT("[已选]")
                : (State == 1 ? TEXT("[部分]") : TEXT("[未选]"));
            const FString OptionDisplayLabel = Prefix + TEXT(" ") + BusinessOptionLabels[Index];
            BusinessOptionDisplayLabels.Add(OptionDisplayLabel);
            BusinessSelector->AddOption(OptionDisplayLabel);
        }
        BusinessSelector->SetSelectedIndex(0);
        bRefreshingBusinessSelector = false;
    }

    BusinessSelector->SetIsEnabled(
        SelectionCount > 0 && BusinessOptionIds.Num() > 0
        && bHasSnapshot);
    const FString Availability = !bHasSnapshot
        ? TEXT("正在同步业务数据，完成后会自动启用。")
        : (BusinessOptionIds.Num() == 0
            ? TEXT("当前项目没有已启用业务，可在下方新建。")
            : (SelectionCount == 0
                ? FString::Printf(TEXT("已同步 %d 项业务。请先在场景中选择对象。"), BusinessOptionIds.Num())
                : FString::Printf(
                    TEXT("已同步 %d 项业务。选择后切换归属：[已选] 全部属于，[部分] 仅部分属于，[未选] 全部不属于。保存后生效。"),
                    BusinessOptionIds.Num())));
    BusinessSelector->SetToolTipText(FText::FromString(Availability));
    if (CreateBusinessButton)
    {
        CreateBusinessButton->SetIsEnabled(
            SelectionCount > 0 && bHasSnapshot);
    }
    if (BusinessNameInput) BusinessNameInput->SetIsEnabled(SelectionCount > 0 && bHasSnapshot);
}

UWidget* UOntoTwinRuntimeEditorPanel::GenerateBusinessOptionWidget(FString Item)
{
    UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    Text->SetText(FText::FromString(Item));
    Text->SetColorAndOpacity(FSlateColor(PrimaryText));
    Text->SetFont(FOntoTwinGlassTheme::Font(10.0f));
    Text->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    RetainedComboTextWidgets.Add(Text);
    return Text;
}

void UOntoTwinRuntimeEditorPanel::RefreshFromManager()
{
    if (!SceneManager) return;

    if (PanelBounds && GEngine && GEngine->GameViewport)
    {
        FVector2D ViewportSize = FVector2D::ZeroVector;
        GEngine->GameViewport->GetViewportSize(ViewportSize);
        if (ViewportSize.Y > 0.0f)
        {
            PanelBounds->SetMaxDesiredHeight(FMath::Max(360.0f, ViewportSize.Y * 0.70f));
        }
    }

    if (HeaderStateText)
    {
        HeaderStateText->SetText(FText::FromString(SceneManager->GetRuntimeEditorHeaderStateText()));
        SetTextColor(HeaderStateText,
            SceneManager->IsRuntimeEditDirty() && !SceneManager->IsRuntimeEditSaving() ? WarningColor : SecondaryText);
    }

    if (DisplayNameText)
    {
        DisplayNameText->SetText(FText::FromString(SceneManager->GetRuntimeEditorDisplayName()));
    }
    const bool bCanCopyIdentity = SceneManager->GetRuntimeEditSelectionCount() == 1;
    const FString DisplayNameRaw = SceneManager->GetRuntimeEditorDisplayNameRaw();
    if (DisplayNameCopyButton)
    {
        DisplayNameCopyButton->SetIsEnabled(bCanCopyIdentity && !DisplayNameRaw.IsEmpty());
        DisplayNameCopyButton->SetToolTipText(FText::FromString(
            bCanCopyIdentity ? TEXT("点击复制完整实例名称") : TEXT("单选一个实例后可复制名称")));
    }
    if (TypeNameText)
    {
        TypeNameText->SetText(FText::FromString(SceneManager->GetRuntimeEditorTypeName()));
    }
    const FString TypeNameRaw = SceneManager->GetRuntimeEditorTypeNameRaw();
    if (TypeNameCopyButton)
    {
        TypeNameCopyButton->SetIsEnabled(bCanCopyIdentity && !TypeNameRaw.IsEmpty());
        TypeNameCopyButton->SetToolTipText(FText::FromString(
            bCanCopyIdentity && !TypeNameRaw.IsEmpty()
                ? TEXT("点击复制完整类型名称")
                : TEXT("单选且类型名称可用时可复制")));
    }
    if (InstanceIdText)
    {
        InstanceIdText->SetText(FText::FromString(SceneManager->GetRuntimeEditorInstanceIdText()));
    }

    FVector Location = FVector::ZeroVector;
    float Yaw = 0.0f;
    const bool bHasTransform = SceneManager->GetRuntimeEditorTransform(Location, Yaw);
    if (XValueText) XValueText->SetText(FText::FromString(bHasTransform ? FString::Printf(TEXT("%.1f"), Location.X) : TEXT("-")));
    if (YValueText) YValueText->SetText(FText::FromString(bHasTransform ? FString::Printf(TEXT("%.1f"), Location.Y) : TEXT("-")));
    if (ZValueText) ZValueText->SetText(FText::FromString(bHasTransform ? FString::Printf(TEXT("%.1f"), Location.Z) : TEXT("-")));
    if (YawValueText) YawValueText->SetText(FText::FromString(bHasTransform ? FString::Printf(TEXT("%.1f"), Yaw) : TEXT("-")));
    if (YawLabelText)
    {
        YawLabelText->SetText(FText::FromString(
            SceneManager->IsRuntimeSelectionMultiple() ? TEXT("偏航增量") : TEXT("偏航")));
    }

    const EOntoTwinRuntimeAccessState AccessState = SceneManager->GetRuntimeEditorAccessState();
    FLinearColor AccessColor = MutedText;
    FString AccessText = TEXT("无法验证");
    FString ActionText;
    bool bShowAccessAction = false;
    bool bEnableAccessAction = false;

    switch (AccessState)
    {
    case EOntoTwinRuntimeAccessState::Checking:
        AccessText = TEXT("正在检查");
        ActionText = TEXT("检查中...");
        bShowAccessAction = true;
        break;
    case EOntoTwinRuntimeAccessState::Ready:
        AccessColor = ReadyColor;
        AccessText = TEXT("已就绪");
        break;
    case EOntoTwinRuntimeAccessState::Unbound:
        AccessColor = WarningColor;
        AccessText = TEXT("尚未绑定");
        ActionText = TEXT("绑定当前数据集");
        bShowAccessAction = true;
        bEnableAccessAction = SceneManager->CanBindRuntimeProject();
        break;
    case EOntoTwinRuntimeAccessState::Mismatch:
        AccessColor = ErrorColor;
        AccessText = TEXT("已绑定其他 UE 工程");
        break;
    case EOntoTwinRuntimeAccessState::Error:
    default:
        AccessColor = ErrorColor;
        AccessText = TEXT("无法验证");
        ActionText = TEXT("重试");
        bShowAccessAction = true;
        bEnableAccessAction = SceneManager->CanRetryRuntimeBindingStatus();
        break;
    }

    if (AccessStatusDot)
    {
        AccessStatusDot->SetBrush(FSlateRoundedBoxBrush(AccessColor, 3.0f));
    }
    if (AccessStatusText)
    {
        AccessStatusText->SetText(FText::FromString(AccessText));
    }
    if (AccessActionLabel)
    {
        AccessActionLabel->SetText(FText::FromString(ActionText));
    }
    if (AccessActionButton)
    {
        AccessActionButton->SetVisibility(bShowAccessAction ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
        AccessActionButton->SetIsEnabled(bEnableAccessAction);
    }

    if (SaveButtonLabel)
    {
        SaveButtonLabel->SetText(FText::FromString(
            SceneManager->IsRuntimeEditSaving() ? TEXT("正在保存...") : TEXT("保存全部修改")));
    }
    if (SaveButton) SaveButton->SetIsEnabled(SceneManager->CanSaveRuntimeEdit());
    if (CancelButton) CancelButton->SetIsEnabled(SceneManager->CanCancelRuntimeEdit());
    if (CloseButton) CloseButton->SetIsEnabled(!SceneManager->IsRuntimeEditSaving());
    if (UndoButton) UndoButton->SetIsEnabled(SceneManager->CanUndoRuntimeEdit());
    if (RedoButton) RedoButton->SetIsEnabled(SceneManager->CanRedoRuntimeEdit());
    if (RemoveButton) RemoveButton->SetIsEnabled(SceneManager->CanRemoveRuntimeSelection());

    if (WallSnapCheckBox && WallSnapCheckBox->IsChecked() != SceneManager->IsRuntimeWallSnapEnabled())
    {
        WallSnapCheckBox->SetIsChecked(SceneManager->IsRuntimeWallSnapEnabled());
    }
    if (GridSnapCheckBox && GridSnapCheckBox->IsChecked() != SceneManager->IsRuntimeGridSnapEnabled())
    {
        GridSnapCheckBox->SetIsChecked(SceneManager->IsRuntimeGridSnapEnabled());
    }

    RefreshPendingList();
    if (bBusinessTabActive) RefreshBusinessEditor();
}

void UOntoTwinRuntimeEditorPanel::HandleAccessActionClicked()
{
    if (!SceneManager) return;

    if (SceneManager->GetRuntimeEditorAccessState() == EOntoTwinRuntimeAccessState::Unbound)
    {
        SceneManager->BindCurrentRuntimeProject();
    }
    else
    {
        SceneManager->RetryRuntimeBindingStatus();
    }
}

void UOntoTwinRuntimeEditorPanel::HandleDisplayNameCopyClicked()
{
    if (!SceneManager) return;
    const FString Value = SceneManager->GetRuntimeEditorDisplayNameRaw();
    if (Value.IsEmpty()) return;
    FPlatformApplicationMisc::ClipboardCopy(*Value);
    ShowToast(TEXT("已复制实例名称"), EOntoTwinRuntimeToastType::Success);
}

void UOntoTwinRuntimeEditorPanel::HandleTypeNameCopyClicked()
{
    if (!SceneManager) return;
    const FString Value = SceneManager->GetRuntimeEditorTypeNameRaw();
    if (Value.IsEmpty()) return;
    FPlatformApplicationMisc::ClipboardCopy(*Value);
    ShowToast(TEXT("已复制类型名称"), EOntoTwinRuntimeToastType::Success);
}

void UOntoTwinRuntimeEditorPanel::HandleCloseClicked()
{
    if (SceneManager) SceneManager->ToggleRuntimeEditMode();
}

void UOntoTwinRuntimeEditorPanel::HandleSaveClicked()
{
    if (SceneManager) SceneManager->SaveRuntimeEdit();
}

void UOntoTwinRuntimeEditorPanel::HandleCancelClicked()
{
    if (SceneManager && SceneManager->CanCancelRuntimeEdit())
    {
        ShowCancelAllConfirmation();
    }
}

void UOntoTwinRuntimeEditorPanel::HandleUndoClicked()
{
    if (SceneManager) SceneManager->UndoRuntimeEdit();
}

void UOntoTwinRuntimeEditorPanel::HandleRedoClicked()
{
    if (SceneManager) SceneManager->RedoRuntimeEdit();
}

void UOntoTwinRuntimeEditorPanel::HandleSceneTabClicked()
{
    SetActiveTab(false);
}

void UOntoTwinRuntimeEditorPanel::HandleBusinessTabClicked()
{
    SetActiveTab(true);
}

void UOntoTwinRuntimeEditorPanel::HandleBusinessSelected(
    FString SelectedItem,
    ESelectInfo::Type SelectionType)
{
    (void)SelectionType;
    if (bRefreshingBusinessSelector || !SceneManager) return;
    const int32 Index = BusinessOptionDisplayLabels.IndexOfByKey(SelectedItem);
    if (BusinessOptionIds.IsValidIndex(Index))
    {
        SceneManager->ToggleRuntimeBusinessMembership(BusinessOptionIds[Index]);
        RefreshBusinessEditor();
    }
}

void UOntoTwinRuntimeEditorPanel::HandleCreateBusinessClicked()
{
    if (!SceneManager || !BusinessNameInput) return;
    const FString Name = BusinessNameInput->GetText().ToString().TrimStartAndEnd();
    if (SceneManager->CreateRuntimeBusiness(Name))
    {
        BusinessNameInput->SetText(FText::GetEmpty());
        ShowToast(
            TEXT("业务已创建，保存后立即进入运行 Dock"),
            EOntoTwinRuntimeToastType::Success);
        RefreshBusinessEditor();
    }
    else
    {
        ShowToast(
            TEXT("无法创建业务，请检查名称和当前选择"),
            EOntoTwinRuntimeToastType::Warning);
    }
}

void UOntoTwinRuntimeEditorPanel::HandleRemoveClicked()
{
    if (SceneManager) SceneManager->RemoveRuntimeSelectionFromScene();
}

void UOntoTwinRuntimeEditorPanel::HandlePendingToggleClicked()
{
    bPendingListExpanded = !bPendingListExpanded;
    RefreshPendingList();
}

void UOntoTwinRuntimeEditorPanel::HandleConfirmationPrimaryClicked()
{
    if (!SceneManager)
    {
        HideConfirmation();
        return;
    }

    const EOntoTwinRuntimePanelConfirmation Action = ConfirmationMode;
    HideConfirmation();
    if (Action == EOntoTwinRuntimePanelConfirmation::ExitEditor)
    {
        SceneManager->SaveAndExitRuntimeEdit();
    }
    else if (Action == EOntoTwinRuntimePanelConfirmation::CancelAll)
    {
        SceneManager->CancelRuntimeEdit();
    }
}

void UOntoTwinRuntimeEditorPanel::HandleConfirmationSecondaryClicked()
{
    const EOntoTwinRuntimePanelConfirmation Action = ConfirmationMode;
    HideConfirmation();
    if (SceneManager && Action == EOntoTwinRuntimePanelConfirmation::ExitEditor)
    {
        SceneManager->DiscardAndExitRuntimeEdit();
    }
}

void UOntoTwinRuntimeEditorPanel::HandleConfirmationContinueClicked()
{
    if (SceneManager) SceneManager->CancelRuntimeEditExitRequest();
    HideConfirmation();
}

void UOntoTwinRuntimeEditorPanel::HandleWallSnapChanged(bool bIsChecked)
{
    if (SceneManager) SceneManager->SetRuntimeWallSnapEnabled(bIsChecked);
}

void UOntoTwinRuntimeEditorPanel::HandleGridSnapChanged(bool bIsChecked)
{
    if (SceneManager) SceneManager->SetRuntimeGridSnapEnabled(bIsChecked);
}
