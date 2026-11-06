#include "Go2DeviceScanner.h"
#include "Misc/ConfigCacheIni.h"
#include "TempoGo2Course.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Components/PostProcessComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "UObject/ConstructorHelpers.h"
#include "InputCoreTypes.h"
#include "TwinSceneManager.h"
#include "SceneInteraction/TwinInteractionManagerComponent.h"
#include "WebInteraction/OntoTwinWebInteractionComponent.h"
#include "TimerManager.h"

AGo2DeviceScanner::AGo2DeviceScanner(){
    PrimaryActorTick.bCanEverTick=true;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("ScanOrigin"));
    ScanGrid=CreateDefaultSubobject<UNiagaraComponent>(TEXT("ScanGridEffect"));
    ScanGrid->SetupAttachment(RootComponent);ScanGrid->SetAutoActivate(false);
    ScanPostProcess=CreateDefaultSubobject<UPostProcessComponent>(TEXT("ScanGridPostProcess"));
    ScanPostProcess->SetupAttachment(RootComponent);ScanPostProcess->bUnbound=true;ScanPostProcess->Priority=100;ScanPostProcess->BlendWeight=0;
    DemoCamera=CreateDefaultSubobject<UCameraComponent>(TEXT("ScanDemoCamera"));
    DemoCamera->SetupAttachment(RootComponent);DemoCamera->FieldOfView=65;
    static ConstructorHelpers::FObjectFinder<UNiagaraSystem> N(TEXT("/Game/ScanGridEffect/Niagara/N_Scan01.N_Scan01"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> M(TEXT("/Game/ScanGridEffect/Materials/PPI_Scan01.PPI_Scan01"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> H(TEXT("/Game/ScanningSystem/Materials/TranslucentMaterials/MI_TranslucentOverlay.MI_TranslucentOverlay"));
    static ConstructorHelpers::FObjectFinder<UMaterialParameterCollection> C(TEXT("/Game/ScanGridEffect/Materials/MaterialFunctions/MPC_Scan.MPC_Scan"));
    if(N.Succeeded())ScanGrid->SetAsset(N.Object);
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> FloorMaterial(
        TEXT("/Game/Robots/UnitreeGo2/Scanning/MI_Go2NativeFloorScan.MI_Go2NativeFloorScan"));
    FloorScanMaterial = FloorMaterial.Object;
    ScanMaterial=M.Object;HighlightMaterial=H.Object;ScanCollection=C.Object;
}
void AGo2DeviceScanner::BeginPlay(){
    Super::BeginPlay();
    // Project timing overrides serialized map values, so timing edits need no map resave.
    GConfig->GetFloat(TEXT("Go2.SecurityBusinessIntro"),TEXT("ScanDelaySeconds"),SecurityScanDelaySeconds,GGameIni);
    GConfig->GetFloat(TEXT("Go2.SecurityBusinessIntro"),TEXT("OverviewDelaySeconds"),SecurityIntroSeconds,GGameIni);
    if(!Robot)for(TActorIterator<ATempoGo2Course> It(GetWorld());It;++It){Robot=*It;break;}
    if(Robot){
        AddTickPrerequisiteActor(Robot);
        Robot->OnStopSettled.AddUObject(this,&AGo2DeviceScanner::OnRouteStop);
        Robot->StopWorkPending.BindWeakLambda(this,[this](){return bScanning || PendingScans>0;});
    }
    // F7 keeps its own entry camera; the scanner takes the camera only on F8/intro.
    bUseDemoCamera=false;
    BindBusinessEntry();
    if(ScanMaterial){ScanMID=UMaterialInstanceDynamic::Create(ScanMaterial,this);ScanPostProcess->Settings.AddBlendable(ScanMID,1.f);}
    // Existing level instances can carry the previous floor material.
    FloorScanMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Robots/UnitreeGo2/Scanning/MI_Go2NativeFloorScan.MI_Go2NativeFloorScan"));
    if (FloorScanMaterial)
        FloorScanMID = UMaterialInstanceDynamic::Create(FloorScanMaterial, this);
    if(HighlightMaterial){HighlightMID=UMaterialInstanceDynamic::Create(HighlightMaterial,this);HighlightMID->SetVectorParameterValue(TEXT("Colour"),FLinearColor(3.f,27.f,7.5f));HighlightMID->SetScalarParameterValue(TEXT("Exponent"),1.35f);}
    ConfigureScanParticles(false);
    RefreshTargets();
    UE_LOG(LogTemp,Display,TEXT("GO2_SCAN_READY targets=%d robot=%s"),TargetCount,*GetNameSafe(Robot));
}
void AGo2DeviceScanner::RefreshTargets(){
    for(TActorIterator<AActor> It(GetWorld());It;++It){
        AActor* Actor=*It;if(Actor==Robot||Actor==this||Actor->IsHidden())continue;
        TInlineComponentArray<UStaticMeshComponent*> Meshes(Actor);
        for(auto* Mesh:Meshes){
            if(!Mesh->GetStaticMesh()||!Mesh->IsVisible()||Mesh->bHiddenInGame)continue;
            const FString Path=Mesh->GetStaticMesh()->GetPathName();
            const bool Device=Actor->ActorHasTag(TEXT("Go2ScanTarget"))||Path.StartsWith(TEXT("/Game/OfficeDevices_20261103/"))||Path.Contains(TEXT("SM_MonitorCamera_Frozen"));
            if(!Device||Targets.ContainsByPredicate([Mesh](const FTarget& T){return T.Mesh.Get()==Mesh;}))continue;
            FTarget T;T.Mesh=Mesh;T.OriginalOverlay=Mesh->GetOverlayMaterial();Targets.Add(T);
        }
    }
    TargetCount=0;for(const FTarget& T:Targets)if(auto* M=T.Mesh.Get())if(M->IsVisible()&&!M->bHiddenInGame&&!M->GetOwner()->IsHidden())TargetCount++;
}
void AGo2DeviceScanner::ConfigureScanParticles(bool bUseOriginal)
{
    // The manual mode uses the authored BC_ScanEnvironment01 particle settings.
    ScanGrid->SetVariableFloat(TEXT("User.DotsGrid Size"), bUseOriginal ? 8.f : 1.5f);
    ScanGrid->SetVariableFloat(TEXT("User.Lines Size"), bUseOriginal ? 10.f : .35f);
    ScanGrid->SetVariableLinearColor(TEXT("User.DotsGrid Color"), bUseOriginal
        ? FLinearColor(0.f, .5f, 1.f, .2f) : FLinearColor(.1f, .8f, 1.2f, 1.f));
    for (const TCHAR* name : {TEXT("User.GridLines ColorX"), TEXT("User.GridLines ColorY"), TEXT("User.GridLines ColorZ")})
        ScanGrid->SetVariableLinearColor(name, bUseOriginal
            ? FLinearColor(0.f, .5f, 1.f, .02f) : FLinearColor(.05f, .25f, .4f, 1.f));
}
void AGo2DeviceScanner::StartScan(){
    if(!Robot||!ScanMID||!ScanCollection)return;
    bScanning=true;ScanAge=0;CurrentRadius=0;DetectedCount=0;ScanCount++;NextScan=Age+FMath::Max(ScanInterval,ScanDuration+1.f);
    if (!bFloorGridActive)
    {
        ScanGrid->SetWorldLocation(Robot->BodyWorldPosition);
        ScanGrid->Activate(true);
    }
    UE_LOG(LogTemp,Display,TEXT("GO2_SCAN_START cycle=%d center=%s"),ScanCount,*Robot->BodyWorldPosition.ToString());
}
void AGo2DeviceScanner::ToggleFloorScan()
{
    if (bFloorGridActive)
    {
        bFloorGridActive = false;
        ScanGrid->DeactivateImmediate();
        ScanGrid->SetUsingAbsoluteLocation(false);
        ConfigureScanParticles(false);
        for (FTarget& target : Targets)
        {
            if (!target.bHeldByFloor) continue;
            if (auto* mesh = target.Mesh.Get()) mesh->SetOverlayMaterial(target.OriginalOverlay.Get());
            target.bHeldByFloor = false;
            target.bHighlighted = false;
        }
        if (bManualDiscovery)
        {
            bScanning = false;
            bManualDiscovery = false;
        }
        ScanPostProcess->Settings.RemoveBlendable(FloorScanMID);
        ScanPostProcess->Settings.AddBlendable(ScanMID, 1.f);
        ScanPostProcess->BlendWeight = 0.f;
        UE_LOG(LogTemp, Display, TEXT("GO2_FLOOR_GRID_OFF"));
        return;
    }
    if (!Robot || !FloorScanMID || !ScanMID || !ScanCollection) return;

    FBox floor(ForceInit), roof(ForceInit);
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        TInlineComponentArray<UStaticMeshComponent*> components(*It);
        for (const UStaticMeshComponent* component : components)
        {
            const UStaticMesh* mesh = component->GetStaticMesh();
            if (!mesh) continue;
            const FString path = mesh->GetPathName();
            if (path == TEXT("/Game/OfficeShell/Meshes/SM_Floor.SM_Floor"))
                floor += component->Bounds.GetBox();
            else if (path == TEXT("/Game/OfficeShell/Meshes/SM_RoofSlab.SM_RoofSlab"))
                roof += component->Bounds.GetBox();
        }
    }
    if (!floor.IsValid || !roof.IsValid)
    {
        UE_LOG(LogTemp, Warning, TEXT("GO2_FLOOR_GRID missing floor or roof geometry"));
        return;
    }
    const FVector lower = floor.Min - FVector(2.f);
    const FVector upper(floor.Max.X + 2.f, floor.Max.Y + 2.f, roof.Max.Z + 2.f);
    FloorOrigin = Robot->BodyWorldPosition;
    const double dx = FMath::Max(FMath::Abs(lower.X - FloorOrigin.X), FMath::Abs(upper.X - FloorOrigin.X));
    const double dy = FMath::Max(FMath::Abs(lower.Y - FloorOrigin.Y), FMath::Abs(upper.Y - FloorOrigin.Y));
    const double dz = FMath::Max(FMath::Abs(lower.Z - FloorOrigin.Z), FMath::Abs(upper.Z - FloorOrigin.Z));
    FloorSweepRadius = FMath::Sqrt(dx * dx + dy * dy + dz * dz) + 40.f;
    FloorSweepAge = 0.f;
    FloorScanMID->SetVectorParameterValue(TEXT("FloorMin"), FLinearColor(lower));
    FloorScanMID->SetVectorParameterValue(TEXT("FloorMax"), FLinearColor(upper));
    FloorScanMID->SetVectorParameterValue(TEXT("Origin"), FLinearColor(FloorOrigin));
    FloorScanMID->SetScalarParameterValue(TEXT("Radius"), 0.f);
    bFloorGridActive = true;
    ConfigureScanParticles(true);
    ScanGrid->SetUsingAbsoluteLocation(true);
    ScanGrid->SetWorldLocation(FloorOrigin);
    ScanGrid->Activate(true);
    bManualDiscovery = true;
    StartScan();
    ScanPostProcess->Settings.RemoveBlendable(ScanMID);
    ScanPostProcess->Settings.AddBlendable(FloorScanMID, 1.f);
    ScanPostProcess->BlendWeight = 1.f;
    UE_LOG(LogTemp, Display, TEXT("GO2_FLOOR_GRID_ON min=%s max=%s range=%.1f deviceRange=%.1f"),
        *lower.ToString(), *upper.ToString(), FloorSweepRadius, ScanRadius);
}

void AGo2DeviceScanner::UpdateFloorSweep(float DeltaSeconds)
{
    if (!bFloorGridActive || FloorSweepAge >= ScanDuration) return;
    FloorSweepAge = FMath::Min(FloorSweepAge + DeltaSeconds, ScanDuration);
    FloorScanMID->SetScalarParameterValue(TEXT("Radius"),
        FloorSweepRadius * FloorSweepAge / FMath::Max(ScanDuration, .01f));
}

void AGo2DeviceScanner::BindBusinessEntry(){
    if(BusinessWeb.IsValid())return;
    for(TActorIterator<ATwinSceneManager> It(GetWorld());It;++It){
        auto* Web=It->WebInteractionManager;
        if(!Web || Web->BusinessIntroRequested.IsBound())continue;
        BusinessWeb=Web;
        Web->BusinessIntroRequested.BindUObject(this,&AGo2DeviceScanner::BeginBusinessIntro);
        Web->BusinessIntroCancelled.AddUObject(this,&AGo2DeviceScanner::CancelBusinessIntro);
        Web->SecurityBusinessExited.AddUObject(this,&AGo2DeviceScanner::StopSecurityEffects);
        break;
    }
}
void AGo2DeviceScanner::UpdateFollowCamera(){
    if(!Robot)return;
    const FVector Center=Robot->BodyWorldPosition;
    const FVector Look=Center+FVector(0,0,70),Pos=Center+FVector(370,450,200);
    DemoCamera->SetWorldLocationAndRotation(Pos,(Look-Pos).Rotation());
}
void AGo2DeviceScanner::SetFollowCamera(bool bEnabled,bool bRestoreView){
    auto* PC=GetWorld()->GetFirstPlayerController();
    if(!PC)return;
    if(bEnabled){
        if(!bUseDemoCamera)PreviousCamera=PC->GetViewTarget();
        bUseDemoCamera=true;UpdateFollowCamera();
        PC->SetViewTargetWithBlend(this,.8f,EViewTargetBlendFunction::VTBlend_Cubic,2.f,true);
    }else{
        bUseDemoCamera=false;
        if(bRestoreView){
            AActor* Target=PreviousCamera.IsValid()?PreviousCamera.Get():PC->GetPawn();
            if(Target)PC->SetViewTargetWithBlend(Target,.5f,EViewTargetBlendFunction::VTBlend_Cubic,2.f,true);
        }
        PreviousCamera.Reset();
    }
}
void AGo2DeviceScanner::RequestAutomaticScan(){
    if(bScanning)++PendingScans;else StartScan();
}
void AGo2DeviceScanner::OnRouteStop(int32 StopIndex){
    ++StopScanRequests;
    UE_LOG(LogTemp,Display,TEXT("GO2_STOP_SCAN stop=%d request=%d"),StopIndex+1,StopScanRequests);
    // Route stops are retained as pauses only; they do not trigger F6 scans.
}
bool AGo2DeviceScanner::BeginBusinessIntro(const FString& BusinessId){
    if(BusinessId!=TEXT("bv.network-security") || !Robot || !ScanMID || !ScanCollection
        || !GetWorld()->GetFirstPlayerController())return false;
    GetWorldTimerManager().ClearTimer(BusinessScanTimer);
    GetWorldTimerManager().ClearTimer(BusinessIntroTimer);
    StopSecurityEffects();
    bSecurityIntroActive=true;
    bIntroWaitingForCamera=true;
    StartIntroWhenCameraReady();
    return true;
}
void AGo2DeviceScanner::StartIntroWhenCameraReady(){
    if(!bSecurityIntroActive || !bIntroWaitingForCamera)return;
    auto* Manager=BusinessWeb.IsValid()?Cast<ATwinSceneManager>(BusinessWeb->GetOwner()):nullptr;
    bIntroWaitingForCamera=false;
    SetFollowCamera(false,false);
    if(Manager)Manager->TryFocusBusinessCamera(TEXT("bv.network-security"));
    GetWorldTimerManager().SetTimer(BusinessScanTimer,this,&AGo2DeviceScanner::ToggleFloorScan,
        FMath::Max(.1f,SecurityScanDelaySeconds),false);
    GetWorldTimerManager().SetTimer(BusinessIntroTimer,this,&AGo2DeviceScanner::FinishBusinessIntro,
        FMath::Max(.1f,SecurityIntroSeconds),false);
    UE_LOG(LogTemp,Display,TEXT("GO2_SECURITY_INTRO_START duration=%.2f"),SecurityIntroSeconds);
}
void AGo2DeviceScanner::CancelBusinessIntro(){
    GetWorldTimerManager().ClearTimer(BusinessScanTimer);
    GetWorldTimerManager().ClearTimer(BusinessIntroTimer);
    bSecurityIntroActive=false;
    bIntroWaitingForCamera=false;
    SetFollowCamera(false);
}
void AGo2DeviceScanner::StopSecurityEffects(){
    GetWorldTimerManager().ClearTimer(BusinessScanTimer);
    GetWorldTimerManager().ClearTimer(BusinessIntroTimer);
    bSecurityIntroActive=false;
    bIntroWaitingForCamera=false;
    if(bFloorGridActive)ToggleFloorScan();
    bScanning=false;
    bManualDiscovery=false;
    PendingScans=0;
    ScanAge=0;
    CurrentRadius=0;
    FloorSweepAge=0;
    ScanGrid->DeactivateImmediate();
    ScanPostProcess->BlendWeight=0;
    RestoreHighlights();
    for(FTarget& Target:Targets){Target.bHighlighted=false;Target.bHeldByFloor=false;}
    SetFollowCamera(false,false);
    UE_LOG(LogTemp,Display,TEXT("GO2_SECURITY_EFFECTS_CLEARED"));
}
void AGo2DeviceScanner::FinishBusinessIntro(){
    if(!bSecurityIntroActive)return;
    bSecurityIntroActive=false;
    // Keep the overview (or the user-selected F8 follow camera) unchanged.
    if(BusinessWeb.IsValid())BusinessWeb->CompleteBusinessIntro();
    UE_LOG(LogTemp,Display,TEXT("GO2_SECURITY_INTRO_COMPLETE"));
}
void AGo2DeviceScanner::Tick(float Dt){
    Super::Tick(Dt);BindBusinessEntry();StartIntroWhenCameraReady();Age+=Dt;if(!Robot)return;
    const FVector Center=Robot->BodyWorldPosition;
    SetActorLocation(Center);
    if(auto* PC=GetWorld()->GetFirstPlayerController()){
        if(PC->WasInputKeyJustPressed(EKeys::F6))ToggleFloorScan();
        if(PC->WasInputKeyJustPressed(EKeys::F8)){
            const bool bEnable=!bUseDemoCamera;
            SetFollowCamera(bEnable);
        }
        if(bUseDemoCamera)UpdateFollowCamera();
    }
    UpdateFloorSweep(Dt);
    if (bFloorGridActive)
    {
        auto* parameters = GetWorld()->GetParameterCollectionInstance(ScanCollection);
        const float progress = FMath::Clamp(FloorSweepAge / FMath::Max(.01f, ScanDuration), 0.f, 1.f);
        parameters->SetVectorParameterValue(TEXT("Location"), FLinearColor(FloorOrigin));
        parameters->SetScalarParameterValue(TEXT("Range"), FloorSweepRadius);
        parameters->SetScalarParameterValue(TEXT("GridCellSize"), 500.f);
        parameters->SetScalarParameterValue(TEXT("PulseSpeed"), 1.f);
        parameters->SetScalarParameterValue(TEXT("NormalizedTime"), progress);
        parameters->SetScalarParameterValue(TEXT("FadeIn"), 1.f);
        parameters->SetScalarParameterValue(TEXT("FadeOut"), 0.f);
        if (!ScanGrid->IsActive()) ScanGrid->Activate(true);
    }
    RefreshAge+=Dt;if(RefreshAge>1){RefreshTargets();RefreshAge=0;}
    if(!bScanning && PendingScans>0){--PendingScans;StartScan();}
    if(bAutoScan&&!bScanning&&Age>=NextScan)StartScan();
    auto* Collection=ScanCollection?GetWorld()->GetParameterCollectionInstance(ScanCollection):nullptr;
    if(bScanning){
        ScanAge+=Dt;
        const float Progress=FMath::Clamp(ScanAge/FMath::Max(ScanDuration-.7f,.1f),0.f,1.f);
        CurrentRadius=FMath::Max(1.f,ScanRadius*Progress);
        const float Fade=FMath::Min(FMath::Clamp(ScanAge/.4f,0.f,1.f),FMath::Clamp((ScanDuration-ScanAge)/.7f,0.f,1.f));
        if (!bFloorGridActive) ScanPostProcess->BlendWeight=Fade*FMath::Clamp(ScanIntensity,0.f,1.f);
        if(Collection && !bFloorGridActive){
            Collection->SetVectorParameterValue(TEXT("Location"),FLinearColor(Center.X,Center.Y,Center.Z,1));
            Collection->SetScalarParameterValue(TEXT("Range"),CurrentRadius);
            Collection->SetScalarParameterValue(TEXT("GridCellSize"),150.f);
            Collection->SetScalarParameterValue(TEXT("PulseSpeed"),.35f);
            Collection->SetScalarParameterValue(TEXT("NormalizedTime"),Progress);
            Collection->SetScalarParameterValue(TEXT("FadeIn"),1.f);
            Collection->SetScalarParameterValue(TEXT("FadeOut"),0.f);
        }
        for(FTarget& T:Targets)if(auto* Mesh=T.Mesh.Get()){
            // OntoTwin hides authoring meshes after spawning runtime twins. Never count hidden originals.
            if(Mesh->GetOwner()->IsHidden()||Mesh->bHiddenInGame||!Mesh->IsVisible())continue;
            // Use the device bounds center, not a possibly offset CAD actor pivot.
            if(T.LastScan!=ScanCount&&FVector::Dist(Center,Mesh->Bounds.Origin)<=CurrentRadius){
                if(HighlightMID)Mesh->SetOverlayMaterial(HighlightMID);
                T.bHeldByFloor |= bManualDiscovery;
                T.bHighlighted=true;T.Expiry=Age+HighlightDuration;T.LastScan=ScanCount;DetectedCount++;
                UE_LOG(LogTemp,Display,TEXT("GO2_SCAN_DETECTED cycle=%d actor=%s distance=%.0f"),ScanCount,*Mesh->GetOwner()->GetName(),FVector::Dist(Center,Mesh->Bounds.Origin));
            }
        }
        if(ScanAge>=ScanDuration){bScanning=false;bManualDiscovery=false;if (!bFloorGridActive) { ScanPostProcess->BlendWeight=0; ScanGrid->DeactivateImmediate(); }UE_LOG(LogTemp,Display,TEXT("GO2_SCAN_COMPLETE cycle=%d found=%d"),ScanCount,DetectedCount);}
    }
    for(FTarget& T:Targets)if(T.bHighlighted&&!T.bHeldByFloor&&Age>=T.Expiry){if(auto* Mesh=T.Mesh.Get())Mesh->SetOverlayMaterial(T.OriginalOverlay.Get());T.bHighlighted=false;}
}
void AGo2DeviceScanner::RestoreHighlights(){for(FTarget& T:Targets)if(T.bHighlighted)if(auto* Mesh=T.Mesh.Get())Mesh->SetOverlayMaterial(T.OriginalOverlay.Get());}
void AGo2DeviceScanner::EndPlay(const EEndPlayReason::Type Reason){
    GetWorldTimerManager().ClearTimer(BusinessScanTimer);
    GetWorldTimerManager().ClearTimer(BusinessIntroTimer);
    if(BusinessWeb.IsValid()){
        BusinessWeb->CancelBusinessIntro();
        BusinessWeb->BusinessIntroRequested.Unbind();
        BusinessWeb->BusinessIntroCancelled.RemoveAll(this);
        BusinessWeb->SecurityBusinessExited.RemoveAll(this);
    }
    if(Robot){Robot->OnStopSettled.RemoveAll(this);Robot->StopWorkPending.Unbind();}
    RestoreHighlights();Super::EndPlay(Reason);
}





