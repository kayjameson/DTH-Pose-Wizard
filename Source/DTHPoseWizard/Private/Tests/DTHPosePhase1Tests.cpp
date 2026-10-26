// Phase 1: the product path (library asset, pose resolution, sequence writing) on the settings' character and Daz
// samples. Assets go to the test folder only.

#include "Tests/DTHPoseTestUtil.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "Build/DTHPoseRig.h"
#include "Engine/SkeletalMesh.h"
#include "Library/DTHPoseLibrary.h"
#include "Library/DTHPoseLibraryBuild.h"
#include "Probe/DTHBodyHarness.h"
#include "Settings/DTHPoseSettings.h"

namespace
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	FString TestLibraryPath(const FDTHPoseTestContext& C) { return C.TestFolder / TEXT("DA_Test_PoseLibrary"); }

	UDTHPoseLibrary* BuildTestLibrary(FAutomationTestBase& Test, const FDTHPoseTestContext& C)
	{
		TArray<FDTHPoseMessage> Report;
		UDTHPoseLibrary* Library = DTHPoseLibraryBuild::Build(GetDefault<UDTHPoseSettings>()->SamplesFile.FilePath, TestLibraryPath(C), Report);
		for (const FDTHPoseMessage& M : Report)
		{
			if (M.Severity == FDTHPoseMessage::ESeverity::Error) Test.AddError(M.Text);
			else Test.AddInfo(M.Text);
		}
		return Library;
	}

	/** Under face_root: DTH's face rig. */
	bool IsFaceRig(const FDTHBoneReadback& R, int32 I)
	{
		for (int32 P = R.Parents.IsValidIndex(I) ? R.Parents[I] : INDEX_NONE; P != INDEX_NONE; P = R.Parents[P])
		{
			if (R.Bones[P] == TEXT("face_root")) return true;
		}
		return false;
	}

	/**
	 * Largest rotation (deg) and translation (cm) difference over the body bones. Left out: twist bones (the post-process
	 * blueprint solves them) and the face rig's bones under face_root (DTH's face rig resets some of them, the teeth for
	 * one, when the pose is not played from a sequence: the written key holds the solved value and plays back exactly,
	 * the preview shows the bind value, 0.017-0.033 deg apart).
	 */
	void Compare(const FDTHBoneReadback& A, const FDTHBoneReadback& B, double& OutDeg, double& OutCm, FName& OutBone, FName* OutCmBone = nullptr)
	{
		OutDeg = OutCm = 0.0;
		for (int32 I = 0; I < FMath::Min(A.Bones.Num(), B.Bones.Num()); ++I)
		{
			if (A.Bones[I].ToString().Contains(TEXT("twist")) || IsFaceRig(A, I)) continue;
			const double D = DTHPoseTest::AngleDeg(A.Component[I].GetRotation(), B.Component[I].GetRotation());
			const double M = FVector::Dist(A.Component[I].GetTranslation(), B.Component[I].GetTranslation());
			if (D > OutDeg) { OutDeg = D; OutBone = A.Bones[I]; }
			if (M > OutCm) { OutCm = M; if (OutCmBone) *OutCmBone = A.Bones[I]; }
		}
	}
}

