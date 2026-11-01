#include "SceneInteraction/TwinRouteFollowerComponent.h"

#include "SceneInteraction/TwinRoamingRoute.h"
#include "SceneInteraction/TwinRoamingCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"

namespace
{
constexpr float RouteLookAheadSeconds = 0.8f;
constexpr float RouteMinLookAheadCm = 100.0f;
constexpr float RouteMaxLookAheadCm = 200.0f;
constexpr float RouteMaxYawRateDegS = 100.0f;
constexpr float RouteSteeringLookAheadSeconds = 0.25f;
constexpr float RouteMinSteeringLookAheadCm = 30.0f;
constexpr float RouteMaxSteeringLookAheadCm = 80.0f;
constexpr float RouteArrivalToleranceCm = 20.0f;
constexpr float RouteProgressThresholdCm = 5.0f;
constexpr float RouteBlockedTimeoutSeconds = 2.0f;

void ClearRouteAnimationMotion(ACharacter* Character)
{
    if (!Character) return;
    if (ATwinRoamingCharacter* RoamingCharacter = Cast<ATwinRoamingCharacter>(Character))
    {
        RoamingCharacter->SetAutoRouteCameraSmoothing(false);
        RoamingCharacter->SetAutoRouteAnimation(false);
    }
    Character->ConsumeMovementInputVector();
    Character->GetCharacterMovement()->StopActiveMovement();
    Character->GetCharacterMovement()->StopMovementImmediately();
}

void SetRouteAnimationMotion(ACharacter* Character, FVector VisualVelocity)
{
    if (!Character) return;
    VisualVelocity.Z = 0.0f;
    if (ATwinRoamingCharacter* RoamingCharacter = Cast<ATwinRoamingCharacter>(Character))
    {
        RoamingCharacter->SetAutoRouteCameraSmoothing(true);
        if (RoamingCharacter->SetAutoRouteAnimation(true, VisualVelocity.Size2D()))
        {
            return;
        }
    }
    // CharacterMovement now owns automatic route motion. Its real velocity
    // and acceleration drive ordinary locomotion AnimBPs; do not overwrite
    // Velocity here or StepUp/floor following would be bypassed again.
}
}

UTwinRouteFollowerComponent::UTwinRouteFollowerComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UTwinRouteFollowerComponent::Configure(
    ATwinRoamingRoute* InRoute,
    float InSpeedCmS,
    bool bInLoop,
    bool bAutoStart,
    const TArray<FTwinRoamingRuntimeWaypoint>& InWaypoints)
{
    Route = InRoute;
    SpeedCmS = FMath::Max(1.0f, InSpeedCmS);
    bLoop = bInLoop && Route && Route->Spline && Route->Spline->IsClosedLoop();
    RuntimeWaypoints = InWaypoints;
    DistanceAlongSpline = 0.0f;
    RouteState = Route && Route->Spline
        ? (bAutoStart ? ETwinRoamingRouteState::AutoRoute : ETwinRoamingRouteState::Idle)
        : ETwinRoamingRouteState::Unavailable;
    ResetStallDetection();
    ResetNarrationSession();
    if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
    {
        ClearRouteAnimationMotion(Character);
    }
}

void UTwinRouteFollowerComponent::SetSpeed(float InSpeedCmS)
{
    SpeedCmS = FMath::Max(1.0f, InSpeedCmS);
}

void UTwinRouteFollowerComponent::SetLoop(bool bInLoop)
{
    bLoop = bInLoop && Route && Route->Spline && Route->Spline->IsClosedLoop();
}

void UTwinRouteFollowerComponent::PauseByUser()
{
    if (RouteState == ETwinRoamingRouteState::PausedForNarration)
    {
        InterruptNarrationByUser();
        return;
    }
    if (RouteState == ETwinRoamingRouteState::AutoRoute || RouteState == ETwinRoamingRouteState::Joining)
    {
        RouteState = ETwinRoamingRouteState::PausedByUser;
        ResetStallDetection();
        if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
        {
            ClearRouteAnimationMotion(Character);
        }
    }
}

void UTwinRouteFollowerComponent::CompleteNarration()
{
    if (RouteState != ETwinRoamingRouteState::PausedForNarration) return;
    ++NextWaypointIndex;
    RouteState = bPauseAfterNarration
        ? ETwinRoamingRouteState::PausedByUser
        : ETwinRoamingRouteState::AutoRoute;
    bPauseAfterNarration = false;
    ResetStallDetection();
}

