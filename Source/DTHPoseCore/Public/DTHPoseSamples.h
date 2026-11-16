#pragma once

#include "CoreMinimal.h"
#include "DTHFaceRegion.h"

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
	/**
	 * The face as DTH's face rig plays it: face-board curves (CTRL_expressions_*) and DTH's morph curves (fac*), fitted to
	 * the sampled face shape (DTHFaceCore's shape fit). Empty when the state leaves the face at rest or it was not fitted.
	 */
	TMap<FName, float> FaceCurves;
	/**
	 * Face bones turned past DTH's face rig (eyes, jaw, tongue), fitted with the curves: a local turn on top of each
	 * bone's reference pose. With FaceCurves, the face bones play at their reference pose turned by these.
	 */
	TMap<FName, FQuat> FaceBones;

	bool HasFace() const { return !FaceCurves.IsEmpty() || !FaceBones.IsEmpty(); }
};

/** One pose preset from the user's library. */
struct FDTHDazPose
{
	FString Pack, Label, File, Icon, Type, Reason;
	bool bLoaded = false;
	FDTHDazState State;
	/**
	 * The preset's face alone (its face values on the figure at rest, the body left out): per face figure, how far each
	 * face-region vertex moved (Daz space, cm). Empty when the preset leaves the face at rest or the samples have no face.
	 */
	TArray<TArray<FVector3f>> Face;
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
	/** The face region (face.json): the face figures the sampler measures each preset's face on. Empty in older samples. */
	TArray<FDTHFaceFigure> Face;
	bool HasFace() const { return !Face.IsEmpty(); }

	bool Load(const FString& File, FString& OutError);

	/** Hands every pose to Visit in the sampler's order; Visit returns false to stop. False (and OutError) if a file is unreadable. */
	bool ReadPoses(TFunctionRef<bool(FDTHDazPose&&)> Visit, FString& OutError) const;

	/** The poses Wanted picks (by label), for checks that look at a few presets. */
	bool ReadPoses(TArray<FDTHDazPose>& Out, TFunctionRef<bool(const FString& Label)> Wanted, FString& OutError) const;

private:
	FString File;
	/** Version 2: the samples files (absolute paths). Version 1 keeps its poses in File itself. */
	TArray<FString> Chunks;
	/** Per chunk, its face geometry file (absolute; empty when the chunk has none). */
	TArray<FString> FaceChunks;
	double FaceUnit = 1e-4;
};

class FJsonObject;

/** The samples' state format, shared by every Daz sampler (DTH_SamplePoses.dsa, the Animation Wizard's clip sampler). */
namespace DTHDazSamples
{
	/** One sampled state ({bones: {name: {parent, pos, rot, local}}, morphs: {name: value}}). False if it is malformed. */
	DTHPOSECORE_API bool ReadState(const TSharedPtr<FJsonObject>& Object, FDTHDazState& Out);
}
