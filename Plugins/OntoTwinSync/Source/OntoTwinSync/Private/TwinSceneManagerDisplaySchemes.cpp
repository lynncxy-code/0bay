#include "TwinSceneManager.h"
#include "TwinInstance.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/ConfigCacheIni.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "SceneInteraction/TwinInteractionManagerComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "SceneInteraction/Minimap/OntoTwinMinimapWidget.h"
#include "SceneInteraction/OntoTwinRuntimeDockWidget.h"
#include "OntoTwinOverlayWidget.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBoxSlot.h"
#include "Kismet/GameplayStatics.h"

namespace
{
TSharedPtr<FJsonObject> CopySchemeSnapshot(const TSharedPtr<FJsonObject>& Source)
{
    FString Text;
    FJsonSerializer::Serialize(Source.ToSharedRef(), TJsonWriterFactory<>::Create(&Text));
    TSharedPtr<FJsonObject> Copy;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Copy);
    return Copy;
}

void MergeSchemeDelta(const TSharedPtr<FJsonObject>& Target, const TSharedPtr<FJsonObject>& Delta)
{
    for (const auto& Pair : Delta->Values)
    {
        const TSharedPtr<FJsonObject>* Incoming = nullptr;
        const TSharedPtr<FJsonObject>* Existing = nullptr;
        if (Pair.Value->TryGetObject(Incoming) && Target->TryGetObjectField(Pair.Key, Existing))
            MergeSchemeDelta(*Existing, *Incoming);
        else Target->SetField(Pair.Key, Pair.Value);
    }
}

TSharedPtr<FJsonObject> VisibleSchemeSnapshot(const TSharedPtr<FJsonObject>& Source)
{
    auto Copy = CopySchemeSnapshot(Source);
    const TSharedPtr<FJsonObject>* Interfaces = nullptr;
    const TSharedPtr<FJsonObject>* Rep = nullptr;
    if (Copy->TryGetObjectField(TEXT("interfaces"), Interfaces)
        && (*Interfaces)->TryGetObjectField(TEXT("I3D_Representable"), Rep))
        (*Rep)->SetBoolField(TEXT("is_visible"), true);
    const TSharedPtr<FJsonObject>* Raw = nullptr;
    if (Copy->TryGetObjectField(TEXT("raw_state"), Raw)) (*Raw)->SetBoolField(TEXT("is_loaded"), true);
    return Copy;
}
}

void ATwinSceneManager::InitializeDisplaySchemes()
{
    const TCHAR* Section = TEXT("OntoTwin.DisplaySchemes");
    FString Project;
    GConfig->GetString(Section, TEXT("UEProjectId"), Project, GGameIni);
    if (Project.IsEmpty() || Project != UEProjectId) return;
    GConfig->GetArray(Section, TEXT("InstanceIds"), DisplaySchemeInstanceIds, GGameIni);
    TSet<FString> Unique;
    for (const auto& Id : DisplaySchemeInstanceIds) if (!Id.IsEmpty()) Unique.Add(Id);
    if (Unique.Num() != 3 || DisplaySchemeInstanceIds.Num() != 3)
    {
        DisplaySchemeInstanceIds.Reset();
        UE_LOG(LogTemp, Error, TEXT("[DisplaySchemes] Expected three distinct instance IDs"));
        return;
    }
    DisplaySchemeIndex = 0;
    GConfig->GetArray(Section, TEXT("Scheme2HiddenInstanceIds"), DisplaySchemeTwoHiddenInstanceIds, GGameIni);
    // These project-owned source actors were tagged by an exact-GUID migration.
    // Only the PIE/game copies are removed; their editor/map originals stay recoverable.
    TArray<AActor*> Sources;
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
        if (It->ActorHasTag(TEXT("OntoTwin.DisplaySchemeSource"))) Sources.Add(*It);
    for (AActor* Source : Sources) Source->Destroy();
    UE_LOG(LogTemp, Log, TEXT("[DisplaySchemes] Ready; default=1; runtime source copies removed=%d"), Sources.Num());
}

void ATwinSceneManager::ApplyDisplaySchemeTwoHiddenVisibility()
{
    const bool bHide = DisplaySchemeIndex == 1;
    for (const FString& Id : DisplaySchemeTwoHiddenInstanceIds)
    {
        if (ATwinInstance** Found = InstanceRegistry.Find(Id))
        {
            if (*Found && IsValid(*Found))
            {
                (*Found)->SetActorHiddenInGame(bHide);
                (*Found)->SetActorEnableCollision(!bHide);
            }
        }
    }
}

