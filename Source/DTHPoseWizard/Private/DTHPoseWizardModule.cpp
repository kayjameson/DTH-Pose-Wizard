#include "DTHPoseWizardModule.h"

#include "DTHPoseLog.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY(LogDTHPose);

FString FDTHPoseWizardModule::ResourcesDir()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DTHPoseWizard"));
	return Plugin.IsValid() ? FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Resources")) : FString();
}

IMPLEMENT_MODULE(FDTHPoseWizardModule, DTHPoseWizard)
