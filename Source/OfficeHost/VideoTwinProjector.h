#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/HUD.h"
#include "Blueprint/UserWidget.h"
#include "VideoTwinProjector.generated.h"

class ACameraActor;
class UMediaSource;
class UMediaPlayer;
class UMediaTexture;
class UTexture2D;
class UTextureRenderTarget2D;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UPostProcessComponent;
class USceneCaptureComponent2D;
class UTextBlock;
class AVideoTwinRuntimeFeature;

/** One calibrated source. Multiple sources use independent players and calibration. */
UCLASS(BlueprintType)
class OFFICEHOST_API AVideoTwinProjector : public AActor
{
    GENERATED_BODY()
public:
    AVideoTwinProjector();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual bool ShouldTickIfViewportsOnly() const override { return true; }

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Video Twin|Source") FString CameraId = TEXT("17");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Video Twin|Source") TObjectPtr<ACameraActor> SourceCamera;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Video Twin|Source") TObjectPtr<UMediaSource> MediaSource;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Video Twin|Source") TObjectPtr<UTexture2D> ReferenceTexture;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Video Twin|Source") bool bMockSource = true;
    /** Live stream address (for example an HLS playlist). When set it replaces the local recording.
     *  Normally filled from "stream_url" in calibration.json. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Video Twin|Source") FString StreamUrl;
    /** Media player to use for StreamUrl ("stream_player" in calibration.json). Empty picks automatically:
     *  WmfMedia for rtsp:// (Electra has no RTSP support), the engine's default for everything else. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Video Twin|Source") FString StreamPlayer;
    /** Seconds behind the live edge, passed to Electra as ElectraLivePresentationOffset ("stream_live_offset" in
     *  calibration.json). DASH ONLY: the engine reads it in ManifestBuilderDASH.cpp and nowhere in the HLS code.
     *  Measured on HLS with 0.5 / 1.0 / 2.0 s and with it unset: 4.0 to 4.7 s every time, no effect. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Video Twin|Source") float StreamLiveOffset = -1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Video Twin|Rendering") TObjectPtr<UMaterialInterface> ProjectionMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Video Twin|Rendering") bool bEnabled = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Video Twin|Rendering", meta=(ClampMin="0",ClampMax="1")) float Opacity = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Video Twin|Rendering") bool bUseOcclusion = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Video Twin|Rendering", meta=(ClampMin="0.1")) float DepthToleranceCm = 6.f;
    /** Depth capture ignores anything nearer than this. The level has a dome housing modelled right where the
     *  real camera is; with the old 1 cm near plane the capture saw the inside of its own housing and rejected everything. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Video Twin|Rendering", meta=(ClampMin="1")) float DepthNearCm = 40.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Video Twin|Rendering") FVector ReceiverMin = FVector(-100,-2500,4315);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Video Twin|Rendering") FVector ReceiverMax = FVector(2850,300,4740);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Video Twin|Rendering", meta=(ClampMin="10")) float MaxDistanceCm = 3000.f;
    /** Width of the soft border inside the receiver box, in cm (X/Y sides only: the box's floor and ceiling stay hard).
     *  "receiver_feather_cm" in calibration.json. 0 = the old hard edge. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Video Twin|Rendering", meta=(ClampMin="0")) float ReceiverFeatherCm = 12.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Video Twin|Calibration") FVector PositionOffset = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Video Twin|Calibration") FRotator RotationOffset = FRotator::ZeroRotator;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Video Twin|Calibration") float FovOffset = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Video Twin|Calibration") FVector2D PrincipalPoint = FVector2D(0.5,0.5);
    /** Brown-Conrady radial terms in normalized camera coordinates; zero means uncalibrated. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Video Twin|Calibration") FVector2D RadialDistortion = FVector2D::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Video Twin|Calibration") bool bMaskTimestamp = true;

    /** Measured calibration (Tools/VideoTwin/calibration.json). When loaded it replaces the hand-placed
     *  camera pose and the assumed 90 degree lens; the offsets above still apply on top for fine tuning. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Video Twin|Calibration") bool bMeasuredCalibration = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Video Twin|Calibration") FVector MeasuredPosition = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Video Twin|Calibration") FQuat MeasuredRotation = FQuat::Identity;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Video Twin|Calibration") float MeasuredHorizontalFov = 0.f;
    UFUNCTION(BlueprintCallable, Category="Video Twin") bool LoadMeasuredCalibration();
    /** Diagnostics: write the projector depth map as a grayscale PNG (near = bright) and log a few samples. */
    void DumpDepth(const FString& PngPath);
    /** Camera ids listed under "enabled_cameras" in calibration.json, in that order. Empty when there is no file. */
    static void GetEnabledCameraIds(TArray<FString>& OutIds);
    /** A top-level true/false switch in calibration.json; bDefault when the file or the field is missing. */
    static bool GetCalibrationRootBool(const TCHAR* Field, bool bDefault);
    /** Set while another camera's view is shown and only that camera's own video should be visible. */
    bool bMutedByView = false;
    /** Seconds between periodic depth re-captures; 0 = never. The office is static and the real people are not in
     *  the model, so re-capturing on a timer only cost a full-scene render per projector every few seconds -- on site
     *  four of them landed in the same frame every 3 s and the picture stuttered. Depth is captured a few times after
     *  start (see DepthCaptureTimes, to catch late streaming) and whenever the pose changes. */
    float DepthRefreshSeconds = 0.f;
    /** Seconds after BeginPlay at which the depth is captured again regardless, so geometry that streams in late
     *  still occludes. Shifted by DepthCaptureStagger so several projectors never capture in the same frame. */
    TArray<float> DepthCaptureTimes = { 1.f, 6.f, 20.f };
    float DepthCaptureStagger = 0.f;
    /** In a camera view that camera's own projection must win over overlapping ones; see SetViewFocus. */
    void SetViewFocus(bool bFocused);

    UFUNCTION(BlueprintCallable, CallInEditor, Category="Video Twin") void RefreshDepth();
    UFUNCTION(BlueprintCallable, Category="Video Twin") void OpenVideo();
    UFUNCTION(BlueprintCallable, Category="Video Twin") void TogglePlayback();
    UFUNCTION(BlueprintCallable, Category="Video Twin") void ResetCalibration();
    FTransform GetProjectionTransform() const;
    float GetProjectionFov() const;
    float GetProjectionAspect() const;
    UMediaPlayer* GetPlayer() const { return Player; }
    UTexture* GetDisplayTexture() const;
    FString Status = TEXT("REFERENCE / uncalibrated");
    bool bSplitView = false;
    bool bDepthDebug = false;
    bool bDepthReady = false;

