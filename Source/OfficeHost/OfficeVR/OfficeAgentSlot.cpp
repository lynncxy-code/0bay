#include "OfficeVR/OfficeAgentSlot.h"
#include "OfficeHost.h"
#include "Animation/AnimSequence.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundWaveProcedural.h"
#include "UObject/ConstructorHelpers.h"
#include "glTFRuntimeAsset.h"
#include "glTFRuntimeFunctionLibrary.h"

AOfficeAgentSlot::AOfficeAgentSlot()
{
	PrimaryActorTick.bCanEverTick = false;
	Tags.Add(TEXT("OfficeVRAgentSlot"));

	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	SetRootComponent(Capsule);
	Capsule->InitCapsuleSize(34.f, 88.f);
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
	Capsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Capsule->SetCanEverAffectNavigation(false);

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	BodyMesh->SetupAttachment(Capsule);
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyMesh->SetCanEverAffectNavigation(false);
	// Engine cylinder is 100cm, pivot at center, Z scale 1.05 -> 52.5cm half-height.
	// Sit the mesh bottom on the capsule bottom (z = -88).
	BodyMesh->SetRelativeLocation(FVector(0.f, 0.f, -35.5f));
	BodyMesh->SetRelativeScale3D(FVector(0.38f, 0.38f, 1.05f));

	HeadMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Head"));
	HeadMesh->SetupAttachment(Capsule);
	HeadMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HeadMesh->SetCanEverAffectNavigation(false);
	HeadMesh->SetRelativeLocation(FVector(0.f, 0.f, 70.f));
	HeadMesh->SetRelativeScale3D(FVector(0.42f, 0.42f, 0.42f));

	LabelText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	LabelText->SetupAttachment(Capsule);
	LabelText->SetRelativeLocation(FVector(0.f, 0.f, 128.f));
	LabelText->SetRelativeRotation(FRotator::ZeroRotator);
	LabelText->SetHorizontalAlignment(EHTA_Center);
	LabelText->SetWorldSize(18.f);
	LabelText->SetTextRenderColor(FColor(255, 210, 80));
	LabelText->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	SpeechBoard = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SpeechBoard"));
	SpeechBoard->SetupAttachment(Capsule);
	SpeechBoard->SetRelativeLocation(FVector(28.f, 0.f, 118.f));
	SpeechBoard->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));
	SpeechBoard->SetRelativeScale3D(FVector(1.35f, 0.03f, 0.72f));
	SpeechBoard->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SpeechBoard->SetCastShadow(false);
	SpeechBoard->SetHiddenInGame(true);
	SpeechBoard->SetVisibility(false);

	SpeechText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("SpeechText"));
	SpeechText->SetupAttachment(Capsule);
	SpeechText->SetRelativeLocation(FVector(32.f, 0.f, 142.f));
	SpeechText->SetRelativeRotation(FRotator::ZeroRotator);
	SpeechText->SetHorizontalAlignment(EHTA_Center);
	SpeechText->SetVerticalAlignment(EVRTA_TextTop);
	SpeechText->SetWorldSize(9.f);
	SpeechText->SetTextRenderColor(FColor(20, 24, 32));
	SpeechText->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SpeechText->SetVisibility(false);

	VoiceAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("VoiceAudio"));
	VoiceAudio->SetupAttachment(Capsule);
	VoiceAudio->SetAutoActivate(false);
	VoiceAudio->bAllowSpatialization = true;

	OutfitRoot = CreateDefaultSubobject<USceneComponent>(TEXT("OutfitRoot"));
	OutfitRoot->SetupAttachment(Capsule);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cylinder.Succeeded())
	{
		BodyMesh->SetStaticMesh(Cylinder.Object);
	}
	if (Sphere.Succeeded())
	{
		HeadMesh->SetStaticMesh(Sphere.Object);
	}
	if (CubeMesh.Succeeded())
	{
		SpeechBoard->SetStaticMesh(CubeMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Unlit(
		TEXT("/Engine/EngineDebugMaterials/DebugMeshMaterial.DebugMeshMaterial"));
	if (Unlit.Succeeded())
	{
		BodyMesh->SetMaterial(0, Unlit.Object);
		HeadMesh->SetMaterial(0, Unlit.Object);
		SpeechBoard->SetMaterial(0, Unlit.Object);
	}

	AgentId = TEXT("office.agent.0");
	DisplayLabel = TEXT("Agent 0");
}

void AOfficeAgentSlot::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyLabel();
}

