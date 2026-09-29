#include "OfficeVR/OfficeVRPawn.h"
#include "OfficeHost.h"
#include "OfficeVR/InspectableComponent.h"
#include "OfficeVR/OfficeAgentAdapter.h"
#include "OfficeVR/OfficeAgentSlot.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "IHeadMountedDisplay.h"
#include "IMotionController.h"
#include "IXRTrackingSystem.h"
#include "Materials/MaterialInterface.h"
#include "MotionControllerComponent.h"
#include "OfficeVR/OfficeVRRenderer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

AOfficeVRPawn::AOfficeVRPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	AutoPossessPlayer = EAutoReceiveInput::Player0;
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	GetCapsuleComponent()->SetCapsuleSize(34.f, 92.f);
	GetMesh()->SetHiddenInGame(true);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->MaxWalkSpeed = WalkSpeed;
		Move->BrakingDecelerationWalking = 2048.f;
		Move->bRequestedMoveUseAcceleration = false;
		Move->GravityScale = 1.f;
		Move->SetWalkableFloorZ(0.7f);
	}

	VROrigin = CreateDefaultSubobject<USceneComponent>(TEXT("VROrigin"));
	VROrigin->SetupAttachment(RootComponent);
	VROrigin->SetRelativeLocation(FVector(0.f, 0.f, -92.f));

	VRCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("VRCamera"));
	VRCamera->SetupAttachment(VROrigin);
	VRCamera->bLockToHmd = true;
	VRCamera->bUsePawnControlRotation = false;

	LeftController = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("LeftController"));
	LeftController->SetupAttachment(VROrigin);
	LeftController->SetTrackingMotionSource(IMotionController::LeftHandSourceId);

	RightController = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("RightController"));
	RightController->SetupAttachment(VROrigin);
	RightController->SetTrackingMotionSource(IMotionController::RightHandSourceId);

	RightAim = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("RightAim"));
	RightAim->SetupAttachment(VROrigin);
	RightAim->SetTrackingMotionSource(FName(TEXT("RightAim")));

	LaserMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Laser"));
	LaserMesh->SetupAttachment(VROrigin);
	LaserMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LaserMesh->SetCanEverAffectNavigation(false);
	LaserMesh->SetCastShadow(false);
	LaserMesh->SetAbsolute(true, true, true);

	TeleportMarker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TeleportMarker"));
	TeleportMarker->SetupAttachment(RootComponent);
	TeleportMarker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TeleportMarker->SetHiddenInGame(true);
	TeleportMarker->SetCanEverAffectNavigation(false);

	PanelRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PanelRoot"));
	PanelRoot->SetupAttachment(VRCamera);
	PanelRoot->SetRelativeLocation(FVector(90.f, 18.f, -8.f));
	PanelRoot->SetRelativeRotation(FRotator(0.f, 180.f, 0.f));

	DialogBoard = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DialogBoard"));
	DialogBoard->SetupAttachment(PanelRoot);
	DialogBoard->SetRelativeLocation(FVector(-2.f, 22.f, -14.f));
	DialogBoard->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));
	DialogBoard->SetRelativeScale3D(FVector(0.95f, 0.02f, 0.62f));
	DialogBoard->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DialogBoard->SetCastShadow(false);
	DialogBoard->SetCanEverAffectNavigation(false);
	DialogBoard->SetVisibility(false);
	DialogBoard->SetHiddenInGame(true);

	PanelText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("PanelText"));
	PanelText->SetupAttachment(PanelRoot);
	PanelText->SetHorizontalAlignment(EHTA_Left);
	PanelText->SetVerticalAlignment(EVRTA_TextTop);
	PanelText->SetWorldSize(4.4f);
	PanelText->SetTextRenderColor(FColor(20, 24, 32));
	PanelText->SetVisibility(false);
	PanelText->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cube.Succeeded())
	{
		LaserMesh->SetStaticMesh(Cube.Object);
		DialogBoard->SetStaticMesh(Cube.Object);
	}
	if (Cylinder.Succeeded())
	{
		TeleportMarker->SetStaticMesh(Cylinder.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Unlit(
		TEXT("/Engine/EngineDebugMaterials/DebugMeshMaterial.DebugMeshMaterial"));
	if (Unlit.Succeeded())
	{
		LaserMesh->SetMaterial(0, Unlit.Object);
		DialogBoard->SetMaterial(0, Unlit.Object);
	}
}

void AOfficeVRPawn::ApplyVRRendererTweaks()
{
	ApplyOfficeVRRendererTweaks();
}

void AOfficeVRPawn::BeginPlay()
{
	Super::BeginPlay();
	ApplyVRRendererTweaks();

	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->MaxWalkSpeed = WalkSpeed;
	}

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->bShowMouseCursor = false;
		PC->SetViewTarget(this);
		PC->SetAudioListenerOverride(VRCamera, FVector::ZeroVector, FRotator::ZeroRotator);
	}

	HidePanel();
	HideMotionControllerVisuals();
}

