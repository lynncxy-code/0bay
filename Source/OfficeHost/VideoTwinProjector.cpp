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
bool AVideoTwinProjector::LoadMeasuredCalibration()
{
    bMeasuredCalibration = false;
    // -VideoTwinNoCalib keeps the old hand-placed pose so the two can be compared side by side.
    if (FParse::Param(FCommandLine::Get(), TEXT("VideoTwinNoCalib"))) return false;
    const FString Path = FPaths::ProjectDir() / TEXT("Tools/VideoTwin/calibration.json");
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
    bDepthDirty = true;
}
void AVideoTwinProjector::BeginPlay()
{
    Super::BeginPlay();
    LoadMeasuredCalibration();
    InitializeResources();
    OpenVideo();
}
void AVideoTwinProjector::OpenVideo()
{
    bVideoReady = false;
    OpenAge = 0;
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
    Status = TEXT("OPENING | reference fallback");
    if (!Player->OpenSource(MediaSource)) MediaFailed(TEXT(""));
}
void AVideoTwinProjector::MediaOpened(FString Url)
{
    Status = bMockSource ? TEXT("RECORDED LOOP | opening first frame") : TEXT("VIDEO | opening first frame");
    UE_LOG(LogTemp,Display,TEXT("VideoTwin camera %s media opened"),*CameraId);
}
void AVideoTwinProjector::MediaFailed(FString Url)
{
    bVideoReady = false;
    Status = TEXT("MEDIA FAILED | reference fallback");
    UE_LOG(LogTemp,Warning,TEXT("VideoTwin camera %s media failed; using reference image"),*CameraId);
}
void AVideoTwinProjector::TogglePlayback()
{
    if (!Player || !bVideoReady) return;
    if (Player->IsPlaying()) Player->Pause(); else Player->Play();
}
UTexture* AVideoTwinProjector::GetDisplayTexture() const
{
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
    ProjectionMID->SetVectorParameterValue(TEXT("Options"),FLinearColor(bEnabled?Opacity:0,DepthToleranceCm,MaxDistanceCm,bUseOcclusion&&bDepthReady?1:0));
    ProjectionMID->SetVectorParameterValue(TEXT("Display"),FLinearColor(bSplitView?1:0,bDepthDebug?1:0,bMaskTimestamp?1:0,0));
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
    // The mock office is static; update at 2 Hz for doors/chairs moved in the demo.
    if (bEnabled && DepthAge > .5f) bDepthDirty = true;
    if (Player)
    {
        OpenAge += DeltaSeconds;
        if (Player->HasError()) MediaFailed(TEXT(""));
        if (!bVideoReady && Player->IsReady() && VideoTexture && VideoTexture->GetWidth()>16 && Player->GetTime().GetTotalSeconds()>.05)
            bVideoReady = true;
        if (bVideoReady) Status = (bMockSource?TEXT("RECORDED LOOP | "):TEXT("VIDEO | ")) + FString(Player->IsPlaying()?TEXT("PLAYING"):TEXT("PAUSED"));
        else if (OpenAge>10 && Status.StartsWith(TEXT("OPENING"))) Status = TEXT("OPEN TIMEOUT | reference fallback");
    }
    UpdateProjection();
}
void AVideoTwinProjector::EndPlay(const EEndPlayReason::Type Reason)
{
    if (Player) { Player->OnMediaOpened.RemoveAll(this); Player->OnMediaOpenFailed.RemoveAll(this); Player->Close(); }
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
    const AVideoTwinProjector* P = Feature->GetProjector();
    FString Line = TEXT("VIDEO TWIN  |  Z 融合  X 摄像头17视角  C 返回原视角");
    if (P)
    {
        Line += FString::Printf(TEXT("\n摄像头17  %s  |  %s  |  深度遮挡 %s"),
            P->bEnabled ? TEXT("融合开启") : TEXT("融合关闭"), *P->Status,
            P->bUseOcclusion ? TEXT("开启") : TEXT("关闭"));
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
    for (TActorIterator<ACameraActor> It(GetWorld()); It; ++It)
    {
        if (It->GetActorLabel().Equals(Camera17Label, ESearchCase::IgnoreCase) || It->GetName().Contains(TEXT("CameraActor_1"))) { Camera17 = *It; break; }
    }
    if (!Camera17)
    {
        UE_LOG(LogTemp,Warning,TEXT("VideoTwin runtime: authored camera 17 was not found"));
        return;
    }
    Projector = GetWorld()->SpawnActor<AVideoTwinProjector>(Camera17->GetActorLocation(), Camera17->GetActorRotation());
    Projector->CameraId = TEXT("17");
    Projector->SourceCamera = Camera17;
    Projector->bMockSource = true;
    Projector->bEnabled = bStartEnabled;
    Projector->SetActorLabel(TEXT("VideoTwin_Runtime_Projector_17"));
    LoadMockAssets();
    // SpawnActor may run the projector's BeginPlay before these assets are assigned.
    // Explicitly open the source after wiring the runtime assets so the original map
    // follows the same path as the standalone acceptance map.
    Projector->OpenVideo();
    if (Projector->bMeasuredCalibration)
    {
        // The level's camera 17 was placed by hand, about two metres from where the real camera is.
        // Move it to the measured pose for this session (the saved map is not modified) so that the
        // X view, the camera gizmo and the projection all agree.
        const FTransform Pose = Projector->GetProjectionTransform();
        if (USceneComponent* CamRoot = Camera17->GetRootComponent()) CamRoot->SetMobility(EComponentMobility::Movable);
        Camera17->SetActorLocationAndRotation(Pose.GetLocation(), Pose.GetRotation());
        Camera17->GetCameraComponent()->SetFieldOfView(Projector->GetProjectionFov());
        Camera17->GetCameraComponent()->SetConstraintAspectRatio(false);
        UE_LOG(LogTemp,Display,TEXT("VideoTwin runtime: level camera 17 moved to the measured pose for this session"));
    }
    Projector->RefreshDepth();
    bAutomatedTest = FParse::Param(FCommandLine::Get(), TEXT("VideoTwinTest"));
    if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
    {
        Controller = PC;
        Widget = CreateWidget<UVideoTwinRuntimeWidget>(PC);
        if (Widget)
        {
            Widget->SetFeature(this);
            Widget->AddToViewport(12000);
        }
        BindRuntimeInput();
    }
    UE_LOG(LogTemp,Display,TEXT("VideoTwin runtime feature ready: camera 17 (Z/X/C; original controls remain unchanged)"));
}
void AVideoTwinRuntimeFeature::BindRuntimeInput()
{
    if (!Controller.IsValid()) return;
    EnableInput(Controller.Get());
}
void AVideoTwinRuntimeFeature::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!Controller.IsValid()) return;
    // Poll the controller's edge state instead of binding repeated key-down events.
    // This makes one physical press produce one action even when the key is held.
    if (Controller->WasInputKeyJustPressed(EKeys::Z)) ToggleCamera17Fusion();
    if (Controller->WasInputKeyJustPressed(EKeys::X)) EnterCamera17View();
    if (Controller->WasInputKeyJustPressed(EKeys::C)) ExitCamera17View();
    if (bAutomatedTest) RunAcceptance(DeltaSeconds);
}
void AVideoTwinRuntimeFeature::RunAcceptance(float DeltaSeconds)
{
    if (!Projector || !Controller.IsValid()) return;
    TestElapsed += DeltaSeconds;
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
        FFileHelper::SaveStringToFile(Result,*(Dir/TEXT("main_acceptance.json")));
        UE_LOG(LogTemp,Display,TEXT("VIDEOTWIN_MAIN_ACCEPTANCE %s"),*Result);
        ++TestStep;
    }
}
void AVideoTwinRuntimeFeature::HandleToggle() { ToggleCamera17Fusion(); }
void AVideoTwinRuntimeFeature::HandleEnterView() { EnterCamera17View(); }
void AVideoTwinRuntimeFeature::HandleExitView() { ExitCamera17View(); }
void AVideoTwinRuntimeFeature::ToggleCamera17Fusion()
{
    if (Projector)
    {
        Projector->bEnabled = !Projector->bEnabled;
        Projector->RefreshDepth();
        UE_LOG(LogTemp, Display, TEXT("VideoTwin camera 17 fusion %s"), Projector->bEnabled ? TEXT("enabled") : TEXT("disabled"));
    }
}
bool AVideoTwinRuntimeFeature::IsCamera17FusionEnabled() const { return Projector && Projector->bEnabled; }
void AVideoTwinRuntimeFeature::EnterCamera17View()
{
    if (!Projector || !Camera17 || !Controller.IsValid()) return;
    if (!bInCamera17View) SavedViewTarget = Controller->GetViewTarget();
    bInCamera17View = true;
    Controller->SetViewTargetWithBlend(Camera17, .35f, VTBlend_Cubic);
    UE_LOG(LogTemp, Display, TEXT("VideoTwin entered camera 17 view"));
}
void AVideoTwinRuntimeFeature::ExitCamera17View()
{
    if (!Controller.IsValid()) return;
    bInCamera17View = false;
    if (SavedViewTarget.IsValid()) Controller->SetViewTargetWithBlend(SavedViewTarget.Get(), .35f, VTBlend_Cubic);
    UE_LOG(LogTemp, Display, TEXT("VideoTwin returned to original view"));
}
void AVideoTwinRuntimeFeature::EndPlay(const EEndPlayReason::Type Reason)
{
    ExitCamera17View();
    if (Widget) Widget->RemoveFromParent();
    if (Projector) Projector->Destroy();
    Super::EndPlay(Reason);
}
