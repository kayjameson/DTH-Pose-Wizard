#include "DTHFaceEvaluator.h"

#include "Animation/AnimInstance.h"
#include "Animation/MorphTarget.h"
#include "AnimationRuntime.h"
#include "BoneWeights.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "DTHCurveDriverInstance.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SkeletalRenderPublic.h"

FDTHFaceEvaluator::FDTHFaceEvaluator()
	: Scene(FPreviewScene::ConstructionValues())
{
}

FDTHFaceEvaluator::~FDTHFaceEvaluator()
{
	if (Component)
	{
		Scene.RemoveComponent(Component);
	}
}

bool FDTHFaceEvaluator::Init(USkeletalMesh* Character, FString& OutError)
{
	Mesh = Character;
	if (!Mesh || !Mesh->GetResourceForRendering() || Mesh->GetResourceForRendering()->LODRenderData.IsEmpty())
	{
		OutError = TEXT("No character mesh, or it has no render data.");
		return false;
	}
	Component = NewObject<USkeletalMeshComponent>(GetTransientPackage());
	Component->SetSkeletalMesh(Mesh);
	Component->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Component->bEnableUpdateRateOptimizations = false;
	Component->SetForcedLOD(1);
	Component->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Component->SetAnimInstanceClass(UDTHCurveDriverInstance::StaticClass());
	Scene.AddComponent(Component, FTransform::Identity);
	Driver = Cast<UDTHCurveDriverInstance>(Component->GetAnimInstance());
	if (!Driver || !Component->GetPostProcessInstance())
	{
		OutError = TEXT("The character has no post-process blueprint running (it needs DTH's, as the DTH import sets up).");
		return false;
	}
	return true;
}

int32 FDTHFaceEvaluator::NumVertices() const
{
	return Mesh ? Mesh->GetResourceForRendering()->LODRenderData[0].GetNumVertices() : 0;
}

FName FDTHFaceEvaluator::MaterialSlot(int32 Vertex) const
{
	const FSkeletalMeshLODRenderData& LOD = Mesh->GetResourceForRendering()->LODRenderData[0];
	int32 Section = 0, InSection = 0;
	LOD.GetSectionFromVertexIndex(Vertex, Section, InSection);
	const int32 Material = LOD.RenderSections[Section].MaterialIndex;
	return Mesh->GetMaterials().IsValidIndex(Material) ? Mesh->GetMaterials()[Material].MaterialSlotName : NAME_None;
}

void FDTHFaceEvaluator::SetVertices(TArray<int32> InVertices)
{
	Vertices = MoveTemp(InVertices);
	const FSkeletalMeshLODRenderData& LOD = Mesh->GetResourceForRendering()->LODRenderData[0];
	const FSkinWeightVertexBuffer& Weights = *LOD.GetSkinWeightVertexBuffer();
	const int32 MaxInfluences = Weights.GetMaxBoneInfluences();

	RefPositions.SetNumUninitialized(Vertices.Num());
	InfluenceStart.Reset(Vertices.Num() + 1);
	InfluenceBone.Reset();
	InfluenceWeight.Reset();
	TMap<int32, int32> Local;
	for (int32 I = 0; I < Vertices.Num(); ++I)
	{
		const int32 V = Vertices[I];
		Local.Add(V, I);
		RefPositions[I] = LOD.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(V);
		int32 Section = 0, InSection = 0;
		LOD.GetSectionFromVertexIndex(V, Section, InSection);
		const FSkelMeshRenderSection& S = LOD.RenderSections[Section];
		const int32 BufferIndex = S.GetVertexBufferIndex() + InSection;
		InfluenceStart.Add(InfluenceBone.Num());
		for (int32 K = 0; K < MaxInfluences; ++K)
		{
			const float W = static_cast<float>(Weights.GetBoneWeight(BufferIndex, K)) * UE::AnimationCore::InvMaxRawBoneWeightFloat;
			const int32 BoneMapIndex = Weights.GetBoneIndex(BufferIndex, K);
			if (W > 0.0f && S.BoneMap.IsValidIndex(BoneMapIndex))
			{
				InfluenceBone.Add(S.BoneMap[BoneMapIndex]);
				InfluenceWeight.Add(W);
			}
		}
	}
	InfluenceStart.Add(InfluenceBone.Num());

	MorphDeltas.Reset();
	MorphByName.Reset();
	for (const UMorphTarget* Morph : Mesh->GetMorphTargets())
	{
		if (!Morph) continue;
		TArray<TPair<int32, FVector3f>> Deltas;
		for (const FMorphTargetDelta& D : Morph->GetMorphTargetDeltas(0))
		{
			if (const int32* I = Local.Find(static_cast<int32>(D.SourceIdx))) Deltas.Emplace(*I, D.PositionDelta);
		}
		if (!Deltas.IsEmpty()) MorphDeltas.Add(Morph, MoveTemp(Deltas));
		MorphByName.Add(Morph->GetFName(), Morph);
	}
}

