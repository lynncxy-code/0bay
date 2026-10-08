#include "Actors/AvatarActor.h"
#include "Components/CapsuleComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/SceneComponent.h"
#include "HAL/PlatformTime.h"

#if METAVERSE_WITH_ONTOTWIN
#include "GameFramework/CharacterMovementComponent.h"
#include "SceneInteraction/TwinRoamingCharacter.h"
#include "SceneInteraction/TwinRoamingTypes.h"
#endif

AAvatarActor::AAvatarActor()
{
    PrimaryActorTick.bCanEverTick = true;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);
    VisualActorComponent = CreateDefaultSubobject<UChildActorComponent>(TEXT("TaskCharacter"));
    VisualActorComponent->SetupAttachment(SceneRoot);
}

void AAvatarActor::SetTarget(double X, double Y, double Z, double HeadingDeg,
                             double SampleTimeSec)
{
#if METAVERSE_WITH_ONTOTWIN
    if (ATwinRoamingCharacter* Character = VisualActorComponent
        ? Cast<ATwinRoamingCharacter>(VisualActorComponent->GetChildActor()) : nullptr)
    {
        if (Samples.Num() > 0 && SampleTimeSec > Samples.Last().T)
        {
            const double SpeedCmS = FVector::Dist2D(
                FVector(X, Y, Z), Samples.Last().Loc) / (SampleTimeSec - Samples.Last().T);
            Character->SetAutoRouteAnimation(SpeedCmS >= 5.0, SpeedCmS);
        }
        else
        {
            Character->SetAutoRouteAnimation(false);
        }
    }
#endif

    // 时间轴回退(服务重启/换实例后 SyncManager 重建映射):旧样本作废
    if (Samples.Num() > 0 && SampleTimeSec < Samples.Last().T)
    {
        Samples.Reset();
    }
    Samples.Add({ SampleTimeSec, FVector(X, Y, Z), HeadingDeg });
    if (Samples.Num() > 240)   // ~8s @30Hz,足够覆盖任何抖动窗口
    {
        Samples.RemoveAt(0, Samples.Num() - 240);
    }
}

void AAvatarActor::SetState(const FString& State)
{
    CurrentState = State;
    // lost 时可在此触发灰显/淡出(一期保留挂点)
}

void AAvatarActor::ResetTimeline()
{
    Samples.Reset();
}

void AAvatarActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (Samples.Num() == 0)
    {
        return;
    }
    // 渲染时刻 = 当前 - 延迟窗口:永远在已知样本之间插值,不外推、不追赶。
    // 首次出现(样本都晚于渲染时刻)→ 钳在首样本,等价"首帧直接落位";
    // 断流超过窗口(样本都早于渲染时刻)→ 钳在末样本,原地保持。
    // 时钟域约束:此处必须与 FTargetSyncManager 生成 SampleT 用的时钟同域
    //(默认二者都是 FPlatformTime::Seconds())。若将来给 SyncManager 注入
    // 非默认时钟(倍速回放/暂停等),这里也要改成同一个时钟源,否则渲染时刻
    // 与样本时间轴分属两个域,会产生恒定偏差或永久插不到值。
    const double RenderT = FPlatformTime::Seconds() - RenderDelaySeconds;
    int32 i = Samples.Num() - 1;
    while (i > 0 && Samples[i].T > RenderT)
    {
        --i;
    }
    const FPoseSample& A = Samples[i];
    const FPoseSample& B = Samples[FMath::Min(i + 1, Samples.Num() - 1)];
    const double Alpha = (B.T > A.T)
        ? FMath::Clamp((RenderT - A.T) / (B.T - A.T), 0.0, 1.0) : 0.0;

    SetActorLocation(FMath::Lerp(A.Loc, B.Loc, Alpha));

    // 朝向按最短弧插值
    double D = FMath::Fmod(B.HeadingDeg - A.HeadingDeg, 360.0);
    if (D > 180.0) { D -= 360.0; } else if (D < -180.0) { D += 360.0; }
    SetActorRotation(FRotator(0.0, A.HeadingDeg + D * Alpha, 0.0));
}

