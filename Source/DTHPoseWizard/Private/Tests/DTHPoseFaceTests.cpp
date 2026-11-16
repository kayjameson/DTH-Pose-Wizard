// The faces of pose presets (eyes, lids, jaw, lips, tongue, brows): fitted on the character as DTH's face rig plays them,
// measured region by region against the face Daz shows, written into a pose sequence and played back.
// Inputs: <Project>/Saved/DTHPoseWizard/PoseFaceTest.json
//   samples    DTH_PoseSamples.json with faces (sampler 0.4.0: face.json, poses/geo_*.txt)
//   character  the DTH Genesis 9 skeletal mesh to fit on (a base Genesis 9 export is 1:1 with the samples)
//   count      how many face-setting presets to check, spread evenly over the library (default 40)
//   labels     optional: these presets as well
//   regionMm   pass mark: the largest error of any region of a preset, mm (default 1.0)
//   report     where to write the report (default Saved/DTHPoseWizard/PoseFaceReport.txt)
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Dom/JsonObject.h"
#include "DTHFaceFitter.h"
#include "DTHFaceRegion.h"
#include "DTHPoseSamples.h"
#include "DTHPoseCharacter.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Settings/DTHPoseSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHPoseFaceFitTest, "DTH.Pose.Face.Fit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDTHPoseFaceFitTest::RunTest(const FString& Parameters)
{
	const FString ConfigFile = UDTHPoseSettings::SavedDir() / TEXT("PoseFaceTest.json");
	FString Text;
	TSharedPtr<FJsonObject> Config;
	if (!FFileHelper::LoadFileToString(Text, *ConfigFile) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Config) || !Config)
	{
		AddError(TEXT("No test inputs: ") + ConfigFile);
		return false;
	}
	FDTHPoseSamples Samples;
	FString Error;
	if (!Samples.Load(Config->GetStringField(TEXT("samples")), Error) || !Samples.HasFace())
	{
		AddError(Error.IsEmpty() ? TEXT("The samples have no faces.") : Error);
		return false;
	}
	USkeletalMesh* Character = LoadObject<USkeletalMesh>(nullptr, *Config->GetStringField(TEXT("character")));
	FDTHFaceFitter Fitter;
	TArray<FString> Report;
	const double T0 = FPlatformTime::Seconds();
	if (!Fitter.Init(Character, Samples.Face, Report, Error))
	{
		AddError(Error);
		return false;
	}
	Report.Add(FString::Printf(TEXT("Set-up %.1f s."), FPlatformTime::Seconds() - T0));
	// The pose sequence keys the face bones itself; the curves are fitted on top (config "bones": false fits the face with curves alone).
	bool bBones = true;
	Config->TryGetBoolField(TEXT("bones"), bBones);
	const DTHPoseSolve::FRig Rig = DTHPoseCharacter::Make(*Character, Samples.RomFrames.IsValidIndex(1) ? Samples.RomFrames[1] : Samples.Rest);

	int32 Count = 40;
	Config->TryGetNumberField(TEXT("count"), Count);
	double RegionMm = 1.0;
	Config->TryGetNumberField(TEXT("regionMm"), RegionMm);
	TSet<FString> Labels;
	const TArray<TSharedPtr<FJsonValue>>* LabelArray = nullptr;
	if (Config->TryGetArrayField(TEXT("labels"), LabelArray)) for (const TSharedPtr<FJsonValue>& L : *LabelArray) Labels.Add(L->AsString());

	// The presets that set a face, then an even spread of them (and the named ones).
	TArray<FDTHDazPose> WithFace;
	int32 Total = 0;
	if (!Samples.ReadPoses([&](FDTHDazPose&& P)
		{
			++Total;
			if (P.bLoaded && !P.Face.IsEmpty()) WithFace.Add(MoveTemp(P));
			return true;
		}, Error))
	{
		AddError(Error);
		return false;
	}
	Report.Add(FString::Printf(TEXT("%d of %d presets set a face."), WithFace.Num(), Total));
	TArray<int32> Picked;
	for (int32 I = 0; I < WithFace.Num(); ++I) if (Labels.Contains(WithFace[I].Label)) Picked.Add(I);
	for (int32 K = 0; K < Count && WithFace.Num() > 0; ++K) Picked.AddUnique(FMath::Min(WithFace.Num() - 1, K * WithFace.Num() / FMath::Max(Count, 1)));

	int32 Passed = 0, Checked = 0, AtRest = 0;
	double Slowest = 0.0, SecondsSum = 0.0;
	for (const int32 I : Picked)
	{
		const FDTHDazPose& P = WithFace[I];
		FDTHFaceFitter::FResult R;
		FDTHShapeFit::FOptions Options;
		if (!bBones) for (const FName& B : FDTHFaceFitter::TurnableBones(*Character)) for (int32 A = 0; A < 3; ++A) Options.Fixed.Add(FName(*FString::Printf(TEXT("bone|%s|%d"), *B.ToString(), A)));
		if (!Fitter.Fit(P.Face, Options, R))
		{
			AddError(TEXT("no face target for ") + P.Label);
			continue;
		}
		if (R.Curves.IsEmpty() && R.Bones.IsEmpty() && R.Largest < FDTHFaceFitter::RestTolerance)
		{
			++AtRest;
			Report.Add(FString::Printf(TEXT("REST  %s (its face moves %.2f mm at most)"), *P.Label, 10.0 * R.Largest));
			continue;
		}
		double WorstMm = 0.0;
		FString WorstRegion;
		const FString Regions = Fitter.RegionReport(R, WorstMm, WorstRegion);
		const bool bPass = WorstMm <= RegionMm;
		Passed += bPass ? 1 : 0;
		++Checked;
		Slowest = FMath::Max(Slowest, R.Seconds);
		SecondsSum += R.Seconds;
		FString Turns;
		for (const TPair<FName, FQuat>& B : R.Bones) Turns += FString::Printf(TEXT(" %s %.1f deg"), *B.Key.ToString(), FMath::RadiansToDegrees(B.Value.GetAngle()));
		Report.Add(FString::Printf(TEXT("%s worst %s %.2f mm | %s / %s | %d curves, bones:%s, %.2f s |%s"), bPass ? TEXT("PASS") : TEXT("FAIL"), *WorstRegion, WorstMm,
			*FPaths::GetCleanFilename(P.Pack), *P.Label, R.Curves.Num(), Turns.IsEmpty() ? TEXT(" none") : *Turns, R.Seconds, *Regions));
	}
	Report.Add(FString::Printf(TEXT("%d of %d faces within %.1f mm in every region (%d at rest). Fit %.2f s on average, %.2f s at most."),
		Passed, Checked, RegionMm, AtRest, Checked ? SecondsSum / Checked : 0.0, Slowest));
	FString ReportFile;
	if (!Config->TryGetStringField(TEXT("report"), ReportFile)) ReportFile = UDTHPoseSettings::SavedDir() / TEXT("PoseFaceReport.txt");
	FFileHelper::SaveStringArrayToFile(Report, *ReportFile);
	for (const FString& Line : Report) AddInfo(Line);
	TestTrue(TEXT("faces checked"), Checked > 0);
	return true;
}

