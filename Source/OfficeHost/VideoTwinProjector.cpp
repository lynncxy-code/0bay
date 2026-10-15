#include "VideoTwinProjector.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/PostProcessComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"
#include "MediaSource.h"
#include "FileMediaSource.h"
#include "StreamMediaSource.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Styling/CoreStyle.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "HttpModule.h"
#include "Components/LocalLightComponent.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "ImageUtils.h"
#include "VideoTwinRtspReader.h"

AVideoTwinProjector::AVideoTwinProjector()
{
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    DepthCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("ProjectorDepth"));
    DepthCapture->SetupAttachment(RootComponent);
    DepthCapture->CaptureSource = SCS_SceneDepth;
    DepthCapture->bCaptureEveryFrame = false;
    DepthCapture->bCaptureOnMovement = false;
    DepthCapture->bAlwaysPersistRenderingState = true;
    // Depth only. None of the lighting the main view needs, so the few captures that do happen stay cheap.
    FEngineShowFlags& Flags = DepthCapture->ShowFlags;
    Flags.SetLumenGlobalIllumination(false); Flags.SetLumenReflections(false); Flags.SetGlobalIllumination(false);
    Flags.SetDynamicShadows(false); Flags.SetAmbientOcclusion(false); Flags.SetScreenSpaceReflections(false);
    Flags.SetPostProcessing(false); Flags.SetFog(false); Flags.SetVolumetricFog(false); Flags.SetAtmosphere(false);
    Flags.SetBloom(false); Flags.SetMotionBlur(false); Flags.SetParticles(false);
    ProjectionPostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("VideoFusion"));
    ProjectionPostProcess->SetupAttachment(RootComponent);
    ProjectionPostProcess->bUnbound = true;
    ProjectionPostProcess->Priority = 500;
}

FTransform AVideoTwinProjector::GetProjectionTransform() const
{
    const UCameraComponent* Cam = SourceCamera ? SourceCamera->GetCameraComponent() : nullptr;
    const FTransform T = Cam ? Cam->GetComponentTransform() : GetActorTransform();
    // A measured pose wins over the camera that was placed by hand in the level.
    if (bMeasuredCalibration) return FTransform(MeasuredRotation * RotationOffset.Quaternion(), MeasuredPosition + PositionOffset);
    return FTransform(T.GetRotation() * RotationOffset.Quaternion(), T.GetLocation() + PositionOffset);
}
/** Where calibration.json lives.
 *  Content/VideoTwin is staged into a packaged build as loose files (DefaultGame.ini, DirectoriesToAlwaysStageAsNonUFS),
 *  so the file exists at runtime and can still be edited on site without repackaging.
 *  Tools/VideoTwin is the old location; it is not part of a packaged build and is kept only as a fallback. */
