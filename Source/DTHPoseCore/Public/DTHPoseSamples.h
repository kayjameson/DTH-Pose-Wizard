#pragma once

#include "CoreMinimal.h"

/** One Daz bone as the sampler recorded it: Daz world space (cm, Y up, right-handed). Rot is the active rotation. */
struct FDTHDazBone
{
	FName Parent;
	FVector Pos = FVector::ZeroVector;
	FQuat Rot = FQuat::Identity;
	/** The evaluated local channels: X, Y, Z rotate (deg), X, Y, Z translate (cm). */
	float Local[6] = {0, 0, 0, 0, 0, 0};
};

/** The figure in one state (rest, a ROM frame or a preset): every bone, and every morph that is not zero. */
struct FDTHDazState
{
	TMap<FName, FDTHDazBone> Bones;
	TMap<FName, float> Morphs;
};

/** One pose preset from the user's library. */
struct FDTHDazPose
{
	FString Pack, Label, File, Icon, Type, Reason;
	bool bLoaded = false;
	FDTHDazState State;
};

/**
 * DTH_PoseSamples.json, written by Resources/Daz/DTH_SamplePoses.dsa. Load reads the rest state, the ROM and the skipped
 * files; the poses, which can run to hundreds of thousands, are only read by ReadPoses, one samples file at a time.
 */
struct DTHPOSECORE_API FDTHPoseSamples
{
	FString DazVersion, SampledAt;
	FDTHDazState Rest;
	/** DTH's ROM, by frame. */
	TArray<FDTHDazState> RomFrames;
	TArray<FString> Skipped;
	/** How many presets the samples hold. */
	int32 NumPoses = 0;

	bool Load(const FString& File, FString& OutError);

	/** Hands every pose to Visit in the sampler's order; Visit returns false to stop. False (and OutError) if a file is unreadable. */
	bool ReadPoses(TFunctionRef<bool(FDTHDazPose&&)> Visit, FString& OutError) const;

	/** The poses Wanted picks (by label), for checks that look at a few presets. */
	bool ReadPoses(TArray<FDTHDazPose>& Out, TFunctionRef<bool(const FString& Label)> Wanted, FString& OutError) const;

private:
	FString File;
	/** Version 2: the samples files (absolute paths). Version 1 keeps its poses in File itself. */
	TArray<FString> Chunks;
};

class FJsonObject;

/** The samples' state format, shared by every Daz sampler (DTH_SamplePoses.dsa, the Animation Wizard's clip sampler). */
namespace DTHDazSamples
{
	/** One sampled state ({bones: {name: {parent, pos, rot, local}}, morphs: {name: value}}). False if it is malformed. */
	DTHPOSECORE_API bool ReadState(const TSharedPtr<FJsonObject>& Object, FDTHDazState& Out);
}
