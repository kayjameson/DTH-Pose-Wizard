#include "Build/DTHRetarget.h"

#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"

namespace DTHRetarget
{
	FTransform Forward(const FBone& Bone, const FTransform& Track)
	{
		FTransform Out = Track;
		switch (Bone.Mode)
		{
		case EBoneTranslationRetargetingMode::AnimationScaled:
		{
			const double Source = Bone.SkeletonRef.GetTranslation().Size();
			if (Source > UE_KINDA_SMALL_NUMBER)
			{
				Out.ScaleTranslation(Bone.MeshRef.GetTranslation().Size() / Source);
			}
			break;
		}
		case EBoneTranslationRetargetingMode::Skeleton:
			Out.SetTranslation(Bone.MeshRef.GetTranslation());
			break;
		case EBoneTranslationRetargetingMode::AnimationRelative:
			Out.SetRotation(Track.GetRotation() * Bone.SkeletonRef.GetRotation().Inverse() * Bone.MeshRef.GetRotation());
			Out.SetTranslation(Track.GetTranslation() + (Bone.MeshRef.GetTranslation() - Bone.SkeletonRef.GetTranslation()));
			Out.SetScale3D(Track.GetScale3D() * (Bone.MeshRef.GetScale3D() * Bone.SkeletonRef.GetSafeScaleReciprocal(Bone.SkeletonRef.GetScale3D())));
			Out.NormalizeRotation();
			break;
		default:
			break;
		}
		return Out;
	}

	FTransform Inverse(const FBone& Bone, const FTransform& Wanted)
	{
		FTransform Out = Wanted;
		switch (Bone.Mode)
		{
		case EBoneTranslationRetargetingMode::AnimationScaled:
		{
			const double Target = Bone.MeshRef.GetTranslation().Size();
			if (Bone.SkeletonRef.GetTranslation().Size() > UE_KINDA_SMALL_NUMBER && Target > UE_KINDA_SMALL_NUMBER)
			{
				Out.ScaleTranslation(Bone.SkeletonRef.GetTranslation().Size() / Target);
			}
			break;
		}
		case EBoneTranslationRetargetingMode::Skeleton:
			Out.SetTranslation(Bone.SkeletonRef.GetTranslation());
			break;
		case EBoneTranslationRetargetingMode::AnimationRelative:
			Out.SetRotation(Wanted.GetRotation() * Bone.MeshRef.GetRotation().Inverse() * Bone.SkeletonRef.GetRotation());
			Out.SetTranslation(Wanted.GetTranslation() - (Bone.MeshRef.GetTranslation() - Bone.SkeletonRef.GetTranslation()));
			Out.SetScale3D(Wanted.GetScale3D() * (Bone.SkeletonRef.GetScale3D() * Bone.MeshRef.GetSafeScaleReciprocal(Bone.MeshRef.GetScale3D())));
			Out.NormalizeRotation();
			break;
		default:
			break;
		}
		return Out;
	}

	TArray<FBone> Gather(const USkeleton& Skeleton, const USkeletalMesh& Mesh)
	{
		const FReferenceSkeleton& MeshRef = Mesh.GetRefSkeleton();
		const FReferenceSkeleton& SkelRef = Skeleton.GetReferenceSkeleton();
		TArray<FBone> Out;
		Out.SetNum(MeshRef.GetNum());
		for (int32 I = 0; I < MeshRef.GetNum(); ++I)
		{
			FBone& B = Out[I];
			B.MeshRef = MeshRef.GetRefBonePose()[I];
			const int32 S = SkelRef.FindBoneIndex(MeshRef.GetBoneName(I));
			if (S == INDEX_NONE)
			{
				B.SkeletonRef = B.MeshRef;
				continue;
			}
			B.SkeletonRef = SkelRef.GetRefBonePose()[S];
			B.Mode = Skeleton.GetBoneTranslationRetargetingMode(S);
		}
		return Out;
	}
}
