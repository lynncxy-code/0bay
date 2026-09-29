#include "OfficeVR/OfficeVRGameMode.h"
#include "OfficeVR/OfficeInspectBinder.h"
#include "OfficeVR/OfficeAgentSlot.h"
#include "OfficeVR/OfficeVRPawn.h"
#include "OfficeVR/OfficeVRRenderer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "OfficeHost.h"

namespace
{
void StripOntoTwinActors(UWorld* World)
{
	if (!World)
	{
		return;
	}

	TArray<AActor*> ToDestroy;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}
		const FString ClassName = Actor->GetClass()->GetName();
		if (ClassName.Contains(TEXT("TwinSceneManager"))
			|| ClassName.Contains(TEXT("TwinGodViewAnchor"))
			|| ClassName.Contains(TEXT("TwinRoamingSpawnAnchor"))
			|| ClassName.Contains(TEXT("TwinMinimapAnchor"))
			|| ClassName.Contains(TEXT("TwinInteraction")))
		{
			ToDestroy.Add(Actor);
		}
	}

	for (AActor* Actor : ToDestroy)
	{
		UE_LOG(LogOfficeVR, Display, TEXT("Removed OntoTwin actor %s (%s) so VR stays first-person."),
			*Actor->GetName(), *Actor->GetClass()->GetName());
		Actor->Destroy();
	}
}
}

AOfficeVRGameMode::AOfficeVRGameMode()
{
	DefaultPawnClass = AOfficeVRPawn::StaticClass();
	PlayerControllerClass = APlayerController::StaticClass();
	HUDClass = nullptr;
}

void AOfficeVRGameMode::StartPlay()
{
	ApplyOfficeVRRendererTweaks();
	StripOntoTwinActors(GetWorld());
	UOfficeInspectBinder::EnsureWalkCollision(GetWorld());
	Super::StartPlay();
	if (UWorld* World = GetWorld())
	{
		UOfficeInspectBinder::BindExistingActors(World);
		UOfficeInspectBinder::EnsureDemoActors(World);
		AOfficeAgentSlot::EnsureSlots(World);
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (APawn* Pawn = PC->GetPawn())
			{
				PC->SetViewTarget(Pawn);
			}
		}
	}
}
