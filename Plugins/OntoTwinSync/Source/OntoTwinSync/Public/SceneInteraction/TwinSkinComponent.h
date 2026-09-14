#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TwinSkinComponent.generated.h"

class UTwinSkinAsset;

/** 只负责会话内换肤；稳定 ID 到 Primary Asset ID 的映射来自后端运行投影。 */
UCLASS(ClassGroup=(OntoTwin), meta=(BlueprintSpawnableComponent))
class ONTOTWINSYNC_API UTwinSkinComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UTwinSkinComponent();

    void Configure(const TMap<FString, FString>& InSkinPrimaryAssetIds, const FString& InDefaultSkinId);
    bool ApplySkin(const FString& SkinId, FString& OutError);
    bool ApplyDefaultSkin(FString& OutError);
    bool CycleSkin(FString& OutError);
    /** Remove session-only material overrides when the next character has no skin. */
    void ClearSkinOverrides();

    FString GetActiveSkinId() const { return ActiveSkinId; }
    const TArray<FString>& GetAllowedSkinIds() const { return AllowedSkinIds; }

private:
    TMap<FString, FString> SkinPrimaryAssetIds;
    TArray<FString> AllowedSkinIds;
    FString DefaultSkinId;
    FString ActiveSkinId;

    UTwinSkinAsset* ResolveSkinAsset(const FString& PrimaryAssetId, FString& OutError) const;
};
