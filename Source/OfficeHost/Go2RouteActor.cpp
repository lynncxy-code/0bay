#include "Go2RouteActor.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/ConstructorHelpers.h"

namespace Go2Offline {
const FName Names[12]={TEXT("FR_hip_joint"),TEXT("FR_thigh_joint"),TEXT("FR_calf_joint"),TEXT("FL_hip_joint"),TEXT("FL_thigh_joint"),TEXT("FL_calf_joint"),TEXT("RR_hip_joint"),TEXT("RR_thigh_joint"),TEXT("RR_calf_joint"),TEXT("RL_hip_joint"),TEXT("RL_thigh_joint"),TEXT("RL_calf_joint")};
const float Up[12]={.00571868f,.608813f,-1.21763f,-.00571868f,.608813f,-1.21763f,.00571868f,.608813f,-1.21763f,-.00571868f,.608813f,-1.21763f};
const float Down[12]={.0473455f,1.22187f,-2.44375f,-.0473455f,1.22187f,-2.44375f,.0473455f,1.22187f,-2.44375f,-.0473455f,1.22187f,-2.44375f};
constexpr float HalfPeriod=.32f, SwingDuration=.25f, SettleDuration=.3f;
float Smooth(float X){X=FMath::Clamp(X,0.f,1.f);return X*X*X*(X*(X*6.f-15.f)+10.f);}
int32 PairOf(int32 Leg){return (Leg==0||Leg==3)?0:1;}
// Independent analytic FK in centimetres, from official URDF dimensions. Output uses UE axes.
FVector FootFK(int32 Leg,const float* Q){
    const double S=(Leg==0||Leg==2)?-1.:1.;
    const double X=-21.3*(FMath::Sin(Q[1])+FMath::Sin(Q[1]+Q[2]));
    const double Z=-21.3*(FMath::Cos(Q[1])+FMath::Cos(Q[1]+Q[2]));
    return FVector((Leg<2?19.34:-19.34)+X,-(S*4.65+S*9.55*FMath::Cos(Q[0])-Z*FMath::Sin(Q[0])),S*9.55*FMath::Sin(Q[0])+Z*FMath::Cos(Q[0]));
}
}
using namespace Go2Offline;

