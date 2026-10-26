// Phase 0 measurements (docs/pose-wizard-plan.md section 7). Each writes a report under Saved/DTHPoseWizard/phase0
// and puts its headline numbers in the test log. Inputs come from UDTHPoseSettings.

#include "Tests/DTHPoseTestUtil.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Build/DTHPoseWriter.h"
#include "Build/DTHRetarget.h"
#include "Engine/SkeletalMesh.h"
#include "Probe/DTHBodyHarness.h"

namespace
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	const TCHAR* ModeName(EBoneTranslationRetargetingMode::Type Mode)
	{
		switch (Mode)
		{
		case EBoneTranslationRetargetingMode::Animation: return TEXT("Animation");
		case EBoneTranslationRetargetingMode::Skeleton: return TEXT("Skeleton");
		case EBoneTranslationRetargetingMode::AnimationScaled: return TEXT("AnimationScaled");
		case EBoneTranslationRetargetingMode::AnimationRelative: return TEXT("AnimationRelative");
		case EBoneTranslationRetargetingMode::OrientAndScale: return TEXT("OrientAndScale");
		default: return TEXT("Other");
		}
	}

	FString Vec(const FVector& V) { return FString::Printf(TEXT("%.4f,%.4f,%.4f"), V.X, V.Y, V.Z); }
	FString Quat(const FQuat& Q) { return FString::Printf(TEXT("%.6f,%.6f,%.6f,%.6f"), Q.X, Q.Y, Q.Z, Q.W); }

	/** Largest rotation (deg) and translation (cm) difference between two read-backs, and the bones where they are. */
	struct FDiff
	{
		double Deg = 0.0, Cm = 0.0;
		FName DegBone, CmBone;
	};

	FDiff Compare(const TArray<FName>& Bones, const TArray<FTransform>& A, const TArray<FTransform>& B)
	{
		FDiff D;
		for (int32 I = 0; I < FMath::Min(A.Num(), B.Num()); ++I)
		{
			const double Deg = DTHPoseTest::AngleDeg(A[I].GetRotation(), B[I].GetRotation());
			const double Cm = FVector::Dist(A[I].GetTranslation(), B[I].GetTranslation());
			if (Deg > D.Deg) { D.Deg = Deg; D.DegBone = Bones[I]; }
			if (Cm > D.Cm) { D.Cm = Cm; D.CmBone = Bones[I]; }
		}
		return D;
	}
}

// The skeleton's and the mesh's reference poses and the translation retargeting modes DTH sets
// [imp apply_skeleton_retargeting_modes], [QS 102-104].
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHPoseP0DescribeSkeletons, "DTH.Pose.P0.DescribeSkeletons", Flags)
bool FDTHPoseP0DescribeSkeletons::RunTest(const FString&)
{
	FDTHPoseTestContext C;
	if (!C.Load(*this)) return false;
	const FReferenceSkeleton& MeshRef = C.Mesh->GetRefSkeleton();
	const FReferenceSkeleton& SkelRef = C.Skeleton->GetReferenceSkeleton();
	const TArray<DTHRetarget::FBone> Bones = DTHRetarget::Gather(*C.Skeleton, *C.Mesh);
	TArray<FString> Lines{TEXT("index\tbone\tparent\tmode\tin_skeleton\tskel_t\tskel_q\tmesh_t\tmesh_q\tref_rot_diff_deg\tref_t_diff_cm")};
	TMap<FString, int32> Modes;
	int32 Missing = 0;
	for (int32 I = 0; I < MeshRef.GetNum(); ++I)
	{
		const FName Name = MeshRef.GetBoneName(I);
		const int32 Parent = MeshRef.GetParentIndex(I);
		const bool bInSkeleton = SkelRef.FindBoneIndex(Name) != INDEX_NONE;
		Missing += bInSkeleton ? 0 : 1;
		const DTHRetarget::FBone& B = Bones[I];
		Modes.FindOrAdd(ModeName(B.Mode))++;
		Lines.Add(FString::Printf(TEXT("%d\t%s\t%s\t%s\t%d\t%s\t%s\t%s\t%s\t%.4f\t%.4f"), I, *Name.ToString(),
			Parent == INDEX_NONE ? TEXT("-") : *MeshRef.GetBoneName(Parent).ToString(), ModeName(B.Mode), bInSkeleton ? 1 : 0,
			*Vec(B.SkeletonRef.GetTranslation()), *Quat(B.SkeletonRef.GetRotation()), *Vec(B.MeshRef.GetTranslation()),
			*Quat(B.MeshRef.GetRotation()), DTHPoseTest::AngleDeg(B.SkeletonRef.GetRotation(), B.MeshRef.GetRotation()),
			FVector::Dist(B.SkeletonRef.GetTranslation(), B.MeshRef.GetTranslation())));
	}
	const FString File = DTHPoseTest::WriteReport(TEXT("skeletons.tsv"), Lines);
	FString Summary;
	for (const TPair<FString, int32>& M : Modes) Summary += FString::Printf(TEXT(" %s=%d"), *M.Key, M.Value);
	AddInfo(FString::Printf(TEXT("Mesh %d bones, skeleton %d bones, %d mesh bones not on the skeleton. Modes:%s. Report: %s"),
		MeshRef.GetNum(), SkelRef.GetNum(), Missing, *Summary, *File));
	return true;
}

