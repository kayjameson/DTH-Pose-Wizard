// Phase 0 measurements that need the Daz samples (Settings > Daz Samples File): the axis conversion, the bone
// mapping, and the closed-form solve checked against DTH's JCM pose asset (plan section 7).

#include "Tests/DTHPoseTestUtil.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "Build/DTHPoseSolve.h"
#include "Build/DTHPoseWriter.h"
#include "Build/DTHRetarget.h"
#include "Misc/Paths.h"
#include "Probe/DTHBodyHarness.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Library/DTHPoseSamples.h"
#include "Settings/DTHPoseSettings.h"

namespace
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
	constexpr double MapMaxCm = 0.5;

	bool LoadSamples(FAutomationTestBase& Test, FDTHPoseSamples& Out)
	{
		FString Error;
		if (!Out.Load(GetDefault<UDTHPoseSettings>()->SamplesFile.FilePath, Error))
		{
			Test.AddError(Error + TEXT(" Set Daz Samples File to the sampler's DTH_PoseSamples.json."));
			return false;
		}
		if (Out.RomFrames.Num() < 2)
		{
			Test.AddError(TEXT("The samples have no ROM frames (frame 1 is UnrealPose, the rest pose)."));
			return false;
		}
		return true;
	}

	/**
	 * The rig on the character: UE bind pose, Daz UnrealPose (ROM frame 1) rotations from the library samples, and the
	 * axes and bone mapping from the character's own joints when Character Joints File is set.
	 */
	DTHPoseSolve::FRig MakeRig(FAutomationTestBase& Test, const USkeletalMesh& Mesh, const FDTHPoseSamples& Samples)
	{
		const FReferenceSkeleton& Ref = Mesh.GetRefSkeleton();
		DTHPoseSolve::FRig Rig;
		Rig.DazRest = Samples.RomFrames[1];
		Rig.MeshRest = DTHPoseSolve::ComponentSpace(Ref, [&Ref](int32 I) { return Ref.GetRefBonePose()[I]; });
		FDTHPoseSamples Joints;
		FString Error;
		const FString JointsFile = GetDefault<UDTHPoseSettings>()->CharacterJointsFile.FilePath;
		const bool bOwn = !JointsFile.IsEmpty() && Joints.Load(JointsFile, Error) && Joints.RomFrames.Num() > 1;
		const FDTHDazState& At = bOwn ? Joints.RomFrames[1] : Rig.DazRest;
		Test.AddInfo(bOwn ? TEXT("Joints: the character's own (Character Joints File).") : TEXT("Joints: the library's base Genesis 9."));
		Rig.Axes = DTHPoseSolve::FitAxes(At, Ref, Rig.MeshRest);
		Rig.Map = DTHPoseSolve::MapBones(Rig.Axes, At, Ref, Rig.MeshRest, MapMaxCm);
		// The library figure on the character: its proportions differ, so it is matched loosely (3 cm) and scaled.
		Rig.LibraryAxes = DTHPoseSolve::FitAxes(Rig.DazRest, Ref, Rig.MeshRest, 3.0);
		Test.AddInfo(FString::Printf(TEXT("Library on the character: %s"), *Rig.LibraryAxes.Describe()));
		return Rig;
	}

	bool IsTwist(FName Bone) { return Bone.ToString().Contains(TEXT("twist")); }
}

