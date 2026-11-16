#pragma once

#include "CoreMinimal.h"
#include "PreviewScene.h"

class UMorphTarget;
class USkeletalMesh;
class USkeletalMeshComponent;
class UDTHCurveDriverInstance;

/**
 * The character in an off-screen preview world, played through its own post-process blueprint (DTH's face rig and pose
 * assets) with a set of face-board curves, and the resulting positions of chosen render vertices: the face as Unreal
 * shows it, measured exactly. Only the chosen vertices are skinned (morph targets, then linear skinning, as Unreal's CPU
 * skinning does it), so one evaluation costs about as much as the animation update.
 */
class DTHFACECORE_API FDTHFaceEvaluator
{
public:
	FDTHFaceEvaluator();
	~FDTHFaceEvaluator();

	bool Init(USkeletalMesh* Character, FString& OutError);

	const USkeletalMesh* GetMesh() const { return Mesh; }

	/** LOD 0 render vertices. */
	int32 NumVertices() const;
	/** The material slot of a render vertex's section. */
	FName MaterialSlot(int32 Vertex) const;

	/** DTH's per-pose face morph curves on the mesh (fac*), which the fit may add on top of the face board. */
	TArray<FName> FaceMorphCurves() const;

	/**
	 * How the chosen vertices move per unit of a morph target's weight, in the pose of the last Evaluate (x, y, z per
	 * vertex). Exact: skinning is linear in the morphed position for a given pose. False if the mesh has no such morph.
	 */
	bool MorphSlope(FName Morph, TArray<float>& Out) const;

	/** The bone with the largest skin weight on a chosen vertex (index into GetVertices()). */
	FName DominantBone(int32 Index) const;

	/** The render vertices Evaluate measures, in this order. */
	void SetVertices(TArray<int32> Vertices);
	const TArray<int32>& GetVertices() const { return Vertices; }

	/**
	 * Bones posed under every following evaluation (local transforms by name; empty: the reference pose), e.g. the face
	 * bones a pose sequence keys, so curves are measured on top of them as the character plays both.
	 */
	void SetBaseBones(const TMap<FName, FTransform>& Bones);

	/** Component-space positions (cm) of the chosen vertices with these face-board curves (every other curve at 0). */
	void Evaluate(const TMap<FName, float>& Curves, TArray<FVector3f>& OutPositions);

	/** The same positions read from Unreal's own CPU skinning of the whole mesh (slow; for checking Evaluate). */
	void EvaluateWithEngine(const TMap<FName, float>& Curves, TArray<FVector3f>& OutPositions);

	int32 Evaluations = 0;

	/**
	 * Skin with dual quaternions, as DTH's mesh deformer (DG_DTH_DQS) renders the character; false: linear, as Unreal's
	 * default skinning and its CPU skinning do (EvaluateWithEngine compares against that).
	 */
	bool bDualQuaternion = true;

private:
	void Play(const TMap<FName, float>& Curves);

	FPreviewScene Scene;
	USkeletalMeshComponent* Component = nullptr;
	UDTHCurveDriverInstance* Driver = nullptr;
	USkeletalMesh* Mesh = nullptr;

	TArray<int32> Vertices;
	/** Per chosen vertex: reference position and its bone influences (mesh bone, weight). */
	TArray<FVector3f> RefPositions;
	TArray<int32> InfluenceStart;
	TArray<int32> InfluenceBone;
	TArray<float> InfluenceWeight;
	/** Per morph target: its deltas on the chosen vertices (index into Vertices, position delta). */
	TMap<const UMorphTarget*, TArray<TPair<int32, FVector3f>>> MorphDeltas;

	TArray<FMatrix44f> RefToLocals;
	TArray<FQuat4f> BoneReal;
	TArray<FQuat4f> BoneDual;
	TArray<FVector3f> Morphed;
	/** Per chosen vertex, the linear part of its skinning in the last Evaluate (morphed position -> skinned position). */
	TArray<FMatrix44f> SkinLinear;
	TMap<FName, const UMorphTarget*> MorphByName;
};
