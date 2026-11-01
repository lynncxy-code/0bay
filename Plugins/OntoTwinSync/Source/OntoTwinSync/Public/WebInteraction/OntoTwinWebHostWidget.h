#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Layout/SlateRect.h"
#include "OntoTwinWebHostWidget.generated.h"

class STextBlock;
class SWebInterface;
class UOntoTwinWebBridge;
class UOntoTwinWebInteractionComponent;
struct FWebNavigationRequest;

UENUM(BlueprintType)
enum class EOntoTwinWebGlassQuality : uint8
{
    High,
    Balanced,
    Performance
};

/** One full-screen Screen Space browser with decoration below sharp page pixels. */
UCLASS()
class ONTOTWINSYNC_API UOntoTwinWebHostWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    void Configure(
        UOntoTwinWebInteractionComponent* InOwner,
        UOntoTwinWebBridge* InBridge,
        EOntoTwinWebGlassQuality InQuality);
    void Navigate(const FString& Url);
    void ReloadPage();
    void ExecuteJavascript(const FString& Script);
    FString GetCurrentUrl() const;
    void SetHostStatus(const FString& Status, bool bError = false);
    void SetBridgeReady(bool bReady);
    void SetInteractiveRegions(const TArray<FSlateRect>& Regions);
    bool IsPointerOverInteractiveRegion() const;

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
    virtual FReply NativeOnPreviewKeyDown(
        const FGeometry& InGeometry,
        const FKeyEvent& InKeyEvent) override;
    virtual void NativeDestruct() override;

private:
    UPROPERTY()
    UOntoTwinWebInteractionComponent* OwnerComponent = nullptr;

    UPROPERTY()
    UOntoTwinWebBridge* BridgeObject = nullptr;

    EOntoTwinWebGlassQuality EffectiveQuality = EOntoTwinWebGlassQuality::Balanced;
    TSharedPtr<SWebInterface> Browser;
    TSharedPtr<STextBlock> StatusText;
    TArray<FSlateRect> InteractiveRegions;
    bool bBridgeReady = false;
    bool bStatusError = false;

    void HandleLoadStarted();
    void HandleLoadCompleted();
    void HandleLoadError();
    void HandleUrlChanged(const FText& UrlText);
    bool HandleBeforePopup(FString Url, FString Frame);
    bool HandleBeforeNavigation(const FString& Url, const FWebNavigationRequest& Request);
    FReply HandleBackClicked();
    FReply HandleCloseClicked();
};