// Plan section 4.5: the axis conversion and the bone mapping, measured on UnrealPose (ROM frame 1) against the bind
// pose. Pass: the axes put the joints within 0.1 cm (median) and every limb, spine, neck, head, hand and finger bone
// maps.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHPoseP0DazMapping, "DTH.Pose.P0.DazMapping", Flags)
bool FDTHPoseP0DazMapping::RunTest(const FString&)
{
	FDTHPoseTestContext C;
	if (!C.Load(*this)) return false;
	FDTHPoseSamples Samples;
	if (!LoadSamples(*this, Samples)) return false;
	const DTHPoseSolve::FRig Rig = MakeRig(*this, *C.Mesh, Samples);
	const FReferenceSkeleton& Ref = C.Mesh->GetRefSkeleton();

	TArray<FString> Lines{TEXT("index\tue_bone\tdaz_bone\tdistance_cm\tnext_daz\tnext_cm")};
	TMap<FName, TArray<FName>> ByDaz;
	int32 Mapped = 0, Unmapped = 0;
	FString UnmappedNames;
	for (int32 I = 0; I < Ref.GetNum(); ++I)
	{
		const FName Bone = Ref.GetBoneName(I);
		if (DTHPoseSolve::IsSkipped(Bone)) continue;
		const FName Daz = Rig.Map.Daz[I];
		Lines.Add(FString::Printf(TEXT("%d\t%s\t%s\t%.4f\t%s\t%.4f"), I, *Bone.ToString(), Daz.IsNone() ? TEXT("-") : *Daz.ToString(),
			Rig.Map.DistanceCm[I], *Rig.Map.SecondDaz[I].ToString(), Rig.Map.SecondCm[I]));
		if (Daz.IsNone()) { ++Unmapped; UnmappedNames += Bone.ToString() + TEXT(" "); }
		else { ++Mapped; ByDaz.FindOrAdd(Daz).Add(Bone); }
	}
	FString Shared;
	for (const TPair<FName, TArray<FName>>& D : ByDaz)
	{
		if (D.Value.Num() > 1)
		{
			Shared += D.Key.ToString() + TEXT("<-");
			for (const FName& B : D.Value) Shared += B.ToString() + TEXT(",");
			Shared += TEXT(" ");
		}
	}
	AddInfo(FString::Printf(TEXT("Axes: %s"), *Rig.Axes.Describe()));
	AddInfo(FString::Printf(TEXT("%d UE bones mapped within %.2f cm, %d not: %s"), Mapped, MapMaxCm, Unmapped, *UnmappedNames));
	AddInfo(FString::Printf(TEXT("Daz bones shared by several UE bones: %s"), Shared.IsEmpty() ? TEXT("none") : *Shared));
	AddInfo(FString::Printf(TEXT("Report: %s"), *DTHPoseTest::WriteReport(TEXT("daz_mapping.tsv"), Lines)));
	if (Rig.Axes.MedianCm > 0.1)
	{
		AddError(FString::Printf(TEXT("No axis conversion puts the Daz joints on the UE joints (median %.3f cm)."), Rig.Axes.MedianCm));
	}
	for (const TCHAR* Must : {TEXT("pelvis"), TEXT("spine_01"), TEXT("neck_01"), TEXT("head"), TEXT("thigh_l"), TEXT("calf_l"),
		TEXT("foot_l"), TEXT("ball_l"), TEXT("clavicle_l"), TEXT("upperarm_l"), TEXT("lowerarm_l"), TEXT("hand_l"), TEXT("index_01_l"),
		TEXT("thumb_01_l")})
	{
		const int32 I = Ref.FindBoneIndex(Must);
		if (I == INDEX_NONE || Rig.Map.Daz[I].IsNone())
		{
			AddError(FString::Printf(TEXT("%s has no Daz bone at its joint."), Must));
		}
	}
	return true;
}

