#include "SceneInteraction/TwinInteractionManagerComponent.h"

#include "SceneInteraction/OntoTwinCrosshairWidget.h"
#include "SceneInteraction/OntoTwinNarrationHUDWidget.h"
#include "SceneInteraction/Minimap/TwinMinimapAnchor.h"
#include "SceneInteraction/OntoTwinRoamingHUDWidget.h"
#include "SceneInteraction/OntoTwinRuntimeDockWidget.h"
#include "SceneInteraction/TwinCameraModeComponent.h"
#include "SceneInteraction/TwinCameraVisibility.h"
#include "SceneInteraction/TwinGodViewPawn.h"
#include "SceneInteraction/TwinRoamingCharacter.h"
#include "SceneInteraction/TwinRoamingRoute.h"
#include "SceneInteraction/TwinRoamingSpawnAnchor.h"
#include "SceneInteraction/TwinRouteFollowerComponent.h"
#include "SceneInteraction/TwinSkinComponent.h"
#include "UI/OntoTwinGlassRenderer.h"
#include "TwinInstance.h"
#include "TwinSceneManager.h"
#include "WebInteraction/OntoTwinWebInteractionComponent.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/CameraComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Audio.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/LightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SplineComponent.h"
#include "Dom/JsonValue.h"
#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Level.h"
#include "Engine/Light.h"
#include "Engine/OverlapResult.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "HAL/PlatformTime.h"
#include "HttpModule.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Interfaces/IHttpResponse.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/FileManager.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PixelFormat.h"
#include "SceneView.h"
#include "Widgets/Layout/Anchors.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TimerManager.h"
#include "Sound/SoundWaveProcedural.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include <bcrypt.h>
#endif

namespace
{
const TSharedPtr<FJsonObject> GetObject(const TSharedPtr<FJsonObject>& Parent, const TCHAR* Field)
{
    if (!Parent.IsValid()) return nullptr;
    const TSharedPtr<FJsonObject>* Result = nullptr;
    return Parent->TryGetObjectField(Field, Result) && Result ? *Result : nullptr;
}

double NumberOr(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, double Default)
{
    double Value = Default;
    return Object.IsValid() && Object->TryGetNumberField(Field, Value) ? Value : Default;
}

bool BoolOr(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, bool Default)
{
    bool Value = Default;
    return Object.IsValid() && Object->TryGetBoolField(Field, Value) ? Value : Default;
}

FString StringOr(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, const FString& Default = FString())
{
    FString Value;
    return Object.IsValid() && Object->TryGetStringField(Field, Value) ? Value : Default;
}

bool HasValidSha256(const FString& Value)
{
    if (Value.Len() != 64) return false;
    for (const TCHAR Character : Value)
    {
        if (!FChar::IsHexDigit(Character)) return false;
    }
    return true;
}

bool MatchesSha256(const TArray<uint8>& Bytes, const FString& Expected)
{
    if (!HasValidSha256(Expected) || Bytes.Num() == 0) return false;
#if PLATFORM_WINDOWS
    BCRYPT_ALG_HANDLE Algorithm = nullptr;
    BCRYPT_HASH_HANDLE Hash = nullptr;
    DWORD ObjectLength = 0;
    DWORD HashLength = 0;
    DWORD ResultLength = 0;
    TArray<uint8> HashObject;
    TArray<uint8> Digest;

    bool bSuccess = BCryptOpenAlgorithmProvider(
        &Algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (bSuccess)
    {
        bSuccess = BCryptGetProperty(
            Algorithm,
            BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&ObjectLength),
            sizeof(ObjectLength),
            &ResultLength,
            0) >= 0;
    }
    if (bSuccess)
    {
        bSuccess = BCryptGetProperty(
            Algorithm,
            BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&HashLength),
            sizeof(HashLength),
            &ResultLength,
            0) >= 0
            && HashLength == 32;
    }
    if (bSuccess)
    {
        HashObject.SetNumUninitialized(static_cast<int32>(ObjectLength));
        Digest.SetNumUninitialized(static_cast<int32>(HashLength));
        bSuccess = BCryptCreateHash(
            Algorithm,
            &Hash,
            HashObject.GetData(),
            ObjectLength,
            nullptr,
            0,
            0) >= 0;
    }
    if (bSuccess)
    {
        bSuccess = BCryptHashData(
            Hash,
            const_cast<PUCHAR>(Bytes.GetData()),
            static_cast<ULONG>(Bytes.Num()),
            0) >= 0;
    }
    if (bSuccess)
    {
        bSuccess = BCryptFinishHash(
            Hash, Digest.GetData(), HashLength, 0) >= 0;
    }

    if (Hash) BCryptDestroyHash(Hash);
    if (Algorithm) BCryptCloseAlgorithmProvider(Algorithm, 0);
    if (!bSuccess) return false;

    FString Actual;
    Actual.Reserve(64);
    for (const uint8 Byte : Digest)
    {
        Actual += FString::Printf(TEXT("%02x"), Byte);
    }
    return Actual.Equals(Expected, ESearchCase::IgnoreCase);
#else
    // Unsupported platforms fail closed so narration falls back to subtitles.
    return false;
#endif
}

FString NormalizeLevelPackageName(FString Value)
{
    Value.TrimStartAndEndInline();
    int32 ObjectSeparator = INDEX_NONE;
    if (Value.FindChar(TEXT('.'), ObjectSeparator)) Value.LeftInline(ObjectSeparator);

    int32 LastSlash = INDEX_NONE;
    Value.FindLastChar(TEXT('/'), LastSlash);
    FString Prefix = LastSlash == INDEX_NONE ? FString() : Value.Left(LastSlash + 1);
    FString Leaf = LastSlash == INDEX_NONE ? Value : Value.Mid(LastSlash + 1);
    if (Leaf.StartsWith(TEXT("UEDPIE_")))
    {
        const int32 NameSeparator = Leaf.Find(
            TEXT("_"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 7);
        if (NameSeparator != INDEX_NONE) Leaf = Leaf.Mid(NameSeparator + 1);
    }
    return Prefix + Leaf;
}

bool ParseRuntimeRouteObject(
    const TSharedPtr<FJsonObject>& Object,
    FTwinRoamingRuntimeRoute& OutRoute)
{
    if (!Object.IsValid()) return false;
    OutRoute.RouteId = StringOr(Object, TEXT("route_id"));
    OutRoute.DisplayName = StringOr(
        Object, TEXT("display_name"), OutRoute.RouteId);
    OutRoute.bDefault = BoolOr(Object, TEXT("is_default"), false);
    OutRoute.Revision = FMath::Max(
        0, FMath::RoundToInt(NumberOr(Object, TEXT("route_revision"), 0.0)));
    OutRoute.Level = NormalizeLevelPackageName(StringOr(Object, TEXT("ue_level")));
    OutRoute.GroundZHintCm = NumberOr(
        Object, TEXT("floor_ground_z_hint_cm"), 0.0);
    OutRoute.SpeedCmS = NumberOr(Object, TEXT("speed_cm_s"), 180.0);
    OutRoute.bLoop = BoolOr(Object, TEXT("loop"), false);

    const TArray<TSharedPtr<FJsonValue>>* Waypoints = nullptr;
    if (OutRoute.RouteId.IsEmpty()
        || !Object->TryGetArrayField(TEXT("waypoints_ue_cm"), Waypoints)
        || !Waypoints || Waypoints->Num() < 2)
    {
        return false;
    }
    for (const TSharedPtr<FJsonValue>& Value : *Waypoints)
    {
        if (!Value.IsValid() || Value->Type != EJson::Array) return false;
        const TArray<TSharedPtr<FJsonValue>>& Coordinates = Value->AsArray();
        if (Coordinates.Num() < 3
            || !Coordinates[0].IsValid() || Coordinates[0]->Type != EJson::Number
            || !Coordinates[1].IsValid() || Coordinates[1]->Type != EJson::Number
            || !Coordinates[2].IsValid() || Coordinates[2]->Type != EJson::Number)
        {
            return false;
        }
        const FVector Point(
            Coordinates[0]->AsNumber(),
            Coordinates[1]->AsNumber(),
            Coordinates[2]->AsNumber());
        if (Point.ContainsNaN()) return false;
        OutRoute.Points.Add(Point);
    }

    const TArray<TSharedPtr<FJsonValue>>* StructuredWaypoints = nullptr;
    if (Object->TryGetArrayField(TEXT("waypoints"), StructuredWaypoints)
        && StructuredWaypoints)
    {
        for (const TSharedPtr<FJsonValue>& Value : *StructuredWaypoints)
        {
            const TSharedPtr<FJsonObject> WaypointObject = Value.IsValid()
                ? Value->AsObject() : nullptr;
            if (!WaypointObject.IsValid()) return false;
            FTwinRoamingRuntimeWaypoint Waypoint;
            Waypoint.WaypointId = StringOr(WaypointObject, TEXT("waypoint_id"));
            Waypoint.TriggerRadiusCm = NumberOr(
                WaypointObject, TEXT("trigger_radius_cm"), 100.0);
            const TArray<TSharedPtr<FJsonValue>>* Position = nullptr;
            if (!WaypointObject->TryGetArrayField(TEXT("position_ue_cm"), Position)
                || !Position || Position->Num() < 3)
            {
                return false;
            }
            Waypoint.Position = FVector(
                (*Position)[0]->AsNumber(),
                (*Position)[1]->AsNumber(),
                (*Position)[2]->AsNumber());
            if (Waypoint.Position.ContainsNaN()) return false;

            const TSharedPtr<FJsonObject>* NarrationObject = nullptr;
            if (WaypointObject->TryGetObjectField(TEXT("narration"), NarrationObject)
                && NarrationObject && NarrationObject->IsValid())
            {
                Waypoint.NarrationMode = StringOr(*NarrationObject, TEXT("mode"));
                Waypoint.AudioState = StringOr(*NarrationObject, TEXT("audio_state"));
                const TArray<TSharedPtr<FJsonValue>>* Segments = nullptr;
                if ((*NarrationObject)->TryGetArrayField(TEXT("segments"), Segments)
                    && Segments)
                {
                    for (const TSharedPtr<FJsonValue>& SegmentValue : *Segments)
                    {
                        const TSharedPtr<FJsonObject> SegmentObject = SegmentValue.IsValid()
                            ? SegmentValue->AsObject() : nullptr;
                        if (!SegmentObject.IsValid()) continue;
                        FTwinNarrationRuntimeSegment Segment;
                        Segment.SegmentId = StringOr(SegmentObject, TEXT("segment_id"));
                        Segment.Text = StringOr(SegmentObject, TEXT("text"));
                        Segment.DurationSeconds = FMath::Clamp(
                            NumberOr(SegmentObject, TEXT("duration_sec"), 3.0), 1.0, 600.0);
                        Segment.AudioAssetId = StringOr(
                            SegmentObject, TEXT("audio_asset_id"));
                        Segment.AudioSha256 = StringOr(
                            SegmentObject, TEXT("audio_sha256"));
                        Segment.AudioDurationSeconds = FMath::Max(
                            0.0, NumberOr(SegmentObject, TEXT("audio_duration_sec"), 0.0));
                        if (!Segment.Text.IsEmpty()) Waypoint.NarrationSegments.Add(Segment);
                    }
                }
            }
            OutRoute.Waypoints.Add(Waypoint);
        }
        if (OutRoute.Waypoints.Num() != OutRoute.Points.Num()) return false;
    }
    return true;
}

void ApplyRuntimeRouteToConfig(
    FTwinRoamingRuntimeConfig& Config,
    const FTwinRoamingRuntimeRoute& Route)
{
    Config.bRouteEnabled = true;
    Config.bHasRuntimeRoute = true;
    Config.RouteId = Route.RouteId;
    Config.RouteDisplayName = Route.DisplayName;
    Config.RuntimeRouteRevision = Route.Revision;
    Config.RuntimeRouteLevel = Route.Level;
    Config.RuntimeRouteGroundZHintCm = Route.GroundZHintCm;
    Config.Movement.AutoRouteSpeedCmS = Route.SpeedCmS;
    Config.bRouteLoop = Route.bLoop;
    Config.RuntimeRoutePoints = Route.Points;
    Config.RuntimeRouteWaypoints = Route.Waypoints;
}

const FTwinRoamingRuntimeRoute* FindRuntimeRoute(
    const TArray<FTwinRoamingRuntimeRoute>& Routes,
    const FString& RouteId)
{
    return Routes.FindByPredicate([&RouteId](const FTwinRoamingRuntimeRoute& Route)
    {
        return Route.RouteId == RouteId;
    });
}

FString CameraModeId(ETwinRoamingCameraMode Mode)
{
    if (Mode == ETwinRoamingCameraMode::FirstPerson) return TEXT("first_person");
    if (Mode == ETwinRoamingCameraMode::God) return TEXT("god");
    return TEXT("near_follow");
}

FString CameraModeLabel(ETwinRoamingCameraMode Mode)
{
    if (Mode == ETwinRoamingCameraMode::FirstPerson) return TEXT("第一人称");
    if (Mode == ETwinRoamingCameraMode::God) return TEXT("上帝视角");
    return TEXT("过肩视角");
}

FString CompactHudLabel(const FString& Value, const TCHAR* Fallback, int32 MaxCharacters = 24)
{
    FString Result = Value.TrimStartAndEnd();
    if (Result.IsEmpty()) Result = Fallback;
    if (Result.Len() > MaxCharacters)
    {
        Result = Result.Left(MaxCharacters - 1) + TEXT("…");
    }
    return Result;
}
}

UTwinInteractionManagerComponent::UTwinInteractionManagerComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UTwinInteractionManagerComponent::BeginPlay()
{
    Super::BeginPlay();
    bShuttingDown = false;
    SceneManager = Cast<ATwinSceneManager>(GetOwner());
    PlayerController = UGameplayStatics::GetPlayerController(this, 0);
    if (!SceneManager)
    {
        RuntimeState = TEXT("blocked");
        LastError = TEXT("TwinInteractionManager must be owned by ATwinSceneManager");
        SetComponentTickEnabled(false);
        return;
    }
    RepresentationVisualReadyHandle = ATwinInstance::OnRepresentationVisualReady.AddUObject(
        this,
        &UTwinInteractionManagerComponent::HandleRepresentationVisualReady);
    SetupInput();
    CreateHud();
    ApplyStartupView();
    PollRuntimeProjection();
}

void UTwinInteractionManagerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    bShuttingDown = true;
    ATwinInstance::OnRepresentationVisualReady.Remove(RepresentationVisualReadyHandle);
    RepresentationVisualReadyHandle.Reset();
    ExitRoaming();
    OntoTwinCameraVisibility::ClearFromPlayer(
        PlayerController, StartupViewVisibilityState);
    DestroyHud();
    RemoveInput();
    Super::EndPlay(EndPlayReason);
}

void UTwinInteractionManagerComponent::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!SceneManager) return;
    if (!PlayerController)
    {
        PlayerController = UGameplayStatics::GetPlayerController(this, 0);
        if (PlayerController)
        {
            SetupInput();
            ApplyStartupView();
        }
    }

    PollAccumulator += DeltaTime;
    if (PollAccumulator >= RuntimePollInterval)
    {
        PollAccumulator = 0.0f;
        PollRuntimeProjection();
    }

    HeartbeatAccumulator += DeltaTime;
    if (HeartbeatAccumulator >= 2.0f)
    {
        HeartbeatAccumulator = 0.0f;
        SendHeartbeat();
    }

    // F7 is the global entry/exit switch. Poll it directly so it remains
    // available even when the host project replaces or reorders Enhanced
    // Input components while possessing a different pawn.
    if (PlayerController && PlayerController->WasInputKeyJustPressed(ToggleRoamingKey))
    {
        ToggleRoaming();
    }

    // V must survive possession and host-project InputComponent replacement just
    // like F7. Keep it out of the dynamically bound Enhanced Input component so
    // there is exactly one global edge-triggered path for camera cycling.
    if (PlayerController && bRoamingActive
        && PlayerController->WasInputKeyJustPressed(ToggleViewKey))
    {
        ToggleCameraMode();
    }

    // Keep mouse capture state resilient when a host replaces its Enhanced
    // Input component or swallows the button release event.
    if (PlayerController && bRoamingActive && bEnhancedInputReady)
    {
        if (PlayerController->WasInputKeyJustPressed(MouseLookKey))
        {
            SetRoamingMouseLook(true);
        }
        if (PlayerController->WasInputKeyJustReleased(MouseLookKey))
        {
            SetRoamingMouseLook(false);
        }
    }

    if (PlayerController && !bRoamingActive
        && PlayerController->WasInputKeyJustPressed(ToggleHudKey)
        && (!SceneManager->IsRuntimeEditModeActive() || bHudInteraction))
    {
        ToggleHudInteraction();
    }

    if (!bEnhancedInputReady)
    {
        TickFallbackInput(DeltaTime);
    }
    UpdateCrosshairTarget();
    UpdateMinimapMarker(DeltaTime);
    RefreshHud();
}

void UTwinInteractionManagerComponent::AddProjectHeaders(
    const TSharedRef<IHttpRequest, ESPMode::ThreadSafe>& Request) const
{
    if (!SceneManager) return;
    Request->SetHeader(TEXT("X-OntoTwin-UE-Project-Id"), SceneManager->UEProjectId);
    Request->SetHeader(TEXT("X-OntoTwin-UE-Project-Name"), SceneManager->UEProjectName);
#if WITH_EDITOR
    Request->SetHeader(TEXT("X-OntoTwin-UE-Context"), TEXT("editor"));
#else
    Request->SetHeader(TEXT("X-OntoTwin-UE-Context"), TEXT("packaged"));
#endif
}

void UTwinInteractionManagerComponent::PollRuntimeProjection()
{
    if (bShuttingDown || !SceneManager || bRuntimeRequestInFlight) return;
    bRuntimeRequestInFlight = true;

    FString BaseUrl = SceneManager->BackendBaseUrl;
    BaseUrl.RemoveFromEnd(TEXT("/"));
    const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(BaseUrl + TEXT("/api/v2/scene-interactions/runtime"));
    Request->SetVerb(TEXT("GET"));
    Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
    AddProjectHeaders(Request);

    const TWeakObjectPtr<UTwinInteractionManagerComponent> WeakThis(this);
    Request->OnProcessRequestComplete().BindLambda(
        [WeakThis](FHttpRequestPtr, FHttpResponsePtr Response, bool bWasSuccessful)
        {
            UTwinInteractionManagerComponent* Self = WeakThis.Get();
            if (!Self || Self->bShuttingDown) return;
            Self->bRuntimeRequestInFlight = false;
            if (!bWasSuccessful || !Response.IsValid())
            {
                Self->RecordBackendFailure(TEXT("Scene Interaction backend is unreachable"));
                return;
            }
            if (Response->GetResponseCode() == 403)
            {
                Self->bBackendOnline = true;
                Self->RuntimeState = TEXT("blocked");
                Self->LastError = Response->GetContentAsString();
                Self->ExitRoaming();
                Self->RuntimeState = TEXT("blocked");
                return;
            }
            if (Response->GetResponseCode() < 200 || Response->GetResponseCode() >= 300)
            {
                Self->RecordBackendFailure(FString::Printf(
                    TEXT("Runtime projection HTTP %d"), Response->GetResponseCode()));
                return;
            }

            TSharedPtr<FJsonObject> Payload;
            const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
            if (!FJsonSerializer::Deserialize(Reader, Payload) || !Payload.IsValid())
            {
                Self->RecordBackendFailure(TEXT("Runtime projection returned invalid JSON"));
                return;
            }
            Self->bBackendOnline = true;
            Self->ConsecutiveFailures = 0;
            Self->HandleRuntimeProjection(Payload);
        });
    if (!Request->ProcessRequest())
    {
        bRuntimeRequestInFlight = false;
        RecordBackendFailure(TEXT("Runtime projection request could not be started"));
    }
}

void UTwinInteractionManagerComponent::RecordBackendFailure(const FString& Error)
{
    ++ConsecutiveFailures;
    LastError = Error;
    if (ConsecutiveFailures >= RuntimeOfflineThreshold)
    {
        bBackendOnline = false;
        RuntimeState = TEXT("offline");
    }
}

