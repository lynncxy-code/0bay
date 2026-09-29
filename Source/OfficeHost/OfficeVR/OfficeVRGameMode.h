#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "OfficeVRGameMode.generated.h"

UCLASS()
class OFFICEHOST_API AOfficeVRGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AOfficeVRGameMode();
	virtual void StartPlay() override;
};
