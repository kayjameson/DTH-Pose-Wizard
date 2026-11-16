#include "DTHCurveDriverInstance.h"

void FDTHCurveDriverProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	Values.Reset();
	const UDTHCurveDriverInstance* Instance = CastChecked<UDTHCurveDriverInstance>(InAnimInstance);
	for (const TPair<FName, float>& V : Instance->GetCurves())
	{
		Values.Emplace(V.Key, V.Value);
	}
	Bones.Reset();
	if (!Instance->GetBones().IsEmpty())
	{
		const FBoneContainer& Required = GetRequiredBones();
		const FReferenceSkeleton& Ref = Required.GetReferenceSkeleton();
		for (const TPair<FName, FTransform>& B : Instance->GetBones())
		{
			const int32 MeshIndex = Ref.FindBoneIndex(B.Key);
			if (MeshIndex == INDEX_NONE) continue;
			const FCompactPoseBoneIndex Compact = Required.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
			if (Compact != INDEX_NONE) Bones.Emplace(Compact.GetInt(), B.Value);
		}
	}
}

bool FDTHCurveDriverProxy::Evaluate(FPoseContext& Output)
{
	Output.ResetToRefPose();
	for (const TPair<int32, FTransform>& B : Bones)
	{
		Output.Pose[FCompactPoseBoneIndex(B.Key)] = B.Value;
	}
	for (const TPair<FName, float>& V : Values)
	{
		Output.Curve.Set(V.Key, V.Value);
	}
	return true;
}
