#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "FX/SlatePostBufferBlur.h"
#include "OntoTwinGlassRenderer.generated.h"

class UMaterialInterface;

DECLARE_LOG_CATEGORY_EXTERN(LogOntoTwinGlass, Log, All);

UENUM(BlueprintType)
enum class EOntoTwinGlassQuality : uint8
{
	Performance UMETA(DisplayName = "Performance"),
	Balanced UMETA(DisplayName = "Balanced"),
	High UMETA(DisplayName = "High")
};

/** Host-project settings for the OntoTwin glass renderer. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "OntoTwin Glass Renderer"))
class ONTOTWINSYNC_API UOntoTwinGlassSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UOntoTwinGlassSettings();

	/** Master visual-system switch. Disabled means the readable Performance surface. */
	UPROPERTY(Config, EditAnywhere, Category = "Glass UI")
	bool bEnableGlassUI = true;

	/** Explicit host opt-in for reserving one global Slate postbuffer for High Screen quality. */
	UPROPERTY(Config, EditAnywhere, Category = "High Quality", meta = (ConfigRestartRequired = true))
	bool bEnableHighQualityRenderer = false;

	/** Legacy Stage-A alias. Existing hosts that enabled the spike continue to enable High. */
	UPROPERTY(Config, meta = (DeprecatedProperty, DeprecationMessage = "Use bEnableHighQualityRenderer"))
	bool bEnableRendererSpike = false;

	/** Project-wide request for diagnostics and legacy callers; Stage-C payloads carry per-overlay quality. */
	UPROPERTY(Config, EditAnywhere, Category = "Glass UI")
	EOntoTwinGlassQuality RequestedQuality = EOntoTwinGlassQuality::Balanced;

	/** The single shared Slate postbuffer reserved by OntoTwin (0-4). */
	UPROPERTY(Config, EditAnywhere, Category = "High Quality", meta = (ClampMin = "0", ClampMax = "4", UIMin = "0", UIMax = "4", ConfigRestartRequired = true))
	int32 ReservedPostBufferIndex = 0;

	/** Gaussian blur strength used by OntoTwin's shared postbuffer processor. */
	UPROPERTY(Config, EditAnywhere, Category = "High Quality", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "64.0", ConfigRestartRequired = true))
	float GaussianBlurStrength = 14.0f;

	/** Remove scale, highlight travel, and semantic pulses; short fades remain. */
	UPROPERTY(Config, EditAnywhere, Category = "Accessibility")
	bool bReduceMotion = false;

	/** Force the readable Performance surface without changing persisted panel requests. */
	UPROPERTY(Config, EditAnywhere, Category = "Accessibility")
	bool bReduceTransparency = false;

	/** Strengthen the neutral scrim and glass rim while preserving semantic colors. */
	UPROPERTY(Config, EditAnywhere, Category = "Accessibility")
	bool bHighContrast = false;
};

/** Concrete processor class required by Slate's per-slot processor setting. */
UCLASS()
class ONTOTWINSYNC_API UOntoTwinSlatePostBufferBlur final : public USlatePostBufferBlur
{
	GENERATED_BODY()

public:
	UOntoTwinSlatePostBufferBlur();
};

/** One deterministic renderer decision for a single overlay presentation context. */
struct ONTOTWINSYNC_API FOntoTwinGlassDecision
{
	EOntoTwinGlassQuality RequestedQuality = EOntoTwinGlassQuality::Balanced;
	EOntoTwinGlassQuality EffectiveQuality = EOntoTwinGlassQuality::Performance;
	FString DegradeReason;
	int32 PostBufferIndex = 0;
	UMaterialInterface* HighMaterial = nullptr;
};

/** Shared glass renderer resolver. It never upgrades above requested quality. */
class ONTOTWINSYNC_API FOntoTwinGlassRenderer final
{
public:
	/** Reserve and configure the selected Slate postbuffer before the first world initializes. */
	static void ConfigureSlatePostBuffer();

	/** Resolve the effective quality for Screen or World presentation. */
	static FOntoTwinGlassDecision Resolve(bool bWorldSpace);

	/** Resolve one overlay's persisted request while still honoring host capability and dev CVars. */
	static FOntoTwinGlassDecision Resolve(
		bool bWorldSpace,
		EOntoTwinGlassQuality RequestedQuality);

	static bool ShouldReduceMotion();
	static bool ShouldReduceTransparency();
	static bool ShouldUseHighContrast();

	static FString QualityToString(EOntoTwinGlassQuality Quality);
};