bool UTwinInteractionManagerComponent::ParseRuntimeConfig(
    const TSharedPtr<FJsonObject>& Payload,
    FTwinRoamingRuntimeConfig& OutConfig,
    int32& OutRevision,
    FString& OutToken,
    FString& OutCatalogVersion,
    FString& OutBlockedReason) const
{
    if (!Payload.IsValid()) return false;
    OutRevision = FMath::Max(0, FMath::RoundToInt(NumberOr(Payload, TEXT("revision"), 0.0)));
    OutToken = StringOr(Payload, TEXT("runtime_token"));
    OutCatalogVersion = StringOr(Payload, TEXT("catalog_version"));
    OutBlockedReason = StringOr(Payload, TEXT("blocked_reason"));

    const TSharedPtr<FJsonObject> Config = GetObject(Payload, TEXT("config"));
    if (!Config.IsValid()) return false;
    OutConfig.bEnabled = BoolOr(Config, TEXT("enabled"), false);
    OutConfig.bAutoEnter = BoolOr(Config, TEXT("auto_enter"), false);
    const TSharedPtr<FJsonObject> Minimap = GetObject(Config, TEXT("minimap"));
    OutConfig.bMinimapEnabled = BoolOr(Minimap, TEXT("enabled"), false);
    OutConfig.CharacterId = StringOr(Config, TEXT("character_id"));
    OutConfig.DefaultSkinId = StringOr(Config, TEXT("default_skin_id"));

    const TSharedPtr<FJsonObject> Spawn = GetObject(Config, TEXT("spawn_ue"));
    if (OutConfig.bEnabled && !Spawn.IsValid()) return false;
    OutConfig.bSpawnFromAnchor = StringOr(Spawn, TEXT("mode")) == TEXT("ue_anchor");
    OutConfig.SpawnAnchorId = StringOr(Spawn, TEXT("anchor_id"));
    OutConfig.SpawnLocation.X = NumberOr(Spawn, TEXT("x_cm"), 0.0);
    OutConfig.SpawnLocation.Y = NumberOr(Spawn, TEXT("y_cm"), 0.0);
    OutConfig.SpawnTraceOriginZCm = NumberOr(Spawn, TEXT("trace_origin_z_cm"), 1000.0);
    OutConfig.SpawnYawDeg = NumberOr(Spawn, TEXT("yaw_deg"), 0.0);
    double ZHint = 0.0;
    OutConfig.bHasZHint = Spawn.IsValid()
        && Spawn->TryGetNumberField(TEXT("z_hint_cm"), ZHint);
    if (!OutConfig.bHasZHint && Spawn.IsValid())
    {
        double ZHintMm = 0.0;
        if (Spawn->TryGetNumberField(TEXT("z_hint_mm"), ZHintMm))
        {
            ZHint = ZHintMm / 10.0;
            OutConfig.bHasZHint = true;
        }
    }
    OutConfig.ZHintCm = ZHint;

    const TSharedPtr<FJsonObject> Movement = GetObject(Config, TEXT("movement"));
    OutConfig.Movement.WalkSpeedCmS = NumberOr(Movement, TEXT("walk_speed_cm_s"), 250.0);
    OutConfig.Movement.SprintSpeedCmS = NumberOr(Movement, TEXT("sprint_speed_cm_s"), 500.0);
    OutConfig.Movement.AutoRouteSpeedCmS = NumberOr(Movement, TEXT("auto_route_speed_cm_s"), 180.0);
    OutConfig.Movement.JumpHeightCm = NumberOr(Movement, TEXT("jump_height_cm"), 80.0);

    const TSharedPtr<FJsonObject> Camera = GetObject(Config, TEXT("camera"));
    const FString DefaultCameraMode = StringOr(Camera, TEXT("default_mode"));
    if (DefaultCameraMode == TEXT("first_person"))
    {
        OutConfig.DefaultCameraMode = ETwinRoamingCameraMode::FirstPerson;
    }
    else if (DefaultCameraMode == TEXT("god"))
    {
        OutConfig.DefaultCameraMode = ETwinRoamingCameraMode::God;
    }
    else
    {
        OutConfig.DefaultCameraMode = ETwinRoamingCameraMode::God;
    }
    const TSharedPtr<FJsonObject> FirstPerson = GetObject(Camera, TEXT("first_person"));
    OutConfig.FirstPersonCamera.EyeHeightCm = NumberOr(
        FirstPerson, TEXT("eye_height_cm"), 165.0);
    OutConfig.FirstPersonCamera.FovDeg = NumberOr(
        FirstPerson, TEXT("fov_deg"), 85.0);
    OutConfig.FirstPersonCamera.LookSensitivity = NumberOr(
        FirstPerson, TEXT("look_sensitivity"), 1.0);
    const TSharedPtr<FJsonObject> Near = GetObject(Camera, TEXT("near_follow"));
    OutConfig.NearCamera.DistanceCm = NumberOr(Near, TEXT("distance_cm"), 120.0);
    OutConfig.NearCamera.HeightCm = NumberOr(Near, TEXT("height_cm"), 35.0);
    OutConfig.NearCamera.LookSensitivity = NumberOr(Near, TEXT("look_sensitivity"), 1.0);
    const TSharedPtr<FJsonObject> God = GetObject(Camera, TEXT("god"));
    OutConfig.GodCamera.CameraId = StringOr(God, TEXT("camera_id"));
    OutConfig.GodCamera.MoveSpeedCmS = NumberOr(God, TEXT("move_speed_cm_s"), 1800.0);
    OutConfig.GodCamera.LookSensitivity = NumberOr(God, TEXT("look_sensitivity"), 1.0);

    const TSharedPtr<FJsonObject> RouteConfig = GetObject(Config, TEXT("route"));
    OutConfig.bRouteEnabled = BoolOr(RouteConfig, TEXT("enabled"), false);
    OutConfig.bRouteAutoStart = BoolOr(RouteConfig, TEXT("auto_start"), true);
    OutConfig.bTakeoverEnabled = BoolOr(RouteConfig, TEXT("takeover_enabled"), true);
    OutConfig.bRouteLoop = StringOr(RouteConfig, TEXT("completion_mode")) == TEXT("loop");
    OutConfig.RouteId = StringOr(RouteConfig, TEXT("route_id"));

    const TSharedPtr<FJsonObject> Resources = GetObject(Payload, TEXT("resources"));
    const TSharedPtr<FJsonObject> Character = GetObject(Resources, TEXT("character"));
    OutConfig.CharacterDisplayName = StringOr(
        Character, TEXT("display_name"), OutConfig.CharacterId);
    OutConfig.CharacterPrimaryAssetId = StringOr(Character, TEXT("ue_primary_asset_id"));
    const TArray<TSharedPtr<FJsonValue>>* Skins = nullptr;
    if (Resources.IsValid() && Resources->TryGetArrayField(TEXT("skins"), Skins) && Skins)
    {
        for (const TSharedPtr<FJsonValue>& Value : *Skins)
        {
            const TSharedPtr<FJsonObject> Skin = Value.IsValid() ? Value->AsObject() : nullptr;
            const FString Id = StringOr(Skin, TEXT("id"));
            const FString PrimaryId = StringOr(Skin, TEXT("ue_primary_asset_id"));
            if (!Id.IsEmpty() && !PrimaryId.IsEmpty()) OutConfig.SkinPrimaryAssetIds.Add(Id, PrimaryId);
        }
    }
    const TArray<TSharedPtr<FJsonValue>>* AvailableCharacters = nullptr;
    if (Resources.IsValid()
        && Resources->TryGetArrayField(TEXT("available_characters"), AvailableCharacters)
        && AvailableCharacters)
    {
        for (const TSharedPtr<FJsonValue>& Value : *AvailableCharacters)
        {
            const TSharedPtr<FJsonObject> CharacterObject =
                Value.IsValid() ? Value->AsObject() : nullptr;
            FTwinRoamingRuntimeCharacter RuntimeCharacter;
            RuntimeCharacter.CharacterId = StringOr(CharacterObject, TEXT("id"));
            RuntimeCharacter.DisplayName = StringOr(
                CharacterObject, TEXT("display_name"), RuntimeCharacter.CharacterId);
            RuntimeCharacter.PrimaryAssetId = StringOr(
                CharacterObject, TEXT("ue_primary_asset_id"));
            RuntimeCharacter.SkeletonId = StringOr(CharacterObject, TEXT("skeleton_id"));
            RuntimeCharacter.DefaultSkinId = StringOr(
                CharacterObject, TEXT("default_skin_id"));

            const TArray<TSharedPtr<FJsonValue>>* CharacterSkins = nullptr;
            if (CharacterObject.IsValid()
                && CharacterObject->TryGetArrayField(TEXT("skins"), CharacterSkins)
                && CharacterSkins)
            {
                for (const TSharedPtr<FJsonValue>& SkinValue : *CharacterSkins)
                {
                    const TSharedPtr<FJsonObject> SkinObject =
                        SkinValue.IsValid() ? SkinValue->AsObject() : nullptr;
                    const FString SkinId = StringOr(SkinObject, TEXT("id"));
                    const FString SkinPrimaryAssetId = StringOr(
                        SkinObject, TEXT("ue_primary_asset_id"));
                    if (!SkinId.IsEmpty() && !SkinPrimaryAssetId.IsEmpty())
                    {
                        RuntimeCharacter.SkinPrimaryAssetIds.Add(
                            SkinId, SkinPrimaryAssetId);
                    }
                }
            }

            if (!RuntimeCharacter.CharacterId.IsEmpty()
                && !RuntimeCharacter.PrimaryAssetId.IsEmpty())
            {
                OutConfig.AvailableCharacters.Add(MoveTemp(RuntimeCharacter));
            }
        }
    }
    bool bCurrentCharacterListed = false;
    for (const FTwinRoamingRuntimeCharacter& RuntimeCharacter : OutConfig.AvailableCharacters)
    {
        if (RuntimeCharacter.CharacterId == OutConfig.CharacterId)
        {
            bCurrentCharacterListed = true;
            break;
        }
    }
    if (!bCurrentCharacterListed
        && !OutConfig.CharacterId.IsEmpty()
        && !OutConfig.CharacterPrimaryAssetId.IsEmpty())
    {
        FTwinRoamingRuntimeCharacter CurrentCharacter;
        CurrentCharacter.CharacterId = OutConfig.CharacterId;
        CurrentCharacter.DisplayName = OutConfig.CharacterDisplayName;
        CurrentCharacter.PrimaryAssetId = OutConfig.CharacterPrimaryAssetId;
        CurrentCharacter.DefaultSkinId = OutConfig.DefaultSkinId;
        CurrentCharacter.SkinPrimaryAssetIds = OutConfig.SkinPrimaryAssetIds;
        OutConfig.AvailableCharacters.Insert(MoveTemp(CurrentCharacter), 0);
    }
    const TSharedPtr<FJsonObject> RouteResource = GetObject(Resources, TEXT("route"));
    OutConfig.RouteDisplayName = StringOr(RouteResource, TEXT("display_name"));
    if (OutConfig.bRouteEnabled)
    {
        OutConfig.RouteId = StringOr(RouteResource, TEXT("ue_route_id"), OutConfig.RouteId);
    }

    const TSharedPtr<FJsonObject> RuntimeRoute = GetObject(Payload, TEXT("runtime_route"));
    if (OutConfig.bRouteEnabled && RuntimeRoute.IsValid())
    {
        FTwinRoamingRuntimeRoute DefaultRoute;
        if (!ParseRuntimeRouteObject(RuntimeRoute, DefaultRoute)) return false;
        DefaultRoute.DisplayName = OutConfig.RouteDisplayName.IsEmpty()
            ? DefaultRoute.DisplayName : OutConfig.RouteDisplayName;
        DefaultRoute.bDefault = true;
        ApplyRuntimeRouteToConfig(OutConfig, DefaultRoute);
    }

    const TArray<TSharedPtr<FJsonValue>>* AvailableRoutes = nullptr;
    if (Payload->TryGetArrayField(TEXT("available_routes"), AvailableRoutes)
        && AvailableRoutes)
    {
        for (const TSharedPtr<FJsonValue>& Value : *AvailableRoutes)
        {
            const TSharedPtr<FJsonObject> RouteObject =
                Value.IsValid() ? Value->AsObject() : nullptr;
            FTwinRoamingRuntimeRoute Route;
            if (ParseRuntimeRouteObject(RouteObject, Route))
            {
                OutConfig.AvailableRoutes.Add(MoveTemp(Route));
            }
        }
    }
    if (OutConfig.bHasRuntimeRoute
        && !FindRuntimeRoute(OutConfig.AvailableRoutes, OutConfig.RouteId))
    {
        FTwinRoamingRuntimeRoute DefaultRoute;
        DefaultRoute.RouteId = OutConfig.RouteId;
        DefaultRoute.DisplayName = OutConfig.RouteDisplayName;
        DefaultRoute.bDefault = true;
        DefaultRoute.Revision = OutConfig.RuntimeRouteRevision;
        DefaultRoute.Level = OutConfig.RuntimeRouteLevel;
        DefaultRoute.GroundZHintCm = OutConfig.RuntimeRouteGroundZHintCm;
        DefaultRoute.SpeedCmS = OutConfig.Movement.AutoRouteSpeedCmS;
        DefaultRoute.bLoop = OutConfig.bRouteLoop;
        DefaultRoute.Points = OutConfig.RuntimeRoutePoints;
        DefaultRoute.Waypoints = OutConfig.RuntimeRouteWaypoints;
        OutConfig.AvailableRoutes.Insert(MoveTemp(DefaultRoute), 0);
    }
    const TSharedPtr<FJsonObject> CameraResource = GetObject(Resources, TEXT("god_camera"));
    OutConfig.GodCamera.CameraId = StringOr(
        CameraResource, TEXT("ue_camera_id"), OutConfig.GodCamera.CameraId);
    return true;
}

void UTwinInteractionManagerComponent::HandleRuntimeProjection(const TSharedPtr<FJsonObject>& Payload)
{
    FTwinRoamingRuntimeConfig Incoming;
    int32 Revision = 0;
    FString Token;
    FString IncomingCatalogVersion;
    FString BlockedReason;
    if (!ParseRuntimeConfig(Payload, Incoming, Revision, Token, IncomingCatalogVersion, BlockedReason))
    {
        RuntimeState = TEXT("blocked");
        LastError = TEXT("Runtime projection is incomplete");
        return;
    }

    const TSharedPtr<FJsonObject> Binding = GetObject(Payload, TEXT("binding"));
    const FString BindingMode = StringOr(Binding, TEXT("mode"));
    BindingWarning = BindingMode == TEXT("unbound_dev")
        ? StringOr(Binding, TEXT("warning"), TEXT("当前项目尚未绑定 UE 工程")) : FString();

    if (Incoming.bEnabled && !SessionSelectedRouteId.IsEmpty())
    {
        const FTwinRoamingRuntimeRoute* SessionRoute = FindRuntimeRoute(
            Incoming.AvailableRoutes, SessionSelectedRouteId);
        if (SessionRoute && IsRuntimeRouteForCurrentLevel(*SessionRoute))
        {
            ApplyRuntimeRouteToConfig(Incoming, *SessionRoute);
        }
        else
        {
            SessionSelectedRouteId.Reset();
        }
    }

    CatalogVersion = IncomingCatalogVersion;
    if (!Incoming.bEnabled)
    {
        CurrentConfig = Incoming;
        AppliedRevision = Revision;
        RuntimeToken = Token;
        if (bRoamingActive) ExitRoaming();
        else ApplyMinimapConfig(false);
        RuntimeState = BlockedReason.IsEmpty() ? TEXT("disabled") : TEXT("blocked");
        LastError = BlockedReason;
        return;
    }

    if (RuntimeToken.IsEmpty() || !bRoamingActive)
    {
        CurrentConfig = Incoming;
        AppliedRevision = Revision;
        RuntimeToken = Token;
        RuntimeState = TEXT("available");
        LastError.Reset();
        ApplyMinimapConfig(CurrentConfig.bMinimapEnabled);
        if (Incoming.bAutoEnter && !bDefaultModeApplied && bSceneBaselineReady)
        {
            FString Error;
            if (!EnterRoaming(Error))
            {
                RuntimeState = TEXT("blocked");
                LastError = Error;
            }
        }
        else if (Incoming.bAutoEnter && !bSceneBaselineReady)
        {
            RuntimeState = TEXT("waiting_for_scene");
        }
        return;
    }

    if (Token == RuntimeToken) return;
    if (IsStructuralChange(CurrentConfig, Incoming))
    {
        PendingConfig = Incoming;
        PendingRevision = Revision;
        PendingRuntimeToken = Token;
        bPendingReload = true;
        RuntimeState = TEXT("reload_required");
    }
    else
    {
        CurrentConfig = Incoming;
        AppliedRevision = Revision;
        RuntimeToken = Token;
        ApplyHotConfig(CurrentConfig);
    }
}

bool UTwinInteractionManagerComponent::IsStructuralChange(
    const FTwinRoamingRuntimeConfig& A,
    const FTwinRoamingRuntimeConfig& B) const
{
    return A.CharacterPrimaryAssetId != B.CharacterPrimaryAssetId
        || A.bSpawnFromAnchor != B.bSpawnFromAnchor
        || A.SpawnAnchorId != B.SpawnAnchorId
        || !A.SpawnLocation.Equals(B.SpawnLocation, 0.1f)
        || !FMath::IsNearlyEqual(A.SpawnYawDeg, B.SpawnYawDeg, 0.1f)
        || A.bRouteEnabled != B.bRouteEnabled
        || A.RouteId != B.RouteId
        || A.bHasRuntimeRoute != B.bHasRuntimeRoute
        || A.RuntimeRouteRevision != B.RuntimeRouteRevision
        || A.RuntimeRouteLevel != B.RuntimeRouteLevel
        || A.GodCamera.CameraId != B.GodCamera.CameraId;
}

void UTwinInteractionManagerComponent::ApplyHotConfig(const FTwinRoamingRuntimeConfig& Config)
{
    bTakeoverEnabled = Config.bTakeoverEnabled;
    ApplyMinimapConfig(Config.bMinimapEnabled);
    if (!RoamingCharacter) return;
    RoamingCharacter->ApplyMovementSettings(Config.Movement);
    RoamingCharacter->CameraMode->Configure(
        Config.FirstPersonCamera, Config.NearCamera, Config.GodCamera);
    RoamingCharacter->RouteFollower->SetSpeed(Config.Movement.AutoRouteSpeedCmS);
    RoamingCharacter->RouteFollower->SetLoop(Config.bRouteLoop);
    const bool bUsingSessionCharacter = !ActiveSessionCharacterId.IsEmpty()
        && ActiveSessionCharacterId != Config.CharacterId;
    if (RoamingCharacter->SkinComponent)
    {
        const TMap<FString, FString>& SkinMap = bUsingSessionCharacter
            ? ActiveSessionSkinPrimaryAssetIds : Config.SkinPrimaryAssetIds;
        const FString& DefaultSkinId = bUsingSessionCharacter
            ? ActiveSessionDefaultSkinId : Config.DefaultSkinId;
        RoamingCharacter->SkinComponent->Configure(SkinMap, DefaultSkinId);
        if (RoamingCharacter->SkinComponent->GetActiveSkinId().IsEmpty())
        {
            FString SkinError;
            if (!SkinMap.IsEmpty() && !DefaultSkinId.IsEmpty()
                && !RoamingCharacter->SkinComponent->ApplyDefaultSkin(SkinError))
            {
                DegradedFeatures.AddUnique(TEXT("default_skin_missing"));
            }
            else if (SkinMap.IsEmpty())
            {
                RoamingCharacter->SkinComponent->ClearSkinOverrides();
                DegradedFeatures.Remove(TEXT("default_skin_missing"));
            }
        }
        RoamingCharacter->RefreshLocomotionAnimation();
    }
}

UObject* UTwinInteractionManagerComponent::ResolvePrimaryAsset(const FString& PrimaryAssetId) const
{
    const FPrimaryAssetId AssetId = FPrimaryAssetId::FromString(PrimaryAssetId);
    if (!AssetId.IsValid()) return nullptr;
    UAssetManager& AssetManager = UAssetManager::Get();
    if (UObject* Existing = AssetManager.GetPrimaryAssetObject(AssetId)) return Existing;
    const FSoftObjectPath AssetPath = AssetManager.GetPrimaryAssetPath(AssetId);
    return AssetPath.IsValid() ? AssetPath.TryLoad() : nullptr;
}

const FTwinRoamingRuntimeCharacter* UTwinInteractionManagerComponent::FindRuntimeCharacter(
    const FString& CharacterId) const
{
    return CurrentConfig.AvailableCharacters.FindByPredicate(
        [&CharacterId](const FTwinRoamingRuntimeCharacter& Character)
        {
            return Character.CharacterId == CharacterId;
        });
}

UTwinCharacterAsset* UTwinInteractionManagerComponent::ResolveCharacterAsset(FString& OutError) const
{
    UTwinCharacterAsset* Asset = Cast<UTwinCharacterAsset>(
        ResolvePrimaryAsset(CurrentConfig.CharacterPrimaryAssetId));
    if (!Asset)
    {
        OutError = FString::Printf(
            TEXT("Character asset %s is missing. Create the UTwinCharacterAsset and configure Asset Manager scan rules."),
            *CurrentConfig.CharacterPrimaryAssetId);
    }
    return Asset;
}

bool UTwinInteractionManagerComponent::ResolveSpawnTransform(
    UTwinCharacterAsset* CharacterAsset,
    FTransform& OutTransform,
    FString& OutError) const
{
    if (!CharacterAsset || !GetWorld())
    {
        OutError = TEXT("Cannot resolve the character spawn point");
        return false;
    }

    FVector SourceLocation = CurrentConfig.SpawnLocation;
    FRotator SourceRotation(0.0f, CurrentConfig.SpawnYawDeg, 0.0f);
    const ATwinRoamingSpawnAnchor* SpawnAnchor = nullptr;
    // An enabled runtime route owns the spawn position independently of
    // auto-start.  Auto-start only decides whether the follower begins moving
    // after the character has been placed at the route's first point.
    const bool bSpawnFromRuntimeRoute = CurrentConfig.bRouteEnabled
        && CurrentConfig.bHasRuntimeRoute
        && ActiveRoute
        && ActiveRoute->Spline
        && ActiveRoute->Spline->GetNumberOfSplinePoints() >= 2;
    if (bSpawnFromRuntimeRoute)
    {
        SourceLocation = ActiveRoute->Spline->GetLocationAtSplinePoint(
            0, ESplineCoordinateSpace::World);
        SourceRotation = ActiveRoute->Spline->GetDirectionAtSplinePoint(
            0, ESplineCoordinateSpace::World).Rotation();
    }
    else if (CurrentConfig.bSpawnFromAnchor)
    {
        SpawnAnchor = FindSpawnAnchor(CurrentConfig.SpawnAnchorId);
        if (!SpawnAnchor)
        {
            OutError = FString::Printf(
                TEXT("Configured roaming spawn anchor was not found: %s"),
                *CurrentConfig.SpawnAnchorId);
            return false;
        }
        SourceLocation = SpawnAnchor->GetActorLocation();
        SourceRotation = SpawnAnchor->GetActorRotation();
    }

    const bool bProjectToGround = bSpawnFromRuntimeRoute || !SpawnAnchor || SpawnAnchor->bProjectToGround;
    const FVector TraceStart(
        SourceLocation.X,
        SourceLocation.Y,
        bSpawnFromRuntimeRoute
            ? SourceLocation.Z + RuntimeRouteTraceUpCm
            : CurrentConfig.bSpawnFromAnchor
            ? SourceLocation.Z + 100.0f
            : CurrentConfig.SpawnTraceOriginZCm);
    const FVector TraceEnd = bSpawnFromRuntimeRoute
        ? SourceLocation - FVector(0.0f, 0.0f, RuntimeRouteTraceDownCm)
        : CurrentConfig.bSpawnFromAnchor
        ? FVector(SourceLocation.X, SourceLocation.Y, SourceLocation.Z - 1000.0f)
        : TraceStart - FVector(0.0f, 0.0f, 200000.0f);
    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TwinRoamingSpawn), false, SceneManager);
    if (SpawnAnchor) QueryParams.AddIgnoredActor(SpawnAnchor);
    FHitResult GroundHit;
    FVector CapsuleCenter;
    bool bFoundWalkableGround = false;
    if (bProjectToGround)
    {
        // Visibility is a presentation/selection channel. A database proxy may
        // block it while deliberately ignoring Pawn, which would place the
        // character on a non-walkable proxy and make it fall forever. Only use
        // world geometry that explicitly blocks the character capsule and has
        // a walkable surface normal.
        FCollisionObjectQueryParams ObjectTypes;
        ObjectTypes.AddObjectTypesToQuery(ECC_WorldStatic);
        ObjectTypes.AddObjectTypesToQuery(ECC_WorldDynamic);
        TArray<FHitResult> GroundHits;
        if (GetWorld()->LineTraceMultiByObjectType(
            GroundHits, TraceStart, TraceEnd, ObjectTypes, QueryParams))
        {
            for (const FHitResult& Hit : GroundHits)
            {
                const UPrimitiveComponent* Component = Hit.GetComponent();
                if (!Hit.bBlockingHit || !Component
                    || Hit.ImpactNormal.Z < 0.55f
                    || Component->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block)
                {
                    continue;
                }
                GroundHit = Hit;
                bFoundWalkableGround = true;
                break;
            }
        }
    }
    if (bFoundWalkableGround)
    {
        CapsuleCenter = GroundHit.ImpactPoint + FVector(
            0.0f, 0.0f, CharacterAsset->CapsuleHalfHeightCm + 2.0f);
    }
    else if (SpawnAnchor && !bProjectToGround)
    {
        CapsuleCenter = SourceLocation;
    }
    else if (bSpawnFromRuntimeRoute)
    {
        // Runtime route points have already been projected in BuildRuntimeRoute.
        // A host floor may block Pawn while intentionally ignoring Visibility,
        // so the route point itself is a valid spawn-height fallback.
        CapsuleCenter = SourceLocation + FVector(
            0.0f, 0.0f, CharacterAsset->CapsuleHalfHeightCm + 2.0f);
        UE_LOG(LogTemp, Warning,
            TEXT("OntoTwin runtime route spawn Visibility trace missed; using projected route Z %.1f cm"),
            SourceLocation.Z);
    }
    else if (!CurrentConfig.bSpawnFromAnchor && CurrentConfig.bHasZHint)
    {
        CapsuleCenter = FVector(
            SourceLocation.X,
            SourceLocation.Y,
            CurrentConfig.ZHintCm + CharacterAsset->CapsuleHalfHeightCm);
    }
    else
    {
        OutError = bSpawnFromRuntimeRoute
            ? TEXT("Runtime route start could not resolve walkable ground")
            : CurrentConfig.bSpawnFromAnchor
            ? TEXT("Spawn anchor could not find visible ground within 10 meters below its local position")
            : TEXT("Spawn ground trace failed and no z_hint_cm fallback is configured");
        return false;
    }

    const FCollisionShape Capsule = FCollisionShape::MakeCapsule(
        CharacterAsset->CapsuleRadiusCm, CharacterAsset->CapsuleHalfHeightCm);
    TArray<FOverlapResult> BlockingOverlaps;
    if (GetWorld()->OverlapMultiByChannel(
        BlockingOverlaps,
        CapsuleCenter,
        FQuat::Identity,
        ECC_Pawn,
        Capsule,
        QueryParams))
    {
        for (const FOverlapResult& Overlap : BlockingOverlaps)
        {
            const AActor* BlockingActor = Overlap.GetActor();
            const UPrimitiveComponent* BlockingComponent = Overlap.GetComponent();
            UE_LOG(LogTemp, Warning,
                TEXT("OntoTwin roaming spawn overlap actor=%s component=%s location=(%.1f,%.1f,%.1f) capsule=(r=%.1f,h=%.1f)"),
                BlockingActor ? *BlockingActor->GetName() : TEXT("none"),
                BlockingComponent ? *BlockingComponent->GetName() : TEXT("none"),
                CapsuleCenter.X,
                CapsuleCenter.Y,
                CapsuleCenter.Z,
                CharacterAsset->CapsuleRadiusCm,
                CharacterAsset->CapsuleHalfHeightCm);
        }
        OutError = TEXT("Character capsule overlaps scene collision at the configured spawn point");
        return false;
    }
    OutTransform = FTransform(
        FRotator(0.0f, SourceRotation.Yaw, 0.0f), CapsuleCenter);
    return true;
}