void UTwinRouteFollowerComponent::InterruptNarrationByUser()
{
    if (RouteState != ETwinRoamingRouteState::PausedForNarration) return;
    ++NextWaypointIndex;
    RouteState = ETwinRoamingRouteState::PausedByUser;
    bPauseAfterNarration = false;
    ResetStallDetection();
    if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
    {
        ClearRouteAnimationMotion(Character);
    }
}

bool UTwinRouteFollowerComponent::TogglePauseAfterNarration()
{
    if (RouteState != ETwinRoamingRouteState::PausedForNarration)
    {
        return false;
    }
    bPauseAfterNarration = !bPauseAfterNarration;
    return true;
}

bool UTwinRouteFollowerComponent::IsSafeJoin(const FVector& Target, FString& OutError) const
{
    const ACharacter* Character = Cast<ACharacter>(GetOwner());
    UWorld* World = GetWorld();
    if (!Character || !World)
    {
        OutError = TEXT("Character or world is unavailable");
        return false;
    }

    const FVector Start = Character->GetActorLocation();
    if (FMath::Abs(Start.Z - Target.Z) > 150.0f)
    {
        OutError = TEXT("Route is on another floor or outside the vertical tolerance");
        return false;
    }
    if (FVector::Dist2D(Start, Target) > 1000.0f)
    {
        OutError = TEXT("Route is too far away; restart from the beginning instead");
        return false;
    }

    // Do not pre-sweep the whole straight segment. A stair riser is a valid
    // blocking hit for a raw capsule sweep even though CharacterMovement can
    // StepUp it safely. The real join is collision-aware and times out when a
    // wall or other non-walkable obstacle prevents progress.
    return true;
}

bool UTwinRouteFollowerComponent::TryResume(FString& OutError)
{
    if (!Route || !Route->Spline)
    {
        OutError = TEXT("Default route is unavailable");
        return false;
    }
    JoinTargetDistance = FindClosestDistanceOnSpline(GetOwner()->GetActorLocation());
    const FVector Target = GetCharacterLocationAtDistance(JoinTargetDistance);
    if (!IsSafeJoin(Target, OutError)) return false;
    RouteState = ETwinRoamingRouteState::Joining;
    ResetStallDetection();
    return true;
}

bool UTwinRouteFollowerComponent::TryStartFromSpawn(FString& OutError)
{
    if (!Route || !Route->Spline)
    {
        OutError = TEXT("Default route is unavailable");
        return false;
    }
    JoinTargetDistance = 0.0f;
    const FVector Target = GetCharacterLocationAtDistance(0.0f);
    if (!IsSafeJoin(Target, OutError)) return false;
    RouteState = ETwinRoamingRouteState::Joining;
    ResetStallDetection();
    return true;
}

float UTwinRouteFollowerComponent::FindClosestDistanceOnSpline(const FVector& WorldLocation) const
{
    if (!Route || !Route->Spline) return 0.0f;
    const float Length = Route->Spline->GetSplineLength();
    const int32 Samples = FMath::Clamp(FMath::CeilToInt(Length / 100.0f), 32, 1024);
    const float Step = Length / Samples;
    float BestDistance = 0.0f;
    float BestDistanceSquared = TNumericLimits<float>::Max();
    for (int32 Index = 0; Index <= Samples; ++Index)
    {
        const float Distance = FMath::Min(Length, Index * Step);
        const FVector Point = GetCharacterLocationAtDistance(Distance);
        const float Candidate = FVector::DistSquared(WorldLocation, Point);
        if (Candidate < BestDistanceSquared)
        {
            BestDistanceSquared = Candidate;
            BestDistance = Distance;
        }
    }

    float SearchStep = Step * 0.5f;
    for (int32 Iteration = 0; Iteration < 6; ++Iteration)
    {
        for (int32 DirectionSign = -1; DirectionSign <= 1; DirectionSign += 2)
        {
            const float Direction = static_cast<float>(DirectionSign);
            const float Distance = FMath::Clamp(BestDistance + Direction * SearchStep, 0.0f, Length);
            const FVector Point = GetCharacterLocationAtDistance(Distance);
            const float Candidate = FVector::DistSquared(WorldLocation, Point);
            if (Candidate < BestDistanceSquared)
            {
                BestDistanceSquared = Candidate;
                BestDistance = Distance;
            }
        }
        SearchStep *= 0.5f;
    }
    return BestDistance;
}

