#include "OntoTwinSyncEditor.h"

#include "Editor.h"
#include "EditorUtilitySubsystem.h"
#include "EditorUtilityWidgetBlueprint.h"
#include "Framework/Commands/UIAction.h"
#include "ToolMenu.h"
#include "ToolMenuEntry.h"
#include "ToolMenuSection.h"
#include "ToolMenus.h"
#include "UObject/CoreRedirects.h"

#define LOCTEXT_NAMESPACE "OntoTwinSyncEditor"

namespace
{
	const TCHAR* MouseWorldWidgetPath = TEXT("/OntoTwinSync/EUW_MouseWorldCoord.EUW_MouseWorldCoord");
	const TCHAR* MouseWorldRedirectSource = TEXT("OntoTwinSyncEditor.MouseWorldCoord");

	const TArray<FCoreRedirect> MouseWorldRedirects =
	{
		FCoreRedirect(
			ECoreRedirectFlags::Type_Class,
			TEXT("/Script/DigitalFactoryBaseEditor.MouseWorldLibrary"),
			TEXT("/Script/OntoTwinSyncEditor.OntoTwinMouseWorldLibrary"))
	};
}

void FOntoTwinSyncEditorModule::StartupModule()
{
	FCoreRedirects::AddRedirectList(MouseWorldRedirects, MouseWorldRedirectSource);

	if (!IsRunningCommandlet() && !IsRunningGame())
	{
		UToolMenus::RegisterStartupCallback(
			FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FOntoTwinSyncEditorModule::RegisterMenus));
	}
}

void FOntoTwinSyncEditorModule::ShutdownModule()
{
	if (UObjectInitialized())
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
	}

	FCoreRedirects::RemoveRedirectList(MouseWorldRedirects, MouseWorldRedirectSource);
}

void FOntoTwinSyncEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);
	UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
	FToolMenuSection& Section = ToolsMenu->FindOrAddSection(TEXT("OntoTwin"));

	Section.AddEntry(FToolMenuEntry::InitMenuEntry(
		TEXT("OntoTwinMouseWorldCoordinates"),
		LOCTEXT("MouseWorldCoordinatesLabel", "OntoTwin Mouse World Coordinates"),
		LOCTEXT("MouseWorldCoordinatesTooltip", "Open the OntoTwin editor viewport mouse world-coordinate panel."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FOntoTwinSyncEditorModule::OpenMouseWorldCoordinates))));
}

void FOntoTwinSyncEditorModule::OpenMouseWorldCoordinates()
{
	UEditorUtilityWidgetBlueprint* WidgetBlueprint = LoadObject<UEditorUtilityWidgetBlueprint>(nullptr, MouseWorldWidgetPath);
	if (!WidgetBlueprint)
	{
		UE_LOG(LogTemp, Error, TEXT("OntoTwinSyncEditor: unable to load %s"), MouseWorldWidgetPath);
		return;
	}

	if (UEditorUtilitySubsystem* UtilitySubsystem = GEditor->GetEditorSubsystem<UEditorUtilitySubsystem>())
	{
		UtilitySubsystem->SpawnAndRegisterTab(WidgetBlueprint);
	}
}

IMPLEMENT_MODULE(FOntoTwinSyncEditorModule, OntoTwinSyncEditor)

#undef LOCTEXT_NAMESPACE