ATwinRoamingRoute* UTwinInteractionManagerComponent::FindRoute(const FString& RouteId) const
{
    if (!GetWorld() || RouteId.IsEmpty()) return nullptr;
    for (TActorIterator<ATwinRoamingRoute> It(GetWorld()); It; ++It)
    {
        if (It->RouteId == RouteId) return *It;
    }
    return nullptr;
}

bool UTwinInteractionManagerComponent::ProjectRuntimeRoutePointToGround(
    const FVector& Source,
    FVector& OutGroundPoint,
    FString& OutError,
    int32 PointIndex) const
{
    if (!GetWorld())
    {
        OutError = TEXT("Runtime route cannot access the current world");
        return false;
    }

    // Routes in 4.0 target a calibrated floor. Keep the search close to that
    // floor hint so horizontal roofs and upper platforms cannot win merely
    // because they also have walkable collision.
    const float TraceUpCm = FMath::Max(RuntimeRouteTraceUpCm, 10.0f);
    const float TraceDownCm = FMath::Max(RuntimeRouteTraceDownCm, 10.0f);
    const FVector TraceStart = Source + FVector(0.0f, 0.0f, TraceUpCm);
    const FVector TraceEnd = Source - FVector(0.0f, 0.0f, TraceDownCm);
    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TwinRuntimeRouteGround), true, SceneManager);
    if (RuntimeRouteActor) QueryParams.AddIgnoredActor(RuntimeRouteActor);
    if (RoamingCharacter) QueryParams.AddIgnoredActor(RoamingCharacter);
    FCollisionObjectQueryParams ObjectTypes;
    ObjectTypes.AddObjectTypesToQuery(ECC_WorldStatic);
    ObjectTypes.AddObjectTypesToQuery(ECC_WorldDynamic);

    TArray<FHitResult> Hits;
    if (!GetWorld()->LineTraceMultiByObjectType(
        Hits, TraceStart, TraceEnd, ObjectTypes, QueryParams))
    {
        OutError = FString::Printf(
            TEXT("Runtime route point %d found no collidable walkable ground at UE XY (%.1f, %.1f)"),
            PointIndex + 1, Source.X, Source.Y);
        UE_LOG(LogTemp, Error, TEXT("OntoTwin %s"), *OutError);
        return false;
    }

    const FHitResult* BestHit = nullptr;
    float BestDistanceToHint = TNumericLimits<float>::Max();
    for (const FHitResult& Hit : Hits)
    {
        const UPrimitiveComponent* Component = Hit.GetComponent();
        if (!Hit.bBlockingHit || !Component
            || Hit.ImpactNormal.Z < RuntimeRouteMinGroundNormalZ
            || Component->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block)
        {
            continue;
        }
        const float DistanceToHint = FMath::Abs(
            Hit.ImpactPoint.Z - CurrentConfig.RuntimeRouteGroundZHintCm);
        if (DistanceToHint < BestDistanceToHint)
        {
            BestDistanceToHint = DistanceToHint;
            BestHit = &Hit;
        }
    }
    if (!BestHit)
    {
        OutError = FString::Printf(
            TEXT("Runtime route point %d only hit non-walkable surfaces at UE XY (%.1f, %.1f)"),
            PointIndex + 1, Source.X, Source.Y);
        UE_LOG(LogTemp, Error, TEXT("OntoTwin %s"), *OutError);
        return false;
    }
    OutGroundPoint = BestHit->ImpactPoint;
    return true;
}

ATwinRoamingRoute* UTwinInteractionManagerComponent::BuildRuntimeRoute(FString& OutError)
{
    DestroyRuntimeRoute();
    if (!CurrentConfig.bHasRuntimeRoute) return nullptr;
    if (!GetWorld() || !GetWorld()->PersistentLevel)
    {
        OutError = TEXT("Runtime route cannot resolve the current UE level");
        return nullptr;
    }
    if (CurrentConfig.RuntimeRoutePoints.Num() < 2)
    {
        OutError = TEXT("Runtime route requires at least two points");
        return nullptr;
    }

    const FString CurrentLevel = NormalizeLevelPackageName(
        GetWorld()->PersistentLevel->GetOutermost()->GetName());
    const FString RequiredLevel = NormalizeLevelPackageName(CurrentConfig.RuntimeRouteLevel);
    if (RequiredLevel.IsEmpty() || !CurrentLevel.Equals(RequiredLevel, ESearchCase::CaseSensitive))
    {
        OutError = FString::Printf(
            TEXT("Runtime route level mismatch: current %s, configured %s"),
            *CurrentLevel,
            RequiredLevel.IsEmpty() ? TEXT("<empty>") : *RequiredLevel);
        return nullptr;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = SceneManager;
    SpawnParams.ObjectFlags |= RF_Transient;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    RuntimeRouteActor = GetWorld()->SpawnActor<ATwinRoamingRoute>(
        ATwinRoamingRoute::StaticClass(), FTransform::Identity, SpawnParams);
    if (!RuntimeRouteActor || !RuntimeRouteActor->Spline)
    {
        RuntimeRouteActor = nullptr;
        OutError = TEXT("Runtime route actor could not be created");
        return nullptr;
    }

    RuntimeRouteActor->RouteId = CurrentConfig.RouteId;
    RuntimeRouteActor->bRuntimeGenerated = true;
    RuntimeRouteActor->bSplineAtGroundLevel = true;
    RuntimeRouteActor->SetActorEnableCollision(false);
    RuntimeRouteActor->Spline->ClearSplinePoints(false);
    for (int32 Index = 0; Index < CurrentConfig.RuntimeRoutePoints.Num(); ++Index)
    {
        FVector GroundPoint;
        if (!ProjectRuntimeRoutePointToGround(
            CurrentConfig.RuntimeRoutePoints[Index], GroundPoint, OutError, Index))
        {
            DestroyRuntimeRoute();
            return nullptr;
        }
        RuntimeRouteActor->Spline->AddSplinePoint(
            GroundPoint, ESplineCoordinateSpace::World, false);
        RuntimeRouteActor->Spline->SetSplinePointType(
            Index, ESplinePointType::Linear, false);
    }
    RuntimeRouteActor->Spline->SetClosedLoop(CurrentConfig.bRouteLoop, false);
    RuntimeRouteActor->Spline->UpdateSpline();
    UE_LOG(LogTemp, Log, TEXT("OntoTwin runtime route %s revision %d created with %d points in %s"),
        *CurrentConfig.RouteId,
        CurrentConfig.RuntimeRouteRevision,
        CurrentConfig.RuntimeRoutePoints.Num(),
        *CurrentLevel);
    return RuntimeRouteActor;
}

bool UTwinInteractionManagerComponent::IsRuntimeRouteForCurrentLevel(
    const FTwinRoamingRuntimeRoute& Route) const
{
    if (!GetWorld() || !GetWorld()->PersistentLevel) return false;
    const FString CurrentLevel = NormalizeLevelPackageName(
        GetWorld()->PersistentLevel->GetOutermost()->GetName());
    const FString RequiredLevel = NormalizeLevelPackageName(Route.Level);
    return !RequiredLevel.IsEmpty()
        && CurrentLevel.Equals(RequiredLevel, ESearchCase::CaseSensitive);
}

void UTwinInteractionManagerComponent::DestroyRuntimeRoute()
{
    if (ActiveRoute == RuntimeRouteActor) ActiveRoute = nullptr;
    if (RuntimeRouteActor && IsValid(RuntimeRouteActor)) RuntimeRouteActor->Destroy();
    RuntimeRouteActor = nullptr;
}

ATwinRoamingSpawnAnchor* UTwinInteractionManagerComponent::FindSpawnAnchor(const FString& SpawnId) const
{
    if (!GetWorld() || SpawnId.IsEmpty()) return nullptr;
    for (TActorIterator<ATwinRoamingSpawnAnchor> It(GetWorld()); It; ++It)
    {
        if (It->SpawnId == SpawnId) return *It;
    }
    return nullptr;
}

ATwinGodViewAnchor* UTwinInteractionManagerComponent::FindGodViewAnchor(const FString& CameraId) const
{
    if (!GetWorld() || CameraId.IsEmpty()) return nullptr;
    for (TActorIterator<ATwinGodViewAnchor> It(GetWorld()); It; ++It)
    {
        if (It->CameraId == CameraId) return *It;
    }
    return nullptr;
}

void UTwinInteractionManagerComponent::ApplyStartupView(bool bForce)
{
    if (bShuttingDown || !PlayerController || (!bForce && bRoamingActive))
    {
        return;
    }

    OntoTwinCameraVisibility::ClearFromPlayer(
        PlayerController, StartupViewVisibilityState);
    StartupViewAnchor = FindGodViewAnchor(StartupViewCameraId);
    if (!StartupViewAnchor
        && StartupViewCameraId != TEXT("camera.god.default"))
    {
        StartupViewAnchor = FindGodViewAnchor(TEXT("camera.god.default"));
    }
    if (!StartupViewAnchor) return;

    TArray<AActor*> HiddenActors;
    OntoTwinCameraVisibility::ResolveHiddenActors(
        GetWorld(),
        ETwinCameraVisibilityProfile::GodView,
        StartupViewAnchor->HiddenActorNames,
        StartupViewAnchor->HiddenLevelNames,
        HiddenActors);
    OntoTwinCameraVisibility::ApplyToPlayer(
        PlayerController, HiddenActors, StartupViewVisibilityState);
    if (PlayerController->GetViewTarget() != StartupViewAnchor)
    {
        PlayerController->SetViewTarget(StartupViewAnchor);
        UE_LOG(LogTemp, Log,
            TEXT("OntoTwin startup view applied: camera_id=%s actor=%s"),
            *StartupViewAnchor->CameraId, *StartupViewAnchor->GetName());
    }

    // GameModeBase creates a visible engine sphere when no host-specific pawn
    // class is configured. Once the startup camera becomes the view target,
    // bOwnerNoSee no longer protects that sphere from the active camera. Hide
    // only the exact native class; project pawns and OntoTwin camera/roaming
    // pawns must remain untouched.
    if (APawn* Pawn = PlayerController->GetPawn();
        Pawn && Pawn->GetClass() == ADefaultPawn::StaticClass())
    {
        const bool bNeedsSuppression = !Pawn->IsHidden() || Pawn->GetActorEnableCollision();
        Pawn->SetActorHiddenInGame(true);
        Pawn->SetActorEnableCollision(false);
        if (bNeedsSuppression)
        {
            UE_LOG(LogTemp, Log,
                TEXT("OntoTwin startup view suppressed native DefaultPawn: actor=%s"),
                *Pawn->GetName());
        }
    }
}

void UTwinInteractionManagerComponent::RestoreStartupView()
{
    ApplyStartupView(true);
}

void UTwinInteractionManagerComponent::NotifySceneBaselineReady()
{
    if (bSceneBaselineReady)
    {
        return;
    }

    bSceneBaselineReady = true;
    UE_LOG(LogTemp, Log, TEXT("OntoTwin scene baseline ready; roaming entry is now available"));

    if (!CurrentConfig.bEnabled || !CurrentConfig.bAutoEnter || bDefaultModeApplied)
    {
        if (CurrentConfig.bEnabled && RuntimeState == TEXT("waiting_for_scene"))
        {
            RuntimeState = TEXT("available");
        }
        RefreshHud();
        return;
    }

    FString Error;
    if (!EnterRoaming(Error))
    {
        RuntimeState = TEXT("blocked");
        LastError = Error;
        UE_LOG(LogTemp, Warning, TEXT("OntoTwin delayed roaming entry failed: %s"), *Error);
    }
}

ATwinMinimapAnchor* UTwinInteractionManagerComponent::FindMinimapAnchor(
    FString& OutState) const
{
    OutState = TEXT("anchor_missing");
    if (!GetWorld()) return nullptr;

    ATwinMinimapAnchor* Match = nullptr;
    int32 MatchCount = 0;
    for (TActorIterator<ATwinMinimapAnchor> It(GetWorld()); It; ++It)
    {
        if (It->MinimapId != TEXT("minimap.default")) continue;
        Match = *It;
        ++MatchCount;
    }
    if (MatchCount == 1) return Match;
    if (MatchCount > 1) OutState = TEXT("anchor_ambiguous");
    return nullptr;
}

void UTwinInteractionManagerComponent::SetMinimapState(const FString& State)
{
    MinimapState = State;
    DegradedFeatures.Remove(TEXT("minimap_anchor_missing"));
    DegradedFeatures.Remove(TEXT("minimap_anchor_ambiguous"));
    DegradedFeatures.Remove(TEXT("minimap_capture_failed"));
    if (State == TEXT("anchor_missing"))
    {
        DegradedFeatures.AddUnique(TEXT("minimap_anchor_missing"));
    }
    else if (State == TEXT("anchor_ambiguous"))
    {
        DegradedFeatures.AddUnique(TEXT("minimap_anchor_ambiguous"));
    }
    else if (State == TEXT("capture_failed"))
    {
        DegradedFeatures.AddUnique(TEXT("minimap_capture_failed"));
    }
}

void UTwinInteractionManagerComponent::ShutdownMinimap(bool bResetState)
{
    CancelMinimapTeleport(true);
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(MinimapRefreshTimer);
    }
    PendingMinimapRefreshReason.Reset();
    PendingMinimapRefreshCaptures = 0;
    if (RoamingHUD) RoamingHUD->ClearMinimap();
    if (MinimapCapture)
    {
        MinimapCapture->TextureTarget = nullptr;
        if (AActor* OwnerActor = GetOwner())
        {
            OwnerActor->RemoveInstanceComponent(MinimapCapture);
        }
        MinimapCapture->DestroyComponent();
    }
    MinimapCapture = nullptr;
    MinimapRenderTarget = nullptr;
    MinimapAnchor = nullptr;
    MinimapViewProjection = FMatrix::Identity;
    MinimapCaptureSize = FIntPoint::ZeroValue;
    MinimapMarkerAccumulator = 0.0f;
    if (bResetState) SetMinimapState(TEXT("disabled"));
}

void UTwinInteractionManagerComponent::ApplyMinimapConfig(bool bEnabled)
{
    if (!bEnabled || !CurrentConfig.bEnabled)
    {
        ShutdownMinimap(true);
        return;
    }
    if (!bRoamingActive || !RoamingCharacter || !RoamingHUD)
    {
        ShutdownMinimap(false);
        SetMinimapState(TEXT("waiting"));
        return;
    }
    if (MinimapState == TEXT("ready")
        && MinimapAnchor && MinimapRenderTarget && MinimapCapture)
    {
        return;
    }

    ShutdownMinimap(false);
    FString Error;
    if (!InitializeMinimap(Error))
    {
        UE_LOG(LogTemp, Warning, TEXT("OntoTwin minimap unavailable: %s"), *Error);
    }
}

bool UTwinInteractionManagerComponent::InitializeMinimap(FString& OutError)
{
    SetMinimapState(TEXT("capturing"));
    FString AnchorState;
    MinimapAnchor = FindMinimapAnchor(AnchorState);
    if (!MinimapAnchor)
    {
        SetMinimapState(AnchorState);
        OutError = AnchorState == TEXT("anchor_ambiguous")
            ? TEXT("Multiple minimap.default anchors exist in the current world")
            : TEXT("TwinMinimapAnchor minimap.default is missing in the current world");
        return false;
    }

    UCameraComponent* AnchorCamera = MinimapAnchor->GetCameraComponent();
    AActor* OwnerActor = GetOwner();
    if (!AnchorCamera || !OwnerActor || !GetWorld())
    {
        SetMinimapState(TEXT("capture_failed"));
        OutError = TEXT("Minimap camera or owner is unavailable");
        return false;
    }

    MinimapCaptureSize.X = FMath::Clamp(MinimapAnchor->CaptureWidth, 256, 2048);
    MinimapCaptureSize.Y = FMath::Clamp(MinimapAnchor->CaptureHeight, 256, 2048);
    MinimapRenderTarget = NewObject<UTextureRenderTarget2D>(
        this, NAME_None, RF_Transient);
    if (!MinimapRenderTarget)
    {
        SetMinimapState(TEXT("capture_failed"));
        OutError = TEXT("Minimap render target could not be created");
        return false;
    }
    MinimapRenderTarget->ClearColor = FLinearColor::Black;
    MinimapRenderTarget->InitCustomFormat(
        MinimapCaptureSize.X,
        MinimapCaptureSize.Y,
        PF_B8G8R8A8,
        false);
    MinimapRenderTarget->UpdateResourceImmediate(true);
    if (!MinimapRenderTarget->GameThread_GetRenderTargetResource())
    {
        ShutdownMinimap(false);
        SetMinimapState(TEXT("capture_failed"));
        OutError = TEXT("Minimap render target resource is unavailable");
        return false;
    }

    MinimapCapture = NewObject<USceneCaptureComponent2D>(
        OwnerActor, NAME_None, RF_Transient);
    if (!MinimapCapture)
    {
        ShutdownMinimap(false);
        SetMinimapState(TEXT("capture_failed"));
        OutError = TEXT("Minimap scene capture could not be created");
        return false;
    }
    OwnerActor->AddInstanceComponent(MinimapCapture);
    MinimapCapture->SetWorldTransform(MinimapAnchor->GetActorTransform());
    MinimapCapture->TextureTarget = MinimapRenderTarget;
    MinimapCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
    MinimapCapture->bCaptureEveryFrame = false;
    MinimapCapture->bCaptureOnMovement = false;
    MinimapCapture->bAlwaysPersistRenderingState = false;
    MinimapCapture->ProjectionType = AnchorCamera->ProjectionMode;
    MinimapCapture->FOVAngle = AnchorCamera->FieldOfView;
    MinimapCapture->OrthoWidth = AnchorCamera->OrthoWidth;
    MinimapCapture->bAutoCalculateOrthoPlanes = AnchorCamera->bAutoCalculateOrthoPlanes;
    MinimapCapture->AutoPlaneShift = AnchorCamera->AutoPlaneShift;
    MinimapCapture->bUpdateOrthoPlanes = AnchorCamera->bUpdateOrthoPlanes;
    MinimapCapture->bUseCameraHeightAsViewTarget = AnchorCamera->bUseCameraHeightAsViewTarget;
    MinimapCapture->PostProcessSettings = AnchorCamera->PostProcessSettings;
    MinimapCapture->PostProcessBlendWeight = AnchorCamera->PostProcessBlendWeight;
    MinimapCapture->HiddenActors.Add(RoamingCharacter);
    MinimapCapture->HideActorComponents(RoamingCharacter, true);
    if (RoamingCharacter->CameraMode)
    {
        if (ATwinGodViewPawn* GodPawn = RoamingCharacter->CameraMode->GetGodPawn())
        {
            MinimapCapture->HiddenActors.Add(GodPawn);
            MinimapCapture->HideActorComponents(GodPawn, true);
        }
    }
    FMinimalViewInfo ViewInfo;
    AnchorCamera->GetCameraView(0.0f, ViewInfo);
    const float VisibleFraction = 1.0f - 2.0f * FMath::Clamp(
        MinimapAnchor->CropFractionPerEdge, 0.0f, 0.45f);
    if (ViewInfo.ProjectionMode == ECameraProjectionMode::Perspective)
    {
        const float HalfFovRadians = FMath::DegreesToRadians(
            FMath::Clamp(ViewInfo.FOV, 5.0f, 170.0f)) * 0.5f;
        ViewInfo.FOV = FMath::RadiansToDegrees(
            2.0f * FMath::Atan(FMath::Tan(HalfFovRadians) * VisibleFraction));
        MinimapCapture->FOVAngle = ViewInfo.FOV;
    }
    else
    {
        ViewInfo.OrthoWidth *= VisibleFraction;
        MinimapCapture->OrthoWidth = ViewInfo.OrthoWidth;
    }
    ViewInfo.AspectRatio = static_cast<float>(MinimapCaptureSize.X)
        / static_cast<float>(MinimapCaptureSize.Y);
    ViewInfo.bConstrainAspectRatio = false;
    MinimapCapture->bUseCustomProjectionMatrix = true;
    MinimapCapture->CustomProjectionMatrix = ViewInfo.CalculateProjectionMatrix();
    const FMatrix ProjectionMatrix = AdjustProjectionMatrixForRHI(
        MinimapCapture->CustomProjectionMatrix);
    const FMatrix ViewRotationMatrix = FInverseRotationMatrix(ViewInfo.Rotation) * FMatrix(
        FPlane(0, 0, 1, 0),
        FPlane(1, 0, 0, 0),
        FPlane(0, 1, 0, 0),
        FPlane(0, 0, 0, 1));
    const FMatrix ViewMatrix = FTranslationMatrix(-ViewInfo.Location) * ViewRotationMatrix;
    MinimapViewProjection = ViewMatrix * ProjectionMatrix;

    MinimapCapture->RegisterComponent();
    if (!CaptureMinimapScene(TEXT("initial")))
    {
        ShutdownMinimap(false);
        SetMinimapState(TEXT("capture_failed"));
        OutError = TEXT("Minimap scene capture could not be submitted");
        return false;
    }

    RoamingHUD->SetMinimapTexture(MinimapRenderTarget, MinimapCaptureSize);
    SetMinimapState(TEXT("ready"));
    MinimapMarkerAccumulator = 1000.0f;
    // The scene baseline can create hundreds of primitive proxies in the same
    // game-thread frame as this capture component. Re-capture a few times while
    // the render thread settles instead of requiring an F7 exit/re-entry.
    ScheduleMinimapRefreshSeries(TEXT("scene_settled"), 3, 0.25f);
    return true;
}

bool UTwinInteractionManagerComponent::CaptureMinimapScene(const TCHAR* Reason)
{
    if (!GetWorld() || !MinimapAnchor || !MinimapCapture
        || !MinimapCapture->IsRegistered() || !MinimapRenderTarget)
    {
        return false;
    }

    // Re-resolve the profile for every one-shot capture so actors streamed in
    // after the initial setup receive the same minimap-only filtering.
    TArray<AActor*> VisibilityHiddenActors;
    OntoTwinCameraVisibility::ResolveHiddenActors(
        GetWorld(),
        ETwinCameraVisibilityProfile::Minimap,
        MinimapAnchor->HiddenActorNames,
        MinimapAnchor->HiddenLevelNames,
        VisibilityHiddenActors);
    for (AActor* HiddenActor : VisibilityHiddenActors)
    {
        if (!IsValid(HiddenActor)) continue;
        MinimapCapture->HiddenActors.AddUnique(HiddenActor);
        MinimapCapture->HideActorComponents(HiddenActor, true);
    }

    TArray<TWeakObjectPtr<ULightComponent>> SuppressedLights;
    OntoTwinCameraVisibility::SuppressLights(
        VisibilityHiddenActors, SuppressedLights);
    if (!MinimapAnchor->CaptureSuppressedLightTag.IsNone())
    {
        for (TActorIterator<ALight> It(GetWorld()); It; ++It)
        {
            ALight* LightActor = *It;
            ULightComponent* LightComponent = LightActor
                ? LightActor->GetLightComponent()
                : nullptr;
            if (LightActor
                && LightActor->ActorHasTag(MinimapAnchor->CaptureSuppressedLightTag)
                && LightComponent
                && LightComponent->IsVisible())
            {
                SuppressedLights.Add(LightComponent);
                LightComponent->SetVisibility(false);
            }
        }
    }

    MinimapCapture->CaptureScene();

    // CaptureScene pushes the hidden-light state before submitting this one-shot capture.
    // Restore immediately so the main viewport never inherits the minimap-only lighting.
    OntoTwinCameraVisibility::RestoreLights(SuppressedLights);
    UE_LOG(LogTemp, Log,
        TEXT("OntoTwin minimap captured: reason=%s hidden_actors=%d hidden_primitives=%d"),
        Reason ? Reason : TEXT("unknown"),
        MinimapCapture->HiddenActors.Num(),
        MinimapCapture->HiddenComponents.Num());
    return true;
}

