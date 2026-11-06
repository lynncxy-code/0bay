#include "SceneInteraction/TwinSkinComponent.h"

#include "SceneInteraction/TwinRoamingTypes.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInterface.h"

UTwinSkinComponent::UTwinSkinComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UTwinSkinComponent::Configure(
    const TMap<FString, FString>& InSkinPrimaryAssetIds,
    const FString& InDefaultSkinId)
{
    SkinPrimaryAssetIds = InSkinPrimaryAssetIds;
    DefaultSkinId = InDefaultSkinId;
    AllowedSkinIds.Reset();
    SkinPrimaryAssetIds.GetKeys(AllowedSkinIds);
    AllowedSkinIds.Sort();

    if (!ActiveSkinId.IsEmpty() && !SkinPrimaryAssetIds.Contains(ActiveSkinId))
    {
        ClearSkinOverrides();
    }
}

void UTwinSkinComponent::ClearSkinOverrides()
{
    if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
    {
        if (USkeletalMeshComponent* MeshComponent = Character->GetMesh())
        {
            MeshComponent->EmptyOverrideMaterials();
        }
    }
    ActiveSkinId.Reset();
}

UTwinSkinAsset* UTwinSkinComponent::ResolveSkinAsset(
    const FString& PrimaryAssetId,
    FString& OutError) const
{
    const FPrimaryAssetId AssetId = FPrimaryAssetId::FromString(PrimaryAssetId);
    if (!AssetId.IsValid())
    {
        OutError = FString::Printf(TEXT("Invalid skin Primary Asset ID: %s"), *PrimaryAssetId);
        return nullptr;
    }

    UAssetManager& AssetManager = UAssetManager::Get();
    UObject* AssetObject = AssetManager.GetPrimaryAssetObject(AssetId);
    if (!AssetObject)
    {
        const FSoftObjectPath AssetPath = AssetManager.GetPrimaryAssetPath(AssetId);
        AssetObject = AssetPath.IsValid() ? AssetPath.TryLoad() : nullptr;
    }

    UTwinSkinAsset* SkinAsset = Cast<UTwinSkinAsset>(AssetObject);
    if (!SkinAsset)
    {
        OutError = FString::Printf(
            TEXT("Skin asset %s is missing or is not UTwinSkinAsset. Check Asset Manager scan rules."),
            *PrimaryAssetId);
    }
    return SkinAsset;
}

bool UTwinSkinComponent::ApplySkin(const FString& SkinId, FString& OutError)
{
    const FString* PrimaryAssetId = SkinPrimaryAssetIds.Find(SkinId);
    if (!PrimaryAssetId)
    {
        OutError = FString::Printf(TEXT("Skin is not allowed in this project: %s"), *SkinId);
        return false;
    }

    UTwinSkinAsset* SkinAsset = ResolveSkinAsset(*PrimaryAssetId, OutError);
    ACharacter* Character = Cast<ACharacter>(GetOwner());
    USkeletalMeshComponent* MeshComponent = Character ? Character->GetMesh() : nullptr;
    USkeletalMesh* Mesh = SkinAsset ? SkinAsset->Mesh.LoadSynchronous() : nullptr;
    if (!MeshComponent || !Mesh)
    {
        if (OutError.IsEmpty())
        {
            OutError = FString::Printf(TEXT("Skin mesh cannot be loaded: %s"), *SkinId);
        }
        return false;
    }

    // A skin is only valid for the currently selected character Skeleton.
    // Rejecting a mismatch before mutation prevents a failed switch from
    // leaving a partially initialized mesh/AnimBP behind.
    if (USkeletalMesh* CurrentMesh = MeshComponent->GetSkeletalMeshAsset())
    {
        if (CurrentMesh->GetSkeleton() != Mesh->GetSkeleton())
        {
            OutError = FString::Printf(
                TEXT("Skin Skeleton mismatch: current=%s skin=%s"),
                CurrentMesh->GetSkeleton() ? *CurrentMesh->GetSkeleton()->GetPathName() : TEXT("none"),
                Mesh->GetSkeleton() ? *Mesh->GetSkeleton()->GetPathName() : TEXT("none"));
            return false;
        }
    }

    UClass* AnimClass = SkinAsset->AnimInstanceClass.LoadSynchronous();
    TArray<UMaterialInterface*> MaterialOverrides;
    MaterialOverrides.Reserve(SkinAsset->MaterialOverrides.Num());
    for (const TSoftObjectPtr<UMaterialInterface>& SoftMaterial : SkinAsset->MaterialOverrides)
    {
        MaterialOverrides.Add(SoftMaterial.LoadSynchronous());
    }

    // SetSkeletalMesh does not clear override materials, and a null skin
    // AnimInstanceClass must not inherit the previous character's AnimBP.
    MeshComponent->Stop();
    MeshComponent->ClearAnimScriptInstance();
    MeshComponent->SetAnimInstanceClass(nullptr);
    MeshComponent->EmptyOverrideMaterials();
    MeshComponent->SetSkeletalMesh(Mesh);
    MeshComponent->SetAnimationMode(EAnimationMode::AnimationBlueprint);
    if (AnimClass)
    {
        MeshComponent->SetAnimInstanceClass(AnimClass);
    }
    for (int32 Index = 0; Index < MaterialOverrides.Num(); ++Index)
    {
        if (UMaterialInterface* Material = MaterialOverrides[Index])
        {
            MeshComponent->SetMaterial(Index, Material);
        }
    }

    ActiveSkinId = SkinId;
    return true;
}

bool UTwinSkinComponent::ApplyDefaultSkin(FString& OutError)
{
    if (DefaultSkinId.IsEmpty())
    {
        OutError = TEXT("No default skin is configured");
        return false;
    }
    return ApplySkin(DefaultSkinId, OutError);
}

bool UTwinSkinComponent::CycleSkin(FString& OutError)
{
    if (AllowedSkinIds.Num() == 0)
    {
        OutError = TEXT("No loadable skins are configured");
        return false;
    }
    const int32 CurrentIndex = AllowedSkinIds.IndexOfByKey(ActiveSkinId);
    const int32 NextIndex = CurrentIndex == INDEX_NONE ? 0 : (CurrentIndex + 1) % AllowedSkinIds.Num();
    return ApplySkin(AllowedSkinIds[NextIndex], OutError);
}