// Imports the export's JCM and RET pose-asset FBX files under the test folder, as the DTH importer imports them
// [imp import_pose_asset_animation].
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHPoseP0ImportGroundTruth, "DTH.Pose.P0.ImportGroundTruth", Flags)
bool FDTHPoseP0ImportGroundTruth::RunTest(const FString&)
{
	FDTHPoseTestContext C;
	if (!C.Load(*this)) return false;
	for (const TCHAR* Kind : {TEXT("Jcm"), TEXT("Ret")})
	{
		TArray<FString> Names;
		const UAnimSequence* Sequence = C.GroundTruth(*this, Kind, Names);
		if (!Sequence) return false;
		TArray<FName> Tracks;
		Sequence->GetDataModel()->GetBoneTrackNames(Tracks);
		AddInfo(FString::Printf(TEXT("%s: %d keys, %d pose names, %d bone tracks, %d curves."), *Sequence->GetPathName(),
			Sequence->GetDataModel()->GetNumberOfKeys(), Names.Num(), Tracks.Num(), Sequence->GetDataModel()->GetNumberOfFloatCurves()));
	}
	return true;
}

// Plan section 3: is the mesh's bind pose the RET pose asset's UnrealPose? Compares every bone track of each RET
// frame with the mesh's reference pose. Pass: UnrealPose within 0.1 deg and 0.01 cm on every bone.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHPoseP0BindPose, "DTH.Pose.P0.BindPoseIsUnrealPose", Flags)
bool FDTHPoseP0BindPose::RunTest(const FString&)
{
	FDTHPoseTestContext C;
	if (!C.Load(*this)) return false;
	TArray<FString> Names;
	const UAnimSequence* Ret = C.GroundTruth(*this, TEXT("Ret"), Names);
	if (!Ret) return false;
	const FReferenceSkeleton& MeshRef = C.Mesh->GetRefSkeleton();
	TArray<FString> Lines{TEXT("frame\tpose\tbone\trot_diff_deg\tt_diff_cm")};
	const int32 Keys = Ret->GetDataModel()->GetNumberOfKeys();
	for (int32 K = 0; K < Keys; ++K)
	{
		const TMap<FName, FTransform> Pose = DTHPoseTest::TrackPose(*Ret, K);
		const FString PoseName = Names.IsValidIndex(K) ? Names[K] : TEXT("?");
		double MaxDeg = 0.0, MaxCm = 0.0;
		FName DegBone, CmBone;
		for (int32 I = 0; I < MeshRef.GetNum(); ++I)
		{
			const FTransform* T = Pose.Find(MeshRef.GetBoneName(I));
			if (!T) continue;
			const FTransform& R = MeshRef.GetRefBonePose()[I];
			const double Deg = DTHPoseTest::AngleDeg(T->GetRotation(), R.GetRotation());
			const double Cm = FVector::Dist(T->GetTranslation(), R.GetTranslation());
			if (Deg > 1e-3 || Cm > 1e-3)
			{
				Lines.Add(FString::Printf(TEXT("%d\t%s\t%s\t%.4f\t%.4f"), K, *PoseName, *MeshRef.GetBoneName(I).ToString(), Deg, Cm));
			}
			if (Deg > MaxDeg) { MaxDeg = Deg; DegBone = MeshRef.GetBoneName(I); }
			if (Cm > MaxCm) { MaxCm = Cm; CmBone = MeshRef.GetBoneName(I); }
		}
		AddInfo(FString::Printf(TEXT("Frame %d %s vs bind pose: max %.4f deg (%s), max %.4f cm (%s)."), K, *PoseName,
			MaxDeg, *DegBone.ToString(), MaxCm, *CmBone.ToString()));
		if (PoseName == TEXT("pasUnrealPose") && (MaxDeg > 0.1 || MaxCm > 0.01))
		{
			AddError(TEXT("The bind pose is not the RET pose asset's UnrealPose."));
		}
	}
	AddInfo(FString::Printf(TEXT("Report: %s"), *DTHPoseTest::WriteReport(TEXT("bindpose.tsv"), Lines)));
	return true;
}