void AOfficeAgentSlot::BeginPlay()
{
	Super::BeginPlay();
	ApplyLabel();
	ApplyFreeOutfit();
}

void AOfficeAgentSlot::ApplyLabel()
{
	if (LabelText)
	{
		LabelText->SetText(FText::FromString(DisplayLabel.IsEmpty() ? AgentId : DisplayLabel));
	}
#if WITH_EDITOR
	SetActorLabel(DisplayLabel.IsEmpty() ? AgentId : DisplayLabel);
#endif
}

namespace
{
	FBox CollectMeshBounds(USceneComponent* Root)
	{
		FBox Box(ForceInit);
		if (!Root)
		{
			return Box;
		}
		TArray<USceneComponent*> Parts;
		Root->GetChildrenComponents(false, Parts);
		for (USceneComponent* Child : Parts)
		{
			if (const USkeletalMeshComponent* Skinned = Cast<USkeletalMeshComponent>(Child))
			{
				if (const USkeletalMesh* Mesh = Skinned->GetSkeletalMeshAsset())
				{
					Box += Mesh->GetImportedBounds().GetBox();
				}
			}
			else if (const UStaticMeshComponent* StaticComp = Cast<UStaticMeshComponent>(Child))
			{
				if (const UStaticMesh* Mesh = StaticComp->GetStaticMesh())
				{
					Box += Mesh->GetBoundingBox();
				}
			}
		}
		return Box;
	}
}

void AOfficeAgentSlot::FitOutfitToCapsule()
{
	// Vertex bounds, not bones. These glTFs keep the skeleton a few centimeters upright while the
	// mesh itself lies along X. Phone-booth seated figure is the scale reference: 170cm standing.
	constexpr float TargetHeight = 170.f;
	if (!OutfitRoot || !Capsule)
	{
		return;
	}

	OutfitRoot->SetRelativeLocation(FVector::ZeroVector);
	OutfitRoot->SetRelativeRotation(FRotator::ZeroRotator);
	OutfitRoot->SetRelativeScale3D(FVector::OneVector);

	const FBox LocalBox = CollectMeshBounds(OutfitRoot);
	if (!LocalBox.IsValid)
	{
		UE_LOG(LogOfficeVR, Warning, TEXT("Agent %s outfit has no mesh bounds."), *AgentId);
		return;
	}

	const FVector Size = LocalBox.GetSize();
	const FVector Center = LocalBox.GetCenter();
	// Feet sit near the origin and the head is offset along Blender Z (UE X). Arm span can be
	// longer than that, so the body axis is the one farthest from the origin, not the longest.
	int32 Axis = 0;
	float BestOffset = FMath::Abs(Center.X);
	if (FMath::Abs(Center.Y) > BestOffset)
	{
		BestOffset = FMath::Abs(Center.Y);
		Axis = 1;
	}
	if (FMath::Abs(Center.Z) > BestOffset)
	{
		BestOffset = FMath::Abs(Center.Z);
		Axis = 2;
	}
	if (BestOffset < 0.05f)
	{
		Axis = 0;
		if (Size.Y >= Size.X && Size.Y >= Size.Z)
		{
			Axis = 1;
		}
		else if (Size.Z >= Size.X && Size.Z >= Size.Y)
		{
			Axis = 2;
		}
	}
	const float Length = Axis == 0 ? Size.X : (Axis == 1 ? Size.Y : Size.Z);
	const float CenterAlong = Axis == 0 ? Center.X : (Axis == 1 ? Center.Y : Center.Z);
	if (Length < 0.1f)
	{
		return;
	}

	FVector BodyAxis = FVector::ZeroVector;
	BodyAxis[Axis] = CenterAlong >= 0.f ? 1.f : -1.f;
	OutfitRoot->SetRelativeRotation(FQuat::FindBetweenNormals(BodyAxis, FVector::UpVector).Rotator());
	OutfitRoot->SetRelativeScale3D(FVector(TargetHeight / Length));

	const FBox Stood = LocalBox.TransformBy(OutfitRoot->GetRelativeTransform());
	const float FloorZ = -Capsule->GetUnscaledCapsuleHalfHeight();
	OutfitRoot->AddRelativeLocation(FVector(-Stood.GetCenter().X, -Stood.GetCenter().Y, FloorZ - Stood.Min.Z));
	UE_LOG(LogOfficeVR, Display, TEXT("Agent %s stood up on mesh axis %d: %.1fcm -> %.0fcm"), *AgentId, Axis, Length, TargetHeight);
}

