#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OfficeAgentSlot.generated.h"

class UCapsuleComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UAudioComponent;
class USceneComponent;
class UglTFRuntimeAsset;
class UAnimSequence;
class UWorld;

UCLASS()
class OFFICEHOST_API AOfficeAgentSlot : public AActor
{
	GENERATED_BODY()

public:
	AOfficeAgentSlot();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Agent")
	TObjectPtr<UCapsuleComponent> Capsule;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Agent")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Agent")
	TObjectPtr<UStaticMeshComponent> HeadMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Agent")
	TObjectPtr<UTextRenderComponent> LabelText;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Agent")
	TObjectPtr<UStaticMeshComponent> SpeechBoard;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Agent")
	TObjectPtr<UTextRenderComponent> SpeechText;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Agent")
	TObjectPtr<USceneComponent> OutfitRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Agent")
	TObjectPtr<UAudioComponent> VoiceAudio;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Agent")
	FString AgentId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Agent")
	FString DisplayLabel;

	UFUNCTION(BlueprintCallable, Category = "Agent")
	void ApplyLabel();

	UFUNCTION(BlueprintCallable, Category = "Agent")
	void SetSpeech(const FString& Text);

	bool PlayWavBytes(const TArray<uint8>& WavBytes, FString& OutError);
	void SetHovered(bool bHovered);

	static FVector SnapToFloor(UWorld* World, const FVector& Guess);
	static void EnsureSlots(UWorld* World);

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

private:
	void ApplyFreeOutfit();
	void FitOutfitToCapsule();

	UPROPERTY()
	TObjectPtr<UglTFRuntimeAsset> OutfitAsset;

	UPROPERTY()
	TArray<TObjectPtr<UAnimSequence>> OutfitAnims;
};