// Plays every JCM pose-asset frame through the character's post-process blueprint. Per frame: which bones the pose
// rotates (against the rest frame), whether the post-process blueprint outputs the JCM curves the pose asset carries for
// that frame (its pose drivers [M u1_results] reading the bones alone), and which bones the post-process blueprint
// itself changes (twist solve [M u1_rig_graph ~1053-1066], and any bone delta the JCM pose asset might add: plan
// risk 6). Pass: every curve the pose asset turns on for a pose reaches 0.5 when its bones
// are played (pose drivers interpolate, so exact values are reported, not gated).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHPoseP0JcmFrames, "DTH.Pose.P0.JcmFrames", Flags)
bool FDTHPoseP0JcmFrames::RunTest(const FString&)
{
	FDTHPoseTestContext C;
	if (!C.Load(*this)) return false;
	TArray<FString> Names;
	const UAnimSequence* Jcm = C.GroundTruth(*this, TEXT("Jcm"), Names);
	if (!Jcm) return false;
	FDTHBodyHarness Harness;
	FString Error;
	if (!Harness.Init(C.Mesh, Error)) { AddError(Error); return false; }

	const IAnimationDataModel* Model = Jcm->GetDataModel();
	const TArray<FFloatCurve>& SequenceCurves = Model->GetFloatCurves();
	TSet<FName> SequenceCurveNames;
	for (const FFloatCurve& Curve : SequenceCurves) SequenceCurveNames.Add(Curve.GetName());
	const int32 Keys = Model->GetNumberOfKeys();
	const TMap<FName, FTransform> RestPose = DTHPoseTest::TrackPose(*Jcm, 0);
	FDTHBoneReadback Rest;
	Harness.EvalPose(RestPose, {}, Rest);

	TArray<FString> Lines{TEXT("frame\tpose\trotated_bones\tsequence_curves_on\tmax_curve_error\tworst_curve\tother_curves_changed\tpost_changed_bones")};
	int32 Failed = 0;
	double WorstError = 0.0;
	for (int32 K = 1; K < Keys; ++K)
	{
		const FString PoseName = Names.IsValidIndex(K) ? Names[K] : FString::Printf(TEXT("frame%d"), K);
		const TMap<FName, FTransform> Pose = DTHPoseTest::TrackPose(*Jcm, K);
		FString Rotated;
		for (const TPair<FName, FTransform>& B : Pose)
		{
			const FTransform* R = RestPose.Find(B.Key);
			const double Deg = R ? DTHPoseTest::AngleDeg(B.Value.GetRotation(), R->GetRotation()) : 0.0;
			if (Deg > 0.5)
			{
				Rotated += FString::Printf(TEXT("%s:%.1f "), *B.Key.ToString(), Deg);
			}
		}
		FDTHBoneReadback Out;
		Harness.EvalPose(Pose, {}, Out);

		// The JCM curves the pose asset carries at this frame (what DTH authored for this pose) against what the
		// post-process blueprint outputs when the bones alone are played.
		const float Time = float(Model->GetFrameRate().AsSeconds(FFrameNumber(K)));
		FString On;
		double MaxError = 0.0;
		FName Worst;
		FName Weak;
		float WeakValue = 1.0f;
		for (const FFloatCurve& Curve : SequenceCurves)
		{
			const float Expected = Curve.FloatCurve.Eval(Time);
			const float Got = Out.Curves.FindRef(Curve.GetName());
			if (FMath::Abs(Expected) > 0.01f)
			{
				On += FString::Printf(TEXT("%s:%.2f->%.2f "), *Curve.GetName().ToString(), Expected, Got);
				if (Expected > 0.5f && Got < WeakValue) { WeakValue = Got; Weak = Curve.GetName(); }
			}
			if (FMath::Abs(Got - Expected) > MaxError)
			{
				MaxError = FMath::Abs(Got - Expected);
				Worst = Curve.GetName();
			}
		}
		WorstError = FMath::Max(WorstError, MaxError);
		FString Others;
		int32 OtherCount = 0;
		for (const TPair<FName, float>& V : Out.Curves)
		{
			if (!SequenceCurveNames.Contains(V.Key) && FMath::Abs(V.Value - Rest.Curves.FindRef(V.Key)) > 0.01f && ++OtherCount <= 8)
			{
				Others += FString::Printf(TEXT("%s:%.2f "), *V.Key.ToString(), V.Value);
			}
		}
		if (OtherCount > 8) Others += FString::Printf(TEXT("(+%d more)"), OtherCount - 8);
		// Bones the post-process blueprint moved away from what was played.
		FString Changed;
		for (int32 I = 0; I < Out.Bones.Num(); ++I)
		{
			const FTransform* Played = Pose.Find(Out.Bones[I]);
			const FTransform Expected = Played ? *Played : C.Mesh->GetRefSkeleton().GetRefBonePose()[I];
			const double Deg = DTHPoseTest::AngleDeg(Out.Local[I].GetRotation(), Expected.GetRotation());
			const double Cm = FVector::Dist(Out.Local[I].GetTranslation(), Expected.GetTranslation());
			if (Deg > 0.05 || Cm > 0.01)
			{
				Changed += FString::Printf(TEXT("%s:%.2fdeg/%.3fcm "), *Out.Bones[I].ToString(), Deg, Cm);
			}
		}
		Lines.Add(FString::Printf(TEXT("%d\t%s\t%s\t%s\t%.4f\t%s\t%s\t%s"), K, *PoseName, *Rotated.TrimEnd(), *On.TrimEnd(), MaxError,
			*Worst.ToString(), *Others.TrimEnd(), *Changed.TrimEnd()));
		// DTH's own gap, measured and recorded (p0_daz_results.md): Daz fires body_cbs_upperarm_z40n at upper arm 40 deg
		// down, DTH's post-process blueprint leaves jcmUpperArmBD40 at 0. Reported, not failed; any other pose fails.
		const bool bKnownDTHGap = PoseName.StartsWith(TEXT("pasJcmUpperArmBD40_"));
		if (WeakValue < 0.5f)
		{
			Failed += bKnownDTHGap ? 0 : 1;
			AddWarning(FString::Printf(TEXT("%s: its JCM curve %s reaches only %.3f when the bones are played%s."), *PoseName, *Weak.ToString(), WeakValue,
				bKnownDTHGap ? TEXT(" (DTH's known gap)") : TEXT("")));
		}
	}
	FString AtRest;
	for (const FFloatCurve& Curve : SequenceCurves)
	{
		const float V = Rest.Curves.FindRef(Curve.GetName());
		if (FMath::Abs(V) > 0.01f) AtRest += FString::Printf(TEXT("%s:%.3f "), *Curve.GetName().ToString(), V);
	}
	AddInfo(FString::Printf(TEXT("JCM curves on at the rest frame: %s"), AtRest.IsEmpty() ? TEXT("none") : *AtRest));
	AddInfo(FString::Printf(TEXT("%d sequence curves; %d of %d JCM poses fire their own curves to 0.5 or more (largest curve difference from the pose asset %.4f). Report: %s"),
		SequenceCurves.Num(), Keys - 1 - Failed, Keys - 1, WorstError, *DTHPoseTest::WriteReport(TEXT("jcm_frames.tsv"), Lines)));
	if (Failed > 0)
	{
		AddError(FString::Printf(TEXT("%d JCM poses do not fire their own curves."), Failed));
	}
	return true;
}