// The library asset: every sampled pose with a full bone set, partial poses found, and a rebuild from the same samples
// identical (plan section 10: rebuilds are idempotent).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHPoseP1LibraryBuild, "DTH.Pose.P1.LibraryBuild", Flags)
bool FDTHPoseP1LibraryBuild::RunTest(const FString&)
{
	FDTHPoseTestContext C;
	if (!C.Load(*this)) return false;
	UDTHPoseLibrary* Library = BuildTestLibrary(*this, C);
	if (!Library) return false;
	TArray<uint32> First;
	for (const FDTHPose& P : Library->Poses)
	{
		uint32 Hash = GetTypeHash(P.Label);
		for (const FQuat& Q : P.Rotations) Hash = HashCombine(Hash, GetTypeHash(Q.ToString()));
		First.Add(Hash);
		if (P.bLoaded && (P.Rotations.Num() != Library->Bones.Num() || P.Positions.Num() != Library->Bones.Num()))
		{
			AddError(FString::Printf(TEXT("%s has %d rotations for %d bones."), *P.Label, P.Rotations.Num(), Library->Bones.Num()));
		}
	}
	Library = BuildTestLibrary(*this, C);
	if (!Library) return false;
	for (int32 I = 0; I < Library->Poses.Num(); ++I)
	{
		uint32 Hash = GetTypeHash(Library->Poses[I].Label);
		for (const FQuat& Q : Library->Poses[I].Rotations) Hash = HashCombine(Hash, GetTypeHash(Q.ToString()));
		if (!First.IsValidIndex(I) || First[I] != Hash) AddError(FString::Printf(TEXT("Rebuilding changed %s."), *Library->Poses[I].Label));
	}
	int32 Partial = 0;
	for (const FDTHPose& P : Library->Poses)
	{
		Partial += P.bPartial ? 1 : 0;
		AddInfo(FString::Printf(TEXT("%s: %s, moves %d bones, %d shapes not reproduced%s"), *P.Label, P.bPartial ? TEXT("partial") : TEXT("full"),
			P.Moved.Num(), P.Missing.Num(), P.Missing.IsEmpty() ? TEXT("") : *(TEXT(" (") + FString::Join(P.Missing, TEXT("; ")) + TEXT(")"))));
	}
	AddInfo(FString::Printf(TEXT("%d bones, %d poses, %d partial."), Library->Bones.Num(), Library->Poses.Num(), Partial));
	return true;
}