void AOfficeAgentSlot::ApplyFreeOutfit()
{
	if (!OutfitRoot)
	{
		return;
	}

	TArray<USceneComponent*> OldParts;
	OutfitRoot->GetChildrenComponents(false, OldParts);
	for (USceneComponent* Child : OldParts)
	{
		if (Child)
		{
			Child->DestroyComponent();
		}
	}
	OutfitAnims.Reset();

	const FString FileName = AgentId.Contains(TEXT("office.agent.1"))
		? TEXT("OfficeAgents/Agent1_WomanDress.glb")
		: TEXT("OfficeAgents/Agent0_BusinessMan.glb");
	const FString IdleSuffix = AgentId.Contains(TEXT("office.agent.1")) ? TEXT("_Idle") : TEXT("|Idle");

	FglTFRuntimeConfig Config;
	OutfitAsset = UglTFRuntimeFunctionLibrary::glTFLoadAssetFromFilename(FileName, true, Config);
	if (!OutfitAsset)
	{
		UE_LOG(LogOfficeVR, Warning, TEXT("Agent outfit missing: %s"), *FileName);
		return;
	}

	int32 IdleIndex = INDEX_NONE;
	const TArray<FString> AnimNames = OutfitAsset->GetAnimationsNames();
	for (int32 Index = 0; Index < AnimNames.Num(); ++Index)
	{
		if (AnimNames[Index].EndsWith(IdleSuffix) && !AnimNames[Index].Contains(TEXT("Gun")))
		{
			IdleIndex = Index;
			break;
		}
	}

	int32 Attached = 0;
	for (const FglTFRuntimeNode& Node : OutfitAsset->GetNodes())
	{
		if (Node.MeshIndex < 0)
		{
			continue;
		}

		USkeletalMesh* Skinned = nullptr;
		if (Node.SkinIndex >= 0)
		{
			FglTFRuntimeSkeletalMeshConfig SkelConfig;
			Skinned = OutfitAsset->LoadSkeletalMesh(Node.MeshIndex, Node.SkinIndex, SkelConfig);
		}
		if (Skinned)
		{
			USkeletalMeshComponent* MeshComp = NewObject<USkeletalMeshComponent>(this);
			MeshComp->SetupAttachment(OutfitRoot);
			MeshComp->SetSkeletalMesh(Skinned);
			MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			MeshComp->SetCastShadow(true);
			MeshComp->RegisterComponent();
			if (IdleIndex != INDEX_NONE)
			{
				FglTFRuntimeSkeletalAnimationConfig AnimConfig;
				if (UAnimSequence* Anim = OutfitAsset->LoadSkeletalAnimation(Skinned, IdleIndex, AnimConfig))
				{
					OutfitAnims.Add(Anim);
					MeshComp->SetAnimationMode(EAnimationMode::AnimationSingleNode);
					MeshComp->PlayAnimation(Anim, true);
				}
			}
			++Attached;
			continue;
		}

		FglTFRuntimeStaticMeshConfig StaticConfig;
		if (UStaticMesh* StaticMesh = OutfitAsset->LoadStaticMesh(Node.MeshIndex, StaticConfig))
		{
			UStaticMeshComponent* MeshComp = NewObject<UStaticMeshComponent>(this);
			MeshComp->SetupAttachment(OutfitRoot);
			MeshComp->SetStaticMesh(StaticMesh);
			MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			MeshComp->SetCastShadow(true);
			MeshComp->RegisterComponent();
			++Attached;
		}
	}

	if (Attached <= 0)
	{
		UE_LOG(LogOfficeVR, Warning, TEXT("Agent outfit has no mesh: %s"), *FileName);
		return;
	}

	if (BodyMesh)
	{
		BodyMesh->SetVisibility(false);
	}
	if (HeadMesh)
	{
		HeadMesh->SetVisibility(false);
	}
	FitOutfitToCapsule();
	UE_LOG(LogOfficeVR, Display, TEXT("Agent %s outfit %s parts=%d"), *AgentId, *FileName, Attached);
}