void AOfficeVRPawn::HideMotionControllerVisuals()
{
	auto HideViz = [](USceneComponent* Root)
	{
		if (!Root)
		{
			return;
		}
		TArray<USceneComponent*> Children;
		Root->GetChildrenComponents(true, Children);
		for (USceneComponent* Child : Children)
		{
			if (!Child)
			{
				continue;
			}
			const FString ClassName = Child->GetClass()->GetName();
			if (ClassName.Contains(TEXT("Visualization")) || ClassName.Contains(TEXT("XRDevice")))
			{
				Child->SetHiddenInGame(true, true);
				Child->SetVisibility(false, true);
			}
		}
	};
	HideViz(LeftController);
	HideViz(RightController);
	HideViz(RightAim);
}

void AOfficeVRPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	PlayerInputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AOfficeVRPawn::OnInspectPressed);
	PlayerInputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &AOfficeVRPawn::OnClosePanel);
	PlayerInputComponent->BindKey(EKeys::B, IE_Pressed, this, &AOfficeVRPawn::OnClosePanel);
	PlayerInputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AOfficeVRPawn::OnClosePanel);
	PlayerInputComponent->BindKey(EKeys::OculusTouch_Right_Trigger_Click, IE_Pressed, this, &AOfficeVRPawn::OnInspectPressed);
	PlayerInputComponent->BindKey(EKeys::ValveIndex_Right_Trigger_Click, IE_Pressed, this, &AOfficeVRPawn::OnInspectPressed);
	PlayerInputComponent->BindKey(EKeys::MixedReality_Right_Trigger_Click, IE_Pressed, this, &AOfficeVRPawn::OnInspectPressed);
	PlayerInputComponent->BindKey(EKeys::OculusTouch_Right_B_Click, IE_Pressed, this, &AOfficeVRPawn::OnClosePanel);
	PlayerInputComponent->BindKey(EKeys::Gamepad_FaceButton_Right, IE_Pressed, this, &AOfficeVRPawn::OnClosePanel);
	PlayerInputComponent->BindKey(EKeys::Y, IE_Pressed, this, &AOfficeVRPawn::OnTalkPressed);
	PlayerInputComponent->BindKey(EKeys::Y, IE_Released, this, &AOfficeVRPawn::OnTalkReleased);
	PlayerInputComponent->BindKey(EKeys::OculusTouch_Left_Y_Click, IE_Pressed, this, &AOfficeVRPawn::OnTalkPressed);
	PlayerInputComponent->BindKey(EKeys::OculusTouch_Left_Y_Click, IE_Released, this, &AOfficeVRPawn::OnTalkReleased);
	PlayerInputComponent->BindKey(EKeys::Gamepad_FaceButton_Top, IE_Pressed, this, &AOfficeVRPawn::OnTalkPressed);
	PlayerInputComponent->BindKey(EKeys::Gamepad_FaceButton_Top, IE_Released, this, &AOfficeVRPawn::OnTalkReleased);
}

