#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Dom/JsonObject.h"
#include "Interfaces/IHttpRequest.h"
#include "WebInteraction/OntoTwinWebHostWidget.h"
#include "OntoTwinWebInteractionComponent.generated.h"

class APlayerController;
class ATwinInstance;
class ATwinSceneManager;
class UOntoTwinWebBridge;

struct FOntoTwinWebNavigationFrame
{
    FString PageId;
    FString FinalUrl;
    FString Trigger;
    TSharedPtr<FJsonObject> Context;
    TSet<FString> VisibleInstanceIds;
    TSet<FString> FocusInstanceIds;
    bool bSceneScopeActive = false;
};

enum class EOntoTwinBusinessMembershipState : uint8
{
    None,
    Mixed,
    All
};

/** OntoTwin 3.8 runtime projection, resolver, singleton browser and scene linkage. */
UCLASS(ClassGroup=(OntoTwin), meta=(BlueprintSpawnableComponent))
class ONTOTWINSYNC_API UOntoTwinWebInteractionComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UOntoTwinWebInteractionComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Web Interaction|Connection",
        meta=(ClampMin="0.25", ClampMax="10.0"))
    float RuntimePollInterval = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Web Interaction|UI")
    EOntoTwinWebGlassQuality GlassQuality = EOntoTwinWebGlassQuality::Balanced;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Web Interaction|UI")
    bool bEnableWebInteraction = true;

    UFUNCTION(BlueprintCallable, Category="Web Interaction")
    bool OpenProjectHome();

    UFUNCTION(BlueprintCallable, Category="Web Interaction")
    bool OpenZone(const FString& ZoneId);

    UFUNCTION(BlueprintCallable, Category="Web Interaction")
    bool OpenBusinessView(const FString& BusinessViewId, const FString& ZoneId);

    UFUNCTION(BlueprintCallable, Category="Web Interaction")
    bool OpenInstanceDetail(const FString& InstanceId);

    void GetAvailableZones(
        TArray<FString>& OutZoneIds,
        TArray<FString>& OutDisplayNames) const;

    void GetAvailableZoneTree(
        TArray<FString>& OutZoneIds,
        TArray<FString>& OutDisplayNames,
        TArray<FString>& OutParentZoneIds) const;

    void GetAvailableBusinessViews(
        TArray<FString>& OutBusinessViewIds,
        TArray<FString>& OutDisplayNames) const;

    void GetAvailableBusinessViewSummaries(
        TArray<FString>& OutBusinessViewIds,
        TArray<FString>& OutDisplayNames,
        TArray<int32>& OutMemberCounts) const;

    bool IsRuntimeReady() const { return AppliedRevision >= 0; }
    bool IsWebOpen() const { return bInputCaptured && !bRuntimeEditorSuppressed; }
    bool IsPointerOverInteractiveWeb() const;
    bool CanGoBack() const { return NavigationHistory.Num() > 0; }

    /** Home restores the unfiltered scene and hides the browser without destroying it. */
    void ResetHome();

    /** F10 is an exclusive editing surface; preserve and restore the current web session. */
    void SetRuntimeEditorSuppressed(bool bSuppressed);

    bool GetRuntimeBusinessEditSnapshot(
        TSharedPtr<FJsonObject>& OutConfig,
        int32& OutRevision) const;
    int32 GetRuntimeBusinessEditRevision() const { return AppliedRevision; }
    void GetRuntimeBusinessMembershipStates(
        const TSharedPtr<FJsonObject>& Config,
        const TArray<FString>& InstanceIds,
        TArray<FString>& OutBusinessIds,
        TArray<FString>& OutNames,
        TArray<EOntoTwinBusinessMembershipState>& OutStates) const;
    bool SetRuntimeBusinessMembership(
        const TSharedPtr<FJsonObject>& Config,
        const FString& BusinessId,
        const TArray<FString>& InstanceIds,
        bool bAdd,
        FString& OutError) const;
    bool CreateRuntimeBusiness(
        const TSharedPtr<FJsonObject>& Config,
        const FString& Name,
        const TArray<FString>& InstanceIds,
        FString& OutBusinessId,
        FString& OutError) const;
    void ApplyRuntimeBusinessEdit(
        const TSharedPtr<FJsonObject>& Config,
        int32 ExpectedRevision,
        TFunction<void(bool, int32, const FString&)> Completion);

    UFUNCTION(BlueprintCallable, Category="Web Interaction")
    void Back();

    UFUNCTION(BlueprintCallable, Category="Web Interaction")
    void Close();

    UFUNCTION(BlueprintCallable, Category="Web Interaction")
    void Retry();

    void HandleBridgeMessage(const FString& MessageJson);
    bool ValidateNavigation(const FString& Url, FString& OutReason) const;
    bool HandlePopupNavigation(const FString& Url);
    void HandlePageLoadStarted();
    void HandlePageLoaded();
    void HandlePageLoadError();
    void HandleUrlChanged(const FString& Url);
    bool HandleHostShortcut(const FKey& Key);