void AOfficeAgentSlot::SetSpeech(const FString& Text)
{
	const bool bShow = !Text.IsEmpty();
	if (SpeechText)
	{
		SpeechText->SetText(FText::FromString(Text));
		SpeechText->SetVisibility(bShow);
	}
	if (SpeechBoard)
	{
		SpeechBoard->SetHiddenInGame(!bShow);
		SpeechBoard->SetVisibility(bShow);
	}
}

namespace
{
	bool ParsePcmWav(const TArray<uint8>& Bytes, int32& SampleRate, int32& Channels, TArray<uint8>& Pcm, FString& OutError)
	{
		SampleRate = 0;
		Channels = 0;
		Pcm.Reset();
		OutError.Reset();
		if (Bytes.Num() < 44)
		{
			OutError = TEXT("语音文件太短");
			return false;
		}
		if (!(Bytes[0] == 'R' && Bytes[1] == 'I' && Bytes[2] == 'F' && Bytes[3] == 'F'
			&& Bytes[8] == 'W' && Bytes[9] == 'A' && Bytes[10] == 'V' && Bytes[11] == 'E'))
		{
			OutError = TEXT("语音文件不是 WAV");
			return false;
		}
		int32 Offset = 12;
		int32 DataBytes = 0;
		bool bSawFmt = false;
		while (Offset + 8 <= Bytes.Num())
		{
			const uint32 ChunkId = Bytes[Offset] | (Bytes[Offset + 1] << 8) | (Bytes[Offset + 2] << 16) | (Bytes[Offset + 3] << 24);
			const int32 ChunkSize = Bytes[Offset + 4] | (Bytes[Offset + 5] << 8) | (Bytes[Offset + 6] << 16) | (Bytes[Offset + 7] << 24);
			Offset += 8;
			if (Offset + ChunkSize > Bytes.Num() && ChunkId != 0x61746164)
			{
				break;
			}
			if (ChunkId == 0x20746D66) // 'fmt '
			{
				if (ChunkSize < 16)
				{
					OutError = TEXT("语音格式头不完整");
					return false;
				}
				const int16 Format = Bytes[Offset] | (Bytes[Offset + 1] << 8);
				Channels = Bytes[Offset + 2] | (Bytes[Offset + 3] << 8);
				SampleRate = Bytes[Offset + 4] | (Bytes[Offset + 5] << 8) | (Bytes[Offset + 6] << 16) | (Bytes[Offset + 7] << 24);
				const int16 Bits = Bytes[Offset + 14] | (Bytes[Offset + 15] << 8);
				bSawFmt = true;
				if (Format != 1 || Bits != 16 || Channels < 1)
				{
					OutError = TEXT("语音不是 16-bit PCM，无法播放");
					return false;
				}
			}
			else if (ChunkId == 0x61746164) // 'data'
			{
				DataBytes = FMath::Clamp(ChunkSize, 0, Bytes.Num() - Offset);
				Pcm.Append(Bytes.GetData() + Offset, DataBytes);
				break;
			}
			Offset += FMath::Max(ChunkSize, 0);
			if (ChunkSize & 1)
			{
				++Offset;
			}
		}
		if (!bSawFmt)
		{
			OutError = TEXT("语音文件没有格式头");
			return false;
		}
		if (Pcm.Num() <= 0)
		{
			OutError = TEXT("语音文件没有音频数据");
			return false;
		}
		if (SampleRate <= 0 || Channels <= 0)
		{
			OutError = TEXT("语音采样率无效");
			return false;
		}
		return true;
	}
}

