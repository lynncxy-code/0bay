#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TwinRoamingTypes.generated.h"

class ACharacter;
class UAnimInstance;
class UAnimationAsset;
class UMaterialInterface;
class USkeletalMesh;

UENUM(BlueprintType)
enum class ETwinRoamingCameraMode : uint8
{
    FirstPerson,
    NearFollow,
    God
};

UENUM(BlueprintType)
enum class ETwinRoamingRouteState : uint8
{
    Unavailable,
    Idle,
    AutoRoute,
    PausedForNarration,
    PausedByUser,
    Joining,
    Completed,
    Blocked
};

struct ONTOTWINSYNC_API FTwinNarrationRuntimeSegment
{
    FString SegmentId;
    FString Text;
    float DurationSeconds = 3.0f;
    FString AudioAssetId;
    FString AudioSha256;
    float AudioDurationSeconds = 0.0f;
};

struct ONTOTWINSYNC_API FTwinRoamingRuntimeWaypoint
{
    FString WaypointId;
    FVector Position = FVector::ZeroVector;
    float TriggerRadiusCm = 100.0f;
    FString NarrationMode;
    FString AudioState;
    TArray<FTwinNarrationRuntimeSegment> NarrationSegments;

    bool HasNarration() const
    {
        return !NarrationMode.IsEmpty() && NarrationSegments.Num() > 0;
    }
};

USTRUCT(BlueprintType)
struct ONTOTWINSYNC_API FTwinRoamingMovementSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OntoTwin|Roaming|Movement")
    float WalkSpeedCmS = 250.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OntoTwin|Roaming|Movement")
    float SprintSpeedCmS = 500.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OntoTwin|Roaming|Movement")
    float AutoRouteSpeedCmS = 180.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OntoTwin|Roaming|Movement")
    float JumpHeightCm = 80.0f;
};

USTRUCT(BlueprintType)
struct ONTOTWINSYNC_API FTwinNearCameraSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OntoTwin|Roaming|Camera")
    float DistanceCm = 120.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OntoTwin|Roaming|Camera")
    float HeightCm = 35.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OntoTwin|Roaming|Camera")
    float LookSensitivity = 1.0f;
};

USTRUCT(BlueprintType)
struct ONTOTWINSYNC_API FTwinFirstPersonCameraSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OntoTwin|Roaming|Camera")
    float EyeHeightCm = 165.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OntoTwin|Roaming|Camera")
    float FovDeg = 85.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OntoTwin|Roaming|Camera")
    float LookSensitivity = 1.0f;
};

USTRUCT(BlueprintType)
struct ONTOTWINSYNC_API FTwinGodCameraSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OntoTwin|Roaming|Camera")
    FString CameraId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OntoTwin|Roaming|Camera")
    float MoveSpeedCmS = 1800.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OntoTwin|Roaming|Camera")
    float LookSensitivity = 1.0f;
};

/** 后端 /runtime 的 UE 精简投影；不包含业务实例或人物位置。 */
struct ONTOTWINSYNC_API FTwinRoamingRuntimeRoute
{
    FString RouteId;
    FString DisplayName;
    bool bDefault = false;
    int32 Revision = 0;
    FString Level;
    float GroundZHintCm = 0.0f;
    float SpeedCmS = 180.0f;
    bool bLoop = false;
    TArray<FVector> Points;
    TArray<FTwinRoamingRuntimeWaypoint> Waypoints;
};

/** UE Dock 可在当前会话内原地切换的人物资源。 */
struct ONTOTWINSYNC_API FTwinRoamingRuntimeCharacter
{
    FString CharacterId;
    FString DisplayName;
    FString PrimaryAssetId;
    FString SkeletonId;
    FString DefaultSkinId;
    TMap<FString, FString> SkinPrimaryAssetIds;
};