private:
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneCaptureComponent2D> DepthCapture;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UPostProcessComponent> ProjectionPostProcess;
    UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> DepthTarget;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ProjectionMID;
    UPROPERTY(Transient) TObjectPtr<UMediaPlayer> Player;
    UPROPERTY(Transient) TObjectPtr<UMediaTexture> VideoTexture;
    bool bVideoReady = false;
    /** The local recording, remembered when a live stream is tried first so there is something to fall back to. */
    UPROPERTY(Transient) TObjectPtr<UMediaSource> RecordedSource;
    bool bLiveFailed = false;
    bool FallBackToRecording(const TCHAR* Why);
    /** While on the recording because the live stream failed: poll the stream address now and then and go back
     *  to live once it answers (the converter restarting used to leave every camera on its recording until UE restarted). */
    void TryReturnToLive(float DeltaSeconds);
    float LiveRetryAge = 0;
    bool bLiveProbeInFlight = false;
    bool bDepthDirty = true;
    float DepthAge = 0;
    float PlayAge = 0;
    int32 NextDepthCapture = 0;
    /** Post-process priority from calibration.json; SetViewFocus adds to it while this camera's view is shown. */
    float BasePriority = 500.f;
    float OpenAge = 0;
    FTransform LastPose;
    float LastFov = 0;
    FVector2D LastPrincipal = FVector2D(-1,-1);
    void InitializeResources();
    void UpdateProjection();
    UFUNCTION() void MediaOpened(FString Url);
    UFUNCTION() void MediaFailed(FString Url);
};

UCLASS()
class OFFICEHOST_API AVideoTwinDemoController : public APlayerController
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;
    virtual void PlayerTick(float DeltaSeconds) override;
    void ShowSourceView();
    void ShowOrbitView();
    AVideoTwinProjector* GetProjector() const { return Projector; }
private:
    UPROPERTY() TObjectPtr<AVideoTwinProjector> Projector;
    UPROPERTY() TObjectPtr<ACameraActor> Observer;
    float Elapsed = 0;
    int32 TestStep = 0;
    bool bAutomatedTest = false;
    void RunAcceptance();
};

UCLASS()
class OFFICEHOST_API AVideoTwinDemoHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
};