FVector UTwinRouteFollowerComponent::GetCharacterLocationAtDistance(float Distance) const
{
    if (!Route || !Route->Spline) return FVector::ZeroVector;
    FVector Location = Route->Spline->GetLocationAtDistanceAlongSpline(
        Distance, ESplineCoordinateSpace::World);
    if (Route->bSplineAtGroundLevel)
    {
        if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
        {
            Location.Z += Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        }
    }
    return Location;
}

bool UTwinRouteFollowerComponent::RestartFromBeginning(FString& OutError)
{
    ACharacter* Character = Cast<ACharacter>(GetOwner());
    if (!Character || !Route || !Route->Spline || !GetWorld())
    {
        OutError = TEXT("Default route is unavailable");
        return false;
    }

    FVector Start = Route->Spline->GetLocationAtDistanceAlongSpline(0.0f, ESplineCoordinateSpace::World);
    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TwinRouteRestart), false, Character);
    if (Route) QueryParams.AddIgnoredActor(Route);
    const float Radius = Character->GetCapsuleComponent()->GetScaledCapsuleRadius();
    const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    // Runtime routes have already projected every spline point to the
    // calibrated floor. Re-tracing from high above would hit the roof first.
    Start += FVector(0.0f, 0.0f, HalfHeight + 2.0f);
    if (GetWorld()->OverlapBlockingTestByChannel(
        Start,
        Character->GetActorQuat(),
        ECC_Pawn,
        FCollisionShape::MakeCapsule(Radius, HalfHeight),
        QueryParams))
    {
        OutError = TEXT("Route start is blocked for the character capsule");
        return false;
    }

    FHitResult MoveHit;
    Character->SetActorLocation(Start, false, &MoveHit, ETeleportType::TeleportPhysics);
    DistanceAlongSpline = 0.0f;
    RouteState = ETwinRoamingRouteState::AutoRoute;
    ResetStallDetection();
    ResetNarrationSession();
    return true;
}

void UTwinRouteFollowerComponent::StopRoute()
{
    RouteState = Route ? ETwinRoamingRouteState::Idle : ETwinRoamingRouteState::Unavailable;
    bPauseAfterNarration = false;
    ResetStallDetection();
    if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
    {
        ClearRouteAnimationMotion(Character);
    }
}

void UTwinRouteFollowerComponent::ResetNarrationSession()
{
    NextWaypointIndex = 0;
    PreviousSplineDistance = 0.0f;
    bPauseAfterNarration = false;
    BlockedDiagnosticText.Reset();
}

void UTwinRouteFollowerComponent::AdvancePassedWaypoints(float SplineLength)
{
    if (!Route || !Route->Spline || RuntimeWaypoints.Num() == 0) return;
    const int32 PointCount = Route->Spline->GetNumberOfSplinePoints();
    while (NextWaypointIndex < RuntimeWaypoints.Num() && NextWaypointIndex < PointCount)
    {
        const float PointDistance = Route->Spline->GetDistanceAlongSplineAtSplinePoint(
            NextWaypointIndex);
        const float Radius = FMath::Max(
            RouteArrivalToleranceCm,
            RuntimeWaypoints[NextWaypointIndex].TriggerRadiusCm);
        if (DistanceAlongSpline <= PointDistance + Radius) break;
        ++NextWaypointIndex;
    }
    if (bLoop && NextWaypointIndex >= RuntimeWaypoints.Num()
        && PreviousSplineDistance > SplineLength * 0.65f
        && DistanceAlongSpline < SplineLength * 0.35f)
    {
        NextWaypointIndex = 0;
    }
}

