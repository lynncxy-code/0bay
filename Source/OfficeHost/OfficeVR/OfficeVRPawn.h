#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "OfficeVRPawn.generated.h"

class AOfficeAgentSlot;
class UCameraComponent;
class UMotionControllerComponent;
class USceneComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UInspectableComponent;

UCLASS()
class OFFICEHOST_API AOfficeVRPawn : public ACharacter
{
	GENERATED_BODY()

public:
	AOfficeVRPawn();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	void SayToActiveAgent(const FString& Text);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR")
	TObjectPtr<USceneComponent> VROrigin;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR")
	TObjectPtr<UCameraComponent> VRCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR")
	TObjectPtr<UMotionControllerComponent> LeftController;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR")
	TObjectPtr<UMotionControllerComponent> RightController;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR")
	TObjectPtr<UMotionControllerComponent> RightAim;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR")
	TObjectPtr<UStaticMeshComponent> LaserMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR")
	TObjectPtr<UStaticMeshComponent> TeleportMarker;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR")
	TObjectPtr<USceneComponent> PanelRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR")
	TObjectPtr<UStaticMeshComponent> DialogBoard;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR")
	TObjectPtr<UTextRenderComponent> PanelText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Move")
	float WalkSpeed = 220.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Move")
	float SmoothTurnRate = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Move")
	float TurnStickDeadzone = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Move")
	float LaserMaxDistance = 1200.f;

protected:
	void OnInspectPressed();
	void OnInspectReleased();
	void OnClosePanel();
	void OnTalkPressed();
	void OnTalkReleased();
	void StartVoiceListen();
	void FinishVoiceListen();
	void ShowTalkPanel(const FString& Text);
	void SetDialogVisible(bool bVisible);
	void PushAgentSpeech(const FString& Text);
	void NoteTalk(const FString& Line);
	void SpeakReply(const FString& Body, const FString& Reply);
	void PlayAgentTts();
	AOfficeAgentSlot* FindAgentSlot(const FString& AgentId) const;
	void ApplyVRRendererTweaks();

	float ReadBestAxis(const TArray<FKey>& Keys) const;
	bool IsAnyKeyDown(const TArray<FKey>& Keys) const;
	FTransform GetAimTransform() const;
	void UpdateLaserAndHover();
	void UpdateLocomotion(float DeltaSeconds);
	void TryTeleport();
	void ShowPanel(UInspectableComponent* Inspectable);
	void HidePanel();
	void ClearHover();
	void HideMotionControllerVisuals();

	TWeakObjectPtr<UInspectableComponent> Hovered;
	TWeakObjectPtr<AOfficeAgentSlot> HoveredAgent;
	TWeakObjectPtr<UInspectableComponent> Opened;
	FString ActiveAgentId;
	FString TalkLog;
	bool bTalkPending = false;
	bool bWantListen = false;
	bool bListening = false;
	bool bTeleportAiming = false;
	bool bTeleportValid = false;
	FVector TeleportDest = FVector::ZeroVector;
	bool bTriggerHeld = false;
};
