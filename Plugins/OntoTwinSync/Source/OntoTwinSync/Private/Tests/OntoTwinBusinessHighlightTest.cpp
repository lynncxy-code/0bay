#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "WebInteraction/OntoTwinWebInteractionComponent.h"
#include "TwinSceneManager.h"
#include "TwinInstance.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Materials/Material.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOntoTwinBusinessHighlightTest,
    "OntoTwin.WebInteraction.BusinessHighlight",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOntoTwinBusinessHighlightTest::RunTest(const FString& Parameters)
{
    // Isolated transient world, no BeginPlay, HTTP, business data or map writes.
    const auto Settings = UWorld::InitializationValues().AllowAudioPlayback(false)
        .CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None,
        nullptr, true, ERHIFeatureLevel::Num, &Settings);
    ATwinSceneManager* Manager = World->SpawnActor<ATwinSceneManager>();
    auto* Web = NewObject<UOntoTwinWebInteractionComponent>(Manager);
    Web->SceneManager = Manager;
    TestNotNull(TEXT("Cooked runtime highlight material"), Web->BusinessHighlightMaterial.Get());
    auto* RegistryProperty = FindFProperty<FMapProperty>(ATwinSceneManager::StaticClass(), TEXT("InstanceRegistry"));
    auto* Registry = RegistryProperty->ContainerPtrToValuePtr<TMap<FString, ATwinInstance*>>(Manager);
    TArray<USkeletalMeshComponent*> Meshes;
    TSet<FString> Members;
    auto* Original = UMaterial::GetDefaultMaterial(MD_Surface);
    for (int32 Number = 17; Number <= 31; ++Number)
    {
        auto* Instance = World->SpawnActor<ATwinInstance>();
        Instance->InstanceId = FString::FromInt(Number);
        Registry->Add(Instance->InstanceId, Instance);
        Web->InstancesById.Add(Instance->InstanceId, MakeShared<FJsonObject>());
        auto* Mesh = NewObject<USkeletalMeshComponent>(Instance);
        Mesh->RegisterComponentWithWorld(World);
        Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
        Mesh->SetOverlayMaterial(Original);
        Mesh->SetOverlayMaterialMaxDrawDistance(1234.0f);
        Meshes.Add(Mesh);
        if (Number <= 30) Members.Add(Instance->InstanceId);
    }
    Web->BusinessViewMembers.Add(TEXT("test"), Members);
    Web->PublishedConfig = MakeShared<FJsonObject>();
    auto View = MakeShared<FJsonObject>();
    View->SetStringField(TEXT("business_view_id"), TEXT("test"));
    View->SetStringField(TEXT("scene_behavior"), TEXT("highlight"));
    Web->PublishedConfig->SetArrayField(TEXT("business_views"), {MakeShared<FJsonValueObject>(View)});
    auto Context = MakeShared<FJsonObject>();
    Context->SetStringField(TEXT("business_view_id"), TEXT("test"));
    FOntoTwinWebNavigationFrame Frame;
    Web->ApplySceneScope(MakeShared<FJsonObject>(), Context, Frame);
    TestEqual(TEXT("Exactly 14 business members, not the entire visible scene"), Frame.HighlightInstanceIds.Num(), 14);
    TestTrue(TEXT("Highlight must keep camera, not infer from member count"), Frame.bKeepCamera);
    Web->ApplyVisibilityFrame(Frame);
    TestEqual(TEXT("Keep all surroundings visible"), Frame.VisibleInstanceIds.Num(), 15);
    for (int32 Index = 0; Index < 14; ++Index)
    {
        TestTrue(TEXT("Member receives visible overlay"), Meshes[Index]->GetOverlayMaterial() == Web->BusinessHighlightMaterial);
        TestTrue(TEXT("Animation mode unchanged"), Meshes[Index]->GetAnimationMode() == EAnimationMode::AnimationSingleNode);
    }
    TestTrue(TEXT("Nonmember unchanged"), Meshes[14]->GetOverlayMaterial() == Original);
    TSet<FString> NextMembers;
    NextMembers.Add(TEXT("31"));
    Web->ApplyBusinessHighlight(NextMembers);
    TestTrue(TEXT("Switch business restores old members"), Meshes[0]->GetOverlayMaterial() == Original);
    TestTrue(TEXT("Switch business highlights new members"), Meshes[14]->GetOverlayMaterial() == Web->BusinessHighlightMaterial);
    Web->ApplyBusinessHighlight(Members);
    Web->ApplyBusinessHighlight(Members); // repeated poll must not replace the saved originals
    Web->RestoreBusinessHighlight();
    for (auto* Mesh : Meshes)
    {
        TestTrue(TEXT("Original overlay restored"), Mesh->GetOverlayMaterial() == Original);
        TestEqual(TEXT("Original overlay distance restored"), Mesh->GetOverlayMaterialMaxDrawDistance(), 1234.0f);
    }
    Web->ApplyBusinessHighlight(Members);
    Web->ApplyVisibilityFrame(FOntoTwinWebNavigationFrame());
    TestTrue(TEXT("Web-only mode clears highlight"), Web->OriginalOverlayMaterials.IsEmpty());
    Web->ApplyBusinessHighlight(Members);
    Web->SetRuntimeEditorSuppressed(true);
    TestTrue(TEXT("Editor mode clears highlight"), Web->OriginalOverlayMaterials.IsEmpty());
    Web->ApplyBusinessHighlight(Members);
    TestTrue(TEXT("Suppression prevents reapplying"), Web->OriginalOverlayMaterials.IsEmpty());
    Web->SetRuntimeEditorSuppressed(false);
    // Exercise the same entry point invoked by HandleWebInstanceSelection.
    // Stub browser surface, disable telemetry: no network or business writes.
    Web->bShuttingDown = true;
    Web->HostWidget = NewObject<UOntoTwinWebHostWidget>();
    Web->CurrentFrame = Frame;
    Web->bHasCurrentFrame = true;
    TArray<TSharedPtr<FJsonValue>> Bindings;
    for (int32 Number = 17; Number <= 30; ++Number)
    {
        const FString Id = FString::FromInt(Number);
        auto Page = MakeShared<FJsonObject>();
        Page->SetStringField(TEXT("page_id"), Id);
        Page->SetStringField(TEXT("base_url"), TEXT("http://127.0.0.1:5000/monitor-test.html?camera_id=") + Id);
        Web->PagesById.Add(Id, Page);
        auto Binding = MakeShared<FJsonObject>();
        Binding->SetStringField(TEXT("trigger"), TEXT("open_detail"));
        Binding->SetStringField(TEXT("page_id"), Id);
        auto Scope = MakeShared<FJsonObject>();
        Scope->SetStringField(TEXT("instance_id"), Id);
        Binding->SetObjectField(TEXT("scope"), Scope);
        Bindings.Add(MakeShared<FJsonValueObject>(Binding));
    }
    Web->PublishedConfig->SetArrayField(TEXT("bindings"), Bindings);
    for (int32 Number = 17; Number <= 30; ++Number)
    {
        const FString Id = FString::FromInt(Number);
        TestTrue(TEXT("Actor detail route accepted"), Web->OpenInstanceDetail(Id));
        TestEqual(TEXT("Correct per-camera page"), Web->CurrentFrame.PageId, Id);
        TestTrue(TEXT("Per-camera URL"), Web->CurrentFrame.FinalUrl.EndsWith(TEXT("camera_id=") + Id));
        TestTrue(TEXT("Detail selection retains no-camera policy"), Web->CurrentFrame.bKeepCamera);
        TestEqual(TEXT("Detail selection retains all 14 highlights"), Web->CurrentFrame.HighlightInstanceIds.Num(), 14);
    }
    Web->Back();
    TestTrue(TEXT("Back retains no-camera policy"), Web->CurrentFrame.bKeepCamera);
    Web->ApplyBusinessHighlight(Members);
    Meshes[0]->DestroyComponent();
    Web->ResetHome();
    TestTrue(TEXT("Home cleans destroyed meshes and saved state"), Web->OriginalOverlayMaterials.IsEmpty());
    World->DestroyWorld(false);
    return true;
}
#endif
