#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "TwinInstance.h"
#include "IndustrialPresentationComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIndustrialPresentationTest,
    "OntoTwin.Presentation.ExecutableChannels",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIndustrialPresentationTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    auto* Twin = World->SpawnActor<ATwinInstance>();
    Twin->InitializeTwin(TEXT("isolated-presentation-test"), TEXT("/Engine/BasicShapes/Cube.Cube"), TEXT(""), TEXT(""), TEXT("primary"));
    const FTransform Original = Twin->GetActorTransform();
    auto MakeSnapshot = [](bool Active) {
        auto Snapshot = MakeShared<FJsonObject>();
        auto Interfaces = MakeShared<FJsonObject>();
        auto Presentation = MakeShared<FJsonObject>();
        auto Resolution = MakeShared<FJsonObject>();
        auto Channels = MakeShared<FJsonObject>();
        const TMap<FString,FString> Behaviors = Active
            ? TMap<FString,FString>{{TEXT("animation"),TEXT("industrial.machine.running")}, {TEXT("fx"),TEXT("industrial.fx.warning_flash")}, {TEXT("visual"),TEXT("industrial.visual.warning")}, {TEXT("label"),TEXT("industrial.label.demo")}}
            : TMap<FString,FString>{{TEXT("animation"),TEXT("safe.idle")}, {TEXT("fx"),TEXT("safe.none")}, {TEXT("visual"),TEXT("safe.visible")}, {TEXT("label"),TEXT("safe.label")}};
        for (const auto& Pair : Behaviors)
        {
            auto Channel = MakeShared<FJsonObject>();
            Channel->SetStringField(TEXT("behavior_id"), Pair.Value);
            Channel->SetStringField(TEXT("source"), TEXT("platform"));
            Channels->SetObjectField(Pair.Key, Channel);
        }
        Resolution->SetObjectField(TEXT("channels"), Channels);
        Presentation->SetObjectField(TEXT("resolution"), Resolution);
        Interfaces->SetObjectField(TEXT("I3D_Presentation"), Presentation);
        Snapshot->SetObjectField(TEXT("interfaces"), Interfaces);
        return Snapshot;
    };
    Twin->ApplySnapshot(MakeSnapshot(true));
    auto* Component = Twin->FindComponentByClass<UIndustrialPresentationComponent>();
    TestNotNull(TEXT("Executor created component"), Component);
    if (Component)
    {
        TestNotNull(TEXT("Animation has real mesh and material"), Component->Motion.Get());
        TestNotNull(TEXT("FX has real mesh and material"), Component->Beacon.Get());
        TestNotNull(TEXT("Label was created"), Component->StatusLabel.Get());
        if (Component->Motion && Component->Beacon)
        {
            Component->TickComponent(.25f, LEVELTICK_All, nullptr);
            TestTrue(TEXT("Rotation advances"), FMath::Abs(Component->Motion->GetRelativeRotation().Yaw) > 1.f);
            const bool FirstFlash = Component->Beacon->IsVisible();
            Component->TickComponent(.4f, LEVELTICK_All, nullptr);
            TestTrue(TEXT("FX visibility changes over time"), FirstFlash != Component->Beacon->IsVisible());
        }
        TArray<UStaticMeshComponent*> Meshes;
        Twin->GetComponents<UStaticMeshComponent>(Meshes);
        bool bOverlayFound = false;
        for (auto* Mesh : Meshes) if (!Mesh->ComponentHasTag(TEXT("OntoTwinPresentation"))) bOverlayFound |= Mesh->GetOverlayMaterial() != nullptr;
        TestTrue(TEXT("Model material overlay applied"), bOverlayFound);
        TestTrue(TEXT("Business transform unchanged"), Twin->GetActorTransform().Equals(Original));
        Twin->ApplySnapshot(MakeSnapshot(false));
        if (Component->Motion) TestFalse(TEXT("Stop hides motion"), Component->Motion->IsVisible());
        if (Component->Beacon) TestFalse(TEXT("Stop clears FX"), Component->Beacon->IsVisible());
        for (auto* Mesh : Meshes) if (!Mesh->ComponentHasTag(TEXT("OntoTwinPresentation"))) TestNull(TEXT("Original material restored"), Mesh->GetOverlayMaterial());
    }
    World->DestroyWorld(false);
    return true;
}
#endif