// Plan section 7, Phase 0 pass rule: the closed-form solve of each DTH ROM frame against the JCM pose asset DTH built
// from the same ROM (JCM frame k is ROM frame k + offset; the offset is measured). Compares component-space
// rotations of every mapped bone. Pass: < 0.5 deg per joint. Twist bones are reported separately (the post-process
// blueprint solves them from the hand [M u1_rig_graph ~1053-1066]).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHPoseP0JcmSolve, "DTH.Pose.P0.JcmSolve", Flags)
bool FDTHPoseP0JcmSolve::RunTest(const FString&)
{
	FDTHPoseTestContext C;
	if (!C.Load(*this)) return false;
	FDTHPoseSamples Samples;
	if (!LoadSamples(*this, Samples)) return false;
	TArray<FString> Names;
	const UAnimSequence* Jcm = C.GroundTruth(*this, TEXT("Jcm"), Names);
	if (!Jcm) return false;
	const DTHPoseSolve::FRig Rig = MakeRig(*this, *C.Mesh, Samples);
	const FReferenceSkeleton& Ref = C.Mesh->GetRefSkeleton();
	const int32 Keys = Jcm->GetDataModel()->GetNumberOfKeys();

	struct FFrameError { double Deg = 0.0, TwistDeg = 0.0; FName Bone, TwistBone; };
	auto Compare = [&](int32 JcmKey, int32 RomFrame)
	{
		FFrameError E;
		const TMap<FName, FTransform> Track = DTHPoseTest::TrackPose(*Jcm, JcmKey);
		const TArray<FTransform> Truth = DTHPoseSolve::ComponentSpace(Ref, [&](int32 I)
		{
			const FTransform* T = Track.Find(Ref.GetBoneName(I));
			return T ? *T : Ref.GetRefBonePose()[I];
		});
		TArray<FTransform> Solved;
		DTHPoseSolve::Solve(Rig, Ref, Samples.RomFrames[RomFrame], &Solved);
		for (int32 I = 0; I < Ref.GetNum(); ++I)
		{
			const FName Bone = Ref.GetBoneName(I);
			if (DTHPoseSolve::IsSkipped(Bone)) continue;
			const double Deg = DTHPoseTest::AngleDeg(Solved[I].GetRotation(), Truth[I].GetRotation());
			if (IsTwist(Bone)) { if (Deg > E.TwistDeg) { E.TwistDeg = Deg; E.TwistBone = Bone; } }
			else if (Deg > E.Deg) { E.Deg = Deg; E.Bone = Bone; }
		}
		return E;
	};

	// The ROM frame that JCM key 1 comes from.
	int32 Offset = INDEX_NONE;
	double BestOffsetError = TNumericLimits<double>::Max();
	for (int32 O = 0; O <= 3; ++O)
	{
		double Sum = 0.0;
		for (int32 K = 1; K <= 6 && K + O < Samples.RomFrames.Num(); ++K) Sum += Compare(K, K + O).Deg;
		if (Sum < BestOffsetError) { BestOffsetError = Sum; Offset = O; }
	}
	AddInfo(FString::Printf(TEXT("Axes: %s. JCM key k is ROM frame k + %d."), *Rig.Axes.Describe(), Offset));

	TArray<FString> Lines{TEXT("jcm_key\tpose\trom_frame\tmax_deg\tbone\tmax_twist_deg\ttwist_bone")};
	int32 Failed = 0;
	double Worst = 0.0;
	FName WorstPose;
	for (int32 K = 1; K < Keys && K + Offset < Samples.RomFrames.Num(); ++K)
	{
		const FFrameError E = Compare(K, K + Offset);
		const FString Pose = Names.IsValidIndex(K) ? Names[K] : FString();
		Lines.Add(FString::Printf(TEXT("%d\t%s\t%d\t%.4f\t%s\t%.4f\t%s"), K, *Pose, K + Offset, E.Deg, *E.Bone.ToString(), E.TwistDeg,
			*E.TwistBone.ToString()));
		if (E.Deg > Worst) { Worst = E.Deg; WorstPose = FName(*Pose); }
		if (E.Deg >= 0.5)
		{
			++Failed;
			AddWarning(FString::Printf(TEXT("%s: %.3f deg at %s."), *Pose, E.Deg, *E.Bone.ToString()));
		}
	}
	const FFrameError Rest = Compare(0, 0);
	AddInfo(FString::Printf(TEXT("JCM rest key vs ROM frame 0 (Daz zero pose): %.4f deg at %s."), Rest.Deg, *Rest.Bone.ToString()));
	AddInfo(FString::Printf(TEXT("%d of %d JCM poses within 0.5 deg on every mapped non-twist bone; worst %.4f deg (%s). Report: %s"),
		Keys - 1 - Failed, Keys - 1, Worst, *WorstPose.ToString(), *DTHPoseTest::WriteReport(TEXT("jcm_solve.tsv"), Lines)));
	if (Failed > 0)
	{
		AddError(FString::Printf(TEXT("%d JCM poses are not recreated within 0.5 deg."), Failed));
	}
	return true;
}