float AOfficeVRPawn::ReadBestAxis(const TArray<FKey>& Keys) const
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		return 0.f;
	}

	float Best = 0.f;
	for (const FKey& Key : Keys)
	{
		const float Value = PC->GetInputAnalogKeyState(Key);
		if (FMath::Abs(Value) > FMath::Abs(Best))
		{
			Best = Value;
		}
	}
	return Best;
}

bool AOfficeVRPawn::IsAnyKeyDown(const TArray<FKey>& Keys) const
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		return false;
	}
	for (const FKey& Key : Keys)
	{
		if (PC->IsInputKeyDown(Key))
		{
			return true;
		}
	}
	return false;
}

FTransform AOfficeVRPawn::GetAimTransform() const
{
	const bool bHeadTracking = GEngine && GEngine->XRSystem.IsValid() && GEngine->XRSystem->IsHeadTrackingAllowed();
	if (bHeadTracking)
	{
		if (RightAim && RightAim->IsTracked())
		{
			return RightAim->GetComponentTransform();
		}
		if (RightController && RightController->IsTracked())
		{
			return RightController->GetComponentTransform();
		}
	}
	if (VRCamera)
	{
		return VRCamera->GetComponentTransform();
	}
	return GetActorTransform();
}

void AOfficeVRPawn::ClearHover()
{
	if (UInspectableComponent* Old = Hovered.Get())
	{
		Old->SetHighlighted(false);
	}
	Hovered = nullptr;
	if (AOfficeAgentSlot* OldAgent = HoveredAgent.Get())
	{
		OldAgent->SetHovered(false);
	}
	HoveredAgent = nullptr;
}

void AOfficeVRPawn::UpdateLaserAndHover()
{
	const FTransform Aim = GetAimTransform();
	const FVector Start = Aim.GetLocation();
	const FVector Dir = Aim.GetRotation().GetForwardVector();
	const FVector End = Start + Dir * LaserMaxDistance;

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OfficeVRLaser), true, this);
	const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
	const float Distance = FMath::Max(1.f, bHit ? Hit.Distance : LaserMaxDistance);

	if (LaserMesh)
	{
		LaserMesh->SetVisibility(true);
		LaserMesh->SetWorldLocation(Start + Dir * (Distance * 0.5f));
		LaserMesh->SetWorldRotation(Dir.Rotation());
		LaserMesh->SetWorldScale3D(FVector(Distance / 100.f, 0.008f, 0.008f));
	}

	UInspectableComponent* NewHover = nullptr;
	AOfficeAgentSlot* NewAgent = nullptr;
	if (bHit && Hit.GetActor())
	{
		NewAgent = Cast<AOfficeAgentSlot>(Hit.GetActor());
		if (!NewAgent)
		{
			NewHover = Hit.GetActor()->FindComponentByClass<UInspectableComponent>();
		}
	}

	if (Hovered.Get() != NewHover || HoveredAgent.Get() != NewAgent)
	{
		ClearHover();
		if (NewHover)
		{
			NewHover->SetHighlighted(true);
		}
		if (NewAgent)
		{
			NewAgent->SetHovered(true);
		}
		Hovered = NewHover;
		HoveredAgent = NewAgent;
	}

	if (LaserMesh)
	{
		FVector Color(0.85f, 0.92f, 1.f);
		if (NewHover)
		{
			Color = FVector(0.2f, 1.f, 0.4f);
		}
		else if (NewAgent)
		{
			Color = FVector(1.f, 0.55f, 0.15f);
		}
		LaserMesh->SetVectorParameterValueOnMaterials(TEXT("Color"), Color);
	}

	bTeleportValid = false;
	if (bTeleportAiming)
	{
		FHitResult FloorHit = Hit;
		if (!bHit || FloorHit.ImpactNormal.Z < 0.55f)
		{
			const FVector Probe = bHit ? Hit.ImpactPoint + FVector(0.f, 0.f, 40.f) : End;
			GetWorld()->LineTraceSingleByChannel(FloorHit, Probe + FVector(0.f, 0.f, 80.f), Probe - FVector(0.f, 0.f, 400.f), ECC_Visibility, Params);
		}
		if (FloorHit.bBlockingHit && FloorHit.ImpactNormal.Z >= 0.55f)
		{
			bTeleportValid = true;
			TeleportDest = FloorHit.ImpactPoint;
		}
	}

	if (TeleportMarker)
	{
		TeleportMarker->SetHiddenInGame(!(bTeleportAiming && bTeleportValid));
		if (bTeleportValid)
		{
			TeleportMarker->SetWorldLocation(TeleportDest + FVector(0.f, 0.f, 8.f));
			TeleportMarker->SetWorldScale3D(FVector(0.45f, 0.45f, 0.04f));
		}
	}
}

