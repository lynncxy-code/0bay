#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GrandNeoBayWidget.generated.h"

class AGrandNeoBayExperience;
class UCanvasPanel;
class UBorder;
class UBackgroundBlur;
class UButton;
class UTextBlock;

UCLASS()
class OFFICEHOST_API UGrandNeoBayWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void SetExperience(AGrandNeoBayExperience* InExperience);
    void Refresh();
    /** Used only by -GNBTest: button delegates and native scene pointer events. */
    bool TestActivateControl(FName Control, FVector2D ViewportPoint = FVector2D::ZeroVector);
protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& Event) override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
private:
    UPROPERTY(Transient) TObjectPtr<AGrandNeoBayExperience> Experience;
    UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Canvas;
    UPROPERTY(Transient) TObjectPtr<UBorder> SceneSurface;
    UPROPERTY(Transient) TObjectPtr<UBackgroundBlur> CardGlass;
    UPROPERTY(Transient) TObjectPtr<UBorder> Card;
    UPROPERTY(Transient) TObjectPtr<UButton> EnterButton;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Status;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> CardTitle;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> CardDescription;
    bool bOrbiting = false;
    bool IsPointerOverCard(FVector2D AbsolutePosition) const;
    void UpdatePreviewPointer(const FGeometry& Geometry, FVector2D AbsolutePosition);
    UFUNCTION() void Enter();
    UFUNCTION() void CloseCard();
};