AGo2RouteActor::AGo2RouteActor(){
    PrimaryActorTick.bCanEverTick=true;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("RouteAnchor"));
    Route=CreateDefaultSubobject<USplineComponent>(TEXT("EditableRoute"));Route->SetupAttachment(RootComponent);
    Route->ClearSplinePoints(false);Route->AddSplinePoint(FVector::ZeroVector,ESplineCoordinateSpace::Local,false);
    Route->AddSplinePoint(FVector(200,0,0),ESplineCoordinateSpace::Local,true);
    RobotMesh=CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("OfflineGo2"));RobotMesh->SetupAttachment(RootComponent);
    static ConstructorHelpers::FObjectFinder<USkeletalMesh> Mesh(TEXT("/Game/Robots/UnitreeGo2/Mesh/SK_Go2.SK_Go2"));
    if(Mesh.Succeeded())RobotMesh->SetSkinnedAssetAndUpdate(Mesh.Object);
    RobotMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    JointAnglesRadians.Append(Down,12);FootTargetsWorld.SetNum(4);FeetInSwing.Init(false,4);SwingStarts.SetNum(4);SwingEnds.SetNum(4);
    BodyHeight=2.2f-FootFK(0,Up).Z;
}
void AGo2RouteActor::OnConstruction(const FTransform& T){Super::OnConstruction(T);if(!bRunning)ResetRoute();}
void AGo2RouteActor::BeginPlay(){Super::BeginPlay();if(bAutoStart)StartRoute();}
void AGo2RouteActor::Tick(float Dt){Super::Tick(Dt);AdvanceSimulation(Dt);}
void AGo2RouteActor::EnterState(EGo2RouteState NewState){
    State=NewState;StateSeconds=0;Status=TEXT("Offline / ")+UEnum::GetValueAsString(NewState);OnRouteStateChanged(NewState);
}
void AGo2RouteActor::ResetRoute(){
    bRunning=false;bPaused=false;DistanceCm=0;CurrentSpeedCmPerSecond=0;MaxFootErrorCm=0;
    StepNumber=-1;SwingPair=-1;FeetInSwing.Init(false,4);JointAnglesRadians.Reset();JointAnglesRadians.Append(Down,12);
    SetBodyAtDistance(0);ApplyJoints(true);EnterState(EGo2RouteState::Crouched);
}
bool AGo2RouteActor::ValidateRoute(FString& Error) const {
    if(!Route||Route->GetNumberOfSplinePoints()<2||Route->GetSplineLength()<1){Error=TEXT("Route needs two distinct points");return false;}
    if(!GetActorScale3D().Equals(FVector::OneVector,.001)||!Route->GetRelativeScale3D().Equals(FVector::OneVector,.001)){
        Error=TEXT("Keep actor and spline scale at 1; edit route points to change distance");return false;}
    if(!FMath::IsFinite(SpeedCmPerSecond)||SpeedCmPerSecond<10||SpeedCmPerSecond>50||
        !FMath::IsFinite(StandUpSeconds)||StandUpSeconds<.5||!FMath::IsFinite(LieDownSeconds)||LieDownSeconds<.5||
        !FMath::IsFinite(PauseBeforeLieDownSeconds)||PauseBeforeLieDownSeconds<0||!FMath::IsFinite(LoopDelaySeconds)||LoopDelaySeconds<0){
        Error=TEXT("Invalid speed or duration parameters");return false;}
    if(bLoop&&!Route->IsClosedLoop()){Error=TEXT("Loop requires a closed route");return false;}
    const float Length=Route->GetSplineLength();
    const FVector Start=Route->GetLocationAtDistanceAlongSpline(0,ESplineCoordinateSpace::World);
    FVector PreviousDirection=Route->GetDirectionAtDistanceAlongSpline(0,ESplineCoordinateSpace::World);
    for(float D=0;D<Length+5;D+=5){
        const FVector P=Route->GetLocationAtDistanceAlongSpline(FMath::Min(D,Length),ESplineCoordinateSpace::World);
        const FVector Dir=Route->GetDirectionAtDistanceAlongSpline(FMath::Min(D,Length),ESplineCoordinateSpace::World);
        if(P.ContainsNaN()||Dir.ContainsNaN()||FMath::Abs(P.Z-Start.Z)>.1){Error=TEXT("This preview supports horizontal floor routes only");return false;}
        if(FVector::DotProduct(PreviousDirection,Dir)<.98){Error=TEXT("Round sharp route corners (recommended radius >= 30 cm)");return false;}
        PreviousDirection=Dir;
    }return true;
}
bool AGo2RouteActor::StartRoute(){
    FString Error;if(!ValidateRoute(Error)){Status=Error;bRunning=false;return false;}
    ResetRoute();bRunning=true;EnterState(EGo2RouteState::StandingUp);return true;
}
void AGo2RouteActor::TogglePause(){if(bRunning)bPaused=!bPaused;}
void AGo2RouteActor::SetBodyAtDistance(float D){
    GroundPosition=Route->GetLocationAtDistanceAlongSpline(D,ESplineCoordinateSpace::World);
    BodyHeading=FRotator(0,Route->GetDirectionAtDistanceAlongSpline(D,ESplineCoordinateSpace::World).Rotation().Yaw,0).Quaternion();
}
FVector AGo2RouteActor::NominalFoot(int32 Leg,float D) const {
    const FVector Ground=Route->GetLocationAtDistanceAlongSpline(D,ESplineCoordinateSpace::World);
    const FQuat Heading=FRotator(0,Route->GetDirectionAtDistanceAlongSpline(D,ESplineCoordinateSpace::World).Rotation().Yaw,0).Quaternion();
    return Ground+FVector(0,0,BodyHeight)+Heading.RotateVector(FootFK(Leg,Up+Leg*3));
}
void AGo2RouteActor::ApplyJoints(bool GroundPose){
    auto* Mesh=Cast<USkeletalMesh>(RobotMesh->GetSkinnedAsset());if(!Mesh||JointAnglesRadians.Num()!=12)return;
    const auto& Ref=Mesh->GetRefSkeleton();RobotMesh->BoneSpaceTransforms=Ref.GetRefBonePose();
    float Height=BodyHeight;
    if(GroundPose){float Lowest=0;for(int32 L=0;L<4;L++)Lowest=FMath::Min(Lowest,float(FootFK(L,JointAnglesRadians.GetData()+L*3).Z));Height=2.2f-Lowest;}
    RobotMesh->SetWorldLocationAndRotation(GroundPosition+FVector(0,0,Height),BodyHeading);
    for(int32 I=0;I<12;I++){
        const int32 B=Ref.FindBoneIndex(Names[I]);if(B==INDEX_NONE)return;
        auto& T=RobotMesh->BoneSpaceTransforms[B];T.SetRotation((T.GetRotation()*FQuat(I%3==0?FVector(0,1,0):FVector(0,0,-1),JointAnglesRadians[I])).GetNormalized());
    }
    RobotMesh->MarkRefreshTransformDirty();RobotMesh->RefreshBoneTransforms();
}
void AGo2RouteActor::SolveFeet(){
    for(int32 L=0;L<4;L++){
        FVector P=BodyHeading.UnrotateVector(FootTargetsWorld[L]-GroundPosition-FVector(0,0,BodyHeight));
        P.Y=-P.Y;P.X-=(L<2?19.34:-19.34);const double S=(L==0||L==2)?-1.:1.;P.Y-=S*4.65;
        const double Z=-FMath::Sqrt(FMath::Max(.001,P.Y*P.Y+P.Z*P.Z-9.55*9.55));
        const double Hip=FMath::UnwindRadians(FMath::Atan2(P.Z,P.Y)-FMath::Atan2(Z,S*9.55));
        const double Calf=-FMath::Acos(FMath::Clamp((P.X*P.X+Z*Z-2*21.3*21.3)/(2*21.3*21.3),-1.,1.));
        const double Thigh=FMath::Atan2(-P.X,-Z)-FMath::Atan2(FMath::Sin(Calf),1+FMath::Cos(Calf));
        JointAnglesRadians[L*3]=FMath::Clamp(float(Hip),-1.0472f,1.0472f);
        JointAnglesRadians[L*3+1]=FMath::Clamp(float(Thigh),-1.5708f,3.4907f);
        JointAnglesRadians[L*3+2]=FMath::Clamp(float(Calf),-2.7227f,-.83776f);
        const FVector Actual=GroundPosition+FVector(0,0,BodyHeight)+BodyHeading.RotateVector(FootFK(L,JointAnglesRadians.GetData()+L*3));
        MaxFootErrorCm=FMath::Max(MaxFootErrorCm,float(FVector::Distance(Actual,FootTargetsWorld[L])));
    }ApplyJoints(false);
}
void AGo2RouteActor::BeginSwing(int32 Pair,float TargetDistance){
    SwingPair=Pair;SwingSeconds=0;
    for(int32 L=0;L<4;L++){
        FeetInSwing[L]=PairOf(L)==Pair;
        if(FeetInSwing[L]){SwingStarts[L]=FootTargetsWorld[L];SwingEnds[L]=NominalFoot(L,TargetDistance);}
    }
}
void AGo2RouteActor::UpdateSwing(float Alpha){
    const float A=FMath::Clamp(Alpha,0.f,1.f);
    for(int32 L=0;L<4;L++)if(PairOf(L)==SwingPair){
        FootTargetsWorld[L]=FMath::Lerp(SwingStarts[L],SwingEnds[L],Smooth(A));
        FootTargetsWorld[L].Z+=4.f*FMath::Square(FMath::Sin(PI*A));FeetInSwing[L]=A<1;
    }
}
void AGo2RouteActor::AdvanceSimulation(float Dt){
    if(!bRunning||bPaused||!FMath::IsFinite(Dt)||Dt<=0)return;
    // Bound editor/runtime stalls, and solve at <= 1/120 s for consistent contact and transitions.
    float Remaining=FMath::Min(Dt,1.f);
    while(Remaining>1.e-6f&&bRunning&&!bPaused){const float H=FMath::Min(Remaining,1.f/120.f);Step(H);Remaining-=H;}
}
void AGo2RouteActor::Step(float Dt){
    StateSeconds+=Dt;
    switch(State){
    case EGo2RouteState::StandingUp:
    case EGo2RouteState::LyingDown:{
        const bool Rising=State==EGo2RouteState::StandingUp;
        const float Duration=Rising?StandUpSeconds:LieDownSeconds;const float A=Smooth(StateSeconds/Duration);
        for(int32 I=0;I<12;I++)JointAnglesRadians[I]=FMath::Lerp(Rising?Down[I]:Up[I],Rising?Up[I]:Down[I],A);
        ApplyJoints(true);
        if(StateSeconds>=Duration){
            if(Rising){for(int32 L=0;L<4;L++)FootTargetsWorld[L]=NominalFoot(L,0);StepNumber=-1;EnterState(EGo2RouteState::Walking);}
            else{EnterState(EGo2RouteState::Complete);if(!bLoop)bRunning=false;}
        }break;}
    case EGo2RouteState::Walking:{
        const float Length=Route->GetSplineLength(),Left=FMath::Max(0.f,Length-DistanceCm);
        const float TargetSpeed=FMath::Min(SpeedCmPerSecond,FMath::Sqrt(2.f*60.f*Left));
        const float OldSpeed=CurrentSpeedCmPerSecond;
        CurrentSpeedCmPerSecond=FMath::FInterpConstantTo(CurrentSpeedCmPerSecond,TargetSpeed,Dt,60.f);
        DistanceCm=FMath::Min(Length,DistanceCm+(OldSpeed+CurrentSpeedCmPerSecond)*.5f*Dt);SetBodyAtDistance(DistanceCm);
        const int32 NewStep=FMath::FloorToInt(StateSeconds/HalfPeriod);
        if(NewStep!=StepNumber){
            if(StepNumber>=0)UpdateSwing(1);
            StepNumber=NewStep;const float Lead=CurrentSpeedCmPerSecond*(SwingDuration+HalfPeriod*.5f);
            BeginSwing(NewStep%2,FMath::Min(Length,DistanceCm+Lead));
        }
        SwingSeconds+=Dt;UpdateSwing(SwingSeconds/SwingDuration);SolveFeet();
        if(Length-DistanceCm<.02f){DistanceCm=Length;CurrentSpeedCmPerSecond=0;SetBodyAtDistance(Length);
            // Finish the active swing before settling the opposite pair; no airborne foot snaps.
            EnterState(EGo2RouteState::SettlingFeet);StepNumber=0;
            BeginSwing(SwingPair<0?0:SwingPair,Length);
        }break;}
    case EGo2RouteState::SettlingFeet:{
        SwingSeconds+=Dt;UpdateSwing(SwingSeconds/SettleDuration);SolveFeet();
        if(SwingSeconds>=SettleDuration){
            if(StepNumber==0){StepNumber=1;BeginSwing(1-SwingPair,DistanceCm);}
            else{for(int32 I=0;I<12;I++)JointAnglesRadians[I]=Up[I];ApplyJoints(true);EnterState(EGo2RouteState::StandingPause);}
        }break;}
    case EGo2RouteState::StandingPause:if(StateSeconds>=PauseBeforeLieDownSeconds)EnterState(EGo2RouteState::LyingDown);break;
    case EGo2RouteState::Complete:if(bLoop&&StateSeconds>=LoopDelaySeconds)StartRoute();break;
    default:break;
    }
}