// Plan section 3 (translation retargeting): a sequence authored on the skeleton is retargeted onto the mesh when it
// plays. Writes chosen JCM poses (plus a pelvis drop, for Animation Scaled) through DTHRetarget::Inverse, plays the
// sequence, and compares with playing the wanted pose directly. Also measures how far off the pose would be without
// the inverse. Pass: within 0.01 deg and 0.01 cm on every bone.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHPoseP0RetargetRoundTrip, "DTH.Pose.P0.RetargetRoundTrip", Flags)
bool FDTHPoseP0RetargetRoundTrip::RunTest(const FString&)
{
	FDTHPoseTestContext C;
	if (!C.Load(*this)) return false;
	TArray<FString> Names;
	const UAnimSequence* Jcm = C.GroundTruth(*this, TEXT("Jcm"), Names);
	if (!Jcm) return false;
	FDTHBodyHarness Harness;
	FString Error;
	if (!Harness.Init(C.Mesh, Error)) { AddError(Error); return false; }

	const FReferenceSkeleton& MeshRef = C.Mesh->GetRefSkeleton();
	const TArray<DTHRetarget::FBone> Retarget = DTHRetarget::Gather(*C.Skeleton, *C.Mesh);
	const FString Folder = C.TestFolder / TEXT("RoundTrip");
	const FName Pelvis(TEXT("pelvis"));
	TArray<FString> Lines{TEXT("pose\tvariant\tmax_deg\tdeg_bone\tmax_cm\tcm_bone")};
	for (const TCHAR* PoseName : {TEXT("pasJcmRestPose"), TEXT("pasJcmThighBF90_l"), TEXT("pasJcmUpperArmBF110BU90_r"), TEXT("pasJcmSpine03BF40_l"), TEXT("pasJcmLowerArmBF135_l")})
	{
		const int32 K = Names.IndexOfByKey(FString(PoseName));
		if (K == INDEX_NONE)
		{
			AddInfo(FString::Printf(TEXT("%s is not in this export's JCM poses; skipped."), PoseName));
			continue;
		}
		TMap<FName, FTransform> Wanted = DTHPoseTest::TrackPose(*Jcm, K);
		if (FTransform* P = Wanted.Find(Pelvis))
		{
			P->AddToTranslation(FVector(0.0, 0.0, -20.0));
		}
		FDTHBoneReadback Direct;
		Harness.EvalPose(Wanted, {}, Direct);

		for (const bool bInverse : {true, false})
		{
			TMap<FName, FTransform> Track;
			for (int32 I = 0; I < MeshRef.GetNum(); ++I)
			{
				const FName Bone = MeshRef.GetBoneName(I);
				const FTransform* W = Wanted.Find(Bone);
				const FTransform Value = W ? *W : MeshRef.GetRefBonePose()[I];
				Track.Add(Bone, bInverse ? DTHRetarget::Inverse(Retarget[I], Value) : Value);
			}
			const FString Name = FString::Printf(TEXT("A_RoundTrip_%s_%s"), PoseName, bInverse ? TEXT("Inverse") : TEXT("Raw"));
			DTHPoseTest::DeleteIfExists(Folder / Name);
			UAnimSequence* Sequence = DTHPoseWriter::Create(Folder, Name, C.Skeleton, {Track}, Error);
			if (!Sequence) { AddError(Error); return false; }
			FDTHBoneReadback Played;
			Harness.EvalSequence(Sequence, 0.0, Played);
			const FDiff D = Compare(Direct.Bones, Direct.Component, Played.Component);
			Lines.Add(FString::Printf(TEXT("%s\t%s\t%.5f\t%s\t%.5f\t%s"), PoseName, bInverse ? TEXT("inverse") : TEXT("raw"),
				D.Deg, *D.DegBone.ToString(), D.Cm, *D.CmBone.ToString()));
			AddInfo(FString::Printf(TEXT("%s, %s track: max %.5f deg (%s), max %.5f cm (%s)."), PoseName,
				bInverse ? TEXT("inverse-retargeted") : TEXT("raw"), D.Deg, *D.DegBone.ToString(), D.Cm, *D.CmBone.ToString()));
			if (bInverse && (D.Deg > 0.01 || D.Cm > 0.01))
			{
				AddError(FString::Printf(TEXT("%s: the played sequence does not reproduce the wanted pose."), PoseName));
			}
		}
	}
	AddInfo(FString::Printf(TEXT("Report: %s"), *DTHPoseTest::WriteReport(TEXT("retarget_roundtrip.tsv"), Lines)));
	return true;
}

#endif
