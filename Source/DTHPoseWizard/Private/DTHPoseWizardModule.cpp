#include "DTHPoseWizardModule.h"

#include "DTHPoseLog.h"

#include "Framework/Docking/TabManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "UI/SDTHPoseWizard.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "DTHPoseWizard"

DEFINE_LOG_CATEGORY(LogDTHPose);

const FName FDTHPoseWizardModule::WizardTabName(TEXT("DTHPoseWizard"));

void FDTHPoseWizardModule::StartupModule()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(WizardTabName,
		FOnSpawnTab::CreateRaw(this, &FDTHPoseWizardModule::SpawnWizardTab))
		.SetDisplayName(LOCTEXT("TabTitle", "DTH Pose Wizard"))
		.SetTooltipText(LOCTEXT("TabTooltip", "Daz Studio body poses on DazToHue Genesis 9 characters, written as animation sequences."))
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.AnimSequence"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FDTHPoseWizardModule::RegisterMenus));
}

void FDTHPoseWizardModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(WizardTabName);
	}
}

FString FDTHPoseWizardModule::ResourcesDir()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DTHPoseWizard"));
	return Plugin.IsValid() ? FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Resources")) : FString();
}

void FDTHPoseWizardModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
	// The same section as the other DTH wizards, so they share one DazToHue group.
	FToolMenuSection& Section = Menu->FindOrAddSection("DTH", LOCTEXT("MenuSection", "DazToHue"));
	Section.AddMenuEntry(
		"OpenDTHPoseWizard",
		LOCTEXT("MenuEntry", "DTH Pose Wizard"),
		LOCTEXT("MenuEntryTooltip", "Daz Studio body poses on DazToHue Genesis 9 characters, written as animation sequences."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.AnimSequence"),
		FUIAction(FExecuteAction::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(FDTHPoseWizardModule::WizardTabName);
		})));
}

TSharedRef<SDockTab> FDTHPoseWizardModule::SpawnWizardTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SDTHPoseWizard)
		];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDTHPoseWizardModule, DTHPoseWizard)