#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHPoseFaceRestTest, "DTH.Pose.Face.RestMatchesExpressions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** The pose sampler's rest face against the expression sampler's, vertex by vertex (inputs: PoseFaceTest.json "expressionFace"). */
bool FDTHPoseFaceRestTest::RunTest(const FString& Parameters)
{
	FString Text;
	TSharedPtr<FJsonObject> Config;
	if (!FFileHelper::LoadFileToString(Text, *(UDTHPoseSettings::SavedDir() / TEXT("PoseFaceTest.json"))) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Config) || !Config) return false;
	TArray<FDTHFaceFigure> A, B;
	double UA = 0, UB = 0;
	FString Error;
	const FString PoseFace = FPaths::GetPath(Config->GetStringField(TEXT("samples"))) / TEXT("face.json");
	if (!DTHFaceRegion::LoadFace(PoseFace, A, UA, Error) || !DTHFaceRegion::LoadFace(Config->GetStringField(TEXT("expressionFace")), B, UB, Error)) { AddError(Error); return false; }
	for (const FDTHFaceFigure& FA : A)
	{
		const FDTHFaceFigure* FB = B.FindByPredicate([&](const FDTHFaceFigure& F) { return F.Name == FA.Name; });
		if (!FB) { AddInfo(FA.Name + TEXT(": only in the pose samples")); continue; }
		TMap<int32, FVector3f> ByVertex;
		for (int32 I = 0; I < FB->Vertices.Num(); ++I) ByVertex.Add(FB->Vertices[I], FB->Rest[I]);
		double Worst = 0, Sum = 0; int32 N = 0; int32 WorstV = -1; FVector3f WA, WB;
		for (int32 I = 0; I < FA.Vertices.Num(); ++I)
		{
			if (const FVector3f* P = ByVertex.Find(FA.Vertices[I]))
			{
				const double D = FVector3f::Dist(*P, FA.Rest[I]);
				Sum += D; ++N;
				if (D > Worst) { Worst = D; WorstV = FA.Vertices[I]; WA = FA.Rest[I]; WB = *P; }
			}
		}
		AddInfo(FString::Printf(TEXT("%s: %d vs %d region vertices, %d shared; mean %.4f cm, worst %.4f cm at vertex %d (pose %s, expression %s)"),
			*FA.Name, FA.Vertices.Num(), FB->Vertices.Num(), N, N ? Sum / N : 0.0, Worst, WorstV, *WA.ToString(), *WB.ToString()));
	}
	return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "DTHBodyHarness.h"
