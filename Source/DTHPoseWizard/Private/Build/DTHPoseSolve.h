#pragma once

#include "CoreMinimal.h"
#include "Library/DTHPoseSamples.h"

struct FReferenceSkeleton;

/**
 * Recreates a pose Daz evaluated on the character's UE bones, in closed form (plan section 4): each mapped UE bone gets
 * the Daz bone's world-space rotation change since UnrealPose, applied to the UE bind pose (which is UnrealPose:
 * DTH.Pose.P0.BindPoseIsUnrealPose). The axis conversion and the bone mapping are measured, not assumed.
 */
namespace DTHPoseSolve
{
	/** A signed axis permutation from Daz world space to UE component space, plus an offset (cm). */
	struct FAxes
	{
		/** UE = M * Daz, column vectors. */
		double M[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
		FVector Offset = FVector::ZeroVector;
		/** Uniform scale: the sampled figure is base Genesis 9, the character has its own proportions. */
		double Scale = 1.0;
		/** UE joints with a Daz joint within InlierCm after the conversion, and their median distance (cm). */
		int32 Inliers = 0;
		double MedianCm = 0.0;

		FVector Position(const FVector& Daz) const;
		FQuat Rotation(const FQuat& Daz) const;
		FString Describe() const;
	};

	/** Per UE bone (mesh bone index): the Daz bone at the same joint, or NAME_None. */
	struct FMapping
	{
		TArray<FName> Daz;
		TArray<double> DistanceCm;
		/** The next nearest Daz bone, for the report. */
		TArray<FName> SecondDaz;
		TArray<double> SecondCm;
	};

	/** Everything the solve needs, measured once per character. */
	struct FRig
	{
		FAxes Axes;
		FMapping Map;
		/** Daz UnrealPose (ROM frame 1). */
		FDTHDazState DazRest;
		/** The mesh's bind pose in component space. */
		TArray<FTransform> MeshRest;
		/**
		 * The library figure (base Genesis 9) fitted to the character's joints, scale included: places the Daz joints that
		 * have no UE joint (hip, pelvis, neck2: DTH places those UE joints itself) on the character.
		 */
		FAxes LibraryAxes;
	};

	/** The reference pose in component space. */
	TArray<FTransform> ComponentSpace(const FReferenceSkeleton& Ref, TFunctionRef<FTransform(int32)> Local);

	/** Bones the solve leaves alone: IK bones (plan section 5) and the root. */
	bool IsSkipped(FName UEBone);

	/** A UE joint and a Daz joint closer than this are the same joint. */
	constexpr double InlierCm = 0.5;

	/** Tries all 48 signed axis permutations and keeps the one that puts the Daz joints on the UE joints. */
	FAxes FitAxes(const FDTHDazState& DazRest, const FReferenceSkeleton& Ref, const TArray<FTransform>& MeshRest,
		double MatchCm = InlierCm);

	/** Maps each UE bone to the Daz bone nearest its joint, within MaxCm. */
	FMapping MapBones(const FAxes& Axes, const FDTHDazState& DazRest, const FReferenceSkeleton& Ref,
		const TArray<FTransform>& MeshRest, double MaxCm);

	/**
	 * The UE local transforms (mesh space, before any retargeting) that recreate Pose. Mapped bones take the Daz bone's
	 * world rotation change; their joints move as points fixed to the frame of the Daz bone their Daz bone hangs from, and
	 * the hip by its translation. Unmapped bones follow their parent at their reference local transform.
	 */
	TMap<FName, FTransform> Solve(const FRig& Rig, const FReferenceSkeleton& Ref, const FDTHDazState& Pose,
		TArray<FTransform>* OutComponent = nullptr);
}
