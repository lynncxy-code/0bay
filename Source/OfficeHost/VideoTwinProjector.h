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
    /** Seconds between depth re-captures. One projector can afford 0.5 s; several at once should not. */
    float DepthRefreshSeconds = .5f;

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
    bool bDepthDirty = true;
    float DepthAge = 0;
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
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Video Twin|Runtime") bool bStartEnabled = true;
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
