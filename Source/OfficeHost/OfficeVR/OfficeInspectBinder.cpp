#include "OfficeVR/OfficeInspectBinder.h"
#include "OfficeHost.h"
#include "OfficeVR/InspectableComponent.h"
#include "OfficeVR/InspectCatalog.h"
#include "OfficeVR/OfficeAgentSlot.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "UObject/ConstructorHelpers.h"

namespace OfficeVRBindPrivate
{
	bool IsArchitectureName(const FString& Name)
	{
		static const TCHAR* Skip[] = {
			TEXT("SM_Wall"), TEXT("SM_Floor"), TEXT("SM_Ceiling"), TEXT("SM_Column"),
			TEXT("SM_Facade"), TEXT("SM_Roof"), TEXT("SM_Cove"), TEXT("SM_Skirting"),
			TEXT("SM_Core"), TEXT("SM_Door"), TEXT("SM_Interior"), TEXT("SM_Exterior"),
			TEXT("SM_Partition"), TEXT("SM_Feature"), TEXT("Sky"), TEXT("Landscape"),
			TEXT("BP_OfficeShell"), TEXT("WorldDataLayers"), TEXT("Brush"),
			TEXT("Light"), TEXT("Camera"), TEXT("PlayerStart"), TEXT("SkyAtmosphere")
		};
		for (const TCHAR* Token : Skip)
		{
			if (Name.Contains(Token))
			{
				return true;
			}
		}
		return false;
	}

	FString CollectNames(AActor* Actor)
	{
		FString Combined = Actor->GetName() + TEXT(" ") + Actor->GetActorNameOrLabel();
		TArray<UStaticMeshComponent*> Meshes;
		Actor->GetComponents<UStaticMeshComponent>(Meshes);
		for (UStaticMeshComponent* Mesh : Meshes)
		{
			if (!Mesh)
			{
				continue;
			}
			Combined += TEXT(" ");
			Combined += Mesh->GetName();
			if (UStaticMesh* Asset = Mesh->GetStaticMesh())
			{
				Combined += TEXT(" ");
				Combined += Asset->GetName();
				Combined += TEXT(" ");
				Combined += Asset->GetPathName();
			}
		}
		return Combined;
	}

	void EnableTraceCollision(AActor* Actor)
	{
		TArray<UStaticMeshComponent*> Meshes;
		Actor->GetComponents<UStaticMeshComponent>(Meshes);
		for (UStaticMeshComponent* Mesh : Meshes)
		{
			if (!Mesh)
			{
				continue;
			}
			if (Mesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
			{
				Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			}
			Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		}
	}

	UInspectableComponent* AddItem(AActor* Actor, const FInspectableItemData& Item)
	{
		UInspectableComponent* Existing = Actor->FindComponentByClass<UInspectableComponent>();
		if (Existing)
		{
			Existing->Item = Item;
			return Existing;
		}

		UInspectableComponent* Comp = NewObject<UInspectableComponent>(Actor, TEXT("Inspectable"));
		Comp->Item = Item;
		Actor->AddInstanceComponent(Comp);
		Comp->RegisterComponent();
		EnableTraceCollision(Actor);
		return Comp;
	}
}

int32 UOfficeInspectBinder::BindExistingActors(UWorld* World)
{
	if (!World)
	{
		return 0;
	}

	int32 Bound = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || Actor->IsA<APlayerStart>() || Actor->IsA<AOfficeAgentSlot>() || Actor->bIsEditorOnlyActor)
		{
			continue;
		}

		const FString Combined = OfficeVRBindPrivate::CollectNames(Actor);
		if (OfficeVRBindPrivate::IsArchitectureName(Combined))
		{
			continue;
		}

		FInspectableItemData Item;
		if (!UInspectCatalog::FindForName(Combined, Item))
		{
			continue;
		}

		OfficeVRBindPrivate::AddItem(Actor, Item);
		++Bound;
	}

	UE_LOG(LogOfficeVR, Display, TEXT("Bound %d inspectable actors from scene furniture."), Bound);
	return Bound;
}

void UOfficeInspectBinder::EnsureDemoActors(UWorld* World)
{
	if (!World)
	{
		return;
	}

	FVector Origin = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		Origin = It->GetActorLocation();
		Rotation = It->GetActorRotation();
		break;
	}

	const FVector Forward = Rotation.Vector();
	const FVector Right = FRotationMatrix(Rotation).GetUnitAxis(EAxis::Y);

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Cube)
	{
		UE_LOG(LogOfficeVR, Warning, TEXT("Could not load engine cube for demo inspectables."));
		return;
	}

	int32 Existing = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->FindComponentByClass<UInspectableComponent>())
		{
			++Existing;
		}
	}

	const bool bNeedSamples = Existing < 5;
	TArray<FInspectableItemData> DemoItems;
	if (bNeedSamples)
	{
		DemoItems = UInspectCatalog::MakeGateDemoItems();
	}
	DemoItems.Insert(UInspectCatalog::MakeHelpPlaque(), 0);

	for (int32 Index = 0; Index < DemoItems.Num(); ++Index)
	{
		if (!bNeedSamples && Index > 0)
		{
			break;
		}

		const FVector Offset = Forward * 220.f + Right * ((Index - 1) * 90.f) + FVector(0.f, 0.f, 45.f);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Origin + Offset, Rotation, Params);
		if (!Actor)
		{
			continue;
		}

		Actor->Tags.Add(TEXT("OfficeVRDemo"));
#if WITH_EDITOR
		Actor->SetActorLabel(DemoItems[Index].DisplayName);
#endif
		if (UStaticMeshComponent* Mesh = Actor->GetStaticMeshComponent())
		{
			Mesh->SetStaticMesh(Cube);
			Mesh->SetWorldScale3D(Index == 0 ? FVector(0.8f, 0.08f, 0.5f) : FVector(0.45f, 0.45f, 0.45f));
			Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		}
		OfficeVRBindPrivate::AddItem(Actor, DemoItems[Index]);
	}

	UE_LOG(LogOfficeVR, Display, TEXT("EnsureDemoActors done. Existing inspectables before spawn: %d"), Existing);
}

void UOfficeInspectBinder::EnsureWalkCollision(UWorld* World)
{
	if (!World)
	{
		return;
	}

	int32 Fixed = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		TArray<UStaticMeshComponent*> Meshes;
		It->GetComponents<UStaticMeshComponent>(Meshes);
		for (UStaticMeshComponent* Mesh : Meshes)
		{
			if (!Mesh || !Mesh->GetStaticMesh())
			{
				continue;
			}
			const FString Name = Mesh->GetStaticMesh()->GetName();
			if (!Name.Contains(TEXT("SM_Floor")) && Name != TEXT("Floor") && !Name.EndsWith(TEXT("_Floor")))
			{
				continue;
			}
			const float Top = Mesh->Bounds.Origin.Z + Mesh->Bounds.BoxExtent.Z;
			if (Top < 3000.f || Top > 5200.f)
			{
				continue;
			}
			Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Mesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
			++Fixed;
			UE_LOG(LogOfficeVR, Display, TEXT("Walk collision on %s top=%.1f"), *Name, Top);
		}
	}
	UE_LOG(LogOfficeVR, Display, TEXT("EnsureWalkCollision: %d office floor mesh(es)."), Fixed);
}