bool ATwinSceneManager::PrepareDisplaySchemeSnapshot(const TSharedPtr<FJsonObject>& Snapshot,
    bool bIsDelta, TSharedPtr<FJsonObject>& OutSnapshot)
{
    const FString Id = Snapshot->GetStringField(TEXT("instanceId"));
    const int32 Index = DisplaySchemeInstanceIds.IndexOfByKey(Id);
    OutSnapshot = Snapshot;
    if (Index == INDEX_NONE) return true;
    auto Copy = CopySchemeSnapshot(Snapshot);
    auto* Cached = DisplaySchemeSnapshots.Find(Id);
    if (bIsDelta && Cached && Cached->IsValid()) MergeSchemeDelta(*Cached, Copy);
    else DisplaySchemeSnapshots.Add(Id, Copy);
    if (Index != DisplaySchemeIndex)
    {
        DestroyTwinInstance(Id);
        // ProcessSnapshot treats false here as successfully cached, with no
        // actor to spawn. Returning true would immediately recreate this actor.
        return false;
    }
    OutSnapshot = VisibleSchemeSnapshot(DisplaySchemeSnapshots[Id]);
    return true;
}

void ATwinSceneManager::PruneDisplaySchemeSnapshots(const TSet<FString>& BackendIds)
{
    for (const FString& Id : DisplaySchemeInstanceIds)
        if (!BackendIds.Contains(Id)) DisplaySchemeSnapshots.Remove(Id);
}

bool ATwinSceneManager::CanSwitchDisplayScheme() const
{
    return HasDisplaySchemes() && bInitialModelBaselineReady
        && !PendingDisplayScheme && !bRuntimeEditSaving && !bRuntimeEditDirty && !bRuntimeBusinessDirty;
}

bool ATwinSceneManager::SelectDisplayScheme(int32 SchemeIndex)
{
    if (!DisplaySchemeInstanceIds.IsValidIndex(SchemeIndex)) return false;
    if (SchemeIndex == DisplaySchemeIndex) return true;
    if (!CanSwitchDisplayScheme())
    {
        DisplaySchemeStatus = TEXT("请等待加载完成，并保存或撤销 F10 修改后切换");
        return false;
    }
    const FString& NextId = DisplaySchemeInstanceIds[SchemeIndex];
    auto* Cached = DisplaySchemeSnapshots.Find(NextId);
    if (!Cached || !Cached->IsValid())
    {
        DisplaySchemeStatus = TEXT("目标方案数据尚未就绪，请稍后重试");
        return false;
    }
    auto Snapshot = VisibleSchemeSnapshot(*Cached);
    ATwinInstance* Next = SpawnTwinInstance(NextId, Snapshot);
    if (!Next) return false;
    PendingDisplayScheme = Next;
    PendingDisplaySchemeIndex = SchemeIndex;
    PendingDisplaySchemeSnapshot = Snapshot;
    DisplaySchemeLoadStarted = FPlatformTime::Seconds();
    Next->SetActorHiddenInGame(true);
    Next->SetActorEnableCollision(false);
    DisplaySchemeStatus = TEXT("正在加载目标方案…");
    TickDisplaySchemeTransition();
    return PendingDisplayScheme != nullptr || DisplaySchemeIndex == SchemeIndex;
}