void AOfficeVRPawn::UpdateLocomotion(float DeltaSeconds)
{
	float MoveX = ReadBestAxis({
		EKeys::OculusTouch_Left_Thumbstick_X,
		EKeys::ValveIndex_Left_Thumbstick_X,
		EKeys::MixedReality_Left_Thumbstick_X,
		EKeys::Gamepad_LeftX
	});
	float MoveY = ReadBestAxis({
		EKeys::OculusTouch_Left_Thumbstick_Y,
		EKeys::ValveIndex_Left_Thumbstick_Y,
		EKeys::MixedReality_Left_Thumbstick_Y,
		EKeys::Gamepad_LeftY
	});

	if (IsAnyKeyDown({EKeys::W, EKeys::Up})) { MoveY += 1.f; }
	if (IsAnyKeyDown({EKeys::S, EKeys::Down})) { MoveY -= 1.f; }
	if (IsAnyKeyDown({EKeys::D, EKeys::Right})) { MoveX += 1.f; }
	if (IsAnyKeyDown({EKeys::A, EKeys::Left})) { MoveX -= 1.f; }
	MoveX = FMath::Clamp(MoveX, -1.f, 1.f);
	MoveY = FMath::Clamp(MoveY, -1.f, 1.f);

	const FRotator BodyYaw(0.f, GetActorRotation().Yaw, 0.f);
	const FVector Forward = FRotationMatrix(BodyYaw).GetUnitAxis(EAxis::X);
	const FVector Right = FRotationMatrix(BodyYaw).GetUnitAxis(EAxis::Y);
	AddMovementInput(Forward, MoveY);
	AddMovementInput(Right, MoveX);

	float TurnX = ReadBestAxis({
		EKeys::OculusTouch_Right_Thumbstick_X,
		EKeys::ValveIndex_Right_Thumbstick_X,
		EKeys::MixedReality_Right_Thumbstick_X,
		EKeys::Gamepad_RightX
	});
	if (IsAnyKeyDown({EKeys::E})) { TurnX = 1.f; }
	if (IsAnyKeyDown({EKeys::Q})) { TurnX = -1.f; }

	if (FMath::Abs(TurnX) > TurnStickDeadzone)
	{
		const float Range = 1.f - TurnStickDeadzone;
		const float Scaled = FMath::Clamp((FMath::Abs(TurnX) - TurnStickDeadzone) / Range, 0.f, 1.f);
		AddActorWorldRotation(FRotator(0.f, FMath::Sign(TurnX) * Scaled * SmoothTurnRate * DeltaSeconds, 0.f));
	}

	const float TeleportAxis = ReadBestAxis({
		EKeys::OculusTouch_Right_Thumbstick_Y,
		EKeys::ValveIndex_Right_Thumbstick_Y,
		EKeys::MixedReality_Right_Thumbstick_Y,
		EKeys::Gamepad_RightY
	});
	const bool bKeyTeleport = IsAnyKeyDown({EKeys::T, EKeys::Gamepad_FaceButton_Bottom});
	const bool bWantTeleport = TeleportAxis < -0.55f || bKeyTeleport;
	if (bTeleportAiming && !bWantTeleport)
	{
		TryTeleport();
	}
	bTeleportAiming = bWantTeleport;
}

