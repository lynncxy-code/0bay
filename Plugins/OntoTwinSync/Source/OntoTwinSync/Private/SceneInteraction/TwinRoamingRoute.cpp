#include "SceneInteraction/TwinRoamingRoute.h"

#include "Components/SplineComponent.h"

ATwinRoamingRoute::ATwinRoamingRoute()
{
    PrimaryActorTick.bCanEverTick = false;
    Spline = CreateDefaultSubobject<USplineComponent>(TEXT("RouteSpline"));
    RootComponent = Spline;
}
