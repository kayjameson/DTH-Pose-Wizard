#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Library/DTHPoseSamples.h"
#include "DTHPoseLibrary.generated.h"

/** One Daz pose preset as Daz evaluated it on base Genesis 9: every bone's world transform (UDTHPoseLibrary::Bones). */
USTRUCT()
struct FDTHPose
{
	GENERATED_BODY()

	UPROPERTY() FString Pack;
	UPROPERTY() FString Label;
	UPROPERTY() FString SourceFile;
	UPROPERTY() FString IconFile;
	/** preset_pose or preset_hierarchical_pose (or "compressed" when the file did not say). */
	UPROPERTY() FString Type;
	UPROPERTY() bool bLoaded = false;
	/** Why it is not in the library, when it is not. */
	UPROPERTY() FString Reason;

	/** Daz world space, per library bone: active rotation and joint position (cm, Y up). */
	UPROPERTY() TArray<FQuat> Rotations;
	UPROPERTY() TArray<FVector> Positions;
	/** The library bones this preset moves (its local channels differ from the rest pose). */
	UPROPERTY() TArray<int32> Moved;
	/** True when it moves only part of the figure (hands, for example): it is laid over a base pose. */
	UPROPERTY() bool bPartial = false;
	/** What the preset does that a DTH character cannot reproduce, one line each ("morph: reason"). */
	UPROPERTY() TArray<FString> Missing;
};

/**
 * The pose library: Daz pose presets sampled once on base Genesis 9, for every DTH Genesis 9 character (the bone map
 * and axes ship with the plugin, DTHBoneMap). Built from DTH_PoseSamples.json by FDTHPoseLibraryBuild.
 */
UCLASS()
class UDTHPoseLibrary : public UDataAsset
{
	GENERATED_BODY()

public:
	/** The Daz bones and their parents ("" for the root bones). */
	UPROPERTY() TArray<FName> Bones;
	UPROPERTY() TArray<FName> Parents;

	/** UnrealPose (DTH ROM frame 1), the pose the DTH bind pose is in: the solve's rest. */
	UPROPERTY() FDTHPose Rest;
	UPROPERTY() TArray<FDTHPose> Poses;
	UPROPERTY() TArray<FString> Skipped;

	UPROPERTY() FString SamplesFile;
	UPROPERTY() FString SampledAt;
	UPROPERTY() FString DazVersion;

	/** A library pose (or Rest) as a Daz state the solve takes. */
	FDTHDazState State(const FDTHPose& Pose) const;

	/**
	 * Base with Overlay's moved bones laid over it, as Daz applies a partial preset: each moved bone keeps its rotation
	 * and offset relative to its parent from Overlay, the rest keep Base's.
	 */
	FDTHDazState Compose(const FDTHPose& Base, const FDTHPose& Overlay) const;

	int32 FindPose(const FString& Label) const;
};