void AOfficeVRPawn::TryTeleport()
{
	if (!bTeleportValid)
	{
		return;
	}

	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	FVector Dest = TeleportDest + FVector(0.f, 0.f, HalfHeight + 2.f);
	GetCharacterMovement()->StopMovementImmediately();
	TeleportTo(Dest, GetActorRotation());
}

void AOfficeVRPawn::SetDialogVisible(bool bVisible)
{
	if (PanelText)
	{
		PanelText->SetVisibility(bVisible);
	}
	if (DialogBoard)
	{
		DialogBoard->SetVisibility(bVisible);
		DialogBoard->SetHiddenInGame(!bVisible);
	}
}

void AOfficeVRPawn::ShowPanel(UInspectableComponent* Inspectable)
{
	if (!Inspectable || !PanelText)
	{
		return;
	}
	Opened = Inspectable;
	SetDialogVisible(true);
	PanelText->SetText(FText::FromString(Inspectable->GetPanelText()));
}

void AOfficeVRPawn::HidePanel()
{
	Opened = nullptr;
	SetDialogVisible(false);
}

AOfficeAgentSlot* AOfficeVRPawn::FindAgentSlot(const FString& AgentId) const
{
	UWorld* World = GetWorld();
	if (!World || AgentId.IsEmpty())
	{
		return nullptr;
	}
	for (TActorIterator<AOfficeAgentSlot> It(World); It; ++It)
	{
		if (*It && It->AgentId == AgentId)
		{
			return *It;
		}
	}
	return nullptr;
}

void AOfficeVRPawn::PushAgentSpeech(const FString& Text)
{
	if (AOfficeAgentSlot* Slot = FindAgentSlot(ActiveAgentId))
	{
		Slot->SetSpeech(Text);
	}
}

void AOfficeVRPawn::NoteTalk(const FString& Line)
{
	if (Line.IsEmpty())
	{
		return;
	}
	TalkLog += TEXT("\n") + Line;
	ShowTalkPanel(TalkLog);
}

void AOfficeVRPawn::PlayAgentTts()
{
	const FString AgentId = ActiveAgentId;
	TWeakObjectPtr<AOfficeVRPawn> WeakThis(this);
	FOfficeAgentAdapter::Get().FetchTtsWav(FOfficeAgentBinaryDone::CreateLambda(
		[WeakThis, AgentId](bool bOk, TArray<uint8> Bytes)
		{
			AOfficeVRPawn* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}
			if (!bOk || Bytes.Num() < 44)
			{
				Self->NoteTalk(TEXT("没有声音：语音文件没有取到。"));
				return;
			}
			AOfficeAgentSlot* Slot = Self->FindAgentSlot(AgentId);
			FString Error;
			if (!Slot || !Slot->PlayWavBytes(Bytes, Error))
			{
				Self->NoteTalk(FString::Printf(TEXT("没有声音：%s"), *(Error.IsEmpty() ? TEXT("播放失败") : Error)));
			}
		}));
}

namespace
{
	FString WrapDialogText(const FString& In, int32 Width)
	{
		FString Out;
		int32 Col = 0;
		for (const TCHAR Ch : In)
		{
			if (Ch == TEXT('\n'))
			{
				Out.AppendChar(Ch);
				Col = 0;
				continue;
			}
			Out.AppendChar(Ch);
			++Col;
			if (Col >= Width)
			{
				Out.AppendChar(TEXT('\n'));
				Col = 0;
			}
		}
		if (Out.Len() > 900)
		{
			Out = Out.Right(900);
		}
		return Out;
	}