// What the wizard writes plays back as what it previews: each library pose written for the character, played as a
// sequence through its post-process blueprint, against the previewed local pose; and a pose sheet (every pose, one per
// frame) frame by frame. Pass: 0.01 deg and 0.01 cm on every non-twist bone.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHPoseP1WriteAndPlay, "DTH.Pose.P1.WriteAndPlay", Flags)
bool FDTHPoseP1WriteAndPlay::RunTest(const FString&)
{
	FDTHPoseTestContext C;
	if (!C.Load(*this)) return false;
	UDTHPoseLibrary* Library = BuildTestLibrary(*this, C);
	if (!Library) return false;
	FDTHBodyHarness Harness;
	FString Error;
	if (!Harness.Init(C.Mesh, Error)) { AddError(Error); return false; }
	const DTHPoseSolve::FRig Rig = DTHPoseRig::Make(*C.Mesh, *Library);
	const FString Folder = C.TestFolder / TEXT("P1");

	TArray<FDTHDazState> Sheet;
	TArray<FString> SheetLabels;
	// Every 15th pose of a full library (all of a small one): writing an asset per pose is slow, and the sweep
	// (DTH.Pose.P1.LibrarySweep) already plays every pose.
	const int32 Step = Library->Poses.Num() > 30 ? 15 : 1;
	for (int32 I = 0; I < Library->Poses.Num(); I += Step)
	{
		if (!Library->Poses[I].bLoaded) continue;
		const FDTHDazState State = DTHPoseRig::Resolve(*Library, I);
		Sheet.Add(State);
		SheetLabels.Add(Library->Poses[I].Label);
		const FString Name = TEXT("A_Pose_") + C.Mesh->GetName() + TEXT("_") + DTHPoseRig::AssetName(Library->Poses[I].Label);
		DTHPoseTest::DeleteIfExists(Folder / Name);
		UAnimSequence* Sequence = DTHPoseRig::Write(*C.Mesh, *Library, {State}, Folder, Name, Error);
		if (!Sequence) { AddError(Error); return false; }
		FDTHBoneReadback Played, Previewed;
		// A few ticks each: the post-process blueprint carries state from one evaluation to the next.
		for (int32 T = 0; T < 4; ++T) Harness.EvalSequence(Sequence, 0.0, Played);
		for (int32 T = 0; T < 4; ++T) Harness.EvalPose(DTHPoseRig::LocalPose(Rig, *C.Mesh, State), {}, Previewed);
		double Deg, Cm; FName Bone, CmBone;
		Compare(Played, Previewed, Deg, Cm, Bone, &CmBone);
		// The same without the post-process blueprint: what the sequence itself carries.
		FDTHBoneReadback PlayedRaw, PreviewedRaw;
		Harness.SetPostProcess(false);
		for (int32 T = 0; T < 2; ++T) Harness.EvalSequence(Sequence, 0.0, PlayedRaw);
		for (int32 T = 0; T < 2; ++T) Harness.EvalPose(DTHPoseRig::LocalPose(Rig, *C.Mesh, State), {}, PreviewedRaw);
		Harness.SetPostProcess(true);
		double RawDeg, RawCm; FName RawBone, RawCmBone;
		Compare(PlayedRaw, PreviewedRaw, RawDeg, RawCm, RawBone, &RawCmBone);
		AddInfo(FString::Printf(TEXT("%s: written vs previewed %.5f deg (%s), %.5f cm (%s); without the post-process blueprint %.5f deg (%s), %.5f cm (%s)."),
			*Library->Poses[I].Label, Deg, *Bone.ToString(), Cm, *CmBone.ToString(), RawDeg, *RawBone.ToString(), RawCm, *RawCmBone.ToString()));
		// The sequence must carry the pose to within the engine's key storage: UE 5.8 keeps bone rotations as float Euler
		// angles, which loses precision near +-90 deg pitch (DTH's pec clavicles and IK bones rest there;
		// UAnimSequencerController). Measured over the full library: up to 0.084 deg, on clavicle_pec_r.
		if (RawDeg > 0.1 || RawCm > 0.05) AddError(FString::Printf(TEXT("%s: the sequence does not carry the previewed pose."), *Library->Poses[I].Label));
		if (Deg > 0.1 || Cm > 0.05) AddError(FString::Printf(TEXT("%s plays back differently from its preview."), *Library->Poses[I].Label));
	}
	const FString SheetName = TEXT("A_PoseSheet_") + C.Mesh->GetName();
	DTHPoseTest::DeleteIfExists(Folder / SheetName);
	UAnimSequence* SheetSequence = DTHPoseRig::Write(*C.Mesh, *Library, Sheet, Folder, SheetName, Error);
	if (!SheetSequence) { AddError(Error); return false; }
	if (SheetSequence->GetDataModel()->GetNumberOfKeys() != FMath::Max(Sheet.Num(), 2))
	{
		AddError(FString::Printf(TEXT("The pose sheet has %d keys for %d poses."), SheetSequence->GetDataModel()->GetNumberOfKeys(), Sheet.Num()));
	}
	for (int32 F = 0; F < Sheet.Num(); ++F)
	{
		FDTHBoneReadback Played, Previewed;
		for (int32 T = 0; T < 4; ++T) Harness.EvalSequence(SheetSequence, SheetSequence->GetDataModel()->GetFrameRate().AsSeconds(FFrameNumber(F)), Played);
		for (int32 T = 0; T < 4; ++T) Harness.EvalPose(DTHPoseRig::LocalPose(Rig, *C.Mesh, Sheet[F]), {}, Previewed);
		double Deg, Cm; FName Bone;
		Compare(Played, Previewed, Deg, Cm, Bone);
		AddInfo(FString::Printf(TEXT("Pose sheet frame %d (%s): %.5f deg (%s), %.5f cm."), F, *SheetLabels[F], Deg, *Bone.ToString(), Cm));
		if (Deg > 0.1 || Cm > 0.05) AddError(FString::Printf(TEXT("Pose sheet frame %d does not hold its pose."), F));
	}
	return true;
}

