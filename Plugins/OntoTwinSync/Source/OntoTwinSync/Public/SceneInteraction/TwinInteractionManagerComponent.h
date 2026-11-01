#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Dom/JsonObject.h"
#include "Interfaces/IHttpRequest.h"
#include "InputCoreTypes.h"
#include "SceneInteraction/TwinCameraVisibility.h"
#include "SceneInteraction/TwinRoamingTypes.h"
#include "SceneInteraction/TwinDeferredUiRequests.h"
#include "TwinInteractionManagerComponent.generated.h"

class APlayerController;
class APawn;
class ATwinGodViewAnchor;
class ATwinInstance;
class ATwinMinimapAnchor;
class ATwinRoamingCharacter;
class ATwinRoamingRoute;
class ATwinRoamingSpawnAnchor;
class ATwinSceneManager;
class UInputAction;
class UInputMappingContext;
class UOntoTwinCrosshairWidget;
class UOntoTwinNarrationHUDWidget;
class UOntoTwinRoamingHUDWidget;
class UOntoTwinRuntimeDockWidget;
class UAudioComponent;
class USoundWaveProcedural;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
struct FInputActionValue;
struct FHitResult;

/**
 * Scene Interaction 的 UE 运行入口。
 * 负责运行投影轮询、绑定失败关闭、人物生命周期、输入仲裁、热更新与无位置心跳。
 */
