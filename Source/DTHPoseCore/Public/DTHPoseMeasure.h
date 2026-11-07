#pragma once

#include "CoreMinimal.h"
#include "DTHPoseSolve.h"

struct FDTHBoneReadback;

/**
 * The Pose Wizard's pass rule (plan section 8, DTH.Pose.P0.PresetPoses), shared so poses and animation frames are
 * judged the same way: a state played on the character in UE against the same state Daz evaluated on the character
 * itself. Each mapped UE joint is carried as a point by the Daz bone its Daz bone hangs from (DTH places a few joints off
 * the Daz joints); rotations are the Daz bone's world change since UnrealPose on the bind pose. Twist bones are kept
 * apart: DTH's post-process blueprint solves them from the hand. Pass: every joint within 0.5 cm and every non-twist bone
 * within 0.5 deg.
 */
namespace DTHPoseMeasure
{
	struct FError
	{
		double MaxCm = 0.0, MaxDeg = 0.0, MaxTwistDeg = 0.0;
		FName CmBone, DegBone, TwistBone;
		int32 Bones = 0;

		bool Passes(double Cm = 0.5, double Deg = 0.5) const { return Bones > 0 && MaxCm <= Cm && MaxDeg <= Deg; }
		DTHPOSECORE_API FString Describe() const;
	};

	/**
	 * Played (the character after its post-process blueprint) against Daz on the character: OwnNow, measured from OwnRest
	 * (UnrealPose sampled on the character). Rig gives the axes, the bone map and the bind pose. Lines, if given, gets one
	 * "label\tbone\tdaz_bone\tpos_err_cm\trot_err_deg" row per bone.
	 */
	DTHPOSECORE_API FError Compare(const DTHPoseSolve::FRig& Rig, const FReferenceSkeleton& Ref, const FDTHBoneReadback& Played,
		const FDTHDazState& OwnRest, const FDTHDazState& OwnNow, TArray<FString>* Lines = nullptr, const FString& Label = FString());
}
