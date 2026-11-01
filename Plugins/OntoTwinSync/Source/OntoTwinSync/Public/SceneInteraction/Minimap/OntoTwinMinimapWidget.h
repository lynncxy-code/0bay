#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "SceneInteraction/Minimap/TwinMinimapTypes.h"
#include "Types/SlateEnums.h"
#include "OntoTwinMinimapWidget.generated.h"

class UImage;
class UBorder;
class UButton;
class USizeBox;
class UTextBlock;
class UTextureRenderTarget2D;
class UTwinInteractionManagerComponent;
class UOverlaySlot;
class FSlateRect;

/** Screen Space map surface with a sharp marker and one explicit collapse control. */
UCLASS()
class ONTOTWINSYNC_API UOntoTwinMinimapWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    void SetInteractionManager(UTwinInteractionManagerComponent* InManager);
    void SetMapTexture(UTextureRenderTarget2D* InTexture, const FIntPoint& InCaptureSize);
    void SetMarker(const FVector2D& InUV, float InAngleDegrees, bool bInOffMap);
    void SetTeleportFeedback(
        const FVector2D& InUV,
        ETwinMinimapTeleportFeedback InFeedback,
        const FString& InMessage);
    void SetUndoAvailable(bool bAvailable);
    void ClearTeleportFeedback();
    void HideMarker();
    void ClearMap();

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
    virtual FReply NativeOnMouseButtonDown(
        const FGeometry& InGeometry,
        const FPointerEvent& InMouseEvent) override;
    virtual FReply NativeOnMouseButtonDoubleClick(
        const FGeometry& InGeometry,
        const FPointerEvent& InMouseEvent) override;
    virtual FReply NativeOnTouchStarted(
        const FGeometry& InGeometry,
        const FPointerEvent& InGestureEvent) override;
    virtual int32 NativePaint(
        const FPaintArgs& Args,
        const FGeometry& AllottedGeometry,
        const FSlateRect& MyCullingRect,
        FSlateWindowElementList& OutDrawElements,
        int32 LayerId,
        const FWidgetStyle& InWidgetStyle,
        bool bParentEnabled) const override;

private:
    friend class FOntoTwinRecoveredUiTest;
    enum class EMinimapGrowthDirection : uint8
    {
        LeftDown,
        RightDown,
        LeftUp,
        RightUp,
    };

    UPROPERTY()
    UTwinInteractionManagerComponent* Manager = nullptr;

    UPROPERTY()
    USizeBox* RootBounds = nullptr;

    UPROPERTY()
    UBorder* MapShell = nullptr;

    UPROPERTY()
    UImage* MapImage = nullptr;

    UPROPERTY()
    UButton* ToggleButton = nullptr;

    UPROPERTY()
    UButton* DisplaySchemeButton = nullptr;
    UPROPERTY()
    UButton* DisplaySchemeButtonTwo = nullptr;
    UPROPERTY()
    UButton* DisplaySchemeButtonThree = nullptr;
    UPROPERTY()
    UTextBlock* DisplaySchemeText = nullptr;
    UPROPERTY()
    UTextBlock* DisplaySchemeTextTwo = nullptr;
    UPROPERTY()
    UTextBlock* DisplaySchemeTextThree = nullptr;
    UOverlaySlot* DisplaySchemeSlot = nullptr;
    UOverlaySlot* DisplaySchemeSlotTwo = nullptr;
    UOverlaySlot* DisplaySchemeSlotThree = nullptr;
    void RefreshDisplaySchemeControl();
    UFUNCTION()
    void OnSelectDisplaySchemeOne();
    UFUNCTION()
    void OnSelectDisplaySchemeTwo();
    UFUNCTION()
    void OnSelectDisplaySchemeThree();

    /** Non-interactive high-contrast focus ring kept separate from the button. */
    UPROPERTY()
    UBorder* ToggleFocusRing = nullptr;

    UPROPERTY()
    UBorder* TeleportStatusPanel = nullptr;

    UPROPERTY()
    UTextBlock* TeleportStatusText = nullptr;

    UPROPERTY()
    UButton* UndoButton = nullptr;

    UPROPERTY()
    UTextureRenderTarget2D* MapTexture = nullptr;

    FVector2D ContentSize = FVector2D(320.0f, 240.0f);
    FVector2D MarkerUV = FVector2D(0.5f, 0.5f);
    float MarkerAngleDegrees = 0.0f;
    float MarkerPulsePhase = 0.0f;
    float ExpandAlpha = 1.0f;
    float FeedbackSecondsRemaining = 0.0f;
    double LastTouchSeconds = -1.0;
    FVector2D TeleportTargetUV = FVector2D(0.5f, 0.5f);
    FVector2D LastTouchLocal = FVector2D::ZeroVector;
    ETwinMinimapTeleportFeedback TeleportFeedback =
        ETwinMinimapTeleportFeedback::Hidden;
    bool bMarkerVisible = false;
    bool bMarkerOffMap = false;
    bool bTeleportTargetVisible = false;
    bool bExpanded = true;
    bool bHasStableButtonAnchor = false;
    bool bHasViewportMetrics = false;
    EMinimapGrowthDirection GrowthDirection = EMinimapGrowthDirection::LeftDown;
    FVector2D ViewportLogicalSize = FVector2D::ZeroVector;
    FVector2D StableButtonTopLeft = FVector2D::ZeroVector;
    FVector2D ButtonEdgeInsets = FVector2D(24.0f, 24.0f);
    bool bAnchorRight = true;
    bool bAnchorBottom = false;
    FVector2D PanelTopLeft = FVector2D::ZeroVector;
    FVector2D PanelLogicalSize = FVector2D::ZeroVector;
    FVector2D PanelPivot = FVector2D(1.0f, 0.0f);
    FVector2D LastViewportLogicalSize = FVector2D::ZeroVector;
    UOverlaySlot* MapShellSlot = nullptr;
    UOverlaySlot* ToggleSlot = nullptr;
    UOverlaySlot* FocusRingSlot = nullptr;

    void BuildDefaultLayout();
    void ApplyMapTexture();
    void ApplyExpansionVisuals();
    void UpdateViewportLayout(const FGeometry& MyGeometry);
    void CaptureStableButtonAnchor(const FGeometry& MyGeometry);
    void ResolveGrowthDirection();
    void ApplyOverlayGeometry();
    void UpdateToggleSemantics();
    FSlateRect GetAnimatedPanelRect() const;
    FVector2D GetButtonCenter() const;
    static float RectOverflow(const FSlateRect& Rect, const FSlateRect& SafeRect);
    static float RectVisibleArea(const FSlateRect& Rect, const FSlateRect& ViewRect);
    static bool IsHorizontalRight(EMinimapGrowthDirection Direction);
    static bool IsVerticalDown(EMinimapGrowthDirection Direction);
    static FVector2D DirectionVector(EMinimapGrowthDirection Direction);
    bool TryGetMapUV(
        const FGeometry& Geometry,
        const FVector2D& ScreenPosition,
        FVector2D& OutUV) const;
    void HandleMapPreview(const FVector2D& UV);
    void HandleMapTeleport(const FVector2D& UV);

    UFUNCTION()
    void OnToggleExpanded();

    UFUNCTION()
    void OnUndoTeleport();
};
