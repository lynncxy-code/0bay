#pragma once

#include "CoreMinimal.h"
#include "SceneInteraction/TwinGodViewPawn.h"
#include "TwinRuntimeEditorCameraPawn.generated.h"

/** Runtime Editor-owned free camera. Its lifetime is independent from F7 roaming. */
UCLASS(BlueprintType)
class ONTOTWINSYNC_API ATwinRuntimeEditorCameraPawn : public ATwinGodViewPawn
{
    GENERATED_BODY()

public:
    ATwinRuntimeEditorCameraPawn();
};
