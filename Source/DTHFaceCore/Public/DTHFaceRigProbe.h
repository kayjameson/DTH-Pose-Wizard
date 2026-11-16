#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
struct FDTHFaceRig;

/**
 * Measures how a DTH character's face setup turns face-board curves into face poses, by playing curves through the
 * character's own post-process blueprint off screen and reading what it outputs: every curve alone at full and half
 * strength, then every pair at full strength (to find the curves that only act together, like a blink closing a lid
 * shape). Takes a minute or so; the result is kept in the project's Saved folder.
 */
struct DTHFACECORE_API FDTHFaceRigProbe
{
	/** Where the measurement for this character's face setup is kept. */
	static FString RigFile(const USkeletalMesh* Character);

	/** The files the measurement depends on (post-process blueprint and the assets it uses), as "package|size|time". */
	static TArray<FString> Fingerprint(const USkeletalMesh* Character);

	/** The kept measurement if it is still valid for the character's current DTH content, otherwise a new one (saved). */
	static bool GetOrMeasure(USkeletalMesh* Character, FDTHFaceRig& OutRig, bool& bOutMeasured, FString& OutError);

	static bool Measure(USkeletalMesh* Character, FDTHFaceRig& OutRig, FString& OutError);

	/**
	 * For checking the measurement: plays these curves on the character (one harness kept between calls, so the order
	 * of calls matters as it does during a measurement) and returns the values of Read after the post-process blueprint.
	 */
	static bool DebugEvaluate(USkeletalMesh* Character, const TMap<FName, float>& Curves, const TArray<FName>& Read, TArray<float>& OutValues, FString& OutError);

	/**
	 * Plays each curve set on the character through its post-process blueprint and returns the skinned positions of
	 * Vertices (LOD 0 render vertices, component space, cm) for each set in turn, as Unreal skins them on the CPU.
	 */
	static bool EvaluateGeometry(USkeletalMesh* Character, TConstArrayView<TMap<FName, float>> CurveSets, const TArray<int32>& Vertices,
		TArray<FVector3f>& OutPositions, FString& OutError);
};
