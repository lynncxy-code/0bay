#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "OntoTwinModelLoadingWidget.generated.h"

class UProgressBar;
class UTextBlock;

/**
 * 全屏启动加载层。由 ATwinSceneManager 持有，直到首轮模型基线完成。
 * 该层不接管输入，只负责向操作者说明连接、加载和重试状态。
 */
UCLASS()
class ONTOTWINSYNC_API UOntoTwinModelLoadingWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    void ShowWaiting(const FString& Detail);
    void ShowProgress(int32 Completed, int32 Total);
    void ShowFailure(const FString& Detail);
    void ShowComplete(int32 Total);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    enum class ELoadingVisualPhase : uint8
    {
        Waiting,
        Applying,
        Completing,
        Failed,
        Finished,
    };

    UPROPERTY()
    UTextBlock* TitleText = nullptr;

    UPROPERTY()
    UTextBlock* DetailText = nullptr;

    UPROPERTY()
    UTextBlock* PercentText = nullptr;

    UPROPERTY()
    UProgressBar* ProgressBar = nullptr;

    ELoadingVisualPhase VisualPhase = ELoadingVisualPhase::Waiting;
    float DisplayedPercent = 0.0f;
    float TargetPercent = 0.0f;
    int32 LatestCompleted = 0;
    int32 LatestTotal = 0;

    void BuildDefaultLayout();
    void RefreshProgressText();
    void SetState(
        const FString& Title,
        const FString& Detail,
        float Percent,
        bool bMarquee,
        const FLinearColor& Accent);
};