UCLASS(ClassGroup=(OntoTwin), meta=(BlueprintSpawnableComponent))
class ONTOTWINSYNC_API UTwinInteractionManagerComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UTwinInteractionManagerComponent();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scene Interaction|Connection",
        meta=(ClampMin="0.25", ClampMax="10.0"))
    float RuntimePollInterval = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scene Interaction|Runtime Route",
        meta=(ClampMin="10.0", ClampMax="1000.0"))
    float RuntimeRouteTraceUpCm = 150.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scene Interaction|Runtime Route",
        meta=(ClampMin="10.0", ClampMax="5000.0"))
    float RuntimeRouteTraceDownCm = 300.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scene Interaction|Runtime Route",
        meta=(ClampMin="0.0", ClampMax="1.0"))
    float RuntimeRouteMinGroundNormalZ = 0.65f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scene Interaction|Connection",
        meta=(ClampMin="1", ClampMax="20"))
    int32 RuntimeOfflineThreshold = 3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scene Interaction|Input")
    FKey ToggleRoamingKey = EKeys::F7;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scene Interaction|Input")
    FKey ToggleViewKey = EKeys::V;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scene Interaction|Input")
    FKey ToggleHudKey = EKeys::Tab;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scene Interaction|Input")
    FKey InteractKey = EKeys::E;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scene Interaction|Input")
    FKey ResumeRouteKey = EKeys::R;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scene Interaction|Input")
    FKey PauseRouteKey = EKeys::P;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scene Interaction|Input")
    FKey MouseLookKey = EKeys::RightMouseButton;

    /** Configurable fixed runtime/home camera; falls back to camera.god.default when absent. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scene Interaction|Camera")
    FString StartupViewCameraId = TEXT("camera.startup.default");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scene Interaction|UI")
    TSubclassOf<UOntoTwinRoamingHUDWidget> RoamingHUDClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scene Interaction|UI")
    TSubclassOf<UOntoTwinRuntimeDockWidget> RuntimeDockClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Scene Interaction|UI")
    TSubclassOf<UOntoTwinNarrationHUDWidget> NarrationHUDClass;

    UFUNCTION(BlueprintCallable, Category="Scene Interaction")
    void ToggleRoaming();

    UFUNCTION(BlueprintCallable, Category="Scene Interaction")
    void ExitRoaming();

    UFUNCTION(BlueprintCallable, Category="Scene Interaction")
    void ToggleCameraMode();

    UFUNCTION(BlueprintCallable, Category="Scene Interaction")
    void SetCameraMode(ETwinRoamingCameraMode Mode);

    UFUNCTION(BlueprintCallable, Category="Scene Interaction")
    void ToggleHudInteraction();

    void RequestUiAction(ETwinUiRequest Action);

    UFUNCTION(BlueprintCallable, Category="Scene Interaction")
    void CycleSkin();

    UFUNCTION(BlueprintCallable, Category="Scene Interaction")
    void ResumeRoute();

    UFUNCTION(BlueprintCallable, Category="Scene Interaction")
    void RestartRoute();

    UFUNCTION(BlueprintCallable, Category="Scene Interaction")
    void SkipNarrationSegment();

    UFUNCTION(BlueprintCallable, Category="Scene Interaction")
    bool SelectRuntimeRoute(const FString& RouteId);

    UFUNCTION(BlueprintCallable, Category="Scene Interaction")
    bool SelectRuntimeCharacter(const FString& CharacterId);

    UFUNCTION(BlueprintCallable, Category="Scene Interaction")
    void ApplyPendingReload();

    bool IsRoamingActive() const { return bRoamingActive; }
    bool IsHudInteractionOpen() const { return bHudInteraction; }
    bool OwnsPointerSelection() const { return bRoamingActive || bHudInteraction; }
    bool HasPendingReload() const { return bPendingReload; }
    bool IsCameraTransitioning() const;
    ETwinRoamingCameraMode GetCameraMode() const;
    bool GetGodViewTransform(FTransform& OutTransform) const;
    bool GetGodViewLookSensitivity(float& OutSensitivity) const;
    bool FocusInstanceCamera(
        const FTransform& TargetTransform,
        float FovDegrees,
        FString& OutError);
    void RestoreStartupView();
    /** SceneManager 首轮模型基线完成后开放人物漫游，并执行待处理的自动进入。 */
    void NotifySceneBaselineReady();
    FString GetHudStatusText() const;
    FString GetHudHintText() const;
    void GetHudShortcutItems(
        TArray<FString>& OutKeys,
        TArray<FString>& OutDescriptions) const;
    void GetAvailableRuntimeRoutes(
        TArray<FString>& OutRouteIds,
        TArray<FString>& OutDisplayNames,
        TArray<bool>& OutDefaultFlags) const;
    void GetAvailableRuntimeCharacters(
        TArray<FString>& OutCharacterIds,
        TArray<FString>& OutDisplayNames) const;
    void GetAvailableWebZones(
        TArray<FString>& OutZoneIds,
        TArray<FString>& OutDisplayNames) const;
    void GetAvailableWebZoneTree(
        TArray<FString>& OutZoneIds,
        TArray<FString>& OutDisplayNames,
        TArray<FString>& OutParentZoneIds) const;
    void GetAvailableWebBusinessViews(
        TArray<FString>& OutBusinessViewIds,
        TArray<FString>& OutDisplayNames) const;
    void GetAvailableWebBusinessViewSummaries(
        TArray<FString>& OutBusinessViewIds,
        TArray<FString>& OutDisplayNames,
        TArray<int32>& OutMemberCounts) const;
    void ActivateRuntimeHome();
    bool CanToggleRuntimeEditor() const;
    void ToggleRuntimeEditor();
    void SetRuntimeEditorSuppressed(bool bSuppressed);
    bool IsFirstPersonCrosshairEnabled() const { return bFirstPersonCrosshairEnabled; }
    bool CanToggleFirstPersonCrosshair() const;
    void ToggleFirstPersonCrosshair();
    bool OpenWebProjectHome();
    bool OpenWebZone(const FString& ZoneId);
    bool OpenWebBusinessView(
        const FString& BusinessViewId,
        const FString& ZoneId = FString());
    void HandleGlobalPointerSelection(const FHitResult* Hit);
    FString GetActiveRuntimeRouteId() const { return CurrentConfig.RouteId; }
    bool IsRouteSwitching() const { return bRouteSwitchInProgress; }
    FString GetActiveRuntimeCharacterId() const
    {
        return ActiveSessionCharacterId.IsEmpty()
            ? CurrentConfig.CharacterId : ActiveSessionCharacterId;
    }
    bool IsCharacterSwitching() const { return bCharacterSwitchInProgress; }
    FString GetMinimapState() const { return MinimapState; }
    void RefreshMinimapForDisplayScheme() { ScheduleMinimapRefreshSeries(TEXT("display_scheme"), 3, 0.2f); }
    bool CanUseMinimapTeleport() const;
    void PreviewMinimapTeleport(const FVector2D& UV);
    void RequestMinimapTeleport(const FVector2D& UV);
    void UndoLastMinimapTeleport();
    FString GetHudDetailText() const;
    void NotifyRuntimeEditorBlocked();

