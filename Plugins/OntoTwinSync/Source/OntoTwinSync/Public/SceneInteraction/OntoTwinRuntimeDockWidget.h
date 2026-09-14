#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Types/SlateEnums.h"
#include "OntoTwinRuntimeDockWidget.generated.h"

class UButton;
class UBorder;
class UCanvasPanelSlot;
class UComboBoxString;
class UHorizontalBox;
class UOntoTwinRuntimeDockWidget;
class UScrollBox;
class USizeBox;
class UTextBlock;
class UVerticalBox;
class UWidgetSwitcher;
class UTwinInteractionManagerComponent;

enum class EOntoTwinRuntimeDockIcon : uint8
{
    Home,
    DrawerUp,
    DrawerDown,
    ViewGlobal,
    ViewShoulder,
    ViewFirstPerson,
    Crosshair,
    Skin,
    ReturnRoute,
    RestartRoute,
    ReloadCharacter
};

/** A single-stroke icon renderer shared by the runtime Dock controls. */
UCLASS()
class ONTOTWINSYNC_API UOntoTwinRuntimeDockIconWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    void SetIcon(EOntoTwinRuntimeDockIcon InIcon);

protected:
    virtual int32 NativePaint(
        const FPaintArgs& Args,
        const FGeometry& AllottedGeometry,
        const FSlateRect& MyCullingRect,
        FSlateWindowElementList& OutDrawElements,
        int32 LayerId,
        const FWidgetStyle& InWidgetStyle,
        bool bParentEnabled) const override;

private:
    EOntoTwinRuntimeDockIcon Icon = EOntoTwinRuntimeDockIcon::Home;
};

enum class EOntoTwinRuntimeDockAction : uint8
{
    ToggleDock,
    Home,
    ToggleRuntimeEditor,
    TabSpace,
    TabBusiness,
    TabRoaming,
    SelectZone,
    EnterZone,
    OpenBusiness,
    ScopeAll,
    ScopeCurrent,
    CameraGlobal,
    CameraShoulder,
    CameraFirstPerson,
    ToggleCrosshair,
    CycleSkin,
    ResumeRoute,
    RestartRoute,
    ReloadCharacter
};

/** Button with a small runtime payload, used by dynamically built tree/list rows. */
UCLASS()
class ONTOTWINSYNC_API UOntoTwinRuntimeDockButton : public UButton
{
    GENERATED_BODY()

public:
    void Configure(
        UOntoTwinRuntimeDockWidget* InOwner,
        EOntoTwinRuntimeDockAction InAction,
        const FString& InPayload = FString(),
        int32 InDepth = INDEX_NONE);
    void SetAccessibleLabel(const FText& InLabel);

private:
    UPROPERTY()
    UOntoTwinRuntimeDockWidget* DockOwner = nullptr;

    EOntoTwinRuntimeDockAction DockAction = EOntoTwinRuntimeDockAction::ToggleDock;
    FString Payload;
    int32 Depth = INDEX_NONE;

    UFUNCTION()
    void HandleClicked();
};

/**
 * Screen-space runtime Dock. All three tabs live in one fixed-height switcher,
 * while a single shared liquid-glass surface provides the backdrop.
 */
