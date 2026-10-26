#pragma once

#include "CoreMinimal.h"
#include "PreviewScene.h"

class UAnimSequence;
class UDTHBonePoseDriverInstance;
class USkeletalMesh;
class USkeletalMeshComponent;

/** The character's bones and curves after its post-process blueprint ran. Indices are the mesh's bone indices. */
struct FDTHBoneReadback
{
	TArray<FName> Bones;
	TArray<FTransform> Local;
	TArray<FTransform> Component;
	TMap<FName, float> Curves;

	int32 Find(FName Bone) const { return Bones.IndexOfByKey(Bone); }
};

/**
 * A DTH character in an off-screen preview world (never the level), driven by UDTHBonePoseDriverInstance, with DTH's
 * own post-process blueprint running after it, exactly as the DTH import sets it up.
 */
class FDTHBodyHarness
{
public:
	FDTHBodyHarness();
	~FDTHBodyHarness();

	/** Fails if the mesh has no post-process blueprint running. */
	bool Init(USkeletalMesh* Mesh, FString& OutError);

	/** Plays these local bone transforms (mesh space, after retargeting) and curves, and reads the result back. */
	void EvalPose(const TMap<FName, FTransform>& Bones, const TMap<FName, float>& Curves, FDTHBoneReadback& Out);

	/** Plays one frame of a sequence the way a sequence player does (retargeting included) and reads the result back. */
	void EvalSequence(const UAnimSequence* Sequence, double Time, FDTHBoneReadback& Out);

	USkeletalMeshComponent* GetComponent() const { return Component; }

private:
	void Tick(FDTHBoneReadback& Out);

	FPreviewScene Scene;
	USkeletalMeshComponent* Component = nullptr;
	UDTHBonePoseDriverInstance* Driver = nullptr;
};