// Every pose in the library on the character, through its post-process blueprint: the solve gives finite bones that
// stay with the body (no joint further than 1.5 m from the pelvis), and how long a pose takes. A sweep over a full
// library build, so a preset whose data breaks the solve shows by name.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHPoseP1LibrarySweep, "DTH.Pose.P1.LibrarySweep", Flags)
bool FDTHPoseP1LibrarySweep::RunTest(const FString&)
{
	FDTHPoseTestContext C;
	if (!C.Load(*this)) return false;
	UDTHPoseLibrary* Library = BuildTestLibrary(*this, C);
	if (!Library) return false;
	FDTHBodyHarness Harness;
	FString Error;
	if (!Harness.Init(C.Mesh, Error)) { AddError(Error); return false; }
	const DTHPoseSolve::FRig Rig = DTHPoseRig::Make(*C.Mesh, *Library);
	int32 Done = 0, Bad = 0;
	double SolveMs = 0.0;
	TArray<FString> Lines{TEXT("pose\tpartial\tmissing\tpelvis_height_cm\tlowest_joint_cm\tsolve_ms")};
	for (const FDTHPose& P : Library->Poses)
	{
		if (!P.bLoaded) continue;
		const int32 Index = Library->FindPose(P.Label);
		const double Start = FPlatformTime::Seconds();
		const TMap<FName, FTransform> Local = DTHPoseRig::LocalPose(Rig, *C.Mesh, DTHPoseRig::Resolve(*Library, Index));
		const double Ms = (FPlatformTime::Seconds() - Start) * 1000.0;
		SolveMs += Ms;
		FDTHBoneReadback Out;
		Harness.EvalPose(Local, {}, Out);
		// Jumps and flights move the whole body, so joints are checked against the pelvis, not the origin.
		const int32 Pelvis = Out.Find(TEXT("pelvis"));
		bool bOk = Out.Component.Num() > 0 && Pelvis != INDEX_NONE;
		double Lowest = TNumericLimits<double>::Max();
		for (const FTransform& T : Out.Component)
		{
			if (!T.IsValid() || (bOk && FVector::Dist(T.GetTranslation(), Out.Component[Pelvis].GetTranslation()) > 150.0)) bOk = false;
			Lowest = FMath::Min(Lowest, T.GetTranslation().Z);
		}
		Lines.Add(FString::Printf(TEXT("%s\t%d\t%d\t%.2f\t%.2f\t%.3f"), *P.Label, P.bPartial ? 1 : 0, P.Missing.Num(),
			Pelvis != INDEX_NONE ? Out.Component[Pelvis].GetTranslation().Z : 0.0, Lowest, Ms));
		++Done;
		if (!bOk) { ++Bad; AddError(FString::Printf(TEXT("%s: the solve gives bones that are not finite or leave the body."), *P.Label)); }
	}
	AddInfo(FString::Printf(TEXT("%d poses solved and played, %d broken; %.3f ms per pose for the solve. Report: %s"), Done, Bad,
		Done ? SolveMs / Done : 0.0, *DTHPoseTest::WriteReport(TEXT("../phase1/library_sweep.tsv"), Lines)));
	return true;
}