// Plan section 8, the bone check, on the Phase 0 presets (a seated pose, a 90 deg forearm twist, a hand pose): the
// product path end to end. Solve from the library (base Genesis 9), write the inverse-retargeted bone-track sequence,
// play it on the character through its post-process blueprint, and compare every mapped joint with Daz's own result on
// the character (each UE joint carried as a point by the Daz bone its parent maps to) (Character Joints File holds the same presets sampled on the character). Twist bones and the pelvis
// are reported on their own: the post-process blueprint solves twist from the hand [M u1_rig_graph ~1053-1066], and the
// pelvis translation goes through Animation Scaled [imp apply_skeleton_retargeting_modes]. Pass: every joint within
// 0.5 cm and every non-twist bone within 0.5 deg. The position budget is measured (p0_daz_results.md): the hip translation
// follows the library figure's scale (Vi's figure scale 0.99333 is not in the export: 0.2 cm on a 34 cm seat drop), and
// DTH places a few joints off the Daz joints (Vi's thighs by 0.47 cm), which every animation on DTH's skeleton pivots on.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHPoseP0PresetPoses, "DTH.Pose.P0.PresetPoses", Flags)
bool FDTHPoseP0PresetPoses::RunTest(const FString&)
{
	FDTHPoseTestContext C;
	if (!C.Load(*this)) return false;
	FDTHPoseSamples Samples, Own;
	if (!LoadSamples(*this, Samples)) return false;
	FString Error;
	if (!Own.Load(GetDefault<UDTHPoseSettings>()->CharacterJointsFile.FilePath, Error) || Own.RomFrames.Num() < 2)
	{
		AddError(TEXT("Set Character Joints File to the presets sampled on the character itself."));
		return false;
	}
	const DTHPoseSolve::FRig Rig = MakeRig(*this, *C.Mesh, Samples);
	const FReferenceSkeleton& Ref = C.Mesh->GetRefSkeleton();
	const TArray<DTHRetarget::FBone> Retarget = DTHRetarget::Gather(*C.Skeleton, *C.Mesh);
	FDTHBodyHarness Harness;
	if (!Harness.Init(C.Mesh, Error)) { AddError(Error); return false; }
	const int32 PelvisIndex = Ref.FindBoneIndex(TEXT("pelvis"));

	TArray<FString> Lines{TEXT("pose\tbone\tdaz_bone\tpos_err_cm\trot_err_deg\tlibrary_vs_own_deg")};
	for (const FDTHDazPose& Pose : Samples.Poses)
	{
		const FDTHDazPose* Mine = Own.Poses.FindByPredicate([&Pose](const FDTHDazPose& P) { return P.Label == Pose.Label; });
		if (!Pose.bLoaded || !Mine || !Mine->bLoaded)
		{
			AddWarning(FString::Printf(TEXT("%s: not sampled on both figures."), *Pose.Label));
			continue;
		}
		// The product path: solve, inverse-retarget, write, play.
		const TMap<FName, FTransform> Wanted = DTHPoseSolve::Solve(Rig, Ref, Pose.State);
		TMap<FName, FTransform> Track;
		for (int32 I = 0; I < Ref.GetNum(); ++I)
		{
			Track.Add(Ref.GetBoneName(I), DTHRetarget::Inverse(Retarget[I], Wanted.FindChecked(Ref.GetBoneName(I))));
		}
		const FString Name = TEXT("A_Preset_") + FPaths::MakeValidFileName(Pose.Label).Replace(TEXT(" "), TEXT("_"));
		const FString Folder = C.TestFolder / TEXT("Presets");
		DTHPoseTest::DeleteIfExists(Folder / Name);
		UAnimSequence* Sequence = DTHPoseWriter::Create(Folder, Name, C.Skeleton, {Track}, Error);
		if (!Sequence) { AddError(Error); return false; }
		FDTHBoneReadback Played;
		Harness.EvalSequence(Sequence, 0.0, Played);

		// Daz on the character, in UE component space. Rotations as world deltas from UnrealPose onto the bind pose.
		double MaxCm = 0.0, MaxDeg = 0.0, MaxTwistDeg = 0.0, MaxLibDeg = 0.0;
		FName CmBone, DegBone, TwistBone, LibBone;
		for (int32 I = 0; I < Ref.GetNum(); ++I)
		{
			const FName Bone = Ref.GetBoneName(I);
			const FName DazName = Rig.Map.Daz[I];
			if (DTHPoseSolve::IsSkipped(Bone) || DazName.IsNone()) continue;
			// Where Daz puts this UE joint: a point fixed to the frame of the Daz bone that the mapped Daz bone hangs from,
			// pivoting at that Daz joint on the character (DTH places a few joints off the Daz joints, so the joint is
			// carried, not compared with a Daz joint). The root Daz bone carries itself.
			const FDTHDazBone* OwnRest = Own.RomFrames[1].Bones.Find(DazName);
			const FName Carrier = OwnRest && !OwnRest->Parent.IsNone() ? OwnRest->Parent : DazName;
			const FDTHDazBone* CarrierNow = Mine->State.Bones.Find(Carrier);
			const FDTHDazBone* CarrierRest = Own.RomFrames[1].Bones.Find(Carrier);
			const FDTHDazBone* Now = Mine->State.Bones.Find(DazName);
			const FDTHDazBone* Rest = Own.RomFrames[1].Bones.Find(DazName);
			const FDTHDazBone* Lib = Pose.State.Bones.Find(DazName);
			const int32 P = Played.Find(Bone);
			if (!Now || !Rest || !Lib || !CarrierNow || !CarrierRest || P == INDEX_NONE) continue;
			const FQuat CarrierDelta = Rig.Axes.Rotation(CarrierNow->Rot) * Rig.Axes.Rotation(CarrierRest->Rot).Inverse();
			const FVector DazPos = Rig.Axes.Position(CarrierNow->Pos)
				+ CarrierDelta.RotateVector(Rig.MeshRest[I].GetTranslation() - Rig.Axes.Position(CarrierRest->Pos));
			const FQuat DazRot = Rig.Axes.Rotation(Now->Rot) * Rig.Axes.Rotation(Rest->Rot).Inverse() * Rig.MeshRest[I].GetRotation();
			const double Cm = FVector::Dist(Played.Component[P].GetTranslation(), DazPos);
			const double Deg = DTHPoseTest::AngleDeg(Played.Component[P].GetRotation(), DazRot);
			const double LibDeg = DTHPoseTest::AngleDeg(Rig.Axes.Rotation(Lib->Rot) * Rig.Axes.Rotation(Rig.DazRest.Bones[DazName].Rot).Inverse(),
				Rig.Axes.Rotation(Now->Rot) * Rig.Axes.Rotation(Rest->Rot).Inverse());
			Lines.Add(FString::Printf(TEXT("%s\t%s\t%s\t%.4f\t%.4f\t%.4f"), *Pose.Label, *Bone.ToString(), *DazName.ToString(), Cm, Deg, LibDeg));
			if (Cm > MaxCm) { MaxCm = Cm; CmBone = Bone; }
			if (Bone.ToString().Contains(TEXT("twist"))) { if (Deg > MaxTwistDeg) { MaxTwistDeg = Deg; TwistBone = Bone; } }
			else if (Deg > MaxDeg) { MaxDeg = Deg; DegBone = Bone; }
			if (LibDeg > MaxLibDeg) { MaxLibDeg = LibDeg; LibBone = Bone; }
		}
		const FDTHDazBone* HipNow = Mine->State.Bones.Find(TEXT("hip"));
		const FDTHDazBone* HipRest = Own.RomFrames[1].Bones.Find(TEXT("hip"));
		const double Drop = HipNow && HipRest ? (Rig.Axes.Position(HipNow->Pos) - Rig.Axes.Position(HipRest->Pos)).Z : 0.0;
		const double PelvisCm = PelvisIndex != INDEX_NONE && Played.Find(TEXT("pelvis")) != INDEX_NONE
			? (Played.Component[Played.Find(TEXT("pelvis"))].GetTranslation() - Rig.MeshRest[PelvisIndex].GetTranslation()).Z : 0.0;
		AddInfo(FString::Printf(TEXT("%s: joints max %.4f cm (%s); bones max %.4f deg (%s); twist bones max %.4f deg (%s); library vs character rotations max %.4f deg (%s); hip height change Daz %.3f cm, UE pelvis %.3f cm."),
			*Pose.Label, MaxCm, *CmBone.ToString(), MaxDeg, *DegBone.ToString(), MaxTwistDeg, *TwistBone.ToString(), MaxLibDeg,
			*LibBone.ToString(), Drop, PelvisCm));
		if (MaxCm > 0.5 || MaxDeg > 0.5)
		{
			AddError(FString::Printf(TEXT("%s is not recreated (%.3f cm, %.3f deg)."), *Pose.Label, MaxCm, MaxDeg));
		}
	}
	AddInfo(FString::Printf(TEXT("Report: %s"), *DTHPoseTest::WriteReport(TEXT("preset_poses.tsv"), Lines)));
	return true;
}