struct ONTOTWINSYNC_API FTwinRoamingRuntimeConfig
{
    bool bEnabled = false;
    bool bAutoEnter = false;
    bool bMinimapEnabled = false;
    FString CharacterId;
    FString CharacterDisplayName;
    FString CharacterPrimaryAssetId;
    TMap<FString, FString> SkinPrimaryAssetIds;
    FString DefaultSkinId;
    bool bSpawnFromAnchor = false;
    FString SpawnAnchorId;
    FVector SpawnLocation = FVector::ZeroVector;
    float SpawnTraceOriginZCm = 1000.0f;
    float SpawnYawDeg = 0.0f;
    bool bHasZHint = false;
    float ZHintCm = 0.0f;
    FTwinRoamingMovementSettings Movement;
    FTwinFirstPersonCameraSettings FirstPersonCamera;
    FTwinNearCameraSettings NearCamera;
    FTwinGodCameraSettings GodCamera;
    ETwinRoamingCameraMode DefaultCameraMode = ETwinRoamingCameraMode::God;
    bool bRouteEnabled = false;
    bool bRouteAutoStart = true;
    bool bRouteLoop = false;
    bool bTakeoverEnabled = true;
    FString RouteId;
    FString RouteDisplayName;
    bool bHasRuntimeRoute = false;
    int32 RuntimeRouteRevision = 0;
    FString RuntimeRouteLevel;
    float RuntimeRouteGroundZHintCm = 0.0f;
    TArray<FVector> RuntimeRoutePoints;
    TArray<FTwinRoamingRuntimeWaypoint> RuntimeRouteWaypoints;
    TArray<FTwinRoamingRuntimeRoute> AvailableRoutes;
    TArray<FTwinRoamingRuntimeCharacter> AvailableCharacters;
};

/**
 * 项目侧创建此 Primary Data Asset，资产名需与后端 Primary Asset ID 的名称部分一致。
 * 例如 TwinCharacter:ObserverBase 对应名为 ObserverBase 的 UTwinCharacterAsset。
 */
UCLASS(BlueprintType)
class ONTOTWINSYNC_API UTwinCharacterAsset : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    virtual FPrimaryAssetId GetPrimaryAssetId() const override
    {
        return FPrimaryAssetId(TEXT("TwinCharacter"), GetFName());
    }

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Character")
    TSoftClassPtr<ACharacter> CharacterClass;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Character")
    TSoftObjectPtr<USkeletalMesh> BaseMesh;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Character")
    TSoftClassPtr<UAnimInstance> AnimInstanceClass;

    /** Optional hidden locomotion source used by runtime-retarget animation blueprints. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Character|Animation")
    TSoftObjectPtr<USkeletalMesh> AnimationSourceMesh;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Character|Animation")
    TSoftClassPtr<UAnimInstance> AnimationSourceAnimInstanceClass;

    /** Optional in-place animation used while an automatic route owns movement. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Character|Animation")
    TSoftObjectPtr<UAnimationAsset> AutoRouteAnimation;

    /**
     * Optional native clips for meshes that do not have a compatible
     * Animation Blueprint.  Both clips must use the exact Skeleton referenced
     * by BaseMesh.  When present, the roaming character drives these clips in
     * single-node mode; they are intentionally independent from the worker /
     * Manny retarget source above.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Character|Animation")
    TSoftObjectPtr<UAnimationAsset> DirectIdleAnimation;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Character|Animation")
    TSoftObjectPtr<UAnimationAsset> DirectWalkAnimation;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Character|Animation", meta=(ClampMin="1.0"))
    float AutoRouteAnimationReferenceSpeedCmS = 180.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Collision", meta=(ClampMin="1.0"))
    float CapsuleRadiusCm = 34.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Collision", meta=(ClampMin="1.0"))
    float CapsuleHalfHeightCm = 88.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mesh")
    FVector MeshOffsetCm = FVector(0.0f, 0.0f, -88.0f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Mesh")
    float MeshYawOffsetDeg = -90.0f;
};

/** 同 Skeleton 换肤资源。当前会话皮肤不写回 OntoTwin。 */
UCLASS(BlueprintType)
class ONTOTWINSYNC_API UTwinSkinAsset : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    virtual FPrimaryAssetId GetPrimaryAssetId() const override
    {
        return FPrimaryAssetId(TEXT("TwinSkin"), GetFName());
    }

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skin")
    FString SkeletonId;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skin")
    TSoftObjectPtr<USkeletalMesh> Mesh;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skin")
    TSoftClassPtr<UAnimInstance> AnimInstanceClass;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skin")
    TArray<TSoftObjectPtr<UMaterialInterface>> MaterialOverrides;
};
