#pragma once

#include "CoreMinimal.h"

// One 70-second tour, expressed relative to the authored overview and floor.
// Camera position and look-at point have independent, continuous tangents.
namespace OntoTwinOverviewTour
{
struct FRoute
{
    FTransform Overview;
    FVector Positions[5];
    FVector Targets[5];
    static constexpr float Times[5] = {5.f, 20.f, 35.f, 55.f, 70.f};

    FRoute(const FTransform& InOverview, const FBox& FloorBounds) : Overview(InOverview)
    {
        const FVector Origin = Overview.GetLocation();
        const FVector Forward = Overview.GetRotation().GetForwardVector();
        const FVector Right = Overview.GetRotation().GetRightVector();
        const double Distance = FMath::Clamp(FVector::DotProduct(FloorBounds.GetCenter() - Origin, Forward), 800., 6000.);
        const FVector Target = Origin + Forward * Distance;
        // Stay above the visible floor contents and use modest offsets so the
        // tour remains an overview, rather than flying through the interior.
        const double MinimumZ = FMath::Min(Origin.Z, FloorBounds.Max.Z + 250.);
        Positions[0] = Origin;
        Positions[1] = Origin - Right * Distance * .22;
        Positions[2] = Origin - Right * Distance * .14 + Forward * Distance * .20;
        Positions[2].Z = FMath::Max(Positions[2].Z, MinimumZ);
        Positions[3] = Origin + Right * Distance * .22 + Forward * Distance * .08;
        Positions[3].Z = FMath::Max(Positions[3].Z, MinimumZ);
        Positions[4] = Origin;
        Targets[0] = Targets[4] = Target;
        Targets[1] = Target - Right * Distance * .08;
        Targets[2] = Target - Right * Distance * .12;
        Targets[3] = Target + Right * Distance * .10;
    }

    FVector Tangent(const FVector* Points, int32 Index) const
    {
        if (Index == 0 || Index == 4) return FVector::ZeroVector;
        return (Points[Index + 1] - Points[Index - 1]) / (Times[Index + 1] - Times[Index - 1]);
    }

    FTransform Sample(float Seconds) const
    {
        if (Seconds <= Times[0] || Seconds >= Times[4]) return Overview;
        int32 Segment = 0;
        while (Segment < 3 && Seconds > Times[Segment + 1]) ++Segment;
        const float Duration = Times[Segment + 1] - Times[Segment];
        const float Alpha = (Seconds - Times[Segment]) / Duration;
        auto Interpolate = [&](const FVector* Points)
        {
            return FMath::CubicInterp(Points[Segment], Tangent(Points, Segment) * Duration,
                Points[Segment + 1], Tangent(Points, Segment + 1) * Duration, Alpha);
        };
        const FVector Position = Interpolate(Positions);
        const FVector Target = Interpolate(Targets);
        return FTransform((Target - Position).Rotation(), Position);
    }
};
}
