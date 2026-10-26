#pragma once

#include "Modules/ModuleInterface.h"

/** Registers the Pose Wizard's dockable tab and its entry in the level editor's Tools menu. */
class FDTHPoseWizardModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static const FName WizardTabName;

	/** The plugin's Resources folder on disk (the Daz sampler script lives there). */
	static FString ResourcesDir();

private:
	void RegisterMenus();
	TSharedRef<class SDockTab> SpawnWizardTab(const class FSpawnTabArgs& Args);
};