void ATwinSceneManager::TickDisplaySchemeTransition()
{
    if (!PendingDisplayScheme) return;
    ATwinInstance* Next = PendingDisplayScheme;
    const int32 SchemeIndex = PendingDisplaySchemeIndex;
    const FString NextId = DisplaySchemeInstanceIds[SchemeIndex];
    const auto Snapshot = PendingDisplaySchemeSnapshot;
    const TSharedPtr<FJsonObject>* Interfaces = nullptr;
    const TSharedPtr<FJsonObject>* Rep = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
    TArray<TSharedPtr<FJsonValue>> Failures;
    const bool bHasRep = Snapshot->TryGetObjectField(TEXT("interfaces"), Interfaces)
        && (*Interfaces)->TryGetObjectField(TEXT("I3D_Representable"), Rep);
    const bool bAssembly = bHasRep && (*Rep)->TryGetArrayField(TEXT("render_parts"), Parts) && Parts->Num() > 0;
    bool bValid = bAssembly && Next->GetCurrentAssemblySignature().Len() > 0
        && Next->GetRenderPartComponentCount() == Parts->Num();
    if (!bAssembly)
    {
        TInlineComponentArray<UStaticMeshComponent*> Meshes(Next);
        for (UStaticMeshComponent* Mesh : Meshes)
            if (Mesh->GetStaticMesh() && Mesh->GetStaticMesh()->GetPathName() != TEXT("/Engine/BasicShapes/Cube.Cube"))
                bValid = true;
    }
    if (!bValid && !bAssembly && FPlatformTime::Seconds() - DisplaySchemeLoadStarted < 180.0
        && DisplaySchemeSnapshots.Contains(NextId)) return;
    if (!DisplaySchemeSnapshots.Contains(NextId)) bValid = false;
    // The user may enter F10 while an asynchronous download is in flight.
    if (bRuntimeEditDirty || bRuntimeEditSaving || bRuntimeBusinessDirty) bValid = false;
    PendingDisplayScheme = nullptr;
    PendingDisplaySchemeIndex = INDEX_NONE;
    PendingDisplaySchemeSnapshot.Reset();
    if (!bValid)
    {
        if (Next) Next->Destroy();
        DisplaySchemeStatus = TEXT("目标方案模型加载失败，已保留当前方案");
        UE_LOG(LogTemp, Error, TEXT("[DisplaySchemes] Load failed: %s failures=%d"), *NextId, Failures.Num());
        return;
    }
    ClearRuntimeSelection(false);
    RuntimeUndoStack.Reset();
    RuntimeRedoStack.Reset();
    DestroyTwinInstance(DisplaySchemeInstanceIds[DisplaySchemeIndex]);
    InstanceRegistry.Add(NextId, Next);
    Next->SetActorHiddenInGame(false);
    Next->SetActorEnableCollision(true);
    DisplaySchemeIndex = SchemeIndex;
    ApplyDisplaySchemeTwoHiddenVisibility();
    DisplaySchemeStatus.Empty();
    if (InteractionManager) InteractionManager->RefreshMinimapForDisplayScheme();
    UE_LOG(LogTemp, Log, TEXT("[DisplaySchemes] Selected=%d id=%s parts=%d registry=%d"),
        SchemeIndex + 1, *NextId, Next->GetRenderPartComponentCount(), InstanceRegistry.Num());
}