bool UTwinRouteFollowerComponent::TryTriggerNarration(float SplineLength)
{
    if (!Route || !Route->Spline || RuntimeWaypoints.Num() == 0) return false;
    AdvancePassedWaypoints(SplineLength);
    const int32 PointCount = Route->Spline->GetNumberOfSplinePoints();
    while (NextWaypointIndex < RuntimeWaypoints.Num() && NextWaypointIndex < PointCount)
    {
        const FTwinRoamingRuntimeWaypoint& Waypoint = RuntimeWaypoints[NextWaypointIndex];
        const float PointDistance = Route->Spline->GetDistanceAlongSplineAtSplinePoint(
            NextWaypointIndex);
        const float Radius = FMath::Max(RouteArrivalToleranceCm, Waypoint.TriggerRadiusCm);
        const FVector WorldPoint = GetCharacterLocationAtDistance(PointDistance);
        const bool bReachedAlongSpline = DistanceAlongSpline + Radius >= PointDistance;
        const bool bReachedInWorld = FVector::Dist2D(GetOwner()->GetActorLocation(), WorldPoint) <= Radius;
        if (!bReachedAlongSpline || !bReachedInWorld) return false;
        if (!Waypoint.HasNarration())
        {
            ++NextWaypointIndex;
            continue;
        }
        RouteState = ETwinRoamingRouteState::PausedForNarration;
        if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
        {
            ClearRouteAnimationMotion(Character);
        }
        OnNarrationRequested.Broadcast(Waypoint);
        return true;
    }
    return false;
}

void UTwinRouteFollowerComponent::UpdateFacing(
    const FVector& FacingDirection,
    float DeltaTime)
{
    AActor* Owner = GetOwner();
    if (!Owner || FacingDirection.IsNearlyZero()) return;

    const float PreviousActorYaw = Owner->GetActorRotation().Yaw;
    AController* Controller = nullptr;
    float CameraYawOffset = 0.0f;
    if (ACharacter* Character = Cast<ACharacter>(Owner))
    {
        Controller = Character->GetController();
        if (Controller)
        {
            CameraYawOffset = FMath::FindDeltaAngleDegrees(
                PreviousActorYaw,
                Controller->GetControlRotation().Yaw);
        }
    }
    const float DesiredActorYaw = FacingDirection.Rotation().Yaw;
    const float NewActorYaw = FMath::FixedTurn(
        PreviousActorYaw,
        DesiredActorYaw,
        RouteMaxYawRateDegS * FMath::Max(0.0f, DeltaTime));
    Owner->SetActorRotation(FRotator(0.0f, NewActorYaw, 0.0f));
    if (Controller)
    {
        const FRotator ControlRotation = Controller->GetControlRotation();
        Controller->SetControlRotation(FRotator(
            ControlRotation.Pitch,
            NewActorYaw + CameraYawOffset,
            0.0f));
    }
}

bool UTwinRouteFollowerComponent::RequestCharacterMovement(
    const FVector& Target,
    const FVector& FacingDirection,
    float DeltaTime)
{
    ACharacter* Character = Cast<ACharacter>(GetOwner());
    UCharacterMovementComponent* Movement = Character
        ? Character->GetCharacterMovement() : nullptr;
    if (!Character || !Movement) return false;

    FVector Direction = Target - Character->GetActorLocation();
    // The route provides plan direction. CharacterMovement owns capsule Z,
    // floor snapping, slopes and StepUp while walking over real collision.
    Direction.Z = 0.0f;
    const FVector RequestedVelocity = Direction.GetSafeNormal2D() * SpeedCmS;
    Movement->RequestDirectMove(RequestedVelocity, false);
    UpdateFacing(FacingDirection, DeltaTime);

    FVector AnimationVelocity = Movement->Velocity;
    if (AnimationVelocity.SizeSquared2D() < FMath::Square(10.0f))
    {
        AnimationVelocity = RequestedVelocity;
    }
    SetRouteAnimationMotion(Character, AnimationVelocity);
    return true;
}

void UTwinRouteFollowerComponent::ResetStallDetection()
{
    NoProgressSeconds = 0.0f;
    if (const AActor* Owner = GetOwner())
    {
        StallReferenceLocation = Owner->GetActorLocation();
        bHasStallReference = true;
    }
    else
    {
        StallReferenceLocation = FVector::ZeroVector;
        bHasStallReference = false;
    }
}

