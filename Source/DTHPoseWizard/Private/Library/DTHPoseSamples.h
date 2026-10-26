#pragma once

#include "CoreMinimal.h"

/** One Daz bone as the sampler recorded it: Daz world space (cm, Y up, right-handed). Rot is the active rotation. */
struct FDTHDazBone
{
	FName Parent;
	FVector Pos = FVector::ZeroVector;
	FQuat Rot = FQuat::Identity;
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

/** DTH_PoseSamples.json, written by Resources/Daz/DTH_SamplePoses.dsa. */
struct FDTHPoseSamples
{
	FString DazVersion, SampledAt;
	FDTHDazState Rest;
	/** DTH's ROM, by frame. */
	TArray<FDTHDazState> RomFrames;
	TArray<FDTHDazPose> Poses;
	TArray<FString> Skipped;

	bool Load(const FString& File, FString& OutError);
};
