#pragma once

#include "CoreMinimal.h"
#include "DTHPoseSolve.h"

class UAnimSequence;
class USkeletalMesh;

/**
 * Daz states on one DTH Genesis 9 character: Daz states in, bone tracks for the character's skeleton out. Shared by the
 * Pose Wizard (one state per pose) and the Animation Wizard (one state per frame). A sequence is written for the
 * character it plays on (Unreal's retargeting does not carry a sequence written for one character to another exactly:
 * DTH.Pose.P0.CrossCharacter).
 */
namespace DTHPoseCharacter
{
	/** The rig: shipped bone map and axes, the sampled figure's rest (Daz UnrealPose), the character's bind pose. */
	DTHPOSECORE_API DTHPoseSolve::FRig Make(const USkeletalMesh& Mesh, const FDTHDazState& DazRest);

	/** The mesh-space local pose that recreates State on the character, as its written sequence plays back (the preview). */
	DTHPOSECORE_API TMap<FName, FTransform> LocalPose(const DTHPoseSolve::FRig& Rig, const USkeletalMesh& Mesh, const FDTHDazState& State);

	/** The bone tracks, authored on the skeleton, that play back as LocalPose on this character. */
	DTHPOSECORE_API TMap<FName, FTransform> Track(const DTHPoseSolve::FRig& Rig, const USkeletalMesh& Mesh, const FDTHDazState& State);

	/**
	 * Creates and saves Folder/Name for the character's skeleton, one key per state. A name already taken gets a number.
	 */
	DTHPOSECORE_API UAnimSequence* Write(const USkeletalMesh& Mesh, const FDTHDazState& DazRest, TConstArrayView<FDTHDazState> Frames,
		const FString& Folder, const FString& Name, FString& OutError);

	/** A content-safe asset name from a label. */
	DTHPOSECORE_API FString AssetName(const FString& Label);
}
