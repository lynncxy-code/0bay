#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TwinRoamingRoute.generated.h"

class USplineComponent;

/** 关卡预放置的测试路线；RouteId 与后端受控目录中的 ue_route_id 对齐。 */
UCLASS(BlueprintType)
class ONTOTWINSYNC_API ATwinRoamingRoute : public AActor
{
    GENERATED_BODY()

public:
    ATwinRoamingRoute();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Route")
    USplineComponent* Spline;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Route")
    FString RouteId = TEXT("route.test.default");

    /** 默认按地面线理解 Spline Z；执行时自动加人物胶囊半高。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Route")
    bool bSplineAtGroundLevel = true;

    /** 由后端 runtime_route 在运行时临时生成，不保存回 UE 关卡。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Route")
    bool bRuntimeGenerated = false;
};