FName FDTHFaceEvaluator::DominantBone(int32 Index) const
{
	int32 Best = INDEX_NONE;
	float BestWeight = -1.0f;
	for (int32 K = InfluenceStart[Index]; K < InfluenceStart[Index + 1]; ++K)
	{
		if (InfluenceWeight[K] > BestWeight) { BestWeight = InfluenceWeight[K]; Best = InfluenceBone[K]; }
	}
	return Best == INDEX_NONE ? NAME_None : Mesh->GetRefSkeleton().GetBoneName(Best);
}

void FDTHFaceEvaluator::Play(const TMap<FName, float>& Curves)
{
	++Evaluations;
	Driver->SetCurves(Curves);
	// two updates: a node that eases toward its input would not have arrived after one
	for (int32 T = 0; T < 2; ++T)
	{
		Component->TickAnimation(1.0f / 30.0f, false);
		Component->RefreshBoneTransforms();
		Component->HandleExistingParallelEvaluationTask(true, true);
	}
}

void FDTHFaceEvaluator::Evaluate(const TMap<FName, float>& Curves, TArray<FVector3f>& OutPositions)
{
	Play(Curves);
	Component->GetCurrentRefToLocalMatrices(RefToLocals, 0);

	Morphed = RefPositions;
	for (const TPair<const UMorphTarget*, int32>& Active : Component->ActiveMorphTargets)
	{
		const float W = Component->MorphTargetWeights.IsValidIndex(Active.Value) ? Component->MorphTargetWeights[Active.Value] : 0.0f;
		if (FMath::Abs(W) < 1e-6f) continue;
		if (const TArray<TPair<int32, FVector3f>>* Deltas = MorphDeltas.Find(Active.Key))
		{
			for (const TPair<int32, FVector3f>& D : *Deltas) Morphed[D.Key] += D.Value * W;
		}
	}

	OutPositions.SetNumUninitialized(Vertices.Num());
	SkinLinear.SetNumUninitialized(Vertices.Num());
	if (!bDualQuaternion)
	{
		for (int32 I = 0; I < Vertices.Num(); ++I)
		{
			FVector3f P = FVector3f::ZeroVector;
			FMatrix44f L(ForceInitToZero);
			for (int32 K = InfluenceStart[I]; K < InfluenceStart[I + 1]; ++K)
			{
				P += RefToLocals[InfluenceBone[K]].TransformPosition(Morphed[I]) * InfluenceWeight[K];
				L += RefToLocals[InfluenceBone[K]] * InfluenceWeight[K];
			}
			OutPositions[I] = P;
			SkinLinear[I] = L;
		}
		return;
	}

	// DTH's deformer (DG_DTH_DQS): each bone's ref-to-local matrix as a dual quaternion, blended by skin weight with each
	// bone turned to the side of the running sum, normalised, then applied to the morphed position.
	BoneReal.SetNumUninitialized(RefToLocals.Num());
	BoneDual.SetNumUninitialized(RefToLocals.Num());
	for (int32 B = 0; B < RefToLocals.Num(); ++B)
	{
		const FMatrix44f& M = RefToLocals[B];
		const FQuat4f R(M);
		const FVector3f T = M.GetOrigin();
		BoneReal[B] = R;
		BoneDual[B] = (FQuat4f(T.X, T.Y, T.Z, 0.0f) * R) * 0.5f;
	}
	for (int32 I = 0; I < Vertices.Num(); ++I)
	{
		FQuat4f Real(0.0f, 0.0f, 0.0f, 0.0f), Dual(0.0f, 0.0f, 0.0f, 0.0f);
		for (int32 K = InfluenceStart[I]; K < InfluenceStart[I + 1]; ++K)
		{
			const int32 B = InfluenceBone[K];
			const float W = (BoneReal[B] | Real) < 0.0f ? -InfluenceWeight[K] : InfluenceWeight[K];
			Real = Real + BoneReal[B] * W;
			Dual = Dual + BoneDual[B] * W;
		}
		const float Length = FMath::Sqrt(Real | Real);
		if (Length < 1e-8f) { OutPositions[I] = Morphed[I]; SkinLinear[I] = FMatrix44f::Identity; continue; }
		Real = Real * (1.0f / Length);
		SkinLinear[I] = Real.ToMatrix();
		Dual = Dual * (1.0f / Length);
		const FQuat4f Translation = (Dual * Real.Inverse()) * 2.0f;
		OutPositions[I] = Real.RotateVector(Morphed[I]) + FVector3f(Translation.X, Translation.Y, Translation.Z);
	}
}

