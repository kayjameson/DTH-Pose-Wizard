#pragma once

#include "CoreMinimal.h"

/**
 * How a character's DTH face setup (its post-process blueprint and face rig) turns face-board curves into weights of
 * DTH's face poses, as measured on the user's own project by FDTHFaceRigProbe. Nothing about DTH's rig ships with the
 * plugin; this is written to the project's Saved folder and measured again when DTH's content changes.
 *
 * Model: weight(pose) = sum of Weight * f(curve) over Linear + sum of Weight * f(a) * f(b) over Pairs, where f(x) is
 * x raised to the curve's Exponent (1, or 2 for curves whose effect goes with the square).
 */
struct DTHFACECORE_API FDTHFaceRig
{
	struct FLink { int32 Curve = INDEX_NONE; int32 Pose = INDEX_NONE; double Weight = 0.0; };
	struct FPair { int32 A = INDEX_NONE; int32 B = INDEX_NONE; int32 Pose = INDEX_NONE; double Weight = 0.0; };

	/** Face-board curves (CTRL_expressions_*) registered on the character. */
	TArray<FName> Curves;
	/** Face poses of DTH's face pose asset, in its order, without its first (rest) pose. */
	TArray<FName> Poses;
	TArray<int32> Exponent;
	TArray<FLink> Linear;
	TArray<FPair> Pairs;
	/** Poses no face-board curve drives. */
	TArray<int32> Undriven;

	/** What was measured, and the files it depends on (re-measured when any of them changes). */
	FString Character;
	FString PostProcessBlueprint;
	FString PoseAsset;
	TArray<FString> Fingerprint;
	/** Anything the model does not capture well (with the size of the miss). */
	TArray<FString> Notes;

	bool Load(const FString& File, FString& OutError);
	bool Save(const FString& File) const;

	int32 FindCurve(FName Name) const { return Curves.IndexOfByKey(Name); }
	int32 FindPose(FName Name) const { return Poses.IndexOfByKey(Name); }

	/** Fills Undriven from Linear and Pairs. */
	void UpdateUndriven();

	/** Pose weights for face-board values C (one per curve). */
	void Evaluate(TConstArrayView<double> C, TArray<double>& OutWeights) const;
};
