#pragma once

#include "CoreMinimal.h"
#include "Layout/SlateRect.h"

namespace OntoTwinCameraFraming
{
// Rectangles are normalized to the viewport, with Y increasing downwards.
inline FSlateRect LargestClearRect(const TArray<FSlateRect>& Obstacles)
{
    TArray<float> Edges{0.f, 1.f};
    for (const FSlateRect& R : Obstacles)
    {
        Edges.AddUnique(FMath::Clamp(R.Left, 0.f, 1.f));
        Edges.AddUnique(FMath::Clamp(R.Right, 0.f, 1.f));
    }
    Edges.Sort();
    FSlateRect Best(0, 0, 0, 0);
    float BestArea = 0;
    for (int32 L = 0; L < Edges.Num(); ++L)
    for (int32 R = L + 1; R < Edges.Num(); ++R)
    {
        const float Width = Edges[R] - Edges[L];
        if (Width <= BestArea) continue;
        TArray<FVector2f> Blocked;
        for (const FSlateRect& O : Obstacles)
        {
            if (O.Left < Edges[R] && O.Right > Edges[L])
                Blocked.Emplace(FMath::Clamp(O.Top, 0.f, 1.f), FMath::Clamp(O.Bottom, 0.f, 1.f));
        }
        Blocked.Sort([](const FVector2f& A, const FVector2f& B) { return A.X < B.X; });
        Blocked.Emplace(1.f, 1.f);
        float Top = 0;
        for (const FVector2f& B : Blocked)
        {
            const float Area = Width * (B.X - Top);
            if (Area > BestArea)
            {
                BestArea = Area;
                Best = FSlateRect(Edges[L], Top, Edges[R], B.X);
            }
            Top = FMath::Max(Top, B.Y);
        }
    }
    return Best;
}

// Fit all eight corners against the four usable frustum planes, including
// their depth. Translate sideways so the target occupies the clear rect.
inline bool FitBounds(const FBox& Bounds, const FRotator& Rotation,
    double TanHalfX, double TanHalfY, FSlateRect Rect,
    FVector& OutLocation, double& OutDistance)
{
    if (!Bounds.IsValid || TanHalfX <= 0 || TanHalfY <= 0
        || Rect.Right - Rect.Left < .05f || Rect.Bottom - Rect.Top < .05f) return false;
    const float PadX = (Rect.Right - Rect.Left) * .06f;
    const float PadY = (Rect.Bottom - Rect.Top) * .06f;
    Rect.Left += PadX; Rect.Right -= PadX;
    Rect.Top += PadY; Rect.Bottom -= PadY;
    const double Left = (2 * Rect.Left - 1) * TanHalfX;
    const double Right = (2 * Rect.Right - 1) * TanHalfX;
    const double Bottom = (1 - 2 * Rect.Bottom) * TanHalfY;
    const double Top = (1 - 2 * Rect.Top) * TanHalfY;
    const double MidX = (Left + Right) * .5, MidY = (Bottom + Top) * .5;
    const FRotationMatrix Axes(Rotation);
    const FVector Forward = Axes.GetUnitAxis(EAxis::X);
    const FVector Side = Axes.GetUnitAxis(EAxis::Y);
    const FVector Up = Axes.GetUnitAxis(EAxis::Z);
    const FVector Center = Bounds.GetCenter(), Extent = Bounds.GetExtent();
    double Distance = 250;
    for (int32 Index = 0; Index < 8; ++Index)
    {
        const FVector Offset(Extent.X * ((Index & 1) ? 1 : -1),
            Extent.Y * ((Index & 2) ? 1 : -1), Extent.Z * ((Index & 4) ? 1 : -1));
        const double X = FVector::DotProduct(Offset, Side);
        const double Y = FVector::DotProduct(Offset, Up);
        const double Z = FVector::DotProduct(Offset, Forward);
        Distance = FMath::Max(Distance, 50 - Z);
        Distance = FMath::Max(Distance, (X - Right * Z) / (Right - MidX));
        Distance = FMath::Max(Distance, (Left * Z - X) / (MidX - Left));
        Distance = FMath::Max(Distance, (Y - Top * Z) / (Top - MidY));
        Distance = FMath::Max(Distance, (Bottom * Z - Y) / (MidY - Bottom));
    }
    OutDistance = Distance;
    OutLocation = Center - Forward * Distance - Side * (MidX * Distance) - Up * (MidY * Distance);
    return !OutLocation.ContainsNaN();
}
}