void UTwinInteractionManagerComponent::ScheduleMinimapRefreshSeries(
    const TCHAR* Reason,
    int32 CaptureCount,
    float InitialDelaySeconds)
{
    if (!GetWorld() || !bRoamingActive || MinimapState != TEXT("ready")
        || !MinimapCapture || !MinimapRenderTarget)
    {
        return;
    }

    PendingMinimapRefreshReason = Reason ? Reason : TEXT("scene_settled");
    PendingMinimapRefreshCaptures = FMath::Max(1, CaptureCount);
    GetWorld()->GetTimerManager().ClearTimer(MinimapRefreshTimer);
    GetWorld()->GetTimerManager().SetTimer(
        MinimapRefreshTimer,
        this,
        &UTwinInteractionManagerComponent::RefreshMinimapAfterSceneSettled,
        FMath::Max(0.01f, InitialDelaySeconds),
        false);
    UE_LOG(LogTemp, Log,
        TEXT("OntoTwin minimap refresh scheduled: reason=%s captures=%d delay=%.2fs"),
        *PendingMinimapRefreshReason,
        PendingMinimapRefreshCaptures,
        InitialDelaySeconds);
}

void UTwinInteractionManagerComponent::HandleRepresentationVisualReady(
    ATwinInstance* Instance)
{
    if (bShuttingDown || !IsValid(Instance) || Instance->GetWorld() != GetWorld()
        || !bRoamingActive || MinimapState != TEXT("ready")
        || !MinimapCapture || !MinimapRenderTarget || !GetWorld())
    {
        return;
    }

    // A shared ArtStudio download can wake many instances in one callback.
    // Restart a short finite series so both the asset and its render proxy settle.
    ScheduleMinimapRefreshSeries(TEXT("representation_ready"), 2, 0.25f);
}

void UTwinInteractionManagerComponent::RefreshMinimapAfterSceneSettled()
{
    if (!bRoamingActive || MinimapState != TEXT("ready")
        || !MinimapCapture || !MinimapRenderTarget || !GetWorld())
    {
        PendingMinimapRefreshReason.Reset();
        PendingMinimapRefreshCaptures = 0;
        return;
    }

    const FString CaptureReason = PendingMinimapRefreshReason.IsEmpty()
        ? TEXT("scene_settled")
        : PendingMinimapRefreshReason;
    if (!CaptureMinimapScene(*CaptureReason))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("OntoTwin minimap refresh skipped while waiting for scene render state"));
    }

    PendingMinimapRefreshCaptures = FMath::Max(0, PendingMinimapRefreshCaptures - 1);
    if (PendingMinimapRefreshCaptures > 0)
    {
        GetWorld()->GetTimerManager().SetTimer(
            MinimapRefreshTimer,
            this,
            &UTwinInteractionManagerComponent::RefreshMinimapAfterSceneSettled,
            0.75f,
            false);
    }
    else
    {
        PendingMinimapRefreshReason.Reset();
    }
}

bool UTwinInteractionManagerComponent::ProjectMinimapPoint(
    const FVector& WorldPoint,
    FVector2D& OutUV) const
{
    if (MinimapState != TEXT("ready")
        || MinimapCaptureSize.X <= 0 || MinimapCaptureSize.Y <= 0)
    {
        return false;
    }
    FVector2D Pixel;
    const FIntRect ViewRect(0, 0, MinimapCaptureSize.X, MinimapCaptureSize.Y);
    if (!FSceneView::ProjectWorldToScreen(
        WorldPoint, ViewRect, MinimapViewProjection, Pixel))
    {
        return false;
    }
    OutUV.X = Pixel.X / static_cast<float>(MinimapCaptureSize.X);
    OutUV.Y = Pixel.Y / static_cast<float>(MinimapCaptureSize.Y);
    return FMath::IsFinite(OutUV.X) && FMath::IsFinite(OutUV.Y);
}

void UTwinInteractionManagerComponent::UpdateMinimapMarker(float DeltaTime)
{
    if (MinimapState != TEXT("ready") || !RoamingHUD
        || !RoamingCharacter || !IsValid(RoamingCharacter))
    {
        return;
    }
    MinimapMarkerAccumulator += DeltaTime;
    if (MinimapMarkerAccumulator < 0.05f) return;
    MinimapMarkerAccumulator = 0.0f;

    float CapsuleHalfHeight = 0.0f;
    if (const UCapsuleComponent* Capsule = RoamingCharacter->GetCapsuleComponent())
    {
        CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
    }
    const FVector FootLocation = RoamingCharacter->GetActorLocation()
        - FVector(0.0f, 0.0f, CapsuleHalfHeight);
    const FVector Forward = RoamingCharacter->GetActorForwardVector().GetSafeNormal2D();
    FVector2D UV;
    FVector2D ForwardUV;
    if (!ProjectMinimapPoint(FootLocation, UV)
        || !ProjectMinimapPoint(FootLocation + Forward * 100.0f, ForwardUV))
    {
        RoamingHUD->HideMinimapMarker();
        return;
    }

    const bool bOffMap = UV.X < 0.0f || UV.X > 1.0f || UV.Y < 0.0f || UV.Y > 1.0f;
    const FVector2D MarkerUV(
        FMath::Clamp(UV.X, 0.05f, 0.95f),
        FMath::Clamp(UV.Y, 0.05f, 0.95f));
    FVector2D Direction = bOffMap ? UV - MarkerUV : ForwardUV - UV;
    if (Direction.IsNearlyZero()) Direction = FVector2D(1.0f, 0.0f);
    Direction.Normalize();
    const float AngleDegrees = FMath::RadiansToDegrees(
        FMath::Atan2(Direction.Y, Direction.X));
    RoamingHUD->SetMinimapMarker(MarkerUV, AngleDegrees, bOffMap);
}

bool UTwinInteractionManagerComponent::CanUseMinimapTeleport() const
{
    if (!bRoamingActive || MinimapState != TEXT("ready")
        || !RoamingCharacter || !IsValid(RoamingCharacter)
        || !RoamingCharacter->CameraMode
        || RoamingCharacter->CameraMode->IsTransitioning()
        || bRouteSwitchInProgress || bMinimapTeleportInProgress)
    {
        return false;
    }
    // The minimap is a screen-space control and must remain usable in every
    // roaming camera mode.  First-person/near-follow no longer require the
    // Tab dock: releasing the RMB puts the pointer back in UI mode.  While
    // RMB is held, camera look owns the pointer and map clicks are ignored.
    return RoamingCharacter->CameraMode->GetMode() == ETwinRoamingCameraMode::God
        || bHudInteraction
        || !bRoamingMouseLook;
}

bool UTwinInteractionManagerComponent::DeprojectMinimapUV(
    const FVector2D& UV,
    float ReferenceFootZ,
    FVector& OutDesiredFoot,
    FString& OutError) const
{
    if (MinimapState != TEXT("ready")
        || MinimapCaptureSize.X <= 0 || MinimapCaptureSize.Y <= 0)
    {
        OutError = TEXT("小地图投影当前不可用");
        return false;
    }

    const FVector2D Pixel(
        FMath::Clamp(UV.X, 0.0f, 1.0f) * MinimapCaptureSize.X,
        FMath::Clamp(UV.Y, 0.0f, 1.0f) * MinimapCaptureSize.Y);
    const FIntRect ViewRect(0, 0, MinimapCaptureSize.X, MinimapCaptureSize.Y);
    FVector RayOrigin;
    FVector RayDirection;
    FSceneView::DeprojectScreenToWorld(
        Pixel,
        ViewRect,
        MinimapViewProjection.InverseFast(),
        RayOrigin,
        RayDirection);
    if (!FMath::IsFinite(RayOrigin.X) || !FMath::IsFinite(RayDirection.Z)
        || FMath::Abs(RayDirection.Z) < KINDA_SMALL_NUMBER)
    {
        OutError = TEXT("无法把小地图位置换算到当前楼层");
        return false;
    }
    const float Distance = (ReferenceFootZ - RayOrigin.Z) / RayDirection.Z;
    if (!FMath::IsFinite(Distance) || Distance <= 0.0f)
    {
        OutError = TEXT("小地图位置不在当前视锥内");
        return false;
    }
    OutDesiredFoot = RayOrigin + RayDirection * Distance;
    OutDesiredFoot.Z = ReferenceFootZ;
    return true;
}

bool UTwinInteractionManagerComponent::ResolveSafeMinimapLocation(
    const FVector& DesiredFoot,
    float ReferenceFootZ,
    FVector& OutCapsuleCenter,
    bool& bOutAdjusted,
    FString& OutError) const
{
    if (!GetWorld() || !RoamingCharacter || !IsValid(RoamingCharacter))
    {
        OutError = TEXT("人物或当前关卡不可用");
        return false;
    }
    const UCapsuleComponent* Capsule = RoamingCharacter->GetCapsuleComponent();
    if (!Capsule)
    {
        OutError = TEXT("人物碰撞体不可用");
        return false;
    }

    const float CapsuleRadius = Capsule->GetScaledCapsuleRadius();
    const float CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
    const UCharacterMovementComponent* CharacterMovement =
        RoamingCharacter->GetCharacterMovement();
    const float TraceUpCm = FMath::Max(RuntimeRouteTraceUpCm, 150.0f);
    const float TraceDownCm = FMath::Max(RuntimeRouteTraceDownCm, 300.0f);
    const float FloorToleranceCm = CharacterMovement
        ? FMath::Max(50.0f, CharacterMovement->MaxStepHeight + 5.0f)
        : 50.0f;

    FCollisionQueryParams QueryParams(
        SCENE_QUERY_STAT(TwinMinimapTeleport), true, RoamingCharacter);
    if (SceneManager) QueryParams.AddIgnoredActor(SceneManager);
    if (ActiveRoute) QueryParams.AddIgnoredActor(ActiveRoute);
    if (RuntimeRouteActor) QueryParams.AddIgnoredActor(RuntimeRouteActor);
    if (MinimapAnchor) QueryParams.AddIgnoredActor(MinimapAnchor);
    FCollisionObjectQueryParams ObjectTypes;
    ObjectTypes.AddObjectTypesToQuery(ECC_WorldStatic);
    ObjectTypes.AddObjectTypesToQuery(ECC_WorldDynamic);

    const auto TryCandidate = [this, &QueryParams, &ObjectTypes,
        CapsuleRadius, CapsuleHalfHeight, TraceUpCm, TraceDownCm,
        ReferenceFootZ, FloorToleranceCm, CharacterMovement](
            const FVector2D& XY, FVector& OutCenter)
    {
        const FVector TraceStart(XY.X, XY.Y, ReferenceFootZ + TraceUpCm);
        const FVector TraceEnd(XY.X, XY.Y, ReferenceFootZ - TraceDownCm);
        TArray<FHitResult> Hits;
        if (!GetWorld()->LineTraceMultiByObjectType(
            Hits, TraceStart, TraceEnd, ObjectTypes, QueryParams))
        {
            return false;
        }

        const FHitResult* BestHit = nullptr;
        float BestHeightDelta = TNumericLimits<float>::Max();
        for (const FHitResult& Hit : Hits)
        {
            const UPrimitiveComponent* Component = Hit.GetComponent();
            const AActor* HitActor = Hit.GetActor();
            const float HeightDelta = FMath::Abs(Hit.ImpactPoint.Z - ReferenceFootZ);
            if (!Hit.bBlockingHit || !Component
                || HeightDelta > FloorToleranceCm
                || Hit.ImpactNormal.Z < RuntimeRouteMinGroundNormalZ
                || (CharacterMovement && !CharacterMovement->IsWalkable(Hit))
                || Component->ComponentHasTag(TEXT("OntoTwinNoTeleport"))
                || (HitActor && HitActor->ActorHasTag(TEXT("OntoTwinNoTeleport")))
                || Component->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block)
            {
                continue;
            }
            if (HeightDelta < BestHeightDelta)
            {
                BestHeightDelta = HeightDelta;
                BestHit = &Hit;
            }
        }
        if (!BestHit) return false;

        const FVector CandidateCenter = BestHit->ImpactPoint
            + FVector(0.0f, 0.0f, CapsuleHalfHeight + 2.0f);
        if (GetWorld()->OverlapBlockingTestByChannel(
            CandidateCenter,
            RoamingCharacter->GetActorQuat(),
            ECC_Pawn,
            FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight),
            QueryParams))
        {
            return false;
        }
        OutCenter = CandidateCenter;
        return true;
    };

    if (TryCandidate(FVector2D(DesiredFoot.X, DesiredFoot.Y), OutCapsuleCenter))
    {
        bOutAdjusted = false;
        return true;
    }

    constexpr int32 DirectionCount = 12;
    const float SearchRadii[] = {50.0f, 100.0f, 150.0f};
    for (const float Radius : SearchRadii)
    {
        for (int32 DirectionIndex = 0;
            DirectionIndex < DirectionCount;
            ++DirectionIndex)
        {
            const float Angle = 2.0f * PI * static_cast<float>(DirectionIndex)
                / static_cast<float>(DirectionCount);
            const FVector2D CandidateXY(
                DesiredFoot.X + FMath::Cos(Angle) * Radius,
                DesiredFoot.Y + FMath::Sin(Angle) * Radius);
            if (TryCandidate(CandidateXY, OutCapsuleCenter))
            {
                bOutAdjusted = true;
                return true;
            }
        }
    }

    OutError = TEXT("目标附近 150 厘米内没有安全落点");
    return false;
}

bool UTwinInteractionManagerComponent::ResolveMinimapTeleportTarget(
    const FVector2D& UV,
    FVector& OutCapsuleCenter,
    FVector2D& OutResolvedUV,
    bool& bOutAdjusted,
    FString& OutError) const
{
    if (!RoamingCharacter || !RoamingCharacter->GetCapsuleComponent())
    {
        OutError = TEXT("人物当前不可用");
        return false;
    }
    const float HalfHeight =
        RoamingCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const float FootZ = RoamingCharacter->GetActorLocation().Z - HalfHeight;
    FVector DesiredFoot;
    if (!DeprojectMinimapUV(UV, FootZ, DesiredFoot, OutError)
        || !ResolveSafeMinimapLocation(
            DesiredFoot, FootZ, OutCapsuleCenter, bOutAdjusted, OutError))
    {
        return false;
    }

    const FVector ResolvedFoot = OutCapsuleCenter
        - FVector(0.0f, 0.0f, HalfHeight);
    if (!ProjectMinimapPoint(ResolvedFoot, OutResolvedUV))
    {
        OutResolvedUV = UV;
    }
    OutResolvedUV.X = FMath::Clamp(OutResolvedUV.X, 0.0f, 1.0f);
    OutResolvedUV.Y = FMath::Clamp(OutResolvedUV.Y, 0.0f, 1.0f);
    return true;
}

void UTwinInteractionManagerComponent::PreviewMinimapTeleport(const FVector2D& UV)
{
    LastMinimapPointerEventSeconds = FPlatformTime::Seconds();
    if (!CanUseMinimapTeleport() || !RoamingHUD) return;

    FVector TargetLocation;
    FVector2D ResolvedUV = UV;
    bool bAdjusted = false;
    FString Error;
    if (!ResolveMinimapTeleportTarget(
        UV, TargetLocation, ResolvedUV, bAdjusted, Error))
    {
        RoamingHUD->SetMinimapTeleportFeedback(
            UV,
            ETwinMinimapTeleportFeedback::Invalid,
            TEXT("该位置不可到达"));
        UE_LOG(LogTemp, Verbose,
            TEXT("OntoTwin minimap teleport preview rejected: %s"), *Error);
        return;
    }

    RoamingHUD->SetMinimapTeleportFeedback(
        ResolvedUV,
        bAdjusted
            ? ETwinMinimapTeleportFeedback::Adjusted
            : ETwinMinimapTeleportFeedback::Valid,
        bAdjusted ? TEXT("已调整至附近安全位置") : TEXT("双击传送到这里"));
}

void UTwinInteractionManagerComponent::RequestMinimapTeleport(const FVector2D& UV)
{
    LastMinimapPointerEventSeconds = FPlatformTime::Seconds();
    if (!CanUseMinimapTeleport() || !RoamingHUD) return;

    FVector TargetLocation;
    FVector2D ResolvedUV = UV;
    bool bAdjusted = false;
    FString Error;
    if (!ResolveMinimapTeleportTarget(
        UV, TargetLocation, ResolvedUV, bAdjusted, Error))
    {
        RoamingHUD->SetMinimapTeleportFeedback(
            UV,
            ETwinMinimapTeleportFeedback::Invalid,
            TEXT("该位置不可到达"));
        UE_LOG(LogTemp, Verbose,
            TEXT("OntoTwin minimap teleport request rejected: %s"), *Error);
        return;
    }
    RoamingHUD->SetMinimapTeleportFeedback(
        ResolvedUV,
        bAdjusted
            ? ETwinMinimapTeleportFeedback::Adjusted
            : ETwinMinimapTeleportFeedback::Valid,
        bAdjusted ? TEXT("已调整至附近安全位置") : TEXT("正在传送"));
    BeginMinimapTeleport(TargetLocation, ResolvedUV, false);
}

void UTwinInteractionManagerComponent::BeginMinimapTeleport(
    const FVector& TargetLocation,
    const FVector2D& TargetUV,
    bool bIsUndo)
{
    if (!GetWorld() || !RoamingCharacter || bMinimapTeleportInProgress) return;

    GetWorld()->GetTimerManager().ClearTimer(MinimapTeleportTimer);
    GetWorld()->GetTimerManager().ClearTimer(MinimapUndoTimer);
    if (!bIsUndo)
    {
        MinimapUndoLocation = RoamingCharacter->GetActorLocation();
    }
    bMinimapUndoAvailable = false;
    if (RoamingHUD) RoamingHUD->SetMinimapUndoAvailable(false);

    PendingMinimapTeleportLocation = TargetLocation;
    PendingMinimapTeleportUV = TargetUV;
    bPendingMinimapTeleportIsUndo = bIsUndo;
    bPendingMinimapTeleportSucceeded = false;
    bMinimapTeleportInProgress = true;

    StopNarration(true);
    if (RoamingCharacter->RouteFollower)
    {
        RoamingCharacter->RouteFollower->PauseByUser();
    }
    RuntimeState = TEXT("manual");
    RefreshHud();

    if (FOntoTwinGlassRenderer::ShouldReduceMotion()
        || !PlayerController || !PlayerController->PlayerCameraManager)
    {
        CompleteMinimapTeleportMove();
        return;
    }
    PlayerController->PlayerCameraManager->StartCameraFade(
        0.0f, 1.0f, 0.15f, FLinearColor::Black, false, true);
    GetWorld()->GetTimerManager().SetTimer(
        MinimapTeleportTimer,
        this,
        &UTwinInteractionManagerComponent::CompleteMinimapTeleportMove,
        0.15f,
        false);
}

void UTwinInteractionManagerComponent::CompleteMinimapTeleportMove()
{
    if (!bMinimapTeleportInProgress || !RoamingCharacter
        || !IsValid(RoamingCharacter))
    {
        CancelMinimapTeleport(true);
        return;
    }

    if (UCharacterMovementComponent* Movement =
        RoamingCharacter->GetCharacterMovement())
    {
        Movement->StopMovementImmediately();
    }
    FHitResult MoveHit;
    bPendingMinimapTeleportSucceeded = RoamingCharacter->SetActorLocation(
        PendingMinimapTeleportLocation,
        false,
        &MoveHit,
        ETeleportType::TeleportPhysics);
    UpdateMinimapMarker(1000.0f);

    if (FOntoTwinGlassRenderer::ShouldReduceMotion()
        || !PlayerController || !PlayerController->PlayerCameraManager
        || !GetWorld())
    {
        FinishMinimapTeleport();
        return;
    }
    PlayerController->PlayerCameraManager->StartCameraFade(
        1.0f, 0.0f, 0.18f, FLinearColor::Black, false, false);
    GetWorld()->GetTimerManager().SetTimer(
        MinimapTeleportTimer,
        this,
        &UTwinInteractionManagerComponent::FinishMinimapTeleport,
        0.18f,
        false);
}

void UTwinInteractionManagerComponent::FinishMinimapTeleport()
{
    if (!bMinimapTeleportInProgress) return;
    const bool bWasUndo = bPendingMinimapTeleportIsUndo;
    const bool bSucceeded = bPendingMinimapTeleportSucceeded;
    bMinimapTeleportInProgress = false;
    bPendingMinimapTeleportIsUndo = false;
    bPendingMinimapTeleportSucceeded = false;

    if (!RoamingHUD) return;
    if (!bSucceeded)
    {
        bMinimapUndoAvailable = false;
        RoamingHUD->SetMinimapUndoAvailable(false);
        RoamingHUD->SetMinimapTeleportFeedback(
            PendingMinimapTeleportUV,
            ETwinMinimapTeleportFeedback::Invalid,
            TEXT("传送失败，请重试"));
        return;
    }

    RoamingHUD->SetMinimapTeleportFeedback(
        PendingMinimapTeleportUV,
        ETwinMinimapTeleportFeedback::Success,
        bWasUndo ? TEXT("已返回原位置") : TEXT("已传送"));
    if (bWasUndo)
    {
        bMinimapUndoAvailable = false;
        RoamingHUD->SetMinimapUndoAvailable(false);
    }
    else if (UWorld* World = GetWorld())
    {
        bMinimapUndoAvailable = true;
        RoamingHUD->SetMinimapUndoAvailable(true);
        World->GetTimerManager().SetTimer(
            MinimapUndoTimer,
            this,
            &UTwinInteractionManagerComponent::ExpireMinimapUndo,
            5.0f,
            false);
    }
}

void UTwinInteractionManagerComponent::ExpireMinimapUndo()
{
    bMinimapUndoAvailable = false;
    if (RoamingHUD) RoamingHUD->SetMinimapUndoAvailable(false);
}

void UTwinInteractionManagerComponent::UndoLastMinimapTeleport()
{
    LastMinimapPointerEventSeconds = FPlatformTime::Seconds();
    if (!CanUseMinimapTeleport() || !bMinimapUndoAvailable
        || !RoamingCharacter || !RoamingCharacter->GetCapsuleComponent())
    {
        return;
    }

    ExpireMinimapUndo();
    const float HalfHeight =
        RoamingCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const FVector DesiredFoot = MinimapUndoLocation
        - FVector(0.0f, 0.0f, HalfHeight);
    FVector TargetLocation;
    bool bAdjusted = false;
    FString Error;
    if (!ResolveSafeMinimapLocation(
        DesiredFoot, DesiredFoot.Z, TargetLocation, bAdjusted, Error))
    {
        if (RoamingHUD)
        {
            FVector2D CurrentUV(0.5f, 0.5f);
            ProjectMinimapPoint(DesiredFoot, CurrentUV);
            RoamingHUD->SetMinimapTeleportFeedback(
                CurrentUV,
                ETwinMinimapTeleportFeedback::Invalid,
                TEXT("原位置当前不可返回"));
        }
        UE_LOG(LogTemp, Verbose,
            TEXT("OntoTwin minimap undo rejected: %s"), *Error);
        return;
    }

    const FVector TargetFoot = TargetLocation
        - FVector(0.0f, 0.0f, HalfHeight);
    FVector2D TargetUV(0.5f, 0.5f);
    ProjectMinimapPoint(TargetFoot, TargetUV);
    if (RoamingHUD && bAdjusted)
    {
        RoamingHUD->SetMinimapTeleportFeedback(
            TargetUV,
            ETwinMinimapTeleportFeedback::Adjusted,
            TEXT("原位置受阻，返回附近安全位置"));
    }
    BeginMinimapTeleport(TargetLocation, TargetUV, true);
}

void UTwinInteractionManagerComponent::CancelMinimapTeleport(bool bRestoreView)
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(MinimapTeleportTimer);
        World->GetTimerManager().ClearTimer(MinimapUndoTimer);
    }
    if (bRestoreView && bMinimapTeleportInProgress
        && PlayerController && PlayerController->PlayerCameraManager)
    {
        PlayerController->PlayerCameraManager->StopCameraFade();
    }
    bMinimapTeleportInProgress = false;
    bPendingMinimapTeleportIsUndo = false;
    bPendingMinimapTeleportSucceeded = false;
    bMinimapUndoAvailable = false;
    if (RoamingHUD)
    {
        RoamingHUD->SetMinimapUndoAvailable(false);
        RoamingHUD->ClearMinimapTeleportFeedback();
    }
}

bool UTwinInteractionManagerComponent::GetGodViewTransform(FTransform& OutTransform) const
{
    const FString CameraId = CurrentConfig.GodCamera.CameraId.IsEmpty()
        ? TEXT("camera.god.default")
        : CurrentConfig.GodCamera.CameraId;
    const ATwinGodViewAnchor* Anchor = GodViewAnchor && IsValid(GodViewAnchor)
        ? GodViewAnchor
        : FindGodViewAnchor(CameraId);
    if (!Anchor) return false;

    OutTransform = Anchor->GetActorTransform();
    return true;
}

