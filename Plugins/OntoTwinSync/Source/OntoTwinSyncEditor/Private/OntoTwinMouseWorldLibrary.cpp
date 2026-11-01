#include "OntoTwinMouseWorldLibrary.h"

#include "EditorViewportClient.h"
#include "LevelEditorViewport.h"

bool UOntoTwinMouseWorldLibrary::GetCursorWorldLocation(
	FVector& OutLocation,
	FVector& OutNormal,
	FString& OutHitActorName,
	FString& OutViewTypeName,
	bool& bOutHitObject,
	float DepthAxisValue)
{
	OutLocation = FVector::ZeroVector;
	OutNormal = FVector::ZeroVector;
	OutHitActorName.Reset();
	OutViewTypeName = TEXT("Unknown");
	bOutHitObject = false;

	FLevelEditorViewportClient* ViewportClient = GCurrentLevelEditingViewportClient;
	if (!ViewportClient || !ViewportClient->Viewport)
	{
		return false;
	}

	UWorld* World = ViewportClient->GetWorld();
	if (!World)
	{
		return false;
	}

	const FViewportCursorLocation Cursor = ViewportClient->GetCursorWorldLocationFromMousePos();
	const FVector Origin = Cursor.GetOrigin();
	const FVector Direction = Cursor.GetDirection();

	switch (ViewportClient->ViewportType)
	{
	case LVT_Perspective:
		OutViewTypeName = TEXT("Perspective");
		break;
	case LVT_OrthoXY:
		OutViewTypeName = TEXT("Top");
		break;
	case LVT_OrthoNegativeXY:
		OutViewTypeName = TEXT("Bottom");
		break;
	case LVT_OrthoXZ:
		OutViewTypeName = TEXT("Front");
		break;
	case LVT_OrthoNegativeXZ:
		OutViewTypeName = TEXT("Back");
		break;
	case LVT_OrthoYZ:
		OutViewTypeName = TEXT("Left");
		break;
	case LVT_OrthoNegativeYZ:
		OutViewTypeName = TEXT("Right");
		break;
	default:
		break;
	}

	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(OntoTwinMouseWorld), true);
	const FVector TraceEnd = Origin + Direction * 1000000.f;

	if (World->LineTraceSingleByChannel(Hit, Origin, TraceEnd, ECC_Visibility, QueryParams))
	{
		OutLocation = Hit.Location;
		OutNormal = Hit.Normal;
		OutHitActorName = Hit.GetActor() ? Hit.GetActor()->GetActorLabel() : FString();
		bOutHitObject = true;
		return true;
	}

	if (!ViewportClient->IsOrtho())
	{
		return false;
	}

	OutLocation = Origin;
	switch (ViewportClient->ViewportType)
	{
	case LVT_OrthoXY:
	case LVT_OrthoNegativeXY:
		OutLocation.Z = DepthAxisValue;
		break;
	case LVT_OrthoXZ:
	case LVT_OrthoNegativeXZ:
		OutLocation.Y = DepthAxisValue;
		break;
	case LVT_OrthoYZ:
	case LVT_OrthoNegativeYZ:
		OutLocation.X = DepthAxisValue;
		break;
	default:
		return false;
	}

	return true;
}
