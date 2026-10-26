#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimTypes.h"

class USkeleton;
class USkeletalMesh;

/**
 * What the engine does to a bone track authored on the skeleton when the character mesh plays it, and the inverse.
 * Per bone, by its translation retargeting mode (UE 5.8 FAnimationRuntime::RetargetBoneTransform,
 * AnimationRuntime.cpp ~2963-3010). DTH sets the modes: the pelvis subtree Animation Relative, the pelvis Animation
 * Scaled, the pec clavicles Skeleton [imp apply_skeleton_retargeting_modes], [QS 102-104].
 */
namespace DTHRetarget
{
	/** One bone's inputs: its mode and its reference transforms on the skeleton and on the mesh. */
	struct FBone
	{
		EBoneTranslationRetargetingMode::Type Mode = EBoneTranslationRetargetingMode::Animation;
		FTransform SkeletonRef;
		FTransform MeshRef;
	};

	/** The track value as the mesh evaluates it. */
	FTransform Forward(const FBone& Bone, const FTransform& Track);

	/** The track value to author so that the mesh evaluates Wanted. Skeleton mode cannot carry a translation. */
	FTransform Inverse(const FBone& Bone, const FTransform& Wanted);

	/** Per mesh bone index; bones missing from the skeleton keep mode Animation and the mesh reference for both. */
	TArray<FBone> Gather(const USkeleton& Skeleton, const USkeletalMesh& Mesh);
}
