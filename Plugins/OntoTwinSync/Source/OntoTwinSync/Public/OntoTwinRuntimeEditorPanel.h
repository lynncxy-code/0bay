#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TimerManager.h"
#include "Types/SlateEnums.h"
#include "OntoTwinRuntimeEditorPanel.generated.h"

class ATwinSceneManager;
class SWidget;
class UButton;
class UBorder;
class UCheckBox;
class UComboBoxString;
class UEditableTextBox;
class USizeBox;
class UTextBlock;
class UVerticalBox;
class UWidget;

enum class EOntoTwinRuntimePanelConfirmation : uint8
{
    None,
    ExitEditor,
    CancelAll
};

enum class EOntoTwinRuntimeToastType : uint8
{
    Info,
    Success,
    Warning,
    Error
};

UCLASS()
class ONTOTWINSYNC_API UOntoTwinRuntimeEditorPanel : public UUserWidget
{
    GENERATED_BODY()

public:
    void SetSceneManager(ATwinSceneManager* InSceneManager);
    void RefreshFromManager();
    void ShowToast(const FString& Message, EOntoTwinRuntimeToastType Type);
    void ShowExitConfirmation();
    void ShowCancelAllConfirmation();
    bool IsPointerOverPanel() const;
    bool IsConfirmationOpen() const;

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
    UPROPERTY()
    ATwinSceneManager* SceneManager = nullptr;

    UPROPERTY()
    UBorder* PanelBorder = nullptr;

    UPROPERTY()
    USizeBox* PanelBounds = nullptr;

    UPROPERTY()
    UTextBlock* HeaderStateText = nullptr;

    UPROPERTY()
    UTextBlock* DisplayNameText = nullptr;

    UPROPERTY()
    UButton* DisplayNameCopyButton = nullptr;

    UPROPERTY()
    UTextBlock* TypeNameText = nullptr;

    UPROPERTY()
    UButton* TypeNameCopyButton = nullptr;

    UPROPERTY()
    UTextBlock* InstanceIdText = nullptr;

    UPROPERTY()
    UTextBlock* XValueText = nullptr;

    UPROPERTY()
    UTextBlock* YValueText = nullptr;

    UPROPERTY()
    UTextBlock* ZValueText = nullptr;

    UPROPERTY()
    UTextBlock* YawValueText = nullptr;

    UPROPERTY()
    UTextBlock* YawLabelText = nullptr;

    UPROPERTY()
    UBorder* AccessStatusDot = nullptr;

    UPROPERTY()
    UTextBlock* AccessStatusText = nullptr;

    UPROPERTY()
    UButton* AccessActionButton = nullptr;

    UPROPERTY()
    UTextBlock* AccessActionLabel = nullptr;

    UPROPERTY()
    UButton* SaveButton = nullptr;

    UPROPERTY()
    UTextBlock* SaveButtonLabel = nullptr;

    UPROPERTY()
    UButton* CancelButton = nullptr;

    UPROPERTY()
    UButton* CloseButton = nullptr;

    UPROPERTY()
    UButton* UndoButton = nullptr;

    UPROPERTY()
    UButton* RedoButton = nullptr;

    UPROPERTY()
    UButton* SceneTabButton = nullptr;

    UPROPERTY()
    UButton* BusinessTabButton = nullptr;

    UPROPERTY()
    UTextBlock* SceneTabLabel = nullptr;

    UPROPERTY()
    UTextBlock* BusinessTabLabel = nullptr;

    UPROPERTY()
    UVerticalBox* SceneContent = nullptr;

    UPROPERTY()
    UVerticalBox* BusinessContent = nullptr;

    UPROPERTY()
    UTextBlock* BusinessSelectionText = nullptr;

    UPROPERTY()
    UComboBoxString* BusinessSelector = nullptr;

    // The combo menu owns only Slate rows; retain each generated UTextBlock
    // so its font/brush data remains valid across GC and window focus changes.
    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> RetainedComboTextWidgets;

    UPROPERTY()
    UEditableTextBox* BusinessNameInput = nullptr;

