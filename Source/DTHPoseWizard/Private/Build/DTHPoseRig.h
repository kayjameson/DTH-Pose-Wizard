#pragma once

#include "CoreMinimal.h"
#include "Build/DTHPoseSolve.h"

class UAnimSequence;
class UDTHPoseLibrary;
class USkeletalMesh;

/**
 * The library on one DTH Genesis 9 character: library poses in, bone-track sequences for the character's skeleton out.
 * A sequence is written for the character it plays on (Unreal's retargeting does not carry a sequence written for one
 * character to another exactly: DTH.Pose.P0.CrossCharacter).
 */
namespace DTHPoseRig
{
	/** The rig: shipped bone map and axes, the library's rest, the character's bind pose. */
	DTHPoseSolve::FRig Make(const USkeletalMesh& Mesh, const UDTHPoseLibrary& Library);

	/** A library pose as Daz would show it: partial poses laid over Base (INDEX_NONE: the rest pose, UnrealPose). */
	FDTHDazState Resolve(const UDTHPoseLibrary& Library, int32 Pose, int32 Base = INDEX_NONE);

	/** The mesh-space local pose that recreates State on the character, as its written sequence plays back (the preview). */
	TMap<FName, FTransform> LocalPose(const DTHPoseSolve::FRig& Rig, const USkeletalMesh& Mesh, const FDTHDazState& State);

	/** The bone tracks, authored on the skeleton, that play back as LocalPose on this character. */
	TMap<FName, FTransform> Track(const DTHPoseSolve::FRig& Rig, const USkeletalMesh& Mesh, const FDTHDazState& State);

	/**
	 * Creates and saves Folder/Name for the character's skeleton, one key per state (a pose sheet when there are
	 * several). A name already taken gets a number.
	 */
	UAnimSequence* Write(const USkeletalMesh& Mesh, const UDTHPoseLibrary& Library, TConstArrayView<FDTHDazState> Frames,
		const FString& Folder, const FString& Name, FString& OutError);

	/** A content-safe asset name from a pose label. */
	FString AssetName(const FString& Label);
}