bool UTwinRouteFollowerComponent::HasTimedOutWithoutProgress(float DeltaTime)
{
    const AActor* Owner = GetOwner();
    if (!Owner) return true;
    const FVector Current = Owner->GetActorLocation();
    if (!bHasStallReference
        || FVector::DistSquared(Current, StallReferenceLocation)
            >= FMath::Square(RouteProgressThresholdCm))
    {
        StallReferenceLocation = Current;
        NoProgressSeconds = 0.0f;
        bHasStallReference = true;
        return false;
    }
    NoProgressSeconds += FMath::Max(0.0f, DeltaTime);
    return NoProgressSeconds >= RouteBlockedTimeoutSeconds;
}

void UTwinRouteFollowerComponent::MarkBlocked(const FVector& Target, const TCHAR* Phase)
{
    RouteState = ETwinRoamingRouteState::Blocked;
    ResetStallDetection();
    if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
    {
        const FVector Current = Character->GetActorLocation();
        const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
        const FHitResult* FloorHit = Movement && Movement->CurrentFloor.bBlockingHit
            ? &Movement->CurrentFloor.HitResult : nullptr;

        FHitResult SweepHit;
        FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TwinRouteBlockedDiagnostic), false, Character);
        const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
        const FVector SweepEnd(Target.X, Target.Y, Current.Z);
        if (GetWorld() && Capsule)
        {
            GetWorld()->SweepSingleByChannel(
                SweepHit,
                Current,
                SweepEnd,
                FQuat::Identity,
                ECC_Pawn,
                FCollisionShape::MakeCapsule(
                    Capsule->GetScaledCapsuleRadius(),
                    Capsule->GetScaledCapsuleHalfHeight()),
                QueryParams);
        }

        BlockedDiagnosticText = FString::Printf(
            TEXT("X %.0f / Y %.0f · %s"),
            Current.X,
            Current.Y,
            SweepHit.GetActor() ? *SweepHit.GetActor()->GetName() : TEXT("未捕获前方碰撞物"));

        UE_LOG(LogTemp, Error,
            TEXT("OntoTwin route blocked: phase=%s current=(%.1f, %.1f, %.1f) target=(%.1f, %.1f, %.1f) movement_mode=%d velocity=(%.1f, %.1f, %.1f) floor_actor=%s floor_component=%s sweep_actor=%s sweep_component=%s sweep_impact=(%.1f, %.1f, %.1f)"),
            Phase ? Phase : TEXT("unknown"),
            Current.X, Current.Y, Current.Z,
            Target.X, Target.Y, Target.Z,
            Movement ? static_cast<int32>(Movement->MovementMode) : -1,
            Movement ? Movement->Velocity.X : 0.0f,
            Movement ? Movement->Velocity.Y : 0.0f,
            Movement ? Movement->Velocity.Z : 0.0f,
            FloorHit && FloorHit->GetActor() ? *FloorHit->GetActor()->GetName() : TEXT("none"),
            FloorHit && FloorHit->GetComponent() ? *FloorHit->GetComponent()->GetName() : TEXT("none"),
            SweepHit.GetActor() ? *SweepHit.GetActor()->GetName() : TEXT("none"),
            SweepHit.GetComponent() ? *SweepHit.GetComponent()->GetName() : TEXT("none"),
            SweepHit.ImpactPoint.X, SweepHit.ImpactPoint.Y, SweepHit.ImpactPoint.Z);
        ClearRouteAnimationMotion(Character);
    }
}

