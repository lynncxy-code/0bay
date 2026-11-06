#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "UI/OntoTwinGlassRenderer.h"

class UTexture2D;

/** Platform-owned visual tokens and packaged typography for OntoTwin overlays. */
class ONTOTWINSYNC_API FOntoTwinGlassTheme final
{
public:
	static FSlateFontInfo Font(float Size, bool bSemibold = false);
	static UTexture2D* FineNoiseTexture();

	static FLinearColor PrimaryText();
	static FLinearColor SecondaryText();
	static FLinearColor MutedText();
	static FLinearColor Rim();
	static FLinearColor ScreenTint(EOntoTwinGlassQuality Quality);
	static FLinearColor WorldTint(EOntoTwinGlassQuality Quality);
	static FLinearColor StatusAccent(const FString& Level);
	static FString StatusLabel(const FString& Level);
};
