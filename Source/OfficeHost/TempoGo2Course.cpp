#include "TempoGo2Course.h"
#include "ControlRigComponent.h"
#include "ControlRig.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "TempoGo2MeshComponent.h"

ATempoGo2Course::ATempoGo2Course(){
    PrimaryActorTick.bCanEverTick=true;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("CourseAnchor"));
    PreviewCamera=CreateDefaultSubobject<UCameraComponent>(TEXT("PreviewCamera"));PreviewCamera->SetupAttachment(RootComponent);PreviewCamera->FieldOfView=50.f;
    Route=CreateDefaultSubobject<USplineComponent>(TEXT("EditableTempoRoute"));Route->SetupAttachment(RootComponent);
    Route->ClearSplinePoints(false);
    for(FVector P:{FVector(0,0,0),FVector(180,0,0),FVector(320,100,0),FVector(380,260,0),FVector(240,400,0),FVector(60,300,0),FVector(-140,380,0),FVector(-260,220,0),FVector(-120,100,0)})
        Route->AddSplinePoint(P,ESplineCoordinateSpace::Local,false);
    Route->SetClosedLoop(true,true);
    RobotMesh=CreateDefaultSubobject<UTempoGo2MeshComponent>(TEXT("TempoGo2Mesh"));RobotMesh->SetupAttachment(RootComponent);
    static ConstructorHelpers::FObjectFinder<USkeletalMesh> Mesh(TEXT("/TempoQuadruped/Meshes/SKM_Unitree_Go2.SKM_Unitree_Go2"));
    if(Mesh.Succeeded())RobotMesh->SetSkeletalMeshAsset(Mesh.Object);
    RobotMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    RobotMesh->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    ProceduralRig=CreateDefaultSubobject<UControlRigComponent>(TEXT("TempoLocomotorRig"));ProceduralRig->SetupAttachment(RootComponent);
    static ConstructorHelpers::FClassFinder<UControlRig> Rig(TEXT("/Game/Robots/UnitreeGo2/Tempo/CR_Go2_Tempo"));
    if(Rig.Succeeded())ProceduralRig->ControlRigClass=Rig.Class;
    ProceduralRig->bUpdateRigOnTick=false;ProceduralRig->bUpdateInEditor=true;ProceduralRig->bEnableLazyEvaluation=false;
    ProceduralRig->OnPreInitializeDelegate.AddDynamic(this,&ATempoGo2Course::MapMesh);
    ProceduralRig->OnPreForwardsSolveDelegate.AddDynamic(this,&ATempoGo2Course::SetRigTarget);
    const float Ends[]={.2f,.45f,.75f,1.f},Speeds[]={30.f,60.f,100.f,45.f};
    for(int32 I=0;I<4;I++){FTempoGo2Leg L;L.EndFraction=Ends[I];L.Speed=Speeds[I];L.HoldSeconds=3.f;Legs.Add(L);}
    FeetWorld.SetNum(4);
}
void ATempoGo2Course::BeginPlay(){Super::BeginPlay();if(bAutoStart)StartCourse();else ResetCourse();
    if(bUsePreviewCamera){UpdatePreviewCamera();if(auto* PC=GetWorld()->GetFirstPlayerController())PC->SetViewTarget(this);}}
void ATempoGo2Course::Tick(float Dt){Super::Tick(Dt);
    if(bUsePreviewCamera)if(auto* PC=GetWorld()->GetFirstPlayerController()){
        if(PC->WasInputKeyJustPressed(EKeys::R))StartCourse();
        if(PC->WasInputKeyJustPressed(EKeys::SpaceBar))TogglePause();
        if(PC->WasInputKeyJustPressed(EKeys::C))bFollowCamera=!bFollowCamera;
    }
    AdvanceCourse(Dt);DrawCourse();if(bUsePreviewCamera)UpdatePreviewCamera();}