static FString VideoTwinCalibrationPath()
{
    const FString Staged = FPaths::ProjectContentDir() / TEXT("VideoTwin/calibration.json");
    if (FPaths::FileExists(Staged)) return Staged;
    const FString Legacy = FPaths::ProjectDir() / TEXT("Tools/VideoTwin/calibration.json");
    if (FPaths::FileExists(Legacy)) return Legacy;
    return Staged;
}
bool AVideoTwinProjector::GetCalibrationRootBool(const TCHAR* Field, bool bDefault)
{
    FString Text; TSharedPtr<FJsonObject> Root; bool bValue = bDefault;
    if (!FFileHelper::LoadFileToString(Text, *VideoTwinCalibrationPath())) return bDefault;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid()) return bDefault;
    Root->TryGetBoolField(Field, bValue);
    return bValue;
}
void AVideoTwinProjector::GetEnabledCameraIds(TArray<FString>& OutIds)
{
    OutIds.Reset();
    if (FParse::Param(FCommandLine::Get(), TEXT("VideoTwinNoCalib"))) return;
    FString Text; TSharedPtr<FJsonObject> Root;
    if (!FFileHelper::LoadFileToString(Text, *VideoTwinCalibrationPath())) return;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid()) return;
    const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
    if (!Root->TryGetArrayField(TEXT("enabled_cameras"), List)) return;
    for (const TSharedPtr<FJsonValue>& V : *List) { FString Id; if (V.IsValid() && V->TryGetString(Id) && !Id.IsEmpty()) OutIds.AddUnique(Id); }
}
bool AVideoTwinProjector::LoadMeasuredCalibration()
{
    bMeasuredCalibration = false;
    // -VideoTwinNoCalib keeps the old hand-placed pose so the two can be compared side by side.
    if (FParse::Param(FCommandLine::Get(), TEXT("VideoTwinNoCalib"))) return false;
    const FString Path = VideoTwinCalibrationPath();
    FString Text;
    if (!FFileHelper::LoadFileToString(Text, *Path)) { UE_LOG(LogTemp,Warning,TEXT("VideoTwin: no calibration file at %s"),*Path); return false; }
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid()) { UE_LOG(LogTemp,Warning,TEXT("VideoTwin: calibration file is not valid JSON")); return false; }
    const TSharedPtr<FJsonObject>* Cams = nullptr; const TSharedPtr<FJsonObject>* Cam = nullptr;
    if (!Root->TryGetObjectField(TEXT("cameras"), Cams) || !(*Cams)->TryGetObjectField(CameraId, Cam)) { UE_LOG(LogTemp,Warning,TEXT("VideoTwin: camera %s is not in the calibration file"),*CameraId); return false; }
    auto Vec = [&](const TCHAR* Name, FVector& Out)
    {
        const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
        if (!(*Cam)->TryGetArrayField(Name, A) || A->Num() != 3) return false;
        Out = FVector((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber());
        return true;
    };
    FVector Pos, Fwd, Up; double Fov = 0;
    if (!Vec(TEXT("position_cm"), Pos) || !Vec(TEXT("forward"), Fwd) || !Vec(TEXT("up"), Up) || !(*Cam)->TryGetNumberField(TEXT("horizontal_fov_deg"), Fov)) { UE_LOG(LogTemp,Warning,TEXT("VideoTwin: camera %s calibration is incomplete"),*CameraId); return false; }
    // Built from the axes directly. Going through Euler angles would invite a sign mistake on roll.
    MeasuredRotation = FRotationMatrix::MakeFromXZ(Fwd, Up).ToQuat();
    MeasuredPosition = Pos;
    MeasuredHorizontalFov = float(Fov);
    const TArray<TSharedPtr<FJsonValue>>* PP = nullptr;
    if ((*Cam)->TryGetArrayField(TEXT("principal_point"), PP) && PP->Num() == 2) PrincipalPoint = FVector2D((*PP)[0]->AsNumber(), (*PP)[1]->AsNumber());
    double K1 = 0, K2 = 0;
    (*Cam)->TryGetNumberField(TEXT("k1"), K1); (*Cam)->TryGetNumberField(TEXT("k2"), K2);
    RadialDistortion = FVector2D(K1, K2);
    // Optional per-camera receiver box and range, so the projected area can be tuned without rebuilding.
    FVector Box; double Range = 0;
    if (Vec(TEXT("receiver_min"), Box)) ReceiverMin = Box;
    if (Vec(TEXT("receiver_max"), Box)) ReceiverMax = Box;
    if ((*Cam)->TryGetNumberField(TEXT("max_distance_cm"), Range) && Range > 10) MaxDistanceCm = float(Range);
    double Feather = -1;
    if ((*Cam)->TryGetNumberField(TEXT("receiver_feather_cm"), Feather) && Feather >= 0) ReceiverFeatherCm = float(Feather);
    FString MaskFile;
    ImageMaskFile = (*Cam)->TryGetStringField(TEXT("image_mask"), MaskFile) ? MaskFile : FString();
    ApplyImageMask();
    // Where two cameras both see a surface the one with the higher priority is drawn last and wins.
    // -VideoTwinNoStream ignores stream_url, so the recordings can be compared against the live feed.
    FString Url;
    if (!FParse::Param(FCommandLine::Get(), TEXT("VideoTwinNoStream")) && (*Cam)->TryGetStringField(TEXT("stream_url"), Url)) StreamUrl = Url;
    double LiveOffset = -1;
    if ((*Cam)->TryGetNumberField(TEXT("stream_live_offset"), LiveOffset)) StreamLiveOffset = float(LiveOffset);
    FString PlayerName;
    if ((*Cam)->TryGetStringField(TEXT("stream_player"), PlayerName)) StreamPlayer = PlayerName;
    double Priority = 0;
    if ((*Cam)->TryGetNumberField(TEXT("priority"), Priority)) BasePriority = float(Priority);
    if (ProjectionPostProcess) ProjectionPostProcess->Priority = BasePriority;
    bMeasuredCalibration = true;
    bDepthDirty = true;
    UE_LOG(LogTemp,Display,TEXT("VideoTwin camera %s measured calibration: pos=(%.1f,%.1f,%.1f) rot=%s hfov=%.2f k=(%.4f,%.4f)"),*CameraId,Pos.X,Pos.Y,Pos.Z,*MeasuredRotation.Rotator().ToCompactString(),MeasuredHorizontalFov,K1,K2);
    return true;
}
float AVideoTwinProjector::GetProjectionFov() const
{
    if (bMeasuredCalibration) return FMath::Clamp(MeasuredHorizontalFov + FovOffset, 20.f, 150.f);
    return FMath::Clamp((SourceCamera ? SourceCamera->GetCameraComponent()->FieldOfView : 90.f) + FovOffset, 20.f, 150.f);
}
float AVideoTwinProjector::GetProjectionAspect() const
{
    return SourceCamera ? FMath::Max(0.1f, SourceCamera->GetCameraComponent()->AspectRatio) : 16.f/9.f;
}
void AVideoTwinProjector::InitializeResources()
{
    if (ProjectionMID || !ProjectionMaterial || !ReferenceTexture || IsRunningCommandlet()) return;
    DepthTarget = NewObject<UTextureRenderTarget2D>(this);
    DepthTarget->RenderTargetFormat = RTF_R32f;
    DepthTarget->ClearColor = FLinearColor(1000000,0,0,0);
    DepthTarget->Filter = TF_Nearest;
    DepthTarget->InitAutoFormat(1920,1080);
    DepthTarget->UpdateResourceImmediate(true);
    DepthCapture->TextureTarget = DepthTarget;
    DepthCapture->HiddenActors.Add(this);
    if (SourceCamera) DepthCapture->HiddenActors.Add(SourceCamera);
    ProjectionMID = UMaterialInstanceDynamic::Create(ProjectionMaterial, this);
    ProjectionMID->SetTextureParameterValue(TEXT("VideoTex"), ReferenceTexture);
    ProjectionMID->SetTextureParameterValue(TEXT("ProjectorDepthTex"), DepthTarget);
    ProjectionPostProcess->AddOrUpdateBlendable(ProjectionMID,1.f);
    // Loaded with the calibration (LoadMeasuredCalibration runs first); hand it over now that the material exists.
    if (MaskTexture) ProjectionMID->SetTextureParameterValue(TEXT("MaskTex"), MaskTexture);
    bDepthDirty = true;
}
void AVideoTwinProjector::ApplyImageMask()
{
    MaskTexture = nullptr;
    if (!ImageMaskFile.IsEmpty())
    {
        const FString Path = FPaths::GetPath(VideoTwinCalibrationPath()) / ImageMaskFile;
        MaskTexture = FImageUtils::ImportFileAsTexture2D(Path);
        if (MaskTexture)
        {
            // The PNG is written sRGB-encoded (mask_from_screenshot.py) and the material samples it as colour.
            MaskTexture->SRGB = true;
            MaskTexture->Filter = TF_Bilinear;
            MaskTexture->AddressX = TA_Clamp;
            MaskTexture->AddressY = TA_Clamp;
            MaskTexture->UpdateResource();
            UE_LOG(LogTemp,Display,TEXT("VideoTwin camera %s image mask %s (%dx%d)"),*CameraId,*ImageMaskFile,MaskTexture->GetSizeX(),MaskTexture->GetSizeY());
        }
        else UE_LOG(LogTemp,Warning,TEXT("VideoTwin camera %s: image mask %s could not be read; showing the whole picture"),*CameraId,*Path);
    }
    // Without a mask the material's own default (a white texture) applies, so nothing needs setting.
    if (ProjectionMID && MaskTexture) ProjectionMID->SetTextureParameterValue(TEXT("MaskTex"), MaskTexture);
}
void AVideoTwinProjector::BeginPlay()
{
    Super::BeginPlay();
    LoadMeasuredCalibration();
    InitializeResources();
    OpenVideo();
}
bool AVideoTwinProjector::UseDirectRtsp() const
{
    return StreamUrl.StartsWith(TEXT("rtsp"), ESearchCase::IgnoreCase)
        && (StreamPlayer.IsEmpty() || StreamPlayer.Equals(TEXT("ffmpeg"), ESearchCase::IgnoreCase));
}
void AVideoTwinProjector::OpenVideo()
{
    bVideoReady = false;
    OpenAge = 0;
    if (!StreamUrl.IsEmpty() && UseDirectRtsp())
    {
        // Direct read: no media player at all while the camera answers. The recording stays in MediaSource for
        // PlayRecording. The password is filled in from the environment here and nowhere else.
        bLiveDirect = true;
        bMockSource = false;
        if (!RecordedSource) RecordedSource = MediaSource;
        if (!LiveReader)
        {
            FString Address = StreamUrl;
            if (Address.Contains(TEXT("${OFFICE_CAM_PASSWORD}")))
            {
                const FString Password = FPlatformMisc::GetEnvironmentVariable(TEXT("OFFICE_CAM_PASSWORD"));
                if (Password.IsEmpty()) UE_LOG(LogTemp,Warning,TEXT("VideoTwin camera %s: stream_url needs OFFICE_CAM_PASSWORD, which is not set"),*CameraId);
                Address.ReplaceInline(TEXT("${OFFICE_CAM_PASSWORD}"), *FGenericPlatformHttp::UrlEncode(Password));
            }
            LiveReader = MakeShared<FVideoTwinRtspReader>(Address, CameraId);
            if (!LiveReader->Start()) PlayRecording(LiveReader->GetLastError());
        }
        Status = TEXT("OPENING LIVE (direct) | reference fallback");
        UE_LOG(LogTemp,Display,TEXT("VideoTwin camera %s opening live stream directly: %s"),*CameraId,*FVideoTwinRtspReader::MaskUrl(StreamUrl));
        return;
    }
    if (!StreamUrl.IsEmpty() && !bLiveFailed)
    {
        RecordedSource = MediaSource;
        // A live address wins over the recording. The rest of the chain (player, texture, projection) is the same.
        UStreamMediaSource* Live = NewObject<UStreamMediaSource>(this);
        Live->StreamUrl = StreamUrl;
        // -VideoTwinLiveOffset=<seconds> overrides the file, for sweeping the value without editing it.
        float CmdOffset = -1.f;
        if (FParse::Value(FCommandLine::Get(), TEXT("VideoTwinLiveOffset="), CmdOffset)) StreamLiveOffset = CmdOffset;
        if (StreamLiveOffset >= 0.f)
        {
            Live->SetMediaOptionDouble(FName(TEXT("ElectraLivePresentationOffset")), double(StreamLiveOffset));
            UE_LOG(LogTemp,Display,TEXT("VideoTwin camera %s live offset %.2f s"),*CameraId,StreamLiveOffset);
        }
        MediaSource = Live;
        bMockSource = false;
        UE_LOG(LogTemp,Display,TEXT("VideoTwin camera %s opening live stream %s"),*CameraId,*FVideoTwinRtspReader::MaskUrl(StreamUrl));
    }
    if (!MediaSource) { Status = TEXT("REFERENCE | no media source"); return; }
    if (!Player)
    {
        Player = NewObject<UMediaPlayer>(this);
        Player->PlayOnOpen = true;
        Player->OnMediaOpened.AddDynamic(this,&AVideoTwinProjector::MediaOpened);
        Player->OnMediaOpenFailed.AddDynamic(this,&AVideoTwinProjector::MediaFailed);
        VideoTexture = NewObject<UMediaTexture>(this);
        VideoTexture->NewStyleOutput = true;
        VideoTexture->AutoClear = false;
        VideoTexture->SetMediaPlayer(Player);
        VideoTexture->UpdateResource();
    }
    Player->SetLooping(bMockSource);
    if (!StreamUrl.IsEmpty() && !bLiveFailed)
    {
        // Two stock players exist. Electra handles HLS/DASH/files; only the Media Foundation one registers rtsp://.
        FName Wanted = NAME_None;
        if (!StreamPlayer.IsEmpty()) Wanted = FName(*StreamPlayer);
        else if (StreamUrl.StartsWith(TEXT("rtsp"), ESearchCase::IgnoreCase)) Wanted = FName(TEXT("WmfMedia"));
        Player->SetDesiredPlayerName(Wanted);
    }
    Status = TEXT("OPENING | reference fallback");
    if (!Player->OpenSource(MediaSource)) MediaFailed(TEXT(""));
}
void AVideoTwinProjector::MediaOpened(FString Url)
{
    Status = bMockSource ? TEXT("RECORDED LOOP | opening first frame") : TEXT("VIDEO | opening first frame");
    UE_LOG(LogTemp,Display,TEXT("VideoTwin camera %s media opened (player=%s)"),*CameraId,Player?*Player->GetPlayerName().ToString():TEXT("?"));
}
bool AVideoTwinProjector::FallBackToRecording(const TCHAR* Why)
{
    // A camera that cannot be reached must not leave a frozen placeholder on screen: play the recording instead
    // and say so in the status. Tried once per session; pressing Play again retries the live stream.
    if (StreamUrl.IsEmpty() || bLiveFailed || !RecordedSource || !Player) return false;
    bLiveFailed = true;
    UE_LOG(LogTemp,Warning,TEXT("VideoTwin camera %s live stream unavailable (%s): %s -- falling back to the recording"),*CameraId,Why,*StreamUrl);
    MediaSource = RecordedSource;
    bMockSource = true;
    bVideoReady = false;
    OpenAge = 0;
    Player->Close();
    Player->SetDesiredPlayerName(NAME_None);
    Player->SetLooping(true);
    Status = TEXT("OPENING | reference fallback");
    if (!Player->OpenSource(MediaSource)) { Status = TEXT("MEDIA FAILED | reference fallback"); }
    return true;
}
void AVideoTwinProjector::PlayRecording(const FString& Why)
{
    if (bLiveFailed) return;
    bLiveFailed = true;
    UE_LOG(LogTemp,Warning,TEXT("VideoTwin camera %s live picture unavailable (%s) -- playing the recording until it is back"),*CameraId,*Why);
    if (!RecordedSource) { Status = TEXT("LIVE UNAVAILABLE | no recording, reference image"); return; }
    if (!Player)
    {
        Player = NewObject<UMediaPlayer>(this);
        Player->PlayOnOpen = true;
        Player->OnMediaOpened.AddDynamic(this,&AVideoTwinProjector::MediaOpened);
        Player->OnMediaOpenFailed.AddDynamic(this,&AVideoTwinProjector::MediaFailed);
        VideoTexture = NewObject<UMediaTexture>(this);
        VideoTexture->NewStyleOutput = true;
        VideoTexture->AutoClear = false;
        VideoTexture->SetMediaPlayer(Player);
        VideoTexture->UpdateResource();
    }
    MediaSource = RecordedSource;
    bMockSource = true;
    bVideoReady = false;
    OpenAge = 0;
    Player->SetDesiredPlayerName(NAME_None);
    Player->SetLooping(true);
    Status = TEXT("OPENING | reference fallback");
    if (!Player->OpenSource(MediaSource)) Status = TEXT("MEDIA FAILED | reference fallback");
}
void AVideoTwinProjector::TickLiveDirect(float DeltaSeconds)
{
    if (!LiveReader) return;
    TArray<uint8> Pixels; int32 W = 0, H = 0;
    if (LiveReader->TakeFrame(Pixels, W, H, LiveSeq))
    {
        if (!LiveTexture || LiveTexture->GetSizeX() != W || LiveTexture->GetSizeY() != H)
        {
            LiveTexture = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
            // sRGB like the media texture it replaces: the material expects linear colour when it samples.
            LiveTexture->SRGB = true;
            LiveTexture->Filter = TF_Bilinear;
            LiveTexture->AddressX = TA_Clamp;
            LiveTexture->AddressY = TA_Clamp;
            LiveTexture->UpdateResource();
        }
        // The render thread copies from this buffer later; it owns it and frees it when done.
        const int32 Bytes = Pixels.Num();
        uint8* Data = static_cast<uint8*>(FMemory::Malloc(Bytes));
        FMemory::Memcpy(Data, Pixels.GetData(), Bytes);
        FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, W, H);
        LiveTexture->UpdateTextureRegions(0, 1, Region, W * 4, 4, Data,
            [](uint8* SrcData, const FUpdateTextureRegion2D* Regions) { FMemory::Free(SrcData); delete Regions; });
        bLiveHasPicture = true;
    }
    // Fresh = a picture in the last 3 s. A stalled camera shows its last picture for those 3 s, then the recording.
    bLiveFresh = bLiveHasPicture && LiveReader->SecondsSinceLastFrame() < 3.0;
    if (bLiveFresh)
    {
        LiveWaitAge = 0;
        if (bLiveFailed)
        {
            UE_LOG(LogTemp,Display,TEXT("VideoTwin camera %s live picture is back; leaving the recording"),*CameraId);
            bLiveFailed = false;
            if (Player) Player->Close();
            bMockSource = false;
        }
        bVideoReady = true;
        Status = FString::Printf(TEXT("LIVE STREAM (direct) | PLAYING %.0f fps"), LiveReader->GetRecentFps());
        return;
    }
    // Not fresh: give a first connection 12 s, a dropped one 3 s more, then show the recording meanwhile.
    LiveWaitAge += DeltaSeconds;
    if (!bLiveFailed && LiveWaitAge > (bLiveHasPicture ? 0.f : 12.f))
    {
        const FString Error = LiveReader->GetLastError();
        PlayRecording(Error.IsEmpty() ? FString(TEXT("no picture")) : Error);
    }
}
void AVideoTwinProjector::MediaFailed(FString Url)
{
    if (FallBackToRecording(TEXT("open failed"))) return;
    bVideoReady = false;
    Status = TEXT("MEDIA FAILED | reference fallback");
    UE_LOG(LogTemp,Warning,TEXT("VideoTwin camera %s media failed (player=%s, url=%s); using reference image"),*CameraId,Player?*Player->GetPlayerName().ToString():TEXT("?"),*StreamUrl);
}
void AVideoTwinProjector::TogglePlayback()
{
    if (!Player || !bVideoReady) return;
    if (Player->IsPlaying()) Player->Pause(); else Player->Play();
}
UTexture* AVideoTwinProjector::GetDisplayTexture() const
{
    if (bLiveDirect && bLiveFresh && LiveTexture) return LiveTexture.Get();
    return bVideoReady && VideoTexture ? static_cast<UTexture*>(VideoTexture.Get()) : static_cast<UTexture*>(ReferenceTexture.Get());
}
void AVideoTwinProjector::ResetCalibration()
{
    PositionOffset = FVector::ZeroVector;
    RotationOffset = FRotator::ZeroRotator;
    FovOffset = 0;
    PrincipalPoint = FVector2D(0.5,0.5);
    RadialDistortion = FVector2D::ZeroVector;
    LoadMeasuredCalibration();
    RefreshDepth();
}
void AVideoTwinProjector::RefreshDepth() { bDepthDirty = true; }
void AVideoTwinProjector::SetViewFocus(bool bFocused)
{
    // Post-process priority decides which projection is drawn last where two overlap. In a camera's own view its
    // projection is the only one that lines up with the picture, so it must win there.
    if (ProjectionPostProcess) ProjectionPostProcess->Priority = BasePriority + (bFocused ? 10000.f : 0.f);
    bViewFocused = bFocused;
}
void AVideoTwinProjector::TryReturnToLive(float DeltaSeconds)
{
    if (!bLiveFailed || StreamUrl.IsEmpty() || bLiveProbeInFlight || !Player) return;
    // Only an http(s) playlist can be checked cheaply without disturbing the recording that is playing meanwhile.
    if (!StreamUrl.StartsWith(TEXT("http"), ESearchCase::IgnoreCase)) return;
    LiveRetryAge += DeltaSeconds;
    if (LiveRetryAge < 15.f) return;
    LiveRetryAge = 0;
    bLiveProbeInFlight = true;
    TWeakObjectPtr<AVideoTwinProjector> Self(this);
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
    Req->SetURL(StreamUrl);
    Req->SetVerb(TEXT("GET"));
    Req->SetTimeout(5.f);
    Req->OnProcessRequestComplete().BindLambda([Self](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
    {
        AVideoTwinProjector* P = Self.Get();
        if (!P) return;
        P->bLiveProbeInFlight = false;
        if (!bConnected || !Response.IsValid() || Response->GetResponseCode() != 200 || !Response->GetContentAsString().StartsWith(TEXT("#EXTM3U"))) return;
        if (!P->bLiveFailed || !P->Player) return;
        UE_LOG(LogTemp,Display,TEXT("VideoTwin camera %s live stream answers again; leaving the recording"),*P->CameraId);
        P->bLiveFailed = false;
        P->MediaSource = P->RecordedSource;
        P->Player->Close();
        P->OpenVideo();
    });
    Req->ProcessRequest();
}
void AVideoTwinProjector::DumpDepth(const FString& PngPath)
{
    if (!DepthTarget) return;
    FTextureRenderTargetResource* Res = DepthTarget->GameThread_GetRenderTargetResource();
    TArray<FLinearColor> Px;
    if (!Res || !Res->ReadLinearColorPixels(Px)) { UE_LOG(LogTemp,Warning,TEXT("VTDIAG depth readback failed")); return; }
    const int32 W = DepthTarget->SizeX, H = DepthTarget->SizeY;
    TArray<FColor> Out; Out.SetNumUninitialized(W*H);
    float MinD = 1e9f, MaxD = 0; int32 Cleared = 0;
    for (int32 i=0;i<W*H;++i)
    {
        const float D = Px[i].R;
        if (D > 100000) { ++Cleared; Out[i] = FColor(0,0,90,255); continue; }
        MinD = FMath::Min(MinD,D); MaxD = FMath::Max(MaxD,D);
        const uint8 G = uint8(FMath::Clamp(255.f*(1.f-D/2000.f),0.f,255.f));
        Out[i] = FColor(G,G,G,255);
    }
    FFileHelper::CreateBitmap(*PngPath, W, H, Out.GetData());
    auto S = [&](float U, float V) { return Px[FMath::Clamp(int32(V*H),0,H-1)*W + FMath::Clamp(int32(U*W),0,W-1)].R; };
    UE_LOG(LogTemp,Display,TEXT("VTDIAG depth %dx%d min=%.1f max=%.1f cleared=%.1f%% centre=%.1f (0.25,0.7)=%.1f (0.75,0.7)=%.1f (0.5,0.1)=%.1f (0.5,0.9)=%.1f"),W,H,MinD,MaxD,100.f*Cleared/(W*H),S(.5f,.5f),S(.25f,.7f),S(.75f,.7f),S(.5f,.1f),S(.5f,.9f));
}
void AVideoTwinProjector::UpdateProjection()
{
    if (!ProjectionMID) return;
    const FTransform Pose = GetProjectionTransform();
    const float Fov = GetProjectionFov();
    if (!Pose.Equals(LastPose,0.001) || !FMath::IsNearlyEqual(Fov,LastFov) || !PrincipalPoint.Equals(LastPrincipal)) bDepthDirty = true;
    const FQuat Q = Pose.GetRotation();
    auto V = [this](FName Name, FVector Value) { ProjectionMID->SetVectorParameterValue(Name,FLinearColor(Value.X,Value.Y,Value.Z,0)); };
    V(TEXT("ProjectorPosition"),Pose.GetLocation());
    V(TEXT("ProjectorForward"),Q.GetForwardVector());
    V(TEXT("ProjectorRight"),Q.GetRightVector());
    V(TEXT("ProjectorUp"),Q.GetUpVector());
    V(TEXT("ReceiverMin"),ReceiverMin); V(TEXT("ReceiverMax"),ReceiverMax);
    ProjectionMID->SetVectorParameterValue(TEXT("Lens"),FLinearColor(FMath::Tan(FMath::DegreesToRadians(Fov*.5f)),GetProjectionAspect(),PrincipalPoint.X,PrincipalPoint.Y));
    ProjectionMID->SetVectorParameterValue(TEXT("Distortion"),FLinearColor(RadialDistortion.X,RadialDistortion.Y,0,0));
    ProjectionMID->SetVectorParameterValue(TEXT("Options"),FLinearColor(bEnabled&&!bMutedByView?Opacity:0,DepthToleranceCm,MaxDistanceCm,bViewFocused?2:(bUseOcclusion&&bDepthReady?1:0)));
    ProjectionMID->SetVectorParameterValue(TEXT("Display"),FLinearColor(bSplitView?1:0,bDepthDebug?1:0,bMaskTimestamp?1:0,ReceiverFeatherCm));
    ProjectionMID->SetTextureParameterValue(TEXT("VideoTex"),GetDisplayTexture());
    if (bDepthDirty)
    {
        DepthCapture->SetWorldTransform(Pose);
        DepthCapture->FOVAngle = Fov;
        // UE perspective projection: +X forward, +Y right, +Z up, reversed Z.
        const float X = 1.f/FMath::Tan(FMath::DegreesToRadians(Fov*.5f));
        DepthCapture->bUseCustomProjectionMatrix = true;
        DepthCapture->CustomProjectionMatrix = FMatrix(FPlane(X,0,0,0), FPlane(0,X*GetProjectionAspect(),0,0),
            FPlane(2*PrincipalPoint.X-1,1-2*PrincipalPoint.Y,0,1), FPlane(0,0,FMath::Max(1.f,DepthNearCm),0));
        DepthCapture->CaptureScene();
        bDepthReady = true;
        bDepthDirty = false;
        DepthAge = 0;
        LastPose = Pose; LastFov = Fov; LastPrincipal = PrincipalPoint;
    }
}
void AVideoTwinProjector::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    InitializeResources();
    DepthAge += DeltaSeconds;
    PlayAge += DeltaSeconds;
    if (bEnabled && DepthRefreshSeconds > 0.f && DepthAge > DepthRefreshSeconds) bDepthDirty = true;
    // A few scheduled re-captures after start, for geometry that streams in late. Staggered per projector.
    if (NextDepthCapture < DepthCaptureTimes.Num() && PlayAge >= DepthCaptureTimes[NextDepthCapture] + DepthCaptureStagger)
    {
        bDepthDirty = true;
        ++NextDepthCapture;
    }
    if (bLiveDirect)
    {
        TickLiveDirect(DeltaSeconds);
        if (!bLiveFailed) { UpdateProjection(); return; }
    }
    else TryReturnToLive(DeltaSeconds);
    if (Player)
    {
        OpenAge += DeltaSeconds;
        if (Player->HasError()) MediaFailed(TEXT(""));
        if (!bVideoReady && Player->IsReady() && VideoTexture && VideoTexture->GetWidth()>16 && Player->GetTime().GetTotalSeconds()>.05)
            bVideoReady = true;
        if (!bVideoReady && OpenAge > 12.f && FallBackToRecording(TEXT("no picture after 12 s"))) { UpdateProjection(); return; }
        if (bVideoReady) Status = (bLiveFailed?TEXT("LIVE UNAVAILABLE, RECORDED LOOP | "):(bMockSource?TEXT("RECORDED LOOP | "):(StreamUrl.IsEmpty()?TEXT("VIDEO | "):TEXT("LIVE STREAM | ")))) + FString(Player->IsPlaying()?TEXT("PLAYING"):TEXT("PAUSED"));
        else if (OpenAge>10 && Status.StartsWith(TEXT("OPENING"))) Status = TEXT("OPEN TIMEOUT | reference fallback");
    }
    UpdateProjection();
}
void AVideoTwinProjector::EndPlay(const EEndPlayReason::Type Reason)
{
    if (Player) { Player->OnMediaOpened.RemoveAll(this); Player->OnMediaOpenFailed.RemoveAll(this); Player->Close(); }
    if (LiveReader) { LiveReader->Shutdown(); LiveReader.Reset(); }
    if (ProjectionMID) ProjectionPostProcess->Settings.RemoveBlendable(ProjectionMID);
    Super::EndPlay(Reason);
}