bool AOfficeAgentSlot::PlayWavBytes(const TArray<uint8>& WavBytes, FString& OutError)
{
	int32 SampleRate = 0;
	int32 Channels = 0;
	TArray<uint8> Pcm;
	if (!ParsePcmWav(WavBytes, SampleRate, Channels, Pcm, OutError))
	{
		return false;
	}
	if (!VoiceAudio)
	{
		OutError = TEXT("这个人没有音频组件");
		return false;
	}
	USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(this);
	Wave->SetSampleRate(SampleRate);
	Wave->NumChannels = Channels;
	Wave->SoundGroup = SOUNDGROUP_Voice;
	Wave->bLooping = false;
	Wave->Duration = static_cast<float>(Pcm.Num()) / static_cast<float>(FMath::Max(1, SampleRate * Channels * 2));
	Wave->QueueAudio(Pcm.GetData(), Pcm.Num());
	VoiceAudio->Stop();
	VoiceAudio->SetSound(Wave);
	VoiceAudio->Play();
	OutError.Reset();
	return true;
}

void AOfficeAgentSlot::SetHovered(bool bHovered)
{
	const int32 Stencil = bHovered ? 2 : 0;
	if (BodyMesh)
	{
		BodyMesh->SetRenderCustomDepth(bHovered);
		BodyMesh->SetCustomDepthStencilValue(Stencil);
	}
	if (HeadMesh)
	{
		HeadMesh->SetRenderCustomDepth(bHovered);
		HeadMesh->SetCustomDepthStencilValue(Stencil);
	}
}

namespace
{
	constexpr float CapsuleHalf = 88.f;

	UStaticMeshComponent* FindCompanyLogoMesh(UWorld* World)
	{
		UStaticMeshComponent* Best = nullptr;
		int32 BestScore = -1;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			TArray<UStaticMeshComponent*> Meshes;
			It->GetComponents<UStaticMeshComponent>(Meshes);
			for (UStaticMeshComponent* MeshComp : Meshes)
			{
				if (!MeshComp || !MeshComp->GetStaticMesh())
				{
					continue;
				}
				const FString MeshName = MeshComp->GetStaticMesh()->GetName();
				int32 Score = -1;
				if (MeshName.Contains(TEXT("CompanyLogo_GoldCurved")))
				{
					Score = 4;
				}
				else if (MeshName.Contains(TEXT("CompanyLogo")))
				{
					Score = 3;
				}
				if (Score > BestScore)
				{
					BestScore = Score;
					Best = MeshComp;
				}
			}
		}
		return Best;
	}

	FVector PlayerStartLocation(UWorld* World, const FVector& Fallback)
	{
		for (TActorIterator<APlayerStart> It(World); It; ++It)
		{
			return It->GetActorLocation();
		}
		return Fallback;
	}
}

FVector AOfficeAgentSlot::SnapToFloor(UWorld* World, const FVector& Guess)
{
	if (!World)
	{
		return Guess;
	}

	const FVector PlayerLoc = PlayerStartLocation(World, Guess);
	const float StandZ = PlayerLoc.Z;
	FVector Probe(Guess.X, Guess.Y, StandZ);

	auto IgnoreAgents = [World](FCollisionQueryParams& Params)
	{
		for (TActorIterator<AOfficeAgentSlot> It(World); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}
	};

	for (int32 Attempt = 0; Attempt < 8; ++Attempt)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(OfficeAgentFloor), true);
		IgnoreAgents(Params);
		const FVector Start(Probe.X, Probe.Y, StandZ + 400.f);
		const FVector End(Probe.X, Probe.Y, StandZ - 600.f);
		const bool bHit = World->LineTraceSingleByObjectType(
			                  Hit, Start, End, FCollisionObjectQueryParams(ECC_WorldStatic), Params)
			|| World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
		if (bHit && Hit.bBlockingHit)
		{
			if (FMath::Abs(Hit.ImpactNormal.Z) >= 0.55f)
			{
				return Hit.ImpactPoint + FVector(0.f, 0.f, CapsuleHalf);
			}
			const FVector Nudge = FVector(Hit.ImpactNormal.X, Hit.ImpactNormal.Y, 0.f).GetSafeNormal();
			Probe += (Nudge.IsNearlyZero() ? FVector(35.f, 0.f, 0.f) : Nudge * 35.f);
			continue;
		}
		break;
	}

	return FVector(Probe.X, Probe.Y, StandZ);
}