bool UTwinInteractionManagerComponent::GetGodViewLookSensitivity(float& OutSensitivity) const
{
    if (AppliedRevision < 0)
    {
        return false;
    }

    const float Sensitivity = CurrentConfig.GodCamera.LookSensitivity;
    if (!FMath::IsFinite(Sensitivity) || Sensitivity <= 0.0f)
    {
        return false;
    }

    OutSensitivity = FMath::Clamp(Sensitivity, 0.1f, 5.0f);
    return true;
}

bool UTwinInteractionManagerComponent::EnterRoaming(FString& OutError)
{
    if (bRoamingActive) return true;
    if (!bSceneBaselineReady)
    {
        OutError = TEXT("Scene model baseline is still loading");
        return false;
    }
    if (!CurrentConfig.bEnabled)
    {
        OutError = TEXT("Roaming is disabled in the active project");
        return false;
    }
    if (!PlayerController)
    {
        OutError = TEXT("Local player controller is unavailable");
        return false;
    }
    if (SceneManager && SceneManager->IsRuntimeEditModeActive())
    {
        OutError = TEXT("Exit Runtime Editor before entering character roaming");
        return false;
    }
    WebSelectedInstance.Reset();
    LastWebInteractionMessage.Reset();

    UTwinCharacterAsset* CharacterAsset = ResolveCharacterAsset(OutError);
    if (!CharacterAsset) return false;
    UClass* CharacterClass = CharacterAsset->CharacterClass.LoadSynchronous();
    if (!CharacterClass) CharacterClass = ATwinRoamingCharacter::StaticClass();
    if (!CharacterClass->IsChildOf(ATwinRoamingCharacter::StaticClass()))
    {
        OutError = TEXT("Configured CharacterClass must derive from ATwinRoamingCharacter");
        return false;
    }

    ActiveRoute = nullptr;
    DestroyRuntimeRoute();
    if (CurrentConfig.bRouteEnabled)
    {
        if (CurrentConfig.bHasRuntimeRoute)
        {
            ActiveRoute = BuildRuntimeRoute(OutError);
            if (!ActiveRoute) return false;
        }
        else
        {
            ActiveRoute = FindRoute(CurrentConfig.RouteId);
        }
    }

    FTransform SpawnTransform;
    if (!ResolveSpawnTransform(CharacterAsset, SpawnTransform, OutError))
    {
        DestroyRuntimeRoute();
        return false;
    }

    bPreRoamingMouseCursor = PlayerController->bShowMouseCursor;
    bRoamingMouseLook = false;
    bFirstPersonCrosshairEnabled = false;
    OriginalPawn = PlayerController->GetPawn();
    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = SceneManager;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
    RoamingCharacter = GetWorld()->SpawnActor<ATwinRoamingCharacter>(
        CharacterClass, SpawnTransform, SpawnParams);
    if (!RoamingCharacter)
    {
        DestroyRuntimeRoute();
        OutError = TEXT("Character could not be spawned");
        return false;
    }
    if (!RoamingCharacter->ApplyCharacterAsset(CharacterAsset, OutError))
    {
        RoamingCharacter->Destroy();
        RoamingCharacter = nullptr;
        DestroyRuntimeRoute();
        return false;
    }

    ActiveSessionCharacterId = CurrentConfig.CharacterId;
    ActiveSessionCharacterDisplayName = CurrentConfig.CharacterDisplayName;
    ActiveSessionCharacterPrimaryAssetId = CurrentConfig.CharacterPrimaryAssetId;
    ActiveSessionDefaultSkinId = CurrentConfig.DefaultSkinId;
    ActiveSessionSkinPrimaryAssetIds = CurrentConfig.SkinPrimaryAssetIds;

    DegradedFeatures.Reset();
    bTakeoverEnabled = CurrentConfig.bTakeoverEnabled;
    RoamingCharacter->ApplyMovementSettings(CurrentConfig.Movement);
    RoamingCharacter->CameraMode->Configure(
        CurrentConfig.FirstPersonCamera, CurrentConfig.NearCamera, CurrentConfig.GodCamera);
    RoamingCharacter->SkinComponent->Configure(
        CurrentConfig.SkinPrimaryAssetIds, CurrentConfig.DefaultSkinId);
    FString SkinError;
    if (!CurrentConfig.SkinPrimaryAssetIds.IsEmpty()
        && !RoamingCharacter->SkinComponent->ApplyDefaultSkin(SkinError))
    {
        DegradedFeatures.AddUnique(TEXT("default_skin_missing"));
    }
    else if (CurrentConfig.SkinPrimaryAssetIds.IsEmpty())
    {
        RoamingCharacter->SkinComponent->ClearSkinOverrides();
    }
    RoamingCharacter->RefreshLocomotionAnimation();

    RoamingCharacter->RouteFollower->OnNarrationRequested.AddUObject(
        this, &UTwinInteractionManagerComponent::HandleNarrationRequested);
    RoamingCharacter->RouteFollower->Configure(
        ActiveRoute,
        CurrentConfig.Movement.AutoRouteSpeedCmS,
        CurrentConfig.bRouteLoop,
        false,
        CurrentConfig.RuntimeRouteWaypoints);
    if (CurrentConfig.bRouteEnabled && !ActiveRoute)
    {
        DegradedFeatures.AddUnique(TEXT("route_missing"));
    }
    else if (ActiveRoute && CurrentConfig.bRouteLoop && !ActiveRoute->Spline->IsClosedLoop())
    {
        DegradedFeatures.AddUnique(TEXT("route_not_closed"));
    }

    GodViewAnchor = FindGodViewAnchor(CurrentConfig.GodCamera.CameraId);
    if (!GodViewAnchor) DegradedFeatures.AddUnique(TEXT("god_camera_missing"));
    OntoTwinCameraVisibility::ClearFromPlayer(
        PlayerController, StartupViewVisibilityState);
    RoamingCharacter->CameraMode->ActivateNear(PlayerController, true);
    bRoamingActive = true;
    ActivateRoamingInput();
    bDefaultModeApplied = true;
    CreateHud();
    ApplyMinimapConfig(CurrentConfig.bMinimapEnabled);

    if (ActiveRoute && CurrentConfig.bRouteAutoStart)
    {
        FString RouteError;
        const bool bRouteStarted = CurrentConfig.bHasRuntimeRoute
            ? RoamingCharacter->RouteFollower->RestartFromBeginning(RouteError)
            : RoamingCharacter->RouteFollower->TryStartFromSpawn(RouteError);
        if (!bRouteStarted)
        {
            DegradedFeatures.AddUnique(CurrentConfig.bHasRuntimeRoute
                ? TEXT("runtime_route_start_rejected")
                : TEXT("route_join_rejected"));
            LastError = RouteError;
            UE_LOG(LogTemp, Warning, TEXT("OntoTwin route auto-start rejected: %s"), *RouteError);
        }
        else if (CurrentConfig.bHasRuntimeRoute)
        {
            UE_LOG(LogTemp, Log, TEXT("OntoTwin runtime route %s started from its first point"),
                *CurrentConfig.RouteId);
        }
    }

    if (CurrentConfig.DefaultCameraMode != ETwinRoamingCameraMode::NearFollow)
    {
        FString CameraError;
        if (!RoamingCharacter->CameraMode->ActivateMode(
            CurrentConfig.DefaultCameraMode,
            PlayerController,
            GodViewAnchor,
            CameraError,
            true))
        {
            DegradedFeatures.AddUnique(
                CurrentConfig.DefaultCameraMode == ETwinRoamingCameraMode::God
                    ? TEXT("god_camera_unavailable")
                    : TEXT("first_person_unavailable"));
        }
    }
    SetHudInteraction(false);
    RuntimeState = RoamingCharacter->CameraMode->GetMode() == ETwinRoamingCameraMode::God
        ? TEXT("god_view")
        : (RoamingCharacter->RouteFollower->IsFollowing() ? TEXT("auto_route") : TEXT("manual"));
    RefreshHud();
    return true;
}

void UTwinInteractionManagerComponent::RestoreOriginalPawn()
{
    if (!PlayerController) return;
    if (OriginalPawn && IsValid(OriginalPawn))
    {
        PlayerController->Possess(OriginalPawn);
    }
    else
    {
        PlayerController->UnPossess();
    }
    OriginalPawn = nullptr;
    ApplyStartupView(true);
}

void UTwinInteractionManagerComponent::ExitRoaming()
{
    CancelRuntimeRouteSwitch(true);
    StopNarration(false);
    if (!bRoamingActive && !RoamingCharacter)
    {
        bFirstPersonCrosshairEnabled = false;
        bCrosshairInteractive = false;
        ShutdownMinimap(false);
        SetMinimapState(CurrentConfig.bEnabled && CurrentConfig.bMinimapEnabled
            ? TEXT("waiting") : TEXT("disabled"));
        DeactivateRoamingInput();
        DestroyRuntimeRoute();
        if (bShuttingDown) DestroyHud();
        else RefreshHud();
        return;
    }
    SetHudInteraction(false);
    DeactivateRoamingInput();
    if (SceneManager) SceneManager->ClearOverlayFromSceneInteraction();
    ShutdownMinimap(false);
    if (RoamingCharacter && IsValid(RoamingCharacter))
    {
        RoamingCharacter->CameraMode->Shutdown(PlayerController);
    }
    RestoreOriginalPawn();
    if (RoamingCharacter && IsValid(RoamingCharacter))
    {
        RoamingCharacter->Destroy();
    }
    RoamingCharacter = nullptr;
    ActiveRoute = nullptr;
    DestroyRuntimeRoute();
    GodViewAnchor = nullptr;
    ActiveSessionCharacterId.Reset();
    ActiveSessionCharacterDisplayName.Reset();
    ActiveSessionCharacterPrimaryAssetId.Reset();
    ActiveSessionDefaultSkinId.Reset();
    ActiveSessionSkinPrimaryAssetIds.Reset();
    bCharacterSwitchInProgress = false;
    bRoamingActive = false;
    bHudInteraction = false;
    bRoamingMouseLook = false;
    bFirstPersonCrosshairEnabled = false;
    if (PlayerController)
    {
        PlayerController->bShowMouseCursor = bPreRoamingMouseCursor;
        if (bPreRoamingMouseCursor)
        {
            FInputModeGameAndUI InputMode;
            InputMode.SetHideCursorDuringCapture(false);
            InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
            PlayerController->SetInputMode(InputMode);
        }
        else
        {
            PlayerController->SetInputMode(FInputModeGameOnly());
        }
    }
    WebSelectedInstance.Reset();
    LastWebInteractionMessage.Reset();
    SetMinimapState(CurrentConfig.bEnabled && CurrentConfig.bMinimapEnabled
        ? TEXT("waiting") : TEXT("disabled"));
    if (CrosshairHUD && IsValid(CrosshairHUD)) CrosshairHUD->RemoveFromParent();
    CrosshairHUD = nullptr;
    bCrosshairInteractive = false;
    RuntimeState = CurrentConfig.bEnabled
        ? (bBackendOnline ? TEXT("available") : TEXT("offline"))
        : TEXT("disabled");
    RefreshHud();
}

void UTwinInteractionManagerComponent::ToggleRoaming()
{
    UE_LOG(LogTemp, Log, TEXT("OntoTwin F7 roaming toggle received; active=%s revision=%d"),
        bRoamingActive ? TEXT("true") : TEXT("false"), AppliedRevision);
    if (SceneManager && SceneManager->IsRuntimeEditModeActive())
    {
        SceneManager->RequestRoamingAfterRuntimeEdit();
        return;
    }
    if (bHudInteraction) SetHudInteraction(false);
    if (bRoamingActive)
    {
        ExitRoaming();
        return;
    }
    FString Error;
    if (!EnterRoaming(Error))
    {
        RuntimeState = TEXT("blocked");
        LastError = Error;
        UE_LOG(LogTemp, Warning, TEXT("OntoTwin roaming entry failed: %s"), *Error);
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(
                -1,
                6.0f,
                FColor::Red,
                FString::Printf(TEXT("OntoTwin roaming unavailable: %s"), *Error));
        }
    }
}

void UTwinInteractionManagerComponent::ToggleCameraMode()
{
    if (!bRoamingActive || !RoamingCharacter || bHudInteraction
        || RoamingCharacter->CameraMode->IsTransitioning())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("OntoTwin V camera toggle ignored: roaming=%s character=%s hud=%s transitioning=%s"),
            bRoamingActive ? TEXT("true") : TEXT("false"),
            RoamingCharacter ? TEXT("ready") : TEXT("missing"),
            bHudInteraction ? TEXT("open") : TEXT("closed"),
            RoamingCharacter && RoamingCharacter->CameraMode->IsTransitioning()
                ? TEXT("true") : TEXT("false"));
        return;
    }
    const ETwinRoamingCameraMode PreviousMode = RoamingCharacter->CameraMode->GetMode();
    FString Error;
    if (!RoamingCharacter->CameraMode->Cycle(PlayerController, GodViewAnchor, Error))
    {
        LastError = Error;
        UE_LOG(LogTemp, Warning,
            TEXT("OntoTwin V camera toggle failed from %s: %s"),
            *CameraModeId(PreviousMode), *Error);
        RefreshHud();
        return;
    }
    LastError.Reset();
    SetHudInteraction(false);
    UE_LOG(LogTemp, Log,
        TEXT("OntoTwin V camera toggle applied: %s -> %s"),
        *CameraModeId(PreviousMode),
        *CameraModeId(RoamingCharacter->CameraMode->GetMode()));
    RefreshHud();
}

void UTwinInteractionManagerComponent::SetCameraMode(ETwinRoamingCameraMode Mode)
{
    if (!bRoamingActive || !RoamingCharacter
        || RoamingCharacter->CameraMode->IsTransitioning()
        || Mode == RoamingCharacter->CameraMode->GetMode()) return;
    FString Error;
    if (!RoamingCharacter->CameraMode->ActivateMode(
        Mode, PlayerController, GodViewAnchor, Error, false))
    {
        LastError = Error;
        if (Mode == ETwinRoamingCameraMode::God)
        {
            DegradedFeatures.AddUnique(TEXT("god_camera_unavailable"));
        }
        RefreshHud();
        return;
    }
    LastError.Reset();
    SetHudInteraction(false);
    RefreshHud();
}

bool UTwinInteractionManagerComponent::IsCameraTransitioning() const
{
    return RoamingCharacter && RoamingCharacter->CameraMode
        && RoamingCharacter->CameraMode->IsTransitioning();
}

ETwinRoamingCameraMode UTwinInteractionManagerComponent::GetCameraMode() const
{
    return RoamingCharacter && RoamingCharacter->CameraMode
        ? RoamingCharacter->CameraMode->GetMode()
        : ETwinRoamingCameraMode::NearFollow;
}

void UTwinInteractionManagerComponent::ToggleHudInteraction()
{
    const bool bGlobalAvailable = SceneManager
        && !SceneManager->IsRuntimeEditModeActive();
    if ((bRoamingActive || bGlobalAvailable) && !IsCameraTransitioning())
    {
        SetHudInteraction(!bHudInteraction);
    }
}

void UTwinInteractionManagerComponent::SetHudInteraction(bool bOpen)
{
    const bool bGlobalAvailable = SceneManager
        && !SceneManager->IsRuntimeEditModeActive();
    const bool bNextOpen = bOpen && (bRoamingActive || bGlobalAvailable);
    if (bNextOpen && !bHudInteraction && !bRoamingActive && PlayerController)
    {
        bGlobalHudPreviousMouseCursor = PlayerController->bShowMouseCursor;
        bGlobalHudInputCaptured = true;
    }
    bHudInteraction = bNextOpen;
    if (bRoamingActive && bHudInteraction)
    {
        bRoamingMouseLook = false;
    }
    // RoamingHUD keeps status, hints and minimap. The independent RuntimeDock
    // owns the fixed-height action drawer.
    if (RoamingHUD) RoamingHUD->SetInteractionOpen(false);
    if (RuntimeDock) RuntimeDock->SetDockOpen(bHudInteraction);
    if (!PlayerController) return;

    if (bRoamingActive)
    {
        ApplyRoamingMouseInputMode();
        RefreshHud();
        return;
    }

    const bool bGodMode = RoamingCharacter
        && RoamingCharacter->CameraMode->GetMode() == ETwinRoamingCameraMode::God;
    PlayerController->bShowMouseCursor = bHudInteraction || bGodMode;
    if (bHudInteraction || bGodMode)
    {
        FInputModeGameAndUI InputMode;
        InputMode.SetHideCursorDuringCapture(false);
        InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        PlayerController->SetInputMode(InputMode);
    }
    else if (bGlobalHudInputCaptured)
    {
        PlayerController->bShowMouseCursor = bGlobalHudPreviousMouseCursor;
        if (bGlobalHudPreviousMouseCursor)
        {
            FInputModeGameAndUI InputMode;
            InputMode.SetHideCursorDuringCapture(false);
            InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
            PlayerController->SetInputMode(InputMode);
        }
        else
        {
            PlayerController->SetInputMode(FInputModeGameOnly());
        }
        bGlobalHudInputCaptured = false;
    }
    else
    {
        PlayerController->SetInputMode(FInputModeGameOnly());
    }
    RefreshHud();
}

void UTwinInteractionManagerComponent::SetRoamingMouseLook(bool bActive)
{
    if (!bRoamingActive || bHudInteraction || !PlayerController)
    {
        return;
    }
    bRoamingMouseLook = bActive;
    ApplyRoamingMouseInputMode();
}

void UTwinInteractionManagerComponent::ApplyRoamingMouseInputMode()
{
    if (!bRoamingActive || !PlayerController)
    {
        return;
    }

    const bool bPointerInteraction = bHudInteraction || !bRoamingMouseLook;
    PlayerController->bShowMouseCursor = bPointerInteraction;
    if (bPointerInteraction)
    {
        FInputModeGameAndUI InputMode;
        InputMode.SetHideCursorDuringCapture(false);
        InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        PlayerController->SetInputMode(InputMode);
    }
    else
    {
        PlayerController->SetInputMode(FInputModeGameOnly());
    }
}

void UTwinInteractionManagerComponent::CycleSkin()
{
    if (!RoamingCharacter) return;
    FString Error;
    if (!RoamingCharacter->SkinComponent->CycleSkin(Error)) LastError = Error;
    else
    {
        LastError.Reset();
        DegradedFeatures.Remove(TEXT("default_skin_missing"));
        RoamingCharacter->RefreshLocomotionAnimation();
    }
    RefreshHud();
}

void UTwinInteractionManagerComponent::ResumeRoute()
{
    if (!RoamingCharacter) return;
    FString Error;
    if (!RoamingCharacter->RouteFollower->TryResume(Error)) LastError = Error;
    else
    {
        LastError.Reset();
        DegradedFeatures.Remove(TEXT("route_join_rejected"));
    }
    RefreshHud();
}

void UTwinInteractionManagerComponent::RestartRoute()
{
    if (!RoamingCharacter) return;
    StopNarration(false);
    FString Error;
    if (!RoamingCharacter->RouteFollower->RestartFromBeginning(Error)) LastError = Error;
    else
    {
        LastError.Reset();
        DegradedFeatures.Remove(TEXT("route_join_rejected"));
    }
    RefreshHud();
}

bool UTwinInteractionManagerComponent::SelectRuntimeRoute(const FString& RouteId)
{
    if (!bRoamingActive || !RoamingCharacter || bRouteSwitchInProgress)
    {
        return false;
    }
    const FTwinRoamingRuntimeRoute* Route = FindRuntimeRoute(
        CurrentConfig.AvailableRoutes, RouteId);
    if (!Route)
    {
        LastError = TEXT("所选漫游路线当前不可用");
        RefreshHud();
        return false;
    }
    if (!IsRuntimeRouteForCurrentLevel(*Route))
    {
        LastError = TEXT("所选漫游路线不属于当前 UE 关卡");
        RefreshHud();
        return false;
    }
    if (RouteId == CurrentConfig.RouteId)
    {
        return true;
    }

    StopNarration(false);
    RoamingCharacter->RouteFollower->PauseByUser();
    PendingRouteSwitchId = RouteId;
    bRouteSwitchInProgress = true;
    LastError.Reset();
    SetHudInteraction(false);

    if (PlayerController && PlayerController->PlayerCameraManager)
    {
        PlayerController->PlayerCameraManager->StartCameraFade(
            0.0f, 1.0f, 0.20f, FLinearColor::Black, false, true);
    }
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimer(
            RouteSwitchTimer,
            this,
            &UTwinInteractionManagerComponent::CompleteRuntimeRouteSwitch,
            0.20f,
            false);
    }
    else
    {
        CompleteRuntimeRouteSwitch();
    }
    RefreshHud();
    return true;
}

bool UTwinInteractionManagerComponent::SelectRuntimeCharacter(const FString& CharacterId)
{
    if (!bRoamingActive || !RoamingCharacter || bCharacterSwitchInProgress
        || bRouteSwitchInProgress)
    {
        return false;
    }
    const FTwinRoamingRuntimeCharacter* Target = FindRuntimeCharacter(CharacterId);
    if (!Target)
    {
        LastError = TEXT("所选人物当前不可用");
        RefreshHud();
        return false;
    }
    if (CharacterId == GetActiveRuntimeCharacterId())
    {
        return true;
    }

    bCharacterSwitchInProgress = true;
    FTwinRoamingRuntimeCharacter Previous;
    Previous.CharacterId = ActiveSessionCharacterId;
    Previous.DisplayName = ActiveSessionCharacterDisplayName;
    Previous.PrimaryAssetId = ActiveSessionCharacterPrimaryAssetId;
    Previous.DefaultSkinId = ActiveSessionDefaultSkinId;
    Previous.SkinPrimaryAssetIds = ActiveSessionSkinPrimaryAssetIds;
    const FString PreviousSkinId = RoamingCharacter->SkinComponent
        ? RoamingCharacter->SkinComponent->GetActiveSkinId() : FString();
    const FVector PreviousLocation = RoamingCharacter->GetActorLocation();
    const FRotator PreviousRotation = RoamingCharacter->GetActorRotation();
    const FVector PreviousVelocity = RoamingCharacter->GetVelocity();
    const float PreviousHalfHeight = RoamingCharacter->GetCapsuleComponent()
        ? RoamingCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.0f;

    FString Error;
    UTwinCharacterAsset* TargetAsset = Cast<UTwinCharacterAsset>(
        ResolvePrimaryAsset(Target->PrimaryAssetId));
    bool bSucceeded = TargetAsset
        && RoamingCharacter->ApplyCharacterAsset(TargetAsset, Error);
    if (!TargetAsset && Error.IsEmpty())
    {
        Error = FString::Printf(
            TEXT("人物资源缺失：%s"), *Target->PrimaryAssetId);
    }

    if (bSucceeded && RoamingCharacter->SkinComponent)
    {
        RoamingCharacter->SkinComponent->Configure(
            Target->SkinPrimaryAssetIds, Target->DefaultSkinId);
        if (!Target->SkinPrimaryAssetIds.IsEmpty())
        {
            bSucceeded = RoamingCharacter->SkinComponent->ApplyDefaultSkin(Error);
        }
        else
        {
            RoamingCharacter->SkinComponent->ClearSkinOverrides();
        }
        if (bSucceeded)
        {
            RoamingCharacter->RefreshLocomotionAnimation();
        }
    }

    if (bSucceeded)
    {
        const float NewHalfHeight = RoamingCharacter->GetCapsuleComponent()
            ? RoamingCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
            : PreviousHalfHeight;
        const FVector PreservedFootLocation = PreviousLocation
            + FVector(0.0f, 0.0f, NewHalfHeight - PreviousHalfHeight);
        RoamingCharacter->SetActorLocationAndRotation(
            PreservedFootLocation,
            PreviousRotation,
            false,
            nullptr,
            ETeleportType::TeleportPhysics);
        if (UCharacterMovementComponent* Movement = RoamingCharacter->GetCharacterMovement())
        {
            Movement->Velocity = PreviousVelocity;
        }
        ActiveSessionCharacterId = Target->CharacterId;
        ActiveSessionCharacterDisplayName = Target->DisplayName;
        ActiveSessionCharacterPrimaryAssetId = Target->PrimaryAssetId;
        ActiveSessionDefaultSkinId = Target->DefaultSkinId;
        ActiveSessionSkinPrimaryAssetIds = Target->SkinPrimaryAssetIds;
        DegradedFeatures.Remove(TEXT("default_skin_missing"));
        LastError.Reset();
        UE_LOG(LogTemp, Log,
            TEXT("OntoTwin session character switched in place: %s -> %s"),
            *Previous.CharacterId, *Target->CharacterId);
    }
    else
    {
        FString RestoreError;
        UTwinCharacterAsset* PreviousAsset = Cast<UTwinCharacterAsset>(
            ResolvePrimaryAsset(Previous.PrimaryAssetId));
        if (PreviousAsset)
        {
            RoamingCharacter->ApplyCharacterAsset(PreviousAsset, RestoreError);
            if (RoamingCharacter->SkinComponent)
            {
                RoamingCharacter->SkinComponent->Configure(
                    Previous.SkinPrimaryAssetIds, Previous.DefaultSkinId);
                if (!PreviousSkinId.IsEmpty())
                {
                    RoamingCharacter->SkinComponent->ApplySkin(
                        PreviousSkinId, RestoreError);
                }
                else if (!Previous.DefaultSkinId.IsEmpty())
                {
                    RoamingCharacter->SkinComponent->ApplyDefaultSkin(RestoreError);
                }
                else
                {
                    RoamingCharacter->SkinComponent->ClearSkinOverrides();
                }
                RoamingCharacter->RefreshLocomotionAnimation();
            }
        }
        RoamingCharacter->SetActorLocationAndRotation(
            PreviousLocation,
            PreviousRotation,
            false,
            nullptr,
            ETeleportType::TeleportPhysics);
        if (UCharacterMovementComponent* Movement = RoamingCharacter->GetCharacterMovement())
        {
            Movement->Velocity = PreviousVelocity;
        }
        LastError = Error.IsEmpty() ? TEXT("人物切换失败，已恢复原人物")
            : FString::Printf(TEXT("人物切换失败，已恢复原人物：%s"), *Error);
        UE_LOG(LogTemp, Warning, TEXT("OntoTwin session character switch failed: %s"), *LastError);
    }

    bCharacterSwitchInProgress = false;
    RefreshHud();
    return bSucceeded;
}