private:
    UPROPERTY()
    ATwinSceneManager* SceneManager = nullptr;

    UPROPERTY()
    APlayerController* PlayerController = nullptr;

    UPROPERTY()
    APawn* OriginalPawn = nullptr;

    UPROPERTY()
    ATwinRoamingCharacter* RoamingCharacter = nullptr;

    UPROPERTY()
    ATwinRoamingRoute* ActiveRoute = nullptr;

    UPROPERTY()
    ATwinRoamingRoute* RuntimeRouteActor = nullptr;

    UPROPERTY()
    ATwinGodViewAnchor* GodViewAnchor = nullptr;

    UPROPERTY()
    ATwinGodViewAnchor* StartupViewAnchor = nullptr;

    UPROPERTY()
    ATwinMinimapAnchor* MinimapAnchor = nullptr;

    UPROPERTY()
    USceneCaptureComponent2D* MinimapCapture = nullptr;

    UPROPERTY()
    UTextureRenderTarget2D* MinimapRenderTarget = nullptr;

    UPROPERTY()
    UOntoTwinRoamingHUDWidget* RoamingHUD = nullptr;

    UPROPERTY()
    UOntoTwinRuntimeDockWidget* RuntimeDock = nullptr;

    UPROPERTY()
    UOntoTwinNarrationHUDWidget* NarrationHUD = nullptr;

    UPROPERTY()
    UAudioComponent* NarrationAudioComponent = nullptr;

    UPROPERTY()
    USoundWaveProcedural* NarrationSound = nullptr;

    UPROPERTY()
    UOntoTwinCrosshairWidget* CrosshairHUD = nullptr;

    UPROPERTY()
    UInputMappingContext* ActiveMappingContext = nullptr;

    UPROPERTY()
    UInputMappingContext* DefaultMappingContext = nullptr;

    UPROPERTY()
    UInputAction* ToggleAction = nullptr;
    UPROPERTY()
    UInputAction* MoveAction = nullptr;
    UPROPERTY()
    UInputAction* LookAction = nullptr;
    UPROPERTY()
    UInputAction* LookCaptureAction = nullptr;
    UPROPERTY()
    UInputAction* VerticalAction = nullptr;
    UPROPERTY()
    UInputAction* HudAction = nullptr;
    UPROPERTY()
    UInputAction* InteractAction = nullptr;
    UPROPERTY()
    UInputAction* RouteAction = nullptr;
    UPROPERTY()
    UInputAction* PauseAction = nullptr;
    UPROPERTY()
    UInputAction* JumpAction = nullptr;
    UPROPERTY()
    UInputAction* CrouchAction = nullptr;
    UPROPERTY()
    UInputAction* SprintAction = nullptr;
    UPROPERTY()
    UInputAction* SelectAction = nullptr;
    UPROPERTY()
    UInputAction* SpeedAction = nullptr;

    FTwinRoamingRuntimeConfig CurrentConfig;
    FTwinRoamingRuntimeConfig PendingConfig;
    int32 AppliedRevision = -1;
    int32 PendingRevision = -1;
    FString RuntimeToken;
    FString PendingRuntimeToken;
    FString CatalogVersion;
    FString RuntimeState = TEXT("disabled");
    FString LastError;
    FString BindingWarning;
    FString SessionSelectedRouteId;
    FString PendingRouteSwitchId;
    FString ActiveSessionCharacterId;
    FString ActiveSessionCharacterDisplayName;
    FString ActiveSessionCharacterPrimaryAssetId;
    FString ActiveSessionDefaultSkinId;
    TMap<FString, FString> ActiveSessionSkinPrimaryAssetIds;
    FString MinimapState = TEXT("disabled");
    TArray<FString> DegradedFeatures;
    bool bBackendOnline = false;
    bool bRuntimeRequestInFlight = false;
    bool bShuttingDown = false;
    FTwinDeferredUiRequests DeferredUiRequests;
    void ProcessUiRequests();
    bool bHudRefreshRequested = true;
    bool bFlushingHud = false;
    float HudRefreshElapsed = 0.0f;
    FVector2D LastHudViewportSize = FVector2D(-1.0f, -1.0f);
    bool bRoamingActive = false;
    bool bHudInteraction = false;
    bool bRoamingMouseLook = false;
    bool bRestoreHudAfterRuntimeEditor = false;
    bool bPendingReload = false;
    bool bEnhancedInputReady = false;
    bool bSprintHeld = false;
    bool bTakeoverEnabled = true;
    bool bDefaultModeApplied = false;
    bool bSceneBaselineReady = false;
    bool bFirstPersonCrosshairEnabled = false;
    bool bCrosshairInteractive = false;
    bool bRouteSwitchInProgress = false;
    bool bCharacterSwitchInProgress = false;
    bool bGlobalHudInputCaptured = false;
    bool bGlobalHudPreviousMouseCursor = false;
    bool bPreRoamingMouseCursor = false;
    TWeakObjectPtr<ATwinInstance> WebSelectedInstance;
    FString LastWebInteractionMessage;
    float PollAccumulator = 1000.0f;
    float HeartbeatAccumulator = 0.0f;
    float MinimapMarkerAccumulator = 0.0f;
    double LastMinimapPointerEventSeconds = -1.0;
    int32 ConsecutiveFailures = 0;
    FTimerHandle RouteSwitchTimer;
    FTimerHandle NarrationTimer;
    FTimerHandle MinimapRefreshTimer;
    FTimerHandle MinimapTeleportTimer;
    FTimerHandle MinimapUndoTimer;
    FDelegateHandle RepresentationVisualReadyHandle;
    FString PendingMinimapRefreshReason;
    int32 PendingMinimapRefreshCaptures = 0;
    FTwinRoamingRuntimeWaypoint ActiveNarrationWaypoint;
    int32 ActiveNarrationSegmentIndex = -1;
    bool bNarrationActive = false;
    TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> NarrationAudioRequest;
    FMatrix MinimapViewProjection = FMatrix::Identity;
    FIntPoint MinimapCaptureSize = FIntPoint::ZeroValue;
    FVector PendingMinimapTeleportLocation = FVector::ZeroVector;
    FVector MinimapUndoLocation = FVector::ZeroVector;
    FVector2D PendingMinimapTeleportUV = FVector2D(0.5f, 0.5f);
    bool bMinimapTeleportInProgress = false;
    bool bPendingMinimapTeleportIsUndo = false;
    bool bPendingMinimapTeleportSucceeded = false;
    bool bMinimapUndoAvailable = false;
    FTwinCameraVisibilityState StartupViewVisibilityState;

    void PollRuntimeProjection();
    void HandleRuntimeProjection(const TSharedPtr<FJsonObject>& Payload);
    void RecordBackendFailure(const FString& Error);
    void SendHeartbeat();
    void AddProjectHeaders(const TSharedRef<IHttpRequest, ESPMode::ThreadSafe>& Request) const;
    bool ParseRuntimeConfig(const TSharedPtr<FJsonObject>& Payload,
        FTwinRoamingRuntimeConfig& OutConfig, int32& OutRevision, FString& OutToken,
        FString& OutCatalogVersion, FString& OutBlockedReason) const;
    bool IsStructuralChange(const FTwinRoamingRuntimeConfig& A,
        const FTwinRoamingRuntimeConfig& B) const;
    void ApplyHotConfig(const FTwinRoamingRuntimeConfig& Config);

    bool EnterRoaming(FString& OutError);
    bool ResolveSpawnTransform(class UTwinCharacterAsset* CharacterAsset,
        FTransform& OutTransform, FString& OutError) const;
    class UTwinCharacterAsset* ResolveCharacterAsset(FString& OutError) const;
    UObject* ResolvePrimaryAsset(const FString& PrimaryAssetId) const;
    const FTwinRoamingRuntimeCharacter* FindRuntimeCharacter(
        const FString& CharacterId) const;
    ATwinRoamingRoute* FindRoute(const FString& RouteId) const;
    ATwinRoamingRoute* BuildRuntimeRoute(FString& OutError);
    void DestroyRuntimeRoute();
    void CompleteRuntimeRouteSwitch();
    void CancelRuntimeRouteSwitch(bool bRestoreView);
    bool IsRuntimeRouteForCurrentLevel(const FTwinRoamingRuntimeRoute& Route) const;
    bool ProjectRuntimeRoutePointToGround(
        const FVector& Source, FVector& OutGroundPoint, FString& OutError, int32 PointIndex) const;
    ATwinRoamingSpawnAnchor* FindSpawnAnchor(const FString& SpawnId) const;
    ATwinGodViewAnchor* FindGodViewAnchor(const FString& CameraId) const;
    void ApplyStartupView(bool bForce = false);
    ATwinMinimapAnchor* FindMinimapAnchor(FString& OutState) const;
    void ApplyMinimapConfig(bool bEnabled);
    bool InitializeMinimap(FString& OutError);
    bool CaptureMinimapScene(const TCHAR* Reason);
    void ScheduleMinimapRefreshSeries(
        const TCHAR* Reason, int32 CaptureCount, float InitialDelaySeconds);
    void HandleRepresentationVisualReady(ATwinInstance* Instance);
    void RefreshMinimapAfterSceneSettled();
    void ShutdownMinimap(bool bResetState);
    void SetMinimapState(const FString& State);
    void UpdateMinimapMarker(float DeltaTime);
    bool ProjectMinimapPoint(const FVector& WorldPoint, FVector2D& OutUV) const;
    bool DeprojectMinimapUV(
        const FVector2D& UV,
        float ReferenceFootZ,
        FVector& OutDesiredFoot,
        FString& OutError) const;
    bool ResolveSafeMinimapLocation(
        const FVector& DesiredFoot,
        float ReferenceFootZ,
        FVector& OutCapsuleCenter,
        bool& bOutAdjusted,
        FString& OutError) const;
    bool ResolveMinimapTeleportTarget(
        const FVector2D& UV,
        FVector& OutCapsuleCenter,
        FVector2D& OutResolvedUV,
        bool& bOutAdjusted,
        FString& OutError) const;
    void BeginMinimapTeleport(
        const FVector& TargetLocation,
        const FVector2D& TargetUV,
        bool bIsUndo);
    void CompleteMinimapTeleportMove();
    void FinishMinimapTeleport();
    void ExpireMinimapUndo();
    void CancelMinimapTeleport(bool bRestoreView);
    void CreateHud();
    void DestroyHud();
    void RefreshHud();
    void HandleNarrationRequested(const FTwinRoamingRuntimeWaypoint& Waypoint);
    void ShowNarrationSegment();
    void FinishNarrationPoint();
    void StopNarration(bool bInterruptedByUser);
    bool TryStartNarrationAudio(const FTwinNarrationRuntimeSegment& Segment);
    bool PlayNarrationWav(
        const TArray<uint8>& Bytes,
        const FTwinNarrationRuntimeSegment& Segment);
    void StartNarrationFallbackTimer(const FTwinNarrationRuntimeSegment& Segment);
    FString NarrationCachePath(const FTwinNarrationRuntimeSegment& Segment) const;
    void UpdateCrosshairTarget();
    void SetHudInteraction(bool bOpen);
    void SetRoamingMouseLook(bool bActive);
    void ApplyRoamingMouseInputMode();
    void RestoreOriginalPawn();
    void HandleRoutePauseAction();

    void SetupInput();
    void ActivateRoamingInput();
    void DeactivateRoamingInput();
    void RemoveInput();
    void BuildDefaultInputContext();
    void BindEnhancedInput();
    void TickFallbackInput(float DeltaTime);
    void SelectFromView(bool bCursorTrace);
    ATwinInstance* ResolveInteractionInstance(const FHitResult& Hit) const;
    void HandleWebInstanceSelection(ATwinInstance* Instance);
    bool CompleteWebOpen(
        bool bOpened,
        const FString& FailureMessage,
        bool bRevealDockOnFailure = true);

    void OnMove(const FInputActionValue& Value);
    void OnLook(const FInputActionValue& Value);
    void OnLookCaptureStarted(const FInputActionValue& Value);
    void OnLookCaptureEnded(const FInputActionValue& Value);
    void OnVertical(const FInputActionValue& Value);
    void OnToggle(const FInputActionValue& Value);
    void OnToggleHud(const FInputActionValue& Value);
    void OnInteract(const FInputActionValue& Value);
    void OnRoute(const FInputActionValue& Value);
    void OnPauseRoute(const FInputActionValue& Value);
    void OnJump(const FInputActionValue& Value);
    void OnCrouchStarted(const FInputActionValue& Value);
    void OnCrouchEnded(const FInputActionValue& Value);
    void OnSprintStarted(const FInputActionValue& Value);
    void OnSprintEnded(const FInputActionValue& Value);
    void OnSelect(const FInputActionValue& Value);
    void OnAdjustSpeed(const FInputActionValue& Value);
};