    UPROPERTY()
    UButton* CreateBusinessButton = nullptr;

    UPROPERTY()
    UButton* RemoveButton = nullptr;

    UPROPERTY()
    UButton* PendingToggleButton = nullptr;

    UPROPERTY()
    UTextBlock* PendingToggleLabel = nullptr;

    UPROPERTY()
    UVerticalBox* PendingList = nullptr;

    UPROPERTY()
    UCheckBox* WallSnapCheckBox = nullptr;

    UPROPERTY()
    UCheckBox* GridSnapCheckBox = nullptr;

    UPROPERTY()
    UBorder* ToastBorder = nullptr;

    UPROPERTY()
    UBorder* ToastAccent = nullptr;

    UPROPERTY()
    UTextBlock* ToastText = nullptr;

    UPROPERTY()
    UBorder* ConfirmationOverlay = nullptr;

    UPROPERTY()
    UTextBlock* ConfirmationTitleText = nullptr;

    UPROPERTY()
    UTextBlock* ConfirmationBodyText = nullptr;

    UPROPERTY()
    UButton* ConfirmationPrimaryButton = nullptr;

    UPROPERTY()
    UTextBlock* ConfirmationPrimaryLabel = nullptr;

    UPROPERTY()
    UButton* ConfirmationSecondaryButton = nullptr;

    UPROPERTY()
    UTextBlock* ConfirmationSecondaryLabel = nullptr;

    UPROPERTY()
    UButton* ConfirmationContinueButton = nullptr;

    UTextBlock* ConfirmationContinueLabel = nullptr;

    FTimerHandle ToastTimerHandle;
    EOntoTwinRuntimePanelConfirmation ConfirmationMode = EOntoTwinRuntimePanelConfirmation::None;
    bool bPendingListExpanded = false;
    bool bBusinessTabActive = false;
    bool bRefreshingBusinessSelector = false;
    FString BusinessOptionsSignature;
    TArray<FString> BusinessOptionIds;
    TArray<FString> BusinessOptionLabels;
    TArray<FString> BusinessOptionDisplayLabels;

    void BuildDefaultLayout();
    UTextBlock* CreateText(const FName Name, const FString& InitialText, int32 FontSize,
        const FLinearColor& Color, bool bBold = false) const;
    UButton* CreateButton(const FName Name, const FString& Label, UTextBlock*& OutLabel,
        bool bPrimary) const;
    void HideToast();
    void HideConfirmation();
    void RefreshPendingList();
    void SetActiveTab(bool bBusiness);
    void RefreshBusinessEditor();

    UFUNCTION()
    void HandleAccessActionClicked();

    UFUNCTION()
    void HandleDisplayNameCopyClicked();

    UFUNCTION()
    void HandleTypeNameCopyClicked();

    UFUNCTION()
    void HandleCloseClicked();

    UFUNCTION()
    void HandleSaveClicked();

    UFUNCTION()
    void HandleCancelClicked();

    UFUNCTION()
    void HandleUndoClicked();

    UFUNCTION()
    void HandleRedoClicked();

    UFUNCTION()
    void HandleSceneTabClicked();

    UFUNCTION()
    void HandleBusinessTabClicked();

    UFUNCTION()
    void HandleBusinessSelected(FString SelectedItem, ESelectInfo::Type SelectionType);

    UFUNCTION()
    UWidget* GenerateBusinessOptionWidget(FString Item);

    UFUNCTION()
    void HandleCreateBusinessClicked();

    UFUNCTION()
    void HandleRemoveClicked();

    UFUNCTION()
    void HandlePendingToggleClicked();

    UFUNCTION()
    void HandleConfirmationPrimaryClicked();

    UFUNCTION()
    void HandleConfirmationSecondaryClicked();

    UFUNCTION()
    void HandleConfirmationContinueClicked();

    UFUNCTION()
    void HandleWallSnapChanged(bool bIsChecked);

    UFUNCTION()
    void HandleGridSnapChanged(bool bIsChecked);
};