void UTwinInteractionManagerComponent::CompleteRuntimeRouteSwitch()
{
    const FTwinRoamingRuntimeConfig PreviousConfig = CurrentConfig;
    const FTwinRoamingRuntimeRoute* Route = FindRuntimeRoute(
        CurrentConfig.AvailableRoutes, PendingRouteSwitchId);
    FString Error;
    bool bSucceeded = Route && IsRuntimeRouteForCurrentLevel(*Route)
        && RoamingCharacter && IsValid(RoamingCharacter);
    if (bSucceeded)
    {
        ApplyRuntimeRouteToConfig(CurrentConfig, *Route);
        ActiveRoute = BuildRuntimeRoute(Error);
        bSucceeded = ActiveRoute != nullptr;
    }
    if (bSucceeded)
    {
        RoamingCharacter->ApplyMovementSettings(CurrentConfig.Movement);
        RoamingCharacter->RouteFollower->Configure(
            ActiveRoute,
            CurrentConfig.Movement.AutoRouteSpeedCmS,
            CurrentConfig.bRouteLoop,
            false,
            CurrentConfig.RuntimeRouteWaypoints);
        bSucceeded = RoamingCharacter->RouteFollower->RestartFromBeginning(Error);
    }
    if (bSucceeded)
    {
        const FVector StartDirection = ActiveRoute->Spline->GetDirectionAtSplinePoint(
            0, ESplineCoordinateSpace::World);
        if (!StartDirection.IsNearlyZero())
        {
            RoamingCharacter->SetActorRotation(FRotator(
                0.0f, StartDirection.Rotation().Yaw, 0.0f));
        }
        SessionSelectedRouteId = CurrentConfig.RouteId;
        RuntimeState = TEXT("auto_route");
        LastError.Reset();
        DegradedFeatures.Remove(TEXT("route_missing"));
        DegradedFeatures.Remove(TEXT("runtime_route_start_rejected"));
    }
    else
    {
        if (CurrentConfig.RouteId != PreviousConfig.RouteId)
        {
            CurrentConfig = PreviousConfig;
            FString RestoreError;
            ActiveRoute = BuildRuntimeRoute(RestoreError);
            if (ActiveRoute && RoamingCharacter)
            {
                RoamingCharacter->ApplyMovementSettings(CurrentConfig.Movement);
                RoamingCharacter->RouteFollower->Configure(
                    ActiveRoute,
                    CurrentConfig.Movement.AutoRouteSpeedCmS,
                    CurrentConfig.bRouteLoop,
                    false,
                    CurrentConfig.RuntimeRouteWaypoints);
                RoamingCharacter->RouteFollower->TryResume(RestoreError);
            }
        }
        LastError = Error.IsEmpty() ? TEXT("漫游路线切换失败") : Error;
        RuntimeState = TEXT("manual");
    }

    PendingRouteSwitchId.Reset();
    bRouteSwitchInProgress = false;
    if (PlayerController && PlayerController->PlayerCameraManager)
    {
        PlayerController->PlayerCameraManager->StartCameraFade(
            1.0f, 0.0f, 0.20f, FLinearColor::Black, false, false);
    }
    RefreshHud();
}

void UTwinInteractionManagerComponent::CancelRuntimeRouteSwitch(bool bRestoreView)
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(RouteSwitchTimer);
    }
    PendingRouteSwitchId.Reset();
    bRouteSwitchInProgress = false;
    if (bRestoreView && PlayerController && PlayerController->PlayerCameraManager)
    {
        PlayerController->PlayerCameraManager->StopCameraFade();
    }
}

void UTwinInteractionManagerComponent::ApplyPendingReload()
{
    if (!bPendingReload) return;
    const bool bWasActive = bRoamingActive;
    ExitRoaming();
    CurrentConfig = PendingConfig;
    AppliedRevision = PendingRevision;
    RuntimeToken = PendingRuntimeToken;
    PendingRevision = -1;
    PendingRuntimeToken.Reset();
    bPendingReload = false;
    if (bWasActive)
    {
        FString Error;
        if (!EnterRoaming(Error))
        {
            RuntimeState = TEXT("blocked");
            LastError = Error;
        }
    }
}

void UTwinInteractionManagerComponent::CreateHud()
{
    if (!PlayerController) return;
    if (!RoamingHUD)
    {
        UClass* WidgetClass = RoamingHUDClass
            ? RoamingHUDClass.Get() : UOntoTwinRoamingHUDWidget::StaticClass();
        RoamingHUD = CreateWidget<UOntoTwinRoamingHUDWidget>(PlayerController, WidgetClass);
        if (RoamingHUD)
        {
            RoamingHUD->SetInteractionManager(this);
            RoamingHUD->AddToViewport(850);
            RoamingHUD->SetPositionInViewport(FVector2D::ZeroVector, true);
            RoamingHUD->SetAnchorsInViewport(FAnchors(0.0f, 0.0f));
            RoamingHUD->SetAlignmentInViewport(FVector2D::ZeroVector);
        }
    }
    if (!RuntimeDock)
    {
        UClass* WidgetClass = RuntimeDockClass
            ? RuntimeDockClass.Get() : UOntoTwinRuntimeDockWidget::StaticClass();
        RuntimeDock = CreateWidget<UOntoTwinRuntimeDockWidget>(
            PlayerController, WidgetClass);
        if (RuntimeDock)
        {
            RuntimeDock->SetInteractionManager(this);
            RuntimeDock->AddToViewport(860);
            RuntimeDock->SetPositionInViewport(FVector2D::ZeroVector, true);
            RuntimeDock->SetAnchorsInViewport(FAnchors(0.0f, 0.0f));
            RuntimeDock->SetAlignmentInViewport(FVector2D::ZeroVector);
            RuntimeDock->SetDockOpen(bHudInteraction);
        }
    }
    if (bRoamingActive && !CrosshairHUD)
    {
        CrosshairHUD = CreateWidget<UOntoTwinCrosshairWidget>(
            PlayerController, UOntoTwinCrosshairWidget::StaticClass());
        if (CrosshairHUD)
        {
            CrosshairHUD->AddToViewport(851);
            CrosshairHUD->SetPositionInViewport(FVector2D::ZeroVector, false);
            CrosshairHUD->SetAnchorsInViewport(FAnchors(0.0f, 0.0f));
            CrosshairHUD->SetAlignmentInViewport(FVector2D(0.5f, 0.5f));
            CrosshairHUD->SetDesiredSizeInViewport(FVector2D(46.0f, 46.0f));
        }
    }
    if (bRoamingActive && !NarrationHUD)
    {
        UClass* WidgetClass = NarrationHUDClass
            ? NarrationHUDClass.Get() : UOntoTwinNarrationHUDWidget::StaticClass();
        NarrationHUD = CreateWidget<UOntoTwinNarrationHUDWidget>(
            PlayerController, WidgetClass);
        if (NarrationHUD)
        {
            NarrationHUD->SetInteractionManager(this);
            NarrationHUD->AddToViewport(852);
            NarrationHUD->SetPositionInViewport(FVector2D::ZeroVector, true);
            NarrationHUD->SetAnchorsInViewport(FAnchors(0.0f, 0.0f));
            NarrationHUD->SetAlignmentInViewport(FVector2D::ZeroVector);
            NarrationHUD->HideNarration();
        }
    }
    RefreshHud();
}

void UTwinInteractionManagerComponent::DestroyHud()
{
    if (RoamingHUD && IsValid(RoamingHUD)) RoamingHUD->RemoveFromParent();
    RoamingHUD = nullptr;
    if (RuntimeDock && IsValid(RuntimeDock)) RuntimeDock->RemoveFromParent();
    RuntimeDock = nullptr;
    if (CrosshairHUD && IsValid(CrosshairHUD)) CrosshairHUD->RemoveFromParent();
    CrosshairHUD = nullptr;
    if (NarrationHUD && IsValid(NarrationHUD)) NarrationHUD->RemoveFromParent();
    NarrationHUD = nullptr;
    if (NarrationAudioComponent)
    {
        NarrationAudioComponent->Stop();
        NarrationAudioComponent->UnregisterComponent();
    }
    NarrationAudioComponent = nullptr;
    NarrationSound = nullptr;
    bFirstPersonCrosshairEnabled = false;
    bCrosshairInteractive = false;
}

void UTwinInteractionManagerComponent::RefreshHud()
{
    if (RoamingHUD)
    {
        if (PlayerController)
        {
            int32 ViewportX = 0;
            int32 ViewportY = 0;
            PlayerController->GetViewportSize(ViewportX, ViewportY);
            const float ViewportScale = FMath::Max(
                0.01f, UWidgetLayoutLibrary::GetViewportScale(this));
            RoamingHUD->SetDesiredSizeInViewport(
                FVector2D(ViewportX / ViewportScale, ViewportY / ViewportScale));
        }
        RoamingHUD->RefreshFromManager();
    }
    if (RuntimeDock)
    {
        if (PlayerController)
        {
            int32 ViewportX = 0;
            int32 ViewportY = 0;
            PlayerController->GetViewportSize(ViewportX, ViewportY);
            const float ViewportScale = FMath::Max(
                0.01f, UWidgetLayoutLibrary::GetViewportScale(this));
            RuntimeDock->SetDesiredSizeInViewport(
                FVector2D(ViewportX / ViewportScale, ViewportY / ViewportScale));
        }
        RuntimeDock->RefreshFromManager();
    }
    if (NarrationHUD && PlayerController)
    {
        int32 ViewportX = 0;
        int32 ViewportY = 0;
        PlayerController->GetViewportSize(ViewportX, ViewportY);
        const float ViewportScale = FMath::Max(
            0.01f, UWidgetLayoutLibrary::GetViewportScale(this));
        NarrationHUD->SetDesiredSizeInViewport(
            FVector2D(ViewportX / ViewportScale, ViewportY / ViewportScale));
    }
    if (CrosshairHUD)
    {
        if (PlayerController)
        {
            int32 ViewportX = 0;
            int32 ViewportY = 0;
            PlayerController->GetViewportSize(ViewportX, ViewportY);
            CrosshairHUD->SetPositionInViewport(
                FVector2D(ViewportX * 0.5f, ViewportY * 0.5f), true);
        }
        const bool bFirstPerson = bRoamingActive
            && RoamingCharacter
            && RoamingCharacter->CameraMode
            && RoamingCharacter->CameraMode->GetMode() == ETwinRoamingCameraMode::FirstPerson;
        CrosshairHUD->SetReticleState(
            bFirstPersonCrosshairEnabled
                && bFirstPerson
                && !bHudInteraction
                && !IsCameraTransitioning(),
            bCrosshairInteractive);
    }
}

void UTwinInteractionManagerComponent::UpdateCrosshairTarget()
{
    bCrosshairInteractive = false;
    if (!bFirstPersonCrosshairEnabled || !bRoamingActive || bHudInteraction
        || !PlayerController || !SceneManager
        || !RoamingCharacter || !RoamingCharacter->CameraMode
        || RoamingCharacter->CameraMode->IsTransitioning()
        || RoamingCharacter->CameraMode->GetMode() != ETwinRoamingCameraMode::FirstPerson)
    {
        return;
    }

    int32 ViewportX = 0;
    int32 ViewportY = 0;
    PlayerController->GetViewportSize(ViewportX, ViewportY);
    FVector Origin;
    FVector Direction;
    if (!PlayerController->DeprojectScreenPositionToWorld(
        ViewportX * 0.5f, ViewportY * 0.5f, Origin, Direction))
    {
        return;
    }

    FHitResult Hit;
    FCollisionQueryParams Params(
        SCENE_QUERY_STAT(TwinRoamingCrosshairTarget), true, RoamingCharacter);
    if (!GetWorld()->LineTraceSingleByChannel(
        Hit, Origin, Origin + Direction * 100000.0f, ECC_Visibility, Params))
    {
        return;
    }

    ATwinInstance* Instance = ResolveInteractionInstance(Hit);
    if (!Instance)
    {
        Instance = SceneManager->FindOverlayInstanceNearHit(Hit);
    }
    bCrosshairInteractive = Instance && Instance->HasSelectedOverlay();
}

void UTwinInteractionManagerComponent::BuildDefaultInputContext()
{
    if (DefaultMappingContext) return;
    DefaultMappingContext = NewObject<UInputMappingContext>(this, TEXT("OntoTwinRoamingDefaultContext"));

    auto MakeAction = [this](const TCHAR* Name, EInputActionValueType Type)
    {
        UInputAction* Action = NewObject<UInputAction>(this, Name);
        Action->ValueType = Type;
        return Action;
    };
    ToggleAction = MakeAction(TEXT("IA_TwinRoamingToggle"), EInputActionValueType::Boolean);
    MoveAction = MakeAction(TEXT("IA_TwinRoamingMove"), EInputActionValueType::Axis2D);
    LookAction = MakeAction(TEXT("IA_TwinRoamingLook"), EInputActionValueType::Axis2D);
    LookCaptureAction = MakeAction(TEXT("IA_TwinRoamingLookCapture"), EInputActionValueType::Boolean);
    VerticalAction = MakeAction(TEXT("IA_TwinRoamingVertical"), EInputActionValueType::Axis1D);
    HudAction = MakeAction(TEXT("IA_TwinRoamingHUD"), EInputActionValueType::Boolean);
    InteractAction = MakeAction(TEXT("IA_TwinRoamingInteract"), EInputActionValueType::Boolean);
    RouteAction = MakeAction(TEXT("IA_TwinRoamingRoute"), EInputActionValueType::Boolean);
    PauseAction = MakeAction(TEXT("IA_TwinRoamingPauseRoute"), EInputActionValueType::Boolean);
    JumpAction = MakeAction(TEXT("IA_TwinRoamingJump"), EInputActionValueType::Boolean);
    CrouchAction = MakeAction(TEXT("IA_TwinRoamingCrouch"), EInputActionValueType::Boolean);
    SprintAction = MakeAction(TEXT("IA_TwinRoamingSprint"), EInputActionValueType::Boolean);
    SelectAction = MakeAction(TEXT("IA_TwinRoamingSelect"), EInputActionValueType::Boolean);
    SpeedAction = MakeAction(TEXT("IA_TwinRoamingSpeed"), EInputActionValueType::Axis1D);

    DefaultMappingContext->MapKey(HudAction, ToggleHudKey);
    DefaultMappingContext->MapKey(InteractAction, InteractKey);
    DefaultMappingContext->MapKey(RouteAction, ResumeRouteKey);
    DefaultMappingContext->MapKey(PauseAction, PauseRouteKey);
    DefaultMappingContext->MapKey(JumpAction, EKeys::SpaceBar);
    DefaultMappingContext->MapKey(CrouchAction, EKeys::C);
    DefaultMappingContext->MapKey(SprintAction, EKeys::LeftShift);
    DefaultMappingContext->MapKey(SelectAction, EKeys::LeftMouseButton);
    DefaultMappingContext->MapKey(LookAction, EKeys::Mouse2D);
    DefaultMappingContext->MapKey(LookCaptureAction, MouseLookKey);
    DefaultMappingContext->MapKey(SpeedAction, EKeys::MouseWheelAxis);

    FEnhancedActionKeyMapping& MoveForward = DefaultMappingContext->MapKey(MoveAction, EKeys::W);
    UInputModifierSwizzleAxis* ForwardSwizzle = NewObject<UInputModifierSwizzleAxis>(DefaultMappingContext);
    ForwardSwizzle->Order = EInputAxisSwizzle::YXZ;
    MoveForward.Modifiers.Add(ForwardSwizzle);

    FEnhancedActionKeyMapping& MoveBackward = DefaultMappingContext->MapKey(MoveAction, EKeys::S);
    UInputModifierSwizzleAxis* BackwardSwizzle = NewObject<UInputModifierSwizzleAxis>(DefaultMappingContext);
    BackwardSwizzle->Order = EInputAxisSwizzle::YXZ;
    MoveBackward.Modifiers.Add(BackwardSwizzle);
    MoveBackward.Modifiers.Add(NewObject<UInputModifierNegate>(DefaultMappingContext));

    FEnhancedActionKeyMapping& MoveLeft = DefaultMappingContext->MapKey(MoveAction, EKeys::A);
    MoveLeft.Modifiers.Add(NewObject<UInputModifierNegate>(DefaultMappingContext));
    DefaultMappingContext->MapKey(MoveAction, EKeys::D);

    FEnhancedActionKeyMapping& MoveDown = DefaultMappingContext->MapKey(VerticalAction, EKeys::Q);
    MoveDown.Modifiers.Add(NewObject<UInputModifierNegate>(DefaultMappingContext));
    DefaultMappingContext->MapKey(VerticalAction, EKeys::E);
}

void UTwinInteractionManagerComponent::SetupInput()
{
    if (!PlayerController) return;
    BuildDefaultInputContext();
    if (!bEnhancedInputReady)
    {
        BindEnhancedInput();
    }
    if (bRoamingActive)
    {
        ActivateRoamingInput();
    }
}

void UTwinInteractionManagerComponent::ActivateRoamingInput()
{
    if (!PlayerController || ActiveMappingContext) return;
    BuildDefaultInputContext();
    if (ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
            LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
        {
            Subsystem->AddMappingContext(DefaultMappingContext, 20);
            ActiveMappingContext = DefaultMappingContext;
        }
    }
}

void UTwinInteractionManagerComponent::DeactivateRoamingInput()
{
    if (!PlayerController || !ActiveMappingContext) return;
    if (ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
            LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
        {
            Subsystem->RemoveMappingContext(ActiveMappingContext);
        }
    }
    ActiveMappingContext = nullptr;
}

void UTwinInteractionManagerComponent::BindEnhancedInput()
{
    UEnhancedInputComponent* Input = PlayerController
        ? Cast<UEnhancedInputComponent>(PlayerController->InputComponent) : nullptr;
    if (!Input)
    {
        bEnhancedInputReady = false;
        return;
    }
    Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &UTwinInteractionManagerComponent::OnMove);
    Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &UTwinInteractionManagerComponent::OnLook);
    Input->BindAction(LookCaptureAction, ETriggerEvent::Started, this, &UTwinInteractionManagerComponent::OnLookCaptureStarted);
    Input->BindAction(LookCaptureAction, ETriggerEvent::Completed, this, &UTwinInteractionManagerComponent::OnLookCaptureEnded);
    Input->BindAction(LookCaptureAction, ETriggerEvent::Canceled, this, &UTwinInteractionManagerComponent::OnLookCaptureEnded);
    Input->BindAction(VerticalAction, ETriggerEvent::Triggered, this, &UTwinInteractionManagerComponent::OnVertical);
    Input->BindAction(HudAction, ETriggerEvent::Started, this, &UTwinInteractionManagerComponent::OnToggleHud);
    Input->BindAction(InteractAction, ETriggerEvent::Started, this, &UTwinInteractionManagerComponent::OnInteract);
    Input->BindAction(RouteAction, ETriggerEvent::Started, this, &UTwinInteractionManagerComponent::OnRoute);
    Input->BindAction(PauseAction, ETriggerEvent::Started, this, &UTwinInteractionManagerComponent::OnPauseRoute);
    Input->BindAction(JumpAction, ETriggerEvent::Started, this, &UTwinInteractionManagerComponent::OnJump);
    Input->BindAction(CrouchAction, ETriggerEvent::Started, this, &UTwinInteractionManagerComponent::OnCrouchStarted);
    Input->BindAction(CrouchAction, ETriggerEvent::Completed, this, &UTwinInteractionManagerComponent::OnCrouchEnded);
    Input->BindAction(SprintAction, ETriggerEvent::Started, this, &UTwinInteractionManagerComponent::OnSprintStarted);
    Input->BindAction(SprintAction, ETriggerEvent::Completed, this, &UTwinInteractionManagerComponent::OnSprintEnded);
    Input->BindAction(SelectAction, ETriggerEvent::Started, this, &UTwinInteractionManagerComponent::OnSelect);
    Input->BindAction(SpeedAction, ETriggerEvent::Triggered, this, &UTwinInteractionManagerComponent::OnAdjustSpeed);
    bEnhancedInputReady = true;
}

void UTwinInteractionManagerComponent::RemoveInput()
{
    DeactivateRoamingInput();
    bEnhancedInputReady = false;
}

void UTwinInteractionManagerComponent::TickFallbackInput(float DeltaTime)
{
    if (!PlayerController) return;
    if (!bRoamingActive || !RoamingCharacter) return;
    if (PlayerController->WasInputKeyJustPressed(MouseLookKey)) SetRoamingMouseLook(true);
    if (PlayerController->WasInputKeyJustReleased(MouseLookKey)) SetRoamingMouseLook(false);
    if (RoamingCharacter->CameraMode->IsTransitioning()) return;
    if (PlayerController->WasInputKeyJustPressed(ToggleHudKey)) ToggleHudInteraction();
    if (bHudInteraction) return;
    if (PlayerController->WasInputKeyJustPressed(ResumeRouteKey)) ResumeRoute();
    if (PlayerController->WasInputKeyJustPressed(PauseRouteKey)) HandleRoutePauseAction();

    const bool bGod = RoamingCharacter->CameraMode->GetMode() == ETwinRoamingCameraMode::God;
    if (PlayerController->WasInputKeyJustPressed(EKeys::LeftMouseButton))
    {
        if (!PlayerController->IsInputKeyDown(MouseLookKey))
        {
            SelectFromView(bGod || !bRoamingMouseLook);
        }
    }
    if (!bGod && PlayerController->WasInputKeyJustPressed(InteractKey)
        && !PlayerController->IsInputKeyDown(MouseLookKey))
    {
        SelectFromView(false);
    }
    if (PlayerController->WasInputKeyJustPressed(EKeys::SpaceBar)
        && RoamingCharacter->CameraMode->GetMode() != ETwinRoamingCameraMode::God)
    {
        RoamingCharacter->Jump();
    }

    FVector2D Move(
        (PlayerController->IsInputKeyDown(EKeys::D) ? 1.0f : 0.0f)
            - (PlayerController->IsInputKeyDown(EKeys::A) ? 1.0f : 0.0f),
        (PlayerController->IsInputKeyDown(EKeys::W) ? 1.0f : 0.0f)
            - (PlayerController->IsInputKeyDown(EKeys::S) ? 1.0f : 0.0f));
    Move = Move.GetClampedToMaxSize(1.0f);
    if (!Move.IsNearlyZero())
    {
        if (bGod && RoamingCharacter->CameraMode->GetGodPawn())
        {
            RoamingCharacter->CameraMode->GetGodPawn()->MovePlanar(Move);
        }
        else
        {
            if (bTakeoverEnabled)
            {
                StopNarration(true);
                RoamingCharacter->RouteFollower->PauseByUser();
            }
            RoamingCharacter->MoveRelativeToView(
                Move, PlayerController->IsInputKeyDown(EKeys::LeftShift));
        }
    }

    float MouseX = 0.0f;
    float MouseY = 0.0f;
    PlayerController->GetInputMouseDelta(MouseX, MouseY);
    if (bGod)
    {
        if (bRoamingMouseLook)
        {
            RoamingCharacter->CameraMode->GetGodPawn()->Look(FVector2D(MouseX, MouseY));
        }
        const float Vertical = (PlayerController->IsInputKeyDown(EKeys::E) ? 1.0f : 0.0f)
            - (PlayerController->IsInputKeyDown(EKeys::Q) ? 1.0f : 0.0f);
        RoamingCharacter->CameraMode->GetGodPawn()->MoveVertical(Vertical);
        RoamingCharacter->CameraMode->GetGodPawn()->AdjustSpeed(
            PlayerController->GetInputAnalogKeyState(EKeys::MouseWheelAxis));
    }
    else
    {
        const float Sensitivity = RoamingCharacter->CameraMode->GetMode()
                == ETwinRoamingCameraMode::FirstPerson
            ? CurrentConfig.FirstPersonCamera.LookSensitivity
            : CurrentConfig.NearCamera.LookSensitivity;
        if (bRoamingMouseLook)
        {
            RoamingCharacter->Look(FVector2D(MouseX, MouseY), Sensitivity);
        }
        if (PlayerController->WasInputKeyJustPressed(EKeys::C)) RoamingCharacter->Crouch();
        if (PlayerController->WasInputKeyJustReleased(EKeys::C)) RoamingCharacter->UnCrouch();
    }
}

