#include "Probe/DTHBonePoseDriverInstance.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/Skeleton.h"

void FDTHBonePoseDriverProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	const UDTHBonePoseDriverInstance* Driver = CastChecked<UDTHBonePoseDriverInstance>(InAnimInstance);
	Sequence = Driver->GetSequence();
	Time = Driver->GetTime();
	Bones.Reset();
	if (const USkeleton* Current = InAnimInstance->CurrentSkeleton)
	{
		const FReferenceSkeleton& Ref = Current->GetReferenceSkeleton();
		for (const TPair<FName, FTransform>& B : Driver->GetBones())
		{
			const int32 Index = Ref.FindBoneIndex(B.Key);
			if (Index != INDEX_NONE)
			{
				Bones.Emplace(Index, B.Value);
			}
		}
	}
	Curves.Reset();
	for (const TPair<FName, float>& V : Driver->GetCurves())
	{
		Curves.Emplace(V.Key, V.Value);
	}
}

bool FDTHBonePoseDriverProxy::Evaluate(FPoseContext& Output)
{
	Output.ResetToRefPose();
	if (Sequence)
	{
		FAnimationPoseData PoseData(Output);
		Sequence->GetAnimationPose(PoseData, FAnimExtractContext(Time));
		return true;
	}
	const FBoneContainer& Container = Output.Pose.GetBoneContainer();
	for (const TPair<int32, FTransform>& B : Bones)
	{
		const FCompactPoseBoneIndex Index = Container.GetCompactPoseIndexFromSkeletonPoseIndex(FSkeletonPoseBoneIndex(B.Key));
		if (Index.IsValid())
		{
			Output.Pose[Index] = B.Value;
		}
	}
	for (const TPair<FName, float>& V : Curves)
	{
		Output.Curve.Set(V.Key, V.Value);
	}
	return true;
}