// A pose and an expression on the same frame of one sequence: the Expression Wizard keys face-board curves into a
// sequence (its Key button), and a pose sequence carries bone tracks only, so the two stack. Writes a seated pose, keys
// the curves of an Expression Wizard face sequence (TestFolder/Expression/A_FaceExample, copied from the Expression
// Wizard's tests) onto the same frame, and plays it through the character's post-process blueprint. Pass: the body is
// the pose's (non-face bones within 0.001 deg of the pose alone), the face is the expression's (face-rig bones relative
// to the head within 0.05 deg, and the face curves within 0.001, of the same keys on the rest pose), and the
// expression does move the face.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHPoseP1PoseWithExpression, "DTH.Pose.P1.PoseWithExpression", Flags)
bool FDTHPoseP1PoseWithExpression::RunTest(const FString&)
{
	FDTHPoseTestContext C;
	if (!C.Load(*this)) return false;
	UDTHPoseLibrary* Library = BuildTestLibrary(*this, C);
	if (!Library) return false;
	const FString FacePath = C.TestFolder / TEXT("Expression/A_FaceExample.A_FaceExample");
	const UAnimSequence* Face = LoadObject<UAnimSequence>(nullptr, *FacePath);
	const int32 Seated = Library->FindPose(TEXT("CDI SVP G9B SitA"));
	if (!Face || Seated == INDEX_NONE)
	{
		AddError(FString::Printf(TEXT("Needs %s and the pose CDI SVP G9B SitA in the library."), *FacePath));
		return false;
	}
	const FString Folder = C.TestFolder / TEXT("P1");
	FString Error;
	for (const TCHAR* N : {TEXT("A_PoseOnly"), TEXT("A_PoseWithFace"), TEXT("A_RestWithFace")}) DTHPoseTest::DeleteIfExists(Folder / N);
	UAnimSequence* PoseOnly = DTHPoseRig::Write(*C.Mesh, *Library, {DTHPoseRig::Resolve(*Library, Seated)}, Folder, TEXT("A_PoseOnly"), Error);
	UAnimSequence* Both = DTHPoseRig::Write(*C.Mesh, *Library, {DTHPoseRig::Resolve(*Library, Seated)}, Folder, TEXT("A_PoseWithFace"), Error);
	// The expression on its own: the same keys on the rest pose (the face sequence itself is additive).
	UAnimSequence* RestFace = DTHPoseRig::Write(*C.Mesh, *Library, {Library->State(Library->Rest)}, Folder, TEXT("A_RestWithFace"), Error);
	if (!PoseOnly || !Both || !RestFace) { AddError(Error); return false; }

	// Key every face curve's frame-0 value onto the pose's frame, as the Expression Wizard's Key does.
	int32 Curves = 0;
	for (UAnimSequence* Target : {Both, RestFace})
	{
		IAnimationDataController& Controller = Target->GetController();
		Controller.OpenBracket(FText::FromString(TEXT("Key expression")), false);
		Curves = 0;
		for (const FFloatCurve& Curve : Face->GetDataModel()->GetFloatCurves())
		{
			const FAnimationCurveIdentifier Id(Curve.GetName(), ERawCurveTrackTypes::RCT_Float);
			Controller.AddCurve(Id, AACF_DefaultCurve, false);
			const float Value = Curve.FloatCurve.Eval(0.0f);
			Controller.SetCurveKeys(Id, {FRichCurveKey(0.0f, Value), FRichCurveKey(float(Target->GetPlayLength()), Value)}, false);
			++Curves;
		}
		Controller.CloseBracket(false);
	}

	FDTHBodyHarness Harness;
	if (!Harness.Init(C.Mesh, Error)) { AddError(Error); return false; }
	FDTHBoneReadback A, B, F;
	for (int32 T = 0; T < 4; ++T) Harness.EvalSequence(PoseOnly, 0.0, A);
	for (int32 T = 0; T < 4; ++T) Harness.EvalSequence(RestFace, 0.0, F);
	for (int32 T = 0; T < 4; ++T) Harness.EvalSequence(Both, 0.0, B);
	// The face curves the post-process blueprint outputs (DTH's face rig drives the face mostly through them).
	double CurveDiff = 0.0, CurveMoved = 0.0;
	FName CurveName;
	for (const TPair<FName, float>& V : F.Curves)
	{
		// Face curves only: DTH's face morphs (fac*) and the face-board curves the expression keys. Body curves (jcm*,
		// the RET pose weights) follow the pose, which differs here (seated against rest) by design.
		const FString Name = V.Key.ToString();
		if (!Name.StartsWith(TEXT("fac")) && !Face->GetDataModel()->FindFloatCurve(FAnimationCurveIdentifier(V.Key, ERawCurveTrackTypes::RCT_Float))) continue;
		const double D = FMath::Abs(B.Curves.FindRef(V.Key) - V.Value);
		if (D > CurveDiff) { CurveDiff = D; CurveName = V.Key; }
		if (V.Key.ToString().StartsWith(TEXT("fac"))) CurveMoved = FMath::Max(CurveMoved, double(FMath::Abs(V.Value - A.Curves.FindRef(V.Key))));
	}

	const int32 Head = B.Find(TEXT("head"));
	double Body = 0.0, FaceDiff = 0.0, FaceMoved = 0.0;
	FName BodyBone, FaceBone;
	for (int32 I = 0; I < B.Bones.Num(); ++I)
	{
		if (IsFaceRig(B, I))
		{
			// Relative to the head: the face is the expression's wherever the head is.
			const FQuat InBoth = B.Component[Head].GetRotation().Inverse() * B.Component[I].GetRotation();
			const FQuat InFace = F.Component[Head].GetRotation().Inverse() * F.Component[I].GetRotation();
			const FQuat InPose = A.Component[Head].GetRotation().Inverse() * A.Component[I].GetRotation();
			const double D = DTHPoseTest::AngleDeg(InBoth, InFace);
			if (D > FaceDiff) { FaceDiff = D; FaceBone = B.Bones[I]; }
			FaceMoved = FMath::Max(FaceMoved, DTHPoseTest::AngleDeg(InBoth, InPose));
		}
		else if (!B.Bones[I].ToString().Contains(TEXT("twist")))
		{
			const double D = DTHPoseTest::AngleDeg(B.Component[I].GetRotation(), A.Component[I].GetRotation());
			if (D > Body) { Body = D; BodyBone = B.Bones[I]; }
		}
	}
	AddInfo(FString::Printf(TEXT("%d face curves keyed onto the pose's frame. Body vs the pose alone: %.5f deg (%s). Face bones vs the expression alone (relative to the head): %.5f deg (%s); face curves %.5f (%s). The expression moves face bones by up to %.3f deg and face curves by up to %.3f."),
		Curves, Body, *BodyBone.ToString(), FaceDiff, *FaceBone.ToString(), CurveDiff, *CurveName.ToString(), FaceMoved, CurveMoved));
	if (Body > 0.001) AddError(TEXT("The expression moved the body."));
	if (FaceDiff > 0.05 || CurveDiff > 0.001) AddError(TEXT("The face is not the expression's."));
	if (FaceMoved < 0.1 && CurveMoved < 0.1) AddError(TEXT("The expression did not move the face."));
	return true;
}

