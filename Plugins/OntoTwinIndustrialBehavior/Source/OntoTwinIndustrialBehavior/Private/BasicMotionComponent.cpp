#include "BasicMotionComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

UBasicMotionComponent::UBasicMotionComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

bool UBasicMotionComponent::Apply(const FString& Behavior, const TSharedPtr<FJsonObject>& Params)
{
    ResetMotion();
    if (Behavior == TEXT("motion.reset") || Behavior.StartsWith(TEXT("safe."))) return true;
    static const TSet<FString> Modes = {TEXT("motion.rotate"), TEXT("motion.translate"),
        TEXT("motion.rotate_to"), TEXT("motion.pingpong"), TEXT("motion.swing")};
    if (!Modes.Contains(Behavior)) return false;
    auto ReadNumber = [&Params](const TCHAR* Key, double Default, double Min, double Max, double& Out)
    {
        Out = Default;
        if (Params && Params->HasField(Key) && !Params->TryGetNumberField(Key, Out)) return false;
        return FMath::IsFinite(Out) && Out >= Min && Out <= Max;
    };
    FString AxisName = TEXT("z"), DirectionName = TEXT("positive");
    if (Params)
    {
        if (Params->HasField(TEXT("axis")) && !Params->TryGetStringField(TEXT("axis"), AxisName)) return false;
        if (Params->HasField(TEXT("direction")) && !Params->TryGetStringField(TEXT("direction"), DirectionName)) return false;
    }
    if (AxisName != TEXT("x") && AxisName != TEXT("y") && AxisName != TEXT("z")) return false;
    if (DirectionName != TEXT("positive") && DirectionName != TEXT("negative")) return false;
    if (!ReadNumber(TEXT("speed"), 90, 1, 720, Speed)
        || !ReadNumber(TEXT("distance"), 100, 0, 10000, Distance)
        || !ReadNumber(TEXT("angle"), 90, 0, 360, Angle)
        || !ReadNumber(TEXT("duration"), 2, .1, 120, Duration)) return false;
    Axis = AxisName == TEXT("x") ? FVector::ForwardVector : AxisName == TEXT("y") ? FVector::RightVector : FVector::UpVector;
    Direction = DirectionName == TEXT("negative") ? -1 : 1;
    FString TargetName = TEXT("model");
    if (Params && Params->HasField(TEXT("target")) && !Params->TryGetStringField(TEXT("target"), TargetName)) return false;
    if (TargetName != TEXT("model") && TargetName != TEXT("tagged")) return false;
    bTaggedOnly = TargetName == TEXT("tagged");
    // Older TwinInstance uses its mesh as root. Split a stable spatial root
    // from the rendered model once, so data-driven location keeps one owner.
    if (auto* OldRoot = Cast<UMeshComponent>(GetOwner()->GetRootComponent()))
    {
        if (OldRoot->IsSimulatingPhysics()) return false;
        const FTransform World = GetOwner()->GetActorTransform();
        auto* SpatialRoot = NewObject<USceneComponent>(GetOwner(), TEXT("OT_SpatialRoot"), RF_Transient);
        SpatialRoot->SetMobility(EComponentMobility::Movable);
        GetOwner()->AddInstanceComponent(SpatialRoot);
        SpatialRoot->RegisterComponent();
        SpatialRoot->SetWorldTransform(World);
        GetOwner()->SetRootComponent(SpatialRoot);
        OldRoot->AttachToComponent(SpatialRoot, FAttachmentTransformRules::KeepWorldTransform);
    }
    Mode = Behavior;
    DiscoverTargets();
    if (bTaggedOnly && Targets.IsEmpty())
        UE_LOG(LogTemp, Warning, TEXT("[OT-Motion] %s: no movable model component tagged OntoTwinMotionTarget; waiting, never falling back to whole model"), *GetOwner()->GetName());
    ApplyOffset();
    // Async GLB meshes may not exist yet; Tick discovers them when loaded.
    return true;
}

