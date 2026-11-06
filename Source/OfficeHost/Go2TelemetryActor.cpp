#include "Go2TelemetryActor.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/ConstructorHelpers.h"

namespace {
const FName JointNames[12]={TEXT("FR_hip_joint"),TEXT("FR_thigh_joint"),TEXT("FR_calf_joint"),TEXT("FL_hip_joint"),TEXT("FL_thigh_joint"),TEXT("FL_calf_joint"),TEXT("RR_hip_joint"),TEXT("RR_thigh_joint"),TEXT("RR_calf_joint"),TEXT("RL_hip_joint"),TEXT("RL_thigh_joint"),TEXT("RL_calf_joint")};
const float Stand[12]={0.00571868f,0.608813f,-1.21763f,-0.00571868f,0.608813f,-1.21763f,0.00571868f,0.608813f,-1.21763f,-0.00571868f,0.608813f,-1.21763f};
bool ReadArray(const TSharedPtr<FJsonObject>& Obj,const TCHAR* Field,int32 Count,TArray<double>& Out) {
    const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
    if(!Obj->TryGetArrayField(Field,Values)||Values->Num()!=Count)return false;
    Out.Reset();for(const auto& V:*Values){double X;if(!V->TryGetNumber(X)||!FMath::IsFinite(X))return false;Out.Add(X);}return true;
}
}
AGo2TelemetryActor::AGo2TelemetryActor(){
    PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PrePhysics;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("GroundAnchor"));
    RobotMesh=CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("Go2Pose"));RobotMesh->SetupAttachment(RootComponent);
    static ConstructorHelpers::FObjectFinder<USkeletalMesh> Mesh(TEXT("/Game/Robots/UnitreeGo2/Mesh/SK_Go2.SK_Go2"));
    if(Mesh.Succeeded())RobotMesh->SetSkinnedAssetAndUpdate(Mesh.Object);
    RobotMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    RobotMesh->SetRelativeLocation(FVector(0,0,37.2));
    StatusLabel=CreateDefaultSubobject<UTextRenderComponent>(TEXT("TelemetryStatus"));StatusLabel->SetupAttachment(RootComponent);
    StatusLabel->SetRelativeLocation(FVector(0,0,80));StatusLabel->SetWorldSize(5);StatusLabel->SetHorizontalAlignment(EHTA_Center);
    JointAnglesRadians.Append(Stand,12);TargetAngles=JointAnglesRadians;
}
AGo2TelemetryActor::~AGo2TelemetryActor(){CloseSocket();}
void AGo2TelemetryActor::OnConstruction(const FTransform& Transform){Super::OnConstruction(Transform);ApplyPose();UpdateStatus();}
void AGo2TelemetryActor::EndPlay(const EEndPlayReason::Type Reason){CloseSocket();Super::EndPlay(Reason);}
void AGo2TelemetryActor::CloseSocket(){if(Socket){Socket->Close();ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);Socket=nullptr;}BoundPort=0;}
void AGo2TelemetryActor::OpenSocket(){
    if(Socket||!bReceiveEnabled||HasAnyFlags(RF_ClassDefaultObject|RF_ArchetypeObject))return;
    const double Now=FPlatformTime::Seconds();if(Now-LastOpenAttempt<2)return;LastOpenAttempt=Now;
    auto* Sub=ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);Socket=Sub->CreateSocket(NAME_DGram,TEXT("Go2ReceiveOnly"),false);
    if(!Socket)return;
    auto Address=Sub->CreateInternetAddr();bool Valid=false;Address->SetIp(TEXT("127.0.0.1"),Valid);Address->SetPort(ListenPort);
    Socket->SetNonBlocking(true);int32 Actual=0;Socket->SetReceiveBufferSize(262144,Actual);
    if(!Valid||!Socket->Bind(*Address)){CloseSocket();ConnectionStatus=TEXT("UDP port unavailable");return;}BoundPort=ListenPort;
}
bool AGo2TelemetryActor::ApplyTelemetryJson(const FString& Json){
    TSharedPtr<FJsonObject> Obj;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Obj)||!Obj)return false;
    FString Schema,IP,NewSource,NewSession;double Seq=0,Age=0;
    if(!Obj->TryGetStringField(TEXT("schema"),Schema)||Schema!=TEXT("go2.telemetry.v1")||!Obj->TryGetStringField(TEXT("robot_ip"),IP)||IP!=ExpectedRobotIP)return false;
    if(!Obj->TryGetStringField(TEXT("source"),NewSource)||!(NewSource==TEXT("unitree_dds")||(bAllowSyntheticData&&NewSource==TEXT("synthetic"))))return false;
    if(!Obj->TryGetStringField(TEXT("session"),NewSession)||NewSession.IsEmpty()||!Obj->TryGetNumberField(TEXT("seq"),Seq)||!FMath::IsFinite(Seq)||Seq<0)return false;
    if(!Obj->TryGetNumberField(TEXT("sample_age_ms"),Age)||!FMath::IsFinite(Age)||Age<0||Age>StaleAfterSeconds*1000)return false;
    if(NewSession==Session&&Seq<=LastSequence)return false;
    TArray<double> Angles,Quat,Position;
    if(!ReadArray(Obj,TEXT("q"),12,Angles))return false;
    for(double X:Angles)if(FMath::Abs(X)>6.5)return false;
    if(!ReadArray(Obj,TEXT("imu_quat_wxyz"),4,Quat))return false;
    // Reflection B=diag(1,-1,1): quaternion xyz -> (-x,y,-z), w unchanged.
    FQuat Converted(-Quat[1],Quat[2],-Quat[3],Quat[0]);if(Converted.SizeSquared()<0.25||Converted.SizeSquared()>2.25)return false;Converted.Normalize();
    const bool NewPosition=ReadArray(Obj,TEXT("position_m"),3,Position);
    if(NewSession!=Session){LastSequence=-1;bHaveOrigin=false;bHavePosition=false;bHavePositionOrigin=false;}
    Session=NewSession;LastSequence=Seq;Source=NewSource;TargetAngles.Reset();for(double Q:Angles)TargetAngles.Add(float(Q));
    BodyRotation=Converted;
    if(NewPosition){bHavePosition=true;BodyPosition=FVector(Position[0]*100,-Position[1]*100,Position[2]*100);}
    if(NewPosition&&!bHavePositionOrigin){InitialPosition=BodyPosition;bHavePositionOrigin=true;}
    if(!bHaveOrigin){InitialYaw=BodyRotation.Rotator().Yaw;bHaveOrigin=true;}
    const int32 OldMode=ReportedMode,OldGait=ReportedGait;double N;
    ReportedMode=Obj->TryGetNumberField(TEXT("mode"),N)&&FMath::IsFinite(N)?int32(N):-1;
    ReportedGait=Obj->TryGetNumberField(TEXT("gait_type"),N)&&FMath::IsFinite(N)?int32(N):-1;
    LastReceive=FPlatformTime::Seconds()-Age/1000;AcceptedFrames++;
    if(AcceptedFrames==1||SmoothingSeconds<=0)JointAnglesRadians=TargetAngles;
    if(OldMode!=ReportedMode||OldGait!=ReportedGait)OnReportedStateChanged(ReportedMode,ReportedGait);
    ApplyPose();UpdateStatus();return true;
}
void AGo2TelemetryActor::RecenterOdometry(){bHaveOrigin=false;bHavePositionOrigin=false;}
void AGo2TelemetryActor::ApplyPose(){
    auto* Mesh=RobotMesh?Cast<USkeletalMesh>(RobotMesh->GetSkinnedAsset()):nullptr;if(!Mesh||JointAnglesRadians.Num()!=12)return;
    const auto& Ref=Mesh->GetRefSkeleton();RobotMesh->BoneSpaceTransforms=Ref.GetRefBonePose();
    for(int32 I=0;I<12;++I){int32 Index=Ref.FindBoneIndex(JointNames[I]);if(Index==INDEX_NONE)return;
        const FVector Axis=I%3==0?FVector(0,1,0):FVector(0,0,-1);
        auto& T=RobotMesh->BoneSpaceTransforms[Index];T.SetRotation((T.GetRotation()*FQuat(Axis,JointAnglesRadians[I])).GetNormalized());
    }
    RobotMesh->SetRelativeRotation(bFollowBodyOrientation&&bHaveOrigin?FRotator(0,-InitialYaw,0).Quaternion()*BodyRotation:FQuat::Identity);
    RobotMesh->MarkRefreshTransformDirty();RobotMesh->RefreshBoneTransforms();
    FVector Offset(0,0,37.2);
    if(bFollowOdometry&&bHavePosition&&bHavePositionOrigin&&bHaveOrigin)Offset+=FRotator(0,-InitialYaw,0).RotateVector(BodyPosition-InitialPosition);
    else if(bGroundFeetWithoutOdometry){
        double MinZ=1e9;for(FName Foot:{FName(TEXT("FL_foot_joint")),FName(TEXT("FR_foot_joint")),FName(TEXT("RL_foot_joint")),FName(TEXT("RR_foot_joint"))}){
            const FVector P=RobotMesh->GetRelativeRotation().RotateVector(RobotMesh->GetBoneLocationByName(Foot,EBoneSpaces::ComponentSpace));MinZ=FMath::Min(MinZ,P.Z);
        }Offset.Z=2.2-MinZ;
    }
    RobotMesh->SetRelativeLocation(Offset);
}
void AGo2TelemetryActor::UpdateStatus(){
    DataAgeSeconds=LastReceive<0?-1.f:float(FPlatformTime::Seconds()-LastReceive);
    bLiveDataFresh=LastReceive>=0&&DataAgeSeconds<=StaleAfterSeconds&&Source==TEXT("unitree_dds");
    if(LastReceive<0&&Socket)ConnectionStatus=TEXT("Waiting for Go2 DDS");
    else if(LastReceive>=0&&DataAgeSeconds>StaleAfterSeconds)ConnectionStatus=TEXT("STALE - last pose held");
    else if(LastReceive>=0)ConnectionStatus=Source==TEXT("synthetic")?TEXT("SYNTHETIC TEST - not live"):TEXT("LIVE - read only");
    if(StatusLabel){StatusLabel->SetText(FText::FromString(ConnectionStatus));StatusLabel->SetTextRenderColor(bLiveDataFresh?FColor::Green:FColor::Orange);}
}
void AGo2TelemetryActor::Tick(float DeltaSeconds){
    Super::Tick(DeltaSeconds);
    PollTelemetry(DeltaSeconds);
}
void AGo2TelemetryActor::PollTelemetry(float DeltaSeconds){
    if(!bReceiveEnabled||BoundPort!=0&&BoundPort!=ListenPort)CloseSocket();if(bReceiveEnabled)OpenSocket();
    if(Socket){uint32 Size=0;int32 Limit=128;while(Limit-->0&&Socket->HasPendingData(Size)){
        TArray<uint8> Buffer;Buffer.SetNumUninitialized(FMath::Min(Size,65507u)+1);int32 Read=0;
        if(Socket->Recv(Buffer.GetData(),Buffer.Num()-1,Read)&&Read>0){Buffer[Read]=0;ApplyTelemetryJson(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Buffer.GetData())));}else break;
    }}
    if(TargetAngles.Num()==12&&JointAnglesRadians.Num()==12&&LastReceive>=0&&FPlatformTime::Seconds()-LastReceive<=StaleAfterSeconds){
        const float A=SmoothingSeconds<=0?1.f:1.f-FMath::Exp(-DeltaSeconds/SmoothingSeconds);
        for(int32 I=0;I<12;++I)JointAnglesRadians[I]=FMath::Lerp(JointAnglesRadians[I],TargetAngles[I],A);ApplyPose();
    }UpdateStatus();
}
