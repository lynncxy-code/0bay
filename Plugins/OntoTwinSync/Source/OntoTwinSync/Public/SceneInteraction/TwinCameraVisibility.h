#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "TwinCameraVisibility.generated.h"

class AActor;
class APlayerController;
class ULightComponent;
class UPrimitiveComponent;
class UWorld;

/** Project-wide exclusions shared by the minimap capture and roaming god view. */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="OntoTwin Camera Visibility"))
class ONTOTWINSYNC_API UTwinCameraVisibilitySettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    /** Actor object names or editor labels hidden only from the minimap capture. */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Minimap")
    TArray<FName> MinimapHiddenActorNames;

    /** Short streamed-level names whose actors are hidden only from the minimap capture. */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Minimap")
    TArray<FName> MinimapHiddenLevelNames;

    /** Actor object names or editor labels hidden only while god/startup view is active. */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="God View")
    TArray<FName> GodViewHiddenActorNames;

    /** Short streamed-level names whose actors are hidden only while god/startup view is active. */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="God View")
    TArray<FName> GodViewHiddenLevelNames;

    /** Include actors attached below a matched actor, such as an Outliner folder-style hierarchy. */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Matching")
    bool bIncludeAttachedActors = true;
};

enum class ETwinCameraVisibilityProfile : uint8
{
    Minimap,
    GodView
};

/** Everything changed for a live player view, so it can be restored without touching other systems. */
struct ONTOTWINSYNC_API FTwinCameraVisibilityState
{
    TArray<TWeakObjectPtr<AActor>> AddedPlayerHiddenActors;
    TArray<TWeakObjectPtr<UPrimitiveComponent>> AddedPlayerHiddenPrimitiveComponents;
    TArray<TWeakObjectPtr<ULightComponent>> SuppressedLights;
};

namespace OntoTwinCameraVisibility
{
    ONTOTWINSYNC_API void ResolveHiddenActors(
        UWorld* World,
        ETwinCameraVisibilityProfile Profile,
        const TArray<FName>& InstanceActorNames,
        const TArray<FName>& InstanceLevelNames,
        TArray<AActor*>& OutActors);

    ONTOTWINSYNC_API void SuppressLights(
        const TArray<AActor*>& Actors,
        TArray<TWeakObjectPtr<ULightComponent>>& OutSuppressedLights);

    ONTOTWINSYNC_API void RestoreLights(
        TArray<TWeakObjectPtr<ULightComponent>>& SuppressedLights);

    ONTOTWINSYNC_API void ApplyToPlayer(
        APlayerController* PlayerController,
        const TArray<AActor*>& Actors,
        FTwinCameraVisibilityState& State);

    ONTOTWINSYNC_API void ClearFromPlayer(
        APlayerController* PlayerController,
        FTwinCameraVisibilityState& State);
}