void UTwinInteractionManagerComponent::OnMove(const FInputActionValue& Value)
{
    if (!bRoamingActive || bHudInteraction || !RoamingCharacter
        || RoamingCharacter->CameraMode->IsTransitioning()) return;
    const FVector2D Move = Value.Get<FVector2D>().GetClampedToMaxSize(1.0f);
    if (Move.IsNearlyZero()) return;
    if (RoamingCharacter->CameraMode->GetMode() == ETwinRoamingCameraMode::God)
    {
        if (ATwinGodViewPawn* Pawn = RoamingCharacter->CameraMode->GetGodPawn()) Pawn->MovePlanar(Move);
    }
    else
    {
        if (bTakeoverEnabled)
        {
            StopNarration(true);
            RoamingCharacter->RouteFollower->PauseByUser();
        }
        RoamingCharacter->MoveRelativeToView(Move, bSprintHeld);
    }
}

void UTwinInteractionManagerComponent::OnLook(const FInputActionValue& Value)
{
    if (!bRoamingActive || bHudInteraction || !RoamingCharacter || !PlayerController
        || RoamingCharacter->CameraMode->IsTransitioning() || !bRoamingMouseLook) return;
    const FVector2D Look = Value.Get<FVector2D>();
    if (RoamingCharacter->CameraMode->GetMode() == ETwinRoamingCameraMode::God)
    {
        if (ATwinGodViewPawn* Pawn = RoamingCharacter->CameraMode->GetGodPawn()) Pawn->Look(Look);
    }
    else
    {
        const float Sensitivity = RoamingCharacter->CameraMode->GetMode()
                == ETwinRoamingCameraMode::FirstPerson
            ? CurrentConfig.FirstPersonCamera.LookSensitivity
            : CurrentConfig.NearCamera.LookSensitivity;
        RoamingCharacter->Look(Look, Sensitivity);
    }
}

void UTwinInteractionManagerComponent::OnVertical(const FInputActionValue& Value)
{
    if (!bRoamingActive || bHudInteraction || !RoamingCharacter
        || RoamingCharacter->CameraMode->IsTransitioning()) return;
    if (RoamingCharacter->CameraMode->GetMode() == ETwinRoamingCameraMode::God)
    {
        if (ATwinGodViewPawn* Pawn = RoamingCharacter->CameraMode->GetGodPawn())
        {
            Pawn->MoveVertical(Value.Get<float>());
        }
    }
}

void UTwinInteractionManagerComponent::OnToggle(const FInputActionValue&) { ToggleRoaming(); }
void UTwinInteractionManagerComponent::OnToggleHud(const FInputActionValue&) { ToggleHudInteraction(); }
void UTwinInteractionManagerComponent::OnRoute(const FInputActionValue&) { ResumeRoute(); }
void UTwinInteractionManagerComponent::OnPauseRoute(const FInputActionValue&)
{
    HandleRoutePauseAction();
}

void UTwinInteractionManagerComponent::OnLookCaptureStarted(const FInputActionValue&)
{
    SetRoamingMouseLook(true);
}

void UTwinInteractionManagerComponent::OnLookCaptureEnded(const FInputActionValue&)
{
    if (bRoamingActive && !bHudInteraction)
    {
        SetRoamingMouseLook(false);
    }
}

void UTwinInteractionManagerComponent::OnInteract(const FInputActionValue&)
{
    if (RoamingCharacter
        && !RoamingCharacter->CameraMode->IsTransitioning()
        && RoamingCharacter->CameraMode->GetMode() != ETwinRoamingCameraMode::God)
    {
        SelectFromView(false);
    }
}

void UTwinInteractionManagerComponent::OnSelect(const FInputActionValue&)
{
    // Slate consumes the minimap event, but Enhanced Input can still observe
    // the same physical pointer on some Pixel Streaming paths. Keep the guard
    // short so it cannot swallow a later, unrelated scene selection.
    if (LastMinimapPointerEventSeconds >= 0.0
        && FPlatformTime::Seconds() - LastMinimapPointerEventSeconds <= 0.20)
    {
        return;
    }
    if (RoamingCharacter && !RoamingCharacter->CameraMode->IsTransitioning()
        && (!PlayerController || !PlayerController->IsInputKeyDown(MouseLookKey)))
    {
        const bool bGodMode =
            RoamingCharacter->CameraMode->GetMode() == ETwinRoamingCameraMode::God;
        SelectFromView(bGodMode || !bRoamingMouseLook);
    }
}

void UTwinInteractionManagerComponent::OnJump(const FInputActionValue&)
{
    if (RoamingCharacter && !bHudInteraction
        && !RoamingCharacter->CameraMode->IsTransitioning()
        && RoamingCharacter->CameraMode->GetMode() != ETwinRoamingCameraMode::God)
    {
        RoamingCharacter->Jump();
    }
}

void UTwinInteractionManagerComponent::HandleRoutePauseAction()
{
    if (!RoamingCharacter || bHudInteraction
        || RoamingCharacter->CameraMode->IsTransitioning())
    {
        return;
    }

    UTwinRouteFollowerComponent* RouteFollower = RoamingCharacter->RouteFollower;
    if (RouteFollower)
    {
        const ETwinRoamingRouteState RouteState = RouteFollower->GetRouteState();
        if (RouteState == ETwinRoamingRouteState::AutoRoute
            || RouteState == ETwinRoamingRouteState::Joining)
        {
            RouteFollower->PauseByUser();
            LastError.Reset();
            RefreshHud();
            return;
        }
        if (RouteState == ETwinRoamingRouteState::PausedByUser)
        {
            ResumeRoute();
            return;
        }
        if (RouteState == ETwinRoamingRouteState::PausedForNarration)
        {
            // 只切换解说完成后的路线行为；当前字幕和语音保持播放。
            RouteFollower->TogglePauseAfterNarration();
            RefreshHud();
            return;
        }
    }

}

void UTwinInteractionManagerComponent::OnCrouchStarted(const FInputActionValue&)
{
    if (RoamingCharacter && !bHudInteraction
        && !RoamingCharacter->CameraMode->IsTransitioning()
        && RoamingCharacter->CameraMode->GetMode() != ETwinRoamingCameraMode::God)
    {
        RoamingCharacter->Crouch();
    }
}

void UTwinInteractionManagerComponent::OnCrouchEnded(const FInputActionValue&)
{
    if (RoamingCharacter) RoamingCharacter->UnCrouch();
}

void UTwinInteractionManagerComponent::OnSprintStarted(const FInputActionValue&) { bSprintHeld = true; }
void UTwinInteractionManagerComponent::OnSprintEnded(const FInputActionValue&) { bSprintHeld = false; }

void UTwinInteractionManagerComponent::OnAdjustSpeed(const FInputActionValue& Value)
{
    if (RoamingCharacter && !bHudInteraction
        && !RoamingCharacter->CameraMode->IsTransitioning()
        && RoamingCharacter->CameraMode->GetMode() == ETwinRoamingCameraMode::God)
    {
        if (ATwinGodViewPawn* Pawn = RoamingCharacter->CameraMode->GetGodPawn())
        {
            Pawn->AdjustSpeed(Value.Get<float>());
        }
    }
}

void UTwinInteractionManagerComponent::SelectFromView(bool bCursorTrace)
{
    if (!PlayerController || !SceneManager || bHudInteraction || IsCameraTransitioning()) return;
    FHitResult Hit;
    bool bHit = false;
    int32 ViewportX = 0;
    int32 ViewportY = 0;
    PlayerController->GetViewportSize(ViewportX, ViewportY);
    FVector2D ScreenPoint(ViewportX * 0.5f, ViewportY * 0.5f);
    if (bCursorTrace)
    {
        float MouseX = 0.0f;
        float MouseY = 0.0f;
        if (PlayerController->GetMousePosition(MouseX, MouseY))
        {
            ScreenPoint = FVector2D(MouseX, MouseY);
        }
        bHit = PlayerController->GetHitResultUnderCursor(ECC_Visibility, true, Hit);
    }
    else
    {
        FVector Origin;
        FVector Direction;
        if (PlayerController->DeprojectScreenPositionToWorld(
            ViewportX * 0.5f, ViewportY * 0.5f, Origin, Direction))
        {
            FCollisionQueryParams Params(SCENE_QUERY_STAT(TwinRoamingSelect), true, RoamingCharacter);
            bHit = GetWorld()->LineTraceSingleByChannel(
                Hit, Origin, Origin + Direction * 100000.0f, ECC_Visibility, Params);
        }
    }
    ATwinInstance* Instance = bHit ? ResolveInteractionInstance(Hit) : nullptr;
    if (!Instance && bHit)
    {
        Instance = SceneManager->FindOverlayInstanceNearHit(Hit);
    }
    const bool bSelectedByOverlay = !Instance
        && SceneManager->SelectOverlayAtScreenPosition(ScreenPoint);
    UE_LOG(LogTemp, Log,
        TEXT("OntoTwin interaction select mode=%s hit=%s actor=%s instance=%s overlay=%s screen_overlay=%s point=(%.1f,%.1f)"),
        bCursorTrace ? TEXT("cursor") : TEXT("crosshair"),
        bHit ? TEXT("true") : TEXT("false"),
        bHit && Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("none"),
        Instance ? *Instance->GetInstanceId() : TEXT("none"),
        Instance && Instance->HasOverlay() ? TEXT("true") : TEXT("false"),
        bSelectedByOverlay ? TEXT("true") : TEXT("false"),
        ScreenPoint.X,
        ScreenPoint.Y);
    if (Instance && Instance->HasOverlay())
    {
        SceneManager->SelectOverlayFromSceneInteraction(Instance);
    }
    else if (!bSelectedByOverlay)
    {
        SceneManager->ClearOverlayFromSceneInteraction();
    }

    if (Instance || !bSelectedByOverlay) HandleWebInstanceSelection(Instance);
}

void UTwinInteractionManagerComponent::HandleGlobalPointerSelection(
    const FHitResult* Hit)
{
    if (!SceneManager || bRoamingActive || bHudInteraction
        || SceneManager->IsRuntimeEditModeActive()) return;

    ATwinInstance* Instance = Hit ? ResolveInteractionInstance(*Hit) : nullptr;
    if (!Instance && Hit)
    {
        Instance = SceneManager->FindOverlayInstanceNearHit(*Hit);
    }
    HandleWebInstanceSelection(Instance);
}

void UTwinInteractionManagerComponent::HandleWebInstanceSelection(
    ATwinInstance* Instance)
{
    if (Instance)
    {
        WebSelectedInstance = Instance;
        const bool bOpened = SceneManager
            && SceneManager->WebInteractionManager
            && SceneManager->WebInteractionManager->OpenInstanceDetail(
                Instance->GetInstanceId());
        CompleteWebOpen(
            bOpened,
            TEXT("已选择对象；当前对象没有可用的网页详情绑定。"),
            false);
        return;
    }
    else
    {
        WebSelectedInstance.Reset();
        LastWebInteractionMessage.Reset();
    }
    RefreshHud();
}

void UTwinInteractionManagerComponent::HandleNarrationRequested(
    const FTwinRoamingRuntimeWaypoint& Waypoint)
{
    StopNarration(false);
    if (!Waypoint.HasNarration())
    {
        if (RoamingCharacter && RoamingCharacter->RouteFollower)
        {
            RoamingCharacter->RouteFollower->CompleteNarration();
        }
        return;
    }
    ActiveNarrationWaypoint = Waypoint;
    ActiveNarrationSegmentIndex = 0;
    bNarrationActive = true;
    RuntimeState = TEXT("route_narration");
    ShowNarrationSegment();
    RefreshHud();
}

void UTwinInteractionManagerComponent::ShowNarrationSegment()
{
    if (!bNarrationActive
        || !ActiveNarrationWaypoint.NarrationSegments.IsValidIndex(
            ActiveNarrationSegmentIndex))
    {
        FinishNarrationPoint();
        return;
    }
    const FTwinNarrationRuntimeSegment& Segment =
        ActiveNarrationWaypoint.NarrationSegments[ActiveNarrationSegmentIndex];
    const bool bVoiceRequested = ActiveNarrationWaypoint.NarrationMode != TEXT("subtitle");
    if (NarrationHUD)
    {
        if (ActiveNarrationWaypoint.NarrationMode == TEXT("voice"))
        {
            NarrationHUD->HideNarration();
        }
        else
        {
            NarrationHUD->ShowSegment(
                Segment.Text,
                ActiveNarrationSegmentIndex,
                ActiveNarrationWaypoint.NarrationSegments.Num(),
                ActiveNarrationWaypoint.NarrationMode,
                false,
                true);
        }
    }
    if (bVoiceRequested && TryStartNarrationAudio(Segment)) return;
    if (bVoiceRequested)
    {
        DegradedFeatures.AddUnique(TEXT("audio_fallback_to_subtitle"));
        if (NarrationHUD
            && ActiveNarrationWaypoint.NarrationMode != TEXT("voice"))
        {
            NarrationHUD->ShowSegment(
                Segment.Text,
                ActiveNarrationSegmentIndex,
                ActiveNarrationWaypoint.NarrationSegments.Num(),
                ActiveNarrationWaypoint.NarrationMode,
                true,
                true);
        }
    }
    StartNarrationFallbackTimer(Segment);
}

void UTwinInteractionManagerComponent::StartNarrationFallbackTimer(
    const FTwinNarrationRuntimeSegment& Segment)
{
    const float Duration = FMath::Clamp(Segment.DurationSeconds, 1.0f, 600.0f);
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimer(
            NarrationTimer,
            this,
            &UTwinInteractionManagerComponent::SkipNarrationSegment,
            Duration,
            false);
    }
}

FString UTwinInteractionManagerComponent::NarrationCachePath(
    const FTwinNarrationRuntimeSegment& Segment) const
{
    if (!HasValidSha256(Segment.AudioSha256)) return FString();
    return FPaths::Combine(
        FPaths::ProjectSavedDir(),
        TEXT("OntoTwin"),
        TEXT("Narration"),
        Segment.AudioSha256.ToLower() + TEXT(".wav"));
}

bool UTwinInteractionManagerComponent::TryStartNarrationAudio(
    const FTwinNarrationRuntimeSegment& Segment)
{
    if (Segment.AudioAssetId.IsEmpty() || !HasValidSha256(Segment.AudioSha256))
    {
        return false;
    }
    const FString CachePath = NarrationCachePath(Segment);
    TArray<uint8> CachedBytes;
    if (!CachePath.IsEmpty()
        && FFileHelper::LoadFileToArray(CachedBytes, *CachePath))
    {
        if (MatchesSha256(CachedBytes, Segment.AudioSha256)
            && PlayNarrationWav(CachedBytes, Segment))
        {
            return true;
        }
        IFileManager::Get().Delete(*CachePath, false, true);
    }
    if (!SceneManager || !bBackendOnline) return false;

    FString BaseUrl = SceneManager->BackendBaseUrl;
    BaseUrl.RemoveFromEnd(TEXT("/"));
    const FString ExpectedSegmentId = Segment.SegmentId;
    const FTwinNarrationRuntimeSegment ExpectedSegment = Segment;
    NarrationAudioRequest = FHttpModule::Get().CreateRequest();
    NarrationAudioRequest->SetURL(
        BaseUrl
        + TEXT("/api/v2/scene-interactions/narration-assets/")
        + FGenericPlatformHttp::UrlEncode(Segment.AudioAssetId));
    NarrationAudioRequest->SetVerb(TEXT("GET"));
    AddProjectHeaders(NarrationAudioRequest.ToSharedRef());
    const TWeakObjectPtr<UTwinInteractionManagerComponent> WeakThis(this);
    NarrationAudioRequest->OnProcessRequestComplete().BindLambda(
        [WeakThis, ExpectedSegmentId, ExpectedSegment, CachePath](
            FHttpRequestPtr,
            FHttpResponsePtr Response,
            bool bWasSuccessful)
        {
            UTwinInteractionManagerComponent* Self = WeakThis.Get();
            if (!Self) return;
            Self->NarrationAudioRequest.Reset();
            if (!Self->bNarrationActive
                || !Self->ActiveNarrationWaypoint.NarrationSegments.IsValidIndex(
                    Self->ActiveNarrationSegmentIndex)
                || Self->ActiveNarrationWaypoint.NarrationSegments[
                    Self->ActiveNarrationSegmentIndex].SegmentId != ExpectedSegmentId)
            {
                return;
            }
            const bool bValidResponse = bWasSuccessful && Response.IsValid()
                && EHttpResponseCodes::IsOk(Response->GetResponseCode());
            const TArray<uint8>& Bytes = bValidResponse
                ? Response->GetContent() : TArray<uint8>();
            if (bValidResponse
                && MatchesSha256(Bytes, ExpectedSegment.AudioSha256)
                && Self->PlayNarrationWav(Bytes, ExpectedSegment))
            {
                if (!CachePath.IsEmpty())
                {
                    IFileManager::Get().MakeDirectory(
                        *FPaths::GetPath(CachePath), true);
                    const FString TemporaryPath = CachePath + TEXT(".tmp");
                    if (FFileHelper::SaveArrayToFile(Bytes, *TemporaryPath))
                    {
                        IFileManager::Get().Move(
                            *CachePath, *TemporaryPath, true, true, false, true);
                    }
                }
                return;
            }
            Self->DegradedFeatures.AddUnique(TEXT("audio_fallback_to_subtitle"));
            if (Self->NarrationHUD
                && Self->ActiveNarrationWaypoint.NarrationMode != TEXT("voice"))
            {
                Self->NarrationHUD->ShowSegment(
                    ExpectedSegment.Text,
                    Self->ActiveNarrationSegmentIndex,
                    Self->ActiveNarrationWaypoint.NarrationSegments.Num(),
                    Self->ActiveNarrationWaypoint.NarrationMode,
                    true,
                    true);
            }
            Self->StartNarrationFallbackTimer(ExpectedSegment);
        });
    if (!NarrationAudioRequest->ProcessRequest())
    {
        NarrationAudioRequest.Reset();
        return false;
    }
    return true;
}

bool UTwinInteractionManagerComponent::FocusInstanceCamera(
    const FTransform& TargetTransform,
    float FovDegrees,
    FString& OutError)
{
    if (!bRoamingActive || !RoamingCharacter || !RoamingCharacter->CameraMode
        || !PlayerController)
    {
        OutError = TEXT("Character roaming camera is not active");
        return false;
    }
    const bool bFocused = RoamingCharacter->CameraMode->FocusAtTransform(
        PlayerController,
        GodViewAnchor,
        TargetTransform,
        FovDegrees,
        OutError);
    if (bFocused)
    {
        RuntimeState = TEXT("god_view");
        RefreshHud();
    }
    return bFocused;
}

bool UTwinInteractionManagerComponent::PlayNarrationWav(
    const TArray<uint8>& Bytes,
    const FTwinNarrationRuntimeSegment& Segment)
{
    // FWaveModInfo repairs malformed streaming WAV chunk lengths in place.
    // Parse a private copy so validation/playback never mutates the downloaded
    // bytes that will be written to the content-addressed cache.
    TArray<uint8> ParsedBytes = Bytes;
    FWaveModInfo WaveInfo;
    FString Error;
    if (!WaveInfo.ReadWaveInfo(ParsedBytes.GetData(), ParsedBytes.Num(), &Error)
        || !WaveInfo.pFormatTag
        || *WaveInfo.pFormatTag != FWaveModInfo::WAVE_INFO_FORMAT_PCM
        || !WaveInfo.pBitsPerSample || *WaveInfo.pBitsPerSample != 16
        || !WaveInfo.pChannels || *WaveInfo.pChannels != 1
        || !WaveInfo.pSamplesPerSec
        || (*WaveInfo.pSamplesPerSec != 8000 && *WaveInfo.pSamplesPerSec != 16000)
        || !WaveInfo.SampleDataStart || WaveInfo.SampleDataSize == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("OntoTwin narration WAV rejected: %s"), *Error);
        return false;
    }
    const int32 BytesPerFrame = *WaveInfo.pChannels
        * (*WaveInfo.pBitsPerSample / 8);
    const int32 PlayableBytes = BytesPerFrame > 0
        ? WaveInfo.SampleDataSize - (WaveInfo.SampleDataSize % BytesPerFrame)
        : 0;
    if (PlayableBytes <= 0)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("OntoTwin narration WAV rejected: no complete PCM frames"));
        return false;
    }
    const float PlaybackDuration = static_cast<float>(PlayableBytes)
        / static_cast<float>(*WaveInfo.pSamplesPerSec * BytesPerFrame);
    if (!NarrationAudioComponent)
    {
        NarrationAudioComponent = NewObject<UAudioComponent>(
            this, TEXT("OntoTwinNarrationAudio"));
        NarrationAudioComponent->bAutoActivate = false;
        NarrationAudioComponent->bAllowSpatialization = false;
        NarrationAudioComponent->bIsUISound = true;
        NarrationAudioComponent->RegisterComponentWithWorld(GetWorld());
    }
    if (NarrationAudioComponent->IsPlaying()) NarrationAudioComponent->Stop();
    NarrationSound = NewObject<USoundWaveProcedural>(this);
    NarrationSound->NumChannels = *WaveInfo.pChannels;
    NarrationSound->SetSampleRate(*WaveInfo.pSamplesPerSec);
    NarrationSound->Duration = PlaybackDuration;
    NarrationSound->SoundGroup = SOUNDGROUP_Voice;
    NarrationSound->QueueAudio(WaveInfo.SampleDataStart, PlayableBytes);
    NarrationAudioComponent->SetSound(NarrationSound);
    NarrationAudioComponent->Play();
    if (UWorld* World = GetWorld())
    {
        // USoundWaveProcedural does not reliably emit OnAudioFinished after its
        // queued PCM is consumed. Resume from the measured PCM duration with a
        // short mixer grace period instead of provider metadata + five seconds.
        const float CompletionDuration = FMath::Clamp(
            PlaybackDuration + 0.2f, 0.4f, 120.2f);
        World->GetTimerManager().SetTimer(
            NarrationTimer,
            this,
            &UTwinInteractionManagerComponent::SkipNarrationSegment,
            CompletionDuration,
            false);
        UE_LOG(LogTemp, Log,
            TEXT("OntoTwin narration playback segment=%s pcm=%.3fs metadata=%.3fs resume=%.3fs"),
            *Segment.SegmentId,
            PlaybackDuration,
            Segment.AudioDurationSeconds,
            CompletionDuration);
    }
    DegradedFeatures.Remove(TEXT("audio_fallback_to_subtitle"));
    return true;
}