#include "ReferenceSkeleton.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHPoseFaceBoneTracksTest, "DTH.Pose.Face.BoneTracks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Whether face bones a pose sequence keys (eyes, lids, jaw, tongue) reach the character through DTH's post-process
 * blueprint, alone and together with face curves: each bone turned 15 deg, then a face curve that moves it, then both.
 */
bool FDTHPoseFaceBoneTracksTest::RunTest(const FString& Parameters)
{
	FString Text;
	TSharedPtr<FJsonObject> Config;
	if (!FFileHelper::LoadFileToString(Text, *(UDTHPoseSettings::SavedDir() / TEXT("PoseFaceTest.json"))) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Config) || !Config) return false;
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *Config->GetStringField(TEXT("character")));
	FDTHBodyHarness Harness;
	FString Error;
	if (!Mesh || !Harness.Init(Mesh, Error)) { AddError(Error); return false; }
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	TMap<FName, FTransform> RestPose;
	for (int32 B = 0; B < Ref.GetNum(); ++B) RestPose.Add(Ref.GetBoneName(B), Ref.GetRefBonePose()[B]);
	auto Angle = [](const FQuat& A, const FQuat& B) { return FMath::RadiansToDegrees(A.AngularDistance(B)); };
	struct FCase { FName Bone; FName Curve; };
	const FString P = TEXT("CTRL_expressions_");
	const TArray<FCase> Cases = {
		{TEXT("eye_l"), FName(*(P + TEXT("eyeLookDownL")))},
		{TEXT("eyelid_upper_l"), FName(*(P + TEXT("eyeBlinkL")))},
		{TEXT("lower_jaw"), FName(*(P + TEXT("jawOpen")))},
		{TEXT("tongue_03"), FName(*(P + TEXT("tongueUp")))},
	};
	FDTHBoneReadback Base;
	Harness.EvalPose(RestPose, {}, Base);
	for (const FCase& C : Cases)
	{
		const int32 I = Base.Find(C.Bone);
		if (I == INDEX_NONE) { AddWarning(C.Bone.ToString() + TEXT(": not on the mesh")); continue; }
		TMap<FName, FTransform> Turned = RestPose;
		FTransform& T = Turned[C.Bone];
		const FQuat Delta(FVector(0, 1, 0), FMath::DegreesToRadians(15.0));
		T.SetRotation(T.GetRotation() * Delta);
		FDTHBoneReadback A, B, AB;
		Harness.EvalPose(Turned, {}, A);
		Harness.EvalPose(RestPose, {{C.Curve, 1.0f}}, B);
		Harness.EvalPose(Turned, {{C.Curve, 1.0f}}, AB);
		const FQuat R0 = Base.Local[I].GetRotation();
		const double Track = Angle(A.Local[I].GetRotation(), R0);
		const double Curve = Angle(B.Local[I].GetRotation(), R0);
		const double Both = Angle(AB.Local[I].GetRotation(), R0);
		// both added: the curve's turn on top of the track's
		const FQuat Expected = A.Local[I].GetRotation() * (R0.Inverse() * B.Local[I].GetRotation());
		AddInfo(FString::Printf(TEXT("%s: track turned 15.0 deg -> %.2f deg after DTH's post-process; %s alone -> %.2f deg; both -> %.2f deg (%.2f deg from track + curve added)"),
			*C.Bone.ToString(), Track, *C.Curve.ToString(), Curve, Both, Angle(AB.Local[I].GetRotation(), Expected)));
	}
	return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "DTHFaceBones.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHPoseFaceShotsTest, "DTH.Pose.Face.Shots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Unreal side of the Daz/Unreal pose face shots (tools/daz/dth_face_shots.dsa renders the Daz side, the preset's face
 * on the body at rest): each face fitted, played through the character's post-process blueprint off screen and captured
 * from the Daz shot's camera. Inputs: PoseFaceTest.json "shots" [{name, label}], "out", "width", "height".
 */