UCLASS()
class OFFICEHOST_API AVideoTwinDemoGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AVideoTwinDemoGameMode();
};

/** Small runtime status panel. It is deliberately additive and does not replace the project's WebUI. */
UCLASS()
class OFFICEHOST_API UVideoTwinRuntimeWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void SetFeature(AVideoTwinRuntimeFeature* InFeature) { Feature = InFeature; }
protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
private:
    UPROPERTY(Transient) TObjectPtr<AVideoTwinRuntimeFeature> Feature;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Text;
};

/** Runtime-only bridge for the original Office_Demo level. */
UCLASS(BlueprintType)
class OFFICEHOST_API AVideoTwinRuntimeFeature : public AActor
{
    GENERATED_BODY()
public:
    AVideoTwinRuntimeFeature();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual bool ShouldTickIfViewportsOnly() const override { return true; }
    UFUNCTION(BlueprintCallable, Category="Video Twin|Runtime") void ToggleCamera17Fusion();
    UFUNCTION(BlueprintCallable, Category="Video Twin|Runtime") void EnterCamera17View();
    UFUNCTION(BlueprintCallable, Category="Video Twin|Runtime") void ExitCamera17View();
    UFUNCTION(BlueprintPure, Category="Video Twin|Runtime") bool IsCamera17FusionEnabled() const;
    UFUNCTION(BlueprintPure, Category="Video Twin|Runtime") AVideoTwinProjector* GetProjector() const { return Projector; }
    /** All projectors, one per enabled camera. Entry 0 is the same object GetProjector() returns. */
    const TArray<TObjectPtr<AVideoTwinProjector>>& GetProjectors() const { return Projectors; }
    int32 GetViewIndex() const { return ViewIndex; }
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Video Twin|Runtime") FString Camera17Label = TEXT("OntoTwin_Camera_17");
    /** Fusion starts off; Z turns it on. -VideoTwinStartOn overrides (the old -VideoTwinStartOff is still accepted). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Video Twin|Runtime") bool bStartEnabled = false;
private:
    UPROPERTY(Transient) TObjectPtr<AVideoTwinProjector> Projector;
    UPROPERTY(Transient) TObjectPtr<UVideoTwinRuntimeWidget> Widget;
    UPROPERTY(Transient) TObjectPtr<ACameraActor> Camera17;
    TWeakObjectPtr<APlayerController> Controller;
    TWeakObjectPtr<AActor> SavedViewTarget;
    bool bInCamera17View = false;
    /** One projector per enabled camera; Projector / Camera17 above mirror entry 0 for existing callers. */
    UPROPERTY(Transient) TArray<TObjectPtr<AVideoTwinProjector>> Projectors;
    /** View camera for each projector (the level's own camera actor when it exists, otherwise spawned). */
    UPROPERTY(Transient) TArray<TObjectPtr<ACameraActor>> ViewCameras;
    int32 ViewIndex = -1;
    /** "camera_view_only_own" in calibration.json (default true): in a camera's view hide the other projections. */
    bool bViewOnlyOwn = true;
    /** Give the viewed camera's projection the top priority and, with bViewOnlyOwn, mute the others. */
    void ApplyViewFocus();
    /** "local_light_shadows" in calibration.json (default false): whether point/spot/rect lights cast shadows.
     *  Measured on site (RTX 3080 Ti, 1600x900): the office's local lights casting shadows took the frame from 22 ms
     *  to 40 ms and the draw calls from 1545 to 7720, with or without the video; Lumen and resolution hardly mattered. */
    bool bLocalLightShadows = false;
    /** Turn shadow casting off on every local light now in the world; run a few times after start for streamed-in lights. */
    void ApplyLocalLightShadows();
    TArray<float> LightPassTimes = { 0.f, 5.f, 20.f };
    int32 NextLightPass = 0;
    float FeatureAge = 0.f;
    int32 TestCam = 1;
    AVideoTwinProjector* SpawnProjectorFor(const FString& Id);
    /** -VideoTwinTest: unattended screenshots of the original level, same idea as the MVP map's acceptance run. */
    UPROPERTY(Transient) TObjectPtr<ACameraActor> TestObserver;
    bool bAutomatedTest = false;
    float TestElapsed = 0.f;
    int32 TestStep = 0;
    void RunAcceptance(float DeltaSeconds);
    void BindRuntimeInput();
    void LoadMockAssets();
    void HandleToggle();
    void HandleEnterView();
    void HandleExitView();
};