	bool ReadSidecarPayload(const FString& Json, bool& bOk, FString& Reply, FString& Error, FString& Transcript)
	{
		bOk = false;
		Reply.Reset();
		Error.Reset();
		Transcript.Reset();
		TSharedPtr<FJsonObject> Obj;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
		{
			Error = Json.IsEmpty() ? TEXT("empty sidecar response") : Json;
			return false;
		}
		Obj->TryGetBoolField(TEXT("ok"), bOk);
		Obj->TryGetStringField(TEXT("reply"), Reply);
		Obj->TryGetStringField(TEXT("transcript"), Transcript);
		FString Hint;
		if (Obj->TryGetStringField(TEXT("hint"), Hint) && Reply.IsEmpty())
		{
			Reply = Hint;
		}
		Obj->TryGetStringField(TEXT("error"), Error);
		FString Detail;
		if (Obj->TryGetStringField(TEXT("detail"), Detail) && !Detail.IsEmpty())
		{
			Error = Error.IsEmpty() ? Detail : (Error + TEXT(": ") + Detail);
		}
		FString Active;
		if (Obj->TryGetStringField(TEXT("active_agent_id"), Active) && Error == TEXT("busy"))
		{
			Error = FString::Printf(TEXT("busy (%s)"), *Active);
		}
		return bOk;
	}

	// Empty notice means the wav can be fetched. A notice that starts with 没有声音 means do not play.
	void ReadTtsNotice(const FString& Json, FString& OutNotice)
	{
		OutNotice.Reset();
		TSharedPtr<FJsonObject> Obj;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
		{
			OutNotice = TEXT("没有声音：语音状态读不出来。");
			return;
		}
		FString TtsError;
		FString TtsWarning;
		FString TtsUrl;
		Obj->TryGetStringField(TEXT("tts_error"), TtsError);
		Obj->TryGetStringField(TEXT("tts_warning"), TtsWarning);
		Obj->TryGetStringField(TEXT("tts_url"), TtsUrl);
		if (!TtsError.IsEmpty())
		{
			OutNotice = FString::Printf(TEXT("没有声音：%s"), *TtsError);
			return;
		}
		if (TtsUrl.IsEmpty())
		{
			OutNotice = TEXT("没有声音：语音没有生成。");
			return;
		}
		OutNotice = TtsWarning;
	}
}

void AOfficeVRPawn::SpeakReply(const FString& Body, const FString& Reply)
{
	if (Reply.IsEmpty())
	{
		NoteTalk(TEXT("没有可朗读的回复。"));
		return;
	}
	FString Notice;
	ReadTtsNotice(Body, Notice);
	if (Notice.StartsWith(TEXT("没有声音")))
	{
		NoteTalk(Notice);
		return;
	}
	if (!Notice.IsEmpty())
	{
		NoteTalk(Notice);
	}
	PlayAgentTts();
}

void AOfficeVRPawn::ShowTalkPanel(const FString& Text)
{
	if (!PanelText)
	{
		return;
	}
	Opened = nullptr;
	SetDialogVisible(true);
	PanelText->SetText(FText::FromString(WrapDialogText(Text, 22)));
}

void AOfficeVRPawn::OnTalkPressed()
{
	bWantListen = true;
	AOfficeAgentSlot* Target = HoveredAgent.Get();
	if (bTalkPending)
	{
		return;
	}

	if (ActiveAgentId.IsEmpty())
	{
		if (!Target)
		{
			return;
		}
		bTalkPending = true;
		const FString AgentId = Target->AgentId;
		const FString Label = Target->DisplayLabel.IsEmpty() ? AgentId : Target->DisplayLabel;
		ShowTalkPanel(FString::Printf(TEXT("正在接通 %s。请继续按住 Y，直到出现「正在听」。"), *Label));
		TWeakObjectPtr<AOfficeVRPawn> WeakThis(this);
		FOfficeAgentAdapter::Get().BeginTalk(AgentId, FOfficeAgentHttpDone::CreateLambda(
			[WeakThis, AgentId, Label](bool bHttpOk, FString Body)
			{
				AOfficeVRPawn* Self = WeakThis.Get();
				if (!Self)
				{
					return;
				}
				Self->bTalkPending = false;
				bool bOk = false;
				FString Reply;
				FString Error;
				FString Transcript;
				ReadSidecarPayload(Body, bOk, Reply, Error, Transcript);
				if (!bHttpOk || !bOk)
				{
					Self->bWantListen = false;
					Self->ShowTalkPanel(FString::Printf(TEXT("接通失败：%s"), *(Error.IsEmpty() ? Body : Error)));
					return;
				}
				Self->ActiveAgentId = AgentId;
				if (Self->bWantListen)
				{
					Self->TalkLog = FString::Printf(
						TEXT("%s\n已接通。请继续按住 Y 说话，说完再松开。\nB 结束会话。"),
						*Label);
					Self->ShowTalkPanel(Self->TalkLog);
					Self->StartVoiceListen();
				}
				else
				{
					Self->TalkLog = FString::Printf(
						TEXT("%s\n已接通。刚才松开太早，这次没有录音。\n请再按住左手 Y，说完再松开。\nB 结束会话。"),
						*Label);
					Self->ShowTalkPanel(Self->TalkLog);
				}
			}));
		return;
	}

	if (Target && Target->AgentId != ActiveAgentId)
	{
		ShowTalkPanel(TEXT("已在和另一人对话。按 B 结束当前会话。"));
		return;
	}

	StartVoiceListen();
}

