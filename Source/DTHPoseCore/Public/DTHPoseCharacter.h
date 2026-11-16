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
	/**
	 * A face bone the fitted face carries: a bone under the head. A state with a fitted face plays these at their
	 * reference pose, turned by its FaceBones (the eyes, jaw and tongue past DTH's face rig: DTH's post-process keeps a
	 * sequence's face bones and adds its poses on top, DTH.Pose.Face.BoneTracks), and the face curves make the rest.
	 */
	DTHPOSECORE_API bool IsCurveFaceBone(const FReferenceSkeleton& Ref, int32 Bone);

	DTHPOSECORE_API TMap<FName, FTransform> LocalPose(const DTHPoseSolve::FRig& Rig, const USkeletalMesh& Mesh, const FDTHDazState& State);

	/** The bone tracks, authored on the skeleton, that play back as LocalPose on this character. */
	DTHPOSECORE_API TMap<FName, FTransform> Track(const DTHPoseSolve::FRig& Rig, const USkeletalMesh& Mesh, const FDTHDazState& State);

	/**
	 * Creates and saves Folder/Name for the character's skeleton, one key per state at Rate. A name already taken gets a
	 * number.
	 */
	DTHPOSECORE_API UAnimSequence* Write(const USkeletalMesh& Mesh, const FDTHDazState& DazRest, TConstArrayView<FDTHDazState> Frames,
		const FString& Folder, const FString& Name, FString& OutError, FFrameRate Rate = FFrameRate(30, 1));

	/** A content-safe asset name from a label. */
	DTHPOSECORE_API FString AssetName(const FString& Label);
}
