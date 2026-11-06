#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Dom/JsonObject.h"
#include "TimerManager.h"
#include "UI/OntoTwinGlassRenderer.h"
#include "OntoTwinOverlayWidget.generated.h"

class UBorder;
class UBackgroundBlur;
class UButton;
class UGridPanel;
class UHorizontalBox;
class UImage;
class UOntoTwinGaugeWidget;
class UMaterialInterface;
class UOverlay;
class USizeBox;
class UTextBlock;
class UTexture;
class UTexture2DDynamic;
class UAsyncTaskDownloadImage;
class UVerticalBox;

enum class EOntoTwinOverlayMediaAction : uint8
{
    PlayPause,
    ToggleMute,
    ToggleExpanded,
    Close,
	Retry,
};

enum class EOntoTwinOverlayVisualPhase : uint8
{
	Opening,
	Idle,
	Closing,
};

UCLASS()
class ONTOTWINSYNC_API UOntoTwinOverlayWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UOntoTwinOverlayWidget(const FObjectInitializer& ObjectInitializer);
    void ApplyOverlayData(const TSharedPtr<FJsonObject>& OverlayData);
    void SetWorldSpacePresentation(bool bEnabled);
    FVector2D GetDesiredRenderSize();
    FString GetConfigRevision() const { return ConfigRevision; }
    bool HasPlayableMedia() const { return bMediaAvailable; }
    bool ShouldAutoplayMedia() const { return bMediaAutoplay; }
    bool IsMediaExpanded() const { return bMediaExpanded; }
    bool IsPointerOverPanel() const { return bPointerOverPanel; }
    FString GetMediaSourceRevision() const { return MediaSourceRevision; }
    void SetMediaActionHandler(TFunction<void(EOntoTwinOverlayMediaAction)> Handler);
    void SetMediaTexture(UTexture* Texture);
	void SetMediaPlaybackState(
		bool bPlaying,
		bool bMuted,
		const FString& StatusMessage = FString(),
		bool bShowRetry = false);
	void SetMediaExpanded(bool bExpanded);
	void PlayCloseAnimation(TFunction<void()> Completion);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnAddedToFocusPath(const FFocusEvent& InFocusEvent) override;
	virtual void NativeOnRemovedFromFocusPath(const FFocusEvent& InFocusEvent) override;

