#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

class UAnimSequence;
class USkeletalMesh;
class USkeleton;

/** Inputs shared by the Phase 0 measurements, all from UDTHPoseSettings. */
struct FDTHPoseTestContext
{
	USkeletalMesh* Mesh = nullptr;
	USkeleton* Skeleton = nullptr;
	/** The export's "Pose Assets" folder. */
	FString PoseAssetsDir;
	FString TestFolder;

	/** Fails the test with a clear message when a setting is missing. */
	bool Load(FAutomationTestBase& Test);

	/**
	 * The export's <Kind>PoseAsset FBX (Kind = Jcm, Ret, ...) imported under TestFolder/Import, imported now if it is
	 * not there yet, and its pose names (one per frame).
	 */
	UAnimSequence* GroundTruth(FAutomationTestBase& Test, const FString& Kind, TArray<FString>& OutNames) const;
};

namespace DTHPoseTest
{
	double AngleDeg(const FQuat& A, const FQuat& B);

	/** Every bone track of a sequence at one key, as authored (no retargeting). */
	TMap<FName, FTransform> TrackPose(const UAnimSequence& Sequence, int32 Key);

	/** Writes lines to Saved/DTHPoseWizard/phase0/<Name> and returns the full path. */
	FString WriteReport(const FString& Name, const TArray<FString>& Lines);

	/** Deletes the asset at a package path if it exists, so a measurement can write it afresh. */
	void DeleteIfExists(const FString& PackageName);
}

#endif