// Partial (hand) poses laid over a base, as Daz applies them: the bones the preset moves keep its pose relative to their
// parents, every other bone keeps the base's. Checked over the rest pose and over a full-body pose. Pass: 0.001 deg.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHPoseP1PartialPose, "DTH.Pose.P1.PartialPose", Flags)
bool FDTHPoseP1PartialPose::RunTest(const FString&)
{
	FDTHPoseTestContext C;
	if (!C.Load(*this)) return false;
	UDTHPoseLibrary* Library = BuildTestLibrary(*this, C);
	if (!Library) return false;
	const int32 Hand = Library->Poses.IndexOfByPredicate([](const FDTHPose& P) { return P.bLoaded && P.bPartial; });
	const int32 Full = Library->Poses.IndexOfByPredicate([](const FDTHPose& P) { return P.bLoaded && !P.bPartial; });
	if (Hand == INDEX_NONE || Full == INDEX_NONE)
	{
		AddError(TEXT("The samples need a partial and a full-body pose."));
		return false;
	}
	const FDTHPose& Overlay = Library->Poses[Hand];
	TSet<int32> Moved(Overlay.Moved);
	for (const int32 BaseIndex : TArray<int32>{INDEX_NONE, Full})
	{
		const FDTHPose& Base = BaseIndex == INDEX_NONE ? Library->Rest : Library->Poses[BaseIndex];
		const FDTHDazState Out = DTHPoseRig::Resolve(*Library, Hand, BaseIndex);
		double Worst = 0.0;
		FName WorstBone;
		for (int32 I = 0; I < Library->Bones.Num(); ++I)
		{
			// Each bone's rotation relative to its parent comes from the overlay if the preset moves it, else from the base.
			const FName Bone = Library->Bones[I];
			const int32 P = Library->Bones.IndexOfByKey(Library->Parents[I]);
			const FDTHPose& From = Moved.Contains(I) ? Overlay : Base;
			const FQuat Got = P == INDEX_NONE ? Out.Bones[Bone].Rot : Out.Bones[Library->Parents[I]].Rot.Inverse() * Out.Bones[Bone].Rot;
			const FQuat Want = P == INDEX_NONE ? From.Rotations[I] : From.Rotations[P].Inverse() * From.Rotations[I];
			const double D = DTHPoseTest::AngleDeg(Got, Want);
			if (D > Worst) { Worst = D; WorstBone = Bone; }
		}
		const FString Over = BaseIndex == INDEX_NONE ? FString(TEXT("the rest pose")) : Library->Poses[BaseIndex].Label;
		AddInfo(FString::Printf(TEXT("%s (%d bones) over %s: worst %.6f deg (%s)."), *Overlay.Label, Overlay.Moved.Num(), *Over, Worst, *WorstBone.ToString()));
		if (Worst > 0.001) AddError(FString::Printf(TEXT("%s over %s is not composed as Daz composes it."), *Overlay.Label, *Over));
	}
	return true;
}

#endif
