#pragma once

#include "CoreMinimal.h"

class UAnimSequence;
class USkeleton;

namespace DTHPoseImport
{
	/**
	 * Imports an animation-only FBX onto a skeleton with Interchange, as the DTH importer imports its pose-asset FBX
	 * files [imp import_pose_asset_animation]. Replaces an earlier import at the same path.
	 */
	UAnimSequence* ImportAnimation(const FString& File, const FString& Folder, USkeleton* Skeleton, FString& OutError);

	/** The pose names of a DTH pose-asset FBX, one per frame, from the export's <Kind>PoseAssetNames.txt. */
	bool LoadPoseNames(const FString& File, TArray<FString>& OutNames, FString& OutError);
}
