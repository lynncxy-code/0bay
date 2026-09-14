#pragma once

#include "CoreMinimal.h"
#include "Engine/TargetPoint.h"
#include "TwinRoamingSpawnAnchor.generated.h"

/** 关卡预放置的人物出生点；位置与朝向均由具体 UE 项目维护。 */
UCLASS(BlueprintType)
class ONTOTWINSYNC_API ATwinRoamingSpawnAnchor : public ATargetPoint
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="OntoTwin")
    FString SpawnId = TEXT("spawn.character.default");

    /** 开启时 Actor 位于地面附近，运行时向下投射并自动加人物胶囊半高。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="OntoTwin")
    bool bProjectToGround = true;
};
