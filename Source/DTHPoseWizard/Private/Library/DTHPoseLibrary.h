#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DTHPoseSamples.h"
#include "Serialization/BulkData.h"
#include "DTHPoseLibrary.generated.h"

/** One Daz pose preset in the library: what it is and how it applies. Its bones are in the library's pose data (UDTHPoseLibrary::Data). */
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

	/** True when it moves only part of the figure (hands, for example): it is laid over a base pose. */
	UPROPERTY() bool bPartial = false;
	/** What the preset does that a DTH character cannot reproduce, one line each ("morph: reason"). */
	UPROPERTY() TArray<FString> Missing;
};

/** A pose's bones as Daz evaluated them on base Genesis 9: Daz world space, per library bone. */
struct FDTHPoseData
{
	/** Active rotation and joint position (cm, Y up). */
	TArray<FQuat> Rotations;
	TArray<FVector> Positions;
	/** The library bones this preset moves (its local channels differ from the rest pose). */
	TArray<int32> Moved;
};

/**
 * The pose library: Daz pose presets sampled once on base Genesis 9, for every DTH Genesis 9 character (the bone map
 * and axes ship with the plugin, DTHBoneMap). Built from DTH_PoseSamples.json by DTHPoseLibraryBuild.
 *
 * Libraries can hold hundreds of thousands of poses, so the poses' bones are not properties: they are kept in blocks of
 * bulk data that are read from the asset only when a pose is used (a few blocks are kept at a time). The asset itself
 * loads with the list of poses alone. The bones are stored in single precision (the sampler rounds quaternions to 1e-8
 * and positions to 1e-6 cm; single precision keeps them within 1e-5 deg and 1e-5 cm).
 */
UCLASS()
class UDTHPoseLibrary : public UDataAsset
{
	GENERATED_BODY()

public:
	/** The layout of the pose data. Libraries from before this layout have none and must be built again. */
	static constexpr int32 CurrentFormat = 2;
	UPROPERTY() int32 Format = 0;

	/** The Daz bones and their parents ("" for the root bones). */
	UPROPERTY() TArray<FName> Bones;
	UPROPERTY() TArray<FName> Parents;

	/** UnrealPose (DTH ROM frame 1), the pose the DTH bind pose is in: the solve's rest. */
	UPROPERTY() FDTHPose Rest;
	UPROPERTY() TArray<FQuat> RestRotations;
	UPROPERTY() TArray<FVector> RestPositions;

	UPROPERTY() TArray<FDTHPose> Poses;
	UPROPERTY() TArray<FString> Skipped;

	UPROPERTY() FString SamplesFile;
	UPROPERTY() FString SampledAt;
	UPROPERTY() FString DazVersion;

	/** Counts, kept at build so nothing has to walk the poses to show them. */
	UPROPERTY() int32 NumLoaded = 0;
	UPROPERTY() int32 NumPartial = 0;
	UPROPERTY() int32 NumWithMissing = 0;

	/** Built in the current layout, with its pose data (a copy made by duplicating the asset has none). */
	bool IsCurrent() const { return Format == CurrentFormat && Blocks.Num() == (NumRecords + PosesPerBlock - 1) / PosesPerBlock; }

	/** A pose's bones (INDEX_NONE: Rest). False if the pose has none (not loaded) or the data cannot be read. */
	bool Data(int32 Pose, FDTHPoseData& Out) const;

	/** A library pose (INDEX_NONE: Rest) as a Daz state the solve takes. */
	FDTHDazState State(int32 Pose) const;

	/**
	 * Base (INDEX_NONE: Rest) with Overlay's moved bones laid over it, as Daz applies a partial preset: each moved bone
	 * keeps its rotation and offset relative to its parent from Overlay, the rest keep Base's.
	 */
	FDTHDazState Compose(int32 Base, int32 Overlay) const;

	/** The first pose with this label, or INDEX_NONE. */
	int32 FindPose(const FString& Label) const;

	/** Building: clears the poses and their data; then AddPose for each pose, in order; then EndBuild. */
	void BeginBuild();
	void AddPose(FDTHPose&& Pose, const FDTHPoseData* PoseData);
	void EndBuild();
	/** Takes a finished build's bones, poses and pose data (Built is left empty). */
	void TakeBuild(UDTHPoseLibrary& Built);

	virtual void Serialize(FArchive& Ar) override;

private:
	static constexpr int32 PosesPerBlock = 256;
	int32 RecordBytes() const;
	void FlushBlock();
	const uint8* Record(int32 Pose) const;

	/** Each pose's record in the blocks (INDEX_NONE: no data); a pose's block is Slot / PosesPerBlock. */
	UPROPERTY() TArray<int32> Slots;

	/** Poses with data (records in the blocks). */
	UPROPERTY() int32 NumRecords = 0;

	TIndirectArray<FByteBulkData> Blocks;
	/** The block being written. */
	TArray<uint8> Writing;

	/** Blocks read lately, most recent last. */
	mutable TArray<TPair<int32, TArray<uint8>>> Cache;
	mutable TMap<FString, int32> LabelIndex;
};
