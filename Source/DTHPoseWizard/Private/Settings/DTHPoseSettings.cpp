#include "Settings/DTHPoseSettings.h"

#include "Misc/Paths.h"

UDTHPoseSettings::UDTHPoseSettings()
{
	SequenceFolder.Path = TEXT("/Game/DTH_Poses/Sequences");
	TestFolder.Path = TEXT("/Game/DTH_Poses/_Test");
}

FString UDTHPoseSettings::SavedDir()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("DTHPoseWizard"));
}