// Opt-in runtime regression; exercises the real registry/editor without database writes.
void ATwinSceneManager::RunDisplaySchemeSelfTest()
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("OntoTwinDisplaySchemesSelfTest"))) return;
    static bool Done = false;
    static int32 Step = 0;
    static int32 BaselineCount = 0;
    static double Started = FPlatformTime::Seconds();
    static double Due = 0.0;
    static TArray<TSharedPtr<FJsonValue>> Checks;
    static TArray<TSharedPtr<FJsonValue>> AssemblyAudits;
    static bool Passed = true;
    static bool Captured = false;
    if (Done) return;
    const double Now = FPlatformTime::Seconds();
    auto Check = [&](const TCHAR* Name, bool OK)
    {
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("check"), Name);
        Row->SetBoolField(TEXT("passed"), OK);
        Checks.Add(MakeShared<FJsonValueObject>(Row));
        Passed &= OK;
        UE_LOG(LogTemp, Display, TEXT("[DisplaySchemesTest] %s = %s"), Name, OK ? TEXT("PASS") : TEXT("FAIL"));
    };
    auto Finish = [&]()
    {
        Done = true;
        auto Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("passed"), Passed);
        Result->SetNumberField(TEXT("registry_count"), InstanceRegistry.Num());
        Result->SetNumberField(TEXT("scheme_index"), DisplaySchemeIndex);
        Result->SetArrayField(TEXT("checks"), Checks);
        Result->SetArrayField(TEXT("assembly_audits"), AssemblyAudits);
        Result->SetStringField(TEXT("backend_url"), BackendBaseUrl);
        Result->SetStringField(TEXT("ue_project_id"), UEProjectId);
        FString Text;
        FJsonSerializer::Serialize(Result, TJsonWriterFactory<>::Create(&Text));
        const FString Dir = FPaths::ProjectSavedDir() / TEXT("OntoTwinMigration");
        IFileManager::Get().MakeDirectory(*Dir, true);
        FString ResultPath = Dir / TEXT("display_scheme_selftest.json");
        FParse::Value(FCommandLine::Get(), TEXT("OntoTwinSelfTestOutput="), ResultPath);
        const bool bWritten = FFileHelper::SaveStringToFile(Text, *ResultPath);
        FPlatformMisc::RequestExitWithStatus(false, Passed && bWritten ? 0 : 1);
    };
    if (Now - Started > 600.0)
    {
        Check(TEXT("completed_before_timeout"), false);
        Finish();
        return;
    }
    if (!bInitialModelBaselineReady || DisplaySchemeSnapshots.Num() != 3 || Now < Due || PendingDisplayScheme) return;
    const int32 Expected[] = {0, 1, 2, 0, 1, 2, 0};
    if (Step < 7)
    {
        if (FParse::Param(FCommandLine::Get(), TEXT("OntoTwinSchemeScreenshots")) && !Captured)
        {
            const FString File = FPaths::ProjectSavedDir() / TEXT("Screenshots") /
                FString::Printf(TEXT("display_scheme_step_%d.png"), Step);
            FScreenshotRequest::RequestScreenshot(File, true, false);
            Captured = true;
            Due = Now + 0.5;
            return;
        }
        const int32 Index = Expected[Step];
        Check(TEXT("selected_scheme"), DisplaySchemeIndex == Index);
        int32 Live = 0;
        for (const FString& Id : DisplaySchemeInstanceIds) if (InstanceRegistry.Contains(Id)) ++Live;
        Check(TEXT("exactly_one_scheme_in_registry"), Live == 1);
        ATwinInstance* Instance = InstanceRegistry.FindRef(DisplaySchemeInstanceIds[Index]);
        Check(TEXT("selected_actor_exists"), IsValid(Instance));
        if (!IsValid(Instance)) { Finish(); return; }
        const TCHAR* ExpectedNames[] = {TEXT("歼-35AE"), TEXT("歼-20S和歼-35AE"), TEXT("歼-10CE")};
        Check(TEXT("current_instance_name"), Instance->TwinDisplayName == ExpectedNames[Index]);
        Check(TEXT("selected_actor_visible"), !Instance->IsHidden() && Instance->GetActorEnableCollision());
        const auto ExpectedSnapshot = VisibleSchemeSnapshot(DisplaySchemeSnapshots[DisplaySchemeInstanceIds[Index]]);
        const auto& ExpectedParts = ExpectedSnapshot->GetObjectField(TEXT("interfaces"))
            ->GetObjectField(TEXT("I3D_Representable"))->GetArrayField(TEXT("render_parts"));
        TArray<TSharedPtr<FJsonValue>> AssemblyFailures;
        const bool bAssemblyMatches = Instance->ValidateRenderPartsAgainstSnapshot(ExpectedParts, true, AssemblyFailures);
        Check(TEXT("meshes_materials_transforms_visibility_match_snapshot"), bAssemblyMatches);
        auto Audit = MakeShared<FJsonObject>();
        Audit->SetNumberField(TEXT("step"), Step);
        Audit->SetStringField(TEXT("instance_id"), Instance->InstanceId);
        const FString ExpectedSignature = ExpectedSnapshot->GetObjectField(TEXT("interfaces"))
            ->GetObjectField(TEXT("I3D_Representable"))->GetStringField(TEXT("assembly_signature"));
        Audit->SetNumberField(TEXT("expected_parts"), ExpectedParts.Num());
        Audit->SetNumberField(TEXT("actual_parts"), Instance->GetRenderPartComponentCount());
        Audit->SetStringField(TEXT("expected_signature"), ExpectedSignature);
        Audit->SetStringField(TEXT("actual_signature"), Instance->GetCurrentAssemblySignature());
        // Dataset revisions may reorder or replace schemes. Validate the release
        // snapshot, not a historical model name/count hard-coded into the test.
        Check(TEXT("scheme_parts_match_release_snapshot"),
            ExpectedParts.Num() > 0 && Instance->GetRenderPartComponentCount() == ExpectedParts.Num());
        Check(TEXT("scheme_signature_matches_release_snapshot"),
            !ExpectedSignature.IsEmpty() && Instance->GetCurrentAssemblySignature() == ExpectedSignature);
        Audit->SetArrayField(TEXT("failures"), AssemblyFailures);
        AssemblyAudits.Add(MakeShared<FJsonValueObject>(Audit));
        // The hidden-instance list is release-data-specific. The 0924 dataset
        // contains eight valid entries; stale IDs from older exports must not
        // make the runtime self-test fail or be reintroduced into the payload.
        Check(TEXT("scheme2_hidden_instance_config_loaded"), DisplaySchemeTwoHiddenInstanceIds.Num() == 8);
        bool bIndependentVisibilityCorrect = true;
        for (const FString& HiddenId : DisplaySchemeTwoHiddenInstanceIds)
        {
            ATwinInstance* HiddenInstance = InstanceRegistry.FindRef(HiddenId);
            bIndependentVisibilityCorrect &= IsValid(HiddenInstance)
                && HiddenInstance->IsHidden() == (Index == 1)
                && HiddenInstance->GetActorEnableCollision() == (Index != 1);
        }
        Check(TEXT("independent_weapons_and_bases_visibility"), bIndependentVisibilityCorrect);
        if (Step == 0)
        {
            BaselineCount = InstanceRegistry.Num();
            int32 Sources = 0;
            for (TActorIterator<AActor> It(GetWorld()); It; ++It)
                if (It->ActorHasTag(TEXT("OntoTwin.DisplaySchemeSource"))) ++Sources;
            Check(TEXT("no_runtime_source_duplicates"), Sources == 0);
            if (FParse::Param(FCommandLine::Get(), TEXT("OntoTwinDockOverlaySelfTest")))
            {
                UOntoTwinRuntimeDockWidget* Dock = nullptr;
                for (TObjectIterator<UOntoTwinRuntimeDockWidget> It; It; ++It)
                    if (It->GetWorld() == GetWorld() && It->WidgetTree) { Dock = *It; break; }
                Check(TEXT("dock_exists"), Dock != nullptr);
                if (Dock && InteractionManager)
                {
                    InteractionManager->ExitRoaming();
                    Dock->SetDockOpen(true);
                    Dock->RefreshFromManager();
                    auto* EnterButton = Cast<UButton>(Dock->WidgetTree->FindWidget(TEXT("RuntimeDockEnterRoaming")));
                    Check(TEXT("enter_roaming_button_enabled"), EnterButton && EnterButton->GetIsEnabled());
                    if (EnterButton) EnterButton->OnClicked.Broadcast();
                    Check(TEXT("dock_button_enters_roaming"), InteractionManager->IsRoamingActive());
                    if (EnterButton) EnterButton->OnClicked.Broadcast();
                    Check(TEXT("repeated_entry_does_not_exit"), InteractionManager->IsRoamingActive());
                }
                auto* Overlay = CreateWidget<UOntoTwinOverlayWidget>(UGameplayStatics::GetPlayerController(this, 0));
                const TSharedRef<SWidget> OverlaySlate = Overlay->TakeWidget();
                auto Data = MakeShared<FJsonObject>();
                auto Slots = MakeShared<FJsonObject>();
                auto Title = MakeShared<FJsonObject>();
                auto Body = MakeShared<FJsonObject>();
                Title->SetStringField(TEXT("display_value"), TEXT("仅有标题"));
                Slots->SetObjectField(TEXT("title"), Title);
                Slots->SetObjectField(TEXT("body"), Body);
                Data->SetStringField(TEXT("template_id"), TEXT("title_body"));
                Data->SetObjectField(TEXT("resolved_slots"), Slots);
                for (bool bWorld : {false, true})
                {
                    Overlay->SetWorldSpacePresentation(bWorld);
                    Body->SetStringField(TEXT("display_value"), TEXT("正文内容"));
                    Overlay->ApplyOverlayData(Data);
                    OverlaySlate->SlatePrepass(1.0f);
                    const float FullHeight = Overlay->GetDesiredRenderSize().Y;
                    Body->SetStringField(TEXT("display_value"), TEXT(" \n\t "));
                    Overlay->ApplyOverlayData(Data);
                    OverlaySlate->SlatePrepass(1.0f);
                    const float EmptyHeight = Overlay->GetDesiredRenderSize().Y;
                    auto* BodyText = Cast<UTextBlock>(Overlay->WidgetTree->FindWidget(TEXT("OverlayBody")));
                    auto* BodySlot = BodyText ? Cast<UVerticalBoxSlot>(BodyText->Slot) : nullptr;
                    Check(TEXT("blank_body_collapsed"), BodyText && BodyText->GetVisibility() == ESlateVisibility::Collapsed);
                    Check(TEXT("blank_body_zero_padding"), BodySlot && BodySlot->GetPadding().Top == 0.0f);
                    Check(TEXT("title_only_card_shrinks"), EmptyHeight < FullHeight);
                    UE_LOG(LogTemp, Display, TEXT("[DockOverlayTest] world=%d fullHeight=%.0f emptyHeight=%.0f"), bWorld, FullHeight, EmptyHeight);
                    Body->SetStringField(TEXT("display_value"), TEXT("正文内容"));
                    Overlay->ApplyOverlayData(Data);
                    Check(TEXT("body_restored_after_edit"), FMath::IsNearlyEqual(Overlay->GetDesiredRenderSize().Y, FullHeight));
                }
            }
        }
        else Check(TEXT("other_instances_preserved"), InstanceRegistry.Num() == BaselineCount);
        if (Index == 1)
        {
            int32 HiddenParts = 0;
            for (const auto& Part : ExpectedParts)
            {
                bool bPartVisible = true, bPartHidden = false;
                Part->AsObject()->TryGetBoolField(TEXT("visible"), bPartVisible);
                Part->AsObject()->TryGetBoolField(TEXT("hidden_in_game"), bPartHidden);
                if (!bPartVisible || bPartHidden) ++HiddenParts;
            }
            Check(TEXT("scheme2_32_internal_weapon_base_parts_hidden"), HiddenParts == 32);
        }
        if (Step == 1 || Step == 2)
        {
            ToggleRuntimeEditMode();
            Check(TEXT("F10_editor_available"), bRuntimeEditMode);
            SelectRuntimeInstance(Instance);
            const FTransform Before = Instance->GetActorTransform();
            Instance->SetActorLocation(Before.GetLocation() + FVector(10.0, 0.0, 0.0));
            MarkRuntimeDirtyFromTransform();
            Check(TEXT("F10_transform_is_dirty"), bRuntimeEditDirty);
            Check(TEXT("unsaved_edit_blocks_switch"), !SelectDisplayScheme((Index + 1) % 3) && DisplaySchemeIndex == Index);
            ClearRuntimeSelection(true);
            Check(TEXT("F10_revert_restores_transform"), Instance->GetActorTransform().Equals(Before, 0.01));
            ToggleRuntimeEditMode();
            Check(TEXT("F10_editor_closed"), !bRuntimeEditMode);
            if (InteractionManager && !InteractionManager->IsRoamingActive()) InteractionManager->ToggleRoaming();
        }
        if (Step < 6)
        {
            UButton* SchemeButton = nullptr;
            const int32 NextIndex = Expected[Step + 1];
            const TCHAR* ButtonName = NextIndex == 0 ? TEXT("DisplaySchemeButton")
                : (NextIndex == 1 ? TEXT("DisplaySchemeButtonTwo") : TEXT("DisplaySchemeButtonThree"));
            for (TObjectIterator<UOntoTwinMinimapWidget> It; It; ++It)
            {
                if (It->GetWorld() != GetWorld() || !It->WidgetTree) continue;
                SchemeButton = Cast<UButton>(It->WidgetTree->FindWidget(ButtonName));
                if (SchemeButton) break;
            }
            const bool bButtonReady = SchemeButton && SchemeButton->GetIsEnabled();
            Check(TEXT("minimap_scheme_button_enabled"), bButtonReady);
            if (bButtonReady) SchemeButton->OnClicked.Broadcast();
            Check(TEXT("button_switch_request_accepted"), PendingDisplayScheme != nullptr || DisplaySchemeIndex == Expected[Step+1]);
            ++Step;
            Captured = false;
            Due = Now + 2.0;
            return;
        }
        // Missing definitions must not empty the currently displayed scheme.
        auto Saved = DisplaySchemeSnapshots[DisplaySchemeInstanceIds[1]];
        DisplaySchemeSnapshots.Remove(DisplaySchemeInstanceIds[1]);
        Check(TEXT("missing_definition_keeps_current"), !SelectDisplayScheme(1) && DisplaySchemeIndex == 0);
        DisplaySchemeSnapshots.Add(DisplaySchemeInstanceIds[1], Saved);
        DisplaySchemeStatus.Empty();
        Check(TEXT("returned_to_default"), DisplaySchemeIndex == 0);
        Finish();
    }
}