void FDTHFaceEvaluator::EvaluateWithEngine(const TMap<FName, float>& Curves, TArray<FVector3f>& OutPositions)
{
	Play(Curves);
	TArray<FFinalSkinVertex> Skinned;
	Component->GetCPUSkinnedVertices(Skinned, 0);
	OutPositions.SetNumUninitialized(Vertices.Num());
	for (int32 I = 0; I < Vertices.Num(); ++I)
	{
		OutPositions[I] = Skinned.IsValidIndex(Vertices[I]) ? Skinned[Vertices[I]].Position : FVector3f::ZeroVector;
	}
}

TArray<FName> FDTHFaceEvaluator::FaceMorphCurves() const
{
	// DTH names its face-pose morphs fac<Pose> (the FAC pose asset's morphs); facs_* would be a Daz FACS name.
	TArray<FName> Out;
	for (const UMorphTarget* Morph : Mesh ? Mesh->GetMorphTargets() : TArray<TObjectPtr<UMorphTarget>>())
	{
		if (!Morph) continue;
		const FString Name = Morph->GetName();
		if (Name.StartsWith(TEXT("fac"), ESearchCase::CaseSensitive) && !Name.StartsWith(TEXT("facs"), ESearchCase::CaseSensitive)
			&& Name != TEXT("facRestPose")) Out.Add(Morph->GetFName());
	}
	Out.Sort(FNameLexicalLess());
	return Out;
}

bool FDTHFaceEvaluator::MorphSlope(FName Morph, TArray<float>& Out) const
{
	const UMorphTarget* const* Target = MorphByName.Find(Morph);
	Out.Init(0.0f, 3 * Vertices.Num());
	if (!Target) return false;
	const TArray<TPair<int32, FVector3f>>* Deltas = MorphDeltas.Find(*Target);
	if (!Deltas || SkinLinear.Num() != Vertices.Num()) return Deltas != nullptr;
	for (const TPair<int32, FVector3f>& D : *Deltas)
	{
		const FVector3f V = SkinLinear[D.Key].TransformVector(D.Value);
		Out[3 * D.Key] = V.X; Out[3 * D.Key + 1] = V.Y; Out[3 * D.Key + 2] = V.Z;
	}
	return true;
}

void FDTHFaceEvaluator::SetBaseBones(const TMap<FName, FTransform>& Bones)
{
	if (Driver) Driver->SetBones(Bones);
}