bool FDTHPoseFaceShotsTest::RunTest(const FString& Parameters)
{
	FString Text;
	TSharedPtr<FJsonObject> Config;
	if (!FFileHelper::LoadFileToString(Text, *(UDTHPoseSettings::SavedDir() / TEXT("PoseFaceTest.json"))) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Config) || !Config) return false;
	FDTHPoseSamples Samples;
	FString Error;
	if (!Samples.Load(Config->GetStringField(TEXT("samples")), Error) || !Samples.HasFace()) { AddError(Error); return false; }
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *Config->GetStringField(TEXT("character")));
	FDTHFaceFitter Fitter;
	TArray<FString> Report;
	if (!Mesh || !Fitter.Init(Mesh, Samples.Face, Report, Error)) { AddError(Error); return false; }
	FDTHBodyHarness Harness;
	if (!Harness.Init(Mesh, Error)) { AddError(Error); return false; }
	Harness.SetLightDirection(FRotator(-30.0, -100.0, 0.0));
	const FString Out = Config->GetStringField(TEXT("out"));
	// The eyes' wet layers (moisture, tear) are translucent shells over the iris that the off-screen capture draws opaque:
	// hidden for the shots, so the gaze shows (the fit never uses them).
	if (USkeletalMeshComponent* C = Harness.GetComponent())
	{
		const FSkeletalMeshRenderData* RD = C->GetSkeletalMeshAsset()->GetResourceForRendering();
		for (int32 S = 0; RD && S < RD->LODRenderData[0].RenderSections.Num(); ++S)
		{
			const int32 M = RD->LODRenderData[0].RenderSections[S].MaterialIndex;
			const FString Slot = C->GetSkeletalMeshAsset()->GetMaterials().IsValidIndex(M) ? C->GetSkeletalMeshAsset()->GetMaterials()[M].MaterialSlotName.ToString() : FString();
			if (Slot.Contains(TEXT("Moisture")) || Slot.Contains(TEXT("Tear"))) C->ShowMaterialSection(M, S, false, 0);
		}
	}

	int32 Width = 768, Height = 768;
	Config->TryGetNumberField(TEXT("width"), Width);
	Config->TryGetNumberField(TEXT("height"), Height);
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	TMap<FName, FTransform> RestPose;
	for (int32 B = 0; B < Ref.GetNum(); ++B) RestPose.Add(Ref.GetBoneName(B), Ref.GetRefBonePose()[B]);
	for (const TSharedPtr<FJsonValue>& V : Config->GetArrayField(TEXT("shots")))
	{
		const FString Name = V->AsObject()->GetStringField(TEXT("name")), Label = V->AsObject()->GetStringField(TEXT("label"));
		TArray<TArray<FVector3f>> Face;
		Samples.ReadPoses([&](FDTHDazPose&& P) { if (P.Label == Label) { Face = MoveTemp(P.Face); return false; } return true; }, Error);
		FDTHFaceFitter::FResult R;
		if (Face.IsEmpty() || !Fitter.Fit(Face, FDTHShapeFit::FOptions(), R)) { AddError(Name + TEXT(": no face")); continue; }
		double WorstMm = 0.0;
		FString WorstRegion;
		const FString Regions = Fitter.RegionReport(R, WorstMm, WorstRegion);
		TMap<FName, FTransform> Pose = RestPose;
		for (const TPair<FName, FTransform>& B : DTHFaceBones::Local(Ref, R.Bones)) Pose.Add(B.Key, B.Value);
		FDTHBoneReadback Read;
		Harness.EvalPose(Pose, R.Curves, Read);
		const int32 Head = Read.Find(TEXT("head"));
		const FVector Target = (Head != INDEX_NONE ? Read.Component[Head].GetLocation() : FVector(0, 0, 160)) + FVector(0.0, 0.0, 6.0);
		if (!Harness.Capture(Out / Name + TEXT("_ue.png"), Target + FVector(0.0, 45.0, 0.0), Target, float(FMath::RadiansToDegrees(2.0 * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(15.0)) * Width / FMath::Max(Height, 1)))), Width, Height, Error)) { AddError(Error); continue; }
		Report.Add(FString::Printf(TEXT("%s (pose %s): worst %s %.2f mm |%s"), *Name, *Label, *WorstRegion, WorstMm, *Regions));
	}
	FFileHelper::SaveStringArrayToFile(Report, *(Out / TEXT("ue_shots_report_poses.txt")));
	for (const FString& L : Report) AddInfo(L);
	return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "Library/DTHPoseLibrary.h"