AVideoTwinDemoGameMode::AVideoTwinDemoGameMode()
{
    PlayerControllerClass = AVideoTwinDemoController::StaticClass();
    HUDClass = AVideoTwinDemoHUD::StaticClass();
    DefaultPawnClass = nullptr;
}
void AVideoTwinDemoController::BeginPlay()
{
    Super::BeginPlay();
    for (TActorIterator<AVideoTwinProjector> It(GetWorld()); It; ++It) { Projector = *It; break; }
    Observer = GetWorld()->SpawnActor<ACameraActor>();
    Observer->GetCameraComponent()->bConstrainAspectRatio = false;
    Observer->GetCameraComponent()->PostProcessSettings.bOverride_AutoExposureMethod = true;
    Observer->GetCameraComponent()->PostProcessSettings.AutoExposureMethod = AEM_Manual;
    Observer->GetCameraComponent()->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
    Observer->GetCameraComponent()->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
    bShowMouseCursor = true;
    bAutoManageActiveCameraTarget = false;
    SetInputMode(FInputModeGameOnly());
    SetViewTarget(Observer);
    ConsoleCommand(TEXT("r.Shadow.Virtual.NonNanite.IncludeInCoarsePages 0"),false);
    ConsoleCommand(TEXT("r.Shadow.Virtual.OnePassProjection.MaxLightsPerPixel 32"),false);
    ShowSourceView();
    bAutomatedTest = FParse::Param(FCommandLine::Get(),TEXT("VideoTwinTest"));
}
void AVideoTwinDemoController::ShowSourceView()
{
    if (!Projector || !Observer) return;
    Observer->SetActorTransform(Projector->GetProjectionTransform());
    Observer->GetCameraComponent()->SetFieldOfView(Projector->GetProjectionFov());
}
void AVideoTwinDemoController::ShowOrbitView()
{
    if (!Projector || !Observer) return;
    const FTransform P = Projector->GetProjectionTransform();
    const FVector Target = P.GetLocation()+P.GetRotation().GetForwardVector()*650;
    FVector Position = P.GetLocation()+P.GetRotation().GetRightVector()*180+FVector(0,0,15);
    if (Projector->bMeasuredCalibration)
    {
        // The real camera sits in a corner (facade about 1.7 m to its right, glass partition behind it),
        // so stepping sideways leaves the building. Step into the room instead: forward along the floor and to the left.
        const FVector FlatForward = P.GetRotation().GetForwardVector().GetSafeNormal2D();
        Position = P.GetLocation()+FlatForward*300-P.GetRotation().GetRightVector()*250+FVector(0,0,-25);
    }
    Observer->SetActorLocationAndRotation(Position,(Target-Position).Rotation());
    Observer->GetCameraComponent()->SetFieldOfView(90);
}
void AVideoTwinDemoController::PlayerTick(float DeltaSeconds)
{
    Super::PlayerTick(DeltaSeconds);
    Elapsed += DeltaSeconds;
    if (!Projector || !Observer) return;
    if (WasInputKeyJustPressed(EKeys::One)) Projector->bEnabled = !Projector->bEnabled;
    if (WasInputKeyJustPressed(EKeys::Two)) ShowSourceView();
    if (WasInputKeyJustPressed(EKeys::Three)) ShowOrbitView();
    if (WasInputKeyJustPressed(EKeys::Four)) Projector->bSplitView = !Projector->bSplitView;
    if (WasInputKeyJustPressed(EKeys::Five)) Projector->bDepthDebug = !Projector->bDepthDebug;
    if (WasInputKeyJustPressed(EKeys::SpaceBar)) Projector->TogglePlayback();
    if (WasInputKeyJustPressed(EKeys::R)) Projector->ResetCalibration();
    if (WasInputKeyJustPressed(EKeys::O)) Projector->bUseOcclusion = !Projector->bUseOcclusion;
    if (WasInputKeyJustPressed(EKeys::PageUp)) Projector->FovOffset += .5f;
    if (WasInputKeyJustPressed(EKeys::PageDown)) Projector->FovOffset -= .5f;
    if (WasInputKeyJustPressed(EKeys::Left)) Projector->RotationOffset.Yaw -= .25f;
    if (WasInputKeyJustPressed(EKeys::Right)) Projector->RotationOffset.Yaw += .25f;
    if (WasInputKeyJustPressed(EKeys::Up)) Projector->RotationOffset.Pitch += .25f;
    if (WasInputKeyJustPressed(EKeys::Down)) Projector->RotationOffset.Pitch -= .25f;
    if (IsInputKeyDown(EKeys::RightMouseButton))
    {
        float X,Y; GetInputMouseDelta(X,Y);
        FRotator R = Observer->GetActorRotation(); R.Yaw += X*.16f; R.Pitch = FMath::Clamp(R.Pitch+Y*.16f,-89.f,89.f);
        Observer->SetActorRotation(R);
    }
    FVector Move = FVector::ZeroVector;
    if (IsInputKeyDown(EKeys::W)) Move += Observer->GetActorForwardVector();
    if (IsInputKeyDown(EKeys::S)) Move -= Observer->GetActorForwardVector();
    if (IsInputKeyDown(EKeys::D)) Move += Observer->GetActorRightVector();
    if (IsInputKeyDown(EKeys::A)) Move -= Observer->GetActorRightVector();
    if (IsInputKeyDown(EKeys::E)) Move.Z += 1;
    if (IsInputKeyDown(EKeys::Q)) Move.Z -= 1;
    Observer->AddActorWorldOffset(Move.GetClampedToMaxSize(1)*DeltaSeconds*(IsInputKeyDown(EKeys::LeftShift)?700:200));
    if (bAutomatedTest) RunAcceptance();
}
void AVideoTwinDemoController::RunAcceptance()
{
    const FString Dir = FPaths::ProjectSavedDir()/TEXT("VideoTwinMVP");
    IFileManager::Get().MakeDirectory(*Dir,true);
    auto Shot = [&](const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(Dir/Name,true,false); };
    if (TestStep==0 && Elapsed>12) { ShowSourceView(); Shot(TEXT("01_source.png")); ++TestStep; }
    else if (TestStep==1 && Elapsed>15) { Projector->bEnabled=false; ++TestStep; }
    else if (TestStep==2 && Elapsed>17) { Shot(TEXT("02_model.png")); ++TestStep; }
    else if (TestStep==3 && Elapsed>19) { Projector->bEnabled=true; ShowOrbitView(); ++TestStep; }
    else if (TestStep==4 && Elapsed>22) { Shot(TEXT("03_orbit.png")); ++TestStep; }
    else if (TestStep==5 && Elapsed>24) { Projector->bSplitView=true; ++TestStep; }
    else if (TestStep==6 && Elapsed>26) { Shot(TEXT("04_comparison.png")); ++TestStep; }
    else if (TestStep==7 && Elapsed>28)
    {
        Projector->bSplitView=false;
        const bool Ready=Projector->GetPlayer()&&Projector->GetPlayer()->IsPlaying()&&Projector->GetDisplayTexture()!=Projector->ReferenceTexture;
        const FString Result=FString::Printf(TEXT("{\"media_playing\":%s,\"depth_ready\":%s,\"camera_id\":\"%s\",\"elapsed_seconds\":%.2f,\"status\":\"%s\"}"),Ready?TEXT("true"):TEXT("false"),Projector->bDepthReady?TEXT("true"):TEXT("false"),*Projector->CameraId,Elapsed,*Projector->Status);
        FFileHelper::SaveStringToFile(Result,*(Dir/TEXT("runtime_acceptance.json")));
        UE_LOG(LogTemp,Display,TEXT("VIDEOTWIN_ACCEPTANCE %s"),*Result);
        ++TestStep;
    }
}
void AVideoTwinDemoHUD::DrawHUD()
{
    Super::DrawHUD();
    const AVideoTwinDemoController* PC=Cast<AVideoTwinDemoController>(GetOwningPlayerController());
    AVideoTwinProjector* P=PC?PC->GetProjector():nullptr;
    if (!P || !Canvas) return;
    const float W=Canvas->SizeX, H=Canvas->SizeY;
    DrawRect(FLinearColor(.015,.022,.035,.94),0,0,W,82);
    DrawText(TEXT("OFFICE / VIDEO TWIN"),FLinearColor(.1,.9,.8),22,13,nullptr,1.45);
    DrawText(TEXT("CAM 17  /  SINGLE-SOURCE MVP"),FLinearColor::White,22,42,nullptr,1.05);
    DrawText(P->Status,FLinearColor(1,.75,.2),W-390,15,nullptr,1.05);
    DrawText(TEXT("14 camera poses | 1 mock feed | lens uncalibrated"),FLinearColor(.65,.73,.8),W-390,43,nullptr,.85);
    DrawRect(FLinearColor(.015,.022,.035,.94),0,H-102,W,102);
    DrawText(TEXT("[1] Fusion   [2] Camera 17   [3] Offset view   [4] Compare   [5] Coverage   [Space] Pause   [O] Occlusion"),FLinearColor::White,22,H-90,nullptr,1);
    DrawText(TEXT("Move: WASD + Q/E   |   Look: hold RMB   |   Shift: fast   |   Lens: PgUp/PgDn   |   Aim: arrows   |   R: reset"),FLinearColor(.65,.73,.8),22,H-63,nullptr,.92);
    DrawText(FString::Printf(TEXT("Fusion %s | FOV %.1f | yaw offset %.2f | pitch offset %.2f | depth %s | mock = supplied still + test marker"),P->bEnabled?TEXT("ON"):TEXT("OFF"),P->GetProjectionFov(),P->RotationOffset.Yaw,P->RotationOffset.Pitch,P->bUseOcclusion?TEXT("ON"):TEXT("OFF")),FLinearColor(.1,.9,.8),22,H-36,nullptr,.9);
    const float PW=230, PH=PW*9/16;
    DrawRect(FLinearColor(.01,.02,.03,.9),W-PW-24,102,PW+8,PH+29);
    if (P->GetDisplayTexture()) DrawTexture(P->GetDisplayTexture(),W-PW-20,106,PW,PH,0,0,1,1);
    DrawText(TEXT("CAM 17 / RECORDED LOOP"),FLinearColor(.1,.9,.8),W-PW-17,PH+110,nullptr,.8);
}

void UVideoTwinRuntimeWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("VideoTwinRuntimeStatus"));
    Text->SetColorAndOpacity(FLinearColor(.1f,.95f,.85f,1.f));
    Text->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 14));
    Text->SetShadowOffset(FVector2D(1,1));
    Text->SetShadowColorAndOpacity(FLinearColor(0,0,0,.8f));
    Text->SetText(FText::FromString(TEXT("VIDEO TWIN  |  Z/X/C 控制摄像头17")));
    WidgetTree->RootWidget = Text;
    SetPositionInViewport(FVector2D(24, 94), false);
    SetDesiredSizeInViewport(FVector2D(620, 80));
}
void UVideoTwinRuntimeWidget::NativeTick(const FGeometry& Geometry, float DeltaSeconds)
{
    Super::NativeTick(Geometry, DeltaSeconds);
    if (!Text || !Feature) return;
    const TArray<TObjectPtr<AVideoTwinProjector>>& All = Feature->GetProjectors();
    FString Line = TEXT("VIDEO TWIN  |  Z 融合开关  X 切换摄像头视角  C 返回原视角");
    if (All.Num() > 0 && All[0])
    {
        FString Cams;
        for (int32 i = 0; i < All.Num(); ++i)
        {
            if (!All[i]) continue;
            // Say what is actually on screen. A looping recording looks alive, so "it moves" must never be
            // mistaken for "it is live".
            const FString& S = All[i]->Status;
            const TCHAR* What = TEXT("(无画面)");
            if (S.Contains(TEXT("PLAYING")) || S.Contains(TEXT("PAUSED")))
            {
                if (S.StartsWith(TEXT("LIVE STREAM"))) What = TEXT("实时");
                else if (S.StartsWith(TEXT("LIVE UNAVAILABLE"))) What = TEXT("录像(实时连不上)");
                else What = TEXT("录像");
            }
            Cams += FString::Printf(TEXT("%s%s%s %s"), i ? TEXT("   ") : TEXT(""), i == Feature->GetViewIndex() ? TEXT(">") : TEXT(""), *All[i]->CameraId, What);
        }
        Line += FString::Printf(TEXT("\n%d 路  %s  |  %s"), All.Num(), All[0]->bEnabled ? TEXT("融合开启") : TEXT("融合关闭"), *Cams);
    }
    Text->SetText(FText::FromString(Line));
}

