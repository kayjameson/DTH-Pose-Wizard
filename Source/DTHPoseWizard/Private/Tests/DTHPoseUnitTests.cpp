// Numeric core against hand-made cases; no assets needed.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Build/DTHRetarget.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHPoseUnitRetarget, "DTH.Pose.Unit.RetargetInverse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTHPoseUnitRetarget::RunTest(const FString&)
{
	FRandomStream Random(9);
	auto RandomTransform = [&Random]()
	{
		return FTransform(FQuat(Random.GetUnitVector(), Random.FRandRange(-PI, PI)),
			Random.GetUnitVector() * Random.FRandRange(1.0, 60.0), FVector::OneVector);
	};
	for (const EBoneTranslationRetargetingMode::Type Mode : {EBoneTranslationRetargetingMode::Animation,
		EBoneTranslationRetargetingMode::AnimationScaled, EBoneTranslationRetargetingMode::AnimationRelative,
		EBoneTranslationRetargetingMode::Skeleton})
	{
		for (int32 Case = 0; Case < 50; ++Case)
		{
			DTHRetarget::FBone Bone;
			Bone.Mode = Mode;
			Bone.SkeletonRef = RandomTransform();
			Bone.MeshRef = RandomTransform();
			FTransform Wanted = RandomTransform();
			if (Mode == EBoneTranslationRetargetingMode::Skeleton)
			{
				Wanted.SetTranslation(Bone.MeshRef.GetTranslation());
			}
			const FTransform Played = DTHRetarget::Forward(Bone, DTHRetarget::Inverse(Bone, Wanted));
			TestTrue(FString::Printf(TEXT("mode %d case %d rotation"), int32(Mode), Case),
				Played.GetRotation().Equals(Wanted.GetRotation(), 1e-5) || Played.GetRotation().Equals(-Wanted.GetRotation(), 1e-5));
			TestTrue(FString::Printf(TEXT("mode %d case %d translation"), int32(Mode), Case),
				Played.GetTranslation().Equals(Wanted.GetTranslation(), 1e-4));
		}
	}
	// Animation Relative on identical references is the identity.
	DTHRetarget::FBone Same;
	Same.Mode = EBoneTranslationRetargetingMode::AnimationRelative;
	Same.SkeletonRef = Same.MeshRef = RandomTransform();
	const FTransform T = RandomTransform();
	TestTrue(TEXT("identical references"), DTHRetarget::Forward(Same, T).Equals(T, 1e-5));
	return true;
}

#endif