void UBasicMotionComponent::DiscoverTargets()
{
    TArray<UMeshComponent*> Meshes;
    GetOwner()->GetComponents<UMeshComponent>(Meshes, true);
    TSet<USceneComponent*> Candidates;
    for (auto* Mesh : Meshes)
    {
        if (!IsValid(Mesh) || Mesh->ComponentHasTag(TEXT("OntoTwinPresentation"))
            || Mesh == GetOwner()->GetRootComponent()) continue;
        const auto* Static = Cast<UStaticMeshComponent>(Mesh);
        const auto* Skeletal = Cast<USkeletalMeshComponent>(Mesh);
        if ((!Static || !Static->GetStaticMesh()) && (!Skeletal || !Skeletal->GetSkeletalMeshAsset())) continue;
        if (bTaggedOnly && !Mesh->ComponentHasTag(TEXT("OntoTwinMotionTarget"))) continue;
        if (Mesh->Mobility != EComponentMobility::Movable || Mesh->IsSimulatingPhysics()) continue;
        Candidates.Add(Mesh);
    }
    for (auto* Candidate : Candidates)
    {
        bool bHasMeshParent = false;
        for (auto* Parent = Candidate->GetAttachParent(); Parent; Parent = Parent->GetAttachParent())
            if (Candidates.Contains(Parent)) { bHasMeshParent = true; break; }
        if (bHasMeshParent || Targets.ContainsByPredicate([Candidate](const FTarget& T) {return T.Component == Candidate;})) continue;
        FTarget Target;
        Target.Component = Candidate;
        Target.OriginalRelative = Candidate->GetRelativeTransform();
        Target.OriginalInOwner = Candidate->GetComponentTransform().GetRelativeTransform(GetOwner()->GetActorTransform());
        Targets.Add(Target);
    }
}

void UBasicMotionComponent::ApplyOffset()
{
    double Amount = FMath::Clamp(Elapsed / Duration, 0.0, 1.0);
    if (Mode == TEXT("motion.pingpong") || Mode == TEXT("motion.swing"))
        Amount = 1.0 - FMath::Abs(FMath::Fmod(Elapsed / Duration, 2.0) - 1.0);
    const bool bTranslate = Mode == TEXT("motion.translate") || Mode == TEXT("motion.pingpong");
    FTransform Offset = FTransform::Identity;
    if (bTranslate) Offset.SetTranslation(Axis * (Direction * Distance * Amount));
    else Offset.SetRotation(FQuat(Axis, FMath::DegreesToRadians(Direction *
        (Mode == TEXT("motion.rotate") ? FMath::Fmod(Elapsed * Speed, 360.0) : Angle * Amount))));
    for (auto& Target : Targets)
        if (Target.Component.IsValid())
        {
            if (bTaggedOnly)
            {
                FTransform Local = Target.OriginalRelative;
                Local.SetTranslation(Local.GetTranslation() + Offset.GetTranslation());
                Local.SetRotation(Local.GetRotation() * Offset.GetRotation());
                Target.Component->SetRelativeTransform(Local, false, nullptr, ETeleportType::TeleportPhysics);
            }
            else Target.Component->SetWorldTransform(Target.OriginalInOwner * Offset * GetOwner()->GetActorTransform(), false, nullptr, ETeleportType::TeleportPhysics);
        }
}

void UBasicMotionComponent::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Function)
{
    Super::TickComponent(Delta, Type, Function);
    if (Mode.IsEmpty()) return;
    Elapsed += FMath::Max(0.f, Delta);
    DiscoverTargets();
    ApplyOffset();
}

void UBasicMotionComponent::ResetMotion()
{
    for (auto& Target : Targets)
        if (Target.Component.IsValid()) Target.Component->SetRelativeTransform(Target.OriginalRelative);
    Targets.Reset();
    Mode.Reset();
    Elapsed = 0;
}

void UBasicMotionComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    ResetMotion();
    Super::EndPlay(Reason);
}