private:
    UPROPERTY()
    ATwinSceneManager* SceneManager = nullptr;

    UPROPERTY()
    APlayerController* PlayerController = nullptr;

    UPROPERTY()
    UOntoTwinWebHostWidget* HostWidget = nullptr;

    UPROPERTY()
    UOntoTwinWebBridge* BridgeObject = nullptr;

    TSharedPtr<FJsonObject> PublishedConfig;
    TMap<FString, TSharedPtr<FJsonObject>> PagesById;
    TMap<FString, TSharedPtr<FJsonObject>> InstancesById;
    TMap<FString, TSet<FString>> BusinessViewMembers;
    TMap<FString, FString> ZoneParents;
    TMap<FString, FString> ZoneDisplayNames;
    TMap<FString, FString> BusinessViewDisplayNames;
    TSet<FString> AllowedHosts;
    TArray<FOntoTwinWebNavigationFrame> NavigationHistory;
    FOntoTwinWebNavigationFrame CurrentFrame;
    TMap<TWeakObjectPtr<ATwinInstance>, bool> OriginalHiddenStates;
    int32 AppliedRevision = -1;
    float PollAccumulator = 1000.0f;
    float VisibilityAccumulator = 0.0f;
    bool bRuntimeRequestInFlight = false;
    bool bShuttingDown = false;
    bool bHasCurrentFrame = false;
    bool bInputCaptured = false;
    bool bPreviousMouseCursor = false;
    bool bBridgeReady = false;
    bool bRuntimeEditorSuppressed = false;
    bool bRestoreAfterRuntimeEditor = false;
    FString UrlPolicy = TEXT("open");
    FString ActiveProjectId;
    FString LastBlockedNavigation;

    void PollRuntimeProjection();
    void AddProjectHeaders(const TSharedRef<IHttpRequest, ESPMode::ThreadSafe>& Request) const;
    void HandleRuntimeProjection(const TSharedPtr<FJsonObject>& Payload);
    void RebuildRuntimeIndexes(const TSharedPtr<FJsonObject>& Payload);
    bool ResolveAndOpen(const FString& Trigger, const TSharedPtr<FJsonObject>& Context,
        const TSharedPtr<FJsonObject>& ExtraParams = nullptr, bool bPushHistory = true);
    bool OpenPage(const TSharedPtr<FJsonObject>& Page, const FString& Trigger,
        const TSharedPtr<FJsonObject>& Context, const TSharedPtr<FJsonObject>& ExtraParams,
        bool bPushHistory);
    TSharedPtr<FJsonObject> ResolveBinding(const FString& Trigger,
        const TSharedPtr<FJsonObject>& Context, TArray<FString>& OutChain, bool& bBlocked) const;
    FString BuildFinalUrl(const TSharedPtr<FJsonObject>& Page,
        const TSharedPtr<FJsonObject>& Context, const TSharedPtr<FJsonObject>& ExtraParams,
        FString& OutError) const;
    void EnsureHostWidget();
    void CaptureWebInput();
    void RestoreInput();
    void ApplySceneScope(const TSharedPtr<FJsonObject>& Page,
        const TSharedPtr<FJsonObject>& Context, FOntoTwinWebNavigationFrame& Frame);
    void ApplyVisibilityFrame(const FOntoTwinWebNavigationFrame& Frame);
    void TickVisibilityScope();
    void RestoreSceneVisibility();
    bool IsInstanceKnown(const FString& InstanceId) const;
    bool IsZoneKnown(const FString& ZoneId) const;
    bool IsBusinessViewKnown(const FString& BusinessViewId) const;
    TSet<FString> DescendantZones(const FString& ZoneId) const;
    void SelectAndFocusInstance(const FString& InstanceId);
    void SendToPage(const FString& Type, const FString& RequestId,
        const TSharedPtr<FJsonObject>& Payload = nullptr);
    void SendContextToPage();
    void SendRuntimeEvent(const FString& EventType, const FString& Result,
        const FString& ErrorCode = FString()) const;
    TSet<FString> BusinessMembersForConfig(
        const TSharedPtr<FJsonObject>& BusinessView) const;
    static FString JsonString(const TSharedPtr<FJsonObject>& Object);
};