void UTwinRouteFollowerComponent::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!Route || !Route->Spline || !GetOwner()) return;

    if (RouteState == ETwinRoamingRouteState::Joining)
    {
        const FVector JoinTarget = GetCharacterLocationAtDistance(JoinTargetDistance);
        const FVector Current = GetOwner()->GetActorLocation();
        FVector MotionTarget = JoinTarget;
        if (Route->bSplineAtGroundLevel) MotionTarget.Z = Current.Z;
        if (FVector::Dist2D(Current, MotionTarget) <= RouteArrivalToleranceCm)
        {
            DistanceAlongSpline = JoinTargetDistance;
            RouteState = ETwinRoamingRouteState::AutoRoute;
            ResetStallDetection();
            return;
        }
        if (!RequestCharacterMovement(MotionTarget, JoinTarget - Current, DeltaTime)
            || HasTimedOutWithoutProgress(DeltaTime))
        {
            MarkBlocked(MotionTarget, TEXT("joining"));
        }
        return;
    }

    if (RouteState != ETwinRoamingRouteState::AutoRoute) return;
    const float Length = Route->Spline->GetSplineLength();
    if (Length <= UE_SMALL_NUMBER)
    {
        RouteState = ETwinRoamingRouteState::Completed;
        if (ACharacter* Character = Cast<ACharacter>(GetOwner())) ClearRouteAnimationMotion(Character);
        return;
    }

    const FVector Current = GetOwner()->GetActorLocation();
    const float ClosestKey = Route->Spline->FindInputKeyClosestToWorldLocation(Current);
    const float ClosestDistance = Route->Spline->GetDistanceAlongSplineAtSplineInputKey(ClosestKey);
    if (bLoop && Route->Spline->IsClosedLoop())
    {
        DistanceAlongSpline = ClosestDistance;
    }
    else
    {
        DistanceAlongSpline = FMath::Max(DistanceAlongSpline, ClosestDistance);
        if (TryTriggerNarration(Length))
        {
            PreviousSplineDistance = DistanceAlongSpline;
            return;
        }
        const FVector End = GetCharacterLocationAtDistance(Length);
        if (FVector::Dist(Current, End) <= RouteArrivalToleranceCm)
        {
            DistanceAlongSpline = Length;
            RouteState = ETwinRoamingRouteState::Completed;
            if (ACharacter* Character = Cast<ACharacter>(GetOwner())) ClearRouteAnimationMotion(Character);
            return;
        }
    }

    if (bLoop)
    {
        if (PreviousSplineDistance > Length * 0.65f && DistanceAlongSpline < Length * 0.35f)
        {
            NextWaypointIndex = 0;
        }
        if (TryTriggerNarration(Length))
        {
            PreviousSplineDistance = DistanceAlongSpline;
            return;
        }
    }
    PreviousSplineDistance = DistanceAlongSpline;

    const float SteeringLookAheadCm = FMath::Clamp(
        SpeedCmS * RouteSteeringLookAheadSeconds,
        RouteMinSteeringLookAheadCm,
        RouteMaxSteeringLookAheadCm);
    float TargetDistance = DistanceAlongSpline + SteeringLookAheadCm;
    if (bLoop && Route->Spline->IsClosedLoop())
    {
        TargetDistance = FMath::Fmod(TargetDistance, Length);
    }
    else
    {
        TargetDistance = FMath::Min(TargetDistance, Length);
    }
    const FVector Target = GetCharacterLocationAtDistance(TargetDistance);
    const FVector Tangent = Route->Spline->GetDirectionAtDistanceAlongSpline(
        TargetDistance, ESplineCoordinateSpace::World);
    const float LookAheadCm = FMath::Clamp(
        SpeedCmS * RouteLookAheadSeconds,
        RouteMinLookAheadCm,
        RouteMaxLookAheadCm);
    float FacingDistance = TargetDistance + LookAheadCm;
    if (bLoop && Route->Spline->IsClosedLoop())
    {
        FacingDistance = FMath::Fmod(FacingDistance, FMath::Max(1.0f, Length));
    }
    else
    {
        FacingDistance = FMath::Min(FacingDistance, Length);
    }
    FVector FacingDirection = GetCharacterLocationAtDistance(FacingDistance) - Target;
    if (FacingDirection.IsNearlyZero()) FacingDirection = Tangent;
    if (!RequestCharacterMovement(Target, FacingDirection, DeltaTime)
        || HasTimedOutWithoutProgress(DeltaTime))
    {
        MarkBlocked(Target, TEXT("auto_route"));
    }
}

FString UTwinRouteFollowerComponent::GetRouteStateText() const
{
    switch (RouteState)
    {
    case ETwinRoamingRouteState::Idle: return TEXT("idle");
    case ETwinRoamingRouteState::AutoRoute: return TEXT("auto_route");
    case ETwinRoamingRouteState::PausedForNarration: return TEXT("paused_for_narration");
    case ETwinRoamingRouteState::PausedByUser: return TEXT("paused_by_user");
    case ETwinRoamingRouteState::Joining: return TEXT("joining");
    case ETwinRoamingRouteState::Completed: return TEXT("completed");
    case ETwinRoamingRouteState::Blocked: return TEXT("blocked");
    default: return TEXT("unavailable");
    }
}