void ATempoGo2Course::MapMesh(UControlRigComponent* C){if(C->MappedElements.IsEmpty())C->AddMappedCompleteSkeletalMesh(RobotMesh,EControlRigComponentMapDirection::Output);}
void ATempoGo2Course::UpdatePreviewCamera(){
    const FVector LookAt=bFollowCamera?BodyWorldPosition:GetActorTransform().TransformPosition(FVector(60,180,20));
    const FVector Position=LookAt+(bFollowCamera?FVector(170,-260,140):FVector(650,-850,950));
    PreviewCamera->SetWorldLocationAndRotation(Position,(LookAt-Position).Rotation());
}
void ATempoGo2Course::SetRigTarget(UControlRigComponent* C){
    // UE 5.6 Locomotor consumes the Target value as world coordinates internally (same convention as upstream BP).
    C->SetControlTransform(TEXT("Target"),TargetTransform,EControlRigComponentSpace::RigSpace);
}
bool ATempoGo2Course::Validate(FString& Error) const {
    if(!ProceduralRig->ControlRigClass||!RobotMesh->GetSkeletalMeshAsset()){Error=TEXT("Tempo rig or mesh missing");return false;}
    if(Route->GetSplineLength()<10||Legs.Num()==0){Error=TEXT("Route and speed sections required");return false;}
    float Previous=0;for(const auto& L:Legs){
        if(!FMath::IsFinite(L.EndFraction)||L.EndFraction<=Previous||L.EndFraction>1||!FMath::IsFinite(L.Speed)||L.Speed<10||L.Speed>150||!FMath::IsFinite(L.HoldSeconds)||L.HoldSeconds<0){Error=TEXT("Invalid course section");return false;}
        Previous=L.EndFraction;
    }
    if(FMath::Abs(Previous-1)>.001){Error=TEXT("Last section must reach 100 percent");return false;}
    if(bRepeat&&!Route->IsClosedLoop()){Error=TEXT("Repeating tests require a closed route");return false;}
    return true;
}
void ATempoGo2Course::ResetCourse(){
    bStopEventSent=false;
    bRunning=false;bPaused=false;CurrentLeg=0;CompletedStops=0;DistanceCm=0;TargetSpeedCm=0;ActualBodySpeedCm=0;HoldTime=0;SettledTime=0;RigSolveCount=0;
    TargetTransform=Route->GetTransformAtDistanceAlongSpline(0,ESplineCoordinateSpace::World,false);
    TargetYawDegrees=TargetTransform.Rotator().Yaw;
    if(!ProceduralRig->ControlRigClass){State=TEXT("Tempo rig missing");return;}
    // Recreate the rig instance so a replay cannot inherit feet from a previous route.
    const TSubclassOf<UControlRig> RigClass=ProceduralRig->ControlRigClass;
    ProceduralRig->SetControlRigClass(nullptr);ProceduralRig->SetControlRigClass(RigClass);
    ProceduralRig->Initialize();bInitialized=true;
    for(int32 I=0;I<90;I++)Solve(1.f/60.f);
    State=TEXT("Ready");
}
bool ATempoGo2Course::StartCourse(){FString Error;if(!Validate(Error)){State=Error;return false;}ResetCourse();bRunning=true;State=TEXT("Starting");return true;}
void ATempoGo2Course::TogglePause(){if(bRunning)bPaused=!bPaused;}
void ATempoGo2Course::Solve(float Dt){
    const FVector Previous=BodyWorldPosition;
    ProceduralRig->Update(Dt);RigSolveCount++;
    RobotMesh->TickAnimation(Dt,false);RobotMesh->RefreshBoneTransforms();
    RobotMesh->FinalizeBoneTransform();RobotMesh->InvalidateCachedBounds();RobotMesh->UpdateBounds();
    RobotMesh->MarkRenderTransformDirty();RobotMesh->MarkRenderDynamicDataDirty();
    BodyWorldPosition=RobotMesh->GetBoneLocation(TEXT("body"));
    ActualBodySpeedCm=FMath::Lerp(ActualBodySpeedCm,float(FVector::Dist2D(Previous,BodyWorldPosition)/FMath::Max(Dt,.001f)),.15f);
    int32 I=0;for(FName N:{FName(TEXT("foot_fl")),FName(TEXT("foot_rl")),FName(TEXT("foot_fr")),FName(TEXT("foot_rr"))})FeetWorld[I++]=RobotMesh->GetBoneLocation(N);
}
void ATempoGo2Course::AdvanceCourse(float Dt){
    if(!bRunning||bPaused||!FMath::IsFinite(Dt)||Dt<=0)return;
    float Remaining=FMath::Min(Dt,.5f);
    while(Remaining>1.e-6f&&bRunning){float H=FMath::Min(Remaining,1.f/60.f);Step(H);Remaining-=H;}
}
void ATempoGo2Course::Step(float Dt){
    if(State==TEXT("Holding")||State==TEXT("Complete")){
        Solve(Dt);HoldTime+=Dt;SettledTime=ActualBodySpeedCm<2.f?SettledTime+Dt:0;
        if(!bStopEventSent && SettledTime>.35f){
            bStopEventSent=true;HoldTime=0;OnStopSettled.Broadcast(CurrentLeg);
        }
        if(bStopEventSent && HoldTime>=Legs[CurrentLeg].HoldSeconds&&SettledTime>.35f
            && (!StopWorkPending.IsBound() || !StopWorkPending.Execute())){
            if(CurrentLeg==Legs.Num()-1){if(bRepeat)StartCourse();else{bRunning=false;State=TEXT("Complete");}}
            else{CurrentLeg++;State=TEXT("Starting");HoldTime=0;}
        }return;
    }
    const float End=Route->GetSplineLength()*Legs[CurrentLeg].EndFraction;
    const float Remaining=FMath::Max(0.f,End-DistanceCm);
    const float GoalSpeed=FMath::Min(Legs[CurrentLeg].Speed,FMath::Sqrt(2.f*BrakingCm*Remaining));
    const float Old=TargetSpeedCm;
    TargetSpeedCm=FMath::FInterpConstantTo(TargetSpeedCm,GoalSpeed,Dt,GoalSpeed<Old?BrakingCm:AccelerationCm);
    DistanceCm=FMath::Min(End,DistanceCm+(Old+TargetSpeedCm)*.5f*Dt);
    State=GoalSpeed<Legs[CurrentLeg].Speed-.5f?TEXT("Braking"):(TargetSpeedCm<Legs[CurrentLeg].Speed-.5f?TEXT("Starting"):TEXT("Walking"));
    TargetTransform=Route->GetTransformAtDistanceAlongSpline(DistanceCm,ESplineCoordinateSpace::World,false);
    TargetYawDegrees=TargetTransform.Rotator().Yaw;Solve(Dt);
    if(End-DistanceCm<.01f){DistanceCm=End;TargetSpeedCm=0;State=TEXT("Holding");HoldTime=0;SettledTime=0;bStopEventSent=false;CompletedStops++;}
}
void ATempoGo2Course::DrawCourse(){
    if(bShowRoute){const float Length=Route->GetSplineLength();
        for(float D=0;D<Length;D+=20)DrawDebugLine(GetWorld(),Route->GetLocationAtDistanceAlongSpline(D,ESplineCoordinateSpace::World)+FVector(0,0,1),Route->GetLocationAtDistanceAlongSpline(FMath::Min(D+20,Length),ESplineCoordinateSpace::World)+FVector(0,0,1),FColor(40,180,225),false,0,0,1.5f);
        for(const auto& L:Legs)DrawDebugSphere(GetWorld(),Route->GetLocationAtDistanceAlongSpline(L.EndFraction*Length,ESplineCoordinateSpace::World)+FVector(0,0,3),6,12,FColor::Orange,false,0,0,1.5f);
    }
    if(bShowStatus&&GEngine){GEngine->AddOnScreenDebugMessage(uint64(GetUniqueID()),0,FColor::Cyan,FString::Printf(TEXT("Tempo Go2 | %s | section %d/%d\nTarget %.2f m/s | Actual %.2f m/s | Stops %d\nR replay   Space pause   C camera"),bPaused?TEXT("Paused"):*State,CurrentLeg+1,Legs.Num(),TargetSpeedCm/100,ActualBodySpeedCm/100,CompletedStops),true,FVector2D(1.4f,1.4f));}
}