UCLASS()
class ONTOTWINSYNC_API UOntoTwinRuntimeDockWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    void SetInteractionManager(UTwinInteractionManagerComponent* InManager);
    void RefreshFromManager();
    void SetDockOpen(bool bOpen);
    void HandleDockAction(
        EOntoTwinRuntimeDockAction Action,
        const FString& Payload,
        int32 Depth);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    UPROPERTY()
    UTwinInteractionManagerComponent* Manager = nullptr;

    UPROPERTY()
    USizeBox* DockShell = nullptr;

    UPROPERTY()
    USizeBox* DockTrigger = nullptr;

    UPROPERTY()
    UOntoTwinRuntimeDockButton* DockTriggerButton = nullptr;

    UPROPERTY()
    UBorder* DockTriggerFocusRing = nullptr;

    UPROPERTY()
    UCanvasPanelSlot* DockTriggerSlot = nullptr;

    UPROPERTY()
    UCanvasPanelSlot* DockShellSlot = nullptr;

    UPROPERTY()
    UOntoTwinRuntimeDockIconWidget* DockTriggerIcon = nullptr;

    UPROPERTY()
    UWidgetSwitcher* ContentSwitcher = nullptr;

    UPROPERTY()
    UOntoTwinRuntimeDockButton* SpaceTabButton = nullptr;

    UPROPERTY()
    UOntoTwinRuntimeDockButton* BusinessTabButton = nullptr;

    UPROPERTY()
    UOntoTwinRuntimeDockButton* RoamingTabButton = nullptr;

    UPROPERTY()
    UOntoTwinRuntimeDockButton* SceneEditButton = nullptr;

    UPROPERTY()
    UTextBlock* SpaceBreadcrumb = nullptr;

    UPROPERTY()
    UScrollBox* SpaceColumnHost = nullptr;

    UPROPERTY()
    UOntoTwinRuntimeDockButton* EnterSpaceButton = nullptr;

    UPROPERTY()
    UVerticalBox* BusinessList = nullptr;

    UPROPERTY()
    UTextBlock* BusinessScopeText = nullptr;

    UPROPERTY()
    UOntoTwinRuntimeDockButton* ScopeAllButton = nullptr;

    UPROPERTY()
    UOntoTwinRuntimeDockButton* ScopeCurrentButton = nullptr;

    UPROPERTY()
    UComboBoxString* CharacterSelector = nullptr;

    UPROPERTY()
    UComboBoxString* RouteSelector = nullptr;

    // Generated combo rows are not part of the WidgetTree hierarchy. Keep
    // their UObject owners alive for as long as Slate can render the rows.
    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> RetainedComboTextWidgets;

    UPROPERTY()
    UOntoTwinRuntimeDockButton* CameraGlobalButton = nullptr;

    UPROPERTY()
    UOntoTwinRuntimeDockButton* CameraShoulderButton = nullptr;

    UPROPERTY()
    UOntoTwinRuntimeDockButton* CameraFirstPersonButton = nullptr;

    UPROPERTY()
    UOntoTwinRuntimeDockButton* CrosshairButton = nullptr;

    UPROPERTY()
    USizeBox* ReloadCharacterBounds = nullptr;

    UPROPERTY()
    USizeBox* QuickActionBounds = nullptr;

    UPROPERTY()
    UVerticalBox* RoamingUnavailable = nullptr;

    UPROPERTY()
    UHorizontalBox* RoamingControls = nullptr;

    TArray<FString> ZoneIds;
    TArray<FString> ZoneNames;
    TArray<FString> ZoneParentIds;
    TArray<FString> SelectedZonePath;
    TArray<FString> BusinessIds;
    TArray<FString> BusinessNames;
    TArray<int32> BusinessMemberCounts;
    TArray<FString> CharacterIds;
    TArray<FString> CharacterLabels;
    TArray<FString> RouteIds;
    TArray<FString> RouteLabels;
    FString SelectedBusinessZoneId;
    FString ZoneCatalogSignature;
    FString BusinessCatalogSignature;
    FString CharacterSignature;
    FString RouteSignature;
    int32 ActiveTabIndex = 0;
    bool bBusinessScopeUsesCurrent = false;
    bool bRefreshingSelectors = false;
    bool bDockOpen = false;
    bool bDockAnimationActive = false;
    bool bDrawerFocusVisible = false;
    float DockAnimationProgress = 0.0f;
    float DockAnimationStartProgress = 0.0f;
    float DockAnimationTargetProgress = 0.0f;
    float DockAnimationElapsed = 0.0f;
    float DockAnimationDuration = 0.0f;

    void BuildDefaultLayout();
    void BuildSpacePanel();
    void BuildBusinessPanel();
    void BuildRoamingPanel();
    void BuildSpaceColumns();
    void BuildBusinessRows();
    void RefreshSpaceCatalog();
    void RefreshBusinessCatalog();
    void RefreshCharacterSelector();
    void RefreshRouteSelector();
    void UpdateBusinessScope();
    void UpdateTabStyles();
    void UpdateRoamingState();
    void SetActiveTab(int32 TabIndex);
    void ApplyDockVisualState(float OpenProgress);
    void UpdateDockTriggerAccessibility();
    void UpdateDockTriggerFocusVisual();

    UOntoTwinRuntimeDockButton* MakeButton(
        const FName Name,
        const FString& Label,
        EOntoTwinRuntimeDockAction Action,
        const FString& Payload = FString(),
        int32 Depth = INDEX_NONE,
        bool bCompact = false);
    UTextBlock* MakeText(
        const FName Name,
        const FString& Text,
        float Size,
        bool bSemibold,
        const FLinearColor& Color);

    UFUNCTION()
    UWidget* GenerateSelectorItem(FString Item);

    UFUNCTION()
    void OnCharacterSelected(FString SelectedItem, ESelectInfo::Type SelectionType);

    UFUNCTION()
    void OnRouteSelected(FString SelectedItem, ESelectInfo::Type SelectionType);
};
