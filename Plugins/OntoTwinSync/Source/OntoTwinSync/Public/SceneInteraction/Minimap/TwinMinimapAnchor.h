#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraActor.h"
#include "TwinMinimapAnchor.generated.h"

/**
 * Project-owned minimap view. The plugin owns capture and projection logic;
 * the host level only owns this camera transform and framing.
 */
UCLASS(BlueprintType)
class ONTOTWINSYNC_API ATwinMinimapAnchor : public ACameraActor
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="OntoTwin|Minimap")
    FString MinimapId = TEXT("minimap.default");

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="OntoTwin|Minimap",
        meta=(ClampMin="256", ClampMax="2048"))
    int32 CaptureWidth = 1024;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="OntoTwin|Minimap",
        meta=(ClampMin="256", ClampMax="2048"))
    int32 CaptureHeight = 768;

    /** Crop this fraction from every edge of the anchor camera framing. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="OntoTwin|Minimap",
        meta=(ClampMin="0.0", ClampMax="0.45"))
    float CropFractionPerEdge = 0.20f;

    /** Lights carrying this Actor tag are disabled only while the minimap is captured. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="OntoTwin|Minimap")
    FName CaptureSuppressedLightTag = TEXT("OntoTwin.Minimap.SuppressLight");

    /** Additional Actor object names or editor labels hidden only from this minimap capture. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="OntoTwin|Minimap|Visibility")
    TArray<FName> HiddenActorNames;

    /** Additional short streamed-level names hidden only from this minimap capture. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="OntoTwin|Minimap|Visibility")
    TArray<FName> HiddenLevelNames;
};