AVideoTwinRuntimeFeature::AVideoTwinRuntimeFeature()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RuntimeVideoTwinRoot"));
}
void AVideoTwinRuntimeFeature::LoadMockAssets()
{
    if (!Projector) return;
    Projector->ReferenceTexture = Cast<UTexture2D>(StaticLoadObject(UTexture2D::StaticClass(), nullptr, TEXT("/Game/VideoTwinMVP/T_Camera17_Reference.T_Camera17_Reference")));
    Projector->MediaSource = Cast<UMediaSource>(StaticLoadObject(UMediaSource::StaticClass(), nullptr, TEXT("/Game/VideoTwinMVP/MS_Camera17_Mock.MS_Camera17_Mock")));
    Projector->ProjectionMaterial = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, TEXT("/Game/VideoTwinMVP/M_VideoProjection.M_VideoProjection")));
}
void AVideoTwinRuntimeFeature::BeginPlay()
{
    Super::BeginPlay();
    // -VideoTwinStartOff: start with fusion off, so the test run captures the bare model from every camera pose.
    // Those pictures are what a camera's calibration is checked against.
    if (FParse::Param(FCommandLine::Get(), TEXT("VideoTwinStartOff"))) bStartEnabled = false;
    if (FParse::Param(FCommandLine::Get(), TEXT("VideoTwinStartOn"))) bStartEnabled = true;
    // In a camera's own view only that camera's video is shown: the other projections are seen from the wrong
    // place there and only smear. "camera_view_only_own": false in calibration.json brings them back.
    bViewOnlyOwn = AVideoTwinProjector::GetCalibrationRootBool(TEXT("camera_view_only_own"), true);
    bLocalLightShadows = AVideoTwinProjector::GetCalibrationRootBool(TEXT("local_light_shadows"), false);
    TArray<FString> Ids;
    AVideoTwinProjector::GetEnabledCameraIds(Ids);
    if (Ids.Num() == 0) Ids.Add(TEXT("17"));     // no list in the calibration file: the original single-camera behaviour
    for (const FString& Id : Ids) SpawnProjectorFor(Id);
    if (Projectors.Num() == 0)
    {
        UE_LOG(LogTemp,Warning,TEXT("VideoTwin runtime: no camera could be set up"));
        return;
    }
    Projector = Projectors[0];
    Camera17 = ViewCameras[0];
    // Each depth capture is a full-scene render; never let two projectors do it in the same frame.
    for (int32 i = 0; i < Projectors.Num(); ++i) if (Projectors[i]) Projectors[i]->DepthCaptureStagger = .2f * i;
    bAutomatedTest = FParse::Param(FCommandLine::Get(), TEXT("VideoTwinTest"));
    if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
    {
        Controller = PC;
        bShowStatus = AVideoTwinProjector::GetCalibrationRootBool(TEXT("show_status"), false);
        if (bShowStatus) Widget = CreateWidget<UVideoTwinRuntimeWidget>(PC);
        if (Widget)
        {
            Widget->SetFeature(this);
            Widget->AddToViewport(12000);
        }
        BindRuntimeInput();
    }
    UE_LOG(LogTemp,Display,TEXT("VideoTwin runtime feature ready: %d camera(s) (Z/X/C; original controls remain unchanged)"),Projectors.Num());
}
AVideoTwinProjector* AVideoTwinRuntimeFeature::SpawnProjectorFor(const FString& Id)
{
    ACameraActor* Cam = nullptr;
#if WITH_EDITOR
    // Actor labels are an editor concept: GetActorLabel does not exist in a packaged build. The level's
    // hand-placed camera is therefore only looked up here. A packaged build does not need it: with a measured
    // calibration the projector knows its own pose and a view camera is spawned below.
    const FString Label = FString::Printf(TEXT("OntoTwin_Camera_%s"), *Id);
    for (TActorIterator<ACameraActor> It(GetWorld()); It; ++It)
    {
        const bool bLegacy17 = Id == TEXT("17") && (It->GetActorLabel().Equals(Camera17Label, ESearchCase::IgnoreCase) || It->GetName().Contains(TEXT("CameraActor_1")));
        if (It->GetActorLabel().Equals(Label, ESearchCase::IgnoreCase) || bLegacy17) { Cam = *It; break; }
    }
#endif
    const FTransform At = Cam ? FTransform(Cam->GetActorRotation(), Cam->GetActorLocation()) : FTransform::Identity;
    // Deferred, so the camera id and assets are in place before the projector's BeginPlay reads the calibration.
    AVideoTwinProjector* P = GetWorld()->SpawnActorDeferred<AVideoTwinProjector>(AVideoTwinProjector::StaticClass(), At);
    if (!P) return nullptr;
    P->CameraId = Id;
    P->SourceCamera = Cam;
    P->bMockSource = true;
    P->bEnabled = bStartEnabled;
    P->ReferenceTexture = Cast<UTexture2D>(StaticLoadObject(UTexture2D::StaticClass(), nullptr, TEXT("/Game/VideoTwinMVP/T_Camera17_Reference.T_Camera17_Reference")));
    // Each camera needs its OWN parent material. Post-process instances that share a parent are blended into a
    // single pass (their parameters are lerped), so with one shared material only the highest-priority camera
    // was ever drawn. M_VideoProjection_<id> are plain copies made by Tools/VideoTwin/dup_materials.py.
    const FString OwnMaterial = FString::Printf(TEXT("/Game/VideoTwinMVP/M_VideoProjection_%s.M_VideoProjection_%s"), *Id, *Id);
    P->ProjectionMaterial = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *OwnMaterial, nullptr, LOAD_NoWarn | LOAD_Quiet));
    if (!P->ProjectionMaterial) P->ProjectionMaterial = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, TEXT("/Game/VideoTwinMVP/M_VideoProjection.M_VideoProjection")));
    // A recording named Camera<id>_Real.mp4 wins. It is looked up by file name so that footage of real people
    // never has to become a tracked asset. Camera 17 falls back to the original still-image mock.
    const FString Movie = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / FString::Printf(TEXT("Movies/Camera%s_Real.mp4"), *Id));
    if (FPaths::FileExists(Movie))
    {
        UFileMediaSource* Source = NewObject<UFileMediaSource>(P);
        Source->SetFilePath(Movie);
        P->MediaSource = Source;
    }
    else if (Id == TEXT("17"))
    {
        P->MediaSource = Cast<UMediaSource>(StaticLoadObject(UMediaSource::StaticClass(), nullptr, TEXT("/Game/VideoTwinMVP/MS_Camera17_Mock.MS_Camera17_Mock")));
    }
    P->FinishSpawning(At);
    if (!P->bMeasuredCalibration && !Cam)
    {
        UE_LOG(LogTemp,Warning,TEXT("VideoTwin runtime: camera %s has neither a calibration nor a level camera; skipped"),*Id);
        P->Destroy();
        return nullptr;
    }