void AOfficeVRPawn::OnTalkReleased()
{
	bWantListen = false;
	if (bListening && !bTalkPending)
	{
		FinishVoiceListen();
	}
}

void AOfficeVRPawn::StartVoiceListen()
{
	if (ActiveAgentId.IsEmpty() || bListening || bTalkPending)
	{
		return;
	}
	bListening = true;
	bTalkPending = true;
	ShowTalkPanel(TalkLog + TEXT("\n\n正在听... 松开 Y 结束"));
	TWeakObjectPtr<AOfficeVRPawn> WeakThis(this);
	FOfficeAgentAdapter::Get().StartListen(FOfficeAgentHttpDone::CreateLambda(
		[WeakThis](bool bHttpOk, FString Body)
		{
			AOfficeVRPawn* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}
			Self->bTalkPending = false;
			bool bOk = false;
			FString Reply;
			FString Error;
			FString Transcript;
			ReadSidecarPayload(Body, bOk, Reply, Error, Transcript);
			if (!bHttpOk || !bOk)
			{
				Self->bListening = false;
				Self->ShowTalkPanel(Self->TalkLog + TEXT("\n麦失败：") + (Error.IsEmpty() ? Body : Error));
				return;
			}
			if (!Self->bWantListen)
			{
				Self->FinishVoiceListen();
			}
		}));
}

void AOfficeVRPawn::FinishVoiceListen()
{
	if (!bListening)
	{
		return;
	}
	bListening = false;
	bTalkPending = true;
	ShowTalkPanel(TalkLog + TEXT("\n\n正在识别..."));
	TWeakObjectPtr<AOfficeVRPawn> WeakThis(this);
	FOfficeAgentAdapter::Get().StopListen(FOfficeAgentHttpDone::CreateLambda(
		[WeakThis](bool bHttpOk, FString Body)
		{
			AOfficeVRPawn* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}
			Self->bTalkPending = false;
			bool bOk = false;
			FString Reply;
			FString Error;
			FString Transcript;
			ReadSidecarPayload(Body, bOk, Reply, Error, Transcript);
			if (!Transcript.IsEmpty())
			{
				Self->TalkLog += TEXT("\n你：") + Transcript;
			}
			if (!bHttpOk || !bOk)
			{
				Self->TalkLog += TEXT("\n") + (Error.IsEmpty() ? Body : Error);
				Self->ShowTalkPanel(Self->TalkLog);
				return;
			}
			const FString AgentLine = Reply.IsEmpty() ? TEXT("(空回复)") : Reply;
			Self->TalkLog += TEXT("\n") + Self->ActiveAgentId + TEXT("：") + AgentLine;
			Self->ShowTalkPanel(Self->TalkLog);
			Self->PushAgentSpeech(WrapDialogText(AgentLine, 14));
			Self->SpeakReply(Body, Reply);
		}));
}

