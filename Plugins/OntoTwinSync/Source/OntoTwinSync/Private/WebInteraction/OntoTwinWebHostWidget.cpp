#include "WebInteraction/OntoTwinWebHostWidget.h"

#include "WebInteraction/OntoTwinWebBridge.h"
#include "WebInteraction/OntoTwinWebInteractionComponent.h"

#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "SWebInterface.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void UOntoTwinWebHostWidget::Configure(
    UOntoTwinWebInteractionComponent* InOwner,
    UOntoTwinWebBridge* InBridge,
    EOntoTwinWebGlassQuality InQuality)
{
    OwnerComponent = InOwner;
    BridgeObject = InBridge;
    EffectiveQuality = InQuality;
}

TSharedRef<SWidget> UOntoTwinWebHostWidget::RebuildWidget()
{
    // The host must not paint or blur the full viewport. The web page owns only
    // its local HUD surfaces; all unpainted pixels remain transparent to the UE scene.
    SAssignNew(Browser, SWebInterface)
        .InitialURL(TEXT("about:blank"))
        .FrameRate(60)
        .BackgroundColor(FColor::Transparent)
        .OnLoadStarted(FSimpleDelegate::CreateUObject(this, &UOntoTwinWebHostWidget::HandleLoadStarted))
        .OnLoadCompleted(FSimpleDelegate::CreateUObject(this, &UOntoTwinWebHostWidget::HandleLoadCompleted))
        .OnLoadError(FSimpleDelegate::CreateUObject(this, &UOntoTwinWebHostWidget::HandleLoadError))
        .OnUrlChanged(FOnTextChanged::CreateUObject(this, &UOntoTwinWebHostWidget::HandleUrlChanged))
        .OnBeforePopup(FOnBeforePopupDelegate::CreateUObject(this, &UOntoTwinWebHostWidget::HandleBeforePopup))
        .OnBeforeNavigation(SWebInterface::FOnBeforeBrowse::CreateUObject(this, &UOntoTwinWebHostWidget::HandleBeforeNavigation));

    if (BridgeObject)
    {
        Browser->BindUObject(TEXT("ontotwinWebBridge"), BridgeObject, true);
    }

    return SNew(SOverlay)
        + SOverlay::Slot()
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(18.0f, 12.0f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("‹")))
                    .ToolTipText(FText::FromString(TEXT("返回上一上下文")))
                    .Visibility_Lambda([this]()
                    {
                        return OwnerComponent && OwnerComponent->CanGoBack()
                            ? EVisibility::Visible
                            : EVisibility::Collapsed;
                    })
                    .OnClicked_UObject(this, &UOntoTwinWebHostWidget::HandleBackClicked)
                ]
                + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                [
                    SAssignNew(StatusText, STextBlock)
                    .Text(FText::FromString(TEXT("准备打开页面")))
                    .ColorAndOpacity(FSlateColor(FLinearColor(0.90f, 0.90f, 0.90f, 1.0f)))
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [
                    SNew(SBox)
                    .WidthOverride(20.0f)
                    .HeightOverride(20.0f)
                    [
                        SNew(SButton)
                        .ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("NoBorder")))
                        .ContentPadding(FMargin(0.0f))
                        .HAlign(HAlign_Center)
                        .VAlign(VAlign_Center)
                        .ToolTipText(FText::FromString(TEXT("关闭网页")))
                        .OnClicked_UObject(this, &UOntoTwinWebHostWidget::HandleCloseClicked)
                        [
                            SNew(STextBlock)
                            .Text(FText::FromString(TEXT("×")))
                            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10))
                            .ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.76f, 0.80f, 0.52f)))
                        ]
                    ]
                ]
            ]
            + SVerticalBox::Slot().FillHeight(1.0f).Padding(12.0f, 0.0f, 12.0f, 12.0f)
            [
                Browser.ToSharedRef()
            ]
        ];
}

void UOntoTwinWebHostWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);
    if (!Browser.IsValid()) return;
    Browser->SetVisibility(IsPointerOverInteractiveRegion()
        ? EVisibility::Visible
        : EVisibility::HitTestInvisible);
}

