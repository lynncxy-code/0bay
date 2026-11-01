#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "TwinInstance.h"
#include "BasicMotionComponent.h"
#include "BeltMaterialComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBasicMotionTest, "OntoTwin.Presentation.BasicModelMotion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBasicMotionTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    auto* Twin = World->SpawnActor<ATwinInstance>();
    Twin->InitializeTwin(TEXT("isolated-motion"), TEXT("/Engine/BasicShapes/Cube.Cube"), TEXT(""), TEXT(""), TEXT("primary"));
    auto* Model = Twin->FindComponentByClass<UStaticMeshComponent>();
    TestNotNull(TEXT("Actual model mesh"), Model);
    if (!Model) { World->DestroyWorld(false); return false; }
    Twin->SetActorLocation(FVector(200,300,400));
    const FTransform Business = Twin->GetActorTransform();
    auto Route = [Twin](const FString& Id, double Value) {
        auto P = MakeShared<FJsonObject>();
        P->SetStringField(TEXT("axis"), TEXT("z"));
        P->SetNumberField(TEXT("speed"), Value);
        P->SetNumberField(TEXT("distance"), 100);
        P->SetNumberField(TEXT("angle"), 90);
        P->SetNumberField(TEXT("duration"), 2);
        auto C = MakeShared<FJsonObject>();
        C->SetStringField(TEXT("behavior_id"), Id);
        C->SetStringField(TEXT("source"), TEXT("platform"));
        C->SetObjectField(TEXT("params"), P);
        auto Channels = MakeShared<FJsonObject>(); Channels->SetObjectField(TEXT("animation"), C);
        auto Resolution = MakeShared<FJsonObject>(); Resolution->SetObjectField(TEXT("channels"), Channels);
        auto Presentation = MakeShared<FJsonObject>(); Presentation->SetObjectField(TEXT("resolution"), Resolution);
        auto Interfaces = MakeShared<FJsonObject>(); Interfaces->SetObjectField(TEXT("I3D_Presentation"), Presentation);
        auto Snapshot = MakeShared<FJsonObject>(); Snapshot->SetObjectField(TEXT("interfaces"), Interfaces);
        Twin->ApplySnapshot(Snapshot);
    };
    Route(TEXT("motion.rotate"), 90);
    auto* Motion = Twin->FindComponentByClass<UBasicMotionComponent>();
    TestNotNull(TEXT("Basic executor loaded"), Motion);
    if (!Motion) { World->DestroyWorld(false); return false; }
    Motion->TickComponent(1, LEVELTICK_All, nullptr);
    TestTrue(TEXT("Model itself rotates 90 degrees"), FMath::IsNearlyEqual(Model->GetRelativeRotation().Yaw,90.,.1));
    TestTrue(TEXT("Business root unchanged"), Twin->GetActorTransform().Equals(Business));
    Route(TEXT("motion.rotate"), 45);
    Motion->TickComponent(1, LEVELTICK_All, nullptr);
    TestTrue(TEXT("Parameter-only change reaches executor"), FMath::IsNearlyEqual(Model->GetRelativeRotation().Yaw,45.,.1));
    Route(TEXT("motion.rotate"), 45);
    Motion->TickComponent(1, LEVELTICK_All, nullptr);
    TestTrue(TEXT("Repeated snapshots do not restart loop"), FMath::IsNearlyEqual(Model->GetRelativeRotation().Yaw,90.,.1));
    Route(TEXT("motion.translate"), 90);
    Motion->TickComponent(1, LEVELTICK_All, nullptr);
    TestTrue(TEXT("Model moves half of 100cm in one second"), FMath::IsNearlyEqual(Model->GetRelativeLocation().Z,50.,.1));
    Twin->SetActorLocation(FVector(600,700,800));
    Motion->TickComponent(1, LEVELTICK_All, nullptr);
    TestTrue(TEXT("Data position plus display offset"), Model->GetComponentLocation().Equals(FVector(600,700,900),.1));
    Motion->TickComponent(10, LEVELTICK_All, nullptr);
    TestTrue(TEXT("One-shot motion holds destination"), FMath::IsNearlyEqual(Model->GetRelativeLocation().Z,100.,.1));
    Route(TEXT("motion.pingpong"), 90);
    Motion->TickComponent(2, LEVELTICK_All, nullptr);
    TestTrue(TEXT("Pingpong reaches end"), FMath::IsNearlyEqual(Model->GetRelativeLocation().Z,100.,.1));
    Motion->TickComponent(2, LEVELTICK_All, nullptr);
    TestTrue(TEXT("Pingpong returns"), Model->GetRelativeLocation().IsNearlyZero(.1));
    Route(TEXT("motion.rotate_to"), 90);
    Motion->TickComponent(2, LEVELTICK_All, nullptr);
    TestTrue(TEXT("Rotate to reaches angle"), FMath::IsNearlyEqual(Model->GetRelativeRotation().Yaw,90.,.1));
    Route(TEXT("motion.swing"), 90);
    Motion->TickComponent(2, LEVELTICK_All, nullptr);
    TestTrue(TEXT("Swing reaches angle"), FMath::IsNearlyEqual(Model->GetRelativeRotation().Yaw,90.,.1));
    Motion->TickComponent(2, LEVELTICK_All, nullptr);
    TestTrue(TEXT("Swing returns"), Model->GetRelativeRotation().IsNearlyZero(.1));
    Route(TEXT("motion.reset"), 90);
    TestTrue(TEXT("Reset restores model"), Model->GetRelativeTransform().Equals(FTransform::Identity,.1));
    TestTrue(TEXT("Reset preserves latest real position"), Twin->GetActorLocation().Equals(FVector(600,700,800),.1));
    auto Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("target"), TEXT("tagged"));
    Params->SetStringField(TEXT("axis"), TEXT("x"));
    Params->SetNumberField(TEXT("distance"), 60);
    Params->SetNumberField(TEXT("duration"), 1);
    auto* Door = NewObject<UStaticMeshComponent>(Twin, TEXT("Door"));
    Twin->AddInstanceComponent(Door);
    Door->SetupAttachment(Twin->GetRootComponent());
    Door->SetStaticMesh(Model->GetStaticMesh());
    Door->SetMobility(EComponentMobility::Movable);
    Door->ComponentTags.Add(TEXT("OntoTwinMotionTarget"));
    Door->SetRelativeLocation(FVector(20,0,0));
    Door->RegisterComponent();
    Motion->Apply(TEXT("motion.translate"), Params);
    Motion->TickComponent(1, LEVELTICK_All, nullptr);
    TestTrue(TEXT("Tagged door travels its stroke"), Door->GetRelativeLocation().Equals(FVector(80,0,0),.1));
    TestTrue(TEXT("Device body does not move with door"), Model->GetRelativeLocation().IsNearlyZero(.1));
    Motion->ResetMotion();
    TestTrue(TEXT("Door restores original local placement"), Door->GetRelativeLocation().Equals(FVector(20,0,0),.1));
    // Invalid wire parameters must stop existing motion safely.
    Params->SetNumberField(TEXT("duration"), 0);
    TestFalse(TEXT("Invalid duration rejected"), Motion->Apply(TEXT("motion.translate"), Params));
    auto* OriginalMaterial = Model->GetMaterial(0);
    auto BeltParams = MakeShared<FJsonObject>();
    BeltParams->SetNumberField(TEXT("speed"), .5);
    TestTrue(TEXT("Real belt material loads"), Twin->ExecutePresentationRoute(TEXT("visual"), TEXT("material.belt_scroll"), TEXT("status_indicator"), TEXT("platform"), BeltParams));
    auto* Belt = Twin->FindComponentByClass<UBeltMaterialComponent>();
    if (Belt && Belt->Material)
    {
        Belt->TickComponent(.5, LEVELTICK_All, nullptr);
        TestTrue(TEXT("Belt UV offset advances"), FMath::IsNearlyEqual(Belt->Material->K2_GetScalarParameterValue(TEXT("Offset")), .25f));
        TestTrue(TEXT("Belt material applied to actual model"), Model->GetMaterial(0) == Belt->Material);
        Twin->ExecutePresentationRoute(TEXT("visual"), TEXT("safe.visible"), TEXT("status_indicator"));
        TestTrue(TEXT("Belt reset restores original material"), Model->GetMaterial(0) == OriginalMaterial);
    }
    World->DestroyWorld(false);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPresentationFallbackPositionTest, "OntoTwin.Presentation.FallbackPreservesPosition",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPresentationFallbackPositionTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    auto* Twin = World->SpawnActor<ATwinInstance>();
    Twin->InitializeTwin(TEXT("fallback-position"), TEXT("/Engine/BasicShapes/Cube.Cube"), TEXT(""), TEXT(""), TEXT("primary"));
    Twin->DispatchBeginPlay();
    const FTransform Initial(FRotator(10,30,20), FVector(1234,5678,90), FVector(2,3,4));
    Twin->SetActorTransform(Initial);
    TestFalse(TEXT("Unknown route is not claimed as applied"), Twin->ExecutePresentationRoute(TEXT("animation"), TEXT("missing.animation"), TEXT("motion"), TEXT("project")));
    TestTrue(TEXT("Uninitialized legacy baseline never teleports to zero"), Twin->GetActorTransform().Equals(Initial,.01));
    Twin->PlayPresentationAnimation(TEXT("translate"));
    static_cast<AActor*>(Twin)->Tick(.5f);
    TestFalse(TEXT("Test precondition: legacy motion actually runs"), Twin->GetActorTransform().Equals(Initial,.01));
    const FTransform Latest(FRotator(5,120,0), FVector(9000,8000,700), FVector(2,3,4));
    Twin->SetActorTransform(Latest);
    Twin->ExecutePresentationRoute(TEXT("animation"), TEXT("missing.animation"), TEXT("motion"), TEXT("project"));
    static_cast<AActor*>(Twin)->Tick(.5f);
    TestTrue(TEXT("Failed route stops legacy animation without restoring stale location"), Twin->GetActorTransform().Equals(Latest,.01));
    Twin->PlayPresentationAnimation(TEXT("translate"));
    auto Channels = MakeShared<FJsonObject>();
    auto Route = MakeShared<FJsonObject>();
    Route->SetStringField(TEXT("behavior_id"), TEXT("safe.idle"));
    Channels->SetObjectField(TEXT("animation"), Route);
    auto Resolution = MakeShared<FJsonObject>(); Resolution->SetObjectField(TEXT("channels"), Channels);
    auto Presentation = MakeShared<FJsonObject>(); Presentation->SetObjectField(TEXT("resolution"), Resolution);
    auto Interfaces = MakeShared<FJsonObject>(); Interfaces->SetObjectField(TEXT("I3D_Presentation"), Presentation);
    auto Snapshot = MakeShared<FJsonObject>(); Snapshot->SetObjectField(TEXT("interfaces"), Interfaces);
    Twin->ApplySnapshot(Snapshot);
    static_cast<AActor*>(Twin)->Tick(.5f);
    TestTrue(TEXT("Authoritative clear stops legacy motion without moving actor"), Twin->GetActorTransform().Equals(Latest,.01));
    World->DestroyWorld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlainLabelTest, "OntoTwin.Presentation.TypeOwnedPlainLabel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPlainLabelTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    auto* Twin = World->SpawnActor<ATwinInstance>();
    Twin->InitializeTwin(TEXT("type-label"), TEXT("/Engine/BasicShapes/Cube.Cube"), TEXT(""), TEXT(""), TEXT("primary"));
    auto* Label = Twin->FindComponentByClass<UTextRenderComponent>();
    TestNotNull(TEXT("Plain label component exists"), Label);
    if (!Label) { World->DestroyWorld(false); return false; }
    TestFalse(TEXT("Default hidden"), Label->IsVisible());
    auto Interfaces = MakeShared<FJsonObject>();
    auto Visual = MakeShared<FJsonObject>();
    Visual->SetStringField(TEXT("ui_label_content"), TEXT("Device A"));
    Interfaces->SetObjectField(TEXT("I3D_Visual"), Visual);
    auto Snapshot = MakeShared<FJsonObject>();
    Snapshot->SetObjectField(TEXT("interfaces"), Interfaces);
    Twin->ApplySnapshot(Snapshot);
    TestTrue(TEXT("Visual-only type can display a label"), Label->IsVisible());
    TestEqual(TEXT("Type-resolved text applied"), Label->Text.ToString(), FString(TEXT("Device A")));
    auto Overlay = MakeShared<FJsonObject>();
    Overlay->SetBoolField(TEXT("enabled"), true);
    Overlay->SetStringField(TEXT("display_mode"), TEXT("selected"));
    Interfaces->SetObjectField(TEXT("I3D_Overlay"), Overlay);
    Twin->ApplySnapshot(Snapshot);
    TestTrue(TEXT("Popup configuration does not suppress plain label"), Label->IsVisible());
    Visual->SetStringField(TEXT("ui_label_content"), TEXT(""));
    Twin->ApplySnapshot(Snapshot);
    TestFalse(TEXT("Turning type label off clears visible label"), Label->IsVisible());
    TestTrue(TEXT("Old text removed"), Label->Text.IsEmpty());
    Interfaces->RemoveField(TEXT("I3D_Overlay"));
    Twin->ApplySnapshot(Snapshot);
    TestFalse(TEXT("Removing popup never resurrects cleared text"), Label->IsVisible());
    Visual->SetStringField(TEXT("ui_label_content"), TEXT("Device B"));
    Twin->ApplySnapshot(Snapshot);
    Interfaces->RemoveField(TEXT("I3D_Visual"));
    Twin->ApplySnapshot(Snapshot);
    TestFalse(TEXT("Removing capability clears label"), Label->IsVisible());
    World->DestroyWorld(false);
    return true;
}
#endif