// Plan section 8, the look: the Phase 0 presets on the character through DTH's own deformer and materials, from the
// harness's preview scene (the level is never touched). Whole body (front, side, three-quarter), the face (front and
// three-quarter: skin, eyes, lids, teeth) and both hands. Written to Saved/DTHPoseWizard/phase0/screens.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHPoseP0PresetScreenshots, "DTH.Pose.P0.PresetScreenshots", Flags)
bool FDTHPoseP0PresetScreenshots::RunTest(const FString&)
{
	FDTHPoseTestContext C;
	if (!C.Load(*this)) return false;
	FDTHPoseSamples Samples;
	if (!LoadSamples(*this, Samples)) return false;
	const DTHPoseSolve::FRig Rig = MakeRig(*this, *C.Mesh, Samples);
	const FReferenceSkeleton& Ref = C.Mesh->GetRefSkeleton();
	const TArray<DTHRetarget::FBone> Retarget = DTHRetarget::Gather(*C.Skeleton, *C.Mesh);
	FDTHBodyHarness Harness;
	FString Error;
	if (!Harness.Init(C.Mesh, Error)) { AddError(Error); return false; }
	USkeletalMeshComponent* Body = Harness.GetComponent();
	const FString Dir = UDTHPoseSettings::SavedDir() / TEXT("phase0") / TEXT("screens");

	auto Shoot = [&](const FString& Pose)
	{
		Body->UpdateBounds();
		const FVector Center = Body->Bounds.Origin;
		// The whole body in a 30 deg view: the bounding sphere at R / sin(15 deg).
		const double Distance = Body->Bounds.SphereRadius * 3.9;
		auto Socket = [Body](const TCHAR* Bone) { return Body->GetSocketLocation(Bone); };
		// DTH characters face +Y on the UE5 Mannequin skeleton; +X is the character's left (Daz +X, DTH.Pose.P0.DazMapping).
		// The face cameras follow the head: its rotation since the bind pose turns the bind-pose view directions.
		const int32 Head = Ref.FindBoneIndex(TEXT("head"));
		const FQuat HeadTurn = Body->GetBoneQuaternion(TEXT("head"), EBoneSpaces::ComponentSpace) * Rig.MeshRest[Head].GetRotation().Inverse();
		const FVector Face = Socket(TEXT("head")) + HeadTurn.RotateVector(FVector(0.0, 2.0, 5.0));
		const FVector Ahead = HeadTurn.RotateVector(FVector(0.0, 1.0, 0.0));
		const FVector Aside = HeadTurn.RotateVector(FVector(1.0, 0.0, 0.0));
		const double Q = FMath::DegreesToRadians(45.0);
		struct FView { const TCHAR* Name; FVector Eye, Target; float Fov; int32 Size; };
		const FView Views[] = {
			{TEXT("body_front"), Center + FVector(0.0, Distance, 0.0), Center, 30.0f, 1024},
			{TEXT("body_left"), Center + FVector(Distance, 0.0, 0.0), Center, 30.0f, 1024},
			{TEXT("body_threequarter"), Center + FVector(Distance * FMath::Sin(Q), Distance * FMath::Cos(Q), 0.0), Center, 30.0f, 1024},
			{TEXT("face_front"), Face + Ahead * 60.0, Face, 30.0f, 768},
			{TEXT("face_threequarter"), Face + (Ahead * FMath::Cos(Q) + Aside * FMath::Sin(Q)) * 60.0, Face, 30.0f, 768},
			{TEXT("hand_left"), Socket(TEXT("hand_l")) + FVector(15.0, 40.0, 10.0), Socket(TEXT("hand_l")), 30.0f, 768},
			{TEXT("hand_right"), Socket(TEXT("hand_r")) + FVector(-15.0, 40.0, 10.0), Socket(TEXT("hand_r")), 30.0f, 768},
		};
		for (const FView& V : Views)
		{
			const FString File = Dir / (Pose + TEXT("_") + V.Name + TEXT(".png"));
			if (!Harness.Capture(File, V.Eye, V.Target, V.Fov, V.Size, V.Size, Error)) AddError(Error);
		}
	};

	FDTHBoneReadback Out;
	Harness.EvalPose({}, {}, Out);
	Shoot(TEXT("bind"));
	for (const FDTHDazPose& Pose : Samples.Poses)
	{
		if (!Pose.bLoaded) continue;
		const TMap<FName, FTransform> Wanted = DTHPoseSolve::Solve(Rig, Ref, Pose.State);
		TMap<FName, FTransform> Track;
		for (int32 I = 0; I < Ref.GetNum(); ++I)
		{
			Track.Add(Ref.GetBoneName(I), DTHRetarget::Inverse(Retarget[I], Wanted.FindChecked(Ref.GetBoneName(I))));
		}
		const FString Name = TEXT("A_Preset_") + FPaths::MakeValidFileName(Pose.Label).Replace(TEXT(" "), TEXT("_"));
		const FString Folder = C.TestFolder / TEXT("Presets");
		DTHPoseTest::DeleteIfExists(Folder / Name);
		UAnimSequence* Sequence = DTHPoseWriter::Create(Folder, Name, C.Skeleton, {Track}, Error);
		if (!Sequence) { AddError(Error); return false; }
		Harness.EvalSequence(Sequence, 0.0, Out);
		Shoot(Name.RightChop(9));
	}
	AddInfo(FString::Printf(TEXT("Screens: %s"), *Dir));
	return true;
}

#endif
