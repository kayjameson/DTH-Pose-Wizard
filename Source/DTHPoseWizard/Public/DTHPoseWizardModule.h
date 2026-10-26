#pragma once

#include "Modules/ModuleInterface.h"

/** The Pose Wizard's editor module. Phase 0 has no UI: its measurements run as automation tests (DTH.Pose.*). */
class FDTHPoseWizardModule : public IModuleInterface
{
public:
	/** The plugin's Resources folder on disk (the Daz sampler script lives there). */
	static FString ResourcesDir();
};