void AOfficeVRPawn::SayToActiveAgent(const FString& Text)
{
	if (ActiveAgentId.IsEmpty())
	{
		UE_LOG(LogOfficeVR, Warning, TEXT("officevr.Say: no active session. Aim at an agent and press Y."));
		return;
	}
	if (bTalkPending)
	{
		UE_LOG(LogOfficeVR, Warning, TEXT("officevr.Say: still waiting for previous reply"));
		return;
	}

	bTalkPending = true;
	TalkLog += TEXT("\n你：") + Text + TEXT("\n...");
	ShowTalkPanel(TalkLog);
	TWeakObjectPtr<AOfficeVRPawn> WeakThis(this);
	FOfficeAgentAdapter::Get().PushText(Text, FOfficeAgentHttpDone::CreateLambda(
		[WeakThis, Text](bool bHttpOk, FString Body)
		{
			AOfficeVRPawn* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}
			Self->bTalkPending = false;
			bool bOk = false;
			FString Reply;
			FString Error;
			FString Transcript;
			ReadSidecarPayload(Body, bOk, Reply, Error, Transcript);
			if (!bHttpOk || !bOk)
			{
				Self->TalkLog += TEXT("\n错误：") + (Error.IsEmpty() ? Body : Error);
				Self->ShowTalkPanel(Self->TalkLog);
				return;
			}
			const FString AgentLine = Reply.IsEmpty() ? TEXT("(空回复)") : Reply;
			Self->TalkLog += TEXT("\n") + Self->ActiveAgentId + TEXT("：") + AgentLine;
			Self->ShowTalkPanel(Self->TalkLog);
			Self->PushAgentSpeech(WrapDialogText(AgentLine, 14));
			Self->SpeakReply(Body, Reply);
		}));
}

void AOfficeVRPawn::OnInspectPressed()
{
	if (HoveredAgent.Get())
	{
		return;
	}
	if (UInspectableComponent* Target = Hovered.Get())
	{
		ShowPanel(Target);
	}
}

void AOfficeVRPawn::OnInspectReleased()
{
}

void AOfficeVRPawn::OnClosePanel()
{
	if (bTalkPending)
	{
		return;
	}
	if (!ActiveAgentId.IsEmpty())
	{
		const FString AgentId = ActiveAgentId;
		bTalkPending = true;
		TWeakObjectPtr<AOfficeVRPawn> WeakThis(this);
		FOfficeAgentAdapter::Get().EndTalk(FOfficeAgentHttpDone::CreateLambda(
			[WeakThis, AgentId](bool, FString)
			{
				AOfficeVRPawn* Self = WeakThis.Get();
				if (!Self)
				{
					return;
				}
				Self->bTalkPending = false;
				Self->bListening = false;
				Self->bWantListen = false;
				if (AOfficeAgentSlot* Slot = Self->FindAgentSlot(AgentId))
				{
					Slot->SetSpeech(FString());
				}
				Self->ActiveAgentId.Reset();
				Self->TalkLog.Reset();
				Self->HidePanel();
			}));
		ShowTalkPanel(TEXT("正在结束会话..."));
		return;
	}
	HidePanel();
}

void AOfficeVRPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (PC->GetViewTarget() != this)
		{
			PC->SetViewTarget(this);
		}
	}
	UpdateLocomotion(DeltaSeconds);
	UpdateLaserAndHover();
	HideMotionControllerVisuals();

	const float Trigger = ReadBestAxis({
		EKeys::OculusTouch_Right_Trigger_Axis,
		EKeys::ValveIndex_Right_Trigger_Axis,
		EKeys::MixedReality_Right_Trigger_Axis,
		EKeys::Gamepad_RightTriggerAxis
	});
	if (Trigger > 0.75f)
	{
		if (!bTriggerHeld)
		{
			bTriggerHeld = true;
			OnInspectPressed();
		}
	}
	else if (Trigger < 0.35f)
	{
		bTriggerHeld = false;
	}
}
