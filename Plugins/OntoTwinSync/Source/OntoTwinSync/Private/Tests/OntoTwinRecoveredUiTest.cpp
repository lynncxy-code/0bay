#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "OntoTwinOverlayWidget.h"
#include "SceneInteraction/OntoTwinRuntimeDockWidget.h"
#include "SceneInteraction/Minimap/OntoTwinMinimapWidget.h"
#include "SceneInteraction/TwinInteractionManagerComponent.h"
#include "TwinSceneManager.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "Widgets/SWidget.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOntoTwinRecoveredUiTest,
    "OntoTwin.UI.RecoveredDockOverlayAndSchemeSelection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOntoTwinRecoveredUiTest::RunTest(const FString& Parameters)
{
    // Transient world: no BeginPlay, backend requests, map saves or business writes.
    const auto Settings = UWorld::InitializationValues().AllowAudioPlayback(false)
        .CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None,
        nullptr, true, ERHIFeatureLevel::Num, &Settings);
    auto* Overlay = NewObject<UOntoTwinOverlayWidget>(World);
    const TSharedRef<SWidget> OverlaySlate = Overlay->TakeWidget();
    auto Data = MakeShared<FJsonObject>();
    auto Slots = MakeShared<FJsonObject>();
    auto Title = MakeShared<FJsonObject>();
    auto Body = MakeShared<FJsonObject>();
    auto Subtitle = MakeShared<FJsonObject>();
    Title->SetStringField(TEXT("display_value"), TEXT("仅有标题"));
    Subtitle->SetStringField(TEXT("display_value"), TEXT(" \t "));
    Slots->SetObjectField(TEXT("title"), Title);
    Slots->SetObjectField(TEXT("body"), Body);
    Slots->SetObjectField(TEXT("subtitle"), Subtitle);
    Data->SetObjectField(TEXT("resolved_slots"), Slots);
    for (const TCHAR* Template : {TEXT("title_body"), TEXT("title_subtitle_body")})
    {
        Data->SetStringField(TEXT("template_id"), Template);
        for (bool bWorld : {false, true})
        {
            Overlay->SetWorldSpacePresentation(bWorld);
            Body->SetStringField(TEXT("display_value"), TEXT("正文内容"));
            Overlay->ApplyOverlayData(Data);
            OverlaySlate->SlatePrepass(1.0f);
            const float FullHeight = Overlay->GetDesiredRenderSize().Y;
            for (const TCHAR* Empty : {TEXT(""), TEXT(" \n\t ")})
            {
                Body->SetStringField(TEXT("display_value"), Empty);
                Overlay->ApplyOverlayData(Data);
                OverlaySlate->SlatePrepass(1.0f);
                const float EmptyHeight = Overlay->GetDesiredRenderSize().Y;
                auto* Text = Cast<UTextBlock>(Overlay->WidgetTree->FindWidget(TEXT("OverlayBody")));
                auto* Slot = Text ? Cast<UVerticalBoxSlot>(Text->Slot) : nullptr;
                TestTrue(TEXT("Empty body collapsed"), Text && Text->GetVisibility() == ESlateVisibility::Collapsed);
                TestTrue(TEXT("Empty body has no reserved gap"), Slot && Slot->GetPadding() == FMargin(0.0f));
                TestTrue(TEXT("Title remains visible and card shrinks"), EmptyHeight > 0 && EmptyHeight < FullHeight);
            }
            Body->SetStringField(TEXT("display_value"), TEXT("正文内容"));
            Overlay->ApplyOverlayData(Data);
            TestTrue(TEXT("Content height restored"), FMath::IsNearlyEqual(Overlay->GetDesiredRenderSize().Y, FullHeight));
        }
    }
    auto* Dock = NewObject<UOntoTwinRuntimeDockWidget>(World);
    const TSharedRef<SWidget> DockSlate = Dock->TakeWidget();
    auto* Enter = Cast<UButton>(Dock->WidgetTree->FindWidget(TEXT("RuntimeDockEnterRoaming")));
    TestNotNull(TEXT("Dock entry is a real button"), Enter);
    TestTrue(TEXT("Dock entry has a click handler"), Enter && Enter->OnClicked.IsBound());

    auto* Scene = World->SpawnActor<ATwinSceneManager>();
    auto* Manager = NewObject<UTwinInteractionManagerComponent>(Scene);
    auto* Minimap = NewObject<UOntoTwinMinimapWidget>(World);
    const TSharedRef<SWidget> MinimapSlate = Minimap->TakeWidget();
    Minimap->SetInteractionManager(Manager);
    Scene->DisplaySchemeInstanceIds = {TEXT("one"), TEXT("two"), TEXT("three")};
    UButton* Buttons[] = {Minimap->DisplaySchemeButton, Minimap->DisplaySchemeButtonTwo, Minimap->DisplaySchemeButtonThree};
    for (int32 Active = 0; Active < 3; ++Active)
    {
        Scene->DisplaySchemeIndex = Active;
        Minimap->RefreshDisplaySchemeControl();
        TestFalse(TEXT("Current scheme cannot be selected again"), Buttons[Active]->GetIsEnabled());
        TestTrue(TEXT("Current scheme is named in tooltip"), Buttons[Active]->GetToolTipText().ToString().Contains(TEXT("当前")));
        TestTrue(TEXT("Selected disabled brush stays distinct"),
            Buttons[Active]->GetStyle().Disabled.TintColor.GetSpecifiedColor() !=
            Buttons[(Active + 1) % 3]->GetStyle().Disabled.TintColor.GetSpecifiedColor());
    }
    World->DestroyWorld(false);
    return true;
}
#endif
