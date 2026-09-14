#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SceneInteraction/Minimap/TwinMinimapTypes.h"
#include "Types/SlateEnums.h"
#include "OntoTwinRoamingHUDWidget.generated.h"

class UBorder;
class UButton;
class UComboBoxString;
class UOntoTwinMinimapWidget;
class USizeBox;
class UTextBlock;
class UTextureRenderTarget2D;
class UVerticalBox;
class UTwinInteractionManagerComponent;

/**
 * Performance 级漫游 HUD：无底板状态文字、按键胶囊提示和 Tab 操作抽屉。
 * 静态深灰半透明表面不依赖 Blur/Postbuffer，装饰层不参与命中测试。
 */
UCLASS()
class ONTOTWINSYNC_API UOntoTwinRoamingHUDWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    void SetInteractionManager(UTwinInteractionManagerComponent* InManager);
    void RefreshFromManager();
    void SetInteractionOpen(bool bOpen);
    void SetMinimapTexture(UTextureRenderTarget2D* Texture, const FIntPoint& CaptureSize);
    void SetMinimapMarker(const FVector2D& UV, float AngleDegrees, bool bOffMap);
    void SetMinimapTeleportFeedback(
        const FVector2D& UV,
        ETwinMinimapTeleportFeedback Feedback,
        const FString& Message);
    void SetMinimapUndoAvailable(bool bAvailable);
    void ClearMinimapTeleportFeedback();
    void HideMinimapMarker();
    void ClearMinimap();

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    UPROPERTY()
    UTwinInteractionManagerComponent* Manager = nullptr;

    UPROPERTY()
    UTextBlock* StatusText = nullptr;

    UPROPERTY()
    USizeBox* StatusPulse = nullptr;

    UPROPERTY()
    UVerticalBox* HintList = nullptr;

    UPROPERTY()
    UTextBlock* DetailText = nullptr;

    UPROPERTY()
    UBorder* DetailPanel = nullptr;

    UPROPERTY()
    UTextBlock* DrawerTitle = nullptr;

    UPROPERTY()
    UWidget* RoamingRouteRow = nullptr;

    UPROPERTY()
    UWidget* RoamingCharacterRow = nullptr;

    UPROPERTY()
    UWidget* RoamingViewModes = nullptr;

    UPROPERTY()
    UWidget* RoamingActions = nullptr;

    UPROPERTY()
    UOntoTwinMinimapWidget* MinimapWidget = nullptr;

    UPROPERTY()
    UComboBoxString* RouteSelector = nullptr;

    UPROPERTY()
    UComboBoxString* CharacterSelector = nullptr;

    UPROPERTY()
    UButton* HomeTabButton = nullptr;

    UPROPERTY()
    UButton* ZoneTabButton = nullptr;

    UPROPERTY()
    UButton* BusinessTabButton = nullptr;

    UPROPERTY()
    UButton* RoamingTabButton = nullptr;

    UPROPERTY()
    UWidget* ZonePanel = nullptr;

    UPROPERTY()
    UWidget* BusinessPanel = nullptr;

    UPROPERTY()
    UWidget* RoamingPanel = nullptr;

    UPROPERTY()
    UComboBoxString* WebZoneSelector = nullptr;

    UPROPERTY()
    UComboBoxString* WebBusinessSelector = nullptr;

    UPROPERTY()
    UComboBoxString* WebBusinessZoneSelector = nullptr;

    // UComboBoxString keeps only the generated Slate widget. Retain the
    // backing UTextBlock objects so GC cannot invalidate Slate brush pointers.
    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> RetainedComboTextWidgets;

    UPROPERTY()
    UButton* FirstPersonButton = nullptr;

    UPROPERTY()
    UButton* ShoulderButton = nullptr;

    UPROPERTY()
    UButton* GlobalButton = nullptr;

    FString ShortcutSignature;
    FString RouteSignature;
    FString CharacterSignature;
    FString WebCatalogSignature;
    TArray<FString> RouteOptionIds;
    TArray<FString> RouteOptionLabels;
    TArray<FString> CharacterOptionIds;
    TArray<FString> CharacterOptionLabels;
    TArray<FString> WebZoneOptionIds;
    TArray<FString> WebZoneOptionLabels;
    TArray<FString> WebBusinessOptionIds;
    TArray<FString> WebBusinessOptionLabels;
    TArray<FString> WebBusinessZoneOptionIds;
    TArray<FString> WebBusinessZoneOptionLabels;
    bool bRefreshingRouteSelector = false;
    bool bRefreshingCharacterSelector = false;
    bool bRefreshingWebSelectors = false;
    int32 ActiveDockTab = 1;
    float StatusPulsePhase = 0.0f;

    void BuildDefaultLayout();
    UBorder* BuildGlassSurface(
        const FName Name,
        const FMargin& SurfacePadding,
        float Radius);
    void RefreshShortcutList();
    void RefreshRouteSelector();
    void RefreshCharacterSelector();
    void RefreshWebSelectors();
    void SetDockTab(int32 TabIndex);
    void AddShortcutRow(int32 Index, const FString& Key, const FString& Description);
    UButton* AddActionButton(UVerticalBox* Parent, const FName Name, const FString& Label);

    UFUNCTION()
    UWidget* GenerateRouteOptionWidget(FString Item);

    UFUNCTION()
    void OnCycleSkin();

    UFUNCTION()
    void OnResumeRoute();

    UFUNCTION()
    void OnRestartRoute();

    UFUNCTION()
    void OnReloadCharacter();

    UFUNCTION()
    void OnFirstPerson();

    UFUNCTION()
    void OnShoulder();

    UFUNCTION()
    void OnGlobal();

    UFUNCTION()
    void OnRouteSelected(FString SelectedItem, ESelectInfo::Type SelectionType);

    UFUNCTION()
    void OnCharacterSelected(FString SelectedItem, ESelectInfo::Type SelectionType);

    UFUNCTION()
    void OnHomeTab();

    UFUNCTION()
    void OnZoneTab();

    UFUNCTION()
    void OnBusinessTab();

    UFUNCTION()
    void OnRoamingTab();

    UFUNCTION()
    void OnWebZoneSelected(FString SelectedItem, ESelectInfo::Type SelectionType);

    UFUNCTION()
    void OnWebBusinessSelected(FString SelectedItem, ESelectInfo::Type SelectionType);

    UFUNCTION()
    void OnWebBusinessScopeSelected(FString SelectedItem, ESelectInfo::Type SelectionType);
};
