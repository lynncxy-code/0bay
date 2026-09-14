#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UObject/NoExportTypes.h"
#include "UEAssetCatalogSyncComponent.generated.h"


/**
 * 项目级 UE 资产目录同步工具。
 *
 * 组件只负责枚举与上报，不负责做 CAD 匹配；推荐和人工确认由 OntoTwin Web 端完成。
 */
UCLASS(ClassGroup=(DigitalTwin), meta=(BlueprintSpawnableComponent, DisplayName="UE Asset Catalog Sync"))
class ONTOTWINSYNC_API UOntoTwinUEAssetCatalogSyncComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UOntoTwinUEAssetCatalogSyncComponent();

    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    /** 兼容入口：PIE / 运行时启动时按“扫描根目录”主动同步一次。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="资产目录",
              meta=(DisplayName="启动时同步资产目录"))
    bool bSyncOnBeginPlay = false;

    /** 接收 OntoTwin Web 端发起的目录扫描请求；编辑器、PIE 和运行时均可轮询。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="资产目录|按需同步",
              meta=(DisplayName="启用 Web 按需目录同步"))
    bool bEnableOnDemandCatalog = true;

    /** Web 按需扫描请求的轮询间隔。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="资产目录|按需同步",
              meta=(DisplayName="扫描请求轮询间隔（秒）", ClampMin="1.0", UIMin="1.0"))
    float ScanRequestPollIntervalSeconds = 2.0f;

    /** 向 OntoTwin 刷新 /Game 轻量目录树的间隔，不上传模型内容。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="资产目录|按需同步",
              meta=(DisplayName="目录树刷新间隔（秒）", ClampMin="15.0", UIMin="15.0"))
    float FolderIndexRefreshIntervalSeconds = 60.0f;

    /**
     * 要递归扫描的 UE 内容根目录。每一项既可从 Content Browser 选择，也可直接填写
     * /Game/... 长包路径；不要求资产位于任何固定业务目录下。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="资产目录",
              meta=(DisplayName="扫描根目录（可选择或填写）", LongPackageName))
    TArray<FDirectoryPath> AssetRoots;

    /** 同步 Blueprint / SkeletalMesh；Blueprint 是否可运行由生成类校验结果决定。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="资产目录",
              meta=(DisplayName="同步蓝图和骨骼网格"))
    bool bIncludeUnsupportedKinds = true;

    /** 读取 UE 编辑器已经缓存到资产包中的缩略图；无缩略图时不阻断同步。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="资产目录",
              meta=(DisplayName="同步编辑器缩略图"))
    bool bIncludeEditorThumbnails = true;

    /** 为 StaticMesh/SkeletalMesh 加载 bounds，供 CAD 尺寸匹配；目录较大时可关闭。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="资产目录",
              meta=(DisplayName="同步静态网格尺寸"))
    bool bIncludeStaticMeshBounds = true;

    UFUNCTION(CallInEditor, BlueprintCallable, Category="资产目录",
              meta=(DisplayName="同步 UE 资产目录"))
    void SyncCatalog();

    /** 只上报 /Game 的目录路径，供 OntoTwin 选择按需扫描范围。 */
    UFUNCTION(CallInEditor, BlueprintCallable, Category="资产目录|按需同步",
              meta=(DisplayName="刷新 OntoTwin 可选目录"))
    void PublishFolderIndex();

    /** 把 /Game 扫描根目录显式加入宿主项目 Cook 配置。 */
    UFUNCTION(CallInEditor, BlueprintCallable, Category="资产目录",
              meta=(DisplayName="配置扫描目录用于打包"))
    void ConfigureAssetRootsForCook();

private:
    bool bRequestInFlight = false;
    bool bFolderIndexRequestInFlight = false;
    bool bScanPollRequestInFlight = false;
    bool bFolderIndexPublished = false;
    float ScanRequestPollElapsed = 0.0f;
    float FolderIndexRefreshElapsed = 0.0f;
    FString ActiveScanRequestId;

    void PollScanRequest();
    void SyncCatalogForRoots(const TArray<FString>& RequestedRoots, const FString& ScanRequestId);
    void ReportScanRequestFailure(const FString& ScanRequestId, const FString& ErrorMessage);
};