#include "Library/DTHPoseLibraryBuild.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHPoseFaceBuildLibraryTest, "DTH.Pose.Face.BuildLibrary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Builds the pose library with its faces (PoseFaceTest.json: samples, character, libraryAsset), as the Build page does. */
bool FDTHPoseFaceBuildLibraryTest::RunTest(const FString& Parameters)
{
	FString Text;
	TSharedPtr<FJsonObject> Config;
	if (!FFileHelper::LoadFileToString(Text, *(UDTHPoseSettings::SavedDir() / TEXT("PoseFaceTest.json"))) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Config) || !Config) return false;
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *Config->GetStringField(TEXT("character")));
	TArray<FDTHPoseMessage> Messages;
	const double T0 = FPlatformTime::Seconds();
	UDTHPoseLibrary* Library = DTHPoseLibraryBuild::Build(Config->GetStringField(TEXT("samples")), Config->GetStringField(TEXT("libraryAsset")), Mesh, Messages);
	TArray<FString> Lines;
	for (const FDTHPoseMessage& M : Messages) Lines.Add(M.Text);
	Lines.Add(FString::Printf(TEXT("Built in %.0f s."), FPlatformTime::Seconds() - T0));
	if (Library) Lines.Add(FString::Printf(TEXT("%d poses, %d with a face."), Library->NumLoaded, Library->NumWithFace));
	FFileHelper::SaveStringArrayToFile(Lines, *(UDTHPoseSettings::SavedDir() / TEXT("PoseLibraryBuildReport.txt")));
	for (const FString& L : Lines.Num() > 30 ? TArray<FString>(Lines.GetData(), 30) : Lines) AddInfo(L);
	return TestNotNull(TEXT("library built"), Library) && TestTrue(TEXT("faces in the library"), Library->NumWithFace > 0);
}
#endif