#if WITH_EDITOR
    P->SetActorLabel(FString::Printf(TEXT("VideoTwin_Runtime_Projector_%s"), *Id));
#endif
    const FTransform Pose = P->GetProjectionTransform();
    if (!Cam) Cam = GetWorld()->SpawnActor<ACameraActor>(Pose.GetLocation(), Pose.GetRotation().Rotator());
    if (P->bMeasuredCalibration && Cam)
    {
        // The level's cameras were placed by hand. Move this one to the measured pose for the session
        // (the saved map is not modified) so the X view, the camera gizmo and the projection agree.
        if (USceneComponent* CamRoot = Cam->GetRootComponent()) CamRoot->SetMobility(EComponentMobility::Movable);
        Cam->SetActorLocationAndRotation(Pose.GetLocation(), Pose.GetRotation());
        Cam->GetCameraComponent()->SetFieldOfView(P->GetProjectionFov());
        Cam->GetCameraComponent()->SetConstraintAspectRatio(false);
    }
    P->RefreshDepth();
    Projectors.Add(P);
    ViewCameras.Add(Cam);
    if (!P->ProjectionMaterial || !P->ReferenceTexture)
    {
        // In a packaged build this means /Game/VideoTwinMVP was not cooked (see DirectoriesToAlwaysCook in DefaultGame.ini).
        UE_LOG(LogTemp,Error,TEXT("VideoTwin runtime: camera %s has no projection material or reference texture; nothing will be projected. Was /Game/VideoTwinMVP cooked?"),*Id);
    }
    UE_LOG(LogTemp,Display,TEXT("VideoTwin runtime: camera %s ready (measured=%d, recording=%d, material=%d, calibration=%s)"),*Id,P->bMeasuredCalibration?1:0,FPaths::FileExists(Movie)?1:0,P->ProjectionMaterial?1:0,*VideoTwinCalibrationPath());
    return P;
}
void AVideoTwinRuntimeFeature::BindRuntimeInput()
{
    if (!Controller.IsValid()) return;
    EnableInput(Controller.Get());
}
void AVideoTwinRuntimeFeature::ApplyLocalLightShadows()
{
    if (bLocalLightShadows) return;
    int32 Total = 0, Switched = 0;
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        TInlineComponentArray<ULocalLightComponent*> Lights(*It);
        for (ULocalLightComponent* L : Lights)
        {
            ++Total;
            if (L && L->CastShadows) { L->SetCastShadows(false); ++Switched; }
        }
    }
    if (Switched > 0) UE_LOG(LogTemp,Display,TEXT("VideoTwin: shadows off on %d of %d local lights (local_light_shadows=false)"),Switched,Total);
}
void AVideoTwinRuntimeFeature::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    // A few passes after start, so lights in levels that stream in later are caught too.
    FeatureAge += DeltaSeconds;
    if (NextLightPass < LightPassTimes.Num() && FeatureAge >= LightPassTimes[NextLightPass]) { ApplyLocalLightShadows(); ++NextLightPass; }
    if (!Controller.IsValid()) return;
    // The opening pose for C: followed every frame until somebody first does something (walks, turns, presses X),
    // then frozen -- so it is the view the scene settled on by itself, after its default route. Waiting for the
    // pose to stand still for a few seconds worked in the editor but not in the packaged build, where the opening
    // view never held perfectly still and the pose was only taken after the first walk.
    if (!bStartPoseSaved)
        if (APawn* Pawn = Controller->GetPawn())
        {
            float MouseX = 0.f, MouseY = 0.f;
            Controller->GetInputMouseDelta(MouseX, MouseY);
            static const FKey Moves[] = { EKeys::W, EKeys::A, EKeys::S, EKeys::D, EKeys::Q, EKeys::E, EKeys::Up, EKeys::Down,
                                          EKeys::Left, EKeys::Right, EKeys::SpaceBar, EKeys::RightMouseButton, EKeys::LeftMouseButton, EKeys::X };
            bool bUserActed = FMath::Abs(MouseX) + FMath::Abs(MouseY) > 0.f;
            for (const FKey& K : Moves) bUserActed = bUserActed || Controller->IsInputKeyDown(K);
            if (bUserActed && FeatureAge > 3.f)
            {
                bStartPoseSaved = true;
                UE_LOG(LogTemp, Display, TEXT("VideoTwin opening position frozen for C at first input (after %.0f s)"), FeatureAge);
            }
            else
            {
                StartPawnLocation = Pawn->GetActorLocation();
                StartPawnRotation = Pawn->GetActorRotation();
                StartControlRotation = Controller->GetControlRotation();
                bHaveStartPose = true;
            }
        }
    // Poll the controller's edge state instead of binding repeated key-down events.
    // This makes one physical press produce one action even when the key is held.
    if (Controller->WasInputKeyJustPressed(EKeys::Z)) ToggleCamera17Fusion();
    if (Controller->WasInputKeyJustPressed(EKeys::X)) EnterCamera17View();
    // C = reset: leave a camera view if in one, and go back to where the session started. It used to only leave a
    // camera view, so in free roam it did nothing and looked broken.
    if (Controller->WasInputKeyJustPressed(EKeys::C)) { ExitCamera17View(); ResetToStart(); }
    if (bAutomatedTest) RunAcceptance(DeltaSeconds);
}
void AVideoTwinRuntimeFeature::RunAcceptance(float DeltaSeconds)
{
    if (!Projector || !Controller.IsValid()) return;
    TestElapsed += DeltaSeconds;
    if (GEngine)
    {
        // Wall clock of this machine, drawn into every frame. When the video source burns in its own wall clock,
        // one screenshot shows both and their difference is the end-to-end delay.
        const FDateTime Now = FDateTime::UtcNow();
        GEngine->AddOnScreenDebugMessage(9001, 0.f, FColor::Cyan, FString::Printf(TEXT("UE CLOCK  %02d:%02d.%03d"), Now.GetMinute(), Now.GetSecond(), Now.GetMillisecond()), true, FVector2D(3.f, 3.f));
    }
    const FString Dir = FPaths::ProjectSavedDir()/TEXT("VideoTwinMVP");
    IFileManager::Get().MakeDirectory(*Dir,true);
    auto Shot = [&](const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(Dir/Name,true,false); };
    if (TestStep==0 && TestElapsed>14) { Shot(TEXT("main_01_default.png")); ++TestStep; }
    else if (TestStep==1 && TestElapsed>16) { EnterCamera17View(); ++TestStep; }
    else if (TestStep==2 && TestElapsed>19) { Shot(TEXT("main_02_cam17.png")); ++TestStep; }
    else if (TestStep==3 && TestElapsed>21) { Projector->bEnabled=false; ++TestStep; }
    else if (TestStep==4 && TestElapsed>23) { Shot(TEXT("main_03_cam17_model.png")); ++TestStep; }
    else if (TestStep==5 && TestElapsed>25) { Projector->bEnabled=true; Projector->bUseOcclusion=false; ++TestStep; }
    else if (TestStep==6 && TestElapsed>27) { Shot(TEXT("main_05_cam17_no_occlusion.png")); ++TestStep; }
    else if (TestStep==7 && TestElapsed>29) { Projector->bUseOcclusion=true; Projector->bDepthDebug=true; ++TestStep; }
    else if (TestStep==8 && TestElapsed>31) { Shot(TEXT("main_06_cam17_coverage.png")); Projector->DumpDepth(Dir/TEXT("main_depth")); ++TestStep; }
    else if (TestStep==9 && TestElapsed>33)
    {
        Projector->bDepthDebug=false;
        // Same oblique view as the MVP map: into the room, forward and to the left of the camera.
        const FTransform P = Projector->GetProjectionTransform();
        const FVector Target = P.GetLocation()+P.GetRotation().GetForwardVector()*650;
        const FVector Position = P.GetLocation()+P.GetRotation().GetForwardVector().GetSafeNormal2D()*300-P.GetRotation().GetRightVector()*250+FVector(0,0,-25);
        if (!TestObserver) TestObserver = GetWorld()->SpawnActor<ACameraActor>(Position,(Target-Position).Rotation());
        if (TestObserver)
        {
            TestObserver->GetCameraComponent()->SetFieldOfView(90);
            TestObserver->GetCameraComponent()->SetConstraintAspectRatio(false);
            Controller->SetViewTarget(TestObserver);
        }
        ++TestStep;
    }
    else if (TestStep==10 && TestElapsed>36) { Shot(TEXT("main_04_oblique.png")); ++TestStep; }
    else if (TestStep==11 && TestElapsed>38)
    {
        const bool Ready=Projector->GetPlayer()&&Projector->GetPlayer()->IsPlaying()&&Projector->GetDisplayTexture()!=Projector->ReferenceTexture;
        const FString Result=FString::Printf(TEXT("{\"map\":\"%s\",\"media_playing\":%s,\"depth_ready\":%s,\"measured_calibration\":%s,\"status\":\"%s\"}"),*GetWorld()->GetMapName(),Ready?TEXT("true"):TEXT("false"),Projector->bDepthReady?TEXT("true"):TEXT("false"),Projector->bMeasuredCalibration?TEXT("true"):TEXT("false"),*Projector->Status);
        UE_LOG(LogTemp,Display,TEXT("VIDEOTWIN_MAIN_ACCEPTANCE %s"),*Result);
        TestStep = 100; TestElapsed = 0; TestCam = 1;
    }
    else if (TestStep >= 100 && TestStep < 200)
    {
        // Remaining cameras: its own view, then the same oblique view used for camera 17.
        if (TestCam >= Projectors.Num())
        {
            FString Report;
            for (AVideoTwinProjector* P : Projectors) Report += FString::Printf(TEXT("%s{\"id\":\"%s\",\"measured\":%s,\"status\":\"%s\"}"),Report.IsEmpty()?TEXT(""):TEXT(","),*P->CameraId,P->bMeasuredCalibration?TEXT("true"):TEXT("false"),*P->Status);
            const FString Result=FString::Printf(TEXT("{\"map\":\"%s\",\"cameras\":[%s]}"),*GetWorld()->GetMapName(),*Report);
            FFileHelper::SaveStringToFile(Result,*(Dir/TEXT("main_acceptance.json")));
            UE_LOG(LogTemp,Display,TEXT("VIDEOTWIN_MAIN_ACCEPTANCE_ALL %s"),*Result);
            TestStep = 200;
            return;
        }
        AVideoTwinProjector* P = Projectors[TestCam];
        const FString Id = P->CameraId;
        if (TestStep==100) { if (ViewCameras[TestCam]) Controller->SetViewTarget(ViewCameras[TestCam]); TestElapsed = 0; TestStep = 101; }
        else if (TestStep==101 && TestElapsed>3) { Shot(*FString::Printf(TEXT("main_cam%s_view.png"),*Id)); TestStep = 102; }
        else if (TestStep==102 && TestElapsed>5)
        {
            const FTransform T = P->GetProjectionTransform();
            const FVector Target = T.GetLocation()+T.GetRotation().GetForwardVector()*650;
            const FVector Position = T.GetLocation()+T.GetRotation().GetForwardVector().GetSafeNormal2D()*300-T.GetRotation().GetRightVector()*250+FVector(0,0,-25);
            if (TestObserver) { TestObserver->SetActorLocationAndRotation(Position,(Target-Position).Rotation()); Controller->SetViewTarget(TestObserver); }
            TestStep = 103;
        }
        else if (TestStep==103 && TestElapsed>8) { Shot(*FString::Printf(TEXT("main_cam%s_oblique.png"),*Id)); TestStep = 104; }
        else if (TestStep==104 && TestElapsed>9) { ++TestCam; TestStep = 100; }
    }
}
void AVideoTwinRuntimeFeature::HandleToggle() { ToggleCamera17Fusion(); }
void AVideoTwinRuntimeFeature::HandleEnterView() { EnterCamera17View(); }
void AVideoTwinRuntimeFeature::HandleExitView() { ExitCamera17View(); }
void AVideoTwinRuntimeFeature::ToggleCamera17Fusion()
{
    if (Projector)
    {
        const bool bOn = !Projector->bEnabled;
        // The depth maps stay valid while fusion is off (the scene does not move), so no re-capture here:
        // it used to re-render all of them in the same frame on every press.
        for (AVideoTwinProjector* P : Projectors) if (P) P->bEnabled = bOn;
        UE_LOG(LogTemp, Display, TEXT("VideoTwin fusion %s for %d camera(s)"), bOn ? TEXT("enabled") : TEXT("disabled"), Projectors.Num());
    }
}
bool AVideoTwinRuntimeFeature::IsCamera17FusionEnabled() const { return Projector && Projector->bEnabled; }
void AVideoTwinRuntimeFeature::EnterCamera17View()
{
    if (Projectors.Num() == 0 || !Controller.IsValid()) return;
    if (!bInCamera17View) SavedViewTarget = Controller->GetViewTarget();
    bInCamera17View = true;
    // First press shows the first camera; pressing again steps through the rest.
    ViewIndex = (ViewIndex + 1) % Projectors.Num();
    if (ViewCameras[ViewIndex]) Controller->SetViewTargetWithBlend(ViewCameras[ViewIndex], .35f, VTBlend_Cubic);
    ApplyViewFocus();
    UE_LOG(LogTemp, Display, TEXT("VideoTwin entered camera %s view"), *Projectors[ViewIndex]->CameraId);
}
void AVideoTwinRuntimeFeature::ExitCamera17View()
{
    if (!Controller.IsValid()) return;
    bInCamera17View = false;
    ViewIndex = -1;
    ApplyViewFocus();
    if (SavedViewTarget.IsValid()) Controller->SetViewTargetWithBlend(SavedViewTarget.Get(), .35f, VTBlend_Cubic);
    UE_LOG(LogTemp, Display, TEXT("VideoTwin returned to original view"));
}
void AVideoTwinRuntimeFeature::ResetToStart()
{
    if (!bHaveStartPose || !Controller.IsValid()) return;
    if (APawn* Pawn = Controller->GetPawn())
    {
        Pawn->SetActorLocationAndRotation(StartPawnLocation, StartPawnRotation, false, nullptr, ETeleportType::TeleportPhysics);
        Controller->SetControlRotation(StartControlRotation);
        UE_LOG(LogTemp, Display, TEXT("VideoTwin reset to the opening position"));
    }
}
void AVideoTwinRuntimeFeature::ApplyViewFocus()
{
    for (int32 i = 0; i < Projectors.Num(); ++i)
    {
        AVideoTwinProjector* P = Projectors[i];
        if (!P) continue;
        P->SetViewFocus(i == ViewIndex);
        P->bMutedByView = bViewOnlyOwn && ViewIndex >= 0 && i != ViewIndex;
    }
}
void AVideoTwinRuntimeFeature::EndPlay(const EEndPlayReason::Type Reason)
{
    ExitCamera17View();
    if (Widget) Widget->RemoveFromParent();
    for (AVideoTwinProjector* P : Projectors) if (P) P->Destroy();
    Projectors.Reset();
    Projector = nullptr;
    Super::EndPlay(Reason);
}
