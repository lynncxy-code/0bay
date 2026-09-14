#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "OntoTwinCrosshairWidget.generated.h"

class UBorder;
class UCanvasPanel;

/**
 * 第一人称中心准星。
 *
 * 普通状态保持中性白色；瞄准配置了 selected 面板的标准实例时切换为青色，
 * 为射线选择提供不依赖宿主项目材质或后处理的跨项目反馈。
 */
UCLASS()
class ONTOTWINSYNC_API UOntoTwinCrosshairWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    void SetReticleState(bool bVisible, bool bInteractive);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;

private:
    UPROPERTY()
    TArray<UBorder*> ReticleSegments;

    bool bPendingVisible = false;
    bool bPendingInteractive = false;

    void BuildDefaultLayout();
    UBorder* AddSegment(
        UCanvasPanel* Canvas,
        const FName Name,
        const FVector2D& Position,
        const FVector2D& Size);
    void ApplyReticleState();
};