void AOfficeAgentSlot::EnsureSlots(UWorld* World)
{
	if (!World)
	{
		return;
	}

	TArray<AOfficeAgentSlot*> Existing;
	for (TActorIterator<AOfficeAgentSlot> It(World); It; ++It)
	{
		Existing.Add(*It);
	}
	// Positions accepted on the two lobby speakers. The new Office_Demo does not
	// store these slots; they are created on top of that scene at play.
	const FVector SpeakerSpots[2] = {
		FVector(3120.f, -1200.f, 4408.f),
		FVector(4180.f, -1200.f, 4408.f)
	};
	FVector Outward = FVector(0.f, -1.f, 0.f);
	if (UStaticMeshComponent* Logo = FindCompanyLogoMesh(World))
	{
		const FVector Extent = Logo->Bounds.BoxExtent;
		Outward = Extent.X >= Extent.Y ? FVector(0.f, 1.f, 0.f) : FVector(1.f, 0.f, 0.f);
		FVector FromWall = (SpeakerSpots[0] + SpeakerSpots[1]) * 0.5f - Logo->Bounds.Origin;
		FromWall.Z = 0.f;
		if (FVector::DotProduct(FromWall, Outward) > 0.f)
		{
			Outward *= -1.f;
		}
	}
	FRotator Face = Outward.Rotation();
	Face.Pitch = 0.f;
	Face.Roll = 0.f;
	const FString Source = TEXT("lobby speakers");
	const TCHAR* Ids[2] = { TEXT("office.agent.0"), TEXT("office.agent.1") };
	const TCHAR* Labels[2] = { TEXT("Agent 0"), TEXT("Agent 1") };

	int32 Changed = 0;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		// Keep the accepted lobby coordinates. Floor traces were lifting the second person onto the wall.
		const FVector Location = SpeakerSpots[Index];

		AOfficeAgentSlot* Slot = nullptr;
		for (AOfficeAgentSlot* Candidate : Existing)
		{
			if (Candidate && Candidate->AgentId == Ids[Index])
			{
				Slot = Candidate;
				break;
			}
		}
		if (!Slot && Existing.IsValidIndex(Index))
		{
			Slot = Existing[Index];
		}

		if (Slot)
		{
			Slot->AgentId = Ids[Index];
			Slot->DisplayLabel = Labels[Index];
			Slot->SetActorLocation(Location);
			Slot->SetActorRotation(Face);
			Slot->ApplyLabel();
			Slot->ApplyFreeOutfit();
		}
		else
		{
			// Set the id before BeginPlay so each person loads their own skin.
			// This only adds actors. It does not move the saved scene.
			const FTransform SpawnTM(Face, Location);
			Slot = World->SpawnActorDeferred<AOfficeAgentSlot>(
				AOfficeAgentSlot::StaticClass(),
				SpawnTM,
				nullptr,
				nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Slot)
			{
				UE_LOG(LogOfficeVR, Warning, TEXT("Failed to spawn agent slot %s"), Ids[Index]);
				continue;
			}
			Slot->AgentId = Ids[Index];
			Slot->DisplayLabel = Labels[Index];
			Slot->FinishSpawning(SpawnTM);
			Existing.Add(Slot);
		}
		++Changed;
		UE_LOG(LogOfficeVR, Display, TEXT("Agent slot %s at %s (anchor %s)"), *Slot->AgentId, *Location.ToCompactString(), *Source);
	}

	if (GEngine && Changed > 0)
	{
		GEngine->AddOnScreenDebugMessage(-1, 12.f, FColor::Yellow,
			FString::Printf(TEXT("OfficeVR: %d agent(s) on lobby speakers (%s)."), Changed, *Source));
	}
}