FReply UOntoTwinWebHostWidget::NativeOnPreviewKeyDown(
    const FGeometry& InGeometry,
    const FKeyEvent& InKeyEvent)
{
    if (OwnerComponent && OwnerComponent->HandleHostShortcut(InKeyEvent.GetKey()))
    {
        return FReply::Handled();
    }
    return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

bool UOntoTwinWebHostWidget::IsPointerOverInteractiveRegion() const
{
    // Until a page declares its hit regions it remains safely interactive as a
    // whole. Bridge-enabled pages become pointer-transparent outside those regions.
    if (!bBridgeReady || InteractiveRegions.Num() == 0 || !Browser.IsValid()) return true;
    // Browser geometry is expressed in Slate desktop space. PlayerController
    // mouse coordinates are viewport-local pixels, which are offset (and often
    // DPI-scaled) in PIE. Mixing the two makes the visible button transparent
    // to input and sends the click into the 3D scene behind it.
    const FVector2D Local = Browser->GetCachedGeometry().AbsoluteToLocal(
        FSlateApplication::Get().GetCursorPos());
    return InteractiveRegions.ContainsByPredicate(
        [&Local](const FSlateRect& Region)
        {
            return Local.X >= Region.Left && Local.X <= Region.Right
                && Local.Y >= Region.Top && Local.Y <= Region.Bottom;
        });
}

void UOntoTwinWebHostWidget::NativeDestruct()
{
    if (Browser.IsValid() && BridgeObject)
    {
        Browser->UnbindUObject(TEXT("ontotwinWebBridge"), BridgeObject, true);
    }
    Browser.Reset();
    StatusText.Reset();
    Super::NativeDestruct();
}

void UOntoTwinWebHostWidget::Navigate(const FString& Url)
{
    if (Browser.IsValid()) Browser->LoadURL(Url);
}

void UOntoTwinWebHostWidget::ReloadPage()
{
    if (Browser.IsValid()) Browser->Reload();
}

void UOntoTwinWebHostWidget::ExecuteJavascript(const FString& Script)
{
    if (Browser.IsValid()) Browser->ExecuteJavascript(Script);
}

FString UOntoTwinWebHostWidget::GetCurrentUrl() const
{
    return Browser.IsValid() ? Browser->GetUrl() : FString();
}

void UOntoTwinWebHostWidget::SetHostStatus(const FString& Status, bool bError)
{
    bStatusError = bError;
    if (StatusText.IsValid())
    {
        StatusText->SetText(FText::FromString(Status));
        StatusText->SetColorAndOpacity(bStatusError
            ? FSlateColor(FLinearColor(0.95f, 0.35f, 0.3f, 1.0f))
            : FSlateColor(FLinearColor(0.90f, 0.90f, 0.90f, 1.0f)));
    }
}

void UOntoTwinWebHostWidget::SetBridgeReady(bool bReady)
{
    bBridgeReady = bReady;
    if (!bReady) InteractiveRegions.Reset();
}

void UOntoTwinWebHostWidget::SetInteractiveRegions(const TArray<FSlateRect>& Regions)
{
    InteractiveRegions = Regions;
}

void UOntoTwinWebHostWidget::HandleLoadStarted()
{
    bBridgeReady = false;
    InteractiveRegions.Reset();
    if (OwnerComponent) OwnerComponent->HandlePageLoadStarted();
    SetHostStatus(TEXT("正在加载页面"));
}

void UOntoTwinWebHostWidget::HandleLoadCompleted()
{
    SetHostStatus(TEXT("页面已加载"));
    if (OwnerComponent) OwnerComponent->HandlePageLoaded();
}

void UOntoTwinWebHostWidget::HandleLoadError()
{
    SetHostStatus(TEXT("页面加载失败，请返回或关闭"), true);
    if (OwnerComponent) OwnerComponent->HandlePageLoadError();
}

void UOntoTwinWebHostWidget::HandleUrlChanged(const FText& UrlText)
{
    if (OwnerComponent) OwnerComponent->HandleUrlChanged(UrlText.ToString());
}

bool UOntoTwinWebHostWidget::HandleBeforePopup(FString Url, FString Frame)
{
    return !OwnerComponent || OwnerComponent->HandlePopupNavigation(Url);
}

bool UOntoTwinWebHostWidget::HandleBeforeNavigation(
    const FString& Url,
    const FWebNavigationRequest& Request)
{
    FString Reason;
    return !OwnerComponent || !OwnerComponent->ValidateNavigation(Url, Reason);
}

FReply UOntoTwinWebHostWidget::HandleBackClicked()
{
    if (OwnerComponent) OwnerComponent->Back();
    return FReply::Handled();
}

FReply UOntoTwinWebHostWidget::HandleCloseClicked()
{
    if (OwnerComponent) OwnerComponent->Close();
    return FReply::Handled();
}
