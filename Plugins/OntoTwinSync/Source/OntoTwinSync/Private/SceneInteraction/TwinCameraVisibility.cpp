#include "SceneInteraction/TwinCameraVisibility.h"

#include "Components/LightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Misc/PackageName.h"

namespace
{
void AppendUniqueNames(TArray<FName>& Target, const TArray<FName>& Source)
{
    for (const FName Name : Source)
    {
        if (!Name.IsNone()) Target.AddUnique(Name);
    }
}

bool MatchesActorName(const AActor* Actor, const TArray<FName>& Names)
{
    if (!Actor || Names.IsEmpty()) return false;
    if (Names.Contains(Actor->GetFName())) return true;
#if WITH_EDITOR
    return Names.Contains(FName(*Actor->GetActorLabel(false)));
#else
    return false;
#endif
}

FName GetActorLevelShortName(const AActor* Actor)
{
    if (!Actor || !Actor->GetLevel() || !Actor->GetLevel()->GetOutermost()) return NAME_None;
    const FString PackageName = UWorld::RemovePIEPrefix(
        Actor->GetLevel()->GetOutermost()->GetName());
    return FName(*FPackageName::GetShortName(PackageName));
}

void AddActorAndAttachments(
    AActor* Actor,
    bool bIncludeAttachedActors,
    TArray<AActor*>& OutActors)
{
    if (!IsValid(Actor)) return;
    OutActors.AddUnique(Actor);
    if (!bIncludeAttachedActors) return;

    TArray<AActor*> AttachedActors;
    Actor->GetAttachedActors(AttachedActors, true, true);
    for (AActor* AttachedActor : AttachedActors)
    {
        if (IsValid(AttachedActor)) OutActors.AddUnique(AttachedActor);
    }
}
}

void OntoTwinCameraVisibility::ResolveHiddenActors(
    UWorld* World,
    ETwinCameraVisibilityProfile Profile,
    const TArray<FName>& InstanceActorNames,
    const TArray<FName>& InstanceLevelNames,
    TArray<AActor*>& OutActors)
{
    OutActors.Reset();
    if (!World) return;

    const UTwinCameraVisibilitySettings* Settings =
        GetDefault<UTwinCameraVisibilitySettings>();
    TArray<FName> ActorNames;
    TArray<FName> LevelNames;
    if (Settings)
    {
        if (Profile == ETwinCameraVisibilityProfile::Minimap)
        {
            AppendUniqueNames(ActorNames, Settings->MinimapHiddenActorNames);
            AppendUniqueNames(LevelNames, Settings->MinimapHiddenLevelNames);
        }
        else
        {
            AppendUniqueNames(ActorNames, Settings->GodViewHiddenActorNames);
            AppendUniqueNames(LevelNames, Settings->GodViewHiddenLevelNames);
        }
    }
    AppendUniqueNames(ActorNames, InstanceActorNames);
    AppendUniqueNames(LevelNames, InstanceLevelNames);

    const bool bIncludeAttachedActors = !Settings || Settings->bIncludeAttachedActors;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        if (!Actor) continue;
        const bool bActorMatch = MatchesActorName(Actor, ActorNames);
        const bool bLevelMatch = LevelNames.Contains(GetActorLevelShortName(Actor));
        if (bActorMatch || bLevelMatch)
        {
            AddActorAndAttachments(Actor, bIncludeAttachedActors, OutActors);
        }
    }
}

void OntoTwinCameraVisibility::SuppressLights(
    const TArray<AActor*>& Actors,
    TArray<TWeakObjectPtr<ULightComponent>>& OutSuppressedLights)
{
    for (AActor* Actor : Actors)
    {
        if (!IsValid(Actor)) continue;
        TInlineComponentArray<ULightComponent*> LightComponents;
        Actor->GetComponents(LightComponents);
        for (ULightComponent* LightComponent : LightComponents)
        {
            if (IsValid(LightComponent) && LightComponent->IsVisible())
            {
                OutSuppressedLights.AddUnique(LightComponent);
                LightComponent->SetVisibility(false);
            }
        }
    }
}

void OntoTwinCameraVisibility::RestoreLights(
    TArray<TWeakObjectPtr<ULightComponent>>& SuppressedLights)
{
    for (const TWeakObjectPtr<ULightComponent>& LightComponent : SuppressedLights)
    {
        if (LightComponent.IsValid()) LightComponent->SetVisibility(true);
    }
    SuppressedLights.Reset();
}

void OntoTwinCameraVisibility::ApplyToPlayer(
    APlayerController* PlayerController,
    const TArray<AActor*>& Actors,
    FTwinCameraVisibilityState& State)
{
    ClearFromPlayer(PlayerController, State);
    if (!PlayerController) return;

    for (AActor* Actor : Actors)
    {
        if (!IsValid(Actor)) continue;
        if (!PlayerController->HiddenActors.Contains(Actor))
        {
            PlayerController->HiddenActors.Add(Actor);
            State.AddedPlayerHiddenActors.Add(Actor);
        }

        // UE 5.6 ultimately renders a primitive-id exclusion set. Populate the
        // explicit component path as well as HiddenActors so attached/Nanite
        // meshes cannot escape through an actor container with no primitives.
        TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
        Actor->GetComponents(PrimitiveComponents);
        for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
        {
            if (!IsValid(PrimitiveComponent)
                || PlayerController->HiddenPrimitiveComponents.Contains(PrimitiveComponent))
            {
                continue;
            }
            PlayerController->HiddenPrimitiveComponents.Add(PrimitiveComponent);
            State.AddedPlayerHiddenPrimitiveComponents.Add(PrimitiveComponent);
        }
    }
    SuppressLights(Actors, State.SuppressedLights);
}

void OntoTwinCameraVisibility::ClearFromPlayer(
    APlayerController* PlayerController,
    FTwinCameraVisibilityState& State)
{
    if (PlayerController)
    {
        for (const TWeakObjectPtr<AActor>& Actor : State.AddedPlayerHiddenActors)
        {
            if (Actor.IsValid()) PlayerController->HiddenActors.Remove(Actor.Get());
        }
    }
    State.AddedPlayerHiddenActors.Reset();
    if (PlayerController)
    {
        for (const TWeakObjectPtr<UPrimitiveComponent>& PrimitiveComponent
            : State.AddedPlayerHiddenPrimitiveComponents)
        {
            if (PrimitiveComponent.IsValid())
            {
                PlayerController->HiddenPrimitiveComponents.Remove(PrimitiveComponent.Get());
            }
        }
    }
    State.AddedPlayerHiddenPrimitiveComponents.Reset();
    RestoreLights(State.SuppressedLights);
}
