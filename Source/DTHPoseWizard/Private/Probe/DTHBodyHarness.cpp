#include "Probe/DTHBodyHarness.h"

#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Probe/DTHBonePoseDriverInstance.h"

FDTHBodyHarness::FDTHBodyHarness()
	: Scene(FPreviewScene::ConstructionValues())
{
}

FDTHBodyHarness::~FDTHBodyHarness()
{
	if (Component)
	{
		Scene.RemoveComponent(Component);
	}
}

bool FDTHBodyHarness::Init(USkeletalMesh* Mesh, FString& OutError)
{
	if (!Mesh)
	{
		OutError = TEXT("No character mesh.");
		return false;
	}
	Component = NewObject<USkeletalMeshComponent>(GetTransientPackage());
	Component->SetSkeletalMesh(Mesh);
	Component->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Component->bEnableUpdateRateOptimizations = false;
	Component->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Component->SetAnimInstanceClass(UDTHBonePoseDriverInstance::StaticClass());
	Scene.AddComponent(Component, FTransform::Identity);
	Driver = Cast<UDTHBonePoseDriverInstance>(Component->GetAnimInstance());
	if (!Driver || !Component->GetPostProcessInstance())
	{
		OutError = TEXT("The character has no post-process blueprint running (it needs DTH's, as the DTH import sets up).");
		return false;
	}
	// The post-process blueprint settles over its first evaluations (its pose drivers and curve smoothing).
	FDTHBoneReadback Settle;
	EvalPose({}, {}, Settle);
	EvalPose({}, {}, Settle);
	return true;
}

void FDTHBodyHarness::EvalPose(const TMap<FName, FTransform>& Bones, const TMap<FName, float>& Curves, FDTHBoneReadback& Out)
{
	Driver->SetPose(Bones, Curves);
	Tick(Out);
}

void FDTHBodyHarness::EvalSequence(const UAnimSequence* Sequence, double Time, FDTHBoneReadback& Out)
{
	Driver->SetSequence(Sequence, Time);
	Tick(Out);
}

void FDTHBodyHarness::Tick(FDTHBoneReadback& Out)
{
	Component->TickAnimation(1.0f / 30.0f, false);
	Component->RefreshBoneTransforms();
	Component->HandleExistingParallelEvaluationTask(true, true);

	const FReferenceSkeleton& Ref = Component->GetSkeletalMeshAsset()->GetRefSkeleton();
	const TArray<FTransform>& Space = Component->GetComponentSpaceTransforms();
	const TArray<FTransform>& Local = Component->GetBoneSpaceTransforms();
	const int32 Num = FMath::Min3(Ref.GetNum(), Space.Num(), Local.Num());
	Out.Bones.SetNum(Num);
	Out.Component.SetNum(Num);
	Out.Local.SetNum(Num);
	for (int32 I = 0; I < Num; ++I)
	{
		Out.Bones[I] = Ref.GetBoneName(I);
		Out.Component[I] = Space[I];
		Out.Local[I] = Local[I];
	}
	Out.Curves.Reset();
	if (const UAnimInstance* Post = Component->GetPostProcessInstance())
	{
		Post->AppendAnimationCurveList(EAnimCurveType::AttributeCurve, Out.Curves);
	}
}
