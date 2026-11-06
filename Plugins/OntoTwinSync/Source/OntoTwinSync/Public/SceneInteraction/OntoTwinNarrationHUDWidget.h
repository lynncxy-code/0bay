#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "OntoTwinNarrationHUDWidget.generated.h"

class UBorder;
class UButton;
class UOverlay;
class USizeBox;
class UTextBlock;
class UTwinInteractionManagerComponent;

/** Bottom-center screen-space narration surface. Only the skip button is hit-testable. */
UCLASS()
class ONTOTWINSYNC_API UOntoTwinNarrationHUDWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    void SetInteractionManager(UTwinInteractionManagerComponent* InManager);
    void ShowSegment(
        const FString& Text,
        int32 SegmentIndex,
        int32 SegmentCount,
        const FString& Mode,
        bool bAudioFallback,
        bool bShowText = true);
    void HideNarration();

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;

private:
    UPROPERTY()
    UTwinInteractionManagerComponent* Manager = nullptr;

    UPROPERTY()
    USizeBox* Bounds = nullptr;

    UPROPERTY()
    UOverlay* Surface = nullptr;

    UPROPERTY()
    UTextBlock* ProgressText = nullptr;

    UPROPERTY()
    UTextBlock* BodyText = nullptr;

    UPROPERTY()
    UButton* SkipButton = nullptr;

    void BuildDefaultLayout();

    UFUNCTION()
    void OnSkipClicked();
};