void UTwinInteractionManagerComponent::SkipNarrationSegment()
{
    if (!bNarrationActive) return;
    if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(NarrationTimer);
    if (NarrationAudioRequest.IsValid())
    {
        NarrationAudioRequest->CancelRequest();
        NarrationAudioRequest.Reset();
    }
    if (NarrationAudioComponent && NarrationAudioComponent->IsPlaying())
    {
        NarrationAudioComponent->Stop();
    }
    NarrationSound = nullptr;
    ++ActiveNarrationSegmentIndex;
    UE_LOG(LogTemp, Log,
        TEXT("OntoTwin narration advance next_segment=%d total=%d"),
        ActiveNarrationSegmentIndex + 1,
        ActiveNarrationWaypoint.NarrationSegments.Num());
    if (ActiveNarrationWaypoint.NarrationSegments.IsValidIndex(
        ActiveNarrationSegmentIndex))
    {
        ShowNarrationSegment();
    }
    else
    {
        FinishNarrationPoint();
    }
}

void UTwinInteractionManagerComponent::FinishNarrationPoint()
{
    if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(NarrationTimer);
    bNarrationActive = false;
    if (NarrationAudioRequest.IsValid())
    {
        NarrationAudioRequest->CancelRequest();
        NarrationAudioRequest.Reset();
    }
    if (NarrationAudioComponent && NarrationAudioComponent->IsPlaying())
    {
        NarrationAudioComponent->Stop();
    }
    NarrationSound = nullptr;
    ActiveNarrationSegmentIndex = -1;
    ActiveNarrationWaypoint = FTwinRoamingRuntimeWaypoint();
    if (NarrationHUD) NarrationHUD->HideNarration();
    if (RoamingCharacter && RoamingCharacter->RouteFollower)
    {
        RoamingCharacter->RouteFollower->CompleteNarration();
    }
    RuntimeState = TEXT("auto_route");
    RefreshHud();
}

void UTwinInteractionManagerComponent::StopNarration(bool bInterruptedByUser)
{
    if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(NarrationTimer);
    if (bInterruptedByUser && bNarrationActive
        && RoamingCharacter && RoamingCharacter->RouteFollower)
    {
        RoamingCharacter->RouteFollower->InterruptNarrationByUser();
    }
    bNarrationActive = false;
    if (NarrationAudioRequest.IsValid())
    {
        NarrationAudioRequest->CancelRequest();
        NarrationAudioRequest.Reset();
    }
    if (NarrationAudioComponent && NarrationAudioComponent->IsPlaying())
    {
        NarrationAudioComponent->Stop();
    }
    NarrationSound = nullptr;
    ActiveNarrationSegmentIndex = -1;
    ActiveNarrationWaypoint = FTwinRoamingRuntimeWaypoint();
    if (NarrationHUD) NarrationHUD->HideNarration();
}

ATwinInstance* UTwinInteractionManagerComponent::ResolveInteractionInstance(
    const FHitResult& Hit) const
{
    AActor* Candidate = Hit.GetActor();
    TSet<const AActor*> Visited;
    for (int32 Depth = 0; Candidate && Depth < 8 && !Visited.Contains(Candidate); ++Depth)
    {
        Visited.Add(Candidate);
        if (ATwinInstance* Instance = Cast<ATwinInstance>(Candidate))
        {
            return Instance;
        }

        AActor* Parent = Candidate->GetAttachParentActor();
        Candidate = Parent ? Parent : Candidate->GetOwner();
    }
    return nullptr;
}

void UTwinInteractionManagerComponent::SendHeartbeat()
{
    if (bShuttingDown || !SceneManager || !bBackendOnline) return;

    FString State = RuntimeState;
    FString CameraMode;
    FString RouteState;
    FString ActiveSkin;
    if (bPendingReload) State = TEXT("reload_required");
    else if (!CurrentConfig.bEnabled) State = TEXT("disabled");
    else if (!bRoamingActive) State = TEXT("available");
    else if (bHudInteraction) State = TEXT("ui_interaction");
    else if (RoamingCharacter)
    {
        const ETwinRoamingCameraMode ActiveCameraMode =
            RoamingCharacter->CameraMode->GetMode();
        const bool bGod = ActiveCameraMode == ETwinRoamingCameraMode::God;
        CameraMode = CameraModeId(ActiveCameraMode);
        RouteState = RoamingCharacter->RouteFollower->GetRouteStateText();
        ActiveSkin = RoamingCharacter->SkinComponent->GetActiveSkinId();
        if (bNarrationActive) State = TEXT("route_narration");
        else if (bGod) State = TEXT("god_view");
        else if (RoamingCharacter->RouteFollower->IsFollowing()) State = TEXT("auto_route");
        else State = TEXT("manual");
    }

    const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
    if (AppliedRevision >= 0) Payload->SetNumberField(TEXT("applied_revision"), AppliedRevision);
    else Payload->SetField(TEXT("applied_revision"), MakeShared<FJsonValueNull>());
    if (PendingRevision >= 0) Payload->SetNumberField(TEXT("pending_revision"), PendingRevision);
    else Payload->SetField(TEXT("pending_revision"), MakeShared<FJsonValueNull>());
    Payload->SetStringField(TEXT("catalog_version"), CatalogVersion);
    Payload->SetStringField(TEXT("runtime_state"), State);
    Payload->SetStringField(TEXT("camera_mode"), CameraMode);
    Payload->SetStringField(TEXT("route_state"), RouteState);
    Payload->SetStringField(TEXT("active_character_id"), GetActiveRuntimeCharacterId());
    Payload->SetStringField(TEXT("active_skin_id"), ActiveSkin);
    Payload->SetStringField(TEXT("minimap_state"), MinimapState);
    TArray<TSharedPtr<FJsonValue>> DegradedValues;
    for (const FString& Feature : DegradedFeatures)
    {
        DegradedValues.Add(MakeShared<FJsonValueString>(Feature));
    }
    Payload->SetArrayField(TEXT("degraded_features"), DegradedValues);
    Payload->SetObjectField(TEXT("realtime_channel"), SceneManager->BuildRealtimeChannelHealth());
    if (LastError.IsEmpty())
    {
        Payload->SetField(TEXT("error"), MakeShared<FJsonValueNull>());
    }
    else
    {
        const TSharedRef<FJsonObject> Error = MakeShared<FJsonObject>();
        Error->SetStringField(TEXT("message"), LastError);
        Payload->SetObjectField(TEXT("error"), Error);
    }

    FString Body;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
    FJsonSerializer::Serialize(Payload, Writer);

    FString BaseUrl = SceneManager->BackendBaseUrl;
    BaseUrl.RemoveFromEnd(TEXT("/"));
    const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(BaseUrl + TEXT("/api/v2/scene-interactions/runtime"));
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    AddProjectHeaders(Request);
    Request->SetContentAsString(Body);
    const TWeakObjectPtr<UTwinInteractionManagerComponent> WeakThis(this);
    Request->OnProcessRequestComplete().BindLambda(
        [WeakThis](FHttpRequestPtr, FHttpResponsePtr Response, bool bWasSuccessful)
        {
            UTwinInteractionManagerComponent* Self = WeakThis.Get();
            if (!Self || Self->bShuttingDown || !bWasSuccessful || !Response.IsValid()) return;
            if (Response->GetResponseCode() == 403)
            {
                Self->LastError = TEXT("UE project binding was rejected while reporting roaming status");
                Self->ExitRoaming();
                Self->RuntimeState = TEXT("blocked");
                return;
            }

            if (Response->GetResponseCode() >= 200 && Response->GetResponseCode() < 300)
            {
                TSharedPtr<FJsonObject> ResponsePayload;
                const TSharedRef<TJsonReader<>> ResponseReader =
                    TJsonReaderFactory<>::Create(Response->GetContentAsString());
                if (FJsonSerializer::Deserialize(ResponseReader, ResponsePayload)
                    && ResponsePayload.IsValid())
                {
                    const TSharedPtr<FJsonObject> Control =
                        GetObject(ResponsePayload, TEXT("realtime_control"));
                    bool bRealtimeEnabled = false;
                    if (Control.IsValid()
                        && Control->TryGetBoolField(TEXT("enabled"), bRealtimeEnabled)
                        && Self->SceneManager)
                    {
                        Self->SceneManager->ApplyRealtimeWebSocketEnabled(bRealtimeEnabled);
                    }
                }
            }
        });
    Request->ProcessRequest();
}

FString UTwinInteractionManagerComponent::GetHudStatusText() const
{
    if (!bRoamingActive) return TEXT("场景交互");
    const FString Character = CompactHudLabel(
        ActiveSessionCharacterDisplayName.IsEmpty()
            ? CurrentConfig.CharacterDisplayName
            : ActiveSessionCharacterDisplayName,
        TEXT("漫游人物"), 16);
    const FString View = RoamingCharacter
        ? CameraModeLabel(RoamingCharacter->CameraMode->GetMode())
        : TEXT("视角不可用");

    FString RouteStatus = TEXT("自由漫游");
    if (RoamingCharacter && RoamingCharacter->RouteFollower)
    {
        const FString RouteName = CompactHudLabel(
            CurrentConfig.RouteDisplayName, TEXT("当前线路"), 24);
        switch (RoamingCharacter->RouteFollower->GetRouteState())
        {
        case ETwinRoamingRouteState::PausedForNarration:
            RouteStatus = FString::Printf(TEXT("路线解说 - %s"), *RouteName);
            break;
        case ETwinRoamingRouteState::AutoRoute:
            RouteStatus = FString::Printf(TEXT("线路漫游 - %s"), *RouteName);
            break;
        case ETwinRoamingRouteState::Joining:
            RouteStatus = FString::Printf(TEXT("正在返回线路 - %s"), *RouteName);
            break;
        case ETwinRoamingRouteState::Blocked:
            RouteStatus = FString::Printf(
                TEXT("线路受阻 - %s · %s"),
                *RouteName,
                *RoamingCharacter->RouteFollower->GetBlockedDiagnosticText());
            break;
        case ETwinRoamingRouteState::Completed:
            RouteStatus = FString::Printf(TEXT("线路已完成 - %s"), *RouteName);
            break;
        default:
            break;
        }
    }
    const FString ViewStatus = IsCameraTransitioning()
        ? View + TEXT(" · 切换中") : View;
    return FString::Printf(
        TEXT("%s   ·   %s   ·   %s"), *Character, *ViewStatus, *RouteStatus);
}

FString UTwinInteractionManagerComponent::GetHudHintText() const
{
    TArray<FString> Keys;
    TArray<FString> Descriptions;
    GetHudShortcutItems(Keys, Descriptions);
    FString Text;
    for (int32 Index = 0; Index < Keys.Num() && Index < Descriptions.Num(); ++Index)
    {
        if (!Text.IsEmpty()) Text += TEXT("\n");
        Text += Keys[Index] + TEXT("  ") + Descriptions[Index];
    }
    return Text;
}

void UTwinInteractionManagerComponent::GetHudShortcutItems(
    TArray<FString>& OutKeys,
    TArray<FString>& OutDescriptions) const
{
    OutKeys.Reset();
    OutDescriptions.Reset();
    const auto AddShortcut = [&OutKeys, &OutDescriptions](
        const FString& Key,
        const FString& Description)
    {
        OutKeys.Add(Key);
        OutDescriptions.Add(Description);
    };

    if (bHudInteraction)
    {
        AddShortcut(ToggleHudKey.ToString(), TEXT("收起操作"));
        AddShortcut(TEXT("鼠标"), TEXT("选择按钮"));
        AddShortcut(
            ToggleRoamingKey.ToString(),
            bRoamingActive ? TEXT("退出漫游") : TEXT("进入漫游"));
        return;
    }

    if (!bRoamingActive)
    {
        AddShortcut(
            TEXT("左键"),
            TEXT("选择并打开网页详情"));
        AddShortcut(ToggleHudKey.ToString(), TEXT("Web 业务"));
        AddShortcut(ToggleRoamingKey.ToString(), TEXT("进入漫游"));
        return;
    }

    const ETwinRoamingCameraMode Mode = GetCameraMode();
    const ETwinRoamingRouteState RouteState = RoamingCharacter
        && RoamingCharacter->RouteFollower
        ? RoamingCharacter->RouteFollower->GetRouteState()
        : ETwinRoamingRouteState::Unavailable;
    const bool bRouteCanPause = RouteState == ETwinRoamingRouteState::AutoRoute
        || RouteState == ETwinRoamingRouteState::Joining;
    const bool bRoutePausedByUser = RouteState == ETwinRoamingRouteState::PausedByUser;
    const bool bNarrationPlaying = RouteState == ETwinRoamingRouteState::PausedForNarration;
    const bool bPauseAfterNarration = bNarrationPlaying
        && RoamingCharacter->RouteFollower->WillPauseAfterNarration();
    if (Mode == ETwinRoamingCameraMode::God)
    {
        AddShortcut(TEXT("WASD"), TEXT("平移"));
        AddShortcut(TEXT("右键"), TEXT("按住观察"));
        AddShortcut(TEXT("Q / E"), TEXT("升降"));
        AddShortcut(TEXT("滚轮"), TEXT("调整速度"));
        if (bRouteCanPause)
        {
            AddShortcut(PauseRouteKey.ToString(), TEXT("暂停路线"));
        }
        else if (bRoutePausedByUser)
        {
            AddShortcut(PauseRouteKey.ToString(), TEXT("继续路线"));
        }
        else if (bNarrationPlaying)
        {
            AddShortcut(
                PauseRouteKey.ToString(),
                bPauseAfterNarration ? TEXT("解说后继续") : TEXT("解说后暂停"));
        }
        AddShortcut(
            TEXT("左键"),
            TEXT("选择并打开网页详情"));
        AddShortcut(ToggleViewKey.ToString(), TEXT("切换视角"));
        AddShortcut(ToggleHudKey.ToString(), TEXT("更多操作"));
        AddShortcut(ToggleRoamingKey.ToString(), TEXT("退出漫游"));
        return;
    }

    AddShortcut(TEXT("WASD"), TEXT("移动"));
    AddShortcut(
        TEXT("右键"),
        TEXT("按住观察"));
    AddShortcut(
        TEXT("左键"),
        TEXT("选择详情"));
    AddShortcut(TEXT("E"), TEXT("准星选择"));
    AddShortcut(TEXT("Space"), TEXT("跳跃"));
    if (bRouteCanPause)
    {
        AddShortcut(PauseRouteKey.ToString(), TEXT("暂停路线"));
    }
    else if (bRoutePausedByUser)
    {
        AddShortcut(PauseRouteKey.ToString(), TEXT("继续路线"));
        AddShortcut(ResumeRouteKey.ToString(), TEXT("返回线路"));
    }
    else if (bNarrationPlaying)
    {
        AddShortcut(
            PauseRouteKey.ToString(),
            bPauseAfterNarration ? TEXT("解说后继续") : TEXT("解说后暂停"));
    }
    AddShortcut(ToggleViewKey.ToString(), TEXT("切换视角"));
    AddShortcut(ToggleHudKey.ToString(), TEXT("更多操作"));
    AddShortcut(ToggleRoamingKey.ToString(), TEXT("退出漫游"));
}

void UTwinInteractionManagerComponent::GetAvailableRuntimeRoutes(
    TArray<FString>& OutRouteIds,
    TArray<FString>& OutDisplayNames,
    TArray<bool>& OutDefaultFlags) const
{
    OutRouteIds.Reset();
    OutDisplayNames.Reset();
    OutDefaultFlags.Reset();
    for (const FTwinRoamingRuntimeRoute& Route : CurrentConfig.AvailableRoutes)
    {
        if (!IsRuntimeRouteForCurrentLevel(Route)) continue;
        OutRouteIds.Add(Route.RouteId);
        OutDisplayNames.Add(Route.DisplayName.IsEmpty() ? Route.RouteId : Route.DisplayName);
        OutDefaultFlags.Add(Route.bDefault);
    }
}

void UTwinInteractionManagerComponent::GetAvailableRuntimeCharacters(
    TArray<FString>& OutCharacterIds,
    TArray<FString>& OutDisplayNames) const
{
    OutCharacterIds.Reset();
    OutDisplayNames.Reset();
    for (const FTwinRoamingRuntimeCharacter& Character : CurrentConfig.AvailableCharacters)
    {
        if (Character.CharacterId.IsEmpty() || Character.PrimaryAssetId.IsEmpty()) continue;
        // Catalog entries are cross-project. The Dock only offers assets that
        // are actually installed in this packaged host project.
        if (!ResolvePrimaryAsset(Character.PrimaryAssetId)) continue;
        OutCharacterIds.Add(Character.CharacterId);
        OutDisplayNames.Add(Character.DisplayName.IsEmpty()
            ? Character.CharacterId : Character.DisplayName);
    }
}

void UTwinInteractionManagerComponent::GetAvailableWebZones(
    TArray<FString>& OutZoneIds,
    TArray<FString>& OutDisplayNames) const
{
    OutZoneIds.Reset();
    OutDisplayNames.Reset();
    if (SceneManager && SceneManager->WebInteractionManager)
    {
        SceneManager->WebInteractionManager->GetAvailableZones(
            OutZoneIds,
            OutDisplayNames);
    }
}

void UTwinInteractionManagerComponent::GetAvailableWebZoneTree(
    TArray<FString>& OutZoneIds,
    TArray<FString>& OutDisplayNames,
    TArray<FString>& OutParentZoneIds) const
{
    OutZoneIds.Reset();
    OutDisplayNames.Reset();
    OutParentZoneIds.Reset();
    if (SceneManager && SceneManager->WebInteractionManager)
    {
        SceneManager->WebInteractionManager->GetAvailableZoneTree(
            OutZoneIds,
            OutDisplayNames,
            OutParentZoneIds);
    }
}

void UTwinInteractionManagerComponent::GetAvailableWebBusinessViews(
    TArray<FString>& OutBusinessViewIds,
    TArray<FString>& OutDisplayNames) const
{
    OutBusinessViewIds.Reset();
    OutDisplayNames.Reset();
    if (SceneManager && SceneManager->WebInteractionManager)
    {
        SceneManager->WebInteractionManager->GetAvailableBusinessViews(
            OutBusinessViewIds,
            OutDisplayNames);
    }
}

void UTwinInteractionManagerComponent::GetAvailableWebBusinessViewSummaries(
    TArray<FString>& OutBusinessViewIds,
    TArray<FString>& OutDisplayNames,
    TArray<int32>& OutMemberCounts) const
{
    OutBusinessViewIds.Reset();
    OutDisplayNames.Reset();
    OutMemberCounts.Reset();
    if (SceneManager && SceneManager->WebInteractionManager)
    {
        SceneManager->WebInteractionManager->GetAvailableBusinessViewSummaries(
            OutBusinessViewIds,
            OutDisplayNames,
            OutMemberCounts);
    }
}

void UTwinInteractionManagerComponent::ActivateRuntimeHome()
{
    WebSelectedInstance.Reset();
    LastWebInteractionMessage.Reset();
    if (SceneManager && SceneManager->WebInteractionManager)
    {
        SceneManager->WebInteractionManager->ResetHome();
    }
    // “主页”表示回到非漫游的场景起点，不能只抢回启动镜头。
    // 走完整退出流程，确保人物、路线、输入、小地图和漫游状态 HUD 一并清理。
    if (bRoamingActive || RoamingCharacter)
    {
        ExitRoaming();
        return;
    }
    RestoreStartupView();
    SetHudInteraction(false);
    RefreshHud();
}

bool UTwinInteractionManagerComponent::CanToggleRuntimeEditor() const
{
    return SceneManager
        && SceneManager->bEnableRuntimeEditor
        && !SceneManager->IsRuntimeEditModeActive();
}

void UTwinInteractionManagerComponent::ToggleRuntimeEditor()
{
    if (!SceneManager) return;
    SceneManager->ToggleRuntimeEditMode();
    RefreshHud();
}

bool UTwinInteractionManagerComponent::CanToggleFirstPersonCrosshair() const
{
    return bRoamingActive
        && RoamingCharacter
        && RoamingCharacter->CameraMode
        && !RoamingCharacter->CameraMode->IsTransitioning()
        && RoamingCharacter->CameraMode->GetMode()
            == ETwinRoamingCameraMode::FirstPerson;
}

void UTwinInteractionManagerComponent::ToggleFirstPersonCrosshair()
{
    if (!CanToggleFirstPersonCrosshair()) return;
    bFirstPersonCrosshairEnabled = !bFirstPersonCrosshairEnabled;
    if (!bFirstPersonCrosshairEnabled)
    {
        bCrosshairInteractive = false;
    }
    RefreshHud();
}

void UTwinInteractionManagerComponent::SetRuntimeEditorSuppressed(bool bSuppressed)
{
    if (bSuppressed)
    {
        bRestoreHudAfterRuntimeEditor = bHudInteraction;
        SetHudInteraction(false);
        return;
    }
    if (bRestoreHudAfterRuntimeEditor)
    {
        SetHudInteraction(true);
    }
    bRestoreHudAfterRuntimeEditor = false;
}

bool UTwinInteractionManagerComponent::CompleteWebOpen(
    bool bOpened,
    const FString& FailureMessage,
    bool bRevealDockOnFailure)
{
    if (bOpened)
    {
        LastWebInteractionMessage.Reset();
        RefreshHud();
        return true;
    }

    const bool bRuntimeReady = SceneManager
        && SceneManager->WebInteractionManager
        && SceneManager->WebInteractionManager->IsRuntimeReady();
    LastWebInteractionMessage = bRuntimeReady
        ? FailureMessage
        : TEXT("Web 配置尚未就绪，请稍后重试。");
    // Scene selection must stay lightweight. A failed instance-detail lookup
    // records the message for the next explicit Tab-opened Dock, but must not
    // make every click expand the runtime UI. Dock-originated actions still
    // opt into reopening the Dock so their failure remains visible.
    if (bRevealDockOnFailure)
    {
        SetHudInteraction(true);
    }
    RefreshHud();
    return false;
}

bool UTwinInteractionManagerComponent::OpenWebProjectHome()
{
    SetHudInteraction(false);
    const bool bOpened = SceneManager
        && SceneManager->WebInteractionManager
        && SceneManager->WebInteractionManager->OpenProjectHome();
    return CompleteWebOpen(
        bOpened,
        TEXT("当前项目没有可用的主页绑定。"));
}

bool UTwinInteractionManagerComponent::OpenWebZone(const FString& ZoneId)
{
    SetHudInteraction(false);
    const bool bOpened = SceneManager
        && SceneManager->WebInteractionManager
        && SceneManager->WebInteractionManager->OpenZone(ZoneId);
    return CompleteWebOpen(
        bOpened,
        TEXT("当前分区没有可用的页面绑定，或该分区已失效。"));
}

bool UTwinInteractionManagerComponent::OpenWebBusinessView(
    const FString& BusinessViewId,
    const FString& ZoneId)
{
    SetHudInteraction(false);
    const bool bOpened = SceneManager
        && SceneManager->WebInteractionManager
        && SceneManager->WebInteractionManager->OpenBusinessView(
            BusinessViewId,
            ZoneId);
    return CompleteWebOpen(
        bOpened,
        TEXT("当前业务没有可用的页面绑定，或该业务已失效。"));
}

FString UTwinInteractionManagerComponent::GetHudDetailText() const
{
    FString Text;
    if (!bRoamingActive)
    {
        Text = TEXT("单击对象打开详情；空间、业务和漫游入口位于 Dock 标签中。");
    }
    else
    {
        Text = bPendingReload
            ? TEXT("存在待重载配置，可点击“重载人物”应用。")
            : TEXT("可在当前会话切换人物、视角或线路；人物选择不会改写网页默认配置。");
    }
    if (!bBackendOnline) Text += TEXT("  后端离线，当前使用最后一次有效配置。");
    if (!BindingWarning.IsEmpty()) Text += TEXT("\n") + BindingWarning;
    if (!LastError.IsEmpty()) Text += TEXT("\n提示：") + LastError;
    if (!LastWebInteractionMessage.IsEmpty())
    {
        Text += TEXT("\nWeb：") + LastWebInteractionMessage;
    }
    return Text;
}

void UTwinInteractionManagerComponent::NotifyRuntimeEditorBlocked()
{
    LastError = TEXT("人物漫游运行中；请先按 F7 退出，再进入 Runtime Editor");
    RefreshHud();
}