void AAvatarActor::ResetForReuse()
{
    Samples.Reset();
    CurrentState = TEXT("active");
}

void AAvatarActor::ConfigureVisual(TSubclassOf<AActor> VisualClass, UObject* CharacterAsset,
                                   const FTransform& RelativeTransform)
{
    if (VisualActorComponent == nullptr)
    {
        return;
    }

    FTransform EffectiveTransform = RelativeTransform;
    bVisualCollisionEnabled = false;

#if METAVERSE_WITH_ONTOTWIN
    UTwinCharacterAsset* TwinAsset = Cast<UTwinCharacterAsset>(CharacterAsset);
    if (TwinAsset != nullptr)
    {
        VisualClass = ATwinRoamingCharacter::StaticClass();
        EffectiveTransform.AddToTranslation(FVector(0.0, 0.0, TwinAsset->CapsuleHalfHeightCm));
    }
#endif

    VisualActorComponent->SetRelativeTransform(EffectiveTransform);
    if (VisualActorComponent->GetChildActorClass() != VisualClass)
    {
        // SetChildActorClass 会销毁旧子角色并生成指定任务包角色；nullptr 会清掉旧外观。
        VisualActorComponent->SetChildActorClass(VisualClass);
    }

    // 普通外观默认仍是纯可视化；OntoTwin 人物在资产应用成功后单独恢复原生胶囊碰撞。
    if (AActor* VisualActor = VisualActorComponent->GetChildActor())
    {
        VisualActor->SetActorEnableCollision(false);
    }

#if METAVERSE_WITH_ONTOTWIN
    if (TwinAsset != nullptr)
    {
        ATwinRoamingCharacter* Character =
            Cast<ATwinRoamingCharacter>(VisualActorComponent->GetChildActor());
        FString Error;
        if (Character == nullptr || !Character->ApplyCharacterAsset(TwinAsset, Error))
        {
            UE_LOG(LogTemp, Error, TEXT("[MetaverseClient] OntoTwin 人物资产加载失败 asset=%s error=%s"),
                *TwinAsset->GetPathName(), *Error);
        }
        else
        {
            if (UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
            {
                // 数据总线继续拥有位置（包括楼梯/二层 Z），胶囊负责与场景及玩家碰撞。
                // Pawn Profile 保留任务包角色与 WorldStatic/WorldDynamic/Pawn 的标准响应。
                Capsule->SetCollisionProfileName(TEXT("Pawn"));
                Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
                Character->SetActorEnableCollision(true);
                bVisualCollisionEnabled = true;
            }
            if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
            {
                // 禁止 CharacterMovement/重力改写总线坐标；胶囊会随父级流式轨迹移动。
                Movement->DisableMovement();
            }
            UE_LOG(LogTemp, Log,
                TEXT("[MetaverseClient] 已加载 OntoTwin 人物资产 %s capsule=%.1fx%.1f collision=%s"),
                *TwinAsset->GetPathName(), TwinAsset->CapsuleRadiusCm,
                TwinAsset->CapsuleHalfHeightCm,
                bVisualCollisionEnabled ? TEXT("Pawn") : TEXT("disabled"));
        }
    }
#else
    if (CharacterAsset != nullptr)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[MetaverseClient] 配置了任务角色资产 %s，但宿主未检测到 OntoTwinSync"),
            *CharacterAsset->GetPathName());
    }
#endif
}

void AAvatarActor::SetRuntimeActive(bool bActive)
{
    SetActorHiddenInGame(!bActive);
    SetActorEnableCollision(bActive);
    SetActorTickEnabled(bActive);

    if (VisualActorComponent != nullptr)
    {
        if (AActor* VisualActor = VisualActorComponent->GetChildActor())
        {
            VisualActor->SetActorHiddenInGame(!bActive);
            VisualActor->SetActorEnableCollision(bActive && bVisualCollisionEnabled);
            VisualActor->SetActorTickEnabled(bActive);
        }
    }
}