private:
    UPROPERTY()
    USizeBox* OverlayBounds = nullptr;

    UPROPERTY()
    UOverlay* GlassLayerRoot = nullptr;

	UPROPERTY()
	UImage* HighGlassSurface = nullptr;

	UPROPERTY()
	UBackgroundBlur* BalancedBackgroundBlur = nullptr;

	UPROPERTY()
	UImage* FineNoiseLayer = nullptr;

    UPROPERTY()
    UMaterialInterface* HighGlassMaterial = nullptr;

    // Hard references let the cooker discover every shared postbuffer material
    // without requiring host projects to maintain an OntoTwin AlwaysCook path.
    UPROPERTY()
    TArray<TObjectPtr<UMaterialInterface>> CookedHighGlassMaterials;

	UPROPERTY()
	UBorder* PanelBorder = nullptr;

	UPROPERTY()
	USizeBox* TopHighlightBounds = nullptr;

	UPROPERTY()
	UBorder* TopHighlight = nullptr;

	UPROPERTY()
	USizeBox* StatusAccentBounds = nullptr;

	UPROPERTY()
	UBorder* StatusAccent = nullptr;

	UPROPERTY()
	UBorder* InteractionRim = nullptr;

	UPROPERTY()
	UBorder* ContentContainer = nullptr;

	UPROPERTY()
	UVerticalBox* ContentStack = nullptr;

    UPROPERTY()
    UTextBlock* TitleText = nullptr;

    UPROPERTY()
    UTextBlock* SubtitleText = nullptr;

    UPROPERTY()
    UTextBlock* BodyText = nullptr;

    UPROPERTY()
    UBorder* StatusDot = nullptr;

    UPROPERTY()
    USizeBox* StatusDotBounds = nullptr;

    UPROPERTY()
    UTextBlock* StatusSemanticText = nullptr;

    UPROPERTY()
    UTextBlock* StatusText = nullptr;

    UPROPERTY()
    UHorizontalBox* StatusRow = nullptr;

    UPROPERTY()
    UGridPanel* MetricsGrid = nullptr;

    UPROPERTY()
    TArray<UTextBlock*> MetricLabels;

    UPROPERTY()
    TArray<UTextBlock*> MetricValues;

    TArray<bool> MetricEmphasis;

    UPROPERTY()
    UVerticalBox* GaugeRegion = nullptr;

    UPROPERTY()
    UHorizontalBox* GaugeRow = nullptr;

    UPROPERTY()
    UOntoTwinGaugeWidget* GaugeWidget = nullptr;

    UPROPERTY()
    UTextBlock* GaugeLabel = nullptr;

    UPROPERTY()
    UTextBlock* GaugeValue = nullptr;

    UPROPERTY()
    UTextBlock* GaugeRange = nullptr;

    UPROPERTY()
    UTextBlock* GaugeFallback = nullptr;

    UPROPERTY()
    USizeBox* MediaBounds = nullptr;

    UPROPERTY()
    UImage* MediaImage = nullptr;

    UPROPERTY()
    UTextBlock* MediaStateText = nullptr;

    UPROPERTY()
    UHorizontalBox* MediaControls = nullptr;

    UPROPERTY()
    UButton* PlayPauseButton = nullptr;

    UPROPERTY()
    UTextBlock* PlayPauseButtonText = nullptr;

    UPROPERTY()
    UButton* MuteButton = nullptr;

    UPROPERTY()
    UTextBlock* MuteButtonText = nullptr;

    UPROPERTY()
    UButton* ExpandButton = nullptr;

    UPROPERTY()
    UTextBlock* ExpandButtonText = nullptr;

    UPROPERTY()
    UButton* CloseButton = nullptr;

    UPROPERTY()
    UButton* RetryButton = nullptr;

    UPROPERTY()
    UAsyncTaskDownloadImage* PosterDownloadTask = nullptr;

    UPROPERTY()
    UTexture* PosterTexture = nullptr;

    UPROPERTY()
    UTexture* PlaybackTexture = nullptr;

    FString ConfigRevision;
    FName ActiveTemplateId = TEXT("title_body");
    EOntoTwinGlassQuality RequestedGlassQuality = EOntoTwinGlassQuality::Balanced;
    TSharedPtr<FJsonObject> PendingData;
    bool bWorldSpacePresentation = false;
    bool bMediaAvailable = false;
    bool bMediaAutoplay = true;
    bool bMediaMuted = true;
    bool bMediaPlaying = false;
	bool bMediaExpanded = false;
	bool bPointerOverPanel = false;
	bool bFocusWithinPanel = false;
	bool bPanelOnline = true;
	bool bHasStatusAccent = false;
	FString MediaKind;
	FString MediaSourceRevision;
	FString PosterUrl;
	FString MediaStatusMessage;
	FString LastStatusLevel;
	FLinearColor CurrentStatusAccent = FLinearColor::Transparent;
	float CurrentRenderDensity = 1.0f;
	float BaseTopHighlightOpacity = 0.0f;
	float BaseFineNoiseOpacity = 0.0f;
	float InteractionProgress = 0.0f;
	float InteractionTarget = 0.0f;
	float VisualPhaseElapsed = 0.0f;
	float StatePulseElapsed = -1.0f;
	EOntoTwinOverlayVisualPhase VisualPhase = EOntoTwinOverlayVisualPhase::Opening;
	FTimerHandle VisualAnimationTimer;
	TFunction<void(EOntoTwinOverlayMediaAction)> MediaActionHandler;
	TFunction<void()> CloseAnimationCompletion;

    void BuildDefaultLayout();
    UTextBlock* CreateText(const FName Name, int32 FontSize, const FLinearColor& Color, bool bBold = false) const;
    void ApplyTemplateRecipe(const FString& TemplateId, bool bForce = false);
    float GetBasePanelWidth() const;
    float GetMinimumPanelHeight() const;
    void ApplyPresentationStyle();
	void ApplyPendingData();
	void LoadPoster(const FString& Url);
	void ApplyMediaVisual();
	void StartOpenAnimation();
	void EnsureVisualAnimationTimer();
	void TickVisualEffects();
	void ApplyTransientVisuals(float TransitionOpacity, float TransitionScale);
	bool IsReducedMotion() const;

    UFUNCTION()
    void HandlePosterDownloaded(UTexture2DDynamic* Texture);

    UFUNCTION()
    void HandlePosterFailed(UTexture2DDynamic* Texture);

    UFUNCTION()
    void HandlePlayPauseClicked();

    UFUNCTION()
    void HandleMuteClicked();

    UFUNCTION()
    void HandleExpandClicked();

    UFUNCTION()
    void HandleCloseClicked();

    UFUNCTION()
    void HandleRetryClicked();
};
